#include "ui/note_window.h"

#include "core/text.h"
#include "resource.h"
#include "ui/app_font.h"
#include "ui/color_popup.h"
#include "ui/find_replace_dialog.h"
#include "ui/icon_button.h"
#include "ui/note_editor.h"
#include "ui/style_popup.h"
#include "ui/theme.h"
#include "ui/ui_ids.h"
#include "ui/window_effects.h"

#include <wx/button.h>
#include <wx/datetime.h>
#include <wx/display.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif

#include <algorithm>
#include <cstring>

namespace {

constexpr int kStatusTimerId = 0x7A01;
constexpr int kAutosaveTimerId = 0x7A02;
constexpr int kHintTimerId = 0x7A03;

wxPanel* MakeHorizontalLine(wxWindow* parent) {
    auto* line = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxBORDER_NONE);
    const int height = std::max(1, parent->FromDIP(1));
    line->SetMinSize(wxSize(-1, height));
    line->SetMaxSize(wxSize(-1, height));
    line->SetBackgroundColour(Theme::Border());
    return line;
}

wxColour ColourFromHex(const std::string& value, const wxColour& fallback) {
    if (value.size() == 7 && value[0] == '#') {
        auto nibble = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            return -1;
        };
        const int r = nibble(value[1]) * 16 + nibble(value[2]);
        const int g = nibble(value[3]) * 16 + nibble(value[4]);
        const int b = nibble(value[5]) * 16 + nibble(value[6]);
        if (r >= 0 && g >= 0 && b >= 0) {
            return wxColour((unsigned char)r, (unsigned char)g,
                            (unsigned char)b);
        }
    }
    return fallback;
}

std::wstring TitleFromUtf8(const std::string& text) {
    return NoteStore::DeriveTitle(ToW(wxString::FromUTF8(text.c_str(), text.size())));
}

}  // namespace

NoteWindow::NoteWindow(wxWindow* parent, std::shared_ptr<NoteStore> store,
                       const std::wstring& notePath, const std::wstring& groupPath,
                       SavedHandler onSaved, DeletedHandler onDeleted,
                       ClosedHandler onClosed)
    // 便签窗口不挂在主窗口下面：主窗口最小化或隐藏到托盘时，便签仍保持独立。
    : wxFrame(nullptr, wxID_ANY, L"PenDown", wxDefaultPosition, wxDefaultSize,
              wxBORDER_NONE | wxRESIZE_BORDER | wxCLIP_CHILDREN),
      store_(std::move(store)),
      notePath_(notePath),
      groupPath_(groupPath),
      onSaved_(std::move(onSaved)),
      onDeleted_(std::move(onDeleted)),
      onClosed_(std::move(onClosed)),
      lastColour_(Theme::Text()),
      statusTimer_(this, kStatusTimerId),
      autosaveTimer_(this, kAutosaveTimerId),
      hintTimer_(this, kHintTimerId) {
    SetBackgroundColour(Theme::Background());
    SetSize(FromDIP(wxSize(620, 700)));
    SetMinSize(FromDIP(wxSize(420, 360)));

    BuildUi();
    BindEvents();
    LoadNote();
    Layout();
    PrepareFramelessWindow(this);
    UpdateStatus();
    UpdateFrameTitle();

    int displayIndex = wxNOT_FOUND;
    if (parent) displayIndex = wxDisplay::GetFromWindow(parent);
    if (displayIndex == wxNOT_FOUND) displayIndex = 0;
    const wxRect workArea =
        wxDisplay(static_cast<unsigned>(displayIndex)).GetClientArea();
    wxSize windowSize = GetSize();
    const wxSize clamped(std::min(windowSize.x, workArea.width),
                         std::min(windowSize.y, workArea.height));
    if (clamped != windowSize) {
        SetSize(clamped);
        windowSize = clamped;
    }
    SetPosition(wxPoint(workArea.x + (workArea.width - windowSize.x) / 2,
                        workArea.y + (workArea.height - windowSize.y) / 2));

    editor_->SetFocus();

#ifdef __WXMSW__
    HICON appIcon = (HICON)::LoadImageW(::GetModuleHandleW(nullptr),
                                        MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 0, 0,
                                        LR_DEFAULTSIZE);
    if (appIcon) {
        wxIcon icon;
        if (icon.CreateFromHICON(appIcon)) {
            SetIcon(icon);
        } else {
            ::DestroyIcon(appIcon);
        }
    }
#endif
}

