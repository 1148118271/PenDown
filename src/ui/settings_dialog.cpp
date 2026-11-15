#include "ui/settings_dialog.h"

#include "core/text.h"
#include "ui/app_font.h"
#include "ui/dialogs.h"
#include "ui/git_config.h"
#include "ui/icon_button.h"
#include "ui/icon_library.h"
#include "ui/theme.h"
#include "ui/ui_ids.h"
#include "ui/ui_text.h"

#include <wx/accel.h>
#include <wx/choice.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif

#include <algorithm>
#include <atomic>
#include <exception>
#include <filesystem>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

namespace {

constexpr int kBrowseDataDirId = wxID_HIGHEST + 700;
constexpr int kBrowseGitId = wxID_HIGHEST + 701;
constexpr int kGitPushId = wxID_HIGHEST + 702;
constexpr int kGitPullId = wxID_HIGHEST + 703;

int ToVirtualKey(int keyCode) {
#ifdef __WXMSW__
    if (keyCode >= 'A' && keyCode <= 'Z') return keyCode;
    if (keyCode >= '0' && keyCode <= '9') return keyCode;
    if (keyCode >= WXK_F1 && keyCode <= WXK_F24) {
        return VK_F1 + (keyCode - WXK_F1);
    }
    switch (keyCode) {
        case WXK_SPACE: return VK_SPACE;
        case WXK_RETURN: return VK_RETURN;
        case WXK_TAB: return VK_TAB;
        case WXK_ESCAPE: return VK_ESCAPE;
        case WXK_HOME: return VK_HOME;
        case WXK_END: return VK_END;
        case WXK_PAGEUP: return VK_PRIOR;
        case WXK_PAGEDOWN: return VK_NEXT;
        case WXK_INSERT: return VK_INSERT;
        case WXK_DELETE: return VK_DELETE;
        case WXK_LEFT: return VK_LEFT;
        case WXK_RIGHT: return VK_RIGHT;
        case WXK_UP: return VK_UP;
        case WXK_DOWN: return VK_DOWN;
        default: return 0;
    }
#else
    return keyCode;
#endif
}

wxString MakeRawHotkey(int flags, int keyCode) {
    wxAcceleratorEntry entry(flags, keyCode);
    return entry.IsOk() ? entry.ToRawString() : wxString();
}

int KeyCodeFromEvent(const wxKeyEvent& event) {
#ifdef __WXMSW__
    int vk = (int)event.GetRawKeyCode();
    if (vk == 0) {
        // 个别合成按键没有原始键码，退回 wx 键码
        const int key = event.GetKeyCode();
        if (key >= 1 && key <= 26) {
            vk = 'A' + (key - 1);
        } else if (key >= 'a' && key <= 'z') {
            vk = key - 'a' + 'A';
        } else if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) {
            vk = key;
        } else if (key >= WXK_F1 && key <= WXK_F24) {
            vk = VK_F1 + (key - WXK_F1);
        } else {
            return 0;
        }
    }
    if (vk >= 'A' && vk <= 'Z') return vk;
    if (vk >= '0' && vk <= '9') return vk;
    if (vk >= VK_F1 && vk <= VK_F24) return WXK_F1 + (vk - VK_F1);
    return 0;
#else
    int key = event.GetKeyCode();
    if (key >= 1 && key <= 26) key += 'A' - 1;
    if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') ||
        (key >= WXK_F1 && key <= WXK_F24)) {
        return key;
    }
    return 0;
#endif
}

}  // namespace

bool ParseHotkey(const wxString& rawValue, HotkeySpec& out) {
    out = HotkeySpec();
    wxString raw = rawValue;
    raw.Trim(true).Trim(false);
    out.raw = raw;
    if (raw.empty()) return true;  // 留空表示不启用

    wxAcceleratorEntry entry;
    if (!entry.FromString(raw) || !entry.IsOk()) return false;

    const int flags = entry.GetFlags();
    out.ctrl = (flags & wxACCEL_CTRL) != 0;
    out.alt = (flags & wxACCEL_ALT) != 0;
    out.shift = (flags & wxACCEL_SHIFT) != 0;
    int keyCode = entry.GetKeyCode();
    if (keyCode >= 'a' && keyCode <= 'z') keyCode -= 'a' - 'A';
    const bool functionKey = keyCode >= WXK_F1 && keyCode <= WXK_F24;
    if (!out.ctrl && !out.alt && !out.shift && !functionKey) return false;
    out.virtualKey = (unsigned)ToVirtualKey(keyCode);
    if (out.virtualKey == 0) return false;
    out.enabled = true;
    return true;
}

