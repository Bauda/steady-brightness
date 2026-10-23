#pragma once
#include "common.h"
#include "third_party/nvapi/nvapi.h"
// Public NVIDIA API, loaded only inside the temporary monitor helper.
class NvidiaDdc {
    HMODULE dll = nullptr;
    using Query = void* (__cdecl*)(unsigned int);
    using Init = NvAPI_Status (__cdecl*)();
    using Display = NvAPI_Status (__cdecl*)(const char*, NvDisplayHandle*);
    using Gpus = NvAPI_Status (__cdecl*)(NvDisplayHandle, NvPhysicalGpuHandle*, NvU32*);
    using Output = NvAPI_Status (__cdecl*)(NvDisplayHandle, NvU32*);
    using Transfer = NvAPI_Status (__cdecl*)(NvPhysicalGpuHandle, NV_I2C_INFO*);
    Init unload = nullptr;
    Display display = nullptr;
    Gpus gpus = nullptr;
    Output output = nullptr;
    Transfer read = nullptr, write = nullptr;
public:
    NvAPI_Status last = NVAPI_OK;
    ~NvidiaDdc() { if (unload) unload(); if (dll) FreeLibrary(dll); }
    bool InitApi() {
        if (dll) return display && gpus && output && read && write;
        dll = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dll) return false;
        auto query = (Query)GetProcAddress(dll,"nvapi_QueryInterface");
        if (!query) return false;
        auto init = (Init)query(0x0150e828);
        if (!init || (last = init()) != NVAPI_OK) return false;
        unload = (Init)query(0xd22bdd7e);
        display = (Display)query(0x35c29134); gpus = (Gpus)query(0x34ef9506); output = (Output)query(0xd995937e);
        read = (Transfer)query(0x2fde12c5); write = (Transfer)query(0xe812eb07);
        return display && gpus && output && read && write;
    }
    bool Resolve(const wchar_t* device, NvPhysicalGpuHandle& gpu, NvU32& mask) {
        if (!InitApi()) return false;
        char name[128]; WideCharToMultiByte(CP_ACP,0,device,-1,name,sizeof(name),nullptr,nullptr);
        NvDisplayHandle d = nullptr; NvPhysicalGpuHandle handles[NVAPI_MAX_PHYSICAL_GPUS]{}; NvU32 count = 0;
        if ((last = display(name,&d)) != NVAPI_OK || (last = gpus(d,handles,&count)) != NVAPI_OK || count != 1) return false;
        if ((last = output(d,&mask)) != NVAPI_OK || !mask) return false;
        gpu = handles[0]; return true;
    }
    bool TransferData(NvPhysicalGpuHandle gpu, NvU32 mask, BYTE* data, DWORD length, bool reading) {
        NV_I2C_INFO info{}; info.version=NV_I2C_INFO_VER;info.displayMask=mask;info.bIsDDCPort=1;info.i2cDevAddress=0x6e;
        info.pbData=data;info.cbSize=length;info.i2cSpeed=NVAPI_I2C_SPEED_DEPRECATED;info.i2cSpeedKhz=NVAPI_I2C_SPEED_DEFAULT;
        if(!reading) { info.pbI2cRegAddress=data;info.regAddrSize=1;info.pbData=data+1;info.cbSize=length-1; }
        last=(reading?read:write)(gpu,&info);return last==NVAPI_OK;
    }
    bool Get(NvPhysicalGpuHandle gpu,NvU32 mask,DWORD& current,DWORD& maximum, BYTE* rawReply=nullptr) {
        BYTE request[]{0x51,0x82,0x01,0x10,0};request[4]=0x6e^request[0]^request[1]^request[2]^request[3];
        if(!TransferData(gpu,mask,request,sizeof(request),false))return false;
        Sleep(80);
        BYTE reply[11]{};
        if(!TransferData(gpu,mask,reply,sizeof(reply),true))return false;
        if(rawReply)CopyMemory(rawReply,reply,sizeof(reply));
        BYTE checksum=0x50;for(auto b:reply)checksum^=b;
        if(reply[0]!=0x6e || reply[1]!=0x88 || reply[2]!=0x02 || reply[3]!=0 || reply[4]!=0x10 || checksum)return false;
        maximum=((DWORD)reply[6]<<8)|reply[7];current=((DWORD)reply[8]<<8)|reply[9];
        return maximum>0 && current<=maximum;
    }
    bool Set(NvPhysicalGpuHandle gpu,NvU32 mask,DWORD value) {
        if(value>65535)return false;
        BYTE request[]{0x51,0x84,0x03,0x10,(BYTE)(value>>8),(BYTE)value,0};
        request[6]=0x6e;for(int i=0;i<6;++i)request[6]^=request[i];
        return TransferData(gpu,mask,request,sizeof(request),false);
    }
};
