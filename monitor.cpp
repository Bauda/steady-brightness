#include "common.h"
#include <physicalmonitorenumerationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <wbemidl.h>
#include <oleauto.h>
#include <stdio.h>
#include "nv-direct.h"

#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace {
struct OwnedMonitor { HANDLE physical = nullptr; bool hasPhysical = false; std::wstring instance, methodPath; NvPhysicalGpuHandle gpu = nullptr; NvU32 mask = 0; };
class Hardware {
public:
    Shared& s;
    OwnedMonitor owned[MaxMonitors];
    IWbemServices* wmi = nullptr;
    bool com = false;
    NvidiaDdc nvidia;
    explicit Hardware(Shared& shared) : s(shared) {}
    ~Hardware() { Clear(); if (wmi) wmi->Release(); if (com) CoUninitialize(); }
    void Clear() {
        for (auto& m : owned) {
            if (m.hasPhysical) DestroyPhysicalMonitor(m.physical);
            m = OwnedMonitor{};
        }
        s.count = 0;
        ZeroMemory(s.monitors, sizeof(s.monitors));
    }
    bool ConnectWmi() {
        if (wmi) return true;
        if (!com) { if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return false; com = true; }
        CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
        IWbemLocator* loc = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
            IID_IWbemLocator, (void**)&loc);
        if (FAILED(hr)) return false;
        BSTR ns = SysAllocString(L"ROOT\\WMI");
        hr = loc->ConnectServer(ns, nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT, nullptr, nullptr, &wmi);
        SysFreeString(ns); loc->Release();
        if (FAILED(hr)) { wmi = nullptr; return false; }
        hr = CoSetProxyBlanket(wmi, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
            RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        if (FAILED(hr)) { wmi->Release(); wmi = nullptr; return false; }
        return true;
    }
    IEnumWbemClassObject* Query(const wchar_t* text) {
        if (!ConnectWmi()) return nullptr;
        IEnumWbemClassObject* e = nullptr;
        BSTR lang = SysAllocString(L"WQL"), query = SysAllocString(text);
        HRESULT hr = wmi->ExecQuery(lang, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &e);
        SysFreeString(lang); SysFreeString(query);
        return SUCCEEDED(hr) ? e : nullptr;
    }
    static std::wstring Text(IWbemClassObject* obj, const wchar_t* property) {
        VARIANT v; VariantInit(&v);
        std::wstring out;
        if (SUCCEEDED(obj->Get(property, 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR && v.bstrVal) out = v.bstrVal;
        VariantClear(&v); return out;
    }
    static DWORD Number(IWbemClassObject* obj, const wchar_t* property) {
        VARIANT v; VariantInit(&v); DWORD n = 0;
        if (SUCCEEDED(obj->Get(property, 0, &v, nullptr, nullptr))) {
            if (v.vt == VT_UI1) n = v.bVal;
            if (v.vt == VT_I4) n = (DWORD)v.lVal;
            if (v.vt == VT_UI4) n = v.ulVal;
            if (v.vt == VT_BOOL) n = v.boolVal != VARIANT_FALSE;
        }
        VariantClear(&v); return n;
    }
    bool Read(int index) {
        auto& d = s.monitors[index]; auto& o = owned[index];
        DWORD current = 0, maximum = 0, minimum = 0;
        bool ok = false;
        if (d.backend == Wmi) {
            IEnumWbemClassObject* e = Query(L"SELECT * FROM WmiMonitorBrightness WHERE Active=TRUE");
            if (e) {
                IWbemClassObject* obj = nullptr; ULONG n = 0;
                while (e->Next(1200, 1, &obj, &n) == S_OK && n) {
                    if (!_wcsicmp(Text(obj, L"InstanceName").c_str(), o.instance.c_str())) {
                        current = Number(obj, L"CurrentBrightness"); maximum = 100; ok = current <= 100;
                    }
                    obj->Release(); if (ok) break;
                }
                e->Release();
            }
        } else if (d.backend == Nvidia) {
            ok = nvidia.Get(o.gpu, o.mask, current, maximum);
        } else if (d.backend == DdcHigh) {
            ok = GetMonitorBrightness(o.physical, &minimum, &current, &maximum) != FALSE;
        } else {
            MC_VCP_CODE_TYPE type{};
            ok = GetVCPFeatureAndVCPFeatureReply(o.physical, 0x10, &type, &current, &maximum) != FALSE;
        }
        if (ok && maximum > minimum && current >= minimum && current <= maximum) {
            d.minimum = minimum; d.maximum = maximum; d.current = current; d.error = 0; return true;
        }
        d.error = GetLastError(); if (!d.error) d.error = ERROR_INVALID_DATA;
        return false;
    }
    static std::wstring Friendly(HMONITOR monitor) {
        UINT32 pathsCount = 0, modesCount = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathsCount, &modesCount) != ERROR_SUCCESS) return L"";
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathsCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modesCount);
        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathsCount, paths.data(), &modesCount, modes.data(), nullptr) != ERROR_SUCCESS) return L"";
        MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi); GetMonitorInfoW(monitor, &mi);
        for (UINT32 i = 0; i < pathsCount; ++i) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header = { DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), paths[i].sourceInfo.adapterId, paths[i].sourceInfo.id };
            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || _wcsicmp(source.viewGdiDeviceName, mi.szDevice)) continue;
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header = { DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target), paths[i].targetInfo.adapterId, paths[i].targetInfo.id };
            if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS && target.monitorFriendlyDeviceName[0]) return target.monitorFriendlyDeviceName;
        }
        return L"";
    }
    static BOOL CALLBACK EnumMonitor(HMONITOR hm, HDC, LPRECT, LPARAM context) {
        auto* self = (Hardware*)context;
        auto& s = self->s;
        MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi); if (!GetMonitorInfoW(hm, &mi)) return TRUE;
        DWORD count = 0;
        std::vector<PHYSICAL_MONITOR> physical;
        if (GetNumberOfPhysicalMonitorsFromHMONITOR(hm, &count) && count && count <= 64) {
            physical.resize(count);
            if (!GetPhysicalMonitorsFromHMONITOR(hm, count, physical.data())) physical.clear();
        }
        std::wstring friendly = Friendly(hm);
        DISPLAY_DEVICEW device{}; device.cb = sizeof(device);
        EnumDisplayDevicesW(mi.szDevice, 0, &device, 0);
        const size_t rows = std::max<size_t>(1, physical.size());
        for (size_t p = 0; p < rows; ++p) {
            if (s.count >= MaxMonitors) { if (p < physical.size()) DestroyPhysicalMonitor(physical[p].hPhysicalMonitor); continue; }
            int i = (int)s.count++;
            auto& d = s.monitors[i];
            swprintf_s(d.id, L"%s|%zu", mi.szDevice, p);
            Copy(d.name, !friendly.empty() ? friendly.c_str() : device.DeviceString[0] ? device.DeviceString : mi.szDevice);
            if (rows > 1 && p < physical.size() && physical[p].szPhysicalMonitorDescription[0]) Copy(d.name, physical[p].szPhysicalMonitorDescription);
            d.bounds = mi.rcMonitor; d.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            if (p < physical.size()) {
                self->owned[i].physical = physical[p].hPhysicalMonitor;
                self->owned[i].hasPhysical = true;
                d.backend = DdcVcp;
                bool ok = false;
                for (int attempt = 0; attempt < 3 && !ok; ++attempt) { if (attempt) Sleep(90 * attempt); ok = self->Read(i); }
                if (!ok) { d.backend = DdcHigh; ok = self->Read(i); }
                if (ok) { Copy(d.note, d.backend == DdcVcp ? L"Hardware brightness (DDC/CI)" : L"Hardware brightness (Windows monitor API)"); continue; }
            }
            if (rows == 1 && self->nvidia.Resolve(mi.szDevice, self->owned[i].gpu, self->owned[i].mask)) {
                d.backend = Nvidia;
                bool ok = false;
                for (int attempt = 0; attempt < 3 && !ok; ++attempt) { if (attempt) Sleep(150); ok = self->Read(i); }
                if (ok) { Copy(d.note, L"Hardware brightness (NVIDIA direct DDC)"); continue; }
            }
            d.backend = Unavailable;
            Copy(d.note, L"No response. Wake display / enable DDC/CI; Refresh.");
        }
        return TRUE;
    }
    static std::wstring InstanceDevice(std::wstring value) {
        // WMI uses DISPLAY\\vendor\\instance_0; EnumDisplayDevices uses MONITOR\\vendor\\instance.
        auto pos = value.find(L'\\'); if (pos != std::wstring::npos) value.erase(0, pos + 1);
        pos = value.rfind(L'_'); if (pos != std::wstring::npos) value.erase(pos);
        std::transform(value.begin(), value.end(), value.begin(), towupper); return value;
    }
    void ScanAll() {
        Clear();
        EnumDisplayMonitors(nullptr, nullptr, EnumMonitor, (LPARAM)this);
        // WMI is only needed for panels that did not answer DDC; never load it for a successful external-only setup.
        bool needWmi = false;
        for (DWORD i = 0; i < s.count; ++i) if (!s.monitors[i].backend) needWmi = true;
        if (!needWmi) return;
        IEnumWbemClassObject* e = Query(L"SELECT * FROM WmiMonitorBrightness WHERE Active=TRUE");
        if (!e) return;
        IWbemClassObject* obj = nullptr; ULONG n = 0;
        while (e->Next(1200, 1, &obj, &n) == S_OK && n) {
            auto instance = Text(obj, L"InstanceName");
            int match = -1;
            for (DWORD i = 0; i < s.count; ++i) {
                if (s.monitors[i].backend) continue;
                wchar_t logical[256]; Copy(logical, s.monitors[i].id);
                if (auto pipe = wcschr(logical, L'|')) *pipe = 0;
                DISPLAY_DEVICEW dd{}; dd.cb = sizeof(dd);
                if (!EnumDisplayDevicesW(logical, 0, &dd, 0)) continue;
                std::wstring device = dd.DeviceID;
                auto slash = device.find(L'\\'); if (slash != std::wstring::npos) device.erase(0, slash + 1);
                std::transform(device.begin(), device.end(), device.begin(), towupper);
                if (device == InstanceDevice(instance)) { match = (int)i; break; }
            }
            if (match >= 0) {
                owned[match].instance = instance;
                auto& d = s.monitors[match]; d.backend = Wmi; d.minimum = 0; d.maximum = 100;
                d.current = Number(obj, L"CurrentBrightness"); d.error = 0; Copy(d.note, L"Built-in panel brightness (WMI)");
            }
            obj->Release();
        }
        e->Release();
        e = Query(L"SELECT * FROM WmiMonitorBrightnessMethods WHERE Active=TRUE");
        if (!e) return;
        while (e->Next(1200, 1, &obj, &n) == S_OK && n) {
            auto instance = Text(obj, L"InstanceName");
            for (DWORD i = 0; i < s.count; ++i)
                if (s.monitors[i].backend == Wmi && !_wcsicmp(instance.c_str(), owned[i].instance.c_str())) owned[i].methodPath = Text(obj, L"__PATH");
            obj->Release();
        }
        e->Release();
        for (DWORD i = 0; i < s.count; ++i) if (s.monitors[i].backend == Wmi && owned[i].methodPath.empty()) {
            s.monitors[i].backend = Unavailable; Copy(s.monitors[i].note, L"Windows did not expose a brightness setter.");
        }
    }
    bool WriteWmi(int index, int percent) {
        if (!ConnectWmi() || owned[index].methodPath.empty()) return false;
        IWbemClassObject *klass = nullptr, *definition = nullptr, *input = nullptr, *output = nullptr;
        BSTR cls = SysAllocString(L"WmiMonitorBrightnessMethods"), method = SysAllocString(L"WmiSetBrightness");
        BSTR path = SysAllocString(owned[index].methodPath.c_str());
        HRESULT hr = wmi->GetObject(cls, 0, nullptr, &klass, nullptr);
        if (SUCCEEDED(hr)) hr = klass->GetMethod(method, 0, &definition, nullptr);
        if (SUCCEEDED(hr)) hr = definition->SpawnInstance(0, &input);
        VARIANT v; VariantInit(&v); v.vt = VT_I4; v.lVal = 0;
        if (SUCCEEDED(hr)) hr = input->Put(L"Timeout", 0, &v, CIM_UINT32);
        v.vt = VT_UI1; v.bVal = (BYTE)percent;
        if (SUCCEEDED(hr)) hr = input->Put(L"Brightness", 0, &v, CIM_UINT8);
        if (SUCCEEDED(hr)) hr = wmi->ExecMethod(path, method, 0, nullptr, input, &output, nullptr);
        bool ok = SUCCEEDED(hr) && output && Number(output, L"ReturnValue") == 0;
        if (output) output->Release(); if (input) input->Release(); if (definition) definition->Release(); if (klass) klass->Release();
        SysFreeString(path); SysFreeString(method); SysFreeString(cls); return ok;
    }
    bool SetRaw(int index, DWORD raw) {
        auto& d = s.monitors[index];
        if (!d.backend) return false;
        if (d.backend == Wmi) return WriteWmi(index, (int)raw);
        if (d.backend == Nvidia) return nvidia.Set(owned[index].gpu, owned[index].mask, raw);
        if (d.backend == DdcHigh) return SetMonitorBrightness(owned[index].physical, raw) != FALSE;
        return SetVCPFeature(owned[index].physical, 0x10, raw) != FALSE;
    }
    bool SetVerified(int index, DWORD target) {
        auto& d = s.monitors[index];
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (attempt) Sleep(100 * attempt);
            if (!SetRaw(index, target)) { d.error = GetLastError(); continue; }
            Sleep(70);
            if (Read(index)) {
                DWORD delta = d.current > target ? d.current - target : target - d.current;
                if (delta <= std::max<DWORD>(1, (d.maximum - d.minimum) / 100)) { d.error = 0; return true; }
                d.error = ERROR_WRITE_FAULT;
            }
        }
        if (!d.error) d.error = ERROR_GEN_FAILURE;
        return false;
    }
};
}

