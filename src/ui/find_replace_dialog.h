#pragma once

#include <wx/dialog.h>

class NoteEditor;
class wxCheckBox;
class wxTextCtrl;

// 查找/替换窗口。无模式对话框，关闭时只隐藏，便签关闭时随之销毁。
class FindReplaceDialog : public wxDialog {
public:
    FindReplaceDialog(wxWindow* parent, NoteEditor* editor);

    void FocusFind();

private:
    void OnFindPrev(wxCommandEvent& event);
    void OnFindNext(wxCommandEvent& event);
    void OnReplace(wxCommandEvent& event);
    void OnReplaceAll(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);

    NoteEditor* editor_ = nullptr;
    wxTextCtrl* find_ = nullptr;
    wxTextCtrl* replace_ = nullptr;
    wxCheckBox* matchCase_ = nullptr;
    wxCheckBox* wholeWord_ = nullptr;
};
