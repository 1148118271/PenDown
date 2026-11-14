#include "ui/find_replace_dialog.h"

#include "ui/app_font.h"
#include "ui/note_editor.h"
#include "ui/theme.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

FindReplaceDialog::FindReplaceDialog(wxWindow* parent, NoteEditor* editor)
    : wxDialog(parent, wxID_ANY, L"查找 / 替换", wxDefaultPosition,
               wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      editor_(editor) {
    SetBackgroundColour(Theme::Background());

    auto* root = new wxBoxSizer(wxVERTICAL);
    const int margin = FromDIP(14);

    auto* grid = new wxFlexGridSizer(2, 2, FromDIP(6), FromDIP(8));
    grid->AddGrowableCol(1, 1);

    auto* findLabel = new wxStaticText(this, wxID_ANY, L"查找");
    findLabel->SetForegroundColour(Theme::TextMuted());
    grid->Add(findLabel, 0, wxALIGN_CENTER_VERTICAL);
    find_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString);
    find_->SetFont(AppFont::Content());
    grid->Add(find_, 1, wxEXPAND);

    auto* replaceLabel = new wxStaticText(this, wxID_ANY, L"替换为");
    replaceLabel->SetForegroundColour(Theme::TextMuted());
    grid->Add(replaceLabel, 0, wxALIGN_CENTER_VERTICAL);
    replace_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString);
    replace_->SetFont(AppFont::Content());
    grid->Add(replace_, 1, wxEXPAND);
    root->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);

    auto* options = new wxBoxSizer(wxHORIZONTAL);
    matchCase_ = new wxCheckBox(this, wxID_ANY, L"区分大小写");
    wholeWord_ = new wxCheckBox(this, wxID_ANY, L"全字匹配");
    options->Add(matchCase_, 0, wxRIGHT, FromDIP(12));
    options->Add(wholeWord_, 0);
    root->Add(options, 0, wxLEFT | wxRIGHT | wxTOP, margin);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* findButton = new wxButton(this, wxID_ANY, L"查找下一个");
    auto* replaceButton = new wxButton(this, wxID_ANY, L"替换");
    auto* replaceAllButton = new wxButton(this, wxID_ANY, L"全部替换");
    auto* closeButton = new wxButton(this, wxID_CANCEL, L"关闭");
    buttons->Add(findButton, 0, wxRIGHT, FromDIP(6));
    buttons->Add(replaceButton, 0, wxRIGHT, FromDIP(6));
    buttons->Add(replaceAllButton, 0, wxRIGHT, FromDIP(6));
    buttons->AddStretchSpacer(1);
    buttons->Add(closeButton, 0);
    root->Add(buttons, 0, wxEXPAND | wxALL, margin);

    SetSizer(root);
    SetSize(FromDIP(wxSize(480, 220)));
    SetMinSize(FromDIP(wxSize(420, 200)));
    CentreOnParent();

    findButton->Bind(wxEVT_BUTTON, &FindReplaceDialog::OnFind, this);
    replaceButton->Bind(wxEVT_BUTTON, &FindReplaceDialog::OnReplace, this);
    replaceAllButton->Bind(wxEVT_BUTTON, &FindReplaceDialog::OnReplaceAll, this);
    closeButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Hide(); });
    Bind(wxEVT_CLOSE_WINDOW, &FindReplaceDialog::OnClose, this);
}

void FindReplaceDialog::FocusFind() {
    Show();
    Raise();
    find_->SetFocus();
    find_->SelectAll();
}

void FindReplaceDialog::OnFind(wxCommandEvent&) {
    if (!editor_) return;
    if (!editor_->FindNext(find_->GetValue(), matchCase_->GetValue(),
                           wholeWord_->GetValue())) {
        wxBell();
    }
}

void FindReplaceDialog::OnReplace(wxCommandEvent&) {
    if (!editor_) return;
    if (editor_->GetSelectionStart() == editor_->GetSelectionEnd()) {
        editor_->FindNext(find_->GetValue(), matchCase_->GetValue(),
                          wholeWord_->GetValue());
        return;
    }
    editor_->ReplaceCurrent(replace_->GetValue());
    editor_->FindNext(find_->GetValue(), matchCase_->GetValue(),
                      wholeWord_->GetValue());
}

void FindReplaceDialog::OnReplaceAll(wxCommandEvent&) {
    if (!editor_) return;
    const int count = editor_->ReplaceAll(
        find_->GetValue(), replace_->GetValue(), matchCase_->GetValue(),
        wholeWord_->GetValue());
    if (count <= 0) {
        wxBell();
    } else {
        wxMessageBox(wxString::Format(L"已替换 %d 处。", count), L"替换",
                     wxOK | wxICON_INFORMATION, this);
    }
}

void FindReplaceDialog::OnClose(wxCloseEvent& event) {
    event.Veto();
    Hide();
}
