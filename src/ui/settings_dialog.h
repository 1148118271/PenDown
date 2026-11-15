#pragma once

#include <wx/dialog.h>
#include <wx/string.h>
#include <wx/textctrl.h>

#include <functional>

#include "core/git_sync.h"
#include "core/i18n.h"

class wxChoice;
class wxScrolledWindow;
class wxStaticText;

struct HotkeySpec {
    bool enabled = false;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    unsigned virtualKey = 0;
    wxString raw;
};

// 将保存的快捷键字符串解析成 Ctrl/Alt/Shift + 虚拟键。
// 空字符串表示不启用；格式不支持时返回 false。
bool ParseHotkey(const wxString& raw, HotkeySpec& out);

// 点击后按下组合键即可录入快捷键的只读输入框。
class HotkeyCtrl : public wxTextCtrl {
public:
    explicit HotkeyCtrl(wxWindow* parent);

    wxString RawHotkey() const { return raw_; }
    void SetRawHotkey(const wxString& raw);
    // 处理一次按键；供本控件和设置窗口的 CHAR_HOOK 共用。
    void HandleKey(wxKeyEvent& event);

private:
    void UpdateDisplay();

    wxString raw_;
};

// 设置对话框：数据目录、Git 同步与快捷键设置。
class SettingsDialog : public wxDialog {
public:
    SettingsDialog(wxWindow* parent, const wxString& dataDir,
                   const wxString& showMainHotkey, const wxString& newNoteHotkey,
                   const GitSyncConfig& gitConfig, i18n::Language language,
                   std::function<bool()> hasOpenNotes);

    wxString DataDirectory() const;
    wxString ShowMainHotkey() const;
    wxString NewNoteHotkey() const;
    GitSyncConfig GitConfig() const;
    i18n::Language Language() const;
    bool DidPull() const { return pulled_; }

private:
    void OnBrowseDataDirectory();
    void OnBrowseGit();
    void OnCharHook(wxKeyEvent& event);
    void OnSave(wxCommandEvent& event);
    void OnPush();
    void OnPull();
    void RunSync(bool push);
    GitSyncConfig CurrentGitConfig() const;
    bool ValidateGitConfig(const GitSyncConfig& config);
    void ShowError(const wxString& text);
    void UpdateGitStatus();

    wxScrolledWindow* scroller_ = nullptr;
    wxTextCtrl* dataDirField_ = nullptr;
    wxTextCtrl* gitExeField_ = nullptr;
    wxTextCtrl* gitUrl_ = nullptr;
    wxTextCtrl* gitUsername_ = nullptr;
    wxTextCtrl* gitToken_ = nullptr;
    wxTextCtrl* gitBranch_ = nullptr;
    HotkeyCtrl* showMain_ = nullptr;
    HotkeyCtrl* newNote_ = nullptr;
    wxChoice* languageChoice_ = nullptr;
    wxStaticText* gitStatus_ = nullptr;
    wxStaticText* error_ = nullptr;
    std::function<bool()> hasOpenNotes_;
    wxString dataDir_;
    i18n::Language languageSetting_ = i18n::Language::Auto;
    bool pulled_ = false;
};
