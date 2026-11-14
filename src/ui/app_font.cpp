#include "ui/app_font.h"

#include <wx/settings.h>

#include <map>
#include <tuple>

int AppFont::basePointSize_ = 0;

namespace {

std::map<std::tuple<int, bool, bool, bool>, wxFont> g_fontCache;

}  // namespace

void AppFont::EnsureDefaults() {
    if (basePointSize_ > 0) return;
    wxFont systemFont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    basePointSize_ = systemFont.GetPointSize();
    if (basePointSize_ <= 0) basePointSize_ = 9;
}

int AppFont::BasePointSize() {
    EnsureDefaults();
    return basePointSize_;
}

wxFont AppFont::Make(int pointSize, bool bold, bool italic, bool underline) {
    EnsureDefaults();
    if (pointSize <= 0) pointSize = basePointSize_;

    const auto key = std::make_tuple(pointSize, bold, italic, underline);
    const auto found = g_fontCache.find(key);
    if (found != g_fontCache.end()) return found->second;

    wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    if (!font.IsOk()) {
        font = wxFont(wxFontInfo(pointSize).Family(wxFONTFAMILY_DEFAULT));
    }
    font.SetPointSize(pointSize);
    font.SetWeight(bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
    font.SetStyle(italic ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL);
    font.SetUnderlined(underline);
    g_fontCache.emplace(key, font);
    return font;
}

wxFont AppFont::Content(int pointSize) {
    EnsureDefaults();
    if (pointSize <= 0) pointSize = basePointSize_ + 2;
    return Make(pointSize);
}
