#include "common.h"
#include "slider.h"
#include "brightness-memory.h"
#include <dbt.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <oleacc.h>
#include <stdio.h>
#include <memory>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
constexpr wchar_t AppName[] = L"Steady Brightness";
constexpr wchar_t HostClass[] = L"SteadyBrightness.Host.1";
constexpr UINT TrayMessage = WM_APP + 1, ToggleMessage = WM_APP + 2;
constexpr UINT BusyTimer = 1, DragTimer = 2, TopologyTimer = 3, IconTimer = 4;
constexpr UINT RefreshId = 20, OptionsId = 21, SliderBase = 100;
constexpr DWORD TimeoutMs = 9000;
HINSTANCE instance;
HWND host, popup, refreshButton, optionsButton, generalStatus;
HFONT bodyFont, valueFont, smallFont;
HBRUSH background;
enum Theme { FollowWindows, Light, Dark };
Theme theme=FollowWindows;
bool dark=false,highContrastMode=false;
COLORREF backgroundColor,foreground,muted,trackColor,accent,buttonColor,borderColor;
HICON icon;
UINT taskbarCreated;
UINT dpi = 96;
bool menuActive = false, scanning = false, needScan = false, pending = false, testMode = false;
bool demo = false, faultHang = false, faultCrash = false;
bool restoreBrightness=true, activeRestore=false, topologyTimerArmed=false;
int recoveryPasses=0;
BrightnessMemory brightnessMemory(L"Software\\SteadyBrightness");
std::wstring demoSettingsKey;
int demoValues[2]{55,40}, demoScanCount=0, demoUnavailable=0;
bool demoReversed=false, demoLateReset=false;
ULONGLONG started = 0;
int activeIndex = -1;
LONG activeCommand = 0;
int iconRetries = 0;
HANDLE instanceMutex;
HPOWERNOTIFY powerNotification;
HDEVNOTIFY monitorNotification;
DWORD lastConsoleDisplayState=MAXDWORD;
constexpr GUID ConsoleDisplayState={0x6fe69556,0x704a,0x47a0,{0x8f,0x24,0xc2,0x8d,0x93,0x6f,0xda,0x47}};
// GUID_DEVINTERFACE_MONITOR from the Windows SDK (ntddvdeo.h).
constexpr GUID MonitorInterface={0xe6f07b5f,0xee97,0x4a90,{0xb0,0x76,0x33,0xf5,0x7b,0xf4,0xea,0xa7}};
NOTIFYICONDATAW tray{};
struct Row {
    MonitorData data{};
    HWND name = nullptr, value = nullptr, slider = nullptr, status = nullptr;
    int desired = -1;
    bool restoring=false, persistentUnique=false;
    ULONGLONG due = 0;
};
std::vector<Row> rows;
struct MenuEntry {
    MSAAMENUINFO accessible{};
    UINT id;const wchar_t* text;bool checked,separator;
    MenuEntry(UINT command,const wchar_t* label,bool isChecked=false,bool isSeparator=false)
        :id(command),text(label),checked(isChecked),separator(isSeparator) {
        accessible.dwMSAASignature=MSAA_MENU_SIG;accessible.cchWText=(DWORD)wcslen(label);accessible.pszWText=const_cast<wchar_t*>(label);
    }
};

int Px(int n) { return MulDiv(n, (int)dpi, 96); }
DWORD ReadDword(const wchar_t* key,const wchar_t* name,DWORD fallback) {
    DWORD value=fallback,bytes=sizeof(value);
    if(RegGetValueW(HKEY_CURRENT_USER,key,name,RRF_RT_REG_DWORD,nullptr,&value,&bytes)!=ERROR_SUCCESS)return fallback;
    return value;
}
void Fill(HDC dc,const RECT& rect,COLORREF color) {
    SetDCBrushColor(dc,color);FillRect(dc,&rect,(HBRUSH)GetStockObject(DC_BRUSH));
}
SliderPalette CurrentSliderPalette(){return {backgroundColor,trackColor,accent,highContrastMode?foreground:(dark?RGB(240,245,252):RGB(255,255,255)),borderColor,muted};}
void ApplyTheme() {
    dark=theme==Dark || (theme==FollowWindows && ReadDword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",1)==0);
    HIGHCONTRASTW contrast{sizeof(contrast)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);
    bool highContrast=(contrast.dwFlags&HCF_HIGHCONTRASTON)!=0;
    highContrastMode=highContrast;
    COLORREF bg=dark?RGB(28,31,37):RGB(250,251,253);
    foreground=dark?RGB(235,239,245):RGB(35,42,52);
    muted=dark?RGB(171,182,200):RGB(91,104,123);
    trackColor=dark?RGB(65,73,86):RGB(212,220,231);
    accent=dark?RGB(112,181,255):RGB(0,106,211);
    buttonColor=dark?RGB(42,47,56):RGB(255,255,255);
    borderColor=dark?RGB(88,99,116):RGB(186,197,212);
    if(highContrast) {
        bg=GetSysColor(COLOR_WINDOW);foreground=GetSysColor(COLOR_WINDOWTEXT);muted=foreground;
        trackColor=GetSysColor(COLOR_GRAYTEXT);accent=GetSysColor(COLOR_HIGHLIGHT);buttonColor=GetSysColor(COLOR_BTNFACE);borderColor=foreground;
    }
    backgroundColor=bg;if(background)DeleteObject(background);background=CreateSolidBrush(bg);
    if(popup) {
        SetClassLongPtrW(popup,GCLP_HBRBACKGROUND,(LONG_PTR)background);
        BOOL useDark=dark&&!highContrast;DwmSetWindowAttribute(popup,20,&useDark,sizeof(useDark));
        RedrawWindow(popup,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    }
}
void SetTheme(Theme value) {
    HKEY key;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\SteadyBrightness",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS) {
        DWORD mode=(DWORD)value;
        LONG result=RegSetValueExW(key,L"Theme",0,REG_DWORD,(BYTE*)&mode,sizeof(mode));RegCloseKey(key);
        if(result!=ERROR_SUCCESS)MessageBoxW(popup,L"The theme will apply now, but Windows could not save this preference.",AppName,MB_ICONINFORMATION);
    }
    theme=value;ApplyTheme();
}
std::wstring Executable() {
    wchar_t path[32768]; DWORD n = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    return n && n < ARRAYSIZE(path) ? std::wstring(path, n) : L"";
}
std::wstring Known(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr; std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) { result = p; CoTaskMemFree(p); }
    return result;
}
bool AutoStart() {
    HKEY key; wchar_t data[32768]; DWORD bytes = sizeof(data), type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    bool yes = RegQueryValueExW(key, L"SteadyBrightness", nullptr, &type, (BYTE*)data, &bytes) == ERROR_SUCCESS;
    RegCloseKey(key); return yes;
}
void SetAutoStart(bool enable) {
    HKEY key;
    LONG error = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error == ERROR_SUCCESS) {
        if (enable) {
            std::wstring command = L"\"" + Executable() + L"\" --background";
            error = RegSetValueExW(key, L"SteadyBrightness", 0, REG_SZ, (const BYTE*)command.c_str(), (DWORD)((command.size() + 1) * sizeof(wchar_t)));
        } else { error = RegDeleteValueW(key, L"SteadyBrightness"); if (error == ERROR_FILE_NOT_FOUND) error = ERROR_SUCCESS; }
        RegCloseKey(key);
    }
    if (error != ERROR_SUCCESS) MessageBoxW(popup, L"Windows could not update the startup preference.", AppName, MB_ICONERROR);
}

