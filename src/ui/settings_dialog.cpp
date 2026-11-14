#include "ui/settings_dialog.h"

#include "core/text.h"
#include "ui/app_font.h"
#include "ui/git_config.h"
#include "ui/icon_button.h"
#include "ui/icon_library.h"
#include "ui/theme.h"
#include "ui/ui_ids.h"

#include <wx/accel.h>
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
        ChangeValue(L"未设置");
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
                               std::function<bool()> hasOpenNotes)
    : wxDialog(parent, wxID_ANY, L"设置", wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE),
      hasOpenNotes_(std::move(hasOpenNotes)),
      dataDir_(dataDir) {
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
        content->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
    };

    addHeading(L"数据存储");

    addHint(L"便签保存在下面的目录；点击右侧文件夹图标更改，修改后下次启动仍然生效。\n"
            L"路径较长时可悬停查看完整内容。");

    auto* dataRow = new wxBoxSizer(wxHORIZONTAL);
    auto* dataLabel = new wxStaticText(scroller_, wxID_ANY, L"数据目录");
    dataLabel->SetForegroundColour(Theme::TextSubtle());
    dataLabel->SetMinSize(FromDIP(wxSize(160, -1)));
    dataRow->Add(dataLabel, 0, wxALIGN_CENTER_VERTICAL);

    dataDirField_ = new wxTextCtrl(scroller_, wxID_ANY, dataDir, wxDefaultPosition,
                                   wxDefaultSize, wxTE_READONLY | wxTE_CENTRE);
    dataDirField_->SetToolTip(dataDir);
    dataRow->Add(dataDirField_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));

    auto* browse = new IconButton(scroller_, kBrowseDataDirId, AppIcon::Folder,
                                  L"选择数据目录", IconButton::Kind::Normal,
                                  FromDIP(wxSize(34, 30)), 14);
    browse->SetEventTarget(this);
    dataRow->Add(browse, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    content->Add(dataRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    addHeading(L"Git 同步");

    addHint(L"data 目录整体作为一个 Git 项目：所有分组、便签和其他文件"
            L"都会一起同步。\n"
            L"推送：提交本地改动后强制推送，远程以本地为准；\n"
            L"拉取：先自动备份本地数据，再用远程内容强制覆盖本地。\n"
            L"不做冲突合并。同步前请关闭所有便签窗口。");

    auto addTextField = [&](const wxString& label, wxTextCtrl*& field,
                            const wxString& value, long style,
                            const wxString& hint) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* labelText = new wxStaticText(scroller_, wxID_ANY, label);
        labelText->SetForegroundColour(Theme::TextSubtle());
        labelText->SetMinSize(FromDIP(wxSize(160, -1)));
        row->Add(labelText, 0, wxALIGN_CENTER_VERTICAL);

        field = new wxTextCtrl(scroller_, wxID_ANY, value, wxDefaultPosition,
                               wxDefaultSize, style | wxTE_CENTRE);
        if (!hint.empty()) field->SetHint(hint);
        row->Add(field, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
        content->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
    };

    addTextField(L"仓库地址", gitUrl_, FromW(gitConfig.remoteUrl), 0,
                 L"https://github.com/user/notes.git");

    auto* exeRow = new wxBoxSizer(wxHORIZONTAL);
    auto* exeLabel = new wxStaticText(scroller_, wxID_ANY, L"Git 程序");
    exeLabel->SetForegroundColour(Theme::TextSubtle());
    exeLabel->SetMinSize(FromDIP(wxSize(160, -1)));
    exeRow->Add(exeLabel, 0, wxALIGN_CENTER_VERTICAL);

    gitExeField_ = new wxTextCtrl(scroller_, wxID_ANY, FromW(gitConfig.gitExe),
                                  wxDefaultPosition, wxDefaultSize, wxTE_CENTRE);
    gitExeField_->SetHint(L"留空自动检测，例如 D:\\Git\\bin\\git.exe");
    gitExeField_->SetToolTip(L"可填写本地 git.exe 的完整路径；留空则自动从 PATH 查找。");
    gitExeField_->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        UpdateGitStatus();
        event.Skip();
    });
    exeRow->Add(gitExeField_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));

    auto* browseGit = new IconButton(scroller_, kBrowseGitId, AppIcon::Folder,
                                     L"选择 git.exe", IconButton::Kind::Normal,
                                     FromDIP(wxSize(34, 30)), 14);
    browseGit->SetEventTarget(this);
    exeRow->Add(browseGit, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    content->Add(exeRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    gitStatus_ = new wxStaticText(scroller_, wxID_ANY, wxEmptyString);
    gitStatus_->SetFont(AppFont::Make(std::max(8, AppFont::BasePointSize() - 2)));
    content->Add(gitStatus_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
    UpdateGitStatus();

    addTextField(L"用户名", gitUsername_, FromW(gitConfig.username), 0,
                 L"公开仓库可留空");
    addTextField(L"密码 / 令牌", gitToken_, FromW(gitConfig.token), wxTE_PASSWORD,
                 L"访问令牌，加密保存在本机");
    addTextField(L"分支", gitBranch_,
                 FromW(gitConfig.branch.empty() ? std::wstring(L"main")
                                                : gitConfig.branch),
                 0, L"main");

    auto* syncRow = new wxBoxSizer(wxHORIZONTAL);
    auto* syncLabel = new wxStaticText(scroller_, wxID_ANY, L"立即同步");
    syncLabel->SetForegroundColour(Theme::TextSubtle());
    syncLabel->SetMinSize(FromDIP(wxSize(160, -1)));
    syncRow->Add(syncLabel, 0, wxALIGN_CENTER_VERTICAL);

    auto* push = new IconButton(
        scroller_, kGitPushId, AppIcon::CloudUpload,
        L"推送到远程：提交本地改动并强制推送，本地覆盖远程", IconButton::Kind::Normal,
        FromDIP(wxSize(34, 30)), 15);
    push->SetEventTarget(this);
    syncRow->Add(push, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));

    auto* pushText = new wxStaticText(scroller_, wxID_ANY, L"推送到远程");
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
        L"从远程拉取：先用远程内容覆盖本地，拉取前自动备份", IconButton::Kind::Normal,
        FromDIP(wxSize(34, 30)), 15);
    pull->SetEventTarget(this);
    syncRow->Add(pull, 0, wxALIGN_CENTER_VERTICAL);

    auto* pullText = new wxStaticText(scroller_, wxID_ANY, L"从远程拉取");
    pullText->SetForegroundColour(Theme::Text());
    pullText->SetCursor(wxCursor(wxCURSOR_HAND));
    pullText->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
        wxCommandEvent event(wxEVT_BUTTON, kGitPullId);
        event.SetEventObject(this);
        ProcessEvent(event);
    });
    syncRow->Add(pullText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
    content->Add(syncRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    addHeading(L"快捷键设置");

    addHint(L"点击输入框后按下组合键；\n"
            L"支持 Ctrl / Alt / Shift 的任意组合 + 字母、数字或 F1–F12；\n"
            L"F1–F12 也可以单独使用。留空表示不启用，程序隐藏到托盘时仍然有效。");

    auto addHotkeyRow = [&](const wxString& label, HotkeyCtrl*& field,
                            const wxString& value) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* labelText = new wxStaticText(scroller_, wxID_ANY, label);
        labelText->SetForegroundColour(Theme::TextSubtle());
        labelText->SetMinSize(FromDIP(wxSize(160, -1)));
        row->Add(labelText, 0, wxALIGN_CENTER_VERTICAL);

        field = new HotkeyCtrl(scroller_);
        field->SetRawHotkey(value);
        row->Add(field, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));

        content->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
    };

    addHotkeyRow(L"显示主窗口", showMain_, showMainHotkey);
    addHotkeyRow(L"新建便签", newNote_, newNoteHotkey);

    error_ = new wxStaticText(scroller_, wxID_ANY, wxEmptyString);
    error_->SetForegroundColour(Theme::Danger());
    error_->Hide();
    content->Add(error_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    scroller_->SetSizer(content);

    root->Add(scroller_, 1, wxEXPAND);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer(1);
    auto* cancel = new IconButton(this, wxID_CANCEL, AppIcon::Close, L"取消");
    auto* save = new IconButton(this, wxID_OK, AppIcon::Check, L"保存",
                                IconButton::Kind::Accent);
    buttons->Add(cancel, 0, wxRIGHT, FromDIP(6));
    buttons->Add(save, 0);
    root->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(20));

    SetSizer(root);

    const wxRect work = wxGetClientDisplayRect();
    const int width = std::min(FromDIP(600), work.width - FromDIP(32));
    const int height = std::min(FromDIP(700), work.height - FromDIP(32));
    SetSize(wxSize(std::max(width, FromDIP(460)),
                   std::max(height, FromDIP(420))));
    SetMinSize(wxSize(std::min(FromDIP(520), width), std::min(FromDIP(520), height)));
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
    wxDirDialog dialog(this, L"选择便签数据目录", DataDirectory(), wxDD_DEFAULT_STYLE);
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
    wxFileDialog dialog(this, L"选择 git.exe", currentFile.GetPath(),
                        currentFile.GetFullName(),
                        L"git.exe|git.exe|可执行文件 (*.exe)|*.exe|所有文件 (*.*)|*.*",
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
        gitStatus_->SetLabel(configured.empty()
                                 ? wxString(L"未检测到 Git，请安装 Git for Windows 或填写 git.exe 路径。")
                                 : wxString(L"Git 程序不存在：") + configured);
        gitStatus_->SetToolTip(wxString());
    } else {
        gitStatus_->SetForegroundColour(Theme::TextFaint());
        gitStatus_->SetLabel(L"Git：" + FromW(gitExe));
        gitStatus_->SetToolTip(FromW(gitExe));
    }
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
        ShowError(L"Git 分支不能为空。");
        return false;
    }
    if (!config.token.empty() && config.username.empty()) {
        ShowError(L"使用密码或访问令牌时，必须填写用户名。");
        return false;
    }
    if (!config.gitExe.empty()) {
        std::error_code ec;
        if (!fs::is_regular_file(fs::path(config.gitExe), ec)) {
            ShowError(wxString(L"填写的 Git 程序不存在：\n") + FromW(config.gitExe));
            return false;
        }
    }
    return true;
}