NoteWindow::~NoteWindow() {
    if (colorPopup_) colorPopup_->StopPending();
    statusTimer_.Stop();
    autosaveTimer_.Stop();
    hintTimer_.Stop();
}

#ifdef __WXMSW__
WXLRESULT NoteWindow::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) {
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

void NoteWindow::UpdateLocation(const std::wstring& notePath,
                                const std::wstring& groupPath) {
    notePath_ = notePath;
    groupPath_ = groupPath;
}

void NoteWindow::BuildUi() {
    auto* borderPanel = new WindowFramePanel(this);
    auto* content = new wxPanel(borderPanel);
    content->SetBackgroundColour(Theme::Background());
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* titleBar = new wxPanel(content);
    titleBar->SetBackgroundColour(Theme::Surface());
    auto* titleOuter = new wxBoxSizer(wxVERTICAL);
    auto* bar = new wxBoxSizer(wxHORIZONTAL);

    wxBitmap formatBitmap =
        IconLibrary::Get(AppIcon::Note, FromDIP(18), Theme::TextMuted());
    wxStaticBitmap* formatIcon = nullptr;
    if (formatBitmap.IsOk()) {
        formatIcon = new wxStaticBitmap(titleBar, wxID_ANY, formatBitmap);
        bar->Add(formatIcon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    }
    bar->AddSpacer(FromDIP(8));

    const wxSize buttonSize = FromDIP(wxSize(26, 22));

    colorButton_ = new IconButton(titleBar, ID_NOTE_COLOR, AppIcon::Palette,
                                  L"文字颜色 / 背景色", IconButton::Kind::Normal,
                                  buttonSize, 12);
    colorButton_->SetEventTarget(this);
    bar->Add(colorButton_, 0, wxRIGHT, FromDIP(1));

    styleButton_ = new IconButton(titleBar, ID_NOTE_STYLES, AppIcon::Styles,
                                  L"样式库：字体、字号、加粗、斜体等",
                                  IconButton::Kind::Normal, buttonSize, 12);
    styleButton_->SetEventTarget(this);
    bar->Add(styleButton_, 0);

    // 清除样式和格式刷是高频的一次性动作，放到标题栏比藏在样式库里更顺手。
    clearFormatButton_ = new IconButton(
        titleBar, ID_NOTE_CLEAR_FORMAT, AppIcon::Eraser,
        L"清除样式：恢复默认字体、字号、颜色和链接",
        IconButton::Kind::Normal, buttonSize, 12);
    clearFormatButton_->SetEventTarget(this);
    bar->Add(clearFormatButton_, 0, wxLEFT, FromDIP(1));

    formatPainterButton_ = new IconButton(
        titleBar, ID_NOTE_FORMAT_PAINTER, AppIcon::FormatPainter,
        L"格式刷：先复制当前格式，再选中目标文字",
        IconButton::Kind::Normal, buttonSize, 12);
    formatPainterButton_->SetEventTarget(this);
    bar->Add(formatPainterButton_, 0, wxLEFT, FromDIP(1));

    bar->AddStretchSpacer(1);

    // 保存和删除都是整篇便签级别的操作，与窗口按钮一起放在右侧。
    auto* save = new IconButton(titleBar, ID_NOTE_SAVE, AppIcon::Save,
                                L"保存便签 (Ctrl+S)", IconButton::Kind::Accent,
                                buttonSize, 12);
    save->SetEventTarget(this);
    bar->Add(save, 0, wxRIGHT, FromDIP(1));

    auto* remove = new IconButton(titleBar, ID_NOTE_DELETE, AppIcon::Trash,
                                  L"删除便签", IconButton::Kind::Danger, buttonSize,
                                  12);
    remove->SetEventTarget(this);
    bar->Add(remove, 0);

    auto* minimize = new IconButton(titleBar, ID_WINDOW_MINIMIZE, AppIcon::Minimize,
                                    L"最小化", IconButton::Kind::Normal, buttonSize,
                                    12);
    auto* maximize = new IconButton(titleBar, ID_WINDOW_MAXIMIZE, AppIcon::Maximize,
                                    L"最大化 / 还原", IconButton::Kind::Normal,
                                    buttonSize, 12);
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
    if (formatIcon) {
        formatIcon->Bind(wxEVT_LEFT_DOWN, beginDrag);
        formatIcon->Bind(wxEVT_LEFT_DCLICK, toggleMax);
    }
#endif

    auto* editorHost = new wxPanel(content);
    editorHost->SetBackgroundColour(Theme::Surface());
    auto* editorSizer = new wxBoxSizer(wxVERTICAL);
    editorSizer->Add(MakeHorizontalLine(editorHost), 0, wxEXPAND);

    editor_ = new NoteEditor(editorHost);
    editorSizer->Add(editor_, 1, wxEXPAND);

    editorSizer->Add(MakeHorizontalLine(editorHost), 0, wxEXPAND);
    editorHost->SetSizer(editorSizer);
    root->Add(editorHost, 1, wxEXPAND | wxTOP, FromDIP(4));

    status_ = new wxStaticText(content, wxID_ANY, wxEmptyString);
    status_->SetForegroundColour(Theme::TextFaint());
    status_->SetFont(AppFont::Make(std::max(8, AppFont::BasePointSize() - 2)));
    // 和主窗口底部状态栏使用同一套间距。
    root->AddSpacer(FromDIP(2));
    root->Add(status_, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(5));
    root->AddSpacer(FromDIP(3));

    content->SetSizer(root);

    auto* borderSizer = new wxBoxSizer(wxVERTICAL);
    borderSizer->Add(content, 1, wxEXPAND | wxALL, borderPanel->ContentInset());
    borderPanel->SetSizer(borderSizer);

    auto* frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(borderPanel, 1, wxEXPAND);
    SetSizer(frameSizer);
}

void NoteWindow::BindEvents() {
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SaveNote(); }, ID_NOTE_SAVE);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DeleteNote(); }, ID_NOTE_DELETE);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Iconize(); }, ID_WINDOW_MINIMIZE);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Maximize(!IsMaximized()); },
         ID_WINDOW_MAXIMIZE);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); }, ID_WINDOW_CLOSE);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ToggleColorPopup(); },
         ID_NOTE_COLOR);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ToggleStylePopup(); },
         ID_NOTE_STYLES);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ClearFormatting(); },
         ID_NOTE_CLEAR_FORMAT);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ToggleFormatPainter(); },
         ID_NOTE_FORMAT_PAINTER);
    Bind(wxEVT_TIMER, [this](wxTimerEvent& event) {
        if (event.GetTimer().GetId() == autosaveTimer_.GetId()) {
            SaveAutosave();
        } else if (event.GetTimer().GetId() == hintTimer_.GetId()) {
            hintText_.clear();
            UpdateStatus();
        } else {
            UpdateStatus();
        }
    });

    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        ApplyRoundedWindowRegion(this, 10);
        event.Skip();
    });
    Bind(wxEVT_CLOSE_WINDOW, &NoteWindow::OnClose, this);
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.ControlDown() && event.GetKeyCode() == 'S') {
            SaveNote();
            return;
        }
        if (event.ControlDown() &&
            (event.GetKeyCode() == 'F' || event.GetKeyCode() == 'H')) {
            ShowFindDialog();
            return;
        }
        event.Skip();
    });

    editor_->SetChangeHandler([this] {
        MarkDirty();
        UpdateStatus();
    });
    editor_->SetPainterAppliedHandler([this] {
        ShowHint(L"格式刷：已应用格式");
        UpdateStatus();
        UpdateFormatPainterState();
    });
    // 键盘快捷键也可能取消格式刷，标题栏按钮必须同步。
    editor_->SetPainterArmedHandler([this](bool) {
        UpdateFormatPainterState();
    });

    editor_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        popupSelectionStart_ = -1;
        popupSelectionEnd_ = -1;
        event.Skip();
    });
}

