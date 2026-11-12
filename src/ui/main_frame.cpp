#include "ui/main_frame.h"

#include "core/text.h"
#include "resource.h"
#include "single_instance.h"
#include "ui/app_font.h"
#include "ui/git_config.h"
#include "ui/icon_button.h"
#include "ui/name_dialog.h"
#include "ui/note_tree_ctrl.h"
#include "ui/note_window.h"
#include "ui/rounded_panel.h"
#include "ui/settings_dialog.h"
#include "ui/theme.h"
#include "ui/ui_ids.h"
#include "ui/window_effects.h"

#include <wx/config.h>
#include <wx/accel.h>
#include <wx/choicdlg.h>
#include <wx/display.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/taskbar.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif

#include <algorithm>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

enum {
    ID_CTX_NEW_SUBGROUP = wxID_HIGHEST + 300,
    ID_CTX_NEW_NOTE,
    ID_CTX_NEW_MARKDOWN_NOTE,
    ID_CTX_NEW_GROUP,
    ID_CTX_NEW_ROOT_NOTE,
    ID_CTX_NEW_ROOT_MARKDOWN_NOTE,
    ID_CTX_OPEN,
    ID_CTX_RENAME,
    ID_CTX_MOVE,
    ID_CTX_DELETE,
    ID_TRAY_OPEN,
    ID_TRAY_EXIT,
};

constexpr int kHotkeyShowMainId = 0x5101;
constexpr int kHotkeyNewNoteId = 0x5102;
constexpr int kHotkeyNewMarkdownId = 0x5103;

std::wstring LowerText(std::wstring text) {
    for (wchar_t& ch : text) ch = (wchar_t)towlower(ch);
    return text;
}

bool IsPathInGroups(const std::wstring& path, const std::set<std::wstring>& groups) {
    for (const std::wstring& group : groups) {
        if (LowerText(path) == LowerText(group)) return true;
        if (path.size() > group.size() &&
            LowerText(path.substr(0, group.size())) == LowerText(group) &&
            (path[group.size()] == L'\\' || path[group.size()] == L'/')) {
            return true;
        }
    }
    return false;
}

int GroupDepth(const std::wstring& path) {
    int depth = 0;
    for (wchar_t ch : path) {
        if (ch == L'\\' || ch == L'/') ++depth;
    }
    return depth;
}

bool HasPathPrefix(const std::wstring& path, const std::wstring& prefix) {
    if (LowerText(path) == LowerText(prefix)) return true;
    return path.size() > prefix.size() &&
           LowerText(path.substr(0, prefix.size())) == LowerText(prefix) &&
           (path[prefix.size()] == L'\\' || path[prefix.size()] == L'/');
}

std::wstring ReplacePathPrefix(const std::wstring& path, const std::wstring& oldPrefix,
                               const std::wstring& newPrefix) {
    if (LowerText(path) == LowerText(oldPrefix)) return newPrefix;
    if (HasPathPrefix(path, oldPrefix)) return newPrefix + path.substr(oldPrefix.size());
    return path;
}

wxSize DefaultWindowSize(wxWindow* window) {
    wxDisplay display((unsigned)0);
    wxRect workArea = display.GetClientArea();
    int minWidth = window->FromDIP(320);
    int minHeight = window->FromDIP(540);
    int width = std::min(window->FromDIP(440), (int)(workArea.width * 0.33));
    int height = std::min(window->FromDIP(760), workArea.height - window->FromDIP(24));
    width = std::max(width, minWidth);
    height = std::max(height, minHeight);
    width = std::min(width, workArea.width);
    height = std::min(height, workArea.height);
    return wxSize(width, height);
}

}  // namespace

class TrayIcon : public wxTaskBarIcon {
public:
    TrayIcon(std::function<void()> onOpen, std::function<void()> onExit)
        : onOpen_(std::move(onOpen)), onExit_(std::move(onExit)) {
        Bind(wxEVT_TASKBAR_LEFT_UP, [this](wxTaskBarIconEvent&) {
            if (onOpen_) onOpen_();
        });
        Bind(wxEVT_TASKBAR_LEFT_DCLICK, [this](wxTaskBarIconEvent&) {
            if (onOpen_) onOpen_();
        });
        Bind(wxEVT_MENU,
             [this](wxCommandEvent&) {
                 if (onOpen_) onOpen_();
             },
             ID_TRAY_OPEN);
        Bind(wxEVT_MENU,
             [this](wxCommandEvent&) {
                 if (onExit_) onExit_();
             },
             ID_TRAY_EXIT);
    }

    wxMenu* CreatePopupMenu() override {
        auto* menu = new wxMenu;
        menu->Append(ID_TRAY_OPEN, L"打开 PenDown");
        menu->AppendSeparator();
        menu->Append(ID_TRAY_EXIT, L"关闭");
        return menu;
    }

private:
    std::function<void()> onOpen_;
    std::function<void()> onExit_;
};

MainFrame::MainFrame(std::shared_ptr<NoteStore> store)
    : wxFrame(nullptr, wxID_ANY, L"PenDown", wxDefaultPosition, wxDefaultSize,
              wxBORDER_NONE | wxRESIZE_BORDER | wxCLIP_CHILDREN),
      store_(std::move(store)) {
    SetBackgroundColour(Theme::Background());
    SetSize(DefaultWindowSize(this));
    SetMinSize(FromDIP(wxSize(320, 540)));

    BuildUi();
    BindEvents();
    if (bottomStatus_) {
        bottomStatus_->SetFont(AppFont::Make(std::max(8, AppFont::BasePointSize() - 2)));
    }
    if (countLabel_) {
        countLabel_->SetFont(AppFont::Make(std::max(8, AppFont::BasePointSize() - 2)));
    }
    RestoreLayout();
    Layout();
    PrepareFramelessWindow(this);

    LoadData();
    RebuildTree();
    if (tree_) tree_->SetFocus();

#ifdef __WXMSW__
    HICON appIcon = (HICON)::LoadImageW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP),
                                        IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    if (appIcon) {
        wxIcon icon;
        if (icon.CreateFromHICON(appIcon)) {
            SetIcon(icon);
        } else {
            ::DestroyIcon(appIcon);
        }
    }
    CreateTrayIcon();
    ::SetPropW((HWND)GetHandle(), kPenDownMainWindowProperty, reinterpret_cast<HANDLE>(1));
#endif
    LoadHotkeys();
}

#ifdef __WXMSW__
WXLRESULT MainFrame::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) {
    static const UINT showMainMessage = ::RegisterWindowMessageW(kPenDownShowMainMessage);
    if (showMainMessage != 0 && message == showMainMessage) {
        // 第二个实例启动时请求已有实例显示主窗口
        RestoreFromTray();
        return 0;
    }

    if (message == WM_HOTKEY) {
        const int id = (int)wParam;
        if (id == kHotkeyShowMainId) {
            RestoreFromTray();
        } else if (id == kHotkeyNewNoteId) {
            // 只打开便签编辑器，不把主窗口从托盘带出来
            AddNote(NoteFormat::RichText);
        } else if (id == kHotkeyNewMarkdownId) {
            AddNote(NoteFormat::Markdown);
        }
        return 0;
    }

    WXLRESULT framelessResult = 0;
    if (HandleFramelessWindowMessage(this, message, wParam, lParam, framelessResult)) {
        return framelessResult;
    }
    if (message == WM_NCHITTEST && !IsMaximized()) {
        int x = (int)(short)LOWORD((DWORD_PTR)lParam);
        int y = (int)(short)HIWORD((DWORD_PTR)lParam);
        RECT rect;
        ::GetWindowRect((HWND)GetHandle(), &rect);
        const int border = FromDIP(4);
        const bool left = x >= rect.left && x < rect.left + border;
        const bool right = x < rect.right && x >= rect.right - border;
        const bool top = y >= rect.top && y < rect.top + border;
        const bool bottom = y < rect.bottom && y >= rect.bottom - border;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }
    return wxFrame::MSWWindowProc(message, wParam, lParam);
}
#endif

