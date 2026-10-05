#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
namespace Brightness {
struct Candidate {
    DWORD pid;
    std::wstring name;
    bool dx9=false, dx10=false;
    DWORD scanError=ERROR_SUCCESS;
};
inline bool GameImage(const wchar_t* name) {
    return !_wcsicmp(name,L"game.dll") || !_wcsicmp(name,L"eden.dll");
}
inline HANDLE ModuleSnapshot(DWORD pid) {
    for(unsigned retry=0;retry<5;retry++) {
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
        if(snapshot!=INVALID_HANDLE_VALUE || GetLastError()!=ERROR_BAD_LENGTH)return snapshot;
        Sleep(1);
    }
    SetLastError(ERROR_BAD_LENGTH);return INVALID_HANDLE_VALUE;
}
inline std::vector<Candidate> FindGames(DWORD* scanError=nullptr) {
    if(scanError)*scanError=ERROR_SUCCESS;
    std::vector<Candidate> out;
    HANDLE processes=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(processes==INVALID_HANDLE_VALUE){if(scanError)*scanError=GetLastError();return out;}
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    BOOL ok=Process32FirstW(processes,&entry);
    if(!ok && scanError)*scanError=GetLastError();
    for(;ok;ok=Process32NextW(processes,&entry)) {
        // The game image itself is an executable with a .dll extension.
        // Retain its PID even if an elevated process denies module access.
        bool game=GameImage(entry.szExeFile);
        Candidate candidate{entry.th32ProcessID,entry.szExeFile};
        HANDLE modules=ModuleSnapshot(entry.th32ProcessID);
        if(modules==INVALID_HANDLE_VALUE)candidate.scanError=GetLastError();
        else {
            MODULEENTRY32W module{};module.dwSize=sizeof(module);
            BOOL found=Module32FirstW(modules,&module);
            if(!found)candidate.scanError=GetLastError();
            for(;found;found=Module32NextW(modules,&module)) {
                if(GameImage(module.szModule))game=true;
                if(!_wcsicmp(module.szModule,L"d3d9.dll"))candidate.dx9=true;
                if(!_wcsicmp(module.szModule,L"d3d10.dll") || !_wcsicmp(module.szModule,L"d3d10_1.dll"))candidate.dx10=true;
            }
            CloseHandle(modules);
        }
        if(game)out.push_back(candidate);
    }
    CloseHandle(processes);return out;
}
}
