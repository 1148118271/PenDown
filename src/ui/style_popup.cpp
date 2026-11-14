#include "ui/style_popup.h"

#include "ui/app_font.h"
#include "ui/icon_button.h"
#include "ui/icon_library.h"
#include "ui/theme.h"

#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <utility>

StylePopup::StylePopup(wxWindow* parent, ApplyHandler apply)
    : wxMiniFrame(parent, wxID_ANY, L"样式", wxDefaultPosition, wxDefaultSize,
                  wxCAPTION | wxCLOSE_BOX | wxFRAME_FLOAT_ON_PARENT),
      apply_(std::move(apply)) {
    SetBackgroundColour(Theme::Background());
    BuildUi();
    // 窄长条：两列图标，4 行排满。
    SetSize(FromDIP(wxSize(180, 250)));
    SetMinSize(FromDIP(wxSize(160, 220)));
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

    AddCommandButton(grid, AppIcon::Bold, FormatCommand::Bold, L"加粗 (Ctrl+B)");
    AddCommandButton(grid, AppIcon::Italic, FormatCommand::Italic,
                     L"斜体 (Ctrl+I)");
    AddCommandButton(grid, AppIcon::Underline, FormatCommand::Underline,
                     L"下划线 (Ctrl+U)");
    AddCommandButton(grid, AppIcon::Strikethrough, FormatCommand::Strike,
                     L"删除线");
    AddCommandButton(grid, AppIcon::Plus, FormatCommand::FontLarger,
                     L"字号加");
    AddCommandButton(grid, AppIcon::Minimize, FormatCommand::FontSmaller,
                     L"字号减");
    AddCommandButton(grid, AppIcon::Link, FormatCommand::Hyperlink,
                     L"超链接：给选中文字设置网址");
    AddCommandButton(grid, AppIcon::Font, FormatCommand::ChooseFont,
                     L"字体字号选择");

    content->Add(grid, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(8));

    auto* hint = new wxStaticText(scroller_, wxID_ANY, L"悬停图标可查看说明");
    hint->SetForegroundColour(Theme::TextFaint());
    hint->SetFont(AppFont::Make(std::max(8, AppFont::BasePointSize() - 2)));
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
