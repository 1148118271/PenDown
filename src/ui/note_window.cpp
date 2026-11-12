#include "ui/note_window.h"

#include "core/text.h"
#include "resource.h"
#include "ui/app_font.h"
#include "ui/icon_button.h"
#include "ui/markdown_preview.h"
#include "ui/rounded_panel.h"
#include "ui/theme.h"
#include "ui/ui_ids.h"
#include "ui/window_effects.h"

#include <wx/colordlg.h>
#include <wx/display.h>
#include <wx/mstream.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/richtext/richtextxml.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif

#include <algorithm>
#include <cwctype>

namespace {

void EnsureRichTextHandlers() {
    static bool registered = false;
    if (!registered) {
        wxRichTextBuffer::AddHandler(new wxRichTextXMLHandler);
        registered = true;
    }
}

std::string Utf8Of(const wxString& text) {
    wxScopedCharBuffer buffer = text.ToUTF8();
    return std::string(buffer.data(), buffer.length());
}

wxString FromUtf8(const std::string& bytes) {
    return wxString::FromUTF8(bytes.data(), bytes.size());
}

void ApplyEditorFont(wxRichTextCtrl* editor) {
    const wxFont font = editor->GetFont();

    // wxRichTextCtrl 默认每段后面留 1mm 间距，正文会显得松散；收紧为 0。
    wxRichTextAttr base = editor->GetBasicStyle();
    if (font.IsOk()) base.SetFont(font);
    base.SetParagraphSpacingBefore(0);
    base.SetParagraphSpacingAfter(0);
    base.SetLineSpacing(wxTEXT_ATTR_LINE_SPACING_NORMAL);
    editor->SetBasicStyle(base);

    wxRichTextAttr attr = editor->GetDefaultStyleEx();
    if (font.IsOk()) attr.SetFont(font);
    attr.SetParagraphSpacingBefore(0);
    attr.SetParagraphSpacingAfter(0);
    attr.SetLineSpacing(wxTEXT_ATTR_LINE_SPACING_NORMAL);
    editor->SetDefaultStyle(attr);
}

std::wstring FirstPlainLine(const std::wstring& text) {
    size_t start = 0;
    while (start <= text.size()) {
        size_t newline = text.find(L'\n', start);
        std::wstring line = newline == std::wstring::npos
                                ? text.substr(start)
                                : text.substr(start, newline - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        size_t begin = 0;
        size_t end = line.size();
        while (begin < end && iswspace(line[begin])) ++begin;
        while (end > begin && iswspace(line[end - 1])) --end;
        line = line.substr(begin, end - begin);
        if (!line.empty()) {
            if (line.size() > 120) line = line.substr(0, 120) + L"…";
            return line;
        }
        if (newline == std::wstring::npos) break;
        start = newline + 1;
    }
    return L"无标题便签";
}

#ifdef __WXMSW__
void SetNativeVerticalScrollbar(wxWindow* window, bool visible) {
    if (!window) return;
    HWND hwnd = (HWND)window->GetHandle();
    if (!hwnd) return;
    LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR updated = visible ? (style | WS_VSCROLL) : (style & ~WS_VSCROLL);
    if (updated != style) {
        ::SetWindowLongPtrW(hwnd, GWL_STYLE, updated);
        ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                           SWP_FRAMECHANGED);
    }
    ::ShowScrollBar(hwnd, SB_VERT, visible ? TRUE : FALSE);
}
#endif
}  // namespace

