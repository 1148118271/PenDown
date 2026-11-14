#pragma once

#include <wx/colour.h>
#include <wx/frame.h>
#include <wx/timer.h>

#include <functional>
#include <memory>
#include <string>

#include "core/note_store.h"
#include "ui/format_command.h"

class ColorPopup;
class FindReplaceDialog;
class IconButton;
class NoteEditor;
class StylePopup;
class wxStaticText;

enum class ColorTarget;

// 便签编辑窗口：TXT 正文 + JSON 样式，使用 wxStyledTextCtrl 直接编辑富文本。
class NoteWindow : public wxFrame {
public:
    using SavedHandler = std::function<void(const std::wstring& oldPath,
                                            const std::wstring& notePath,
                                            const std::wstring& groupPath)>;
    using DeletedHandler =
        std::function<void(const std::wstring& notePath, const std::wstring& groupPath)>;
    using ClosedHandler = std::function<void(NoteWindow* window)>;

    NoteWindow(wxWindow* parent, std::shared_ptr<NoteStore> store,
               const std::wstring& notePath, const std::wstring& groupPath,
               SavedHandler onSaved, DeletedHandler onDeleted, ClosedHandler onClosed);
    ~NoteWindow() override;

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
    void SaveAutosave();
    void RemoveAutosave();
    void DeleteNote();
    void ToggleColorPopup();
    void ToggleStylePopup();
    void ApplyColour(const wxColour& colour, bool clear, ColorTarget target);
    void ApplyStyle(FormatCommand command);
    void ClearFormatting();
    void ToggleFormatPainter();
    void ShowFindDialog();
    void ShowHint(const wxString& text);
    void RestorePopupSelection();
    void UpdateStatus();
    void UpdateFrameTitle();
    void UpdateFormatPainterState();
    void OnClose(wxCloseEvent& event);
    std::wstring DeriveTitle() const;
    void MarkDirty();
    void ScheduleAutosave();

    std::shared_ptr<NoteStore> store_;
    std::wstring notePath_;
    std::wstring groupPath_;
    std::wstring title_;
    SavedHandler onSaved_;
    DeletedHandler onDeleted_;
    ClosedHandler onClosed_;

    NoteEditor* editor_ = nullptr;
    IconButton* colorButton_ = nullptr;
    IconButton* styleButton_ = nullptr;
    IconButton* clearFormatButton_ = nullptr;
    IconButton* formatPainterButton_ = nullptr;
    wxStaticText* status_ = nullptr;
    ColorPopup* colorPopup_ = nullptr;
    StylePopup* stylePopup_ = nullptr;
    FindReplaceDialog* findDialog_ = nullptr;
    wxColour lastColour_;
    long popupSelectionStart_ = -1;
    long popupSelectionEnd_ = -1;
    wxString autosaveLabel_;
    wxString hintText_;

    bool loading_ = false;
    bool dirty_ = false;
    bool loadFailed_ = false;
    bool closedNotified_ = false;
    bool autosaving_ = false;
    wxTimer statusTimer_;
    wxTimer autosaveTimer_;
    wxTimer hintTimer_;
};