class Worker {
public:
    HANDLE mapping = nullptr, command = nullptr, done = nullptr, process = nullptr, job = nullptr;
    Shared* shared = nullptr;
    ~Worker() { Stop(); }
    void Stop() {
        if (process) {
            if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
                if (!pending && shared && command) { shared->command = Quit; SetEvent(command); }
                if (WaitForSingleObject(process, 200) == WAIT_TIMEOUT) {
                    TerminateProcess(process, ERROR_TIMEOUT); WaitForSingleObject(process, 500);
                }
            }
            CloseHandle(process); process = nullptr;
        }
        if (job) { CloseHandle(job); job = nullptr; }
        if (shared) { UnmapViewOfFile(shared); shared = nullptr; }
        if (mapping) { CloseHandle(mapping); mapping = nullptr; }
        if (command) { CloseHandle(command); command = nullptr; }
        if (done) { CloseHandle(done); done = nullptr; }
    }
    bool Start() {
        if (process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT) return true;
        Stop();
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(Shared), nullptr);
        command = CreateEventW(&sa, FALSE, FALSE, nullptr); done = CreateEventW(&sa, FALSE, FALSE, nullptr);
        HANDLE parent = nullptr;
        DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &parent, SYNCHRONIZE, TRUE, 0);
        if (!mapping || !command || !done || !parent) { if (parent) CloseHandle(parent); Stop(); return false; }
        shared = (Shared*)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared));
        if (!shared) { CloseHandle(parent); Stop(); return false; }
        ZeroMemory(shared, sizeof(*shared)); shared->version = ProtocolVersion;
        wchar_t commandLine[33000];
        swprintf_s(commandLine, L"\"%s\" --worker %llu %llu %llu %llu", Executable().c_str(),
            (unsigned long long)(uintptr_t)mapping, (unsigned long long)(uintptr_t)command,
            (unsigned long long)(uintptr_t)done, (unsigned long long)(uintptr_t)parent);
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<BYTE> buffer(bytes);
        auto* attributes = (PPROC_THREAD_ATTRIBUTE_LIST)buffer.data();
        bool init = InitializeProcThreadAttributeList(attributes, 1, 0, &bytes) != FALSE;
        HANDLE inherited[]{mapping, command, done, parent};
        bool updated = init && UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited, sizeof(inherited), nullptr, nullptr) != FALSE;
        STARTUPINFOEXW si{}; si.StartupInfo.cb = sizeof(si); si.lpAttributeList = attributes;
        PROCESS_INFORMATION pi{};
        bool created = updated && CreateProcessW(Executable().c_str(), commandLine, nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si.StartupInfo, &pi) != FALSE;
        if (init) DeleteProcThreadAttributeList(attributes);
        CloseHandle(parent);
        if (!created) { Stop(); return false; }
        process = pi.hProcess; CloseHandle(pi.hThread);
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (job && (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) || !AssignProcessToJobObject(job, process))) {
            CloseHandle(job); job = nullptr;
        }
        return true;
    }
} worker;

