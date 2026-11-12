#pragma once

#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/string.h>

#include <map>
#include <string>

// 图标来自 lucide-static（ISC），SVG 在运行时按颜色和尺寸生成位图
enum class AppIcon {
    Trash,
    Note,
    Save,
    Close,
    Bold,
    Italic,
    Folder,
    FolderPlus,
    ChevronRight,
    ChevronDown,
    Strikethrough,
    Check,
    NotePlus,
    Eye,
    EyeOff,
    Palette,
    Markdown,
    Minimize,
    Maximize,
    Settings,
    CloudUpload,
    CloudDownload,
};

class IconLibrary {
public:
    static bool Load(const wxString& iconDir);
    static wxBitmap Get(AppIcon icon, int size, const wxColour& colour);

private:
    static std::map<AppIcon, std::string> icons_;
    static std::map<std::string, wxBitmap> cache_;
};