NoteWindow::NoteWindow(wxWindow* parent, std::shared_ptr<NoteStore> store,
                       const std::wstring& notePath, const std::wstring& groupPath,
                       NoteFormat format, SavedHandler onSaved, DeletedHandler onDeleted,
                       ClosedHandler onClosed)
    : wxFrame(parent, wxID_ANY, L"PenDown", wxDefaultPosition, wxDefaultSize,
              wxBORDER_NONE | wxRESIZE_BORDER | wxCLIP_CHILDREN),
      store_(std::move(store)),
      notePath_(notePath),
      groupPath_(groupPath),
      format_(format),
      onSaved_(std::move(onSaved)),
      onDeleted_(std::move(onDeleted)),
      onClosed_(std::move(onClosed)),
      statusTimer_(this) {
    SetBackgroundColour(Theme::Background());
    SetSize(FromDIP(wxSize(620, 700)));
    SetMinSize(FromDIP(wxSize(380, 360)));

    BuildUi();
    if (rich_) {
        // 正文使用系统默认字体，并比 UI 字号稍大，保证阅读舒适度。
        rich_->SetFont(AppFont::Content());
        ApplyEditorFont(rich_);
    }
    if (markdown_) markdown_->SetFont(AppFont::Content());
    if (status_) status_->SetFont(AppFont::Make(std::max(8, AppFont::BasePointSize() - 2)));
    LoadNote();
    BindEvents();
    Layout();
    PrepareFramelessWindow(this);
    UpdateStatus();
    UpdateFrameTitle();

    // 新便签默认出现在主窗口所在屏幕的中间。
    int displayIndex = wxNOT_FOUND;
    if (wxWindow* owner = GetParent()) {
        displayIndex = wxDisplay::GetFromWindow(owner);
    }
    if (displayIndex == wxNOT_FOUND) displayIndex = 0;
    const wxRect workArea = wxDisplay(static_cast<unsigned>(displayIndex)).GetClientArea();
    const wxSize windowSize = GetSize();
    SetPosition(wxPoint(workArea.x + (workArea.width - windowSize.x) / 2,
                        workArea.y + (workArea.height - windowSize.y) / 2));

    if (markdown_) markdown_->SetFocus();
    if (rich_) rich_->SetFocus();

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
#endif
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
void NoteWindow::UpdateLocation(const std::wstring& notePath, const std::wstring& groupPath) {
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

    wxBitmap formatBitmap = IconLibrary::Get(
        format_ == NoteFormat::Markdown ? AppIcon::Markdown : AppIcon::Note, FromDIP(18),
        Theme::TextMuted());
    wxStaticBitmap* formatIcon = nullptr;
    if (formatBitmap.IsOk()) {
        formatIcon = new wxStaticBitmap(titleBar, wxID_ANY, formatBitmap);
        bar->Add(formatIcon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    }
    bar->AddSpacer(FromDIP(8));

    const wxSize buttonSize = FromDIP(wxSize(26, 22));
    auto* save = new IconButton(titleBar, ID_NOTE_SAVE, AppIcon::Save, L"保存便签 (Ctrl+S)",
                                IconButton::Kind::Accent, buttonSize, 12);
    save->SetEventTarget(this);
    bar->Add(save, 0, wxRIGHT, FromDIP(1));

    if (format_ == NoteFormat::Markdown) {
        eyeButton_ = new IconButton(titleBar, ID_NOTE_PREVIEW, AppIcon::EyeOff,
                                    L"切换到预览模式 (Ctrl+P)：查看样式后的效果",
                                    IconButton::Kind::Normal, buttonSize, 12);
        eyeButton_->SetEventTarget(this);
        bar->Add(eyeButton_, 0, wxRIGHT, FromDIP(1));
    } else {
        auto addFormatButton = [&](wxWindowID id, AppIcon icon, const wxString& tip) {
            auto* button = new IconButton(titleBar, id, icon, tip, IconButton::Kind::Normal,
                                          buttonSize, 12);
            button->SetEventTarget(this);
            bar->Add(button, 0, wxRIGHT, FromDIP(1));
        };
        addFormatButton(ID_FORMAT_BOLD, AppIcon::Bold, L"加粗选中文字 (Ctrl+B)");
        addFormatButton(ID_FORMAT_ITALIC, AppIcon::Italic, L"斜体选中文字 (Ctrl+I)");
        addFormatButton(ID_FORMAT_STRIKE, AppIcon::Strikethrough, L"给选中文字加删除线");
        addFormatButton(ID_FORMAT_COLOUR, AppIcon::Palette, L"设置选中文字颜色");
    }

    bar->AddStretchSpacer(1);
    auto* remove = new IconButton(titleBar, ID_NOTE_DELETE, AppIcon::Trash, L"删除便签",
                                  IconButton::Kind::Danger, buttonSize, 12);
    remove->SetEventTarget(this);
    bar->Add(remove, 0);

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
    if (formatIcon) {
        formatIcon->Bind(wxEVT_LEFT_DOWN, beginDrag);
        formatIcon->Bind(wxEVT_LEFT_DCLICK, toggleMax);
    }
#endif

    auto* bodyHost = new RoundedPanel(content, 7, 1, format_ == NoteFormat::Markdown ? 8 : 3);
    bodyHost->SetHorizontalBordersOnly(true);
    if (format_ == NoteFormat::Markdown) {
        bodyBook_ = new wxSimplebook(bodyHost);
        bodyBook_->SetBackgroundColour(Theme::Surface());

        markdown_ = new wxTextCtrl(bodyBook_, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                   wxDefaultSize,
                                   wxTE_MULTILINE | wxTE_RICH2 | wxBORDER_NONE |
                                       wxTE_PROCESS_TAB);
        markdown_->SetBackgroundColour(Theme::Surface());
        markdown_->SetForegroundColour(Theme::Text());
        markdown_->SetFont(AppFont::Content());
#ifdef __WXMSW__
        SetNativeVerticalScrollbar(markdown_, false);
        markdownScrollbarShown_ = false;
        markdown_->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            UpdateMarkdownScrollbar();
            event.Skip();
        });
#endif

        preview_ = new wxRichTextCtrl(bodyBook_, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                      wxDefaultSize, wxBORDER_NONE | wxTE_READONLY);
        preview_->SetBackgroundColour(Theme::Surface());
        preview_->SetEditable(false);

        bodyBook_->AddPage(markdown_, L"Markdown");
        bodyBook_->AddPage(preview_, L"预览");
        bodyBook_->SetSelection(0);
        bodyHost->AddContent(bodyBook_, 1);
    } else {
        EnsureRichTextHandlers();
        rich_ = new wxRichTextCtrl(bodyHost, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                   wxDefaultSize, wxBORDER_NONE);
        rich_->SetBackgroundColour(Theme::Surface());
        rich_->SetForegroundColour(Theme::Text());
        rich_->SetFont(AppFont::Content());
        ApplyEditorFont(rich_);
        bodyHost->AddContent(rich_, 1);
    }
    root->Add(bodyHost, 1, wxEXPAND | wxTOP, FromDIP(4));


    status_ = new wxStaticText(content, wxID_ANY, wxEmptyString);
    status_->SetForegroundColour(Theme::TextFaint());
    status_->SetFont(AppFont::Make(std::max(8, AppFont::BasePointSize() - 2)));
    root->Add(status_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(5));

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
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { UpdateStatus(); });
    Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
        if (event.IsShown()) {
            CallAfter([this] { UpdateMarkdownScrollbar(); });
        }
        event.Skip();
    });
    if (eyeButton_) {
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { TogglePreview(); }, ID_NOTE_PREVIEW);
    }
    if (format_ == NoteFormat::RichText) {
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplyBold(); }, ID_FORMAT_BOLD);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplyItalic(); }, ID_FORMAT_ITALIC);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplyStrike(); }, ID_FORMAT_STRIKE);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplyColour(); }, ID_FORMAT_COLOUR);
    }

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
        if (event.ControlDown() && event.GetKeyCode() == 'P' &&
            format_ == NoteFormat::Markdown) {
            TogglePreview();
            return;
        }
        if (event.ControlDown() && event.GetKeyCode() == 'B' && rich_) {
            ApplyBold();
            return;
        }
        if (event.ControlDown() && event.GetKeyCode() == 'I' && rich_) {
            ApplyItalic();
            return;
        }
        event.Skip();
    });

    if (markdown_) {
        markdown_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
            if (!loading_) MarkDirty();
            event.Skip();
        });
    }
    if (rich_) {
        rich_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
            if (!loading_) MarkDirty();
            event.Skip();
        });

        rich_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
            if (event.ControlDown() && event.GetKeyCode() == 'B') {
                ApplyBold();
                return;
            }
            if (event.ControlDown() && event.GetKeyCode() == 'I') {
                ApplyItalic();
                return;
            }
            event.Skip();
        });
    }
}

