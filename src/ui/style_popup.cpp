#include "ui/style_popup.h"

#include "ui/app_font.h"
#include "ui/icon_button.h"
#include "ui/icon_library.h"
#include "ui/theme.h"
#include "ui/ui_text.h"

#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <utility>

StylePopup::StylePopup(wxWindow* parent, ApplyHandler apply)
    : wxMiniFrame(parent, wxID_ANY, UiText("style.styles"), wxDefaultPosition,
                  wxDefaultSize,
                  wxCAPTION | wxCLOSE_BOX | wxFRAME_FLOAT_ON_PARENT),
      apply_(std::move(apply)) {
    SetBackgroundColour(Theme::Background());
    BuildUi();
    // 窄长条：两列图标，4 行排满；英文提示需要略宽的空间。
    SetSize(FromDIP(wxSize(200, 250)));
    SetMinSize(FromDIP(wxSize(180, 220)));
    Layout();
    Bind(wxEVT_CLOSE_WINDOW, &StylePopup::OnClose, this);
}

void StylePopup::BuildUi() {
    auto* root = new wxBoxSizer(wxVERTICAL);
    scroller_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                     wxDefaultSize, wxVSCROLL);
    scroller_->SetBackgroundColour(Theme::Background());
    scroller_->SetScrollRate(0, FromDIP(8));

    auto* content = new wxBoxSizer(wxVERTICAL);
    content->AddSpacer(FromDIP(4));

    const int gap = FromDIP(5);
    auto* grid = new wxFlexGridSizer(0, 2, gap, gap);

    AddCommandButton(grid, AppIcon::Bold, FormatCommand::Bold,
                     UiText("style.bold_ctrl_b"));
    AddCommandButton(grid, AppIcon::Italic, FormatCommand::Italic,
                     UiText("style.italic_ctrl_i"));
    AddCommandButton(grid, AppIcon::Underline, FormatCommand::Underline,
                     UiText("style.underline_ctrl_u"));
    AddCommandButton(grid, AppIcon::Strikethrough, FormatCommand::Strike,
                     UiText("style.strikethrough"));
    AddCommandButton(grid, AppIcon::Plus, FormatCommand::FontLarger,
                     UiText("style.increase_font_size"));
    AddCommandButton(grid, AppIcon::Minimize, FormatCommand::FontSmaller,
                     UiText("style.decrease_font_size"));
    AddCommandButton(grid, AppIcon::Link, FormatCommand::Hyperlink,
                     UiText("style.link_set_a_url_for_the_selected_text"));
    AddCommandButton(grid, AppIcon::Font, FormatCommand::ChooseFont,
                     UiText("style.choose_font_and_size"));

    content->Add(grid, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(8));

    auto* hint = new wxStaticText(
        scroller_, wxID_ANY, UiText("style.hover_over_an_icon_to_see_its_description"));
    hint->SetForegroundColour(Theme::TextFaint());
    hint->SetFont(AppFont::Make(std::max(8, AppFont::BasePointSize() - 2)));
    hint->Wrap(FromDIP(160));
    content->Add(hint, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM,
                 FromDIP(10));

    scroller_->SetSizer(content);
    root->Add(scroller_, 1, wxEXPAND);
    SetSizer(root);
    scroller_->FitInside();
}

void StylePopup::AddCommandButton(wxSizer* sizer, AppIcon icon,
                                  FormatCommand command, const wxString& tip) {
    auto* button = new IconButton(scroller_, wxID_ANY, icon, tip,
                                  IconButton::Kind::Normal,
                                  FromDIP(wxSize(46, 36)), 17);
    button->SetEventTarget(button);
    button->Bind(wxEVT_BUTTON, [this, command](wxCommandEvent&) {
        if (apply_) apply_(command);
    });
    sizer->Add(button, 0, wxALIGN_CENTER);
}

void StylePopup::OnClose(wxCloseEvent& event) {
    event.Veto();
    Hide();
}
