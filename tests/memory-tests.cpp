#include "../brightness-memory.h"
#include "../startup.h"
#include <stdio.h>
#pragma comment(lib,"advapi32.lib")

int main() {
    std::wstring key=L"Software\\SteadyBrightness.Tests\\memory-"+std::to_wstring(GetCurrentProcessId());
    int failures=0;
    auto check=[&](bool ok,const char* label){printf("%s: %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;};
    MonitorData monitors[2]{};
    for(auto& m:monitors){m.backend=DdcVcp;m.maximum=100;Copy(m.name,L"Same monitor model");}
    Copy(monitors[0].persistentId,L"device:one");Copy(monitors[1].persistentId,L"device:two");
    BrightnessMemory memory(key.c_str());int value=-1;
    check(memory.ReadLink()==-1,"new settings start unlinked");
    check(memory.SaveLink(0) && BrightnessMemory(key.c_str()).ReadLink()==0,"linked mode and zero target survive a new settings instance");
    check(memory.SaveLink(100) && memory.ReadLink()==100,"linked maximum brightness persists");
    check(!memory.SaveLink(-2) && !memory.SaveLink(101) && memory.ReadLink()==100,"invalid linked targets preserve saved mode");
    check(memory.SaveLink(-1) && BrightnessMemory(key.c_str()).ReadLink()==-1,"unlinking survives a new settings instance");
    check(!memory.Read(monitors[0],value),"no remembered brightness before a successful observation");
    check(memory.Save(monitors[0],37),"save a confirmed brightness");
    check(BrightnessMemory(key.c_str()).Read(monitors[0],value) && value==37,"brightness survives a new settings instance");
    check(!memory.Read(monitors[1],value),"identical model names never share brightness");
    Copy(monitors[0].id,L"DISPLAY99|0");
    check(memory.Read(monitors[0],value) && value==37,"Windows display renumbering does not change identity");
    check(!memory.Save(monitors[0],-1) && !memory.Save(monitors[0],101),"reject invalid saved percentages");
    check(memory.Read(monitors[0],value) && value==37,"invalid input preserves the previous confirmed value");
    check(memory.Save(monitors[0],0) && memory.Read(monitors[0],value) && value==0,"zero brightness persists");
    check(memory.Save(monitors[0],100) && memory.Read(monitors[0],value) && value==100,"maximum brightness persists");
    check(BrightnessMemory::Unique(monitors,2,0),"different device paths can restore independently");
    Copy(monitors[1].persistentId,L"DEVICE:ONE");
    check(!BrightnessMemory::Unique(monitors,2,0),"duplicate device identity is rejected case-insensitively");
    monitors[0].backend=Unavailable;check(!memory.Save(monitors[0],100),"unavailable monitor cannot overwrite remembered brightness");
    monitors[0].backend=Wmi;check(!BrightnessMemory::Eligible(monitors[0]),"Windows laptop power policy remains independent");
    monitors[0].backend=DdcVcp;monitors[0].persistentId[0]=0;
    check(!memory.Save(monitors[0],100),"unknown identity cannot be saved under a display number");

    auto startupKey = key + L"\\Startup";
    auto approvalKey = key + L"\\Approval";
    using Startup::State;
    auto startup = [&] { return Startup::Read(startupKey.c_str(), approvalKey.c_str()); };
    check(startup() == State::Off, "startup is off without a registration");
    check(Startup::Write(startupKey.c_str(), L"C:\\Missing Steady Brightness\\SteadyBrightness.exe", true)
        == ERROR_SUCCESS && startup() == State::Broken, "missing startup executable is reported as repairable");
    wchar_t exe[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
    check(length > 0 && length < ARRAYSIZE(exe)
        && Startup::Write(startupKey.c_str(), exe, true) == ERROR_SUCCESS && startup() == State::On,
        "repairing startup writes a valid background command");
    const DWORD approval[3]{3,1,0};
    check(RegSetKeyValueW(HKEY_CURRENT_USER, approvalKey.c_str(), Startup::Entry, REG_BINARY, approval, sizeof(approval))
        == ERROR_SUCCESS && startup() == State::DisabledByWindows, "Windows can disable an existing Run entry");
    check(Startup::Write(startupKey.c_str(), exe, true) == ERROR_SUCCESS && startup() == State::DisabledByWindows,
        "rebinding the executable preserves Windows Startup Apps approval");
    check(Startup::Write(startupKey.c_str(), std::wstring(Startup::MaxCommand, L'x'), true) == ERROR_FILENAME_EXCED_RANGE
        && startup() == State::DisabledByWindows, "an overlong command leaves the previous registration intact");
    check(Startup::Write(startupKey.c_str(), exe, false) == ERROR_SUCCESS && startup() == State::Off,
        "disabling the app's startup entry removes the command");
    RegDeleteKeyW(HKEY_CURRENT_USER, startupKey.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, approvalKey.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER,key.c_str());RegDeleteKeyW(HKEY_CURRENT_USER,L"Software\\SteadyBrightness.Tests");
    return failures?1:0;
}