HotkeyCtrl::HotkeyCtrl(wxWindow* parent)
    : wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                 wxTE_READONLY | wxTE_CENTRE) {
    Bind(wxEVT_KEY_DOWN, &HotkeyCtrl::HandleKey, this);
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        SetFocus();
        event.Skip();
    });
    UpdateDisplay();
}

void HotkeyCtrl::SetRawHotkey(const wxString& raw) {
    raw_ = raw;
    raw_.Trim(true).Trim(false);
    UpdateDisplay();
}

void HotkeyCtrl::UpdateDisplay() {
    if (raw_.empty()) {
        ChangeValue(UiText("settings.not_set"));
        return;
    }
    wxAcceleratorEntry entry;
    if (entry.FromString(raw_) && entry.IsOk()) {
        wxString keyName = entry.ToString().AfterLast('+');
        if (keyName.empty()) keyName = entry.ToString();
        wxString text;
        const int flags = entry.GetFlags();
        if (flags & wxACCEL_CTRL) text += L"Ctrl+";
        if (flags & wxACCEL_ALT) text += L"Alt+";
        if (flags & wxACCEL_SHIFT) text += L"Shift+";
        ChangeValue(text + keyName);
    } else {
        ChangeValue(raw_);
    }
}

void HotkeyCtrl::HandleKey(wxKeyEvent& event) {
    const int key = event.GetKeyCode();
    if (key == WXK_CONTROL || key == WXK_SHIFT || key == WXK_ALT ||
        key == WXK_WINDOWS_LEFT || key == WXK_WINDOWS_RIGHT) {
        return;
    }

    if (key == WXK_BACK || key == WXK_DELETE) {
        SetRawHotkey(wxString());
        return;
    }
    if (key == WXK_TAB || key == WXK_ESCAPE || key == WXK_RETURN) {
        event.Skip();
        return;
    }

    const bool ctrl = event.ControlDown();
    const bool alt = event.AltDown();
    const bool shift = event.ShiftDown();
    const bool functionKey = key >= WXK_F1 && key <= WXK_F24;

    // 字母和数字至少需要一个修饰键；F1-F24 可以单独使用。
    if (!ctrl && !alt && !shift && !functionKey) return;

    const int keyCode = KeyCodeFromEvent(event);
    if (keyCode == 0) return;

    int flags = 0;
    if (ctrl) flags |= wxACCEL_CTRL;
    if (alt) flags |= wxACCEL_ALT;
    if (shift) flags |= wxACCEL_SHIFT;

    const wxString raw = MakeRawHotkey(flags, keyCode);
    if (!raw.empty()) SetRawHotkey(raw);
}

