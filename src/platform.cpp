#include "platform.h"
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <algorithm>
#include <winternl.h>

namespace breakly {
Ms ActivityMonitor::idle() {
    LASTINPUTINFO info{sizeof(info)};
    if(!GetLastInputInfo(&info)) return 0;
    // Unsigned subtraction handles the 32-bit GetLastInputInfo wrap (49.7 days).
    return static_cast<DWORD>(static_cast<DWORD>(GetTickCount64())-info.dwTime);
}
Sample SystemClock::sample() {
    SYSTEMTIME t{};GetLocalTime(&t);
    ULONGLONG awake=0;bool unbiased=QueryUnbiasedInterruptTime(&awake)!=FALSE;
    return {static_cast<Ms>(GetTickCount64()),activity.idle(),t.wYear*10000+t.wMonth*100+t.wDay,
        (t.wDayOfWeek+6)%7,t.wHour*60+t.wMinute,locked,suspended,unbiased?static_cast<Ms>(awake/10000):-1};
}
std::wstring executablePath() {
    std::wstring s(32768,L'\0');DWORD n=GetModuleFileNameW(nullptr,s.data(),static_cast<DWORD>(s.size()));s.resize(n);return s;
}
std::filesystem::path dataDirectory() {
    PWSTR p=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_CREATE,nullptr,&p)))return std::filesystem::path(executablePath()).parent_path()/L"data";
    std::filesystem::path path(p);CoTaskMemFree(p);return path/L"Breakly";
}
bool AutostartManager::enabled() {
    wchar_t value[32768]{};DWORD bytes=sizeof(value);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"Breakly",RRF_RT_REG_SZ,nullptr,value,&bytes)!=ERROR_SUCCESS)return false;
    return value==L"\""+executablePath()+L"\" --background";
}
bool AutostartManager::set(bool on) {
    HKEY key{};
    if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    std::wstring command=L"\""+executablePath()+L"\" --background";
    LSTATUS status=on ? RegSetValueExW(key,L"Breakly",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t))) : RegDeleteValueW(key,L"Breakly");
    RegCloseKey(key);return status==ERROR_SUCCESS || (!on && status==ERROR_FILE_NOT_FOUND);
}
bool BreakPolicyEngine::fullscreen() {
    HWND w=GetForegroundWindow();if(!w || IsIconic(w))return false;
    wchar_t name[80]{};GetClassNameW(w,name,80);
    if(wcscmp(name,L"Progman")==0||wcscmp(name,L"WorkerW")==0||wcscmp(name,L"Shell_TrayWnd")==0)return false;
    RECT r{};if(FAILED(DwmGetWindowAttribute(w,DWMWA_EXTENDED_FRAME_BOUNDS,&r,sizeof(r))))GetWindowRect(w,&r);
    MONITORINFO m{sizeof(m)};GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&m);
    return r.left<=m.rcMonitor.left && r.top<=m.rcMonitor.top && r.right>=m.rcMonitor.right && r.bottom>=m.rcMonitor.bottom;
}
bool isWindowsBuild(DWORD build) {
    using Fn=LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    auto fn=reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
    RTL_OSVERSIONINFOW v{};v.dwOSVersionInfoSize=sizeof(v);return fn && fn(&v)==0 && v.dwBuildNumber>=build;
}
bool systemDark() {DWORD v=1,size=sizeof(v);RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&v,&size);return v==0;}
bool highContrast() {HIGHCONTRASTW h{sizeof(h)};return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(h),&h,0) && (h.dwFlags&HCF_HIGHCONTRASTON);}
bool transparency() {DWORD v=1,size=sizeof(v);RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"EnableTransparency",RRF_RT_REG_DWORD,nullptr,&v,&size);return v!=0;}
void decorate(HWND w,bool dark,bool mica,bool effects) {
    if(!isWindowsBuild(22000))return;
    BOOL d=dark;DwmSetWindowAttribute(w,DWMWA_USE_IMMERSIVE_DARK_MODE,&d,sizeof(d));
    DWM_WINDOW_CORNER_PREFERENCE corners=DWMWCP_ROUND;
    DwmSetWindowAttribute(w,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
    BOOL animation=TRUE;SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animation,0);
    BOOL disable=!effects || !animation || highContrast();
    DwmSetWindowAttribute(w,DWMWA_TRANSITIONS_FORCEDISABLED,&disable,sizeof(disable));
    if(isWindowsBuild(22621)) {
        DWM_SYSTEMBACKDROP_TYPE type=mica && effects && !highContrast() && transparency() ? DWMSBT_MAINWINDOW : DWMSBT_NONE;
        DwmSetWindowAttribute(w,DWMWA_SYSTEMBACKDROP_TYPE,&type,sizeof(type));
        // Only the brand band is extended glass. Native accessible GDI controls
        // stay in the opaque client area, avoiding invalid GDI alpha on Mica.
        MARGINS margins=type==DWMSBT_MAINWINDOW ? MARGINS{0,0,MulDiv(64,static_cast<int>(GetDpiForWindow(w)),96),0} : MARGINS{};
        DwmExtendFrameIntoClientArea(w,&margins);
    }
}
HICON makeIcon(int size) {
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
    BITMAPV5HEADER h{};h.bV5Size=sizeof(h);h.bV5Width=size;h.bV5Height=-size;h.bV5Planes=1;h.bV5BitCount=32;h.bV5Compression=BI_BITFIELDS;
    h.bV5RedMask=0x00ff0000;h.bV5GreenMask=0x0000ff00;h.bV5BlueMask=0x000000ff;h.bV5AlphaMask=0xff000000;
    void* bits=nullptr;HBITMAP color=CreateDIBSection(dc,reinterpret_cast<BITMAPINFO*>(&h),DIB_RGB_COLORS,&bits,nullptr,0);
    if(!color){DeleteDC(dc);ReleaseDC(nullptr,screen);return nullptr;}
    auto pixels=static_cast<DWORD*>(bits);
    for(int y=0;y<size;++y)for(int x=0;x<size;++x){double dx=x-size/2.0,dy=y-size/2.0;bool inside=dx*dx+dy*dy<size*size*.23;
        bool bars=(x>size*.32 && x<size*.44 || x>size*.56 && x<size*.68) && y>size*.28 && y<size*.72;
        pixels[y*size+x]=inside ? (bars?0xfff5ffff:0xff147d70):0;
    }
    HBITMAP mask=CreateBitmap(size,size,1,1,nullptr);ICONINFO i{TRUE,0,0,mask,color};HICON icon=CreateIconIndirect(&i);
    DeleteObject(mask);DeleteObject(color);DeleteDC(dc);ReleaseDC(nullptr,screen);return icon;
}
std::wstring durationText(Ms ms) {
    auto s=(std::max<Ms>(0,ms)+999)/1000;wchar_t b[64]{};
    swprintf_s(b,L"%02lld:%02lld",s/60,s%60);return b;
}
std::wstring kindText(Kind k) {return k==Kind::Eyes?L"Отдых для глаз":k==Kind::Stretch?L"Короткая разминка":L"Полноценный перерыв";}
}
