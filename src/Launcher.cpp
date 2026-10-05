#include "Shared.h"
#include "ProcessDiscovery.h"
#include "resource.h"
#include "EmbeddedHook.h"
#include "UiText.h"
#include "SettingsStore.h"
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <cstdio>
namespace {
using namespace Brightness;
using Ui::Id;
std::vector<Candidate> candidates;
HWND list=nullptr,status=nullptr,connectButton=nullptr;
HWND settingsStatus=nullptr;
HANDLE mapping=nullptr; Channel* channel=nullptr; DWORD connectedPid=0; HANDLE targetProcess=nullptr;
std::wstring settingsPath;
Settings currentSettings,lastSaved;
bool pendingSave=false,saveAttempted=false;
DWORD changedAt=0,lastSaveAttempt=0;
void Message(HWND w,const std::wstring& text){MessageBoxW(w,text.c_str(),Ui::W(Id::AppTitle),MB_OK|MB_ICONINFORMATION);}
std::wstring Error(DWORD code){
    wchar_t* text=nullptr;
    DWORD flags=FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD count=FormatMessageW(flags,nullptr,code,Ui::MessageLanguage(),reinterpret_cast<LPWSTR>(&text),0,nullptr);
    if(!count && Ui::Current()==Ui::Language::Korean){if(text)LocalFree(text);text=nullptr;FormatMessageW(flags,nullptr,code,MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),reinterpret_cast<LPWSTR>(&text),0,nullptr);}
    std::wstring out=text?text:std::wstring(Ui::W(Id::UnknownError))+L" ("+std::to_wstring(code)+L")";
    if(text)LocalFree(text);return out;
}
void InitSettings(){
    DWORD error=0;bool found=false;
    if(!DefaultSettingsPath(settingsPath,error) || !LoadSettings(settingsPath,currentSettings,found,error)){
        currentSettings=Settings{};SetWindowTextW(settingsStatus,(Ui::W(Id::SettingsLoadFailed)+Error(error)).c_str());
    }else SetWindowTextW(settingsStatus,Ui::W(found?Id::SettingsRestored:Id::SettingsDefaults));
    lastSaved=currentSettings;
}
void SyncSettings(bool force=false){
    if(channel && connectedPid){
        Settings snapshot;
        for(int retry=0;retry<4;retry++)if(ReadPublishedSettings(channel,snapshot)){
            if(!Same(snapshot,currentSettings)){currentSettings=snapshot;changedAt=GetTickCount();pendingSave=!Same(currentSettings,lastSaved);saveAttempted=false;}
            break;
        }
    }
    if(!pendingSave)return;
    DWORD now=GetTickCount();
    if(!force && (now-changedAt<500 || (saveAttempted && now-lastSaveAttempt<2000)))return;
    DWORD error=ERROR_PATH_NOT_FOUND;saveAttempted=true;lastSaveAttempt=now;
    if(!settingsPath.empty() && SaveSettings(settingsPath,currentSettings,error)){
        lastSaved=currentSettings;pendingSave=false;SetWindowTextW(settingsStatus,Ui::W(Id::SettingsSaved));
    }else SetWindowTextW(settingsStatus,(Ui::W(Id::SettingsSaveFailed)+Error(error)).c_str());
}
void ResetSettings(){
    currentSettings=Settings{};if(channel)RequestSettings(channel,currentSettings);
    pendingSave=true;changedAt=GetTickCount();saveAttempted=false;SyncSettings(true);
}
std::vector<Candidate> Find() {
    DWORD error=0;auto found=FindGames(&error);
    if(error && !channel)SetWindowTextW(status,(Ui::W(Id::ProcessQueryFailed)+Error(error)).c_str());
    return found;
}
void Refresh(){
    DWORD selected=0;LRESULT index=SendMessageW(list,CB_GETCURSEL,0,0);if(index>=0 && size_t(index)<candidates.size())selected=candidates[index].pid;
    if(!selected)GetWindowThreadProcessId(GetForegroundWindow(),&selected);
    candidates=Find();SendMessageW(list,CB_RESETCONTENT,0,0);int choice=0;
    for(size_t i=0;i<candidates.size();i++){
        auto& c=candidates[i];wchar_t text[256]{};
        std::wstring state=c.dx9 && c.dx10?L"DX9 / DX10":c.dx9?L"DX9":c.dx10?L"DX10":
            c.scanError==ERROR_ACCESS_DENIED?Ui::W(Id::ModuleAccessDenied):
            c.scanError?Ui::W(Id::ModuleQueryError)+std::to_wstring(c.scanError):Ui::W(Id::WaitingDirectX);
        swprintf_s(text,L"PID %lu | %s | %s",c.pid,c.name.c_str(),state.c_str());
        SendMessageW(list,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));if(c.pid==selected)choice=int(i);
    }if(!candidates.empty()){
        SendMessageW(list,CB_SETCURSEL,choice,0);
        if(!channel)SetWindowTextW(status,Ui::W(Id::GameDetected));
    }
}
bool Module(DWORD pid,const wchar_t* name,uintptr_t& base,std::wstring* path=nullptr) {
    HANDLE snap=ModuleSnapshot(pid);if(snap==INVALID_HANDLE_VALUE)return false;
    MODULEENTRY32W me{};me.dwSize=sizeof(me);bool result=false;
    for(BOOL ok=Module32FirstW(snap,&me);ok;ok=Module32NextW(snap,&me))if(!_wcsicmp(name,me.szModule)){base=reinterpret_cast<uintptr_t>(me.modBaseAddr);if(path)*path=me.szExePath;result=true;break;}
    CloseHandle(snap);return result;
}
bool Inject(HANDLE process,DWORD pid,const std::wstring& path,DWORD& error) {
    uintptr_t loaded=0;std::wstring loadedPath;
    if(Module(pid,L"DaocBrightnessHook.dll",loaded,&loadedPath)){
        if(IsEmbeddedHookFile(GetModuleHandleW(nullptr),loadedPath,error))return true;
        error=ERROR_REVISION_MISMATCH;return false;
    }
    BOOL targetWow=FALSE,selfWow=FALSE;
    if(!IsWow64Process(process,&targetWow) || !IsWow64Process(GetCurrentProcess(),&selfWow)){error=GetLastError();return false;}
    if(targetWow!=selfWow){error=ERROR_BAD_EXE_FORMAT;return false;}
    auto load=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");
    HMODULE implementation=nullptr;
    if(!load || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(load),&implementation)){error=GetLastError();return false;}
    wchar_t localPath[MAX_PATH]{};GetModuleFileNameW(implementation,localPath,MAX_PATH);const wchar_t* basename=wcsrchr(localPath,L'\\');
    uintptr_t remoteBase=0;if(!Module(pid,basename?basename+1:localPath,remoteBase)){error=ERROR_MOD_NOT_FOUND;return false;}
    auto remoteLoad=reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteBase+(reinterpret_cast<uintptr_t>(load)-reinterpret_cast<uintptr_t>(implementation)));
    SIZE_T bytes=(path.size()+1)*sizeof(wchar_t);void* memory=VirtualAllocEx(process,nullptr,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!memory){error=GetLastError();return false;}
    if(!WriteProcessMemory(process,memory,path.c_str(),bytes,nullptr)){error=GetLastError();VirtualFreeEx(process,memory,0,MEM_RELEASE);return false;}
    HANDLE thread=CreateRemoteThread(process,nullptr,0,remoteLoad,memory,0,nullptr);
    if(!thread){error=GetLastError();VirtualFreeEx(process,memory,0,MEM_RELEASE);return false;}
    DWORD wait=WaitForSingleObject(thread,10000);DWORD code=0;
    if(wait==WAIT_OBJECT_0){GetExitCodeThread(thread,&code);VirtualFreeEx(process,memory,0,MEM_RELEASE);error=code?ERROR_SUCCESS:ERROR_DLL_INIT_FAILED;}
    else error=wait==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError();
    CloseHandle(thread);return wait==WAIT_OBJECT_0 && code!=0;
}
void Disconnect(){
    SyncSettings(true);
    if(channel){InterlockedExchange(&channel->enabled,0);UnmapViewOfFile(channel);channel=nullptr;}
    if(mapping){CloseHandle(mapping);mapping=nullptr;}if(targetProcess){CloseHandle(targetProcess);targetProcess=nullptr;}
    connectedPid=0;SetWindowTextW(connectButton,Ui::W(Id::Connect));SetWindowTextW(status,Ui::W(Id::Disconnected));
}
void Connect(HWND w){
    if(channel){Disconnect();return;}
    LRESULT index=SendMessageW(list,CB_GETCURSEL,0,0);
    if(index<0 || size_t(index)>=candidates.size()){Message(w,Ui::W(Id::NoGame));return;}
    auto c=candidates[index];
    if(c.scanError==ERROR_ACCESS_DENIED){Message(w,Ui::W(Id::NoModulePermission));return;}
    if(c.scanError){Message(w,Ui::W(Id::ModuleQueryFailed)+Error(c.scanError)+Ui::W(Id::RefreshAndRetry));return;}
    if(!c.dx9 && !c.dx10){Message(w,Ui::W(Id::WaitDirectX));return;}
    HookLease hook;DWORD extractionError=0;
    if(!ExtractHook(GetModuleHandleW(nullptr),hook,extractionError)){Message(w,Ui::W(Id::EmbeddedModuleFailed)+Error(extractionError));return;}
    targetProcess=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ|SYNCHRONIZE,FALSE,c.pid);
    if(!targetProcess){Message(w,Ui::W(Id::ProcessAccessFailed)+Error(GetLastError())+Ui::W(Id::SamePermission));return;}
    wchar_t name[96]{};Name(name,_countof(name),c.pid);mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Channel),name);DWORD existed=GetLastError();
    if(mapping && existed==ERROR_ALREADY_EXISTS){
        const DWORD* version=static_cast<const DWORD*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(DWORD)));
        bool incompatible=version && *version!=Protocol;if(version)UnmapViewOfFile(version);
        if(incompatible){CloseHandle(mapping);mapping=nullptr;CloseHandle(targetProcess);targetProcess=nullptr;Message(w,Ui::W(Id::ChannelVersionMismatch));return;}
    }
    if(mapping)channel=static_cast<Channel*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Channel)));
    if(!channel){DWORD e=GetLastError();Disconnect();Message(w,Ui::W(Id::ChannelFailed)+Error(e));return;}
    if(existed!=ERROR_ALREADY_EXISTS){ZeroMemory(channel,sizeof(Channel));channel->protocol=Protocol;channel->settings=Settings{};}
    if(channel->protocol!=Protocol){Disconnect();Message(w,Ui::W(Id::ChannelVersionMismatch));return;}
    if(channel->enabled && DWORD(GetTickCount()-DWORD(channel->heartbeat))<5000 && DWORD(channel->owner)!=GetCurrentProcessId()) {
        UnmapViewOfFile(channel);channel=nullptr;CloseHandle(mapping);mapping=nullptr;CloseHandle(targetProcess);targetProcess=nullptr;
        Message(w,Ui::W(Id::AlreadyConnected));return;
    }
    InterlockedExchange(&channel->owner,GetCurrentProcessId());InterlockedExchange(&channel->heartbeat,GetTickCount());
    InterlockedExchange(&channel->error,0);InterlockedExchange(&channel->renderer,0);InterlockedExchange(&channel->frames,0);
    RequestSettings(channel,currentSettings);InterlockedExchange(&channel->enabled,1);
    DWORD error=0;if(!Inject(targetProcess,c.pid,hook.path,error)){Disconnect();
        Message(w,error==ERROR_REVISION_MISMATCH?Ui::W(Id::ModuleVersionMismatch):Ui::W(Id::ConnectionFailed)+Error(error));return;}
    connectedPid=c.pid;SetWindowTextW(connectButton,Ui::W(Id::Disconnect));SetWindowTextW(status,Ui::W(Id::WaitingFrames));
}
HWND Control(HWND w,const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id){
    HWND control=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,w,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return control;
}
LRESULT CALLBACK LicenseProc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_CREATE){
        std::wstring text=LicenseText(GetModuleHandleW(nullptr));
        Control(w,L"EDIT",text.c_str(),ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_HSCROLL,8,8,650,480,110);
        return 0;
    }
    if(msg==WM_SIZE){MoveWindow(GetDlgItem(w,110),8,8,std::max(1,int(LOWORD(lp))-16),std::max(1,int(HIWORD(lp))-16),TRUE);return 0;}
    if(msg==WM_CLOSE){DestroyWindow(w);return 0;}
    return DefWindowProcW(w,msg,wp,lp);
}
void Licenses(HWND owner){
    WNDCLASSW wc{};wc.lpfnWndProc=LicenseProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"DaocBrightnessLicenses";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&wc);
    HWND w=CreateWindowW(wc.lpszClassName,Ui::W(Id::LicenseTitle),WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,700,550,owner,nullptr,wc.hInstance,nullptr);
    if(w)ShowWindow(w,SW_SHOWNORMAL);
}
LRESULT CALLBACK Proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:
        Control(w,L"STATIC",Ui::W(Id::MainTitle),0,20,16,430,24,0);
        list=Control(w,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,20,48,450,180,100);
        Control(w,L"BUTTON",Ui::W(Id::Refresh),0,20,88,105,32,101);
        connectButton=Control(w,L"BUTTON",Ui::W(Id::Connect),0,140,88,105,32,102);
        Control(w,L"BUTTON",Ui::W(Id::Reset),0,260,88,130,32,103);
        Control(w,L"BUTTON",Ui::W(Id::Licenses),0,400,88,80,32,104);
        Control(w,L"STATIC",Ui::W(Id::Instructions),0,20,140,450,48,0);
        status=Control(w,L"STATIC",Ui::W(Id::SelectGame),0,20,202,450,70,0);
        settingsStatus=Control(w,L"STATIC",Ui::W(Id::SettingsDefaults),0,20,278,450,42,0);
        InitSettings();
        Refresh();SetTimer(w,1,500,nullptr);SetTimer(w,2,2000,nullptr);return 0;
    case WM_COMMAND:
        if(LOWORD(wp)==101)Refresh();else if(LOWORD(wp)==102)Connect(w);else if(LOWORD(wp)==103)ResetSettings();else if(LOWORD(wp)==104)Licenses(w);return 0;
    case WM_TIMER:
        if(wp==2){if(!channel && !SendMessageW(list,CB_GETDROPPEDSTATE,0,0))Refresh();return 0;}
        if(channel){
            if(WaitForSingleObject(targetProcess,0)==WAIT_OBJECT_0){Disconnect();Refresh();return 0;}
            InterlockedExchange(&channel->heartbeat,GetTickCount());wchar_t text[256]{};
            LONG error=InterlockedCompareExchange(&channel->error,0,0);LONG renderer=InterlockedCompareExchange(&channel->renderer,0,0);
            if(FAILED(error))swprintf_s(text,Ui::W(Id::RenderingError),connectedPid,error);
            else if(renderer)swprintf_s(text,Ui::W(Id::ActiveFrames),connectedPid,renderer,channel->frames);
            else swprintf_s(text,Ui::W(Id::WaitingRenderer),connectedPid);
            SetWindowTextW(status,text);
        }SyncSettings();return 0;
    case WM_DESTROY:KillTimer(w,1);KillTimer(w,2);Disconnect();PostQuitMessage(0);return 0;
    }return DefWindowProcW(w,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    HANDLE single=CreateMutexW(nullptr,TRUE,L"Local\\DaocBrightnessController");if(GetLastError()==ERROR_ALREADY_EXISTS){Message(nullptr,Ui::W(Id::AlreadyRunning));if(single)CloseHandle(single);return 0;}
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=Proc;wc.hInstance=instance;wc.lpszClassName=L"DaocBrightnessController";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    wc.hIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_APP_ICON),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_SHARED));
    wc.hIconSm=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_APP_ICON),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
    RegisterClassExW(&wc);
    HWND w=CreateWindowW(wc.lpszClassName,Ui::W(Id::AppTitle),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,510,370,nullptr,nullptr,instance,nullptr);
    if(!w){if(single)CloseHandle(single);return 1;}ShowWindow(w,show);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}if(single)CloseHandle(single);return 0;
}