void NoteWindow::LoadNote() {
    loading_ = true;
    loadFailed_ = false;
    std::string text;
    StyleModel styles = editor_->Styles();
    styles.ClearRuns();
    std::wstring warning;

    if (!notePath_.empty() && store_) {
        std::wstring error;
        if (!store_->LoadNoteDocument(notePath_, text, styles, warning, error)) {
            loadFailed_ = true;
            wxMessageBox(wxString(L"读取便签失败：\n") + FromW(error), L"PenDown",
                         wxOK | wxICON_ERROR, this);
        }
    }

    editor_->SetDocument(text, styles);
    title_ = DeriveTitle();
    dirty_ = false;
    loading_ = false;
    popupSelectionStart_ = -1;
    popupSelectionEnd_ = -1;
    editor_->SetInsertionPoint(0);
    editor_->MarkSaved();
    UpdateStatus();
    UpdateFrameTitle();

    if (!warning.empty()) {
        wxMessageBox(warning, L"PenDown", wxOK | wxICON_WARNING, this);
    }

    if (!notePath_.empty() && store_ && store_->HasNoteAutosave(notePath_) &&
        !loadFailed_) {
        const int answer = wxMessageBox(
            L"发现上次退出前自动保存的内容，是否恢复？", L"PenDown",
            wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
        if (answer == wxYES) {
            std::string autosaveText;
            StyleModel autosaveStyles = editor_->Styles();
            autosaveStyles.ClearRuns();
            std::wstring autosaveWarning;
            if (store_->LoadNoteAutosave(notePath_, autosaveText, autosaveStyles,
                                         autosaveWarning)) {
                editor_->SetDocument(autosaveText, autosaveStyles);
                title_ = DeriveTitle();
                dirty_ = true;
                MarkDirty();
                UpdateStatus();
                UpdateFrameTitle();
            }
        } else {
            store_->RemoveNoteAutosave(notePath_);
        }
    }
}

bool NoteWindow::SaveNote() {
    if (!store_ || !editor_) return false;
    if (loadFailed_) {
        wxMessageBox(L"便签读取失败，为避免覆盖原文件，本次不会保存。\n"
                     L"请先备份原文件后再处理。",
                     L"PenDown", wxOK | wxICON_ERROR, this);
        return false;
    }

    editor_->CompactStyles();
    const std::string text = editor_->GetTextUtf8();
    const std::wstring title = TitleFromUtf8(text);
    const std::wstring oldPath = notePath_;
    std::wstring savedPath;
    std::wstring error;
    if (!store_->SaveNote(notePath_, groupPath_, text, title, editor_->Styles(),
                          savedPath, error)) {
        wxMessageBox(wxString(L"保存便签失败：\n") + FromW(error), L"PenDown",
                     wxOK | wxICON_ERROR, this);
        return false;
    }

    notePath_ = savedPath;
    title_ = title;
    dirty_ = false;
    autosaveLabel_.clear();
    editor_->MarkSaved();
    RemoveAutosave();
    if (!oldPath.empty() && oldPath != notePath_ && store_) {
        store_->RemoveNoteAutosave(oldPath);
    }
    UpdateStatus();
    UpdateFrameTitle();
    if (onSaved_) onSaved_(oldPath, notePath_, groupPath_);
    return true;
}

void NoteWindow::SaveAutosave() {
    if (!dirty_ || notePath_.empty() || !store_ || autosaving_) return;
    autosaving_ = true;
    const std::string text = editor_->GetTextUtf8();
    std::wstring error;
    if (store_->SaveNoteAutosave(notePath_, text, editor_->Styles(), error)) {
        autosaveLabel_ = wxDateTime::Now().Format(L"自动保存 %H:%M");
        UpdateStatus();
    }
    autosaving_ = false;
}

void NoteWindow::RemoveAutosave() {
    if (store_ && !notePath_.empty()) store_->RemoveNoteAutosave(notePath_);
}

void NoteWindow::ScheduleAutosave() {
    if (!dirty_ || notePath_.empty() || autosaving_) return;
    // 大文件写盘更耗时，降低自动保存频率，避免打断输入。
    const long length = editor_ ? editor_->GetTextLength() : 0;
    const int delay = length > 10 * 1024 * 1024 ? 20000 : 5000;
    autosaveTimer_.StartOnce(delay);
}

void NoteWindow::DeleteNote() {
    if (notePath_.empty()) {
        const int answer = wxMessageBox(L"确定删除当前便签？\n删除后无法恢复。",
                                        L"删除便签",
                                        wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION,
                                        this);
        if (answer != wxYES) return;
        dirty_ = false;
        editor_->MarkSaved();
        RemoveAutosave();
        Close();
        return;
    }

    wxString title = FromW(title_.empty() ? L"无标题便签" : title_);
    const int answer = wxMessageBox(wxString(L"删除便签「") + title +
                                        L"」？\n删除后无法恢复。",
                                    L"删除便签",
                                    wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
    if (answer != wxYES) return;

    std::wstring error;
    if (store_ && !store_->DeleteNote(notePath_, error)) {
        wxMessageBox(wxString(L"删除便签失败：\n") + FromW(error), L"PenDown",
                     wxOK | wxICON_ERROR, this);
        return;
    }
    if (store_) store_->RemoveNoteAutosave(notePath_);

    const std::wstring deletedPath = notePath_;
    notePath_.clear();
    dirty_ = false;
    if (onDeleted_) onDeleted_(deletedPath, groupPath_);
    if (!closedNotified_ && onClosed_) {
        closedNotified_ = true;
        onClosed_(this);
    }
    Destroy();
}

void NoteWindow::ToggleColorPopup() {
    if (!editor_) return;
    TextStyle style = editor_->StyleAtCaret();
    ColorTarget target = colorPopup_ ? colorPopup_->Target()
                                     : ColorTarget::Foreground;
    wxColour initial =
        target == ColorTarget::Background
            ? ColourFromHex(style.background, Theme::Surface())
            : ColourFromHex(style.foreground, Theme::Text());

    const long selectionStart = editor_->GetSelectionStart();
    const long selectionEnd = editor_->GetSelectionEnd();
    if (selectionStart != selectionEnd) {
        popupSelectionStart_ = std::min(selectionStart, selectionEnd);
        popupSelectionEnd_ = std::max(selectionStart, selectionEnd);
    } else {
        popupSelectionStart_ = -1;
        popupSelectionEnd_ = -1;
    }

    if (!colorPopup_) {
        colorPopup_ = new ColorPopup(
            this, initial,
            [this](const wxColour& colour, bool clear, ColorTarget target) {
                ApplyColour(colour, clear, target);
            });
        colorPopup_->Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
            if (colorButton_) colorButton_->SetActive(event.IsShown());
            event.Skip();
        });
    }

    if (colorPopup_->IsShown()) {
        colorPopup_->Hide();
        return;
    }

    colorPopup_->SetTarget(target);
    colorPopup_->SetColour(initial);
    const wxPoint base = GetPosition();
    const int top = base.y + FromDIP(52);
    const int right = base.x + GetSize().x - FromDIP(20);
    const int colorX = right - colorPopup_->GetSize().x;
    colorPopup_->Move(colorX, top);
    if (stylePopup_ && stylePopup_->IsShown()) {
        // 两个浮动面板同时打开时，样式库放到颜色盘左侧，避免互相遮挡。
        stylePopup_->Move(colorX - stylePopup_->GetSize().x - FromDIP(8), top);
        stylePopup_->Raise();
    }
    colorPopup_->Show();
    colorPopup_->Raise();
}