int WorkerMain(HANDLE mapping, HANDLE commandEvent, HANDLE done, HANDLE parent) {
    auto* shared = (Shared*)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared));
    if (!shared || shared->version != ProtocolVersion) return 2;
    {
        Hardware hardware(*shared);
        HANDLE wait[] = { commandEvent, parent };
        while (WaitForMultipleObjects(2, wait, FALSE, INFINITE) == WAIT_OBJECT_0) {
            LONG command = shared->command;
            if (command == Quit) break;
            shared->success = FALSE; shared->error = 0;
            if (command == SimulateHang) Sleep(INFINITE);
            if (command == SimulateCrash) ExitProcess(97);
            if (command == Scan) { hardware.ScanAll(); shared->success = TRUE; }
            if (command == Set && shared->index >= 0 && shared->index < (LONG)shared->count) {
                int index = shared->index;
                shared->success = hardware.SetVerified(index, Raw(shared->monitors[index], shared->percent));
                shared->error = shared->monitors[index].error;
            }
            SetEvent(done);
        }
    }
    UnmapViewOfFile(shared); return 0;
}

int DiagnosticMain(const wchar_t* path, bool exercise) {
    FILE* f = nullptr; if (_wfopen_s(&f, path, L"w, ccs=UTF-8") || !f) return 2;
    Shared shared{}; shared.version = ProtocolVersion;
    Hardware hardware(shared); hardware.ScanAll();
    fwprintf(f, L"Steady Brightness 1.0.2 - %s\nDetected: %lu\n", exercise ? L"hardware write/read/restore test" : L"read-only monitor probe", shared.count);
    int failures = 0;
    for (DWORD i = 0; i < shared.count; ++i) {
        auto& m = shared.monitors[i];
        fwprintf(f, L"\n%lu. %s [%s]\nBackend=%lu Range=%lu..%lu Raw=%lu Percent=%d Error=%lu\n%s\n",
            i + 1, m.name, m.id, m.backend, m.minimum, m.maximum, m.current, Percent(m), m.error, m.note);
        fflush(f);
        if (!m.backend) {
            if (hardware.owned[i].hasPhysical) {
                DWORD caps=0, temp=0, minimum=0, current=0, maximum=0;
                BOOL capsOk=GetMonitorCapabilities(hardware.owned[i].physical,&caps,&temp);
                DWORD capsError=GetLastError();
                BOOL highOk=GetMonitorBrightness(hardware.owned[i].physical,&minimum,&current,&maximum);
                fwprintf(f,L"Capability call=%d caps=%lu error=%lu; high-level read=%d error=%lu raw=%lu range=%lu..%lu\n",capsOk,caps,capsError,highOk,GetLastError(),current,minimum,maximum);
                DWORD length=0;
                if(GetCapabilitiesStringLength(hardware.owned[i].physical,&length) && length>0 && length<4096) {
                    std::vector<char> text(length+1);
                    BOOL ok=CapabilitiesRequestAndCapabilitiesReply(hardware.owned[i].physical,text.data(),length);
                    fwprintf(f,L"Capabilities string result=%d error=%lu text=%hs\n",ok,GetLastError(),text.data());
                }
                MC_VCP_CODE_TYPE type{};
                BOOL lowOk=GetVCPFeatureAndVCPFeatureReply(hardware.owned[i].physical,0x10,&type,&current,&maximum);
                fwprintf(f,L"After capability query: low-level read=%d error=%lu raw=%lu max=%lu\n",lowOk,GetLastError(),current,maximum);
            }
            if(hardware.owned[i].gpu) {
                BYTE reply[11]{};DWORD current=0,maximum=0;
                bool ok=hardware.nvidia.Get(hardware.owned[i].gpu,hardware.owned[i].mask,current,maximum,reply);
                fwprintf(f,L"NVIDIA read=%d status=%d reply=",ok,(int)hardware.nvidia.last);
                for(auto b:reply)fwprintf(f,L"%02x ",(unsigned)b);
                fwprintf(f,L"\n");
            }
            fflush(f);
        }
        if (exercise && m.backend) {
            DWORD original = m.current;
            DWORD step = std::max<DWORD>(1, (m.maximum - m.minimum) / 50);
            DWORD test = original + step <= m.maximum ? original + step : original - std::min(step, original - m.minimum);
            bool changed = hardware.SetVerified((int)i, test);
            fwprintf(f, L"Change target=%lu read=%lu result=%s\n", test, m.current, changed ? L"PASS" : L"FAIL"); fflush(f);
            bool restored = hardware.SetVerified((int)i, original);
            fwprintf(f, L"Restore target=%lu read=%lu result=%s\n", original, m.current, restored ? L"PASS" : L"FAIL"); fflush(f);
            if (!changed || !restored) ++failures;
        }
    }
    fwprintf(f, L"\nFailures=%d\n", failures); fclose(f); return failures ? 1 : 0;
}
