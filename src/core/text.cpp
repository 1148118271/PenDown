#include "core/text.h"

std::wstring ToW(const wxString& text) {
    return std::wstring(text.wc_str(), text.length());
}

wxString FromW(const std::wstring& text) {
    return wxString(text.c_str(), text.size());
}