void MainFrame::BuildUi() {
    auto* borderPanel = new WindowFramePanel(this);
    auto* content = new wxPanel(borderPanel);
    content->SetBackgroundColour(Theme::Background());
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* titleBar = new wxPanel(content);
    titleBar->SetBackgroundColour(Theme::Surface());
    auto* titleOuter = new wxBoxSizer(wxVERTICAL);
    auto* bar = new wxBoxSizer(wxHORIZONTAL);

    // 标题栏左右留白与便签窗口保持一致
    const int titleLeftPadding = FromDIP(8);
    bar->AddSpacer(titleLeftPadding);

    const wxSize buttonSize = FromDIP(wxSize(26, 22));
    addGroupButton_ = new IconButton(titleBar, ID_NOTE_ADD_GROUP, AppIcon::FolderPlus,
                                     L"新建分组 (Ctrl+Shift+N)：选中分组时在其下新增子分组",
                                     IconButton::Kind::Normal, buttonSize, 12);
    addNoteButton_ = new IconButton(titleBar, ID_NOTE_ADD, AppIcon::NotePlus,
                                    L"新建普通便签 (Ctrl+N)：未选分组时创建在根目录",
                                    IconButton::Kind::Normal, buttonSize, 12);
    addMarkdownButton_ = new IconButton(titleBar, ID_NOTE_ADD_MARKDOWN, AppIcon::Markdown,
                                        L"新建 Markdown 便签 (Ctrl+M)：编辑原文或切换预览模式",
                                        IconButton::Kind::Normal, buttonSize, 12);
    deleteButton_ = new IconButton(titleBar, ID_NOTE_DELETE, AppIcon::Trash,
                                   L"删除选中的分组或便签 (Del)", IconButton::Kind::Danger,
                                   buttonSize, 12);
    addGroupButton_->SetEventTarget(this);
    addNoteButton_->SetEventTarget(this);
    addMarkdownButton_->SetEventTarget(this);
    deleteButton_->SetEventTarget(this);
    bar->Add(addGroupButton_, 0, wxRIGHT, FromDIP(1));
    bar->Add(addNoteButton_, 0, wxRIGHT, FromDIP(1));
    bar->Add(addMarkdownButton_, 0, wxRIGHT, FromDIP(1));
    bar->Add(deleteButton_, 0);
    bar->AddStretchSpacer(1);

    auto* settings = new IconButton(titleBar, ID_SETTINGS, AppIcon::Settings, L"设置",
                                    IconButton::Kind::Normal, buttonSize, 12);
    settings->SetEventTarget(this);
    bar->Add(settings, 0);

    auto* minimize = new IconButton(titleBar, ID_WINDOW_MINIMIZE, AppIcon::Minimize,
                                    L"最小化", IconButton::Kind::Normal, buttonSize, 12);
    auto* maximize = new IconButton(titleBar, ID_WINDOW_MAXIMIZE, AppIcon::Maximize,
                                    L"最大化 / 还原", IconButton::Kind::Normal, buttonSize,
                                    12);
    auto* close = new IconButton(titleBar, ID_WINDOW_CLOSE, AppIcon::Close, L"关闭",
                                 IconButton::Kind::Danger, buttonSize, 12);
    minimize->SetEventTarget(this);
    maximize->SetEventTarget(this);
    close->SetEventTarget(this);
    bar->Add(minimize, 0, wxLEFT, FromDIP(4));
    bar->Add(maximize, 0, wxLEFT, FromDIP(1));
    bar->Add(close, 0, wxLEFT | wxRIGHT, FromDIP(4));

    titleOuter->Add(bar, 1, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(5));
    titleBar->SetSizer(titleOuter);
    root->Add(titleBar, 0, wxEXPAND);

#ifdef __WXMSW__
    auto beginDrag = [this](wxMouseEvent&) {
        ::ReleaseCapture();
        ::SendMessageW((HWND)GetHandle(), WM_NCLBUTTONDOWN, HTCAPTION, 0);
    };
    auto toggleMax = [this](wxMouseEvent&) { Maximize(!IsMaximized()); };
    titleBar->Bind(wxEVT_LEFT_DOWN, beginDrag);
    titleBar->Bind(wxEVT_LEFT_DCLICK, toggleMax);
#endif

    auto* treeHost = new RoundedPanel(content, 7, 4);
    // 主列表只保留上下两条分隔线，去掉左右边框和圆角
    treeHost->SetHorizontalBordersOnly(true);
    tree_ = new NoteTreeCtrl(treeHost, ID_NOTE_TREE);
    treeHost->AddContent(tree_, 1);
    root->AddSpacer(FromDIP(4));
    root->Add(treeHost, 1, wxEXPAND);

    bottomStatus_ = new wxStaticText(content, wxID_ANY, wxEmptyString);
    bottomStatus_->SetForegroundColour(Theme::TextFaint());
    bottomStatus_->SetFont(AppFont::Make(std::max(8, AppFont::BasePointSize() - 2)));

    countLabel_ = new wxStaticText(content, wxID_ANY, wxEmptyString);
    countLabel_->SetForegroundColour(Theme::TextFaint());
    countLabel_->SetFont(AppFont::Make(std::max(8, AppFont::BasePointSize() - 2)));

    auto* footer = new wxBoxSizer(wxHORIZONTAL);
    footer->Add(bottomStatus_, 1, wxALIGN_CENTER_VERTICAL);
    footer->Add(countLabel_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    root->AddSpacer(FromDIP(2));
    root->Add(footer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(1));
    root->AddSpacer(FromDIP(3));

    content->SetSizer(root);

    auto* borderSizer = new wxBoxSizer(wxVERTICAL);
    borderSizer->Add(content, 1, wxEXPAND | wxALL, borderPanel->ContentInset());
    borderPanel->SetSizer(borderSizer);

    auto* frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(borderPanel, 1, wxEXPAND);
    SetSizer(frameSizer);
}

void MainFrame::BindEvents() {
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddGroup(); }, ID_NOTE_ADD_GROUP);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddNote(NoteFormat::RichText); }, ID_NOTE_ADD);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddNote(NoteFormat::Markdown); },
         ID_NOTE_ADD_MARKDOWN);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DeleteSelected(); }, ID_NOTE_DELETE);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Iconize(); }, ID_WINDOW_MINIMIZE);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Maximize(!IsMaximized()); },
         ID_WINDOW_MAXIMIZE);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); }, ID_WINDOW_CLOSE);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OpenSettings(); }, ID_SETTINGS);
    Bind(wxEVT_QUERY_END_SESSION, [this](wxCloseEvent& event) {
        reallyExit_ = true;
        event.Skip();
    });
    Bind(wxEVT_END_SESSION, [this](wxCloseEvent& event) {
        reallyExit_ = true;
        event.Skip();
    });
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        ApplyRoundedWindowRegion(this, 10);
        event.Skip();
    });
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.ControlDown() && event.GetKeyCode() == 'N') {
            if (event.ShiftDown()) {
                AddGroup();
            } else {
                AddNote(NoteFormat::RichText);
            }
            return;
        }
        if (event.ControlDown() && event.GetKeyCode() == 'M') {
            AddNote(NoteFormat::Markdown);
            return;
        }
        if (event.GetKeyCode() == WXK_F5) {
            std::wstring group = selectedGroupPath_;
            std::wstring note = selectedNotePath_;
            LoadData();
            selectedGroupPath_ = group;
            selectedNotePath_ = note;
            RebuildTree();
            return;
        }
        event.Skip();
    });

    tree_->SetSelectionHandler([this] { OnTreeSelection(); });
    tree_->SetOpenNoteHandler([this](const std::wstring& path) { OpenNote(path); });
    tree_->SetToggleGroupHandler(
        [this](const std::wstring& path) { OnToggleGroup(path); });
    tree_->SetContextHandler(
        [this](bool hasItem, NoteTreeCtrl::Kind kind, const std::wstring& path,
               const wxPoint& screen) { OnTreeContext(hasItem, kind, path, screen); });
    tree_->SetDeleteHandler([this] { DeleteSelected(); });
    tree_->SetRenameHandler([this] { RenameSelected(); });
}