void NoteWindow::LoadNote() {
    loading_ = true;
    if (!notePath_.empty() && store_) {
        std::string bytes;
        std::wstring error;
        if (store_->ReadNote(notePath_, bytes, error)) {
            if (format_ == NoteFormat::Markdown) {
                markdown_->ChangeValue(FromUtf8(bytes));
                markdown_->SetInsertionPointEnd();
            } else if (rich_) {
                EnsureRichTextHandlers();
                wxMemoryInputStream stream(bytes.data(), bytes.size());
                rich_->Clear();
                ApplyEditorFont(rich_);
                bool loaded = rich_->GetBuffer().LoadFile(stream, wxRICHTEXT_TYPE_XML);
                if (!loaded) {
                    wxMessageBox(L"普通文本便签加载失败。", L"PenDown", wxOK | wxICON_ERROR, this);
                }
                // 已保存的便签也统一成紧凑段落间距。
                ApplyEditorFont(rich_);
                rich_->Invalidate();
                rich_->SetInsertionPointEnd();
                rich_->DiscardEdits();
            }
        } else {
            wxMessageBox(wxString(L"读取便签失败：\n") + FromW(error), L"PenDown",
                         wxOK | wxICON_ERROR, this);
        }
    }
    title_ = DeriveTitle();
    dirty_ = false;
    loading_ = false;
}