SettingsDialog::SettingsDialog(wxWindow* parent, const wxString& dataDir,
                               const wxString& showMainHotkey,
                               const wxString& newNoteHotkey,
                               const GitSyncConfig& gitConfig,
                               i18n::Language language,
                               std::function<bool()> hasOpenNotes)
    : wxDialog(parent, wxID_ANY, UiText("main.settings"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE),
      hasOpenNotes_(std::move(hasOpenNotes)),
      dataDir_(dataDir),
      languageSetting_(language) {
    SetBackgroundColour(Theme::Background());

    auto* root = new wxBoxSizer(wxVERTICAL);
    scroller_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                     wxVSCROLL);
    scroller_->SetBackgroundColour(Theme::Background());
    scroller_->SetScrollRate(0, FromDIP(8));

    auto* content = new wxBoxSizer(wxVERTICAL);

    auto addHeading = [&](const wxString& text) {
        auto* heading = new wxStaticText(scroller_, wxID_ANY, text);
        heading->SetForegroundColour(Theme::Text());
        heading->SetFont(AppFont::Make(AppFont::BasePointSize() + 2, true));
        content->Add(heading, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
    };
    auto addHint = [&](const wxString& text) {
        auto* hint = new wxStaticText(scroller_, wxID_ANY, text);
        hint->SetForegroundColour(Theme::TextMuted());
        // 英文提示通常比中文长，固定换行宽度避免超出窗口。
        hint->Wrap(FromDIP(520));
        content->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
    };

    addHeading(UiText("settings.language_2"));
    addHint(UiText("settings.changing_the_language_takes_effect_after_restart"));

    auto* languageRow = new wxBoxSizer(wxHORIZONTAL);
    auto* languageLabel = new wxStaticText(scroller_, wxID_ANY,
                                           UiText("settings.language"));
    languageLabel->SetForegroundColour(Theme::TextSubtle());
    languageLabel->SetMinSize(FromDIP(wxSize(180, -1)));
    languageRow->Add(languageLabel, 0, wxALIGN_CENTER_VERTICAL);

    languageChoice_ = new wxChoice(scroller_, wxID_ANY);
    languageChoice_->Append(UiText("settings.system_default"));
    languageChoice_->Append(UiText("settings.chinese"));
    languageChoice_->Append(L"English");
    languageChoice_->SetSelection(language == i18n::Language::Chinese
                                      ? 1
                                      : (language == i18n::Language::English ? 2 : 0));
    languageRow->Add(languageChoice_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT,
                     FromDIP(12));
    content->Add(languageRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    addHeading(UiText("settings.data_storage"));

    addHint(UiText("settings.notes_are_stored_in_the_folder_below_click_the_f"));

    auto* dataRow = new wxBoxSizer(wxHORIZONTAL);
    auto* dataLabel = new wxStaticText(scroller_, wxID_ANY,
                                       UiText("settings.data_folder"));
    dataLabel->SetForegroundColour(Theme::TextSubtle());
    dataLabel->SetMinSize(FromDIP(wxSize(180, -1)));
    dataRow->Add(dataLabel, 0, wxALIGN_CENTER_VERTICAL);

    dataDirField_ = new wxTextCtrl(scroller_, wxID_ANY, dataDir, wxDefaultPosition,
                                   wxDefaultSize, wxTE_READONLY | wxTE_CENTRE);
    dataDirField_->SetToolTip(dataDir);
    dataRow->Add(dataDirField_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));

    auto* browse = new IconButton(scroller_, kBrowseDataDirId, AppIcon::Folder,
                                  UiText("settings.choose_data_folder"),
                                  IconButton::Kind::Normal,
                                  FromDIP(wxSize(34, 30)), 14);
    browse->SetEventTarget(this);
    dataRow->Add(browse, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    content->Add(dataRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    addHeading(UiText("settings.git_sync"));

    addHint(UiText("settings.the_entire_data_folder_is_a_single_git_repositor"));

    auto addTextField = [&](const wxString& label, wxTextCtrl*& field,
                            const wxString& value, long style,
                            const wxString& hint) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* labelText = new wxStaticText(scroller_, wxID_ANY, label);
        labelText->SetForegroundColour(Theme::TextSubtle());
        labelText->SetMinSize(FromDIP(wxSize(180, -1)));
        row->Add(labelText, 0, wxALIGN_CENTER_VERTICAL);

        field = new wxTextCtrl(scroller_, wxID_ANY, value, wxDefaultPosition,
                               wxDefaultSize, style | wxTE_CENTRE);
        if (!hint.empty()) field->SetHint(hint);
        row->Add(field, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
        content->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
    };

    addTextField(UiText("settings.repository_url"), gitUrl_,
                 FromW(gitConfig.remoteUrl), 0,
                 L"https://github.com/user/notes.git");

    auto* exeRow = new wxBoxSizer(wxHORIZONTAL);
    auto* exeLabel = new wxStaticText(scroller_, wxID_ANY,
                                      UiText("settings.git_executable"));
    exeLabel->SetForegroundColour(Theme::TextSubtle());
    exeLabel->SetMinSize(FromDIP(wxSize(180, -1)));
    exeRow->Add(exeLabel, 0, wxALIGN_CENTER_VERTICAL);

    gitExeField_ = new wxTextCtrl(scroller_, wxID_ANY, FromW(gitConfig.gitExe),
                                  wxDefaultPosition, wxDefaultSize, wxTE_CENTRE);
    gitExeField_->SetHint(
        UiText("settings.leave_empty_to_auto_detect_e_g_d_git_bin_git_exe"));
    gitExeField_->SetToolTip(
        UiText("settings.full_path_to_a_local_git_exe_leave_empty_to_sear"));
    gitExeField_->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        UpdateGitStatus();
        event.Skip();
    });
    exeRow->Add(gitExeField_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));

    auto* browseGit = new IconButton(scroller_, kBrowseGitId, AppIcon::Folder,
                                     UiText("settings.choose_git_exe"),
                                     IconButton::Kind::Normal,
                                     FromDIP(wxSize(34, 30)), 14);
    browseGit->SetEventTarget(this);
    exeRow->Add(browseGit, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    content->Add(exeRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    gitStatus_ = new wxStaticText(scroller_, wxID_ANY, wxEmptyString);
    gitStatus_->SetFont(AppFont::Make(std::max(8, AppFont::BasePointSize() - 2)));
    content->Add(gitStatus_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
    UpdateGitStatus();

    addTextField(UiText("settings.username"), gitUsername_,
                 FromW(gitConfig.username), 0,
                 UiText("settings.optional_for_public_repositories"));
    addTextField(UiText("settings.password_token"), gitToken_,
                 FromW(gitConfig.token), wxTE_PASSWORD,
                 UiText("settings.access_token_encrypted_and_stored_on_this_comput"));
    addTextField(UiText("settings.branch"), gitBranch_,
                 FromW(gitConfig.branch.empty() ? std::wstring(L"main")
                                                : gitConfig.branch),
                 0, L"main");

    auto* syncRow = new wxBoxSizer(wxHORIZONTAL);
    auto* syncLabel = new wxStaticText(scroller_, wxID_ANY,
                                       UiText("settings.sync_now"));
    syncLabel->SetForegroundColour(Theme::TextSubtle());
    syncLabel->SetMinSize(FromDIP(wxSize(180, -1)));
    syncRow->Add(syncLabel, 0, wxALIGN_CENTER_VERTICAL);

    auto* push = new IconButton(
        scroller_, kGitPushId, AppIcon::CloudUpload,
        UiText("settings.push_commit_local_changes_and_force_push_local_r"),
        IconButton::Kind::Normal, FromDIP(wxSize(34, 30)), 15);
    push->SetEventTarget(this);
    syncRow->Add(push, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));

    auto* pushText = new wxStaticText(scroller_, wxID_ANY,
                                      UiText("settings.push"));
    pushText->SetForegroundColour(Theme::Text());
    pushText->SetCursor(wxCursor(wxCURSOR_HAND));
    pushText->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
        wxCommandEvent event(wxEVT_BUTTON, kGitPushId);
        event.SetEventObject(this);
        ProcessEvent(event);
    });
    syncRow->Add(pushText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));

    syncRow->AddSpacer(FromDIP(24));

    auto* pull = new IconButton(
        scroller_, kGitPullId, AppIcon::CloudDownload,
        UiText("settings.pull_overwrite_local_content_with_the_remote_sta"),
        IconButton::Kind::Normal, FromDIP(wxSize(34, 30)), 15);
    pull->SetEventTarget(this);
    syncRow->Add(pull, 0, wxALIGN_CENTER_VERTICAL);

    auto* pullText = new wxStaticText(scroller_, wxID_ANY,
                                      UiText("settings.pull"));
    pullText->SetForegroundColour(Theme::Text());
    pullText->SetCursor(wxCursor(wxCURSOR_HAND));
    pullText->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
        wxCommandEvent event(wxEVT_BUTTON, kGitPullId);
        event.SetEventObject(this);
        ProcessEvent(event);
    });
    syncRow->Add(pullText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
    content->Add(syncRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    addHeading(UiText("settings.keyboard_shortcuts"));

    addHint(UiText("settings.click_a_field_and_press_a_key_combination_ctrl_a"));

    auto addHotkeyRow = [&](const wxString& label, HotkeyCtrl*& field,
                            const wxString& value) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* labelText = new wxStaticText(scroller_, wxID_ANY, label);
        labelText->SetForegroundColour(Theme::TextSubtle());
        labelText->SetMinSize(FromDIP(wxSize(180, -1)));
        row->Add(labelText, 0, wxALIGN_CENTER_VERTICAL);

        field = new HotkeyCtrl(scroller_);
        field->SetRawHotkey(value);
        row->Add(field, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));

        content->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
    };

    addHotkeyRow(UiText("main.show_main_window"), showMain_,
                 showMainHotkey);
    addHotkeyRow(UiText("main.new_note"), newNote_, newNoteHotkey);

    error_ = new wxStaticText(scroller_, wxID_ANY, wxEmptyString);
    error_->SetForegroundColour(Theme::Danger());
    error_->Wrap(FromDIP(520));
    error_->Hide();
    content->Add(error_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    scroller_->SetSizer(content);

    root->Add(scroller_, 1, wxEXPAND);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer(1);
    auto* cancel = new IconButton(this, wxID_CANCEL, AppIcon::Close,
                                  UiText("dialogs.cancel"));
    auto* save = new IconButton(this, wxID_OK, AppIcon::Check,
                                UiText("settings.save"),
                                IconButton::Kind::Accent);
    buttons->Add(cancel, 0, wxRIGHT, FromDIP(6));
    buttons->Add(save, 0);
    root->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(20));

    SetSizer(root);

    const wxRect work = wxGetClientDisplayRect();
    const int width = std::min(FromDIP(640), work.width - FromDIP(32));
    const int height = std::min(FromDIP(720), work.height - FromDIP(32));
    SetSize(wxSize(std::max(width, FromDIP(520)),
                   std::max(height, FromDIP(460))));
    SetMinSize(wxSize(std::min(FromDIP(560), width), std::min(FromDIP(520), height)));
    CentreOnParent();

    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnBrowseDataDirectory(); },
         kBrowseDataDirId);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnBrowseGit(); }, kBrowseGitId);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnPush(); }, kGitPushId);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnPull(); }, kGitPullId);
    Bind(wxEVT_BUTTON, &SettingsDialog::OnSave, this, wxID_OK);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); },
         wxID_CANCEL);
    Bind(wxEVT_CHAR_HOOK, &SettingsDialog::OnCharHook, this);

    Layout();
    scroller_->FitInside();
    scroller_->Scroll(0, 0);
    scroller_->SetFocus();
    CallAfter([this] {
        scroller_->Scroll(0, 0);
        scroller_->SetFocus();
    });
}