void MainFrame::RestoreLayout() {
    wxConfigBase* config = wxConfigBase::Get();
    if (!config) {
        Centre();
        return;
    }

    long layoutVersion = 0;
    config->Read("/Window/Version", &layoutVersion, 0);
    if (layoutVersion < 4) {
        // 旧版主窗口是宽屏任务列表，升级后第一次启动改用窄长便签窗口
        Centre();
        config->Write("/Window/Version", 4);
        config->Write("/Window/X", GetPosition().x);
        config->Write("/Window/Y", GetPosition().y);
        config->Write("/Window/Width", GetSize().x);
        config->Write("/Window/Height", GetSize().y);
        config->Flush();
        return;
    }

    long x = -1;
    long y = -1;
    long width = 0;
    long height = 0;
    config->Read("/Window/X", &x, -1);
    config->Read("/Window/Y", &y, -1);
    config->Read("/Window/Width", &width, 0);
    config->Read("/Window/Height", &height, 0);

    int minWidth = FromDIP(320);
    int minHeight = FromDIP(540);
    if (x != -1 && y != -1 && width >= minWidth && height >= minHeight) {
        int displayIndex = wxDisplay::GetFromPoint(wxPoint((int)x, (int)y));
        if (displayIndex == wxNOT_FOUND) {
            Centre();
        } else {
            wxRect workArea = wxDisplay(displayIndex).GetClientArea();
            int w = std::min(std::max((int)width, minWidth), workArea.width);
            int h = std::min(std::max((int)height, minHeight), workArea.height);
            int left = std::min(std::max((int)x, workArea.x), workArea.GetRight() - w);
            int top = std::min(std::max((int)y, workArea.y), workArea.GetBottom() - h);
            SetSize(wxRect(left, top, w, h));
        }
    } else {
        Centre();
    }
}

void MainFrame::SaveLayout() {
    wxConfigBase* config = wxConfigBase::Get();
    if (!config) return;
    config->Write("/Window/X", GetPosition().x);
    config->Write("/Window/Y", GetPosition().y);
    config->Write("/Window/Width", GetSize().x);
    config->Write("/Window/Height", GetSize().y);
    config->Write("/Window/Version", 4);
    config->Flush();
}

MainFrame::~MainFrame() {
    UnregisterHotkeys();
    if (trayIcon_) {
        trayIcon_->RemoveIcon();
        delete trayIcon_;
        trayIcon_ = nullptr;
    }
}

void MainFrame::CreateTrayIcon() {
#ifdef __WXMSW__
    if (trayIcon_) return;
    trayIcon_ = new TrayIcon([this] { RestoreFromTray(); }, [this] { ExitFromTray(); });

    const int cx = ::GetSystemMetrics(SM_CXSMICON);
    const int cy = ::GetSystemMetrics(SM_CYSMICON);
    HICON trayHandle = (HICON)::LoadImageW(::GetModuleHandleW(nullptr),
                                           MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, cx, cy,
                                           LR_DEFAULTCOLOR);
    bool ok = false;
    if (trayHandle) {
        wxIcon icon;
        if (icon.CreateFromHICON(trayHandle)) {
            ok = trayIcon_->SetIcon(icon, L"PenDown");
        } else {
            ::DestroyIcon(trayHandle);
        }
    }
    trayAvailable_ = ok;
    if (!ok) {
        delete trayIcon_;
        trayIcon_ = nullptr;
    }
#endif
}

