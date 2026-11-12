#pragma once

#include <wx/string.h>

#include <string>

// std::wstring 与 wxString 之间来回转换，统一放在这里，避免各处重复写 wc_str()
std::wstring ToW(const wxString& text);
wxString FromW(const std::wstring& text);
