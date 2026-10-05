#include "EmbeddedHook.h"
#include "resource.h"
#include "UiText.h"
#include <bcrypt.h>
#include <shlobj.h>
#include <vector>
#include <algorithm>
#include <cstring>
namespace Brightness {
bool ResourceBytes(HMODULE module,int id,const BYTE*& data,DWORD& size,DWORD& error){
    data=nullptr;size=0;error=ERROR_SUCCESS;
    HRSRC resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);
    if(!resource){error=GetLastError();return false;}
    size=SizeofResource(module,resource);
    data=static_cast<const BYTE*>(LockResource(LoadResource(module,resource)));
    if(!size || !data){error=ERROR_RESOURCE_DATA_NOT_FOUND;return false;}return true;
}
namespace {
bool Fingerprint(const BYTE* data,DWORD size,std::wstring& text,DWORD& error){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    NTSTATUS status=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0);
    if(status>=0)status=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0);
    if(status>=0)status=BCryptHashData(hash,const_cast<BYTE*>(data),size,0);
    BYTE digest[32]{};if(status>=0)status=BCryptFinishHash(hash,digest,sizeof(digest),0);
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0){error=ERROR_INVALID_DATA;return false;}
    constexpr wchar_t hex[]=L"0123456789abcdef";text.clear();
    for(BYTE b:digest){text+=hex[b>>4];text+=hex[b&15];}return true;
}
bool Matches(HANDLE file,const BYTE* data,DWORD size,DWORD& error){
    LARGE_INTEGER length{};
    if(!GetFileSizeEx(file,&length)){error=GetLastError();return false;}
    if(length.QuadPart!=size){error=ERROR_CRC;return false;}
    LARGE_INTEGER start{};
    if(!SetFilePointerEx(file,start,nullptr,FILE_BEGIN)){error=GetLastError();return false;}
    BYTE chunk[16384]{};
    for(DWORD offset=0;offset<size;){
        DWORD count=std::min<DWORD>(DWORD(sizeof(chunk)),size-offset),read=0;
        if(!ReadFile(file,chunk,count,&read,nullptr)){error=GetLastError();return false;}
        if(read!=count || memcmp(chunk,data+offset,count)){error=ERROR_CRC;return false;}
        offset+=count;
    }error=ERROR_SUCCESS;return true;
}
bool OpenVerified(const std::wstring& path,const BYTE* data,DWORD size,HookLease& lease,DWORD& error){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){error=GetLastError();return false;}
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file,&info) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){error=ERROR_ACCESS_DENIED;CloseHandle(file);return false;}
    if(!Matches(file,data,size,error)){CloseHandle(file);return false;}
    lease.Close();lease.path=path;lease.file=file;return true;
}
}
bool ExtractHook(HMODULE module,HookLease& lease,DWORD& error,const std::wstring& cacheRoot){
    lease.Close();lease.path.clear();const BYTE* data=nullptr;DWORD size=0;
    if(!ResourceBytes(module,IDR_HOOK_DLL,data,size,error))return false;
    std::wstring hash;if(!Fingerprint(data,size,hash,error))return false;
    std::wstring root=cacheRoot;
    if(root.empty()){
        wchar_t local[MAX_PATH]{};HRESULT hr=SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,local);
        if(FAILED(hr)){error=ERROR_PATH_NOT_FOUND;return false;}
        root=std::wstring(local)+L"\\DaocBrightness\\Hook";
    }
    std::wstring directory=root+L"\\"+hash;
    for(std::wstring parent=directory;parent.size()>3;){
        DWORD existing=GetFileAttributesW(parent.c_str());
        if(existing!=INVALID_FILE_ATTRIBUTES && (existing&FILE_ATTRIBUTE_REPARSE_POINT)){error=ERROR_ACCESS_DENIED;return false;}
        size_t slash=parent.find_last_of(L"\\/");if(slash==std::wstring::npos)break;parent.resize(slash);
    }
    int created=SHCreateDirectoryExW(nullptr,directory.c_str(),nullptr);
    if(created!=ERROR_SUCCESS && created!=ERROR_ALREADY_EXISTS && created!=ERROR_FILE_EXISTS){error=DWORD(created);return false;}
    DWORD attributes=GetFileAttributesW(directory.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT)){error=ERROR_ACCESS_DENIED;return false;}
    std::wstring path=directory+L"\\DaocBrightnessHook.dll";
    if(OpenVerified(path,data,size,lease,error))return true;
    if(error==ERROR_ACCESS_DENIED)return false;
    // Rewrite a stale or incomplete cache only when it is not in use.
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){error=GetLastError();return false;}
    DWORD written=0;bool ok=WriteFile(file,data,size,&written,nullptr)!=FALSE;
    if(!ok)error=GetLastError();else if(written!=size){ok=false;error=ERROR_WRITE_FAULT;}
    if(ok && !FlushFileBuffers(file)){ok=false;error=GetLastError();}
    CloseHandle(file);
    if(!ok)return false;
    return OpenVerified(path,data,size,lease,error);
}
bool IsEmbeddedHookFile(HMODULE module,const std::wstring& path,DWORD& error){
    const BYTE* data=nullptr;DWORD size=0;
    if(!ResourceBytes(module,IDR_HOOK_DLL,data,size,error))return false;
    HookLease lease;return OpenVerified(path,data,size,lease,error);
}
std::wstring LicenseText(HMODULE module){
    const BYTE* data=nullptr;DWORD size=0,error=0;
    if(!ResourceBytes(module,IDR_LICENSES,data,size,error))return Ui::W(Ui::Id::LicenseReadFailed);
    int length=MultiByteToWideChar(CP_UTF8,0,reinterpret_cast<const char*>(data),int(size),nullptr,0);
    if(!length)return Ui::W(Ui::Id::LicenseReadFailed);
    std::wstring text(size_t(length),L'\0');
    MultiByteToWideChar(CP_UTF8,0,reinterpret_cast<const char*>(data),int(size),text.data(),length);
    std::wstring normalized;for(size_t i=0;i<text.size();i++){if(text[i]==L'\n' && (i==0 || text[i-1]!=L'\r'))normalized+=L'\r';normalized+=text[i];}return normalized;
}
}