void Text(HWND control, const wchar_t* value) { if (control) SetWindowTextW(control, value); }
HWND Label(const wchar_t* text, HFONT font, DWORD style = 0) {
    HWND w = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_NOPREFIX | style, 0, 0, 0, 0, popup, nullptr, instance, nullptr);
    SendMessageW(w, WM_SETFONT, (WPARAM)font, TRUE); return w;
}
void DestroyRows() {
    for (auto& r : rows) { DestroyWindow(r.name); DestroyWindow(r.value); DestroyWindow(r.slider); DestroyWindow(r.status); }
    rows.clear();
}
void PositionPopup();
void Layout() {
    int width = Px(376), top = 20, rowHeight = 102;
    int count = (int)rows.size();
    int maxHeight = 850;
    MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromWindow(popup, MONITOR_DEFAULTTONEAREST), &mi);
    maxHeight = std::max(150, MulDiv(mi.rcWork.bottom - mi.rcWork.top, 96, (int)dpi) - 30);
    int height = std::min(maxHeight, 96 + rowHeight * count);
    // Rows scroll using a standard vertical scrollbar when many displays are present.
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
    info.nMin = 0; info.nMax = std::max(0, count * rowHeight - 1); info.nPage = (UINT)std::max(1, height - 96);
    SCROLLINFO old{sizeof(old), SIF_POS}; GetScrollInfo(popup, SB_VERT, &old); info.nPos = old.nPos;
    SetScrollInfo(popup, SB_VERT, &info, TRUE); info.fMask = SIF_POS; GetScrollInfo(popup, SB_VERT, &info);
    RECT rect{0,0,width,Px(height)};
    AdjustWindowRectEx(&rect, (DWORD)GetWindowLongPtrW(popup, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtrW(popup, GWL_EXSTYLE));
    SetWindowPos(popup, nullptr, 0,0,rect.right-rect.left,rect.bottom-rect.top, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    RECT client{}; GetClientRect(popup,&client); width = client.right;
    for (int i=0;i<count;++i) {
        auto& r = rows[i]; int y = top + i * rowHeight - info.nPos;
        bool visible = y >= top && y + rowHeight <= height - 62;
        ShowWindow(r.name, visible ? SW_SHOWNA : SW_HIDE); ShowWindow(r.value, visible ? SW_SHOWNA : SW_HIDE);
        ShowWindow(r.slider, visible ? SW_SHOWNA : SW_HIDE); ShowWindow(r.status, visible ? SW_SHOWNA : SW_HIDE);
        MoveWindow(r.name, Px(22), Px(y), width - Px(110), Px(24), TRUE);
        MoveWindow(r.value, width - Px(80), Px(y-1), Px(58), Px(27), TRUE);
        MoveWindow(r.slider, Px(18), Px(y+28), width - Px(36), Px(30), TRUE);
        MoveWindow(r.status, Px(22), Px(y+65), width - Px(44), Px(25), TRUE);
    }
    MoveWindow(generalStatus, Px(22), Px(height - 72), width - Px(44), Px(22), TRUE);
    MoveWindow(refreshButton, Px(22), Px(height - 43), Px(100), Px(28), TRUE);
    MoveWindow(optionsButton, width - Px(122), Px(height - 43), Px(100), Px(28), TRUE);
    InvalidateRect(popup, nullptr, TRUE);
}
void MakeRows(const MonitorData* monitors, DWORD count) {
    DestroyRows(); count = std::min<DWORD>(count, MaxMonitors); rows.resize(count);
    for (DWORD i = 0; i < count; ++i) {
        auto& r = rows[i]; r.data = monitors[i];
        r.persistentUnique=BrightnessMemory::Unique(monitors,count,i);
        std::wstring name = std::to_wstring(i+1) + L". " + r.data.name;
        if (r.data.primary) name += L" (main)";
        r.name = Label(name.c_str(), bodyFont, SS_ENDELLIPSIS);
        r.value = Label(L"", valueFont, SS_RIGHT);
        r.status = Label(r.data.note, smallFont, SS_ENDELLIPSIS);
        r.slider = CreateBrightnessSlider(instance,popup,SliderBase+i,name.c_str());
        SendMessageW(r.slider, TBM_SETRANGE, FALSE, MAKELPARAM(0,100));
        SendMessageW(r.slider, TBM_SETPAGESIZE, 0, 10); SendMessageW(r.slider, TBM_SETLINESIZE, 0, 1);
        SendMessageW(r.slider, TBM_SETPOS, TRUE, Percent(r.data));
        EnableWindow(r.slider, r.data.backend != Unavailable);
        wchar_t percent[20]; swprintf_s(percent, L"%d%%", Percent(r.data)); Text(r.value, r.data.backend ? percent : L"--");
    }
    Text(generalStatus, count ? L"Changes are checked with your monitor." : L"No active displays found. Click Refresh to retry.");
    Layout(); if (IsWindowVisible(popup)) PositionPopup();
}
void RememberRows() {
    for(size_t i=0;i<rows.size();++i) {
        auto& row=rows[i];
        if(!row.persistentUnique)continue;
        int saved=0;
        if(!brightnessMemory.Read(row.data,saved) || !restoreBrightness) {
            if(!brightnessMemory.Save(row.data,Percent(row.data)))Text(row.status,L"Could not remember brightness for this monitor.");
        } else if(saved!=Percent(row.data)) {
            row.desired=saved;row.restoring=true;row.due=GetTickCount64();
            SendMessageW(row.slider,TBM_SETPOS,TRUE,saved);
            wchar_t text[20];swprintf_s(text,L"%d%%",saved);Text(row.value,text);
            Text(row.status,L"Restoring saved brightness...");
        }
    }
}
void ArmTopologyTimer(UINT delay) {
    SetTimer(host,TopologyTimer,demo?30:delay,nullptr);topologyTimerArmed=true;
}
void RetryRecovery() {
    if(recoveryPasses>0 && !topologyTimerArmed)ArmTopologyTimer(2500);
}
void QueueRecovery() {
    if(testMode && !demo)return;
    if(!restoreBrightness && !IsWindowVisible(popup))return;
    recoveryPasses=3;ArmTopologyTimer(1400);
}
void ReadDemo() {
    ++demoScanCount;
    if(demoLateReset && recoveryPasses==1){demoValues[1]=100;demoLateReset=false;}
    MonitorData monitors[2]{};
    for(int i=0;i<2;++i) {
        int which=demoReversed?1-i:i;
        Copy(monitors[i].name,which?L"C27JG5x":L"XV271U M3");
        Copy(monitors[i].persistentId,which?L"demo:samsung":L"demo:acer");
        swprintf_s(monitors[i].id,L"DISPLAY%d",i+1);
        monitors[i].backend=which && demoUnavailable>0?Unavailable:DdcVcp;
        monitors[i].maximum=100;monitors[i].current=(DWORD)demoValues[which];monitors[i].primary=which==0;
        Copy(monitors[i].note,L"Demo only - no monitor changed");
    }
    if(demoUnavailable>0)--demoUnavailable;
    MakeRows(monitors,2);RememberRows();
}
void AddIcon() {
    tray.cbSize = sizeof(tray); tray.hWnd = host; tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_GUID | NIF_SHOWTIP;
    tray.guidItem = {0xe2512460,0xb58d,0x48d3,{0x9a,0x30,0x40,0x63,0x21,0xab,0x91,0x18}};
    tray.uCallbackMessage = TrayMessage; tray.hIcon = icon; Copy(tray.szTip, AppName);
    if (Shell_NotifyIconW(NIM_ADD, &tray)) { tray.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &tray); KillTimer(host, IconTimer); iconRetries = 0; }
    else if (++iconRetries <= 12) SetTimer(host, IconTimer, 5000, nullptr);
}
void PositionPopup() {
    NOTIFYICONIDENTIFIER id{sizeof(id), host, 1, tray.guidItem}; RECT anchor{};
    POINT point; GetCursorPos(&point);
    if (SUCCEEDED(Shell_NotifyIconGetRect(&id, &anchor))) point = {anchor.right, anchor.top};
    MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &mi);
    RECT rect; GetWindowRect(popup,&rect); int w=rect.right-rect.left,h=rect.bottom-rect.top;
    int x=(int)std::clamp<LONG>(point.x-w,mi.rcWork.left,std::max<LONG>(mi.rcWork.left,mi.rcWork.right-w));
    int y=point.y-h-Px(10);
    if(y<mi.rcWork.top) y=(int)std::min<LONG>(mi.rcWork.bottom-h,point.y+Px(24));
    y=(int)std::max<LONG>(mi.rcWork.top,y);
    SetWindowPos(popup,HWND_TOPMOST,x,y,0,0,SWP_NOSIZE | SWP_NOACTIVATE);
}
void Dispatch();
void StartRequest(LONG command, int index = -1, int percent = 0) {
    if (pending) return;
    if (!worker.Start()) { Text(generalStatus,L"Could not start monitor helper. Click Refresh to retry.");RetryRecovery();return; }
    pending = true; activeCommand = command; activeIndex = index; started = GetTickCount64();
    if (command == Scan) { scanning = true; Text(generalStatus,L"Reading displays..."); EnableWindow(refreshButton,FALSE); }
    worker.shared->command = command; worker.shared->index = index; worker.shared->percent = percent;
    SetEvent(worker.command); SetTimer(host,BusyTimer,40,nullptr);
}
void ScheduleDrag() {
    ULONGLONG next = 0, now = GetTickCount64();
    for (auto& r : rows) if (r.desired >= 0) next = !next ? r.due : std::min(next, r.due);
    KillTimer(host,DragTimer);
    if (next && !pending) SetTimer(host,DragTimer,(UINT)std::max<ULONGLONG>(1,next>now?next-now:1),nullptr);
}
void Dispatch() {
    if (pending) return;
    if (needScan) {
        needScan=false;
        for(auto& r:rows) r.desired=-1;
        StartRequest(Scan); return;
    }
    ULONGLONG now=GetTickCount64();
    for(size_t i=0;i<rows.size();++i) if(rows[i].desired>=0 && rows[i].due<=now) {
        int target=rows[i].desired; rows[i].desired=-1;
        if(demo) {
            rows[i].data.current=Raw(rows[i].data,target); Text(rows[i].status,L"Demo only - no monitor changed");
            demoValues[!_wcsicmp(rows[i].data.persistentId,L"demo:samsung")?1:0]=target;
            if(!rows[i].restoring && rows[i].persistentUnique)brightnessMemory.Save(rows[i].data,target);
            rows[i].restoring=false;
            continue;
        }
        if (!worker.process) { needScan=true; Dispatch(); return; }
        activeRestore=rows[i].restoring;rows[i].restoring=false;
        StartRequest(Set,(int)i,target); return;
    }
    ScheduleDrag();
    bool queued=false;for(auto& r:rows) if(r.desired>=0) queued=true;
    if(!queued && !IsWindowVisible(popup)) worker.Stop();
    if(!queued)RetryRecovery();
}
void Complete(bool failed, const wchar_t* reason=nullptr) {
    KillTimer(host,BusyTimer);
    LONG command=activeCommand; int index=activeIndex;
    pending=false; scanning=false;
    EnableWindow(refreshButton,TRUE);
    if(failed) {
        worker.Stop();
        for(auto& r:rows) {r.desired=-1; EnableWindow(r.slider,FALSE); Text(r.status,L"Connection lost. Click Refresh to reconnect.");}
        Text(generalStatus,reason?reason:L"Monitor helper stopped. Click Refresh to reconnect.");
    } else if(command==Scan) {
        MakeRows(worker.shared->monitors,worker.shared->count);
        RememberRows();
    } else if(command==Set && index>=0 && index<(int)rows.size()) {
        auto& r=rows[index]; r.data=worker.shared->monitors[index];
        bool remembered=!worker.shared->success || activeRestore || !r.persistentUnique || brightnessMemory.Save(r.data,Percent(r.data));
        if(r.desired<0) {
            if(!BrightnessSliderDragging(r.slider)) {
                int value=Percent(r.data); SendMessageW(r.slider,TBM_SETPOS,TRUE,value);
                wchar_t text[20];swprintf_s(text,L"%d%%",value);Text(r.value,text);
            }
            Text(r.status,!remembered?L"Brightness applied, but could not save it.":worker.shared->success?L"Brightness confirmed by monitor":L"Change not confirmed. Click Refresh to retry.");
        }
        Text(generalStatus,worker.shared->success?L"Changes are checked with your monitor.":L"Monitor did not confirm the requested brightness.");
    }
    if(failed) { needScan=false;RetryRecovery();return; }
    Dispatch();
}
void Refresh() {
    if(demo) {ReadDemo();Dispatch();return;}
    for(auto& r:rows) {r.desired=-1;EnableWindow(r.slider,FALSE);}
    needScan=true;Dispatch();
}
void Hide() {
    for(auto& row:rows)if(BrightnessSliderDragging(row.slider))CancelBrightnessSliderDrag(row.slider);
    ShowWindow(popup,SW_HIDE);
    if(!pending) Dispatch();
}
void Show(bool toggle=true) {
    if(toggle && IsWindowVisible(popup)) {Hide();return;}
    PositionPopup(); ShowWindow(popup,SW_SHOWNORMAL); ShowWindow(popup,SW_SHOW); SetForegroundWindow(popup);
    if(demo) {
        if(rows.empty()){ReadDemo();Dispatch();}
    } else if(faultHang || faultCrash) StartRequest(faultHang?SimulateHang:SimulateCrash);
    else Refresh();
}
void ToggleRestoration() {
    restoreBrightness=!restoreBrightness;
    HKEY key=nullptr;
    const wchar_t* path=testMode?demoSettingsKey.c_str():L"Software\\SteadyBrightness";
    if(RegCreateKeyExW(HKEY_CURRENT_USER,path,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS) {
        DWORD enabled=restoreBrightness?1:0;
        RegSetValueExW(key,L"RestoreBrightness",0,REG_DWORD,(BYTE*)&enabled,sizeof(enabled));RegCloseKey(key);
    }
    if(!restoreBrightness){recoveryPasses=0;KillTimer(host,TopologyTimer);topologyTimerArmed=false;}
    Refresh();
}
void Options() {
    menuActive=true;
    HMENU menu=CreatePopupMenu();
    MenuEntry entries[]={
        {1,L"Start with Windows",AutoStart()},
        {2,L"Keep tray icon visible"},
        {13,L"Restore brightness after wake",restoreBrightness},
        {0,L"",false,true},
        {10,L"Theme: Use Windows setting",theme==FollowWindows},
        {11,L"Theme: Light",theme==Light},
        {12,L"Theme: Dark",theme==Dark},
        {0,L"",false,true},
        {3,L"About / resource use"},
        {4,L"Exit"}
    };
    MENUINFO menuInfo{sizeof(menuInfo)};menuInfo.fMask=MIM_BACKGROUND;menuInfo.hbrBack=background;SetMenuInfo(menu,&menuInfo);
    for(auto& entry:entries) {
        MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_FTYPE|MIIM_DATA|MIIM_ID|MIIM_STRING|MIIM_STATE;
        item.fType=MFT_OWNERDRAW;item.wID=entry.id;item.dwItemData=(ULONG_PTR)&entry;item.dwTypeData=const_cast<wchar_t*>(entry.text);
        item.fState=entry.checked?MFS_CHECKED:MFS_ENABLED;
        if(entry.separator){item.fType=MFT_SEPARATOR;item.fMask=MIIM_FTYPE;}
        InsertMenuItemW(menu,(UINT)-1,TRUE,&item);
    }
    POINT point;GetCursorPos(&point);SetForegroundWindow(popup);
    UINT selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,popup,nullptr);
    DestroyMenu(menu);
    if(selected==1) SetAutoStart(!AutoStart());
    if(selected==2) {Hide();ShellExecuteW(nullptr,L"open",L"ms-settings:taskbar",nullptr,nullptr,SW_SHOWNORMAL);}
    if(selected>=10 && selected<=12)SetTheme((Theme)(selected-10));
    if(selected==13)ToggleRestoration();
    if(selected==3) MessageBoxW(popup,
        L"Steady Brightness 1.0.3\n\nNative Windows hardware brightness control.\nNo periodic polling while idle. No network access, telemetry, animations or GPU rendering loop.\n\nMonitor calls run in a temporary helper with a 9-second timeout. The helper exits when the flyout closes after pending writes finish.\n\nExternal displays use DDC/CI; built-in panels use Windows WMI when available. Unsupported displays are shown explicitly.\n\nClose other brightness utilities to avoid competing monitor commands.",
        AppName,MB_OK|MB_ICONINFORMATION);
    menuActive=false;
    if(selected==4) DestroyWindow(host);
    else if(IsWindowVisible(popup)) SetForegroundWindow(popup);
    PostMessageW(popup,WM_NULL,0,0);
}
void Fonts() {
    if(bodyFont) DeleteObject(bodyFont);if(valueFont) DeleteObject(valueFont);if(smallFont) DeleteObject(smallFont);
    auto create=[](int size,int weight){return CreateFontW(-Px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");};
    bodyFont=create(14,FW_SEMIBOLD);valueFont=create(19,FW_SEMIBOLD);smallFont=create(12,FW_NORMAL);
}
void DrawItem(DRAWITEMSTRUCT* item) {
    HDC dc=item->hDC;RECT rect=item->rcItem;
    SaveDC(dc);SetBkMode(dc,TRANSPARENT);SelectObject(dc,bodyFont);SetTextColor(dc,foreground);
    if(item->CtlType==ODT_BUTTON) {
        FillRect(dc,&rect,background);RECT inner=rect;InflateRect(&inner,-1,-1);
        Fill(dc,inner,(item->itemState&ODS_SELECTED)?trackColor:buttonColor);
        SetDCPenColor(dc,(item->itemState&ODS_FOCUS)?accent:borderColor);SelectObject(dc,GetStockObject(DC_PEN));SelectObject(dc,GetStockObject(NULL_BRUSH));
        RoundRect(dc,inner.left,inner.top,inner.right,inner.bottom,Px(6),Px(6));
        wchar_t text[100];GetWindowTextW(item->hwndItem,text,ARRAYSIZE(text));DrawTextW(dc,text,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(item->itemState&ODS_FOCUS){RECT focus=rect;InflateRect(&focus,-Px(4),-Px(4));DrawFocusRect(dc,&focus);}
    } else if(item->CtlType==ODT_MENU && item->itemData) {
        auto* entry=(MenuEntry*)item->itemData;
        Fill(dc,rect,(item->itemState&ODS_SELECTED)?buttonColor:backgroundColor);
        if(entry->checked) {
            HPEN pen=CreatePen(PS_SOLID,Px(2),accent);SelectObject(dc,pen);int x=rect.left+Px(12),y=(rect.top+rect.bottom)/2;
            MoveToEx(dc,x,y,nullptr);LineTo(dc,x+Px(4),y+Px(4));LineTo(dc,x+Px(11),y-Px(4));SelectObject(dc,GetStockObject(BLACK_PEN));DeleteObject(pen);
        }
        rect.left+=Px(34);rect.right-=Px(12);DrawTextW(dc,entry->text,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
    }
    RestoreDC(dc,-1);
}
LRESULT CALLBACK PopupProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_ACTIVATE: if(LOWORD(wp)==WA_INACTIVE && !menuActive) Hide();return 0;
    case WM_CLOSE: Hide();return 0;
    case WM_KEYDOWN: if(wp==VK_ESCAPE){Hide();return 0;}break;
    case WM_COMMAND:
        if(LOWORD(wp)==RefreshId)Refresh();if(LOWORD(wp)==OptionsId)Options();
        if(LOWORD(wp)==13)ToggleRestoration();
        if(LOWORD(wp)>=10 && LOWORD(wp)<=12)SetTheme((Theme)(LOWORD(wp)-10));return 0;
    case WM_DRAWITEM:DrawItem((DRAWITEMSTRUCT*)lp);return TRUE;
    case WM_MEASUREITEM: {
        auto* item=(MEASUREITEMSTRUCT*)lp;
        if(item->CtlType==ODT_MENU){item->itemWidth=Px(260);item->itemHeight=Px(32);return TRUE;}break;
    }
    case WM_HSCROLL:
        for(auto& r:rows)if(r.slider==(HWND)lp && IsWindowEnabled(r.slider)) {
            int value=(int)SendMessageW(r.slider,TBM_GETPOS,0,0);
            wchar_t text[20];swprintf_s(text,L"%d%%",value);Text(r.value,text);
            if(LOWORD(wp)==TB_ENDTRACK && r.desired<0 && value==Percent(r.data))return 0;
            r.desired=value;r.restoring=false;r.due=GetTickCount64()+(LOWORD(wp)==TB_ENDTRACK?0:130);
            Text(r.status,demo?L"Demo only - no monitor changed":L"Applying brightness...");
            if(!scanning)Dispatch();return 0;
        }
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(window,SB_VERT,&info);int pos=info.nPos;
        switch(LOWORD(wp)){case SB_LINEUP:pos-=102;break;case SB_LINEDOWN:pos+=102;break;case SB_PAGEUP:pos-=(int)info.nPage;break;case SB_PAGEDOWN:pos+=(int)info.nPage;break;case SB_THUMBTRACK:pos=info.nTrackPos;break;}
        info.fMask=SIF_POS;info.nPos=pos;SetScrollInfo(window,SB_VERT,&info,TRUE);Layout();return 0;
    }
    case WM_MOUSEWHEEL: SendMessageW(window,WM_VSCROLL,GET_WHEEL_DELTA_WPARAM(wp)>0?SB_LINEUP:SB_LINEDOWN,0);return 0;
    case WM_DPICHANGED: {
        dpi=HIWORD(wp);Fonts();
        SendMessageW(generalStatus,WM_SETFONT,(WPARAM)smallFont,TRUE);SendMessageW(refreshButton,WM_SETFONT,(WPARAM)bodyFont,TRUE);SendMessageW(optionsButton,WM_SETFONT,(WPARAM)bodyFont,TRUE);
        for(auto& r:rows){SendMessageW(r.name,WM_SETFONT,(WPARAM)bodyFont,TRUE);SendMessageW(r.value,WM_SETFONT,(WPARAM)valueFont,TRUE);SendMessageW(r.status,WM_SETFONT,(WPARAM)smallFont,TRUE);}
        Layout();PositionPopup();return 0;
    }
    case WM_CTLCOLORSTATIC: {
        bool secondary=(HWND)lp==generalStatus;
        for(auto& r:rows)if((HWND)lp==r.status)secondary=true;
        SetBkMode((HDC)wp,TRANSPARENT);SetTextColor((HDC)wp,secondary?muted:foreground);return (LRESULT)background;
    }
    case WM_ERASEBKGND: {RECT rc;GetClientRect(window,&rc);FillRect((HDC)wp,&rc,background);return 1;}
    }
    return DefWindowProcW(window,message,wp,lp);
}
LRESULT CALLBACK HostProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==taskbarCreated && taskbarCreated){AddIcon();return 0;}
    switch(message) {
    case WM_SETTINGCHANGE:case WM_THEMECHANGED:case WM_SYSCOLORCHANGE:ApplyTheme();return 0;
    case ToggleMessage: Show();return 0;
    // Simulation-only reconnect controls used by the native regression suite.
    case WM_APP+30:
        if(demo){demoValues[1]=100;demoReversed=(wp&1)!=0;demoUnavailable=(wp&2)?1:0;demoLateReset=(wp&4)!=0;QueueRecovery();}
        return 0;
    case WM_APP+31:return demo?demoScanCount:0;
    case WM_APP+32:return demo?(recoveryPasses==0 && !topologyTimerArmed && !pending && !needScan):0;
    case TrayMessage:
        switch(LOWORD(lp)) {
        case NIN_SELECT:case NIN_KEYSELECT:Show();break;
        case WM_CONTEXTMENU:if(!IsWindowVisible(popup))Show(false);Options();break;
        }return 0;
    case WM_TIMER:
        if(wp==BusyTimer && pending) {
            if(WaitForSingleObject(worker.done,0)==WAIT_OBJECT_0)Complete(false);
            else if(WaitForSingleObject(worker.process,0)==WAIT_OBJECT_0)Complete(true);
            else if(GetTickCount64()-started>=TimeoutMs)Complete(true,L"Monitor timed out. Click Refresh to reconnect.");
        }
        if(wp==DragTimer){KillTimer(host,DragTimer);Dispatch();}
        if(wp==TopologyTimer) {
            KillTimer(host,TopologyTimer);topologyTimerArmed=false;
            bool busy=pending;
            for(auto& r:rows)if(r.desired>=0 || BrightnessSliderDragging(r.slider))busy=true;
            // Finish the user's current adjustment before replacing rows or
            // reading saved targets. Never discard a newly queued slider value.
            if(busy)ArmTopologyTimer(2500);
            else {if(recoveryPasses>0)--recoveryPasses;Refresh();}
        }
        if(wp==IconTimer) {if(iconRetries>=12)KillTimer(host,IconTimer);else AddIcon();}
        return 0;
    case WM_DISPLAYCHANGE:
        for(auto& r:rows){r.desired=-1;EnableWindow(r.slider,FALSE);}
        QueueRecovery();
        return 0;
    case WM_DEVICECHANGE:
        if((wp==DBT_DEVICEARRIVAL || wp==DBT_DEVICEREMOVECOMPLETE) && lp) {
            auto* device=(DEV_BROADCAST_HDR*)lp;
            if(device->dbch_devicetype==DBT_DEVTYP_DEVICEINTERFACE && device->dbch_size>=offsetof(DEV_BROADCAST_DEVICEINTERFACE_W,dbcc_name)) {
                auto* monitor=(DEV_BROADCAST_DEVICEINTERFACE_W*)lp;
                if(IsEqualGUID(monitor->dbcc_classguid,MonitorInterface))QueueRecovery();
            }
        }
        return 0;
    case WM_POWERBROADCAST:
        if(wp==PBT_POWERSETTINGCHANGE && lp) {
            auto* setting=(POWERBROADCAST_SETTING*)lp;
            if(IsEqualGUID(setting->PowerSetting,ConsoleDisplayState) && setting->DataLength==sizeof(DWORD)) {
                DWORD state=0;CopyMemory(&state,setting->Data,sizeof(state));
                bool woke=lastConsoleDisplayState!=MAXDWORD && lastConsoleDisplayState!=1 && state==1;
                lastConsoleDisplayState=state;
                if(woke)QueueRecovery();
            }
        }
        if(wp==PBT_APMSUSPEND) {
            KillTimer(host,BusyTimer);KillTimer(host,DragTimer);KillTimer(host,TopologyTimer);
            topologyTimerArmed=false;recoveryPasses=0;
            worker.Stop();pending=false;scanning=false;needScan=false;
            EnableWindow(refreshButton,TRUE);
            for(auto& r:rows){r.desired=-1;EnableWindow(r.slider,FALSE);}
        }
        if(wp==PBT_APMRESUMEAUTOMATIC || wp==PBT_APMRESUMESUSPEND) {
            QueueRecovery();
        }
        return TRUE;
    case WM_QUERYENDSESSION:return TRUE;
    case WM_ENDSESSION:if(wp)DestroyWindow(window);return 0;
    case WM_CLOSE:DestroyWindow(window);return 0;
    case WM_DESTROY:
        KillTimer(window,BusyTimer);KillTimer(window,DragTimer);KillTimer(window,TopologyTimer);KillTimer(window,IconTimer);
        worker.Stop();Shell_NotifyIconW(NIM_DELETE,&tray);DestroyWindow(popup);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}