std::wstring NoteWindow::PlainText() const {
    if (markdown_) return ToW(markdown_->GetValue());
    if (rich_) return ToW(rich_->GetValue());
    return std::wstring();
}

std::wstring NoteWindow::DeriveTitle() const {
    std::wstring text = PlainText();
    if (format_ == NoteFormat::Markdown && store_) return store_->DeriveTitle(text);
    return FirstPlainLine(text);
}

void NoteWindow::MarkDirty() {
    dirty_ = true;
    // 输入时不做整篇文本扫描，也不立刻刷新界面；180ms 后统一更新状态栏。
    statusTimer_.StartOnce(180);
}

bool NoteWindow::SaveNote() {
    if (!store_) return false;

    std::string bytes;
    if (format_ == NoteFormat::Markdown) {
        if (!markdown_) return false;
        bytes = Utf8Of(markdown_->GetValue());
    } else {
        if (!rich_) return false;
        EnsureRichTextHandlers();
        wxMemoryOutputStream stream;
        if (!rich_->GetBuffer().SaveFile(stream, wxRICHTEXT_TYPE_XML)) {
            wxMessageBox(L"保存富文本内容失败。", L"PenDown", wxOK | wxICON_ERROR, this);
            return false;
        }
        size_t length = stream.GetSize();
        bytes.assign(length, '\0');
        stream.CopyTo(bytes.data(), length);
    }

    std::wstring title = DeriveTitle();
    std::wstring savedPath;
    std::wstring error;
    const std::wstring oldPath = notePath_;
    if (!store_->SaveNote(notePath_, groupPath_, bytes, title, format_, savedPath, error)) {
        wxMessageBox(wxString(L"保存便签失败：\n") + FromW(error), L"PenDown",
                     wxOK | wxICON_ERROR, this);
        return false;
    }

    notePath_ = savedPath;
    title_ = title;
    dirty_ = false;
    UpdateStatus();
    UpdateFrameTitle();
    if (onSaved_) onSaved_(oldPath, notePath_, groupPath_, format_);
    return true;
}