void NoteWindow::ToggleStylePopup() {
    if (!editor_) return;
    if (!stylePopup_) {
        stylePopup_ = new StylePopup(
            this, [this](FormatCommand command) { ApplyStyle(command); });
        stylePopup_->Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
            if (styleButton_) styleButton_->SetActive(event.IsShown());
            event.Skip();
        });
    }

    const long selectionStart = editor_->GetSelectionStart();
    const long selectionEnd = editor_->GetSelectionEnd();
    if (selectionStart != selectionEnd) {
        popupSelectionStart_ = std::min(selectionStart, selectionEnd);
        popupSelectionEnd_ = std::max(selectionStart, selectionEnd);
    } else {
        popupSelectionStart_ = -1;
        popupSelectionEnd_ = -1;
    }

    if (stylePopup_->IsShown()) {
        stylePopup_->Hide();
        return;
    }

    UpdateFormatPainterState();
    const wxPoint base = GetPosition();
    const int top = base.y + FromDIP(52);
    const int right = base.x + GetSize().x - FromDIP(20);
    int styleX = right - stylePopup_->GetSize().x;
    if (colorPopup_ && colorPopup_->IsShown()) {
        // 颜色盘已经在右侧时，样式库放到它左边，两个面板可以同时使用。
        styleX = colorPopup_->GetPosition().x - stylePopup_->GetSize().x -
                 FromDIP(8);
    }
    stylePopup_->Move(styleX, top);
    stylePopup_->Show();
    stylePopup_->Raise();
}