wxString SettingsDialog::DataDirectory() const {
    if (!dataDirField_) return wxString();
    wxString value = dataDirField_->GetValue();
    value.Trim(true).Trim(false);
    return value;
}

void SettingsDialog::OnBrowseDataDirectory() {
    wxDirDialog dialog(this, UiText("settings.choose_note_data_folder"),
                       DataDirectory(), wxDD_DEFAULT_STYLE);
    if (dialog.ShowModal() == wxID_OK && dataDirField_) {
        dataDirField_->ChangeValue(dialog.GetPath());
        dataDirField_->SetToolTip(dialog.GetPath());
    }
}

void SettingsDialog::OnCharHook(wxKeyEvent& event) {
    // wxEVT_CHAR_HOOK 在顶层窗口触发，Alt / F 键也能在菜单和控件处理前拿到。
    if (auto* hotkey = wxDynamicCast(wxWindow::FindFocus(), HotkeyCtrl)) {
        hotkey->HandleKey(event);
        return;
    }
    event.Skip();
}

void SettingsDialog::OnBrowseGit() {
    wxString current = gitExeField_ ? gitExeField_->GetValue() : wxString();
    wxFileName currentFile(current);
    wxFileDialog dialog(this, UiText("settings.choose_git_exe"),
                        currentFile.GetPath(), currentFile.GetFullName(),
                        UiText("settings.git_exe_git_exe_executable_files_exe_exe_all_fil"),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK && gitExeField_) {
        gitExeField_->ChangeValue(dialog.GetPath());
        UpdateGitStatus();
    }
}

