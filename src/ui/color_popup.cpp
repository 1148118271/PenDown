#include "ui/color_popup.h"

#include "ui/app_font.h"
#include "ui/theme.h"
#include "ui/ui_text.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace {

struct SwatchEntry {
    wxString tooltip;
    wxColour colour;
    bool clear = false;
};

const std::vector<SwatchEntry>& CommonColours() {
    static const std::vector<SwatchEntry> kColours = {
        {UiText("color.clear_color"), wxColour(), true},
        {UiText("color.default_black"), wxColour(0x2B, 0x27, 0x23)},
        {UiText("color.gray"), wxColour(0x6E, 0x67, 0x5F)},
        {UiText("color.light_gray"), wxColour(0xA6, 0x9E, 0x94)},
        {UiText("color.red"), wxColour(0xE4, 0x56, 0x49)},
        {UiText("color.orange"), wxColour(0xE6, 0x7E, 0x22)},
        {UiText("color.amber"), wxColour(0xD9, 0xA4, 0x00)},
        {UiText("color.yellow"), wxColour(0xCA, 0x8A, 0x04)},
        {UiText("color.green"), wxColour(0x50, 0xA1, 0x4F)},
        {UiText("color.dark_green"), wxColour(0x16, 0x65, 0x34)},
        {UiText("color.cyan"), wxColour(0x0F, 0x9D, 0x8A)},
        {UiText("color.sky_blue"), wxColour(0x0E, 0xA5, 0xE9)},
        {UiText("color.blue"), wxColour(0x25, 0x63, 0xEB)},
        {UiText("color.dark_blue"), wxColour(0x1E, 0x3A, 0x8A)},
        {UiText("color.indigo"), wxColour(0x63, 0x66, 0xF1)},
        {UiText("color.purple"), wxColour(0xA6, 0x26, 0xA4)},
        {UiText("color.pink"), wxColour(0xD9, 0x46, 0x8F)},
        {UiText("color.rose"), wxColour(0xE1, 0x1D, 0x48)},
        {UiText("color.dark_red"), wxColour(0x99, 0x1B, 0x1B)},
        {UiText("color.brown"), wxColour(0xA0, 0x52, 0x2D)},
        {UiText("color.slate_gray"), wxColour(0x47, 0x55, 0x69)},
        {UiText("color.dark_cyan"), wxColour(0x0F, 0x76, 0x6E)},
        {UiText("color.dark_purple"), wxColour(0x6B, 0x21, 0xA8)},
        {UiText("color.warm_gray"), wxColour(0x8A, 0x81, 0x78)},
    };
    return kColours;
}

}  // namespace

// 单个色块。用自绘而不是 wxColourPickerCtrl，避免点击后弹出带确定按钮的系统对话框。
class ColourSwatch : public wxPanel {
public:
    ColourSwatch(wxWindow* parent, const wxColour& colour, bool clear,
                 std::function<void()> onClick)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                  wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE),
          colour_(colour),
          clear_(clear),
          onClick_(std::move(onClick)) {
        SetMinSize(FromDIP(wxSize(28, 28)));
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetCursor(wxCursor(wxCURSOR_HAND));
        Bind(wxEVT_PAINT, &ColourSwatch::OnPaint, this);
        Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
            if (onClick_) onClick_();
        });
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) {
            hovered_ = true;
            Refresh();
        });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
            hovered_ = false;
            Refresh();
        });
    }

