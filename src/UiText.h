#pragma once
#include <windows.h>
#include "resource.h"
#include <string>
namespace Brightness::Ui {
enum class Language { English, Korean };
constexpr Language ForLanguageId(LANGID id){return PRIMARYLANGID(id)==LANG_KOREAN?Language::Korean:Language::English;}
inline Language Current(){static const Language language=ForLanguageId(GetUserDefaultUILanguage());return language;}
constexpr LANGID LanguageId(Language language){return language==Language::Korean?MAKELANGID(LANG_KOREAN,SUBLANG_KOREAN):MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US);}
inline LANGID MessageLanguage(){return LanguageId(Current());}
enum class Id : UINT {
    AppTitle=IDS_UI_APP_TITLE,
    MainTitle=IDS_UI_MAIN_TITLE,
    Refresh=IDS_UI_REFRESH,
    Connect=IDS_UI_CONNECT,
    Disconnect=IDS_UI_DISCONNECT,
    Reset=IDS_UI_RESET,
    Licenses=IDS_UI_LICENSES,
    LicenseTitle=IDS_UI_LICENSE_TITLE,
    Instructions=IDS_UI_INSTRUCTIONS,
    SelectGame=IDS_UI_SELECT_GAME,
    GameDetected=IDS_UI_GAME_DETECTED,
    ProcessQueryFailed=IDS_UI_PROCESS_QUERY_FAILED,
    ModuleAccessDenied=IDS_UI_MODULE_ACCESS_DENIED,
    ModuleQueryError=IDS_UI_MODULE_QUERY_ERROR,
    WaitingDirectX=IDS_UI_WAITING_DIRECT_X,
    Disconnected=IDS_UI_DISCONNECTED,
    NoGame=IDS_UI_NO_GAME,
    NoModulePermission=IDS_UI_NO_MODULE_PERMISSION,
    ModuleQueryFailed=IDS_UI_MODULE_QUERY_FAILED,
    RefreshAndRetry=IDS_UI_REFRESH_AND_RETRY,
    WaitDirectX=IDS_UI_WAIT_DIRECT_X,
    EmbeddedModuleFailed=IDS_UI_EMBEDDED_MODULE_FAILED,
    ProcessAccessFailed=IDS_UI_PROCESS_ACCESS_FAILED,
    SamePermission=IDS_UI_SAME_PERMISSION,
    ChannelFailed=IDS_UI_CHANNEL_FAILED,
    ChannelVersionMismatch=IDS_UI_CHANNEL_VERSION_MISMATCH,
    AlreadyConnected=IDS_UI_ALREADY_CONNECTED,
    ModuleVersionMismatch=IDS_UI_MODULE_VERSION_MISMATCH,
    ConnectionFailed=IDS_UI_CONNECTION_FAILED,
    WaitingFrames=IDS_UI_WAITING_FRAMES,
    RenderingError=IDS_UI_RENDERING_ERROR,
    ActiveFrames=IDS_UI_ACTIVE_FRAMES,
    WaitingRenderer=IDS_UI_WAITING_RENDERER,
    AlreadyRunning=IDS_UI_ALREADY_RUNNING,
    UnknownError=IDS_UI_UNKNOWN_ERROR,
    LicenseReadFailed=IDS_UI_LICENSE_READ_FAILED,
    Gamma=IDS_UI_GAMMA,
    Exposure=IDS_UI_EXPOSURE,
    Contrast=IDS_UI_CONTRAST,
    Saturation=IDS_UI_SATURATION,
    Shadows=IDS_UI_SHADOWS,
    ShadowHelp=IDS_UI_SHADOW_HELP,
    SettingsLoadFailed=IDS_UI_SETTINGS_LOAD_FAILED,
    SettingsSaveFailed=IDS_UI_SETTINGS_SAVE_FAILED,
    ClientSettings=IDS_UI_CLIENT_SETTINGS,
    Help=IDS_UI_HELP,
    Count
};
constexpr UINT FirstStringId=IDS_UI_APP_TITLE;
constexpr size_t TextCount=static_cast<UINT>(Id::Count)-FirstStringId;
std::wstring ReadString(HMODULE module,Id id,Language language);
const wchar_t* W(Id id,Language language=Current());
const char* N(Id id,Language language=Current());
}
