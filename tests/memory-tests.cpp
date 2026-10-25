#include "../brightness-memory.h"
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
    RegDeleteKeyW(HKEY_CURRENT_USER,key.c_str());RegDeleteKeyW(HKEY_CURRENT_USER,L"Software\\SteadyBrightness.Tests");
    return failures?1:0;
}