private:
    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(Theme::Background()));
        dc.Clear();

        wxRect rect = GetClientRect();
        rect.Deflate(2);
        if (rect.width <= 0 || rect.height <= 0) return;

        wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
        if (!gc) return;
        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

        if (clear_) {
            gc->SetBrush(wxBrush(Theme::Surface()));
            gc->SetPen(wxPen(hovered_ ? Theme::Accent() : Theme::Border(), 1));
            gc->DrawRoundedRectangle(rect.x + 0.5, rect.y + 0.5, rect.width - 1,
                                     rect.height - 1, FromDIP(5));
            gc->SetPen(wxPen(Theme::Danger(), FromDIP(2)));
            gc->StrokeLine(rect.x + FromDIP(7), rect.y + FromDIP(7),
                           rect.GetRight() - FromDIP(7),
                           rect.GetBottom() - FromDIP(7));
        } else {
            gc->SetBrush(wxBrush(colour_));
            gc->SetPen(wxPen(hovered_ ? Theme::Accent() : Theme::Border(), 1));
            gc->DrawRoundedRectangle(rect.x + 0.5, rect.y + 0.5, rect.width - 1,
                                     rect.height - 1, FromDIP(5));
        }
        delete gc;
    }

    wxColour colour_;
    bool clear_ = false;
    bool hovered_ = false;
    std::function<void()> onClick_;
};

ColorPopup::ColorPopup(wxWindow* parent, const wxColour& initial, ApplyHandler apply)
    : wxMiniFrame(parent, wxID_ANY, UiText("color.color"), wxDefaultPosition,
                  wxDefaultSize,
                  wxCAPTION | wxCLOSE_BOX | wxFRAME_FLOAT_ON_PARENT),
      apply_(std::move(apply)),
      colour_(initial.IsOk() ? initial : Theme::Text()),
      applyTimer_(this) {
    SetBackgroundColour(Theme::Background());
    BuildUi();
    UpdateTargetButtons();
    // 窄长条：4 列色块 + 垂直 RGB 滑杆 + 文字/背景切换。
    SetSize(FromDIP(wxSize(210, 540)));
    SetMinSize(FromDIP(wxSize(200, 500)));
    Layout();
    Bind(wxEVT_CLOSE_WINDOW, &ColorPopup::OnClose, this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { ApplyCurrent(); });
    Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
        if (!event.IsShown()) applyTimer_.Stop();
        event.Skip();
    });
    SetColour(colour_);
}

