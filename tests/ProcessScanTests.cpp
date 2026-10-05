#include "ProcessDiscovery.h"
#include <cstdio>
#include <algorithm>
using namespace Brightness;
int failures=0;
void Check(bool ok,const wchar_t* name){wprintf(L"%s %s\n",ok?L"PASS":L"FAIL",name);if(!ok)failures++;}
int Child(const wchar_t* readyName,const wchar_t* stopName) {
    HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,readyName);
    HANDLE stop=OpenEventW(SYNCHRONIZE,FALSE,stopName);
    if(!ready || !stop)return 2;
    HMODULE dx=LoadLibraryW(L"d3d9.dll");SetEvent(ready);WaitForSingleObject(stop,30000);
    if(dx)FreeLibrary(dx);CloseHandle(ready);CloseHandle(stop);return 0;
}
void SyntheticGame() {
    wchar_t temp[MAX_PATH]{},unique[MAX_PATH]{},self[MAX_PATH]{};
    GetTempPathW(MAX_PATH,temp);if(!GetTempFileNameW(temp,L"dbc",0,unique)){Check(false,L"test directory");return;}
    DeleteFileW(unique);if(!CreateDirectoryW(unique,nullptr)){Check(false,L"test directory");return;}
    std::wstring executable=std::wstring(unique)+L"\\eden.dll";
    GetModuleFileNameW(nullptr,self,MAX_PATH);
    bool copied=CopyFileW(self,executable.c_str(),TRUE)!=FALSE;Check(copied,L"executable with .dll extension");
    std::wstring id=std::to_wstring(GetCurrentProcessId());
    std::wstring readyName=L"Local\\BrightnessDiscoveryReady-"+id,stopName=L"Local\\BrightnessDiscoveryStop-"+id;
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,readyName.c_str());HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,stopName.c_str());
    std::wstring command=L"\""+executable+L"\" --child "+readyName+L" "+stopName;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    bool started=copied && ready && stop && CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);
    Check(started,L"spawn isolated game-shaped process");
    if(started){
        Check(WaitForSingleObject(ready,10000)==WAIT_OBJECT_0,L"child DirectX module ready");
        DWORD error=0;auto found=FindGames(&error);
        auto match=std::find_if(found.begin(),found.end(),[&](const Candidate& c){return c.pid==process.dwProcessId;});
        Check(error==0 && match!=found.end(),L"discover .dll executable PID");
        if(match!=found.end())Check(match->dx9 && match->scanError==0,L"discover DirectX 9 modules");
        SetEvent(stop);Check(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0,L"child graceful exit");
        CloseHandle(process.hThread);CloseHandle(process.hProcess);
    }
    if(ready)CloseHandle(ready);if(stop)CloseHandle(stop);DeleteFileW(executable.c_str());RemoveDirectoryW(unique);
}
int wmain(int argc,wchar_t** argv) {
    if(argc==4 && !wcscmp(argv[1],L"--child"))return Child(argv[2],argv[3]);
    SyntheticGame();DWORD error=0;auto games=FindGames(&error);Check(error==0,L"live process snapshot");
    for(const auto& game:games)wprintf(L"PID %lu | %s | DX9=%d DX10=%d | module-error=%lu\n",game.pid,game.name.c_str(),game.dx9,game.dx10,game.scanError);
    if(argc==3 && !wcscmp(argv[1],L"--expect-pid")) {
        DWORD expected=wcstoul(argv[2],nullptr,10);
        Check(std::any_of(games.begin(),games.end(),[&](const Candidate& c){return c.pid==expected;}),L"running game remains selectable even when module access is denied");
    }
    wprintf(L"Failures: %d\n",failures);return failures?1:0;
}
