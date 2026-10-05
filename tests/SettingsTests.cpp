#include "SettingsStore.h"
#include "../src/Launcher.cpp"
#include <cstdio>
#include <filesystem>
#include <limits>
using namespace Brightness;
int failures=0;
void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)failures++;}
Settings Sample(){return Settings{1.7345678f,.456789f,1.234567f,.8765432f,.3456789f};}
bool Write(const std::wstring& path,const char* text){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=WriteFile(file,text,DWORD(strlen(text)),&written,nullptr) && written==strlen(text);CloseHandle(file);return ok;
}
void ChannelTests(){
    Channel exchange{};Settings read;LONG serial=0;Settings render;
    Check(!ReadPublishedSettings(&exchange,read),"unpublished settings ignored");
    PublishSettings(&exchange,render,serial);
    RequestSettings(&exchange,Sample());
    Check(!ReadPublishedSettings(&exchange,read),"stale defaults ignored during restore");
    ReadRequestedSettings(&exchange,serial,render);PublishSettings(&exchange,render,serial);
    Check(ReadPublishedSettings(&exchange,read) && Same(read,Sample()),"restored settings acknowledged");
    render.gamma=2.2f;PublishSettings(&exchange,render,serial);
    Check(ReadPublishedSettings(&exchange,read) && Same(read,render),"menu changes published");
    RequestSettings(&exchange,Settings{});
    Check(!ReadPublishedSettings(&exchange,read),"stale values ignored during reset");
    ReadRequestedSettings(&exchange,serial,render);PublishSettings(&exchange,render,serial);
    Check(ReadPublishedSettings(&exchange,read) && Default(read),"reset acknowledged");
    InterlockedIncrement(&exchange.settingsSequence);
    Check(!ReadPublishedSettings(&exchange,read),"incomplete render publication ignored");
    InterlockedIncrement(&exchange.settingsSequence);InterlockedIncrement(&exchange.sequence);
    render=Sample();ReadRequestedSettings(&exchange,serial,render);
    Check(Same(render,Sample()) && !ReadPublishedSettings(&exchange,read),"incomplete controller command ignored");
}
void ControllerTests(const std::wstring& path){
    status=CreateWindowW(L"STATIC",L"",0,0,0,276,54,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    settingsPath=path;currentSettings=Settings{};lastSaved=Settings{};pendingSave=false;saveAttempted=false;
    Channel shared{};channel=&shared;connectedPid=GetCurrentProcessId();
    LONG serial=0;Settings render,value;bool found=false;DWORD error=0;
    RequestSettings(channel,Sample());
    PublishSettings(channel,Settings{},0);SyncSettings();
    Check(!pendingSave && Default(currentSettings),"controller ignores unacknowledged stale values");
    ReadRequestedSettings(channel,serial,render);PublishSettings(channel,render,serial);SyncSettings();
    Check(pendingSave && Same(currentSettings,Sample()) && GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES,"controller defers save during adjustment");
    changedAt-=600;SyncSettings();
    Check(!pendingSave && LoadSettings(path,value,found,error) && found && Same(value,Sample()),"controller automatically saves stable adjustment");
    render.exposure=.9f;PublishSettings(channel,render,serial);SyncSettings(true);
    Check(!pendingSave && LoadSettings(path,value,found,error) && Same(value,render),"controller flushes final adjustment on exit");
    ResetSettings();
    Check(LoadSettings(path,value,found,error) && Default(value) && Default(currentSettings),"controller reset persists before game acknowledgement");
    ReadRequestedSettings(channel,serial,render);PublishSettings(channel,render,serial);SyncSettings();
    Check(Default(currentSettings) && !pendingSave,"reset acknowledgement retains persisted defaults");
    channel=nullptr;connectedPid=0;
    DestroyWindow(status);status=nullptr;
}
int wmain(int argc,wchar_t** argv){
    Settings value;bool found=false;DWORD error=0;
    if(argc==3 && !wcscmp(argv[1],L"--read"))return LoadSettings(argv[2],value,found,error) && found && Same(value,Sample())?0:1;
    ChannelTests();
    wchar_t temporary[MAX_PATH]{};GetTempPathW(MAX_PATH,temporary);
    std::filesystem::path root=std::filesystem::path(temporary)/(L"DaocBrightnessSettingsTests-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::wstring path=(root/L"nested"/L"settings.ini").wstring();
    Check(LoadSettings(path,value,found,error) && !found && Default(value),"first run defaults without file");
    Check(SaveSettings(path,Sample(),error),"save all five controls and create directory");
    Check(LoadSettings(path,value,found,error) && found && Same(value,Sample()),"exact float roundtrip");
    wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);
    std::wstring command=L"\""+std::wstring(executable)+L"\" --read \""+path+L"\"";
    STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION child{};
    bool created=CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&child)!=FALSE;
    DWORD code=1;
    if(created){if(WaitForSingleObject(child.hProcess,10000)==WAIT_OBJECT_0)GetExitCodeProcess(child.hProcess,&code);CloseHandle(child.hThread);CloseHandle(child.hProcess);}
    Check(created && code==0,"new process restores saved values");
    HANDLE locked=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(locked!=INVALID_HANDLE_VALUE && !SaveSettings(path,Settings{},error),"locked destination reports save failure");
    if(locked!=INVALID_HANDLE_VALUE)CloseHandle(locked);
    Check(LoadSettings(path,value,found,error) && Same(value,Sample()),"failed replacement preserves previous settings");
    size_t files=0;for(const auto& entry:std::filesystem::directory_iterator(root/L"nested")){(void)entry;files++;}
    Check(files==1,"failed save removes temporary file");
    Check(SaveSettings(path,Settings{},error) && LoadSettings(path,value,found,error) && found && Default(value),"reset defaults saved and restored");
    const char* invalid[]={
        "[Brightness]\nVersion=1\nGamma=nan\nExposure=0\nContrast=1\nSaturation=1\nShadows=0\n",
        "[Brightness]\nVersion=1\nGamma=9\nExposure=0\nContrast=1\nSaturation=1\nShadows=0\n",
        "[Brightness]\nVersion=1\nGamma=1\nExposure=0\nContrast=1\nSaturation=1\n",
        "[Brightness]\nVersion=2\nGamma=1\nExposure=0\nContrast=1\nSaturation=1\nShadows=0\n",
        "[Brightness]\nVersion=1\nGamma=1oops\nExposure=0\nContrast=1\nSaturation=1\nShadows=0\n"
    };
    for(auto text:invalid){value=Sample();Check(Write(path,text) && !LoadSettings(path,value,found,error) && !found && Default(value) && error!=0,"invalid settings fall back safely");}
    value=Sample();value.gamma=std::numeric_limits<float>::quiet_NaN();
    Check(!SaveSettings(path,value,error),"nonfinite settings cannot be saved");
    ControllerTests((root/L"controller"/L"settings.ini").wstring());
    std::filesystem::remove_all(root);
    printf("Failures: %d\n",failures);return failures?1:0;
}
