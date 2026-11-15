#include "app.h"
#include "ui.h"
#include <commctrl.h>
#include <wtsapi32.h>
#include <powrprof.h>
#include <sddl.h>
#include <algorithm>
#include <sstream>

namespace breakly {
App::App(std::filesystem::path root,bool bg,bool diag):store(std::move(root)),background_(bg),diagnostics_(diag) {
    QueryPerformanceCounter(&startup_);
    isolated_=store.root()!=dataDirectory();
}
App::~App() {notification.reset();main.reset();if(icon)DestroyIcon(icon);}
void App::tray(bool add) {
    tray_.cbSize=sizeof(tray_);tray_.hWnd=host;tray_.uID=1;
    tray_.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray_.uCallbackMessage=trayMessage;tray_.hIcon=icon;
    std::wstring tip=L"Breakly · "+statusText();if(!add && trayReady_ && tip==lastTip_)return;lastTip_=tip;
    wcsncpy_s(tray_.szTip,tip.c_str(),_TRUNCATE);
    bool adding=add||!trayReady_;
    trayReady_=Shell_NotifyIconW(adding?NIM_ADD:NIM_MODIFY,&tray_)!=FALSE;
    if(trayReady_ && adding){tray_.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray_);}
    if(trayReady_ && diagnostics_ && !startupWritten_) {
        LARGE_INTEGER end{},freq{};QueryPerformanceCounter(&end);QueryPerformanceFrequency(&freq);
        std::ostringstream report;report<<"{\"trayReadyMs\":"<<1000.0*(end.QuadPart-startup_.QuadPart)/freq.QuadPart<<",\"pid\":"<<GetCurrentProcessId()<<"}";
        startupWritten_=store.write(L"startup.json",report.str());
    }
}
void App::flush() {
    if(scheduler.dirty) {
        if(store.saveStatistics(scheduler.days))scheduler.dirty=false;
        else if(main)main->refresh();
    }
    nextFlush_=static_cast<Ms>(GetTickCount64())+5*minute;
}
void App::plan() {
    Ms now=static_cast<Ms>(GetTickCount64());Ms delay=scheduler.nextDelay();
    delay=std::min(delay,std::max<Ms>(100,nextFlush_-now));
    if(deferUntil_>now)delay=std::min(delay,deferUntil_-now);
    if(scheduler.pending && scheduler.settings.notificationMode!=1) {
        Ms life=(scheduler.settings.notificationMode==0?scheduler.settings.gentleSec:scheduler.settings.repeatSec)*second;
        delay=std::min(delay,std::max<Ms>(100,shownAt_+life-now));
    }
    KillTimer(host,1);SetTimer(host,1,static_cast<UINT>(std::clamp<Ms>(delay,100,USER_TIMER_MAXIMUM)),nullptr);
}
void App::update() {
    scheduler.tick(clock.sample());Ms now=static_cast<Ms>(GetTickCount64());
    if(notification && !scheduler.pending && !scheduler.active)closeNotification();
    if(scheduler.state==State::Locked || scheduler.state==State::Suspended || scheduler.state==State::Paused) {
        if(notification)ShowWindow(notification->hwnd,SW_HIDE);
    } else if(notification && !IsWindowVisible(notification->hwnd)) {
        ShowWindow(notification->hwnd,SW_SHOWNOACTIVATE);
    }
    if(scheduler.pending && scheduler.settings.notificationMode!=1) {
        Ms life=(scheduler.settings.notificationMode==0?scheduler.settings.gentleSec:scheduler.settings.repeatSec)*second;
        if(now-shownAt_>=life) {
            scheduler.snooze(scheduler.settings.repeatSec*second);
            closeNotification();
        }
    }
    if(!scheduler.active && now>=deferUntil_) {
        if(auto due=scheduler.due()) {
            if(!scheduler.pending || int(*due)>int(*scheduler.pending)) {
            bool full=BreakPolicyEngine::fullscreen();
            if(full && scheduler.settings.fullscreen!=0) {
                // Mode 1 backs off for 60 s; mode 2 samples for exit at the normal 15 s cadence.
                deferUntil_=now+(scheduler.settings.fullscreen==1?60:15)*second;
            } else if(scheduler.propose(*due))show(*due);
            }
        }
    }
    if(now>=nextFlush_)flush();
    if(main)main->refresh();if(notification)notification->refresh();tray(false);plan();
}
std::wstring App::statusText() const {
    switch(scheduler.state) {
    case State::Locked:return L"Сеанс заблокирован";
    case State::Suspended:return L"Учёт приостановлен";
    case State::Paused:return L"Напоминания на паузе";
    case State::Idle:return L"Нет активности";
    case State::BreakActive:return scheduler.breakRemaining()>0?L"Идёт перерыв":L"Подтвердите завершение";
    case State::BreakDue:return L"Пора отдохнуть";
    case State::Snoozed:return L"Напоминание отложено";
    default:return L"Время для работы";
    }
}
void App::show(Kind k) {
    shownAt_=static_cast<Ms>(GetTickCount64());
    if(scheduler.settings.nativeNotifications && !scheduler.active) {
        NOTIFYICONDATAW n{};n.cbSize=sizeof(n);n.hWnd=host;n.uID=1;n.uFlags=NIF_INFO;
        wcsncpy_s(n.szInfoTitle,kindText(k).c_str(),_TRUNCATE);
        wcsncpy_s(n.szInfo,L"Нажмите, чтобы открыть действия Breakly. Доставка зависит от настроек Windows.",_TRUNCATE);
        n.dwInfoFlags=NIIF_INFO|(scheduler.settings.sound?0:NIIF_NOSOUND);Shell_NotifyIconW(NIM_MODIFY,&n);
    } else {
        notification=std::make_unique<View>(*this,true);
        ShowWindow(notification->hwnd,SW_SHOWNOACTIVATE);
    }
    if(scheduler.settings.sound && !scheduler.settings.nativeNotifications)MessageBeep(MB_OK);
}
void App::openMain(int page) {
    if(!main)main=std::make_unique<View>(*this,false,page);else main->setPage(page);
    main->activate();
}
void App::closeMain() {main.reset();}
void App::closeNotification(bool dismiss) {if(dismiss){if(scheduler.active)scheduler.skip();else scheduler.snooze(scheduler.settings.repeatSec*second);}notification.reset();}
void App::act(int id) {
    scheduler.tick(clock.sample());
    switch(id) {
    case ShowMain:openMain();break;
    case ShowSettings:openMain(1);break;
    case CloseMain:closeMain();break;
    case TestNotify:if(!scheduler.active && !scheduler.pending){scheduler.propose(Kind::Eyes);show(Kind::Eyes);}break;
    case CloseNotification:closeNotification(true);break;
    case StartNow:closeNotification(true);scheduler.start(Kind::Long);show(Kind::Long);break;
    case Pause15:scheduler.pause(15*minute);if(!scheduler.active)closeNotification();break;
    case Pause30:scheduler.pause(30*minute);if(!scheduler.active)closeNotification();break;
    case Pause60:scheduler.pause(60*minute);if(!scheduler.active)closeNotification();break;
    case Resume:scheduler.resume();break;
    case 201: {bool keyboard=notification && GetForegroundWindow()==notification->hwnd;auto k=scheduler.pending.value_or(Kind::Long);closeNotification();scheduler.start(k);show(k);if(keyboard && notification)notification->activate();break;}
    case 202:scheduler.snooze();closeNotification();break;
    case 203:scheduler.skip();closeNotification();break;
    case 204:if(scheduler.complete())closeNotification();break;
    case Exit:quitting_=true;DestroyWindow(host);return;
    }
    update();
}
bool App::saveSettings(const Settings& s) {
    if(!s.valid())return false;
    if(!store.saveSettings(s))return false;
    scheduler.tick(clock.sample());scheduler.reconfigure(s);
    if(notification){auto k=scheduler.active?scheduler.active:scheduler.pending;notification.reset();if(k)show(*k);}
    return true;
}
void App::menu() {
    HMENU m=CreatePopupMenu();
    Ms remaining=std::min({scheduler.remaining(Kind::Eyes),scheduler.remaining(Kind::Stretch),scheduler.remaining(Kind::Long)});
    std::wstring info=L"Следующий перерыв: "+durationText(remaining);
    AppendMenuW(m,MF_STRING|MF_GRAYED,0,info.c_str());AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    for(auto [id,text]:{std::pair{StartNow,L"Сделать перерыв сейчас"},{Pause15,L"Пауза на 15 минут"},{Pause30,L"Пауза на 30 минут"},{Pause60,L"Пауза на 1 час"},{Resume,L"Возобновить напоминания"},{ShowMain,L"Открыть Breakly"},{ShowSettings,L"Настройки"},{Exit,L"Выход"}})AppendMenuW(m,MF_STRING,id,text);
    POINT p{};GetCursorPos(&p);SetForegroundWindow(host);
    int id=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,host,nullptr);DestroyMenu(m);
    PostMessageW(host,WM_NULL,0,0);if(id)act(id);
}
LRESULT CALLBACK App::proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    App* a=reinterpret_cast<App*>(GetWindowLongPtrW(w,GWLP_USERDATA));
    if(msg==WM_NCCREATE){a=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);a->host=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(a));}
    if(!a)return DefWindowProcW(w,msg,wp,lp);
    if(msg==a->taskbarCreated_){a->tray(true);return 0;}
    switch(msg) {
    case WM_TIMER:KillTimer(w,1);a->update();return 0;
    case openMessage:a->openMain();return 0;
    case WM_COMMAND:a->act(LOWORD(wp));return 0;
    case trayMessage:
        switch(LOWORD(lp)) {
        case NIN_SELECT:case NIN_KEYSELECT:a->openMain();break;
        case WM_CONTEXTMENU:a->menu();break;
        case NIN_BALLOONUSERCLICK:if(a->scheduler.pending){a->notification=std::make_unique<View>(*a,true);a->notification->activate();}break;
        }return 0;
    case WM_WTSSESSION_CHANGE:
        if(wp==WTS_SESSION_LOCK){a->scheduler.tick(a->clock.sample());a->clock.locked=true;}
        if(wp==WTS_SESSION_UNLOCK)a->clock.locked=false;
        a->update();return 0;
    case WM_POWERBROADCAST:
        if(wp==PBT_APMSUSPEND){a->scheduler.tick(a->clock.sample());a->clock.suspended=true;a->flush();}
        if(wp==PBT_APMRESUMEAUTOMATIC||wp==PBT_APMRESUMESUSPEND)a->clock.suspended=a->displayOff_;
        if(wp==PBT_POWERSETTINGCHANGE) {
            auto p=reinterpret_cast<POWERBROADCAST_SETTING*>(lp);
            if(p->PowerSetting==GUID_CONSOLE_DISPLAY_STATE && p->DataLength==sizeof(DWORD)) {
                a->scheduler.tick(a->clock.sample());a->displayOff_=(*reinterpret_cast<DWORD*>(p->Data)==0);a->clock.suspended=a->displayOff_;
            }
        }
        a->update();return TRUE;
    case WM_TIMECHANGE:a->update();return 0;
    case WM_SETTINGCHANGE:case WM_THEMECHANGED:
        if(a->main)a->main->setPage(-1);
        if(a->notification)a->notification->setPage(-1);
        a->update();return 0;
    case WM_QUERYENDSESSION:a->scheduler.tick(a->clock.sample());a->flush();return TRUE;
    case WM_ENDSESSION:if(wp)DestroyWindow(w);return 0;
    case WM_DESTROY:
        a->scheduler.tick(a->clock.sample());a->flush();a->main.reset();a->notification.reset();
        KillTimer(w,1);Shell_NotifyIconW(NIM_DELETE,&a->tray_);WTSUnRegisterSessionNotification(w);
        if(a->power_)UnregisterPowerSettingNotification(a->power_);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
int App::run() {
    store.loadSettings(scheduler.settings);store.loadStatistics(scheduler.days);
    scheduler.tick(clock.sample());nextFlush_=static_cast<Ms>(GetTickCount64())+5*minute;
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=proc;wc.lpszClassName=L"Breakly.Host.V1";RegisterClassW(&wc);
    host=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,hostTitle().c_str(),WS_POPUP,0,0,0,0,nullptr,nullptr,wc.hInstance,this);
    if(!host)return 2;
    icon=makeIcon(32);taskbarCreated_=RegisterWindowMessageW(L"TaskbarCreated");tray(true);
    WTSRegisterSessionNotification(host,NOTIFY_FOR_THIS_SESSION);
    DWORD session=0;ProcessIdToSessionId(GetCurrentProcessId(),&session);
    LPWSTR buffer=nullptr;DWORD bytes=0;
    if(WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,session,WTSSessionInfoEx,&buffer,&bytes)) {
        auto info=reinterpret_cast<WTSINFOEXW*>(buffer);
        if(info->Level==1)clock.locked=info->Data.WTSInfoExLevel1.SessionFlags==WTS_SESSIONSTATE_LOCK;
        WTSFreeMemory(buffer);
    }
    power_=RegisterPowerSettingNotification(host,&GUID_CONSOLE_DISPLAY_STATE,DEVICE_NOTIFY_WINDOW_HANDLE);
    update();
    if(!background_ && !scheduler.settings.minimized)openMain();
    MSG msg{};BOOL result=0;
    while((result=GetMessageW(&msg,nullptr,0,0))>0) {
        HWND dialog=notification && (msg.hwnd==notification->hwnd || IsChild(notification->hwnd,msg.hwnd))?notification->hwnd:
            main && (msg.hwnd==main->hwnd || IsChild(main->hwnd,msg.hwnd))?main->hwnd:nullptr;
        if(dialog && IsDialogMessageW(dialog,&msg))continue;
        TranslateMessage(&msg);DispatchMessageW(&msg);
    }
    return result<0?3:0;
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    using namespace breakly;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_TAB_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&controls);
    int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);
    auto root=dataDirectory();bool background=false,diagnostics=false;
    for(int i=1;i<count;++i) {
        if(wcscmp(args[i],L"--background")==0)background=true;
        else if(wcscmp(args[i],L"--diagnostics")==0)diagnostics=true;
        else if(wcscmp(args[i],L"--data-dir")==0 && i+1<count)root=std::filesystem::absolute(args[++i]);
    }
    if(args)LocalFree(args);
    // A per-user, per-session name; isolated profiles have independent test instances.
    HANDLE token=nullptr;std::wstring sid=L"user";
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) {
        DWORD n=0;GetTokenInformation(token,TokenUser,nullptr,0,&n);std::vector<BYTE> data(n);
        if(GetTokenInformation(token,TokenUser,data.data(),n,&n)) {LPWSTR text=nullptr;if(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&text)){sid=text;LocalFree(text);}}
        CloseHandle(token);
    }
    std::wstring mutexName=L"Local\\Breakly."+sid+L"."+std::to_wstring(std::hash<std::wstring>{}(root.wstring()));
    HANDLE mutex=CreateMutexW(nullptr,FALSE,mutexName.c_str());
    if(!mutex){MessageBoxW(nullptr,L"Не удалось создать одиночный экземпляр Breakly.",L"Breakly",MB_ICONERROR);if(SUCCEEDED(co))CoUninitialize();return 1;}
    if(GetLastError()==ERROR_ALREADY_EXISTS) {
        // Find the instance in this session; a bounded retry covers startup races.
        std::wstring title=L"Breakly.Host."+std::to_wstring(std::hash<std::wstring>{}(root.wstring()));
        for(int i=0;i<20;++i){HWND w=FindWindowW(L"Breakly.Host.V1",title.c_str());if(w){if(!background)PostMessageW(w,openMessage,0,0);break;}Sleep(50);}
        CloseHandle(mutex);if(SUCCEEDED(co))CoUninitialize();return 0;
    }
    int result=0;
    try {App app(root,background,diagnostics);result=app.run();}
    catch(const std::exception&) {MessageBoxW(nullptr,L"Не удалось запустить Breakly. Проверьте папку данных и доступную память.",L"Breakly",MB_ICONERROR);result=1;}
    CloseHandle(mutex);if(SUCCEEDED(co))CoUninitialize();return result;
}
