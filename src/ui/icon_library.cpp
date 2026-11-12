#include "ui/icon_library.h"

#include <wx/bmpbndl.h>
#include <wx/file.h>
#include <wx/filename.h>

std::map<AppIcon, std::string> IconLibrary::icons_;
std::map<std::string, wxBitmap> IconLibrary::cache_;

namespace {

std::string Utf8(const wxString& text) {
    wxScopedCharBuffer buffer = text.ToUTF8();
    return std::string(buffer.data(), buffer.length());
}

struct IconFile {
    AppIcon icon;
    const wchar_t* fileName;
};

const IconFile kIconFiles[] = {
    {AppIcon::Trash, L"trash-2.svg"},
    {AppIcon::Note, L"sticky-note.svg"},
    {AppIcon::Save, L"save.svg"},
    {AppIcon::Close, L"x.svg"},
    {AppIcon::Bold, L"bold.svg"},
    {AppIcon::Italic, L"italic.svg"},
    {AppIcon::Folder, L"folder.svg"},
    {AppIcon::FolderPlus, L"folder-plus.svg"},
    {AppIcon::ChevronRight, L"chevron-right.svg"},
    {AppIcon::ChevronDown, L"chevron-down.svg"},
    {AppIcon::Strikethrough, L"strikethrough.svg"},
    {AppIcon::Check, L"check.svg"},
    {AppIcon::NotePlus, L"sticky-note-plus.svg"},
    {AppIcon::Eye, L"eye.svg"},
    {AppIcon::EyeOff, L"eye-off.svg"},
    {AppIcon::Palette, L"palette.svg"},
    {AppIcon::Markdown, L"markdown.svg"},
    {AppIcon::Minimize, L"minus.svg"},
    {AppIcon::Maximize, L"square.svg"},
    {AppIcon::Settings, L"settings.svg"},
    {AppIcon::CloudUpload, L"cloud-upload.svg"},
    {AppIcon::CloudDownload, L"cloud-download.svg"},
};

}  // namespace

bool IconLibrary::Load(const wxString& iconDir) {
    icons_.clear();
    cache_.clear();

    for (const auto& entry : kIconFiles) {
        wxString path = iconDir + wxFileName::GetPathSeparator() + entry.fileName;
        wxFile file(path);
        if (!file.IsOpened()) continue;
        wxString content;
        if (!file.ReadAll(&content)) continue;
        icons_[entry.icon] = Utf8(content);
    }
    return !icons_.empty();
}

wxBitmap IconLibrary::Get(AppIcon icon, int size, const wxColour& colour) {
    auto source = icons_.find(icon);
    if (source == icons_.end() || size <= 0) return wxBitmap();

    wxString keyText = wxString::Format(L"%d|%d|%06X", (int)icon, size, colour.GetRGB());
    std::string key = Utf8(keyText);
    auto cached = cache_.find(key);
    if (cached != cache_.end()) return cached->second;

    std::string svg = source->second;
    std::string hex = Utf8(wxString::Format(L"#%02X%02X%02X", colour.Red(), colour.Green(),
                                              colour.Blue()));
    size_t pos = 0;
    while ((pos = svg.find("currentColor", pos)) != std::string::npos) {
        svg.replace(pos, 12, hex);
        pos += hex.size();
    }

    wxBitmapBundle bundle = wxBitmapBundle::FromSVG(svg.c_str(), wxSize(size, size));
    wxBitmap bitmap = bundle.GetBitmap(wxSize(size, size));
    if (bitmap.IsOk()) {
        cache_[key] = bitmap;
    }
    return bitmap;
}
