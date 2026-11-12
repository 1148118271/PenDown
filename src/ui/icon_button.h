#pragma once

#include <wx/window.h>

#include "ui/icon_library.h"

// 自绘的扁平图标按钮：圆角、悬停、按下，工具提示显示文字说明
class IconButton : public wxWindow {
public:
    enum class Kind {
        Normal,
        Accent,
        Danger,
    };

    IconButton(wxWindow* parent, wxWindowID id, AppIcon icon, const wxString& tooltip,
               Kind kind = Kind::Normal, const wxSize& size = wxDefaultSize,
               int iconSize = 18);

    void SetIcon(AppIcon icon);
    void SetEventTarget(wxEvtHandler* target) { eventTarget_ = target; }
    void SetActive(bool active);

    bool Enable(bool enable = true) override;
    bool AcceptsFocus() const override { return IsEnabled(); }
    bool AcceptsFocusFromKeyboard() const override { return IsEnabled(); }

    wxSize DoGetBestSize() const override;

private:
    void OnPaint(wxPaintEvent& event);
    void SendClick();

    AppIcon icon_;
    Kind kind_ = Kind::Normal;
    wxSize requestedSize_;
    int iconSize_ = 18;
    bool hovered_ = false;
    bool pressed_ = false;
    bool active_ = false;
    wxEvtHandler* eventTarget_ = nullptr;
};