void NoteWindow::ApplyColour(const wxColour& colour, bool clear,
                             ColorTarget target) {
    if (!editor_) return;
    RestorePopupSelection();
    if (target == ColorTarget::Background) {
        editor_->ApplyBackground(colour, clear);
    } else {
        editor_->ApplyForeground(colour, clear);
        if (!clear) lastColour_ = colour;
    }
    UpdateStatus();
    UpdateFormatPainterState();
}

void NoteWindow::ApplyStyle(FormatCommand command) {
    if (!editor_) return;

    RestorePopupSelection();
    editor_->ApplyCommand(command);
    UpdateStatus();
    UpdateFormatPainterState();
}

void NoteWindow::ClearFormatting() {
    if (!editor_) return;
    editor_->ApplyCommand(FormatCommand::ClearFormatting);
    ShowHint(L"已清除样式");
    UpdateStatus();
    UpdateFormatPainterState();
    editor_->FocusEditor();
}

void NoteWindow::ToggleFormatPainter() {
    if (!editor_) return;

    if (editor_->FormatPainterArmed()) {
        // 已经复制格式时再点一次就是取消，避免误把格式刷到整行。
        editor_->CancelFormatPainter();
        ShowHint(L"格式刷：已取消");
    } else {
        const NoteEditor::FormatPainterResult result =
            editor_->ApplyFormatPainter();
        if (result == NoteEditor::FormatPainterResult::Copied) {
            ShowHint(L"格式刷：已复制格式，选择目标文字后松开鼠标即可应用");
        } else if (result == NoteEditor::FormatPainterResult::Applied) {
            ShowHint(L"格式刷：已应用格式");
        }
    }
    UpdateStatus();
    UpdateFormatPainterState();
    editor_->FocusEditor();
}