bool Shortcut(const std::wstring& exe) {
    HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    IShellLinkW* link=nullptr;IPersistFile* file=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,(void**)&link);
    if(SUCCEEDED(hr)) {
        hr=link->SetPath(exe.c_str());
        if(SUCCEEDED(hr))hr=link->SetWorkingDirectory(exe.substr(0,exe.find_last_of(L'\\')).c_str());
        if(SUCCEEDED(hr))hr=link->SetDescription(L"Monitor brightness sliders in the Windows system tray");
        if(SUCCEEDED(hr))hr=link->SetIconLocation(exe.c_str(),0);
        if(SUCCEEDED(hr))hr=link->QueryInterface(IID_IPersistFile,(void**)&file);
        auto programs=Known(FOLDERID_Programs);
        if(SUCCEEDED(hr) && programs.empty())hr=E_FAIL;
        if(SUCCEEDED(hr))hr=file->Save((programs+L"\\Steady Brightness.lnk").c_str(),TRUE);
    }
    if(file)file->Release();if(link)link->Release();if(SUCCEEDED(initialized))CoUninitialize();return SUCCEEDED(hr);
}
bool Extract(int id,const std::wstring& target) {
    HRSRC resource=FindResourceW(instance,MAKEINTRESOURCEW(id),RT_RCDATA);
    if(!resource)return false;
    DWORD size=SizeofResource(instance,resource),written=0;
    HGLOBAL loaded=LoadResource(instance,resource);const void* data=LockResource(loaded);
    HANDLE file=CreateFileW(target.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    bool ok=WriteFile(file,data,size,&written,nullptr)!=FALSE && written==size;
    CloseHandle(file);return ok;
}
bool RegisterInstall(const std::wstring& dir) {
    HKEY key;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SteadyBrightness",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    bool ok=true;
    auto set=[&](const wchar_t* name,const std::wstring& value){ if(RegSetValueExW(key,name,0,REG_SZ,(const BYTE*)value.c_str(),(DWORD)((value.size()+1)*sizeof(wchar_t)))!=ERROR_SUCCESS)ok=false; };
    set(L"DisplayName",AppName);set(L"DisplayVersion",L"1.0.3");set(L"Publisher",L"Steady Brightness contributors");
    set(L"InstallLocation",dir);set(L"DisplayIcon",dir+L"\\SteadyBrightness.exe");
    wchar_t system[MAX_PATH];GetSystemDirectoryW(system,ARRAYSIZE(system));
    set(L"UninstallString",L"\""+std::wstring(system)+L"\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \""+dir+L"\\Uninstall.ps1\"");
    DWORD one=1;RegSetValueExW(key,L"NoModify",0,REG_DWORD,(BYTE*)&one,sizeof(one));RegSetValueExW(key,L"NoRepair",0,REG_DWORD,(BYTE*)&one,sizeof(one));
    RegCloseKey(key);return ok;
}
int Install() {
    auto root=Known(FOLDERID_LocalAppData);if(root.empty())return 2;
    auto dir=root+L"\\Programs\\SteadyBrightness";
    if(SHCreateDirectoryExW(nullptr,dir.c_str(),nullptr)!=ERROR_SUCCESS && GetFileAttributesW(dir.c_str())==INVALID_FILE_ATTRIBUTES)return 2;
    auto target=dir+L"\\SteadyBrightness.exe";
    HWND existing=FindWindowW(HostClass,nullptr);
    if(existing){SendMessageTimeoutW(existing,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,2000,nullptr);Sleep(300);}
    bool same=!_wcsicmp(Executable().c_str(),target.c_str());
    if(!same && !CopyFileW(Executable().c_str(),target.c_str(),FALSE)) {
        MessageBoxW(nullptr,L"Could not copy the app. Exit Steady Brightness and try installing again.",AppName,MB_ICONERROR);return 2;
    }
    if(!Extract(2,dir+L"\\Uninstall.ps1") || !Extract(3,dir+L"\\Uninstall.cmd") || !RegisterInstall(dir) || !Shortcut(target)) {
        MessageBoxW(nullptr,L"The app was copied, but Windows could not finish creating the shortcuts or uninstall entry. Try the installer again.",AppName,MB_ICONERROR);return 2;
    }
    HINSTANCE launched=ShellExecuteW(nullptr,L"open",target.c_str(),L"--show",dir.c_str(),SW_SHOWNORMAL);
    if((INT_PTR)launched<=32){MessageBoxW(nullptr,L"Installed. Open Steady Brightness from the Start menu to launch it.",AppName,MB_ICONINFORMATION);return 0;}
    MessageBoxW(nullptr,L"Installed for your Windows account. No administrator access is needed.\n\nLook for the sun icon next to the clock (it may be under the ^ arrow). Drag it out to keep it visible.\n\nClick Options > Start with Windows to enable automatic startup.\n\nExit Monitorian and Twinkle Tray to avoid competing brightness commands.",AppName,MB_ICONINFORMATION);return 0;
}
}