void SettingsDialog::UpdateGitStatus() {
    if (!gitStatus_) return;

    wxString configured = gitExeField_ ? gitExeField_->GetValue() : wxString();
    configured.Trim(true).Trim(false);
    std::wstring gitExe;
    if (configured.empty()) {
        gitExe = FindGitExecutable();
    } else {
        std::error_code ec;
        const fs::path path(ToW(configured));
        if (fs::is_regular_file(path, ec)) gitExe = path.wstring();
    }

    if (gitExe.empty()) {
        gitStatus_->SetForegroundColour(Theme::Danger());
        gitStatus_->SetLabel(
            configured.empty()
                ? UiText("settings.git_was_not_detected_install_git_for_windows_or")
                : UiText("settings.git_executable_not_found") +
                      configured);
        gitStatus_->SetToolTip(wxString());
    } else {
        gitStatus_->SetForegroundColour(Theme::TextFaint());
        gitStatus_->SetLabel(UiText("settings.git") + FromW(gitExe));
        gitStatus_->SetToolTip(FromW(gitExe));
    }
    gitStatus_->Wrap(FromDIP(520));
    Layout();
    scroller_->Layout();
    scroller_->FitInside();
}

wxString SettingsDialog::ShowMainHotkey() const {
    return showMain_ ? showMain_->RawHotkey() : wxString();
}