void NoteWindow::ShowHint(const wxString& text) {
    hintText_ = text;
    UpdateStatus();
    hintTimer_.StartOnce(1800);
}

void NoteWindow::ShowFindDialog() {
    if (!editor_) return;
    if (!findDialog_) findDialog_ = new FindReplaceDialog(this, editor_);
    findDialog_->FocusFind();
}

void NoteWindow::RestorePopupSelection() {
    if (!editor_) return;
    const long start = editor_->GetSelectionStart();
    const long end = editor_->GetSelectionEnd();
    if (start != end) return;
    if (popupSelectionStart_ >= 0 && popupSelectionEnd_ > popupSelectionStart_ &&
        popupSelectionEnd_ <= editor_->GetTextLength()) {
        editor_->SetSelection(popupSelectionStart_, popupSelectionEnd_);
    }
}

void NoteWindow::UpdateFormatPainterState() {
    if (formatPainterButton_ && editor_) {
        formatPainterButton_->SetActive(editor_->FormatPainterArmed());
    }
}

void NoteWindow::UpdateStatus() {
    if (!status_ || !editor_) return;
    statusTimer_.Stop();
    wxString text = L"富文本";
    if (notePath_.empty() && !dirty_) {
        text += L" · 新建";
    } else {
        text += dirty_ ? L" · 未保存" : L" · 已保存";
        text += wxString::Format(L" · %ld 字", editor_->CharCount());
    }
    if (!autosaveLabel_.empty()) text += L" · " + autosaveLabel_;
    if (!hintText_.empty()) text += L" · " + hintText_;
    if (status_->GetLabel() != text) status_->SetLabel(text);
}

void NoteWindow::UpdateFrameTitle() {
    if (!editor_) return;
    wxString display = FromW(title_.empty() ? L"无标题便签" : title_);
    if (notePath_.empty()) display = L"新建便签";
    if (GetTitle() != display) SetTitle(display);
}

std::wstring NoteWindow::DeriveTitle() const {
    if (!editor_) return L"无标题便签";
    return TitleFromUtf8(editor_->GetTextUtf8());
}

void NoteWindow::MarkDirty() {
    if (loading_) return;
    dirty_ = true;
    statusTimer_.StartOnce(180);
    ScheduleAutosave();
}

void NoteWindow::OnClose(wxCloseEvent& event) {
    if (dirty_ || (editor_ && editor_->IsDirty())) {
        const int answer = wxMessageBox(L"当前文件未保存，是否保存后关闭？", L"PenDown",
                                        wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
        if (answer == wxCANCEL) {
            event.Veto();
            return;
        }
        if (answer == wxYES && !SaveNote()) {
            event.Veto();
            return;
        }
        if (answer == wxNO) {
            RemoveAutosave();
        }
    }

    statusTimer_.Stop();
    autosaveTimer_.Stop();
    if (!closedNotified_ && onClosed_) {
        closedNotified_ = true;
        onClosed_(this);
    }
    event.Skip();
}