int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,PWSTR,int) {
    instance=inst;
    int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv)return 2;
    auto has=[&](const wchar_t* value){for(int i=1;i<argc;++i)if(!_wcsicmp(argv[i],value))return true;return false;};
    if(argc==6 && !_wcsicmp(argv[1],L"--worker")) {
        HANDLE handles[4];for(int i=0;i<4;++i)handles[i]=(HANDLE)(uintptr_t)_wcstoui64(argv[i+2],nullptr,10);
        LocalFree(argv);return WorkerMain(handles[0],handles[1],handles[2],handles[3]);
    }
    if(argc==3 && (!_wcsicmp(argv[1],L"--probe") || !_wcsicmp(argv[1],L"--test-hardware"))) {
        bool exercise=!_wcsicmp(argv[1],L"--test-hardware");std::wstring path=argv[2];LocalFree(argv);return DiagnosticMain(path.c_str(),exercise);
    }
    if(has(L"--install")){LocalFree(argv);return Install();}
    demo=has(L"--demo");faultHang=has(L"--test-timeout");faultCrash=has(L"--test-crash");testMode=demo||faultHang||faultCrash;
    if(testMode){demoSettingsKey=L"Software\\SteadyBrightness.Tests\\"+std::to_wstring(GetCurrentProcessId());brightnessMemory.UseKey(demoSettingsKey);}
    restoreBrightness=testMode || ReadDword(L"Software\\SteadyBrightness",L"RestoreBrightness",1)!=0;
    bool backgroundStart=has(L"--background"),show=!backgroundStart||testMode;LocalFree(argv);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetCurrentProcessExplicitAppUserModelID(L"SteadyBrightness.Tray.1");
    instanceMutex=CreateMutexW(nullptr,FALSE,testMode?L"Local\\SteadyBrightness.Test.1":L"Local\\SteadyBrightness.Instance.1");
    if(!instanceMutex)return 2;
    if(GetLastError()==ERROR_ALREADY_EXISTS && !testMode) {
        if(!backgroundStart)if(auto existing=FindWindowW(HostClass,nullptr)){AllowSetForegroundWindow(ASFW_ANY);PostMessageW(existing,ToggleMessage,0,0);}
        CloseHandle(instanceMutex);return 0;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,0,0,LR_DEFAULTSIZE);
    DWORD savedTheme=ReadDword(L"Software\\SteadyBrightness",L"Theme",0);theme=savedTheme<=2?(Theme)savedTheme:FollowWindows;ApplyTheme();
    if(!RegisterBrightnessSlider(instance,CurrentSliderPalette))return 2;
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=icon;wc.hIconSm=icon;
    wc.lpfnWndProc=HostProc;wc.lpszClassName=testMode?L"SteadyBrightness.TestHost.1":HostClass;RegisterClassExW(&wc);
    host=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,AppName,WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,instance,nullptr);
    wc.lpfnWndProc=PopupProc;wc.lpszClassName=L"SteadyBrightness.Popup.1";wc.hbrBackground=background;RegisterClassExW(&wc);
    popup=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,wc.lpszClassName,AppName,WS_POPUP|WS_BORDER|WS_VSCROLL,
        0,0,400,200,host,nullptr,instance,nullptr);
    if(!host||!popup)return 2;
    dpi=GetDpiForWindow(popup);Fonts();
    DWORD corner=2;DwmSetWindowAttribute(popup,33,&corner,sizeof(corner));
    generalStatus=Label(L"Click Refresh to read your displays.",smallFont,SS_ENDELLIPSIS);
    refreshButton=CreateWindowExW(0,L"BUTTON",L"Refresh",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,popup,(HMENU)(INT_PTR)RefreshId,instance,nullptr);
    optionsButton=CreateWindowExW(0,L"BUTTON",L"Options",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,popup,(HMENU)(INT_PTR)OptionsId,instance,nullptr);
    SendMessageW(refreshButton,WM_SETFONT,(WPARAM)bodyFont,TRUE);SendMessageW(optionsButton,WM_SETFONT,(WPARAM)bodyFont,TRUE);
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    powerNotification=RegisterPowerSettingNotification(host,&ConsoleDisplayState,DEVICE_NOTIFY_WINDOW_HANDLE);
    DEV_BROADCAST_DEVICEINTERFACE_W filter{};filter.dbcc_size=sizeof(filter);filter.dbcc_devicetype=DBT_DEVTYP_DEVICEINTERFACE;filter.dbcc_classguid=MonitorInterface;
    if(!testMode)monitorNotification=RegisterDeviceNotificationW(host,&filter,DEVICE_NOTIFY_WINDOW_HANDLE);
    ApplyTheme();Layout();if(!testMode)AddIcon();if(show)Show(false);
    if(!show && restoreBrightness && !testMode)Refresh();
    MSG message;
    while(GetMessageW(&message,nullptr,0,0)>0) {
        if(message.message==WM_KEYDOWN && message.wParam==VK_ESCAPE && IsWindowVisible(popup)){Hide();continue;}
        if(!IsWindowVisible(popup)||!IsDialogMessageW(popup,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    if(powerNotification)UnregisterPowerSettingNotification(powerNotification);
    if(monitorNotification)UnregisterDeviceNotification(monitorNotification);
    if(!demoSettingsKey.empty()){RegDeleteKeyW(HKEY_CURRENT_USER,demoSettingsKey.c_str());RegDeleteKeyW(HKEY_CURRENT_USER,L"Software\\SteadyBrightness.Tests");}
    DestroyRows();DeleteObject(bodyFont);DeleteObject(valueFont);DeleteObject(smallFont);DeleteObject(background);DestroyIcon(icon);
    CloseHandle(instanceMutex);return 0;
}
