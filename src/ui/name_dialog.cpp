#include "ui/name_dialog.h"

#include "ui/app_font.h"
#include "ui/icon_button.h"
#include "ui/theme.h"
#include "ui/ui_text.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

NameDialog::NameDialog(wxWindow* parent, const wxString& title, const wxString& label,
                       const wxString& value)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE) {
    SetBackgroundColour(Theme::Background());

    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* labelText = new wxStaticText(this, wxID_ANY, label);
    labelText->SetForegroundColour(Theme::TextSubtle());
    root->Add(labelText, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    input_ = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxDefaultSize,
                            wxTE_PROCESS_ENTER | wxTE_CENTRE);
    input_->SetHint(UiText("name.enter_a_name"));
    root->Add(input_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    error_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    error_->SetForegroundColour(Theme::Danger());
    error_->Hide();
    root->Add(error_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer(1);
    auto* cancel = new IconButton(this, wxID_CANCEL, AppIcon::Close,
                                  UiText("dialogs.cancel"));
    ok_ = new IconButton(this, wxID_OK, AppIcon::Check, UiText("dialogs.ok"),
                         IconButton::Kind::Accent);
    buttons->Add(cancel, 0, wxRIGHT, FromDIP(6));
    buttons->Add(ok_, 0);
    root->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(20));

    SetSizer(root);
    SetSize(FromDIP(wxSize(460, 205)));
    SetMinSize(FromDIP(wxSize(400, 190)));
    CentreOnParent();

    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Confirm(); }, wxID_OK);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); }, wxID_CANCEL);
    Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { Confirm(); });
    input_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        error_->Hide();
        UpdateButtons();
        Layout();
        event.Skip();
    });

    Layout();
    UpdateButtons();
    input_->SetFocus();
    input_->SelectAll();
}

wxString NameDialog::GetValue() const {
    if (!input_) return wxString();
    wxString value = input_->GetValue();
    value.Trim(true).Trim(false);
    return value;
}

void NameDialog::UpdateButtons() {
    if (ok_) ok_->Enable(!GetValue().empty());
}

void NameDialog::Confirm() {
    if (GetValue().empty()) {
        error_->SetLabel(UiText("name.the_name_cannot_be_empty"));
        error_->Show();
        Layout();
        input_->SetFocus();
        return;
    }
    EndModal(wxID_OK);
}