void SettingsDialog::ShowError(const wxString& text) {
    error_->SetLabel(text);
    error_->Show();
    Layout();
    scroller_->Layout();
    scroller_->FitInside();
    scroller_->Scroll(0, scroller_->GetScrollRange(wxVERTICAL));
}

void SettingsDialog::OnSave(wxCommandEvent&) {
    if (DataDirectory().empty()) {
        ShowError(L"数据目录不能为空。");
        return;
    }

    const wxString showMain = ShowMainHotkey();
    const wxString newNote = NewNoteHotkey();

    HotkeySpec spec;
    const wxString values[] = {showMain, newNote};
    for (const wxString& value : values) {
        if (!ParseHotkey(value, spec)) {
            ShowError(L"快捷键格式无效，请重新录入。");
            return;
        }
    }

    auto same = [](const wxString& a, const wxString& b) {
        return !a.empty() && a.IsSameAs(b, false);
    };
    if (same(showMain, newNote)) {
        ShowError(L"不同操作不能使用相同的快捷键。");
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
        ShowError(L"数据目录已修改，请先保存设置，再执行 Git 同步。");
        return;
    }

    const GitSyncConfig config = CurrentGitConfig();
    if (config.remoteUrl.empty()) {
        ShowError(L"请先填写 Git 仓库地址。");
        return;
    }
    if (!ValidateGitConfig(config)) return;

    const std::wstring gitExe =
        config.gitExe.empty() ? FindGitExecutable() : config.gitExe;
    if (gitExe.empty()) {
        ShowError(L"未检测到 Git，请填写 git.exe 的实际路径。");
        return;
    }

    const bool hasNotes = hasOpenNotes_ && hasOpenNotes_();
    if (!push && hasNotes) {
        ShowError(L"请先关闭所有便签窗口，再执行“从远程拉取”。");
        return;
    }

    if (push) {
        if (hasNotes) {
            const int answer = wxMessageBox(
                L"有打开的便签窗口，未保存的修改不会包含在推送内容中。\n\n是否继续？",
                L"Git 同步", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
            if (answer != wxYES) return;
        }
        const int answer = wxMessageBox(
            L"推送会提交本地改动并强制推送到远程仓库。\n"
            L"如果远程和本地不一致，以本地为准，远程内容会被覆盖。\n\n是否继续？",
            L"推送到远程", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this);
        if (answer != wxYES) return;
    } else {
        const int answer = wxMessageBox(
            L"拉取会用远程仓库内容强制覆盖本地便签。\n"
            L"如果本地和远程不一致，以远程为准；覆盖前会自动备份本地数据。\n\n"
            L"是否继续？",
            L"从远程拉取", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this);
        if (answer != wxYES) return;
    }

    wxString saveError;
    if (!SaveGitSyncConfig(config, saveError)) {
        ShowError(saveError);
        return;
    }

    wxProgressDialog progress(push ? L"推送到远程" : L"从远程拉取",
                              L"正在准备...", 100, this,
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
            result.message = L"Git 同步过程中发生异常。";
        } catch (...) {
            result.ok = false;
            result.message = L"Git 同步过程中发生未知异常。";
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
        wxMessageBox(message, L"Git 同步失败", wxOK | wxICON_ERROR, this);
        ShowError(L"Git 同步失败，请检查仓库地址、Git 路径、账号和网络。");
        return;
    }

    if (!push) pulled_ = true;
    if (error_) error_->Hide();

    wxString message = FromW(result.message);
    if (!result.details.empty()) message += L"\n\n" + FromW(result.details);
    if (!result.backupPath.empty()) {
        message += L"\n\n本地备份：\n" + FromW(result.backupPath);
    }
    if (!push) message += L"\n\n关闭设置后便签列表会自动刷新。";
    wxMessageBox(message, L"Git 同步", wxOK | wxICON_INFORMATION, this);
}
