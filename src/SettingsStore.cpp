#include "SettingsStore.h"
#include <shlobj.h>
#include <charconv>
#include <string_view>
#include <limits>
namespace Brightness {
bool DefaultSettingsPath(std::wstring& path,DWORD& error){
    wchar_t local[MAX_PATH]{};HRESULT hr=SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,local);
    if(FAILED(hr)){error=ERROR_PATH_NOT_FOUND;return false;}
    path=std::wstring(local)+L"\\DaocBrightness\\settings.ini";error=ERROR_SUCCESS;return true;
}
namespace {
bool Valid(const Settings& value){
    return std::isfinite(value.gamma) && std::isfinite(value.exposure) && std::isfinite(value.contrast) && std::isfinite(value.saturation) && std::isfinite(value.shadows) && Same(value,Clamp(value));
}
std::string_view Trim(std::string_view value){
    while(!value.empty() && (value.front()==' ' || value.front()=='\t' || value.front()=='\r'))value.remove_prefix(1);
    while(!value.empty() && (value.back()==' ' || value.back()=='\t' || value.back()=='\r'))value.remove_suffix(1);
    return value;
}
}
bool LoadSettings(const std::wstring& path,Settings& value,bool& found,DWORD& error){
    value=Settings{};found=false;error=ERROR_SUCCESS;
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){error=GetLastError();if(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND){error=ERROR_SUCCESS;return true;}return false;}
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size)){error=GetLastError();CloseHandle(file);return false;}
    if(size.QuadPart<=0 || size.QuadPart>4096){error=ERROR_INVALID_DATA;CloseHandle(file);return false;}
    std::string content(size_t(size.QuadPart),'\0');DWORD read=0;
    bool ok=ReadFile(file,content.data(),DWORD(content.size()),&read,nullptr)!=FALSE;
    if(!ok)error=GetLastError();CloseHandle(file);
    if(!ok)return false;
    if(read!=content.size() || content.find('\0')!=std::string::npos){error=ERROR_INVALID_DATA;return false;}
    Settings parsed;float* fields[]={&parsed.gamma,&parsed.exposure,&parsed.contrast,&parsed.saturation,&parsed.shadows};
    constexpr std::string_view keys[]={"Gamma","Exposure","Contrast","Saturation","Shadows"};
    unsigned seen=0;bool version=false;
    std::string_view rest(content);
    while(!rest.empty()){
        size_t end=rest.find('\n');auto line=Trim(rest.substr(0,end));
        if(end==std::string_view::npos)rest={};else rest.remove_prefix(end+1);
        if(line.empty() || line.front()==';' || line.front()=='#' || line=="[Brightness]")continue;
        size_t equal=line.find('=');if(equal==std::string_view::npos){error=ERROR_INVALID_DATA;return false;}
        auto key=Trim(line.substr(0,equal)),text=Trim(line.substr(equal+1));
        if(key=="Version"){
            if(text!="1"){error=ERROR_REVISION_MISMATCH;return false;}version=true;continue;
        }
        for(unsigned i=0;i<5;i++)if(key==keys[i]){
            auto converted=std::from_chars(text.data(),text.data()+text.size(),*fields[i]);
            if(converted.ec!=std::errc() || converted.ptr!=text.data()+text.size()){error=ERROR_INVALID_DATA;return false;}
            seen|=1u<<i;break;
        }
    }
    if(!version || seen!=31 || !Valid(parsed)){error=ERROR_INVALID_DATA;return false;}
    value=parsed;found=true;return true;
}
bool SaveSettings(const std::wstring& path,Settings value,DWORD& error){
    value=Clamp(value);if(!Valid(value)){error=ERROR_INVALID_DATA;return false;}
    size_t slash=path.find_last_of(L"\\/");
    if(slash!=std::wstring::npos){
        std::wstring parent=path.substr(0,slash);int created=SHCreateDirectoryExW(nullptr,parent.c_str(),nullptr);
        if(created!=ERROR_SUCCESS && created!=ERROR_ALREADY_EXISTS && created!=ERROR_FILE_EXISTS){error=DWORD(created);return false;}
    }
    std::string content="[Brightness]\r\nVersion=1\r\n";
    const float fields[]={value.gamma,value.exposure,value.contrast,value.saturation,value.shadows};
    const char* keys[]={"Gamma","Exposure","Contrast","Saturation","Shadows"};
    for(unsigned i=0;i<5;i++){
        char number[64]{};auto converted=std::to_chars(number,number+sizeof(number),fields[i],std::chars_format::general,std::numeric_limits<float>::max_digits10);
        if(converted.ec!=std::errc()){error=ERROR_INVALID_DATA;return false;}
        content+=keys[i];content+='=';content.append(number,converted.ptr);content+="\r\n";
    }
    std::wstring temporary=path+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){error=GetLastError();return false;}
    DWORD written=0;bool ok=WriteFile(file,content.data(),DWORD(content.size()),&written,nullptr)!=FALSE;
    if(!ok)error=GetLastError();else if(written!=content.size()){ok=false;error=ERROR_WRITE_FAULT;}
    if(ok && !FlushFileBuffers(file)){ok=false;error=GetLastError();}CloseHandle(file);
    if(ok && !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){ok=false;error=GetLastError();}
    if(!ok){DeleteFileW(temporary.c_str());return false;}error=ERROR_SUCCESS;return true;
}
}