wxString SettingsDialog::NewNoteHotkey() const {
    return newNote_ ? newNote_->RawHotkey() : wxString();
}

i18n::Language SettingsDialog::Language() const {
    if (!languageChoice_) return languageSetting_;
    switch (languageChoice_->GetSelection()) {
        case 1:
            return i18n::Language::Chinese;
        case 2:
            return i18n::Language::English;
        default:
            return i18n::Language::Auto;
    }
}

GitSyncConfig SettingsDialog::CurrentGitConfig() const {
    GitSyncConfig config;
    const auto read = [](wxTextCtrl* field) {
        if (!field) return std::wstring();
        wxString value = field->GetValue();
        value.Trim(true).Trim(false);
        return ToW(value);
    };
    config.remoteUrl = read(gitUrl_);
    config.username = read(gitUsername_);
    config.token = read(gitToken_);
    config.branch = read(gitBranch_);
    config.gitExe = read(gitExeField_);
    if (config.branch.empty()) config.branch = L"main";
    return config;
}

GitSyncConfig SettingsDialog::GitConfig() const {
    return CurrentGitConfig();
}

bool SettingsDialog::ValidateGitConfig(const GitSyncConfig& config) {
    if (config.remoteUrl.empty()) return true;

    if (config.branch.empty()) {
        ShowError(UiText("settings.the_git_branch_cannot_be_empty"));
        return false;
    }
    if (!config.token.empty() && config.username.empty()) {
        ShowError(UiText("git_sync.a_username_is_required_when_using_a_password_or"));
        return false;
    }
    if (!config.gitExe.empty()) {
        std::error_code ec;
        if (!fs::is_regular_file(fs::path(config.gitExe), ec)) {
            ShowError(UiText("settings.the_specified_git_executable_does_not_exist") +
                      FromW(config.gitExe));
            return false;
        }
    }
    return true;
}

void SettingsDialog::ShowError(const wxString& text) {
    error_->SetLabel(text);
    error_->Wrap(FromDIP(520));
    error_->Show();
    Layout();
    scroller_->Layout();
    scroller_->FitInside();
    scroller_->Scroll(0, scroller_->GetScrollRange(wxVERTICAL));
}

void SettingsDialog::OnSave(wxCommandEvent&) {
    if (DataDirectory().empty()) {
        ShowError(UiText("main.the_data_folder_cannot_be_empty"));
        return;
    }

    const wxString showMain = ShowMainHotkey();
    const wxString newNote = NewNoteHotkey();

    HotkeySpec spec;
    const wxString values[] = {showMain, newNote};
    for (const wxString& value : values) {
        if (!ParseHotkey(value, spec)) {
            ShowError(UiText("settings.invalid_shortcut_format_please_enter_it_again"));
            return;
        }
    }

    auto same = [](const wxString& a, const wxString& b) {
        return !a.empty() && a.IsSameAs(b, false);
    };
    if (same(showMain, newNote)) {
        ShowError(UiText("main.different_actions_cannot_use_the_same_shortcut"));
        return;
    }

    if (!ValidateGitConfig(CurrentGitConfig())) return;
    EndModal(wxID_OK);
}

