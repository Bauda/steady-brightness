#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <stdint.h>

constexpr int MaxMonitors = 32;
constexpr DWORD ProtocolVersion = 2;
enum Command : LONG { Scan = 1, Set = 2, Quit = 3, SimulateHang = 90, SimulateCrash = 91 };
enum Backend : DWORD { Unavailable, DdcVcp, DdcHigh, Wmi, Nvidia };
struct MonitorData {
    wchar_t id[256];
    wchar_t persistentId[128];
    wchar_t name[128];
    wchar_t note[160];
    DWORD backend, minimum, maximum, current, error;
    BOOL primary;
    RECT bounds;
};
struct Shared {
    DWORD version;
    LONG command, index, percent;
    DWORD count, error;
    BOOL success;
    MonitorData monitors[MaxMonitors];
};
inline int Percent(const MonitorData& m) {
    if (m.maximum <= m.minimum) return 0;
    return (int)(((uint64_t)(std::clamp(m.current, m.minimum, m.maximum) - m.minimum) * 100 +
        (m.maximum - m.minimum) / 2) / (m.maximum - m.minimum));
}
inline DWORD Raw(const MonitorData& m, int percent) {
    return m.minimum + (DWORD)(((uint64_t)(m.maximum - m.minimum) * std::clamp(percent, 0, 100) + 50) / 100);
}
template<size_t N> inline void Copy(wchar_t (&dest)[N], const wchar_t* src) { wcsncpy_s(dest, src ? src : L"", _TRUNCATE); }
int WorkerMain(HANDLE mapping, HANDLE command, HANDLE done, HANDLE parent);
int DiagnosticMain(const wchar_t* path, bool exercise);
