#pragma once
#include <windows.h>
#include <string>
namespace Brightness {
struct HookLease {
    std::wstring path;
    HANDLE file=INVALID_HANDLE_VALUE;
    HookLease()=default;
    HookLease(const HookLease&)=delete;
    HookLease& operator=(const HookLease&)=delete;
    ~HookLease(){Close();}
    void Close(){if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);file=INVALID_HANDLE_VALUE;}}
};
bool ResourceBytes(HMODULE module,int id,const BYTE*& data,DWORD& size,DWORD& error);
bool ExtractHook(HMODULE module,HookLease& lease,DWORD& error,const std::wstring& cacheRoot=L"");
bool IsEmbeddedHookFile(HMODULE module,const std::wstring& path,DWORD& error);
std::wstring LicenseText(HMODULE module);
}
