#pragma once
#include "platform.h"
#include "storage.h"
#include <memory>
#include <shellapi.h>

namespace breakly {
class View;
constexpr UINT openMessage=WM_APP+20;
constexpr UINT trayMessage=WM_APP+21;
enum Command { ShowMain=100,CloseMain,TestNotify,Exit,CloseNotification,ShowSettings,StartNow,Pause15,Pause30,Pause60,Resume };
class App final : public INotifications {
public:
    App(std::filesystem::path root,bool background,bool diagnostics);
    ~App();
    int run();
    Scheduler scheduler;
    SystemClock clock;
    FileStore store;
    HWND host=nullptr;
    HICON icon=nullptr;
    std::unique_ptr<View> main,notification;
    void show(Kind) override;
    void openMain(int page=0);
    void closeMain();
    void closeNotification(bool dismiss=false);
    void act(int);
    void update();
    bool saveSettings(const Settings&);
    bool isolated() const {return isolated_;}
    std::wstring hostTitle() const {return L"Breakly.Host."+std::to_wstring(std::hash<std::wstring>{}(store.root().wstring()));}
    std::wstring statusText() const;
private:
    bool background_,diagnostics_,isolated_=false,quitting_=false,displayOff_=false;
    NOTIFYICONDATAW tray_{};
    UINT taskbarCreated_=0;
    Ms nextFlush_=0,shownAt_=0,deferUntil_=0;
    HPOWERNOTIFY power_=nullptr;
    LARGE_INTEGER startup_{};
    std::wstring lastTip_;
    bool trayReady_=false,startupWritten_=false;
    void tray(bool add);
    void menu();
    void flush();
    void plan();
    static LRESULT CALLBACK proc(HWND,UINT,WPARAM,LPARAM);
};
}
