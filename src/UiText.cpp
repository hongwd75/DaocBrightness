#include "UiText.h"
#include <array>
namespace Brightness::Ui {
std::wstring ReadString(HMODULE module,Id id,Language language){
    UINT number=static_cast<UINT>(id);
    HRSRC resource=FindResourceExW(module,RT_STRING,MAKEINTRESOURCEW((number>>4)+1),LanguageId(language));
    if(!resource)return {};
    DWORD bytes=SizeofResource(module,resource);
    const WORD* text=static_cast<const WORD*>(LockResource(LoadResource(module,resource)));
    if(!text || bytes%sizeof(WORD))return {};
    const WORD* end=text+bytes/sizeof(WORD);
    for(UINT index=0;index<=(number&15);index++){
        if(text==end)return {};
        WORD length=*text++;
        if(size_t(end-text)<length)return {};
        if(index==(number&15))return std::wstring(reinterpret_cast<const wchar_t*>(text),length);
        text+=length;
    }
    return {};
}
namespace {
HMODULE OwnModule(){
    static HMODULE module=[](){
        HMODULE result=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&OwnModule),&result);
        return result;
    }();
    return module;
}
struct Text {std::wstring wide;std::string utf8;};
const Text& Lookup(Id id,Language language){
    static const auto cache=[](){
        std::array<std::array<Text,2>,TextCount> strings{};
        for(size_t i=0;i<TextCount;i++)for(size_t j=0;j<2;j++){
            auto& value=strings[i][j];
            value.wide=ReadString(OwnModule(),static_cast<Id>(FirstStringId+i),j?Language::Korean:Language::English);
            if(j && value.wide.empty())value.wide=strings[i][0].wide;
            int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.wide.data(),int(value.wide.size()),nullptr,0,nullptr,nullptr);
            if(count>0){value.utf8.resize(size_t(count));WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.wide.data(),int(value.wide.size()),value.utf8.data(),count,nullptr,nullptr);}
        }
        return strings;
    }();
    UINT number=static_cast<UINT>(id);
    static const Text empty;
    if(number<FirstStringId || number-FirstStringId>=TextCount)return empty;
    return cache[number-FirstStringId][language==Language::Korean?1:0];
}
}
const wchar_t* W(Id id,Language language){return Lookup(id,language).wide.c_str();}
const char* N(Id id,Language language){return Lookup(id,language).utf8.c_str();}
}
