#include "ui.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <algorithm>
#include <cmath>

namespace breakly {
namespace {
constexpr int Apply=400,Autostart=401,FirstYes=402,FirstNo=403;
constexpr int TimerLabel=900,StatusLabel=901,StatsLabel=902,NextLabel=903;
const wchar_t* titles[]={L"Обзор",L"Общие",L"Перерывы",L"Уведомления",L"Активность",L"Расписание",L"Статистика"};
std::vector<HMONITOR> monitors() {
    std::vector<HMONITOR> m;EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM p)->BOOL{reinterpret_cast<std::vector<HMONITOR>*>(p)->push_back(monitor);return TRUE;},reinterpret_cast<LPARAM>(&m));return m;
}
COLORREF accentColor(bool dark) {
    DWORD color=0;BOOL opaque=FALSE;DwmGetColorizationColor(&color,&opaque);
    COLORREF c=RGB((color>>16)&255,(color>>8)&255,color&255);
    // Keep an accent visible in both themes, even with a very pale system choice.
    int r=GetRValue(c),g=GetGValue(c),b=GetBValue(c);
    if(!dark && r+g+b>540){r=r*2/3;g=g*2/3;b=b*2/3;}
    if(dark && r+g+b<250){r=(r+160)/2;g=(g+190)/2;b=(b+210)/2;}
    return RGB(r,g,b);
}
D2D1_COLOR_F colorF(COLORREF c,float alpha=1) {return D2D1::ColorF(GetRValue(c)/255.f,GetGValue(c)/255.f,GetBValue(c)/255.f,alpha);}
LRESULT CALLBACK focusSubclass(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
    if(msg==WM_SETFOCUS)PostMessageW(GetParent(w),WM_APP+44,reinterpret_cast<WPARAM>(w),0);
    if(msg==WM_NCDESTROY)RemoveWindowSubclass(w,focusSubclass,id);
    return DefSubclassProc(w,msg,wp,lp);
}
}
int View::px(int value) const {return MulDiv(value,static_cast<int>(dpi_),96);}
View::View(App& a,bool notification,int page):app_(a),notification_(notification),page_(page) {
    WNDCLASSEXW c{sizeof(c)};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=proc;
    c.lpszClassName=notification?L"Breakly.Reminder":L"Breakly.Main";c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hIcon=app_.icon;c.hIconSm=app_.icon;
    RegisterClassExW(&c);dpi_=GetDpiForSystem();
    DWORD style=notification ? WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN : WS_OVERLAPPEDWINDOW|WS_VSCROLL|WS_HSCROLL|WS_CLIPCHILDREN;
    DWORD ex=notification?WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE:0;
    HMONITOR monitor=MonitorFromWindow(GetForegroundWindow(),MONITOR_DEFAULTTOPRIMARY);
    auto m=monitors();int index=app_.scheduler.settings.monitor;
    if(notification && index>0 && index<=static_cast<int>(m.size()))monitor=m[index-1];
    UINT dx=96,dy=96;if(SUCCEEDED(GetDpiForMonitor(monitor,MDT_EFFECTIVE_DPI,&dx,&dy)))dpi_=dx;
    MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(monitor,&mi);
    int width=std::min(px(notification?484:920),static_cast<int>(mi.rcWork.right-mi.rcWork.left)-px(24));
    int height=std::min(px(notification?342:760),static_cast<int>(mi.rcWork.bottom-mi.rcWork.top)-px(24));
    int x=notification?mi.rcWork.right-width-px(16):mi.rcWork.left+(mi.rcWork.right-mi.rcWork.left-width)/2;
    int y=notification?mi.rcWork.bottom-height-px(16):mi.rcWork.top+(mi.rcWork.bottom-mi.rcWork.top-height)/2;
    hwnd=CreateWindowExW(ex,c.lpszClassName,notification?L"Breakly — пора отдохнуть":L"Breakly",style,x,y,width,height,nullptr,nullptr,c.hInstance,this);
    if(!hwnd)throw std::runtime_error("CreateWindow");
}
View::~View() {
    if(hwnd && IsWindow(hwnd))DestroyWindow(hwnd);
    target_.Reset();brand_.Reset();write_.Reset();factory_.Reset();
    if(font_)DeleteObject(font_);if(heading_)DeleteObject(heading_);if(timer_)DeleteObject(timer_);if(background_)DeleteObject(background_);
}
void View::theme() {
    auto& s=app_.scheduler.settings;
    dark_=s.theme==2 || (s.theme==0 && systemDark());
    if(highContrast()){bg_=GetSysColor(COLOR_WINDOW);fg_=GetSysColor(COLOR_WINDOWTEXT);muted_=fg_;accent_=GetSysColor(COLOR_HIGHLIGHT);}
    else {bg_=dark_?RGB(32,32,32):RGB(249,249,249);fg_=dark_?RGB(245,245,245):RGB(28,28,28);muted_=dark_?RGB(188,188,188):RGB(92,92,92);accent_=accentColor(dark_);}
    if(background_)DeleteObject(background_);background_=CreateSolidBrush(bg_);
    decorate(hwnd,dark_,!notification_,s.effects);
    LOGFONTW lf{};lf.lfHeight=-px(14);lf.lfWeight=FW_NORMAL;lf.lfQuality=CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName,L"Segoe UI Variable Text");
    bool available=false;HDC dc=GetDC(hwnd);
    EnumFontFamiliesExW(dc,&lf,[](const LOGFONTW*,const TEXTMETRICW*,DWORD,LPARAM p)->int{*reinterpret_cast<bool*>(p)=true;return 0;},reinterpret_cast<LPARAM>(&available),0);
    ReleaseDC(hwnd,dc);if(!available)wcscpy_s(lf.lfFaceName,L"Segoe UI");
    if(font_)DeleteObject(font_);font_=CreateFontIndirectW(&lf);
    lf.lfHeight=-px(26);lf.lfWeight=FW_SEMIBOLD;if(heading_)DeleteObject(heading_);heading_=CreateFontIndirectW(&lf);
    lf.lfHeight=-px(64);lf.lfWeight=FW_NORMAL;if(timer_)DeleteObject(timer_);timer_=CreateFontIndirectW(&lf);
    brand_.Reset();target_.Reset();
}
HWND View::control(const wchar_t* cls,const std::wstring& text,DWORD style,int id,int x,int y,int width,int height) {
    HWND c=CreateWindowExW(wcscmp(cls,L"EDIT")==0?WS_EX_CLIENTEDGE:0,cls,text.c_str(),WS_CHILD|WS_VISIBLE|style,px(x),px(y-scroll_),px(width),px(height),hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);SetWindowTheme(c,L"Explorer",nullptr);
    SetWindowSubclass(c,focusSubclass,1,0);
    controls_.push_back(c);placements_.push_back({c,x,y,width,height});return c;
}
HWND View::label(const std::wstring& text,int x,int y,int width,int height,bool head) {
    HWND c=control(L"STATIC",text,SS_LEFT|SS_NOPREFIX,0,x,y,width,height);
    if(head)SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(heading_),TRUE);return c;
}
void View::button(const wchar_t* text,int id,int x,int y,int width,int height) {control(L"BUTTON",text,WS_TABSTOP|BS_PUSHBUTTON|BS_OWNERDRAW,id,x,y,width,height);}
void View::check(const wchar_t* text,int id,bool value,int y) {HWND c=control(L"BUTTON",text,WS_TABSTOP|BS_AUTOCHECKBOX,id,32,y,790,30);SendMessageW(c,BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);}
void View::field(const wchar_t* text,int id,int value,int y) {
    label(text,32,y+5,520,28);control(L"EDIT",std::to_wstring(value),WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL,id,590,y,170,32);
    SendDlgItemMessageW(hwnd,id,EM_SETLIMITTEXT,6,0);
}
void View::timeField(const wchar_t* text,int id,int value,int y) {
    label(text,32,y+5,520,28);wchar_t formatted[8]{};swprintf_s(formatted,L"%02d:%02d",value/60,value%60);
    control(L"EDIT",formatted,WS_TABSTOP|ES_AUTOHSCROLL,id,590,y,170,32);SendDlgItemMessageW(hwnd,id,EM_SETLIMITTEXT,5,0);
}
void View::combo(const wchar_t* text,int id,const std::vector<std::wstring>& items,int selected,int y) {
    label(text,32,y+5,270,28);HWND c=control(WC_COMBOBOXW,L"",WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,id,320,y,480,250);
    for(auto& item:items)SendMessageW(c,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item.c_str()));SendMessageW(c,CB_SETCURSEL,selected,0);
}
void View::build(bool preserve) {
    struct Draft {int id;std::wstring text;LRESULT selection,check;};std::vector<Draft> draft;
    int oldScroll=scroll_,oldX=scrollX_,focusId=IsChild(hwnd,GetFocus())?GetDlgCtrlID(GetFocus()):-1;
    bool wasActive=GetForegroundWindow()==hwnd;
    if(preserve)for(HWND c:controls_) {
        int id=GetDlgCtrlID(c);if(id!=Autostart && (id<500 || id>556))continue;
        int len=GetWindowTextLengthW(c);std::wstring text(static_cast<size_t>(len)+1,L'\0');GetWindowTextW(c,text.data(),len+1);text.resize(len);
        draft.push_back({id,std::move(text),SendMessageW(c,CB_GETCURSEL,0,0),SendMessageW(c,BM_GETCHECK,0,0)});
    }
    for(HWND c:controls_)DestroyWindow(c);controls_.clear();placements_.clear();tabs_=nullptr;scroll_=0;scrollX_=0;theme();
    auto& s=app_.scheduler.settings;
    if(notification_) {
        contentHeight_=300;
        Kind k=app_.scheduler.active.value_or(app_.scheduler.pending.value_or(Kind::Eyes));
        label(k==Kind::Eyes?L"Дай глазам отдохнуть":k==Kind::Stretch?L"Время размяться":L"Пора сделать перерыв",24,20,430,44,true);
        std::wstring copy=k==Kind::Eyes?L"Посмотри на предмет в 6 метрах от себя в течение 20 секунд.":k==Kind::Stretch?L"Встань и пройдись. Плавно подними плечи, мягко потянись или смени положение тела.":L"Ты уже час за компьютером. Пора сделать перерыв.";
        // Custom intervals must not claim that an hour has necessarily elapsed.
        if(k==Kind::Long && s.intervalMin[2]!=60)copy=L"Время полноценного перерыва. Отойди от экрана и немного пройдись.";
        if(k==Kind::Eyes && s.durationSec[0]!=20)copy=L"Посмотри на предмет вдали. Длительность отдыха — "+std::to_wstring(s.durationSec[0])+L" с.";
        label(copy,24,76,420,66);
        if(app_.scheduler.active) {
            HWND timer=control(L"STATIC",L"",SS_LEFT|SS_NOPREFIX,TimerLabel,24,146,400,44);SendMessageW(timer,WM_SETFONT,reinterpret_cast<WPARAM>(heading_),TRUE);
            button(L"Подтвердить отдых",204,24,216,232);button(L"Завершить раньше",203,270,216,174);
            label(L"Подтверждение по кнопке — это твоя оценка отдыха.",24,264,426,22);
        } else {
            label(L"Окно не переключает фокус с твоей работы.",24,148,420,28);
            button(L"Начать отдых",201,24,206,142);button(L"Через 5 минут",202,176,206,146);button(L"Пропустить",203,332,206,112);
        }
    } else {
        contentHeight_=680;
        int navX=24;const int widths[]={92,94,110,146,124,132,120};
        for(int i=0;i<7;++i){button(titles[i],800+i,navX,64,widths[i],36);navX+=widths[i];}
        if(page_==0) {
            label(L"Перерывы вовремя",32,124,810,44,true);
            control(L"STATIC",L"",SS_LEFT,StatusLabel,32,182,810,28);
            HWND timer=control(L"STATIC",L"",SS_LEFT,TimerLabel,26,218,790,92);SendMessageW(timer,WM_SETFONT,reinterpret_cast<WPARAM>(timer_),TRUE);
            control(L"STATIC",L"",SS_LEFT,NextLabel,32,322,810,32);
            button(L"Сделать перерыв",StartNow,32,388,210);button(L"Пауза на 15 минут",Pause15,256,388,220);button(L"Возобновить",Resume,490,388,170);
            button(L"Открыть напоминание",208,674,388,190);
            label(L"Сегодня",32,460,810,40,true);control(L"STATIC",L"",SS_LEFT,StatsLabel,32,516,810,62);
            if(!s.onboarded) {
                label(L"Запускать Breakly при входе в Windows? Окно останется в трее.",32,590,810,26);
                button(L"Включить автозапуск",FirstYes,32,628,230);button(L"Не сейчас",FirstNo,276,628,132);
            } else label(L"Закрытие окна оставляет Breakly в трее. Выход — через меню значка.",32,620,810,28);
        } else if(page_==1) {
            label(L"Общие настройки",32,124,810,44,true);
            combo(L"Тема",500,{L"Как в Windows",L"Светлая",L"Тёмная"},s.theme,188);
            check(L"Запускать при входе в Windows",Autostart,AutostartManager::enabled(),246);
            if(app_.isolated())EnableWindow(GetDlgItem(hwnd,Autostart),FALSE);
            check(L"Запускать ручной экземпляр свёрнутым",501,s.minimized,290);
            check(L"Звук напоминания",502,s.sound,334);
            check(L"Системные материалы и переходы",503,s.effects,378);
            label(L"Язык: русский. Данные остаются на этом компьютере.",32,440,810,28);
            button(L"Открыть папку данных",209,32,490,250);
        } else if(page_==2) {
            label(L"Ритм перерывов",32,124,810,44,true);
            const wchar_t* names[]={L"Отдых для глаз: интервал, мин",L"Отдых для глаз: длительность, с",L"Разминка: интервал, мин",L"Разминка: длительность, с",L"Полноценный перерыв: интервал, мин",L"Полноценный перерыв: длительность, с"};
            for(int i=0;i<6;++i)field(names[i],510+i,i%2?s.durationSec[i/2]:s.intervalMin[i/2],188+i*54);
        } else if(page_==3) {
            contentHeight_=758;
            label(L"Напоминания",32,124,810,44,true);
            combo(L"Режим",520,{L"Мягкий — исчезает сам",L"Стандартный — ждёт действия",L"Настойчивый — повторяется"},s.notificationMode,188);
            combo(L"Доставка",521,{L"Собственное окно Breakly",L"Уведомление Windows (зависит от «Не беспокоить»)"},s.nativeNotifications?1:0,238);
            combo(L"Полноэкранные приложения",522,{L"Показывать всегда",L"Откладывать на минуту",L"Ждать выхода из полного экрана"},s.fullscreen,288);
            std::vector<std::wstring> names{L"Монитор активного окна"};int i=1;for(auto m:monitors()){MONITORINFOEXW info{};info.cbSize=sizeof(info);GetMonitorInfoW(m,&info);names.push_back(L"Монитор "+std::to_wstring(i++)+L" · "+info.szDevice);}
            combo(L"Монитор",523,names,std::min(s.monitor,static_cast<int>(names.size())-1),338);
            field(L"Повтор напоминания, с",524,s.repeatSec,400);field(L"Мягкое окно: время показа, с",525,s.gentleSec,454);
            label(L"Собственное окно работает при «Не беспокоить» в обычном сеансе.\nНа экране блокировки и защищённом рабочем столе окна не показываются.",32,514,820,62);
            button(L"Проверить напоминание",TestNotify,32,590,270);
        } else if(page_==4) {
            label(L"Учёт активности",32,124,810,44,true);
            field(L"Приостановить после бездействия, с",530,s.idleSec,188);field(L"Кандидат на длительный отдых, с",531,s.restSec,242);
            check(L"Чтение: учитывать экранное время без ввода",532,s.reading,304);
            check(L"Видео: учитывать экранное время без ввода",533,s.video,348);
            check(L"Учитывать блокировку как возможный отдых",534,s.lockRest,392);
            check(L"Учитывать сон как возможный отдых",535,s.sleepRest,436);
            label(L"Сон и блокировка никогда не считаются работой.\nОтсутствие ввода не подтверждает, что глаза отдыхали.",32,498,810,58);
        } else if(page_==5) {
            contentHeight_=790;
            label(L"Расписание",32,124,810,44,true);
            check(L"Напоминать только в рабочие часы",540,s.schedule,182);
            timeField(L"Начало работы",541,s.workStart,230);timeField(L"Конец работы",542,s.workEnd,278);
            label(L"Рабочие дни",32,336,810,28);
            const wchar_t* days[]={L"Пн",L"Вт",L"Ср",L"Чт",L"Пт",L"Сб",L"Вс"};
            for(int i=0;i<7;++i){HWND c=control(L"BUTTON",days[i],WS_TABSTOP|BS_AUTOCHECKBOX,550+i,32+i*110,374,100,30);SendMessageW(c,BM_SETCHECK,s.days&(1<<i)?BST_CHECKED:BST_UNCHECKED,0);}
            check(L"Тихие часы",543,s.quiet,430);timeField(L"Начало тихих часов",544,s.quietStart,478);timeField(L"Конец тихих часов",545,s.quietEnd,526);
            label(L"Время в формате ЧЧ:ММ. Расписание может переходить через полночь.\nВременно отключить напоминания можно через паузу в трее.",32,584,810,56);
        } else if(page_==6) {
            label(L"День и неделя",32,124,810,44,true);
            auto describe=[](const Daily& d) {return L"Экранная активность: "+std::to_wstring(d.screen/minute)+L" мин\nС недавним вводом (оценка): "+std::to_wstring(d.input/minute)+L" мин\nПредложено / начато / подтверждено: "+std::to_wstring(d.offered)+L" / "+std::to_wstring(d.started)+L" / "+std::to_wstring(d.completed)+L"\nПропущено / отложено: "+std::to_wstring(d.skipped)+L" / "+std::to_wstring(d.snoozed)+L"\nОтдых по подтверждению: "+std::to_wstring(d.confirmedRest/minute)+L" мин\nСамая длинная сессия: "+std::to_wstring(d.longest/minute)+L" мин";};
            label(L"Сегодня",32,190,380,30);label(L"Последние 7 дней",454,190,400,30);
            label(describe(app_.scheduler.today()),32,236,400,260);label(describe(app_.scheduler.week()),454,236,400,260);
            label(L"Нажатие кнопки подтверждает твою оценку отдыха.\nBreakly не измеряет усталость и не проверяет, куда ты смотришь.",32,530,810,62);
        }
        if(page_>=1 && page_<=5)button(L"Сохранить настройки",Apply,32,contentHeight_-54,240);
    }
    if(preserve) {
        for(auto& d:draft){HWND c=GetDlgItem(hwnd,d.id);if(!c)continue;wchar_t cls[32]{};GetClassNameW(c,cls,32);
            if(_wcsicmp(cls,L"Edit")==0)SetWindowTextW(c,d.text.c_str());
            else if(_wcsicmp(cls,L"ComboBox")==0)SendMessageW(c,CB_SETCURSEL,d.selection,0);
            else if(_wcsicmp(cls,L"Button")==0)SendMessageW(c,BM_SETCHECK,d.check,0);
        }
        scroll_=oldScroll;scrollX_=oldX;
    }
    layout();refresh(true);InvalidateRect(hwnd,nullptr,TRUE);
    if(preserve && wasActive && focusId>0 && GetDlgItem(hwnd,focusId))SetFocus(GetDlgItem(hwnd,focusId));
}
void View::layout() {
    RECT r{};GetClientRect(hwnd,&r);int visible=MulDiv(r.bottom,96,static_cast<int>(dpi_));
    int maxScroll=std::max(0,contentHeight_-visible+16);scroll_=std::clamp(scroll_,0,maxScroll);
    if(!notification_) {SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,contentHeight_+15,static_cast<UINT>(visible),scroll_,0};SetScrollInfo(hwnd,SB_VERT,&si,TRUE);
        int width=MulDiv(r.right,96,static_cast<int>(dpi_));scrollX_=std::clamp(scrollX_,0,std::max(0,904-width));
        SCROLLINFO horizontal{sizeof(horizontal),SIF_RANGE|SIF_PAGE|SIF_POS,0,903,static_cast<UINT>(width),scrollX_,0};SetScrollInfo(hwnd,SB_HORZ,&horizontal,TRUE);
    }
    for(auto& p:placements_)SetWindowPos(p.w,nullptr,px(p.x-scrollX_),px(p.y-scroll_),px(p.width),px(p.height),SWP_NOZORDER|SWP_NOACTIVATE);
    if(target_)target_->Resize(D2D1::SizeU(static_cast<UINT32>(r.right),static_cast<UINT32>(r.bottom)));
}
void View::refresh(bool initial) {
    if(!hwnd || (!initial && (!IsWindowVisible(hwnd)||IsIconic(hwnd))))return;
    auto& scheduler=app_.scheduler;
    auto text=[&](int id,const std::wstring& value){HWND c=GetDlgItem(hwnd,id);if(!c)return;int len=GetWindowTextLengthW(c);std::wstring before(static_cast<size_t>(len)+1,L'\0');GetWindowTextW(c,before.data(),len+1);before.resize(len);if(before!=value)SetWindowTextW(c,value.c_str());};
    if(notification_) {
        if(scheduler.active){text(TimerLabel,scheduler.breakRemaining()>0?durationText(scheduler.breakRemaining()):L"Готово. Как прошёл отдых?");EnableWindow(GetDlgItem(hwnd,204),scheduler.breakRemaining()==0);}
    } else if(page_==0) {
        text(StatusLabel,app_.statusText());
        Kind next=Kind::Eyes;
        for(int i=1;i<3;++i)if(scheduler.remaining(Kind(i))<scheduler.remaining(next))next=Kind(i);
        text(TimerLabel,scheduler.active?durationText(scheduler.breakRemaining()):durationText(scheduler.remaining(next)));
        text(NextLabel,scheduler.active?L"Осталось до завершения отдыха":L"До следующего: "+kindText(next));
        auto d=scheduler.today();text(StatsLabel,L"Экранное время: "+std::to_wstring(d.screen/minute)+L" мин     Подтверждено перерывов: "+std::to_wstring(d.completed)+L"\n"+(app_.store.error.empty()?L"Данные хранятся локально. Сеть не используется.":app_.store.error));
        EnableWindow(GetDlgItem(hwnd,208),scheduler.pending.has_value()||scheduler.active.has_value());
        RECT r{px(32),px(360-scroll_),px(860),px(370-scroll_)};InvalidateRect(hwnd,&r,FALSE);
    }
}
void View::activate() {
    if(notification_)SetWindowLongPtrW(hwnd,GWL_EXSTYLE,GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&~WS_EX_NOACTIVATE);
    ShowWindow(hwnd,IsIconic(hwnd)?SW_RESTORE:SW_SHOW);SetForegroundWindow(hwnd);
    // STARTUPINFO may request a hidden process; an explicit Open action wins.
    if(!IsWindowVisible(hwnd))ShowWindow(hwnd,SW_SHOW);
    HWND first=GetNextDlgTabItem(hwnd,nullptr,FALSE);if(first)SetFocus(first);refresh();
}
void View::setPage(int page) {if(page>=0)page_=page;build(page<0);}
void View::save() {
    Settings s=app_.scheduler.settings;bool valid=true;
    auto number=[&](int id,int& value) {BOOL translated=FALSE;value=static_cast<int>(GetDlgItemInt(hwnd,id,&translated,FALSE));valid=valid && translated;};
    auto checked=[&](int id) {return IsDlgButtonChecked(hwnd,id)==BST_CHECKED;};
    auto selected=[&](int id){return static_cast<int>(SendDlgItemMessageW(hwnd,id,CB_GETCURSEL,0,0));};
    auto time=[&](int id,int& value){wchar_t b[16]{};GetDlgItemTextW(hwnd,id,b,16);int hour=0,min=0;bool ok=wcslen(b)==5 && b[2]==L':' && b[0]>=L'0'&&b[0]<=L'9'&&b[1]>=L'0'&&b[1]<=L'9'&&b[3]>=L'0'&&b[3]<=L'9'&&b[4]>=L'0'&&b[4]<=L'9';if(ok){hour=(b[0]-L'0')*10+b[1]-L'0';min=(b[3]-L'0')*10+b[4]-L'0';ok=hour<24&&min<60;}valid=valid&&ok;value=hour*60+min;};
    if(page_==1){s.theme=selected(500);s.minimized=checked(501);s.sound=checked(502);s.effects=checked(503);}
    if(page_==2)for(int i=0;i<6;++i)number(510+i,i%2?s.durationSec[i/2]:s.intervalMin[i/2]);
    if(page_==3){s.notificationMode=selected(520);s.nativeNotifications=selected(521)==1;s.fullscreen=selected(522);s.monitor=selected(523);number(524,s.repeatSec);number(525,s.gentleSec);}
    if(page_==4){number(530,s.idleSec);number(531,s.restSec);s.reading=checked(532);s.video=checked(533);s.lockRest=checked(534);s.sleepRest=checked(535);}
    if(page_==5){s.schedule=checked(540);time(541,s.workStart);time(542,s.workEnd);s.quiet=checked(543);time(544,s.quietStart);time(545,s.quietEnd);s.days=0;for(int i=0;i<7;++i)if(checked(550+i))s.days|=1<<i;}
    if(!valid || !s.valid()){MessageBoxW(hwnd,L"Проверь значения. Интервалы: 1–1440 мин; отдых: 5–3600 с; бездействие: 15–3600 с; длительный отдых — не меньше порога бездействия и не больше 7200 с; повтор: 15–3600 с; мягкое окно: 5–300 с. Время суток: ЧЧ:ММ; выбери хотя бы один день.",L"Настройки не сохранены",MB_OK|MB_ICONWARNING);return;}
    if(page_==1 && !app_.isolated() && checked(Autostart)!=AutostartManager::enabled()) {
        if(!AutostartManager::set(checked(Autostart))){MessageBoxW(hwnd,L"Не удалось изменить автозапуск. Проверь доступ к настройкам Windows.",L"Breakly",MB_ICONERROR);return;}
    }
    if(!app_.saveSettings(s)){MessageBoxW(hwnd,app_.store.error.c_str(),L"Не удалось сохранить настройки",MB_ICONERROR);return;}
    build();PostMessageW(app_.host,WM_TIMER,1,0);
}
void View::command(int id) {
    if(id==IDCANCEL){PostMessageW(hwnd,WM_CLOSE,0,0);return;}
    if(id>=800 && id<=806){page_=id-800;build();SetFocus(GetDlgItem(hwnd,id));return;}
    if(id==Apply){save();return;}
    if(id==FirstYes || id==FirstNo) {
        if(id==FirstYes && !app_.isolated() && !AutostartManager::set(true)){MessageBoxW(hwnd,L"Не удалось включить автозапуск.",L"Breakly",MB_ICONERROR);return;}
        Settings s=app_.scheduler.settings;s.onboarded=true;
        if(!app_.saveSettings(s)){MessageBoxW(hwnd,app_.store.error.c_str(),L"Не удалось сохранить настройки",MB_ICONERROR);return;}build();return;
    }
    if(id==208){if(app_.notification)app_.notification->activate();else if(app_.scheduler.pending||app_.scheduler.active){app_.notification=std::make_unique<View>(app_,true);app_.notification->activate();}return;}
    if(id==209){ShellExecuteW(hwnd,L"open",app_.store.root().c_str(),nullptr,nullptr,SW_SHOWNORMAL);return;}
    PostMessageW(app_.host,WM_COMMAND,id,0);
}
void View::paint() {
    PAINTSTRUCT ps{};BeginPaint(hwnd,&ps);
    if(!factory_)D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory_.GetAddressOf());
    if(!write_)DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write_.GetAddressOf()));
    RECT r{};GetClientRect(hwnd,&r);
    if(factory_ && !target_) {
        // This UI paints text and a single progress line. Software D2D avoids
        // a per-window GPU context and driver caches for these tiny scenes.
        auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE);props.dpiX=static_cast<float>(dpi_);props.dpiY=static_cast<float>(dpi_);
        props.pixelFormat.alphaMode=D2D1_ALPHA_MODE_PREMULTIPLIED;
        factory_->CreateHwndRenderTarget(props,D2D1::HwndRenderTargetProperties(hwnd,D2D1::SizeU(r.right,r.bottom),D2D1_PRESENT_OPTIONS_NONE),target_.GetAddressOf());
    }
    if(write_ && !brand_)write_->CreateTextFormat(L"Segoe UI Variable Text",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,20,L"ru-RU",brand_.GetAddressOf());
    if(target_) {
        target_->BeginDraw();bool mica=!notification_ && app_.scheduler.settings.effects && !highContrast() && transparency() && isWindowsBuild(22621);
        target_->Clear(mica?D2D1::ColorF(0,0.f):colorF(bg_));
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;target_->CreateSolidColorBrush(colorF(fg_),brush.GetAddressOf());
        if(brush && !notification_) {
            if(mica){brush->SetColor(colorF(bg_));target_->FillRectangle(D2D1::RectF(0,64.f-scroll_,static_cast<float>(r.right)*96/dpi_,static_cast<float>(r.bottom)*96/dpi_),brush.Get());brush->SetColor(colorF(fg_));}
            target_->SetTransform(D2D1::Matrix3x2F::Translation(-static_cast<float>(scrollX_),0));
            if(brand_)target_->DrawText(L"Breakly",7,brand_.Get(),D2D1::RectF(32.f,20.f-scroll_,400.f,52.f-scroll_),brush.Get());
            if(page_==0){float width=828;Kind next=Kind::Eyes;for(int i=1;i<3;++i)if(app_.scheduler.remaining(Kind(i))<app_.scheduler.remaining(next))next=Kind(i);
                float progress=1-static_cast<float>(app_.scheduler.remaining(next))/static_cast<float>(app_.scheduler.settings.intervalMin[int(next)]*minute);
                brush->SetColor(colorF(muted_,.2f));target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(32,360.f-scroll_,32+width,365.f-scroll_),2.5f,2.5f),brush.Get());
                brush->SetColor(colorF(accent_));target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(32,360.f-scroll_,32+width*std::clamp(progress,0.f,1.f),365.f-scroll_),2.5f,2.5f),brush.Get());
            }
        }
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        if(target_->EndDraw()==D2DERR_RECREATE_TARGET)target_.Reset();
    } else FillRect(ps.hdc,&r,background_);
    EndPaint(hwnd,&ps);
}
LRESULT CALLBACK View::proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    auto v=reinterpret_cast<View*>(GetWindowLongPtrW(w,GWLP_USERDATA));
    if(msg==WM_NCCREATE){v=static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);v->hwnd=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(v));}
    if(!v)return DefWindowProcW(w,msg,wp,lp);
    switch(msg) {
    case WM_CREATE:v->dpi_=GetDpiForWindow(w);v->build();return 0;
    case WM_SHOWWINDOW:if(wp){SetTimer(w,2,1000,nullptr);v->refresh();}else KillTimer(w,2);return 0;
    case WM_SIZE:if(wp==SIZE_MINIMIZED){KillTimer(w,2);v->target_.Reset();}else if(IsWindowVisible(w))SetTimer(w,2,1000,nullptr);v->layout();return 0;
    case WM_TIMER:if(IsWindowVisible(w)&&!IsIconic(w)){v->app_.scheduler.tick(v->app_.clock.sample());v->refresh();}return 0;
    case WM_PAINT:v->paint();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:
        SetTextColor(reinterpret_cast<HDC>(wp),v->fg_);SetBkColor(reinterpret_cast<HDC>(wp),v->bg_);return reinterpret_cast<LRESULT>(v->background_);
    case WM_DRAWITEM: {
        auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(item->CtlType==ODT_COMBOBOX){HDC dc=item->hDC;HBRUSH b=CreateSolidBrush(v->bg_);FillRect(dc,&item->rcItem,b);DeleteObject(b);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,v->fg_);wchar_t text[512]{};if(item->itemID!=static_cast<UINT>(-1))SendMessageW(item->hwndItem,CB_GETLBTEXT,item->itemID,reinterpret_cast<LPARAM>(text));RECT r=item->rcItem;r.left+=v->px(6);DrawTextW(dc,text,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);if(item->itemState&ODS_FOCUS)DrawFocusRect(dc,&r);return TRUE;}
        if(item->CtlType!=ODT_BUTTON)break;
        HDC dc=item->hDC;RECT rect=item->rcItem;bool disabled=(item->itemState&ODS_DISABLED)!=0;
        COLORREF fill=v->dark_?RGB(52,52,52):RGB(255,255,255);
        if(item->itemState&ODS_SELECTED)fill=v->dark_?RGB(72,72,72):RGB(229,229,229);
        if(highContrast())fill=GetSysColor(COLOR_BTNFACE);
        bool nav=item->CtlID>=800 && item->CtlID<=806;
        if(nav)fill=v->bg_;
        HBRUSH brush=CreateSolidBrush(fill);HPEN pen=CreatePen(PS_SOLID,1,nav?v->bg_:v->dark_?RGB(84,84,84):RGB(204,204,204));
        auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,v->px(8),v->px(8));SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);DeleteObject(pen);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,disabled?v->muted_:v->fg_);auto oldFont=SelectObject(dc,v->font_);
        wchar_t text[256]{};GetWindowTextW(item->hwndItem,text,256);DrawTextW(dc,text,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,oldFont);
        if(nav && static_cast<int>(item->CtlID)-800==v->page_){RECT line=rect;line.top=line.bottom-v->px(3);line.left+=v->px(14);line.right-=v->px(14);HBRUSH accent=CreateSolidBrush(v->accent_);FillRect(dc,&line,accent);DeleteObject(accent);}
        if(item->itemState&ODS_FOCUS){InflateRect(&rect,-v->px(4),-v->px(4));DrawFocusRect(dc,&rect);}return TRUE;
    }
    case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED)v->command(LOWORD(wp));return 0;
    case WM_MEASUREITEM: {auto item=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);if(item->CtlType==ODT_COMBOBOX){item->itemHeight=v->px(28);return TRUE;}break;}
    case WM_NOTIFY: {
        auto header=reinterpret_cast<NMHDR*>(lp);
        if(header->code==NM_CUSTOMDRAW && (GetWindowLongPtrW(header->hwndFrom,GWL_STYLE)&BS_TYPEMASK)==BS_AUTOCHECKBOX) {
            auto draw=reinterpret_cast<NMCUSTOMDRAW*>(lp);if(draw->dwDrawStage!=CDDS_PREPAINT)break;
            HDC dc=draw->hdc;FillRect(dc,&draw->rc,v->background_);RECT box=draw->rc;box.right=box.left+v->px(18);box.top+=(box.bottom-box.top-v->px(18))/2;box.bottom=box.top+v->px(18);
            UINT flags=DFCS_BUTTONCHECK|DFCS_FLAT;if(SendMessageW(header->hwndFrom,BM_GETCHECK,0,0)==BST_CHECKED)flags|=DFCS_CHECKED;
            if(!IsWindowEnabled(header->hwndFrom))flags|=DFCS_INACTIVE;DrawFrameControl(dc,&box,DFC_BUTTON,flags);
            RECT text=draw->rc;text.left+=v->px(28);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,IsWindowEnabled(header->hwndFrom)?v->fg_:v->muted_);wchar_t copy[256]{};GetWindowTextW(header->hwndFrom,copy,256);auto old=SelectObject(dc,v->font_);DrawTextW(dc,copy,-1,&text,DT_LEFT|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,old);
            if(draw->uItemState&CDIS_FOCUS)DrawFocusRect(dc,&text);return CDRF_SKIPDEFAULT;
        }break;
    }
    case WM_CLOSE:PostMessageW(v->app_.host,WM_COMMAND,v->notification_?CloseNotification:CloseMain,0);return 0;
    case WM_KEYDOWN:if(wp==VK_ESCAPE){PostMessageW(w,WM_CLOSE,0,0);return 0;}break;
    case WM_DPICHANGED: {v->dpi_=HIWORD(wp);auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);v->build(true);return 0;}
    case WM_APP+44: {
        if(v->notification_)return 0;HWND focused=reinterpret_cast<HWND>(wp);RECT client{};GetClientRect(w,&client);
        int height=MulDiv(client.bottom,96,static_cast<int>(v->dpi_)),width=MulDiv(client.right,96,static_cast<int>(v->dpi_));
        for(auto& p:v->placements_)if(p.w==focused) {
            wchar_t cls[32]{};GetClassNameW(p.w,cls,32);int controlHeight=wcscmp(cls,L"ComboBox")==0?32:p.height;
            if(p.y<v->scroll_+8)v->scroll_=p.y-8;else if(p.y+controlHeight>v->scroll_+height-8)v->scroll_=p.y+controlHeight-height+8;
            if(p.x<v->scrollX_+8)v->scrollX_=p.x-8;else if(p.x+p.width>v->scrollX_+width-8)v->scrollX_=p.x+p.width-width+8;
            v->layout();InvalidateRect(w,nullptr,TRUE);break;
        }return 0;
    }
    case WM_VSCROLL:case WM_HSCROLL:case WM_MOUSEWHEEL: {
        if(v->notification_)return 0;
        bool horizontal=msg==WM_HSCROLL || (msg==WM_MOUSEWHEEL && (GetKeyState(VK_SHIFT)&0x8000));
        int delta=msg==WM_MOUSEWHEEL?-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*72:0;
        if(msg!=WM_MOUSEWHEEL)switch(LOWORD(wp)){case SB_LINEUP:delta=-32;break;case SB_LINEDOWN:delta=32;break;case SB_PAGEUP:delta=-300;break;case SB_PAGEDOWN:delta=300;break;case SB_THUMBTRACK:{SCROLLINFO si{sizeof(si),SIF_TRACKPOS};GetScrollInfo(w,horizontal?SB_HORZ:SB_VERT,&si);delta=si.nTrackPos-(horizontal?v->scrollX_:v->scroll_);break;}}
        (horizontal?v->scrollX_:v->scroll_)+=delta;v->layout();InvalidateRect(w,nullptr,TRUE);return 0;
    }
    case WM_GETMINMAXINFO:if(!v->notification_){auto m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize={v->px(480),v->px(360)};}return 0;
    case WM_DESTROY:KillTimer(w,2);v->target_.Reset();return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
}
