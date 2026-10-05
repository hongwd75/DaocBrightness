#pragma once
#include "Shared.h"
#include <string>
namespace Brightness {
bool DefaultSettingsPath(std::wstring& path,DWORD& error);
bool LoadSettings(const std::wstring& path,Settings& value,bool& found,DWORD& error);
bool SaveSettings(const std::wstring& path,Settings value,DWORD& error);
}