void NoteWindow::DeleteNote() {
    if (notePath_.empty()) {
        int answer = wxMessageBox(L"确定删除当前便签？\n删除后无法恢复。", L"删除便签",
                                  wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
        if (answer != wxYES) return;
        dirty_ = false;
        Close();
        return;
    }
    wxString title = FromW(title_.empty() ? L"无标题便签" : title_);
    int answer = wxMessageBox(wxString(L"删除便签「") + title + L"」？\n删除后无法恢复。",
                              L"删除便签", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
    if (answer != wxYES) return;

    std::wstring error;
    if (store_ && !store_->DeleteNote(notePath_, error)) {
        wxMessageBox(wxString(L"删除便签失败：\n") + FromW(error), L"PenDown",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    std::wstring deletedPath = notePath_;
    notePath_.clear();
    dirty_ = false;
    if (onDeleted_) onDeleted_(deletedPath, groupPath_);
    if (!closedNotified_ && onClosed_) {
        closedNotified_ = true;
        onClosed_(this);
    }
    Destroy();
}

void NoteWindow::TogglePreview() {
    if (format_ != NoteFormat::Markdown || !bodyBook_ || !markdown_ || !preview_) return;
    previewMode_ = !previewMode_;
    if (previewMode_) {
        RenderMarkdownPreview(preview_, markdown_->GetValue());
        bodyBook_->SetSelection(1);
        if (eyeButton_) {
            eyeButton_->SetIcon(AppIcon::Eye);
            eyeButton_->SetToolTip(L"切换到编辑模式 (Ctrl+P)：查看 Markdown 原文");
            eyeButton_->SetActive(true);
            eyeButton_->SetFocus();
        }
        preview_->SetInsertionPoint(0);
    } else {
        bodyBook_->SetSelection(0);
        if (eyeButton_) {
            eyeButton_->SetIcon(AppIcon::EyeOff);
            eyeButton_->SetToolTip(L"切换到预览模式 (Ctrl+P)：查看样式后的效果");
            eyeButton_->SetActive(false);
        }
        markdown_->SetFocus();
    }
    UpdateStatus();
}

void NoteWindow::ApplyBold() {
    if (!rich_) return;
    rich_->ApplyBoldToSelection();
    MarkDirty();
    rich_->SetFocus();
}

void NoteWindow::ApplyItalic() {
    if (!rich_) return;
    rich_->ApplyItalicToSelection();
    MarkDirty();
    rich_->SetFocus();
}

void NoteWindow::ApplyStrike() {
    if (!rich_) return;
    rich_->ApplyTextEffectToSelection(wxTEXT_ATTR_EFFECT_STRIKETHROUGH);
    MarkDirty();
    rich_->SetFocus();
}

void NoteWindow::ApplyColour() {
    if (!rich_) return;
    wxColourDialog dialog(this);
    if (dialog.ShowModal() != wxID_OK) return;
    wxColour colour = dialog.GetColourData().GetColour();

    wxRichTextRange range = rich_->GetSelectionRange();
    if (range.GetLength() > 0) {
        wxRichTextAttr attr;
        attr.SetTextColour(colour);
        rich_->SetStyleEx(range, attr, wxRICHTEXT_SETSTYLE_WITH_UNDO);
    } else {
        wxRichTextAttr attr = rich_->GetDefaultStyleEx();
        attr.SetTextColour(colour);
        rich_->SetDefaultStyle(attr);
    }
    MarkDirty();
    rich_->SetFocus();
}

void NoteWindow::UpdateStatus() {
    if (!status_) return;
    statusTimer_.Stop();
    wxString text = format_ == NoteFormat::Markdown ? L"Markdown" : L"普通文本";
    if (previewMode_) {
        text += L" · 预览模式";
    } else {
        if (format_ == NoteFormat::Markdown) text += L" · 编辑模式";
        if (notePath_.empty() && !dirty_) {
            text += L" · 新建";
        } else {
            text += dirty_ ? L" · 未保存" : L" · 已保存";
            long length = 0;
            if (markdown_) {
                length = markdown_->GetLastPosition();
            } else if (rich_) {
                length = rich_->GetLastPosition();
            }
            text += wxString::Format(L" · %ld 字", length);
        }
    }
    // 状态栏文字变化不需要整窗重新布局，否则输入时会全页闪烁。
    if (status_->GetLabel() != text) status_->SetLabel(text);
    UpdateMarkdownScrollbar();
}

void NoteWindow::UpdateMarkdownScrollbar() {
#ifdef __WXMSW__
    if (!markdown_) return;
    HWND hwnd = (HWND)markdown_->GetHandle();
    if (!hwnd) return;
    const int totalLines = (int)::SendMessageW(hwnd, EM_GETLINECOUNT, 0, 0);
    const int lineHeight = std::max(1, markdown_->GetCharHeight() + FromDIP(4));
    const int visibleLines = std::max(1, markdown_->GetClientSize().y / lineHeight);
    const bool needScrollbar = totalLines > visibleLines;
    if (!needScrollbar) {
        markdownScrollbarShown_ = false;
        // 原生 RichEdit 会自己把 WS_VSCROLL 样式加回来，这里每次都确保它不可见。
        ::ShowScrollBar(hwnd, SB_VERT, FALSE);
        return;
    }
    if (markdownScrollbarShown_) return;
    markdownScrollbarShown_ = true;
    SetNativeVerticalScrollbar(markdown_, true);
#endif
}

void NoteWindow::UpdateFrameTitle() {
    wxString display = FromW(title_.empty() ? L"无标题便签" : title_);
    if (notePath_.empty()) display = L"新建便签";
    if (GetTitle() != display) SetTitle(display);
}

void NoteWindow::OnClose(wxCloseEvent& event) {
    if (dirty_) {
        // 不展示便签标题或正文，避免把内容带到系统弹窗里。
        int answer = wxMessageBox(L"当前文件未保存，是否保存后关闭？", L"PenDown",
                                  wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
        if (answer == wxCANCEL) {
            event.Veto();
            return;
        }
        if (answer == wxYES && !SaveNote()) {
            event.Veto();
            return;
        }
    }

    if (!closedNotified_ && onClosed_) {
        closedNotified_ = true;
        onClosed_(this);
    }
    event.Skip();
}
