#pragma once

#include <wx/dialog.h>

class IconButton;
class wxStaticText;
class wxTextCtrl;

// 统一风格的单行名称输入框，用于新建 / 重命名分组。
// 对话框里的确认、取消仍然是图标按钮。
class NameDialog : public wxDialog {
public:
    NameDialog(wxWindow* parent, const wxString& title, const wxString& label,
               const wxString& value = wxString());

    wxString GetValue() const;

private:
    void Confirm();
    void UpdateButtons();

    wxTextCtrl* input_ = nullptr;
    wxStaticText* error_ = nullptr;
    IconButton* ok_ = nullptr;
};
