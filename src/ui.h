#pragma once
#include "app.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <vector>

namespace breakly {
class View {
public:
    View(App&,bool notification,int page=0);
    ~View();
    HWND hwnd=nullptr;
    void refresh(bool initial=false);
    void activate();
    void setPage(int);
    bool isNotification() const {return notification_;}
private:
    App& app_;
    bool notification_=false,dark_=false,creating_=false;
    int page_=0,scroll_=0,scrollX_=0,contentHeight_=620;
    UINT dpi_=96;
    HBRUSH background_=nullptr;
    HFONT font_=nullptr,heading_=nullptr,timer_=nullptr;
    COLORREF bg_=0,fg_=0,muted_=0,accent_=0;
    HWND tabs_=nullptr;
    std::vector<HWND> controls_;
    struct Placement {HWND w;int x,y,width,height;};
    std::vector<Placement> placements_;
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> brand_;
    int px(int) const;
    void theme();
    void build(bool preserve=false);
    void layout();
    void paint();
    HWND control(const wchar_t*,const std::wstring&,DWORD,int,int,int,int,int);
    HWND label(const std::wstring&,int,int,int,int,bool heading=false);
    void button(const wchar_t*,int,int,int,int,int=36);
    void check(const wchar_t*,int,bool,int);
    void field(const wchar_t*,int,int,int);
    void timeField(const wchar_t*,int,int,int);
    void combo(const wchar_t*,int,const std::vector<std::wstring>&,int,int);
    void save();
    void command(int);
    static LRESULT CALLBACK proc(HWND,UINT,WPARAM,LPARAM);
};
}