void SettingsDialog::OnPush() {
    RunSync(true);
}

void SettingsDialog::OnPull() {
    RunSync(false);
}

void SettingsDialog::RunSync(bool push) {
    if (error_) error_->Hide();
    const wxString requestedDataDir = DataDirectory();
    if (!requestedDataDir.IsSameAs(dataDir_, false)) {
        ShowError(UiText("settings.the_data_folder_has_changed_save_the_settings_be"));
        return;
    }

    const GitSyncConfig config = CurrentGitConfig();
    if (config.remoteUrl.empty()) {
        ShowError(UiText("git_sync.enter_the_git_repository_url_first"));
        return;
    }
    if (!ValidateGitConfig(config)) return;

    const std::wstring gitExe =
        config.gitExe.empty() ? FindGitExecutable() : config.gitExe;
    if (gitExe.empty()) {
        ShowError(UiText("settings.git_was_not_detected_enter_the_actual_path_to_gi"));
        return;
    }

    const bool hasNotes = hasOpenNotes_ && hasOpenNotes_();
    if (!push && hasNotes) {
        ShowError(UiText("settings.close_all_note_windows_before_pulling_from_the_r"));
        return;
    }

    if (push) {
        if (hasNotes) {
            const int answer = dialogs::Message(
                this,
                UiText("settings.some_note_windows_are_open_unsaved_changes_will"),
                UiText("settings.git_sync"),
                wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
            if (answer != wxYES) return;
        }
        const int answer = dialogs::Message(
            this,
            UiText("settings.push_will_commit_local_changes_and_force_push_th"),
            UiText("settings.push"),
            wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
        if (answer != wxYES) return;
    } else {
        const int answer = dialogs::Message(
            this,
            UiText("settings.pull_will_overwrite_local_notes_with_the_remote"),
            UiText("settings.pull"),
            wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
        if (answer != wxYES) return;
    }

    wxString saveError;
    if (!SaveGitSyncConfig(config, saveError)) {
        ShowError(saveError);
        return;
    }

    wxProgressDialog progress(
        push ? UiText("settings.push") : UiText("settings.pull"),
        UiText("settings.preparing"), 100, this,
        wxPD_APP_MODAL | wxPD_AUTO_HIDE);
    std::atomic<bool> done{false};
    std::mutex statusMutex;
    std::wstring status;
    GitSyncResult result;

    std::thread worker([&] {
        try {
            GitSync sync(ToW(dataDir_), config);
            const auto update = [&](const std::wstring& text) {
                std::lock_guard<std::mutex> lock(statusMutex);
                status = text;
            };
            result = push ? sync.Push(update) : sync.Pull(update);
        } catch (const std::exception&) {
            result.ok = false;
            result.message = i18n::Tr("settings.an_error_occurred_during_git_sync");
        } catch (...) {
            result.ok = false;
            result.message = i18n::Tr("settings.an_unknown_error_occurred_during_git_sync");
        }
        done.store(true);
    });

    while (!done.load()) {
        wxMilliSleep(100);
        std::wstring current;
        {
            std::lock_guard<std::mutex> lock(statusMutex);
            current = status;
        }
        if (!current.empty()) {
            progress.Pulse(FromW(current));
        } else {
            progress.Pulse();
        }
        wxYieldIfNeeded();
    }
    worker.join();
    progress.Hide();

    if (!result.ok) {
        wxString message = FromW(result.message);
        if (!result.details.empty()) message += L"\n\n" + FromW(result.details);
        dialogs::Message(this, message,
                         UiText("settings.git_sync_failed"),
                         wxOK | wxICON_ERROR);
        ShowError(UiText("settings.git_sync_failed_check_the_repository_url_git_pat"));
        return;
    }

    if (!push) pulled_ = true;
    if (error_) error_->Hide();

    wxString message = FromW(result.message);
    if (!result.details.empty()) message += L"\n\n" + FromW(result.details);
    if (!result.backupPath.empty()) {
        message += UiText("settings.local_backup") +
                   FromW(result.backupPath);
    }
    if (!push) {
        message += UiText("settings.the_note_list_will_refresh_after_closing_setting");
    }
    dialogs::Message(this, message, UiText("settings.git_sync"),
                     wxOK | wxICON_INFORMATION);
}
