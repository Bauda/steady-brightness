#pragma once
#include "common.h"

// Store only successfully read/confirmed values, keyed by the device interface,
// never by a display number or model name. No timer, file watcher, or open key.
class BrightnessMemory {
    std::wstring key;
    static std::wstring ValueName(const MonitorData& monitor) {
        return L"Brightness:" + std::wstring(monitor.persistentId);
    }
public:
    explicit BrightnessMemory(const wchar_t* path) : key(path) {}
    void UseKey(const std::wstring& path) { key = path; }
    static bool Eligible(const MonitorData& m) {
        return m.persistentId[0] && m.maximum > m.minimum &&
            (m.backend == DdcVcp || m.backend == DdcHigh || m.backend == Nvidia);
    }
    static bool Unique(const MonitorData* monitors, size_t count, size_t index) {
        if (!Eligible(monitors[index])) return false;
        for (size_t i=0;i<count;++i)
            if (i!=index && !_wcsicmp(monitors[i].persistentId,monitors[index].persistentId)) return false;
        return true;
    }
    bool Read(const MonitorData& monitor, int& percent) const {
        if (!Eligible(monitor)) return false;
        DWORD value=0, bytes=sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER,key.c_str(),ValueName(monitor).c_str(),
            RRF_RT_REG_DWORD,nullptr,&value,&bytes)!=ERROR_SUCCESS || value>100) return false;
        percent=(int)value; return true;
    }
    bool Save(const MonitorData& monitor, int percent) const {
        if (!Eligible(monitor) || percent<0 || percent>100) return false;
        int previous=0;
        if (Read(monitor,previous) && previous==percent) return true;
        HKEY opened=nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER,key.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&opened,nullptr)!=ERROR_SUCCESS) return false;
        DWORD value=(DWORD)percent;
        LONG error=RegSetValueExW(opened,ValueName(monitor).c_str(),0,REG_DWORD,(BYTE*)&value,sizeof(value));
        RegCloseKey(opened); return error==ERROR_SUCCESS;
    }
};
