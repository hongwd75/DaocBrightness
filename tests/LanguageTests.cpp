#include "UiText.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <filesystem>
using namespace Brightness::Ui;
int failures=0;
void Check(bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)failures++;}
std::string Formats(const char* text){
    std::string signature;
    for(const char* p=text;*p;p++)if(*p=='%'){
        if(p[1]=='%'){p++;continue;}
        const char* start=p;
        for(p++;*p && !strchr("diuoxXfFeEgGaAcspn",*p);p++){}
        if(*p){signature.append(start,size_t(p-start)+1);signature+=';';}else break;
    }
    return signature;
}
BOOL CALLBACK Languages(HMODULE,LPCWSTR,LPCWSTR,WORD language,LONG_PTR result){
    auto& mask=*reinterpret_cast<unsigned*>(result);
    if(language==LanguageId(Language::English))mask|=1;
    if(language==LanguageId(Language::Korean))mask|=2;
    return TRUE;
}
void CheckModule(HMODULE module,const char* name){
    bool complete=module!=nullptr,languages=complete,equal=complete;
    if(module)for(size_t i=0;i<TextCount;i++){
        auto id=static_cast<Id>(FirstStringId+i);unsigned mask=0;
        EnumResourceLanguagesW(module,RT_STRING,MAKEINTRESOURCEW((static_cast<UINT>(id)>>4)+1),Languages,reinterpret_cast<LONG_PTR>(&mask));
        languages=languages && mask==3;
        for(Language language:{Language::English,Language::Korean}){
            auto text=ReadString(module,id,language);
            complete=complete && !text.empty();equal=equal && text==W(id,language);
        }
    }
    printf("Resource module: %s\n",name);
    Check(complete,"STRINGTABLE includes every UI string");
    Check(languages,"STRINGTABLE has English and Korean language entries");
    Check(equal,"EXE and DLL translations match cached UI strings");
}
int main(){
    Check(ForLanguageId(0)==Language::English,"unknown language defaults to English");
    Check(ForLanguageId(MAKELANGID(LANG_KOREAN,SUBLANG_KOREAN))==Language::Korean,"Korean Windows display language");
    Check(ForLanguageId(MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US))==Language::English,"English US display language");
    Check(ForLanguageId(MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_UK))==Language::English,"English UK display language");
    for(WORD primary:{WORD(LANG_JAPANESE),WORD(LANG_CHINESE),WORD(LANG_GERMAN),WORD(LANG_FRENCH)})
        Check(ForLanguageId(MAKELANGID(primary,SUBLANG_DEFAULT))==Language::English,"other Windows languages use English");
    LANGID original=GetThreadUILanguage();SetThreadUILanguage(MAKELANGID(LANG_JAPANESE,SUBLANG_DEFAULT));
    Check(Current()==ForLanguageId(GetUserDefaultUILanguage()),"display language is independent of thread and input language");
    Check(!wcscmp(W(Id::MainTitle,Language::English),L"DAOC screen brightness") && !wcscmp(W(Id::MainTitle,Language::Korean),L"DAOC 화면 밝기 조절"),"explicit STRINGTABLE language ignores thread language");
    SetThreadUILanguage(original);
    bool complete=true,valid=true,formats=true,ascii=true;
    for(size_t i=0;i<TextCount;i++){
        auto id=static_cast<Id>(FirstStringId+i);
        complete=complete && *N(id,Language::English) && *N(id,Language::Korean);
        formats=formats && Formats(N(id,Language::English))==Formats(N(id,Language::Korean));
        const char* english=N(id,Language::English);
        ascii=ascii && std::all_of(english,english+strlen(english),[](char c){return static_cast<unsigned char>(c)<128;});
        for(Language language:{Language::English,Language::Korean}){
            const char* utf8=N(id,language);const wchar_t* wide=W(id,language);
            int count=WideCharToMultiByte(CP_UTF8,0,wide,-1,nullptr,0,nullptr,nullptr);
            std::string roundTrip(size_t(std::max(1,count)),0);
            if(count)WideCharToMultiByte(CP_UTF8,0,wide,-1,roundTrip.data(),count,nullptr,nullptr);
            valid=valid && count>1 && !strcmp(roundTrip.c_str(),utf8);
        }
    }
    Check(complete,"both catalogs cover every UI text");Check(valid,"UTF-8 overlay and UTF-16 native UI text agree");
    Check(formats,"localized status format parameters are identical");Check(ascii,"English fallback requires no Korean glyphs");
    for(Language language:{Language::English,Language::Korean}){
        HWND label=CreateWindowW(L"STATIC",W(Id::MainTitle,language),WS_POPUP,0,0,500,30,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        wchar_t actual[128]{};if(label)GetWindowTextW(label,actual,_countof(actual));
        Check(label && !wcscmp(actual,W(Id::MainTitle,language)),"native controls accept localized text");if(label)DestroyWindow(label);
    }
    CheckModule(GetModuleHandleW(nullptr),"language test host");
    wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);
    auto directory=std::filesystem::path(executable).parent_path();
    for(auto path:{directory.parent_path()/L"dist"/L"DaocBrightness.exe",directory/L"DaocBrightnessHook.dll"}){
        HMODULE module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        CheckModule(module,path.extension()==L".dll"?"rendering DLL":"standalone EXE");
        if(module)FreeLibrary(module);
    }
    printf("Display LANGID: 0x%04X; selected: %s; text count: %zu\n",unsigned(GetUserDefaultUILanguage()),Current()==Language::Korean?"Korean":"English",TextCount);
    printf("Failures: %d\n",failures);return failures?1:0;
}