void MainFrame::HideToTray() {
    if (hiddenToTray_) return;
    hiddenToTray_ = true;
    for (NoteWindow* window : noteWindows_) {
        if (window) window->Hide();
    }
    Hide();
#ifdef __WXMSW__
    // 隐藏到托盘后把工作集交还系统，任务管理器里的内存占用会明显下降；
    // 恢复窗口时按需重新读入，已打开的便签内容不受影响。
    ::SetProcessWorkingSetSize(::GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
#endif
}

void MainFrame::RestoreFromTray() {
    hiddenToTray_ = false;
    Iconize(false);
    Show(true);
    for (NoteWindow* window : noteWindows_) {
        if (window) window->Show(true);
    }
    Raise();
    if (tree_) tree_->SetFocus();
}

void MainFrame::ExitFromTray() {
    // 托盘菜单事件还在 wxTaskBarIcon 的处理链上，延迟到菜单关闭后再退出，
    // 避免在菜单事件处理中销毁托盘图标。
    CallAfter([this] {
        reallyExit_ = true;
        if (hiddenToTray_ || !IsShown() || IsIconized()) RestoreFromTray();
        Close();
    });
}

void MainFrame::LoadHotkeys() {
    auto makeDefault = [](int key) {
        return wxAcceleratorEntry(wxACCEL_CTRL | wxACCEL_ALT, key).ToRawString();
    };
    hotkeyShowMain_ = makeDefault('H');
    hotkeyNewNote_ = makeDefault('N');
    hotkeyNewMarkdown_ = makeDefault('M');

    if (wxConfigBase* config = wxConfigBase::Get()) {
        config->Read("/Hotkeys/ShowMain", &hotkeyShowMain_, hotkeyShowMain_);
        config->Read("/Hotkeys/NewNote", &hotkeyNewNote_, hotkeyNewNote_);
        config->Read("/Hotkeys/NewMarkdown", &hotkeyNewMarkdown_, hotkeyNewMarkdown_);
    }
    RegisterHotkeys();
}

void MainFrame::RegisterHotkeys() {
#ifdef __WXMSW__
    HWND hwnd = (HWND)GetHandle();
    if (!hwnd) return;
    UnregisterHotkeys();

    auto registerOne = [&](int id, const wxString& raw) {
        HotkeySpec spec;
        if (!ParseHotkey(raw, spec) || !spec.enabled) return;
        UINT modifiers = 0;
        if (spec.ctrl) modifiers |= MOD_CONTROL;
        if (spec.alt) modifiers |= MOD_ALT;
        if (spec.shift) modifiers |= MOD_SHIFT;
        ::RegisterHotKey(hwnd, id, modifiers | MOD_NOREPEAT, spec.virtualKey);
    };

    registerOne(kHotkeyShowMainId, hotkeyShowMain_);
    registerOne(kHotkeyNewNoteId, hotkeyNewNote_);
    registerOne(kHotkeyNewMarkdownId, hotkeyNewMarkdown_);
#endif
}

void MainFrame::UnregisterHotkeys() {
#ifdef __WXMSW__
    HWND hwnd = (HWND)GetHandle();
    if (!hwnd) return;
    ::UnregisterHotKey(hwnd, kHotkeyShowMainId);
    ::UnregisterHotKey(hwnd, kHotkeyNewNoteId);
    ::UnregisterHotKey(hwnd, kHotkeyNewMarkdownId);
#endif
}

bool MainFrame::ApplyHotkeys(const wxString& showMain, const wxString& newNote,
                             const wxString& newMarkdown) {
    HotkeySpec specs[3];
    const wxString values[3] = {showMain, newNote, newMarkdown};
    const wxString labels[3] = {L"显示主窗口", L"新建普通便签", L"新建 Markdown 便签"};
    for (int i = 0; i < 3; ++i) {
        if (!ParseHotkey(values[i], specs[i])) {
            wxMessageBox(L"快捷键格式无效，请重新设置。", L"设置", wxOK | wxICON_WARNING, this);
            return false;
        }
    }

    auto duplicate = [](const HotkeySpec& a, const HotkeySpec& b) {
        return a.enabled && b.enabled && a.ctrl == b.ctrl && a.alt == b.alt &&
               a.shift == b.shift && a.virtualKey == b.virtualKey;
    };
    if (duplicate(specs[0], specs[1]) || duplicate(specs[0], specs[2]) ||
        duplicate(specs[1], specs[2])) {
        wxMessageBox(L"不同操作不能使用相同的快捷键。", L"设置", wxOK | wxICON_WARNING, this);
        return false;
    }

#ifdef __WXMSW__
    HWND hwnd = (HWND)GetHandle();
    if (!hwnd) return false;
    UnregisterHotkeys();
    const int ids[3] = {kHotkeyShowMainId, kHotkeyNewNoteId, kHotkeyNewMarkdownId};
    for (int i = 0; i < 3; ++i) {
        if (!specs[i].enabled) continue;
        UINT modifiers = 0;
        if (specs[i].ctrl) modifiers |= MOD_CONTROL;
        if (specs[i].alt) modifiers |= MOD_ALT;
        if (specs[i].shift) modifiers |= MOD_SHIFT;
        if (!::RegisterHotKey(hwnd, ids[i], modifiers | MOD_NOREPEAT, specs[i].virtualKey)) {
            UnregisterHotkeys();
            RegisterHotkeys();
            wxMessageBox(wxString(L"快捷键「") + labels[i] +
                             L"」已被其他程序占用，请换一个。",
                         L"设置", wxOK | wxICON_WARNING, this);
            return false;
        }
    }
#endif

    hotkeyShowMain_ = specs[0].raw;
    hotkeyNewNote_ = specs[1].raw;
    hotkeyNewMarkdown_ = specs[2].raw;
    if (wxConfigBase* config = wxConfigBase::Get()) {
        config->Write("/Hotkeys/ShowMain", hotkeyShowMain_);
        config->Write("/Hotkeys/NewNote", hotkeyNewNote_);
        config->Write("/Hotkeys/NewMarkdown", hotkeyNewMarkdown_);
        config->Flush();
    }
    return true;
}

void MainFrame::OpenSettings() {
    wxString dataDir = store_ ? FromW(store_->DataDir()) : wxString();
    wxString showMain = hotkeyShowMain_;
    wxString newNote = hotkeyNewNote_;
    wxString newMarkdown = hotkeyNewMarkdown_;
    GitSyncConfig gitConfig = LoadGitSyncConfig();
    for (;;) {
        SettingsDialog dialog(
            this, dataDir, showMain, newNote, newMarkdown, gitConfig,
            [this]() { return !noteWindows_.empty(); });
        const int result = dialog.ShowModal();
        const bool pulled = dialog.DidPull();
        if (pulled) {
            // 拉取会用远程文件覆盖数据目录，关闭设置后按新内容重建列表
            LoadData();
            RebuildTree();
        }
        if (result != wxID_OK) return;

        const wxString requestedDataDir = dialog.DataDirectory();
        if (!requestedDataDir.IsSameAs(dataDir, false)) {
            if (!ChangeDataDirectory(requestedDataDir)) return;
            dataDir = FromW(store_->DataDir());
        }

        showMain = dialog.ShowMainHotkey();
        newNote = dialog.NewNoteHotkey();
        newMarkdown = dialog.NewMarkdownHotkey();
        gitConfig = dialog.GitConfig();
        wxString gitError;
        if (!SaveGitSyncConfig(gitConfig, gitError)) {
            wxMessageBox(gitError, L"Git 配置", wxOK | wxICON_WARNING, this);
        }
        if (ApplyHotkeys(showMain, newNote, newMarkdown)) return;
    }
}

bool MainFrame::ChangeDataDirectory(const wxString& newDirValue) {
    if (!store_) return false;
    wxString target = newDirValue;
    target.Trim(true).Trim(false);
    if (target.empty()) {
        wxMessageBox(L"数据目录不能为空。", L"更改数据目录", wxOK | wxICON_WARNING, this);
        return false;
    }

    std::error_code ec;
    const fs::path oldPath = fs::path(ToW(store_->DataDir())).lexically_normal();
    fs::path targetPath = fs::absolute(fs::path(ToW(target)), ec).lexically_normal();
    if (ec) targetPath = fs::path(ToW(target)).lexically_normal();

    const std::wstring oldLower = LowerText(oldPath.wstring());
    const std::wstring targetLower = LowerText(targetPath.wstring());
    if (oldLower == targetLower) return true;

    auto isSameOrChild = [](const std::wstring& child, const std::wstring& parent) {
        if (child == parent) return true;
        return child.size() > parent.size() && child.compare(0, parent.size(), parent) == 0 &&
               (child[parent.size()] == L'\\' || child[parent.size()] == L'/');
    };
    if (isSameOrChild(targetLower, oldLower) || isSameOrChild(oldLower, targetLower)) {
        wxMessageBox(L"新数据目录不能位于旧数据目录内部，反之亦然。", L"更改数据目录",
                     wxOK | wxICON_WARNING, this);
        return false;
    }

    fs::create_directories(targetPath, ec);
    if (ec || !fs::is_directory(targetPath, ec)) {
        wxMessageBox(wxString(L"无法创建数据目录：\n") + FromW(targetPath.wstring()),
                     L"更改数据目录", wxOK | wxICON_ERROR, this);
        return false;
    }

    auto hasEntries = [](const fs::path& dir) {
        std::error_code entryError;
        if (!fs::is_directory(dir, entryError)) return false;
        fs::directory_iterator iterator(dir, entryError);
        if (entryError) return false;
        return iterator != fs::directory_iterator();
    };

    const bool oldHasData = hasEntries(oldPath);
    const bool targetHasData = hasEntries(targetPath);
    bool copyExisting = false;
    if (targetHasData) {
        int answer = wxMessageBox(
            L"目标目录已有内容。\n\n切换后将直接使用该目录，不会自动合并或覆盖现有便签。\n\n是否继续？",
            L"更改数据目录", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
        if (answer != wxYES) return false;
    } else if (oldHasData) {
        int answer = wxMessageBox(
            L"是否把当前便签复制到新目录？\n\n选择“否”只切换目录，原便签会保留在原位置。",
            L"更改数据目录", wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
        if (answer == wxCANCEL) return false;
        copyExisting = answer == wxYES;
    }

    // 修改数据目录前先关闭打开中的便签，避免仍在写旧路径。
    std::vector<NoteWindow*> windows = noteWindows_;
    for (NoteWindow* window : windows) {
        if (window && !window->Close()) return false;
    }
    if (!noteWindows_.empty()) {
        wxMessageBox(L"仍有便签窗口未关闭，数据目录未修改。", L"更改数据目录",
                     wxOK | wxICON_WARNING, this);
        return false;
    }

    if (copyExisting) {
        try {
            for (const fs::directory_entry& entry : fs::directory_iterator(oldPath)) {
                std::error_code copyError;
                fs::copy(entry.path(), targetPath / entry.path().filename(),
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing,
                         copyError);
                if (copyError) {
                    wxMessageBox(L"复制便签数据失败，旧数据未改动。", L"更改数据目录",
                                 wxOK | wxICON_ERROR, this);
                    return false;
                }
            }
        } catch (const std::exception&) {
            wxMessageBox(L"复制便签数据失败，旧数据未改动。", L"更改数据目录",
                         wxOK | wxICON_ERROR, this);
            return false;
        }
    }

    auto newStore = std::make_shared<NoteStore>();
    std::wstring error;
    if (!newStore->Open(targetPath.wstring(), error)) {
        wxMessageBox(wxString(L"切换数据目录失败：\n") + FromW(error), L"更改数据目录",
                     wxOK | wxICON_ERROR, this);
        LoadData();
        RebuildTree();
        return false;
    }

    store_ = newStore;
    selectedGroupPath_.clear();
    selectedNotePath_.clear();
    expandedGroups_.clear();
    expansionInitialized_ = false;
    LoadData();
    RebuildTree();

    if (wxConfigBase* config = wxConfigBase::Get()) {
        config->Write("/Data/Directory", FromW(store_->DataDir()));
        config->Flush();
    }
    if (copyExisting) {
        wxMessageBox(L"现有便签已复制到新目录，旧目录保留作为备份。", L"更改数据目录",
                     wxOK | wxICON_INFORMATION, this);
    }
    return true;
}

void MainFrame::OnClose(wxCloseEvent& event) {
    if (shuttingDown_) {
        event.Skip();
        return;
    }

    // 关闭主窗口时先隐藏到系统托盘；托盘菜单里的“关闭”才真正退出。
    if (!reallyExit_ && trayAvailable_ && event.CanVeto()) {
        event.Veto();
        HideToTray();
        return;
    }

    shuttingDown_ = true;

    std::vector<NoteWindow*> windows = noteWindows_;
    for (NoteWindow* window : windows) {
        if (window && !window->Close()) {
            shuttingDown_ = false;
            reallyExit_ = false;
            event.Veto();
            if (hiddenToTray_) RestoreFromTray();
            return;
        }
    }

    SaveLayout();
    if (trayIcon_) trayIcon_->RemoveIcon();
    event.Skip();
}

void MainFrame::LoadData() {
    groups_.clear();
    notes_.clear();
    indexesDirty_ = true;
    if (!store_) return;

    std::wstring error;
    if (!store_->ListTree(groups_, notes_, error)) {
        wxMessageBox(wxString(L"读取便签目录失败：\n") + FromW(error), L"PenDown",
                     wxOK | wxICON_ERROR, this);
    }
    RebuildIndexes();
}

void MainFrame::RebuildIndexes() {
    groupIndex_.clear();
    noteIndex_.clear();
    groupChildren_.clear();
    notesByGroup_.clear();
    groupLowerPaths_.clear();
    groupLowerNames_.clear();
    groupLowerParents_.clear();
    groupIndex_.reserve(groups_.size());
    noteIndex_.reserve(notes_.size());
    groupChildren_.reserve(groups_.size());
    notesByGroup_.reserve(groups_.size());
    groupLowerPaths_.reserve(groups_.size());
    groupLowerNames_.reserve(groups_.size());
    groupLowerParents_.reserve(groups_.size());

    for (size_t i = 0; i < groups_.size(); ++i) {
        groupLowerPaths_.push_back(LowerText(groups_[i].rel_path));
        groupLowerNames_.push_back(LowerText(groups_[i].name));
        groupLowerParents_.push_back(LowerText(groups_[i].parent_path));
        groupIndex_[groupLowerPaths_.back()] = i;
        groupChildren_[groupLowerParents_.back()].push_back(i);
    }
    for (size_t i = 0; i < notes_.size(); ++i) {
        noteIndex_[LowerText(notes_[i].rel_path)] = i;
        notesByGroup_[LowerText(notes_[i].group_path)].push_back(i);
    }
    indexesDirty_ = false;
}

void MainFrame::RebuildTree() {
    if (!tree_) return;
    if (indexesDirty_) RebuildIndexes();

    if (!expansionInitialized_) {
        for (const NoteGroupEntry& group : groups_) expandedGroups_.insert(group.rel_path);
        expansionInitialized_ = true;
    }

    std::vector<size_t> roots;
    roots.reserve(groups_.size());
    for (size_t i = 0; i < groups_.size(); ++i) {
        if (groupLowerParents_[i].empty() ||
            groupIndex_.find(groupLowerParents_[i]) == groupIndex_.end()) {
            roots.push_back(i);
        }
    }

    auto sortGroups = [&](std::vector<size_t>& indices) {
        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
            return groupLowerNames_[a] < groupLowerNames_[b];
        });
    };
    sortGroups(roots);

    auto noteCount = [&](const std::wstring& lowerPath) -> size_t {
        const auto found = notesByGroup_.find(lowerPath);
        return found == notesByGroup_.end() ? 0 : found->second.size();
    };

    std::vector<NoteTreeCtrl::Item> items;
    items.reserve(groups_.size() + notes_.size());

    // 根目录下的便签直接显示为顶层节点。
    const auto rootNotes = notesByGroup_.find(L"");
    if (rootNotes != notesByGroup_.end()) {
        for (size_t noteIndex : rootNotes->second) {
            const NoteFileEntry& note = notes_[noteIndex];
            NoteTreeCtrl::Item noteItem;
            noteItem.kind = NoteTreeCtrl::Kind::Note;
            noteItem.path = note.rel_path;
            noteItem.depth = 0;
            noteItem.text = note.title;
            noteItem.markdown = note.format == NoteFormat::Markdown;
            items.push_back(std::move(noteItem));
        }
    }

    std::function<void(size_t, int)> addGroup = [&](size_t groupIndex, int depth) {
        const NoteGroupEntry& group = groups_[groupIndex];
        const std::wstring& groupLower = groupLowerPaths_[groupIndex];

        NoteTreeCtrl::Item item;
        item.kind = NoteTreeCtrl::Kind::Group;
        item.path = group.rel_path;
        item.depth = depth;
        item.text = group.name;
        item.expanded = IsGroupExpanded(group.rel_path);
        item.count = (int)noteCount(groupLower);

        const auto notesFound = notesByGroup_.find(groupLower);
        const auto childrenFound = groupChildren_.find(groupLower);
        const bool hasNotes = notesFound != notesByGroup_.end() && !notesFound->second.empty();
        const bool hasGroups =
            childrenFound != groupChildren_.end() && !childrenFound->second.empty();
        item.hasChildren = hasNotes || hasGroups;
        items.push_back(std::move(item));

        if (!IsGroupExpanded(group.rel_path)) return;

        if (hasNotes) {
            for (size_t noteIndex : notesFound->second) {
                const NoteFileEntry& note = notes_[noteIndex];
                NoteTreeCtrl::Item noteItem;
                noteItem.kind = NoteTreeCtrl::Kind::Note;
                noteItem.path = note.rel_path;
                noteItem.depth = depth + 1;
                noteItem.text = note.title;
                noteItem.markdown = note.format == NoteFormat::Markdown;
                items.push_back(std::move(noteItem));
            }
        }
        if (hasGroups) {
            std::vector<size_t> childIndices = childrenFound->second;
            sortGroups(childIndices);
            for (size_t childIndex : childIndices) addGroup(childIndex, depth + 1);
        }
    };

    for (size_t rootIndex : roots) addGroup(rootIndex, 0);

    tree_->SetItems(std::move(items));

    int noteRow = selectedNotePath_.empty()
                      ? wxNOT_FOUND
                      : tree_->FindItem(NoteTreeCtrl::Kind::Note, selectedNotePath_);
    if (noteRow != wxNOT_FOUND) {
        tree_->SetSelection(noteRow);
        tree_->ScrollToRow((size_t)noteRow);
    } else {
        if (!selectedNotePath_.empty()) {
            if (const NoteFileEntry* note = FindNote(selectedNotePath_)) {
                selectedGroupPath_ = note->group_path;
            }
            selectedNotePath_.clear();
        }
        int groupRow = selectedGroupPath_.empty()
                           ? wxNOT_FOUND
                           : tree_->FindItem(NoteTreeCtrl::Kind::Group, selectedGroupPath_);
        if (groupRow != wxNOT_FOUND) {
            tree_->SetSelection(groupRow);
            tree_->ScrollToRow((size_t)groupRow);
        } else {
            selectedGroupPath_.clear();
            tree_->SetSelection(wxNOT_FOUND);
        }
    }

    UpdateSelectionState();
    UpdateCounts();
}

void MainFrame::UpdateCounts() {
    if (!countLabel_) return;
    const wxString text =
        wxString::Format(L"%d 条 · %d 组", (int)notes_.size(), (int)groups_.size());
    if (countLabel_->GetLabel() == text) return;
    countLabel_->SetLabel(text);
    if (auto* footer = countLabel_->GetContainingSizer()) footer->Layout();
}

void MainFrame::UpdateSelectionState() {
    if (const NoteTreeCtrl::Item* item = tree_ ? tree_->SelectedItem() : nullptr) {
        if (item->kind == NoteTreeCtrl::Kind::Group) {
            selectedGroupPath_ = item->path;
            selectedNotePath_.clear();
            SetBottomStatus(wxString(L"分组：") + FromW(GroupName(item->path)));
        } else {
            selectedGroupPath_.clear();
            selectedNotePath_ = item->path;
            if (const NoteFileEntry* note = FindNote(item->path)) {
                SetBottomStatus(wxString(L"便签：") + FromW(note->title));
            } else {
                SetBottomStatus(L"便签");
            }
        }
    } else {
        selectedGroupPath_.clear();
        selectedNotePath_.clear();
        SetBottomStatus(L"未选分组：新便签将创建在根目录");
    }

    if (deleteButton_) {
        deleteButton_->Enable(!selectedNotePath_.empty() || !selectedGroupPath_.empty());
    }
}

void MainFrame::SetBottomStatus(const wxString& text) {
    if (!bottomStatus_) return;
    if (bottomStatus_->GetLabel() == text) return;
    bottomStatus_->SetLabel(text);
}

void MainFrame::AddGroup() {
    AddGroupUnder(selectedGroupPath_);
}

void MainFrame::AddGroupUnder(const std::wstring& parentPath) {
    NameDialog dialog(this, parentPath.empty() ? L"新建分组" : L"新建子分组", L"分组名称");
    if (dialog.ShowModal() != wxID_OK) return;

    std::wstring created;
    std::wstring error;
    if (!store_ ||
        !store_->CreateGroup(parentPath, ToW(dialog.GetValue()), created, error)) {
        wxMessageBox(wxString(L"新建分组失败：\n") + FromW(error), L"PenDown",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    if (!parentPath.empty()) ExpandGroup(parentPath);
    NoteGroupEntry entry;
    entry.rel_path = created;
    entry.parent_path = parentPath;
    entry.name = fs::path(created).filename().wstring();
    groups_.push_back(std::move(entry));
    indexesDirty_ = true;

    selectedGroupPath_ = created;
    selectedNotePath_.clear();
    RebuildTree();
    tree_->SelectItem(NoteTreeCtrl::Kind::Group, created);
}

void MainFrame::AddNote(NoteFormat format) {
    AddNoteToGroup(ResolveNewNoteGroup(), format);
}

void MainFrame::AddNoteToGroup(const std::wstring& groupPath, NoteFormat format) {
    const std::wstring target = groupPath;
    if (!target.empty()) ExpandGroup(target);
    RebuildTree();

    auto* window = new NoteWindow(
        this, store_, L"", target, format,
        [this](const std::wstring& oldPath, const std::wstring& notePath,
               const std::wstring& group, NoteFormat format) {
            OnNoteSaved(oldPath, notePath, group, format);
        },
        [this](const std::wstring& notePath, const std::wstring& group) {
            OnNoteDeleted(notePath, group);
        },
        [this](NoteWindow* closed) { OnNoteWindowClosed(closed); });
    noteWindows_.push_back(window);
    window->Show();
    window->Raise();
}

void MainFrame::OpenNote(const std::wstring& notePath) {
    if (NoteWindow* existing = FindNoteWindow(notePath)) {
        existing->Raise();
        existing->SetFocus();
        return;
    }

    const NoteFileEntry* note = FindNote(notePath);
    if (!note) return;
    ExpandGroup(note->group_path);

    auto* window = new NoteWindow(
        this, store_, notePath, note->group_path, note->format,
        [this](const std::wstring& oldPath, const std::wstring& path,
               const std::wstring& group, NoteFormat format) {
            OnNoteSaved(oldPath, path, group, format);
        },
        [this](const std::wstring& path, const std::wstring& group) {
            OnNoteDeleted(path, group);
        },
        [this](NoteWindow* closed) { OnNoteWindowClosed(closed); });
    noteWindows_.push_back(window);
    window->Show();
    window->Raise();
}

void MainFrame::RenameSelected() {
    if (!selectedGroupPath_.empty()) {
        RenameGroup(selectedGroupPath_);
    } else if (!selectedNotePath_.empty()) {
        OpenNote(selectedNotePath_);
    }
}

void MainFrame::RenameGroup(const std::wstring& groupPath) {
    // groupPath 可能引用 selectedGroupPath_，先复制，避免后面赋值时被改掉。
    const std::wstring targetPath = groupPath;
    const NoteGroupEntry* target = nullptr;
    for (const NoteGroupEntry& group : groups_) {
        if (group.rel_path == targetPath) {
            target = &group;
            break;
        }
    }
    if (!target) return;
    std::wstring oldName = target->name;

    std::set<std::wstring> subtree;
    CollectGroupTree(targetPath, subtree);

    bool hasOpenWindow = false;
    for (NoteWindow* window : noteWindows_) {
        if (window && IsPathInGroups(window->GroupPath(), subtree)) {
            hasOpenWindow = true;
            break;
        }
    }
    if (hasOpenWindow) {
        int answer = wxMessageBox(
            L"这个分组里有打开的便签。\n重命名前会先关闭这些窗口，是否继续？",
            L"重命名分组", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
        if (answer != wxYES) return;
        std::vector<NoteWindow*> windows = noteWindows_;
        for (NoteWindow* window : windows) {
            if (window && IsPathInGroups(window->GroupPath(), subtree) && !window->Close()) {
                return;
            }
        }
    }

    NameDialog dialog(this, L"重命名分组", L"分组名称", FromW(oldName));
    if (dialog.ShowModal() != wxID_OK) return;

    std::wstring renamed;
    std::wstring error;
    if (!store_ || !store_->RenameGroup(targetPath, ToW(dialog.GetValue()), renamed, error)) {
        wxMessageBox(wxString(L"重命名分组失败：\n") + FromW(error), L"PenDown",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    selectedGroupPath_ = renamed;
    selectedNotePath_.clear();
    ReplaceGroupPrefixInModel(targetPath, renamed);
    std::set<std::wstring> expanded;
    for (const std::wstring& item : expandedGroups_) {
        expanded.insert(ReplacePathPrefix(item, targetPath, renamed));
    }
    expandedGroups_.swap(expanded);
    RebuildTree();
    tree_->SelectItem(NoteTreeCtrl::Kind::Group, renamed);
}

void MainFrame::MoveToGroup(NoteTreeCtrl::Kind kind, const std::wstring& path) {
    if (!store_) return;

    const NoteFileEntry* note = nullptr;
    std::wstring currentGroup;
    if (kind == NoteTreeCtrl::Kind::Group) {
        currentGroup = GroupParent(path);
    } else {
        note = FindNote(path);
        if (!note) return;
        currentGroup = note->group_path;
    }

    std::vector<std::wstring> targets;
    wxArrayString labels;
    if (!currentGroup.empty()) {
        // 不选上级：分组放到根；便签放到根目录。
        targets.push_back(std::wstring());
        labels.Add(kind == NoteTreeCtrl::Kind::Group ? L"不选上级（根分组）"
                                                     : L"不选分组（根目录）");
    }
    for (const NoteGroupEntry& group : groups_) {
        if (kind == NoteTreeCtrl::Kind::Group) {
            if (HasPathPrefix(group.rel_path, path)) continue;  // 自己和自己的子分组
            if (LowerText(group.rel_path) == LowerText(currentGroup)) continue;
        } else if (LowerText(group.rel_path) == LowerText(currentGroup)) {
            continue;
        }
        targets.push_back(group.rel_path);
        std::wstring label(GroupDepth(group.rel_path) * 2, L' ');
        label += group.name;
        labels.Add(FromW(label));
    }
    if (targets.empty()) {
        wxMessageBox(L"没有可用的目标分组。", L"移动到分组", wxOK | wxICON_INFORMATION, this);
        return;
    }

    wxSingleChoiceDialog dialog(this, L"选择目标分组：", L"移动到分组", labels);
    if (dialog.ShowModal() != wxID_OK) return;
    const int selection = dialog.GetSelection();
    if (selection < 0 || selection >= (int)targets.size()) return;
    const std::wstring& target = targets[(size_t)selection];

    std::wstring error;
    std::wstring newRel;
    if (kind == NoteTreeCtrl::Kind::Group) {
        if (!store_->MoveGroup(path, target, newRel, error)) {
            wxMessageBox(wxString(L"移动分组失败：\n") + FromW(error), L"PenDown",
                         wxOK | wxICON_ERROR, this);
            return;
        }

        // 打开的便签跟着分组一起换到新路径
        for (NoteWindow* window : noteWindows_) {
            if (!window || !HasPathPrefix(window->GroupPath(), path)) continue;
            const std::wstring newGroup =
                ReplacePathPrefix(window->GroupPath(), path, newRel);
            const std::wstring newNote =
                ReplacePathPrefix(window->NotePath(), path, newRel);
            window->UpdateLocation(newNote, newGroup);
        }

        std::set<std::wstring> expanded;
        for (const std::wstring& item : expandedGroups_) {
            expanded.insert(ReplacePathPrefix(item, path, newRel));
        }
        expandedGroups_.swap(expanded);

        selectedGroupPath_ = ReplacePathPrefix(selectedGroupPath_, path, newRel);
        selectedNotePath_ = ReplacePathPrefix(selectedNotePath_, path, newRel);
        ReplaceGroupPrefixInModel(path, newRel);
        ExpandGroup(target);
        ExpandGroup(newRel);
        RebuildTree();
        if (!selectedNotePath_.empty()) {
            tree_->SelectItem(NoteTreeCtrl::Kind::Note, selectedNotePath_);
        } else if (!selectedGroupPath_.empty()) {
            tree_->SelectItem(NoteTreeCtrl::Kind::Group, selectedGroupPath_);
        }
    } else {
        if (!store_->MoveNote(path, target, newRel, error)) {
            wxMessageBox(wxString(L"移动便签失败：\n") + FromW(error), L"PenDown",
                         wxOK | wxICON_ERROR, this);
            return;
        }
        if (NoteWindow* window = FindNoteWindow(path)) {
            window->UpdateLocation(newRel, target);
        }
        if (indexesDirty_) RebuildIndexes();
        const auto found = noteIndex_.find(LowerText(path));
        if (found != noteIndex_.end() && found->second < notes_.size()) {
            notes_[found->second].rel_path = newRel;
            notes_[found->second].group_path = target;
            indexesDirty_ = true;
        } else {
            LoadData();
        }
        selectedGroupPath_.clear();
        selectedNotePath_ = newRel;
        ExpandGroup(target);
        RebuildTree();
        tree_->SelectItem(NoteTreeCtrl::Kind::Note, newRel);
    }
}

void MainFrame::DeleteSelected() {
    if (!selectedGroupPath_.empty()) {
        DeleteGroup(selectedGroupPath_);
    } else if (!selectedNotePath_.empty()) {
        DeleteNote(selectedNotePath_);
    }
}

void MainFrame::DeleteGroup(const std::wstring& groupPath) {
    // groupPath 可能是 selectedGroupPath_ 的引用，后面会改 selectedGroupPath_，
    // 必须先复制，否则删除目标会变成父分组。
    const std::wstring targetPath = groupPath;
    std::set<std::wstring> subtree;
    CollectGroupTree(targetPath, subtree);
    int answer = wxMessageBox(
        wxString(L"删除分组「") + FromW(GroupName(targetPath)) +
            L"」及其中所有子分组和便签？\n删除后无法恢复。",
        L"删除分组", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
    if (answer != wxYES) return;

    CloseNoteWindowsInGroups(subtree);
    std::wstring error;
    if (!store_ || !store_->DeleteGroup(targetPath, error)) {
        wxMessageBox(wxString(L"删除分组失败：\n") + FromW(error), L"PenDown",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    for (const std::wstring& path : subtree) expandedGroups_.erase(path);
    std::wstring parent = GroupParent(targetPath);
    selectedGroupPath_ = parent;
    selectedNotePath_.clear();
    RemoveGroupSubtreeFromModel(targetPath);
    RebuildTree();
    if (!selectedGroupPath_.empty()) {
        tree_->SelectItem(NoteTreeCtrl::Kind::Group, selectedGroupPath_);
    }
}

void MainFrame::DeleteNote(const std::wstring& notePath) {
    // notePath 可能是 selectedNotePath_ 的引用，后面会 clear，必须先复制。
    const std::wstring targetPath = notePath;
    const NoteFileEntry* note = FindNote(targetPath);
    if (!note) return;
    std::wstring title = note->title;
    std::wstring groupPath = note->group_path;

    int answer = wxMessageBox(wxString(L"删除便签「") + FromW(title) + L"」？\n删除后无法恢复。",
                              L"删除便签", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
    if (answer != wxYES) return;

    if (NoteWindow* window = FindNoteWindow(targetPath)) {
        noteWindows_.erase(std::remove(noteWindows_.begin(), noteWindows_.end(), window),
                           noteWindows_.end());
        window->Destroy();
    }

    std::wstring error;
    if (!store_ || !store_->DeleteNote(targetPath, error)) {
        wxMessageBox(wxString(L"删除便签失败：\n") + FromW(error), L"PenDown",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    selectedGroupPath_ = groupPath;
    selectedNotePath_.clear();
    OnNoteDeleted(targetPath, groupPath);
    tree_->SelectItem(NoteTreeCtrl::Kind::Group, groupPath);
}

void MainFrame::OnToggleGroup(const std::wstring& groupPath) {
    if (expandedGroups_.count(groupPath)) {
        expandedGroups_.erase(groupPath);
    } else {
        expandedGroups_.insert(groupPath);
    }
    RebuildTree();
}

void MainFrame::OnTreeContext(bool hasItem, NoteTreeCtrl::Kind kind,
                              const std::wstring& path, const wxPoint& screen) {
    wxMenu menu;
    if (!hasItem) {
        menu.Append(ID_CTX_NEW_GROUP, L"新建分组");
        menu.Append(ID_CTX_NEW_ROOT_NOTE, L"新建普通便签（根目录）");
        menu.Append(ID_CTX_NEW_ROOT_MARKDOWN_NOTE, L"新建 Markdown 便签（根目录）");
    } else if (kind == NoteTreeCtrl::Kind::Group) {
        menu.Append(ID_CTX_NEW_SUBGROUP, L"新建子分组");
        menu.Append(ID_CTX_NEW_NOTE, L"在此分组新建普通便签");
        menu.Append(ID_CTX_NEW_MARKDOWN_NOTE, L"在此分组新建 Markdown 便签");
        menu.AppendSeparator();
        menu.Append(ID_CTX_RENAME, L"重命名分组");
        menu.Append(ID_CTX_MOVE, L"移动到分组...");
        menu.Append(ID_CTX_DELETE, L"删除分组");
    } else {
        menu.Append(ID_CTX_OPEN, L"打开便签");
        menu.Append(ID_CTX_MOVE, L"移动到分组...");
        menu.AppendSeparator();
        menu.Append(ID_CTX_DELETE, L"删除便签");
    }

    int command = GetPopupMenuSelectionFromUser(menu, tree_->ScreenToClient(screen));
    switch (command) {
        case ID_CTX_NEW_GROUP:
            AddGroupUnder(L"");
            break;
        case ID_CTX_NEW_ROOT_NOTE:
            AddNoteToGroup(L"", NoteFormat::RichText);
            break;
        case ID_CTX_NEW_ROOT_MARKDOWN_NOTE:
            AddNoteToGroup(L"", NoteFormat::Markdown);
            break;
        case ID_CTX_NEW_SUBGROUP:
            AddGroupUnder(path);
            break;
        case ID_CTX_NEW_NOTE:
            AddNoteToGroup(path, NoteFormat::RichText);
            break;
        case ID_CTX_NEW_MARKDOWN_NOTE:
            AddNoteToGroup(path, NoteFormat::Markdown);
            break;
        case ID_CTX_OPEN:
            OpenNote(path);
            break;
        case ID_CTX_RENAME:
            RenameGroup(path);
            break;
        case ID_CTX_MOVE:
            MoveToGroup(kind, path);
            break;
        case ID_CTX_DELETE:
            if (kind == NoteTreeCtrl::Kind::Group) {
                DeleteGroup(path);
            } else {
                DeleteNote(path);
            }
            break;
        default:
            break;
    }
}

void MainFrame::OnTreeSelection() {
    UpdateSelectionState();
}

void MainFrame::OnNoteSaved(const std::wstring& oldPath, const std::wstring& notePath,
                            const std::wstring& groupPath, NoteFormat format) {
    if (notePath.empty()) {
        LoadData();
        RebuildTree();
        return;
    }

    if (indexesDirty_) RebuildIndexes();
    const std::wstring resolvedGroup = groupPath;
    if (!resolvedGroup.empty() &&
        groupIndex_.find(LowerText(resolvedGroup)) == groupIndex_.end()) {
        // 分组不在当前模型中，走一次完整刷新，避免把便签挂到错误位置
        LoadData();
        ExpandGroup(resolvedGroup);
        selectedNotePath_ = notePath;
        selectedGroupPath_.clear();
        RebuildTree();
        return;
    }

    const std::wstring newLower = LowerText(notePath);
    const std::wstring oldLower = LowerText(oldPath);
    std::vector<size_t> removeRows;
    const auto addRow = [&](const std::wstring& key) {
        const auto found = noteIndex_.find(key);
        if (found != noteIndex_.end() && found->second < notes_.size()) {
            removeRows.push_back(found->second);
        }
    };
    addRow(newLower);
    if (!oldLower.empty() && oldLower != newLower) addRow(oldLower);
    std::sort(removeRows.begin(), removeRows.end(), std::greater<size_t>());
    removeRows.erase(std::unique(removeRows.begin(), removeRows.end()), removeRows.end());
    for (size_t row : removeRows) notes_.erase(notes_.begin() + (long)row);

    NoteFileEntry entry;
    entry.rel_path = notePath;
    entry.group_path = resolvedGroup;
    entry.title = fs::path(notePath).stem().wstring();
    entry.format = format;
    if (entry.title.empty()) entry.title = L"无标题便签";
    notes_.insert(notes_.begin(), std::move(entry));

    indexesDirty_ = true;
    ExpandGroup(resolvedGroup);
    selectedNotePath_ = notePath;
    selectedGroupPath_.clear();
    RebuildTree();
}

void MainFrame::OnNoteDeleted(const std::wstring& notePath, const std::wstring& groupPath) {
    (void)notePath;
    selectedGroupPath_ = groupPath;
    selectedNotePath_.clear();

    const std::wstring removed = LowerText(notePath);
    if (indexesDirty_) RebuildIndexes();
    const auto found = noteIndex_.find(removed);
    if (found != noteIndex_.end() && found->second < notes_.size()) {
        notes_.erase(notes_.begin() + (long)found->second);
        indexesDirty_ = true;
    } else {
        LoadData();
    }
    RebuildTree();
}

void MainFrame::OnNoteWindowClosed(NoteWindow* window) {
    noteWindows_.erase(std::remove(noteWindows_.begin(), noteWindows_.end(), window),
                       noteWindows_.end());
    // 便签窗口带有 DWM 阴影，关闭后重绘主窗口，避免残影留在白色背景上。
    Refresh();
    Update();
}

std::wstring MainFrame::ResolveNewNoteGroup() const {
    // 只有明确选中分组时才使用它；选中便签或空白处都创建在根目录。
    return selectedGroupPath_;
}

std::wstring MainFrame::GroupParent(const std::wstring& groupPath) const {
    const auto found = groupIndex_.find(LowerText(groupPath));
    if (found == groupIndex_.end() || found->second >= groups_.size()) return std::wstring();
    return groups_[found->second].parent_path;
}

std::wstring MainFrame::GroupName(const std::wstring& groupPath) const {
    const auto found = groupIndex_.find(LowerText(groupPath));
    if (found == groupIndex_.end() || found->second >= groups_.size()) return groupPath;
    return groups_[found->second].name;
}

const NoteFileEntry* MainFrame::FindNote(const std::wstring& notePath) const {
    const auto found = noteIndex_.find(LowerText(notePath));
    if (found == noteIndex_.end() || found->second >= notes_.size()) return nullptr;
    return &notes_[found->second];
}

NoteWindow* MainFrame::FindNoteWindow(const std::wstring& notePath) const {
    for (NoteWindow* window : noteWindows_) {
        if (window && window->NotePath() == notePath) return window;
    }
    return nullptr;
}

void MainFrame::RemoveGroupSubtreeFromModel(const std::wstring& groupPath) {
    groups_.erase(std::remove_if(groups_.begin(), groups_.end(),
                                 [&](const NoteGroupEntry& group) {
                                     return HasPathPrefix(group.rel_path, groupPath);
                                 }),
                  groups_.end());
    notes_.erase(std::remove_if(notes_.begin(), notes_.end(),
                                [&](const NoteFileEntry& note) {
                                    return HasPathPrefix(note.group_path, groupPath);
                                }),
                 notes_.end());
    indexesDirty_ = true;
    RebuildIndexes();
}

void MainFrame::ReplaceGroupPrefixInModel(const std::wstring& oldPath,
                                          const std::wstring& newPath) {
    if (oldPath == newPath) return;
    const std::wstring newParent = fs::path(newPath).parent_path().wstring();
    for (NoteGroupEntry& group : groups_) {
        const bool movedRoot = LowerText(group.rel_path) == LowerText(oldPath);
        if (HasPathPrefix(group.rel_path, oldPath)) {
            group.rel_path = ReplacePathPrefix(group.rel_path, oldPath, newPath);
            if (movedRoot) {
                group.name = fs::path(newPath).filename().wstring();
                group.parent_path = newParent;
            }
        }
        if (HasPathPrefix(group.parent_path, oldPath)) {
            group.parent_path = ReplacePathPrefix(group.parent_path, oldPath, newPath);
        }
    }
    for (NoteFileEntry& note : notes_) {
        if (HasPathPrefix(note.group_path, oldPath)) {
            note.group_path = ReplacePathPrefix(note.group_path, oldPath, newPath);
        }
        if (HasPathPrefix(note.rel_path, oldPath)) {
            note.rel_path = ReplacePathPrefix(note.rel_path, oldPath, newPath);
        }
    }
    indexesDirty_ = true;
    RebuildIndexes();
}

void MainFrame::ExpandGroup(const std::wstring& groupPath) {
    std::wstring current = groupPath;
    std::set<std::wstring> seen;
    while (!current.empty() && !seen.count(current)) {
        seen.insert(current);
        expandedGroups_.insert(current);
        current = GroupParent(current);
    }
}

bool MainFrame::IsGroupExpanded(const std::wstring& groupPath) const {
    return expandedGroups_.count(groupPath) != 0;
}

void MainFrame::CollectGroupTree(const std::wstring& groupPath,
                                 std::set<std::wstring>& paths) const {
    if (!paths.insert(groupPath).second) return;
    const auto found = groupChildren_.find(LowerText(groupPath));
    if (found == groupChildren_.end()) return;
    for (size_t childIndex : found->second) {
        if (childIndex < groups_.size()) {
            CollectGroupTree(groups_[childIndex].rel_path, paths);
        }
    }
}

void MainFrame::CloseNoteWindowsInGroups(const std::set<std::wstring>& groupPaths) {
    std::vector<NoteWindow*> victims;
    for (NoteWindow* window : noteWindows_) {
        if (window && IsPathInGroups(window->GroupPath(), groupPaths)) {
            victims.push_back(window);
        }
    }
    for (NoteWindow* window : victims) {
        noteWindows_.erase(std::remove(noteWindows_.begin(), noteWindows_.end(), window),
                           noteWindows_.end());
        window->Destroy();
    }
}
