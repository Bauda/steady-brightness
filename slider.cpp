#include "slider.h"
#include <commctrl.h>
#include <windowsx.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <cmath>
#include <new>
#include <atomic>

#pragma comment(lib,"uiautomationcore.lib")

namespace {
SliderPaletteCallback paletteCallback;
constexpr UINT SetAccessibleValue=WM_APP+40;
class SliderProvider;
struct State {
    int minimum=0,maximum=100,value=0,line=1,page=10,grabOffset=0;
    bool dragging=false;
    SliderProvider* provider=nullptr;
};
State* Data(HWND w){return (State*)GetWindowLongPtrW(w,GWLP_USERDATA);}
float Scale(HWND w,float value){return value*GetDpiForWindow(w)/96.0f;}
struct Geometry { float left,right,y,radius; };
Geometry Metrics(HWND w) {
    RECT rc;GetClientRect(w,&rc);
    float inset=Scale(w,12.0f);
    return {inset,std::max(inset,(float)rc.right-inset-1.0f),rc.bottom/2.0f,Scale(w,9.0f)};
}
float ThumbX(HWND w,const State& s){auto m=Metrics(w);return m.left+(m.right-m.left)*(s.value-s.minimum)/std::max(1,s.maximum-s.minimum);}
RECT ThumbRect(HWND w,const State& s){auto m=Metrics(w);float x=ThumbX(w,s);return {(LONG)std::floor(x-m.radius),(LONG)std::floor(m.y-m.radius),(LONG)std::ceil(x+m.radius),(LONG)std::ceil(m.y+m.radius)};}
void Notify(HWND w,UINT code){SendMessageW(GetParent(w),WM_HSCROLL,MAKEWPARAM(code,Data(w)->value),(LPARAM)w);}
void EndDrag(HWND w,State& s){if(!s.dragging)return;s.dragging=false;if(GetCapture()==w)ReleaseCapture();Notify(w,TB_ENDTRACK);InvalidateRect(w,nullptr,FALSE);}

// A small software rasterizer: opaque DIB, analytic one-pixel edge coverage,
// no GPU context, graphics runtime, retained bitmap, animation, or idle timer.
struct Canvas {
    int width,height;DWORD* pixels=nullptr;HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;
    Canvas(HDC target,int w,int h):width(w),height(h){
        if(w<=0||h<=0)return;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        bitmap=CreateDIBSection(target,&info,DIB_RGB_COLORS,(void**)&pixels,nullptr,0);
        if(bitmap){dc=CreateCompatibleDC(target);if(dc)old=SelectObject(dc,bitmap);}
    }
    ~Canvas(){if(old)SelectObject(dc,old);if(dc)DeleteDC(dc);if(bitmap)DeleteObject(bitmap);}
    static DWORD Color(COLORREF c){return 0xff000000u|((DWORD)GetRValue(c)<<16)|((DWORD)GetGValue(c)<<8)|GetBValue(c);}
    void Clear(COLORREF c){std::fill(pixels,pixels+(size_t)width*height,Color(c));}
    void Blend(int x,int y,COLORREF color,float coverage){
        if(coverage<=0)return;unsigned a=(unsigned)(std::min(coverage,1.0f)*256.0f+0.5f),b=256-a;
        DWORD& p=pixels[(size_t)y*width+x];
        unsigned r=(((p>>16)&255)*b+GetRValue(color)*a+128)>>8;
        unsigned g=(((p>>8)&255)*b+GetGValue(color)*a+128)>>8;
        unsigned blue=((p&255)*b+GetBValue(color)*a+128)>>8;
        p=0xff000000u|(r<<16)|(g<<8)|blue;
    }
    void Capsule(float left,float right,float cy,float radius,COLORREF color){
        if(right<left)return;
        int x0=std::max(0,(int)std::floor(left-radius-1)),x1=std::min(width,(int)std::ceil(right+radius+1));
        int y0=std::max(0,(int)std::floor(cy-radius-1)),y1=std::min(height,(int)std::ceil(cy+radius+1));
        for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){float px=x+0.5f,dy=y+0.5f-cy,dx=px-std::clamp(px,left,right);Blend(x,y,color,radius+0.5f-std::sqrt(dx*dx+dy*dy));}
    }
    void Circle(float x,float y,float radius,COLORREF color){Capsule(x,x,y,radius,color);}
};
void Paint(HWND w,HDC target,const State& s){
    RECT rc;GetClientRect(w,&rc);auto p=paletteCallback();Canvas canvas(target,rc.right,rc.bottom);
    if(!canvas.pixels||!canvas.dc){SetDCBrushColor(target,p.background);FillRect(target,&rc,(HBRUSH)GetStockObject(DC_BRUSH));return;}
    canvas.Clear(p.background);auto m=Metrics(w);float x=ThumbX(w,s);bool enabled=IsWindowEnabled(w)!=FALSE;
    float trackRadius=Scale(w,2.5f);
    canvas.Capsule(m.left,m.right,m.y,trackRadius,p.track);
    if(enabled&&s.value>s.minimum)canvas.Capsule(m.left,x,m.y,trackRadius,p.accent);
    if(GetFocus()==w && !(SendMessageW(w,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)){
        canvas.Circle(x,m.y,Scale(w,11.5f),p.accent);canvas.Circle(x,m.y,Scale(w,10.3f),p.background);
    }
    canvas.Circle(x,m.y,m.radius,p.border);
    canvas.Circle(x,m.y,m.radius-Scale(w,0.8f),p.thumb);
    canvas.Circle(x,m.y,Scale(w,s.dragging?5.2f:4.3f),enabled?p.accent:p.disabled);
    BitBlt(target,0,0,rc.right,rc.bottom,canvas.dc,0,0,SRCCOPY);
}

class SliderProvider final:public IRawElementProviderSimple,public IRangeValueProvider {
    LONG refs=1;std::atomic<HWND> window;
public:
    explicit SliderProvider(HWND w):window(w){}
    void Disconnect(){window=nullptr;UiaDisconnectProvider(this);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(IRawElementProviderSimple))*out=static_cast<IRawElementProviderSimple*>(this);
        else if(iid==__uuidof(IRangeValueProvider))*out=static_cast<IRangeValueProvider*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return (ULONG)InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release()override{ULONG n=(ULONG)InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value)override{if(!value)return E_POINTER;*value=ProviderOptions_ServerSideProvider;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id,IUnknown** out)override{if(!out)return E_POINTER;*out=nullptr;if(id==UIA_RangeValuePatternId){*out=static_cast<IRangeValueProvider*>(this);AddRef();}return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id,VARIANT* out)override{
        if(!out)return E_POINTER;VariantInit(out);if(!window)return UIA_E_ELEMENTNOTAVAILABLE;
        if(id==UIA_ControlTypePropertyId){out->vt=VT_I4;out->lVal=UIA_SliderControlTypeId;}
        else if(id==UIA_NamePropertyId){wchar_t text[256]{};GetWindowTextW(window,text,ARRAYSIZE(text));out->vt=VT_BSTR;out->bstrVal=SysAllocString(text);}
        else if(id==UIA_IsKeyboardFocusablePropertyId||id==UIA_IsControlElementPropertyId||id==UIA_IsContentElementPropertyId){out->vt=VT_BOOL;out->boolVal=VARIANT_TRUE;}
        else if(id==UIA_IsEnabledPropertyId){out->vt=VT_BOOL;out->boolVal=IsWindowEnabled(window)?VARIANT_TRUE:VARIANT_FALSE;}
        else if(id==UIA_OrientationPropertyId){out->vt=VT_I4;out->lVal=OrientationType_Horizontal;}
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out)override{if(!out)return E_POINTER;*out=nullptr;return window?UiaHostProviderFromHwnd(window,out):UIA_E_ELEMENTNOTAVAILABLE;}
    HRESULT STDMETHODCALLTYPE SetValue(double value)override{
        if(!window)return UIA_E_ELEMENTNOTAVAILABLE;if(!IsWindowEnabled(window))return UIA_E_ELEMENTNOTENABLED;
        if(!std::isfinite(value)||value<0||value>100)return E_INVALIDARG;
        SendMessageW(window,SetAccessibleValue,(WPARAM)(int)std::lround(value),0);return S_OK;
    }
    HRESULT Read(UINT message,double* value){if(!value)return E_POINTER;if(!window)return UIA_E_ELEMENTNOTAVAILABLE;*value=(double)SendMessageW(window,message,0,0);return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Value(double* value)override{return Read(TBM_GETPOS,value);}
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* value)override{if(!value)return E_POINTER;if(!window)return UIA_E_ELEMENTNOTAVAILABLE;*value=!IsWindowEnabled(window);return S_OK;}
    HRESULT STDMETHODCALLTYPE get_Maximum(double* value)override{return Read(TBM_GETRANGEMAX,value);}
    HRESULT STDMETHODCALLTYPE get_Minimum(double* value)override{return Read(TBM_GETRANGEMIN,value);}
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* value)override{return Read(TBM_GETPAGESIZE,value);}
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* value)override{return Read(TBM_GETLINESIZE,value);}
};
void ValueChanged(State& s,int before){
    if(s.provider&&before!=s.value){VARIANT a{},b{};a.vt=b.vt=VT_R8;a.dblVal=before;b.dblVal=s.value;UiaRaiseAutomationPropertyChangedEvent(s.provider,UIA_RangeValueValuePropertyId,a,b);}
}
void SetPosition(HWND w,State& s,int value,bool redraw){int before=s.value;s.value=std::clamp(value,s.minimum,s.maximum);ValueChanged(s,before);if(redraw)InvalidateRect(w,nullptr,FALSE);}
void Point(HWND w,State& s,int mouseX){
    auto m=Metrics(w);if(m.right<=m.left)return;
    float fraction=std::clamp((mouseX-s.grabOffset-m.left)/(m.right-m.left),0.0f,1.0f);
    int value=s.minimum+(int)std::lround(fraction*(s.maximum-s.minimum));
    if(value!=s.value){SetPosition(w,s,value,true);UpdateWindow(w);Notify(w,TB_THUMBTRACK);}
}
LRESULT CALLBACK Proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_NCCREATE){auto* s=new(std::nothrow)State;if(!s)return FALSE;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)s);}
    State* s=Data(w);if(!s)return DefWindowProcW(w,msg,wp,lp);
    switch(msg){
    case WM_GETDLGCODE:return DLGC_WANTARROWS;
    case WM_GETOBJECT:
        if((LONG)lp==UiaRootObjectId){if(!s->provider)s->provider=new(std::nothrow)SliderProvider(w);if(s->provider)return UiaReturnRawElementProvider(w,wp,lp,s->provider);}break;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);Paint(w,dc,*s);EndPaint(w,&ps);return 0;}
    case WM_PRINTCLIENT:Paint(w,(HDC)wp,*s);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_ENABLE:
        if(!wp)EndDrag(w,*s);InvalidateRect(w,nullptr,FALSE);
        if(s->provider){VARIANT before{},after{};before.vt=after.vt=VT_BOOL;before.boolVal=wp?VARIANT_FALSE:VARIANT_TRUE;after.boolVal=wp?VARIANT_TRUE:VARIANT_FALSE;UiaRaiseAutomationPropertyChangedEvent(s->provider,UIA_IsEnabledPropertyId,before,after);}return 0;
    case WM_SETFOCUS:
        if(s->provider)UiaRaiseAutomationEvent(s->provider,UIA_AutomationFocusChangedEventId);
        InvalidateRect(w,nullptr,FALSE);break;
    case WM_KILLFOCUS:InvalidateRect(w,nullptr,FALSE);break;
    case WM_UPDATEUISTATE:{LRESULT result=DefWindowProcW(w,msg,wp,lp);InvalidateRect(w,nullptr,FALSE);return result;}
    case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:{
        if(!IsWindowEnabled(w))return 0;
        SendMessageW(GetParent(w),WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);SetFocus(w);
        RECT thumb=ThumbRect(w,*s);POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};bool inside=PtInRect(&thumb,point)!=FALSE;
        s->grabOffset=inside?point.x-(int)std::lround(ThumbX(w,*s)):0;s->dragging=true;SetCapture(w);
        if(!inside)Point(w,*s,point.x);InvalidateRect(w,nullptr,FALSE);UpdateWindow(w);return 0;
    }
    case WM_MOUSEMOVE:if(s->dragging)Point(w,*s,GET_X_LPARAM(lp));return 0;
    case WM_LBUTTONUP:if(s->dragging){Point(w,*s,GET_X_LPARAM(lp));EndDrag(w,*s);}return 0;
    case WM_CAPTURECHANGED:if(s->dragging&&(HWND)lp!=w)EndDrag(w,*s);return 0;
    case WM_CANCELMODE:EndDrag(w,*s);return 0;
    case WM_KEYDOWN:{
        if(!IsWindowEnabled(w))return 0;int value=s->value;UINT code=0;
        switch(wp){case VK_LEFT:case VK_UP:value-=s->line;code=TB_LINEUP;break;case VK_RIGHT:case VK_DOWN:value+=s->line;code=TB_LINEDOWN;break;
        case VK_PRIOR:value-=s->page;code=TB_PAGEUP;break;case VK_NEXT:value+=s->page;code=TB_PAGEDOWN;break;
        case VK_HOME:value=s->minimum;code=TB_TOP;break;case VK_END:value=s->maximum;code=TB_BOTTOM;break;default:return DefWindowProcW(w,msg,wp,lp);}
        SendMessageW(GetParent(w),WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);SetPosition(w,*s,value,true);Notify(w,code);return 0;
    }
    case WM_KEYUP:if(wp==VK_LEFT||wp==VK_RIGHT||wp==VK_UP||wp==VK_DOWN||wp==VK_PRIOR||wp==VK_NEXT||wp==VK_HOME||wp==VK_END){Notify(w,TB_ENDTRACK);return 0;}break;
    case TBM_GETPOS:return s->value;
    case TBM_SETPOS:SetPosition(w,*s,(int)lp,wp!=0);return 0;
    case TBM_SETRANGE:s->minimum=(short)LOWORD(lp);s->maximum=std::max(s->minimum,(int)(short)HIWORD(lp));SetPosition(w,*s,s->value,wp!=0);return 0;
    case TBM_GETRANGEMIN:return s->minimum;
    case TBM_GETRANGEMAX:return s->maximum;
    case TBM_SETLINESIZE:{int old=s->line;s->line=std::max(1,(int)lp);return old;}
    case TBM_GETLINESIZE:return s->line;
    case TBM_SETPAGESIZE:{int old=s->page;s->page=std::max(1,(int)lp);return old;}
    case TBM_GETPAGESIZE:return s->page;
    case TBM_GETTHUMBRECT:if(lp)*(RECT*)lp=ThumbRect(w,*s);return 0;
    case TBM_GETCHANNELRECT:if(lp){auto m=Metrics(w);*(RECT*)lp={(LONG)std::floor(m.left-m.radius),(LONG)std::floor(m.y-Scale(w,2.5f)),(LONG)std::ceil(m.right+m.radius),(LONG)std::ceil(m.y+Scale(w,2.5f))};}return 0;
    case SetAccessibleValue:if(IsWindowEnabled(w)){SetPosition(w,*s,(int)wp,true);Notify(w,TB_ENDTRACK);}return 0;
    case WM_NCDESTROY:
        s->dragging=false;if(GetCapture()==w)ReleaseCapture();if(s->provider){s->provider->Disconnect();s->provider->Release();}
        SetWindowLongPtrW(w,GWLP_USERDATA,0);delete s;break;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
}
bool RegisterBrightnessSlider(HINSTANCE instance,SliderPaletteCallback palette){paletteCallback=palette;WNDCLASSEXW c{sizeof(c)};c.lpfnWndProc=Proc;c.hInstance=instance;c.hCursor=LoadCursorW(nullptr,IDC_HAND);c.lpszClassName=BrightnessSliderClass;c.style=CS_DBLCLKS;return RegisterClassExW(&c)!=0;}
HWND CreateBrightnessSlider(HINSTANCE instance,HWND parent,UINT id,const wchar_t* name){return CreateWindowExW(0,BrightnessSliderClass,name,WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,0,0,parent,(HMENU)(INT_PTR)id,instance,nullptr);}
bool BrightnessSliderDragging(HWND window){auto* s=Data(window);return s&&s->dragging;}
void CancelBrightnessSliderDrag(HWND window){SendMessageW(window,WM_CANCELMODE,0,0);}
