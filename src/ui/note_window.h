#pragma once

#include <wx/frame.h>
#include <wx/timer.h>

#include <functional>
#include <memory>
#include <string>

#include "core/note_store.h"

class IconButton;
class wxRichTextCtrl;
class wxSimplebook;
class wxStaticText;
class wxTextCtrl;

// 便签编辑窗口：独立、可自由拖动的顶层窗口。
// 普通便签使用富文本编辑；Markdown 便签使用单窗原始编辑 + 眼睛开关的预览模式。
class NoteWindow : public wxFrame {
public:
    using SavedHandler =
        std::function<void(const std::wstring& oldPath, const std::wstring& notePath,
                           const std::wstring& groupPath, NoteFormat format)>;
    using DeletedHandler =
        std::function<void(const std::wstring& notePath, const std::wstring& groupPath)>;
    using ClosedHandler = std::function<void(NoteWindow* window)>;

    NoteWindow(wxWindow* parent, std::shared_ptr<NoteStore> store,
               const std::wstring& notePath, const std::wstring& groupPath,
               NoteFormat format, SavedHandler onSaved, DeletedHandler onDeleted,
               ClosedHandler onClosed);

    const std::wstring& NotePath() const { return notePath_; }
    const std::wstring& GroupPath() const { return groupPath_; }
    void UpdateLocation(const std::wstring& notePath, const std::wstring& groupPath);

#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

private:
    void BuildUi();
    void BindEvents();
    void LoadNote();
    bool SaveNote();
    void DeleteNote();
    void TogglePreview();
    void ApplyBold();
    void ApplyItalic();
    void ApplyStrike();
    void ApplyColour();
    void UpdateStatus();
    void UpdateMarkdownScrollbar();
    void UpdateFrameTitle();
    void OnClose(wxCloseEvent& event);
    std::wstring DeriveTitle() const;
    std::wstring PlainText() const;
    void MarkDirty();

    std::shared_ptr<NoteStore> store_;
    std::wstring notePath_;
    std::wstring groupPath_;
    std::wstring title_;
    NoteFormat format_ = NoteFormat::Markdown;
    SavedHandler onSaved_;
    DeletedHandler onDeleted_;
    ClosedHandler onClosed_;

    wxTextCtrl* markdown_ = nullptr;
    wxRichTextCtrl* rich_ = nullptr;
    wxRichTextCtrl* preview_ = nullptr;
    wxSimplebook* bodyBook_ = nullptr;
    IconButton* eyeButton_ = nullptr;
    wxStaticText* status_ = nullptr;

    bool previewMode_ = false;
    bool loading_ = false;
    bool dirty_ = false;
    bool closedNotified_ = false;
    bool markdownScrollbarShown_ = false;
    wxTimer statusTimer_;
};
