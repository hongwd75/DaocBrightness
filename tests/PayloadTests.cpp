#include "EmbeddedHook.h"
#include "resource.h"
#include <cstdio>
using namespace Brightness;
int failures=0;
void Check(bool ok,const wchar_t* name){wprintf(L"%s %s\n",ok?L"PASS":L"FAIL",name);if(!ok)failures++;}
int wmain(int argc,wchar_t** argv){
    if(argc==3 && !wcscmp(argv[1],L"--load")){
        // Exercise the real loader in an isolated process; keep the hook resident.
        return LoadLibraryW(argv[2])?0:1;
    }
    if(argc<2){wprintf(L"Usage: PayloadTests.exe <standalone-exe> [built-dll]\n");return 2;}
    HMODULE module=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_AS_DATAFILE);
    Check(module!=nullptr,L"load standalone EXE resources without running UI");if(!module)return 1;
    const BYTE* payload=nullptr;DWORD size=0,error=0;
    Check(ResourceBytes(module,IDR_HOOK_DLL,payload,size,error) && size>0,L"embedded DLL resource");
    if(argc==3)Check(IsEmbeddedHookFile(module,argv[2],error),L"embedded bytes equal freshly built DLL");
    std::wstring license=LicenseText(module);
    Check(license.find(L"Dear ImGui")!=std::wstring::npos && license.find(L"MinHook")!=std::wstring::npos,L"third-party notices embedded");
    wchar_t temp[MAX_PATH]{},root[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);
    if(!GetTempFileNameW(temp,L"dbh",0,root)){Check(false,L"temporary test root");FreeLibrary(module);return 1;}
    DeleteFileW(root);CreateDirectoryW(root,nullptr);
    HookLease first;bool extracted=ExtractHook(module,first,error,root);
    Check(extracted,L"extract DLL with no adjacent DLL");
    if(extracted){
        Check(IsEmbeddedHookFile(module,first.path,error),L"extracted bytes match EXE resource");
        HookLease second;Check(ExtractHook(module,second,error,root) && second.path==first.path,L"reuse verified cache while read lease is held");
        HANDLE write=CreateFileW(first.path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        Check(write==INVALID_HANDLE_VALUE,L"lease prevents DLL replacement during injection");if(write!=INVALID_HANDLE_VALUE)CloseHandle(write);
        second.Close();first.Close();
        write=CreateFileW(first.path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
        DWORD written=0;const char bad[]="incomplete cache";
        if(write!=INVALID_HANDLE_VALUE){WriteFile(write,bad,sizeof(bad),&written,nullptr);CloseHandle(write);}
        Check(!IsEmbeddedHookFile(module,first.path,error),L"detect damaged cache");
        Check(ExtractHook(module,first,error,root) && IsEmbeddedHookFile(module,first.path,error),L"repair damaged cache");
        wchar_t self[MAX_PATH]{};GetModuleFileNameW(nullptr,self,MAX_PATH);
        std::wstring command=L"\""+std::wstring(self)+L"\" --load \""+first.path+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        bool started=CreateProcessW(self,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE;
        DWORD exitCode=1;
        if(started){DWORD wait=WaitForSingleObject(process.hProcess,10000);if(wait==WAIT_OBJECT_0)GetExitCodeProcess(process.hProcess,&exitCode);CloseHandle(process.hThread);CloseHandle(process.hProcess);}
        Check(started && exitCode==0,L"extracted DLL loads with system dependencies");
        first.Close();DeleteFileW(first.path.c_str());
        std::wstring folder=first.path.substr(0,first.path.find_last_of(L'\\'));RemoveDirectoryW(folder.c_str());
    }
    RemoveDirectoryW(root);FreeLibrary(module);wprintf(L"Failures: %d\n",failures);return failures?1:0;
}