void ColorPopup::BuildUi() {
    auto* root = new wxBoxSizer(wxVERTICAL);
    const int margin = FromDIP(14);

    auto* targetRow = new wxBoxSizer(wxHORIZONTAL);
    textButton_ = new wxToggleButton(this, wxID_ANY, UiText("color.text"));
    backgroundButton_ =
        new wxToggleButton(this, wxID_ANY, UiText("color.background"));
    textButton_->SetFont(AppFont::Make(std::max(8, AppFont::BasePointSize() - 1)));
    backgroundButton_->SetFont(
        AppFont::Make(std::max(8, AppFont::BasePointSize() - 1)));
    targetRow->Add(textButton_, 1, wxEXPAND | wxRIGHT, FromDIP(4));
    targetRow->Add(backgroundButton_, 1, wxEXPAND);
    root->Add(targetRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);

    textButton_->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        SetTarget(ColorTarget::Foreground);
    });
    backgroundButton_->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        SetTarget(ColorTarget::Background);
    });

    auto* commonLabel =
        new wxStaticText(this, wxID_ANY, UiText("color.common_colors"));
    commonLabel->SetForegroundColour(Theme::TextMuted());
    commonLabel->SetFont(AppFont::Make(AppFont::BasePointSize() - 1, true));
    root->Add(commonLabel, 0, wxLEFT | wxRIGHT | wxTOP, margin);

    // 四列色块，保持颜色盘是窄长条，不横向铺开。
    auto* grid = new wxFlexGridSizer(0, 4, FromDIP(6), FromDIP(6));
    for (const SwatchEntry& entry : CommonColours()) {
        auto* swatch =
            new ColourSwatch(this, entry.colour, entry.clear, [this, entry]() {
                SelectSwatch(entry.colour, entry.clear);
            });
        swatch->SetToolTip(entry.tooltip);
        grid->Add(swatch, 0);
    }
    root->Add(grid, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(8));

    auto* customLabel =
        new wxStaticText(this, wxID_ANY, UiText("color.custom_color"));
    customLabel->SetForegroundColour(Theme::TextMuted());
    customLabel->SetFont(AppFont::Make(AppFont::BasePointSize() - 1, true));
    root->Add(customLabel, 0, wxLEFT | wxRIGHT | wxTOP, margin);

    auto* previewRow = new wxBoxSizer(wxHORIZONTAL);
    preview_ = new wxPanel(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(40, 40)),
                           wxBORDER_NONE);
    preview_->SetMinSize(FromDIP(wxSize(40, 40)));
    previewRow->Add(preview_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

    values_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    values_->SetForegroundColour(Theme::TextFaint());
    values_->SetFont(AppFont::Make(std::max(8, AppFont::BasePointSize() - 2)));
    previewRow->Add(values_, 1, wxALIGN_CENTER_VERTICAL);
    root->Add(previewRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);

    auto* sliders = new wxBoxSizer(wxVERTICAL);
    auto addSlider = [&](const wxString& label, wxSlider*& slider, int value) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* text = new wxStaticText(this, wxID_ANY, label);
        text->SetForegroundColour(Theme::TextSubtle());
        text->SetMinSize(FromDIP(wxSize(14, -1)));
        row->Add(text, 0, wxALIGN_CENTER_VERTICAL);
        slider = new wxSlider(this, wxID_ANY, value, 0, 255, wxDefaultPosition,
                              FromDIP(wxSize(120, -1)), wxSL_HORIZONTAL);
        row->Add(slider, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));
        sliders->Add(row, 0, wxEXPAND | wxBOTTOM, FromDIP(2));
        slider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
            if (updating_) return;
            const wxColour next((unsigned char)red_->GetValue(),
                                (unsigned char)green_->GetValue(),
                                (unsigned char)blue_->GetValue());
            colour_ = next;
            UpdatePreview();
            applyTimer_.StartOnce(40);
        });
    };
    addSlider(L"R", red_, colour_.Red());
    addSlider(L"G", green_, colour_.Green());
    addSlider(L"B", blue_, colour_.Blue());
    root->Add(sliders, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
    root->AddSpacer(FromDIP(8));

    SetSizer(root);
}

void ColorPopup::SetColour(const wxColour& colour) {
    colour_ = colour.IsOk() ? colour : Theme::Text();
    updating_ = true;
    red_->SetValue(colour_.Red());
    green_->SetValue(colour_.Green());
    blue_->SetValue(colour_.Blue());
    updating_ = false;
    UpdatePreview();
}

void ColorPopup::SetTarget(ColorTarget target) {
    target_ = target;
    UpdateTargetButtons();
}

void ColorPopup::UpdateTargetButtons() {
    if (textButton_) textButton_->SetValue(target_ == ColorTarget::Foreground);
    if (backgroundButton_) {
        backgroundButton_->SetValue(target_ == ColorTarget::Background);
    }
}

void ColorPopup::SelectSwatch(const wxColour& colour, bool clear) {
    if (clear) {
        SetColour(target_ == ColorTarget::Foreground ? Theme::Text()
                                                     : Theme::Surface());
        if (apply_) apply_(wxColour(), true, target_);
        return;
    }
    SetColour(colour);
    if (apply_) apply_(colour, false, target_);
}

void ColorPopup::UpdatePreview() {
    if (preview_) {
        preview_->SetBackgroundColour(colour_);
        preview_->Refresh();
    }
    if (values_) {
        values_->SetLabel(wxString::Format(L"#%02X%02X%02X", colour_.Red(),
                                           colour_.Green(), colour_.Blue()));
    }
}

void ColorPopup::ApplyCurrent() {
    if (apply_ && colour_.IsOk()) apply_(colour_, false, target_);
}

void ColorPopup::OnClose(wxCloseEvent& event) {
    event.Veto();
    applyTimer_.Stop();
    Hide();
}
