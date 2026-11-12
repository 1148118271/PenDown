#include "ui/icon_button.h"

#include "ui/theme.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/utils.h>


IconButton::IconButton(wxWindow* parent, wxWindowID id, AppIcon icon,
                       const wxString& tooltip, Kind kind, const wxSize& size, int iconSize)
    : wxWindow(parent, id, wxDefaultPosition, wxDefaultSize,
                wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE),
      icon_(icon),
      kind_(kind),
      iconSize_(iconSize) {
    wxSize target = size == wxDefaultSize ? FromDIP(wxSize(34, 30)) : size;
    requestedSize_ = target;
    SetMinSize(target);
    SetSize(target);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetCursor(wxCursor(wxCURSOR_HAND));
    SetToolTip(tooltip);
    SetName(tooltip);
    Bind(wxEVT_PAINT, &IconButton::OnPaint, this);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) {
        hovered_ = true;
        Refresh();
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
        hovered_ = false;
        if (!pressed_) Refresh();
    });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        if (!IsEnabled()) return;
        pressed_ = true;
        SetFocus();
        if (!HasCapture()) CaptureMouse();
        Refresh();
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) {
        if (!pressed_) return;
        pressed_ = false;
        if (HasCapture()) ReleaseMouse();
        wxRect rect = GetClientRect();
        if (rect.Contains(event.GetPosition())) SendClick();
        Refresh();
    });
    Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        if (!pressed_) return;
        bool inside = GetClientRect().Contains(event.GetPosition());
        if (hovered_ != inside) {
            hovered_ = inside;
            Refresh();
        }
    });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_SPACE || event.GetKeyCode() == WXK_RETURN) {
            SendClick();
            return;
        }
        event.Skip();
    });
}

void IconButton::SetIcon(AppIcon icon) {
    icon_ = icon;
    Refresh();
}

void IconButton::SetActive(bool active) {
    active_ = active;
    Refresh();
}

bool IconButton::Enable(bool enable) {
    bool changed = wxWindow::Enable(enable);
    if (changed) Refresh();
    return changed;
}

wxSize IconButton::DoGetBestSize() const {
    return requestedSize_;
}

void IconButton::SendClick() {
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    event.SetEventObject(this);
    if (eventTarget_) {
        eventTarget_->ProcessEvent(event);
        return;
    }
    wxWindow* target = wxGetTopLevelParent(this);
    if (!target) target = GetParent();
    if (target) target->GetEventHandler()->ProcessEvent(event);
}

void IconButton::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();

    wxRect rect = GetClientRect();
    int radius = FromDIP(6);

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (!gc) return;

    wxColour background = GetParent()->GetBackgroundColour();
    wxColour iconColour = Theme::TextMuted();

    if (!IsEnabled()) {
        iconColour = Theme::Disabled();
    } else if (kind_ == Kind::Accent) {
        iconColour = Theme::Accent();
    } else if (kind_ == Kind::Danger) {
        iconColour = Theme::Danger();
    }

    if (active_) {
        background = Theme::AccentSoft();
        iconColour = Theme::Accent();
    }

    if (IsEnabled() && hovered_) {
        if (kind_ == Kind::Danger) {
            background = Theme::DangerSoft();
        } else if (kind_ == Kind::Accent) {
            background = Theme::AccentHover();
        } else {
            background = Theme::Hover();
        }
    }
    if (IsEnabled() && pressed_) {
        background = kind_ == Kind::Danger ? wxColour(254, 202, 202) : Theme::Pressed();
    }

    if (background != GetParent()->GetBackgroundColour()) {
        gc->SetBrush(wxBrush(background));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->DrawRoundedRectangle(rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2, radius);
    }

    int iconSize = FromDIP(iconSize_);
    wxBitmap bitmap = IconLibrary::Get(icon_, iconSize, iconColour);
    if (bitmap.IsOk()) {
        double x = (rect.width - iconSize) / 2.0;
        double y = (rect.height - iconSize) / 2.0;
        gc->DrawBitmap(bitmap, x, y, iconSize, iconSize);
    }

    delete gc;
}
