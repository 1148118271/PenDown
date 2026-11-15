#include "ui/note_tree_ctrl.h"

#include "core/text.h"
#include "ui/app_font.h"
#include "ui/icon_library.h"
#include "ui/theme.h"
#include "ui/ui_text.h"

#include <wx/control.h>
#include <wx/dcclient.h>

#include <algorithm>

namespace {

int ClampTextWidth(int width) {
    return width > 0 ? width : 1;
}

}  // namespace

NoteTreeCtrl::NoteTreeCtrl(wxWindow* parent, wxWindowID id)
    : wxVListBox(parent, id, wxDefaultPosition, wxDefaultSize,
                 wxBORDER_NONE | wxVSCROLL | wxFULL_REPAINT_ON_RESIZE) {
    SetBackgroundColour(Theme::Surface());
    SetForegroundColour(Theme::Text());
    SetSelectionBackground(Theme::AccentSoft());
    SetFont(AppFont::Make(AppFont::BasePointSize()));

    baseFont_ = AppFont::Make(std::max(8, AppFont::BasePointSize() - 1));
    groupFont_ = baseFont_;
    groupFont_.MakeBold();
    countFont_ = AppFont::Make(std::max(8, AppFont::BasePointSize() - 2));

    Bind(wxEVT_LEFT_DOWN, &NoteTreeCtrl::OnLeftDown, this);
    Bind(wxEVT_LEFT_DCLICK, &NoteTreeCtrl::OnLeftDoubleClick, this);
    Bind(wxEVT_MOTION, &NoteTreeCtrl::OnMotion, this);
    Bind(wxEVT_LEAVE_WINDOW, &NoteTreeCtrl::OnLeave, this);
    Bind(wxEVT_CONTEXT_MENU, &NoteTreeCtrl::OnContextMenu, this);
    Bind(wxEVT_KEY_DOWN, &NoteTreeCtrl::OnKeyDown, this);
    Bind(wxEVT_LISTBOX, &NoteTreeCtrl::OnListBox, this);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
}

void NoteTreeCtrl::SetItems(std::vector<Item> items) {
    items_ = std::move(items);
    hovered_ = -1;
    groupRows_.clear();
    noteRows_.clear();
    groupRows_.reserve(items_.size());
    noteRows_.reserve(items_.size());
    for (size_t i = 0; i < items_.size(); ++i) {
        auto& map = items_[i].kind == Kind::Group ? groupRows_ : noteRows_;
        map.emplace(items_[i].path, (int)i);
    }
    SetItemCount(items_.size());
    if (items_.empty()) {
        SetSelection(wxNOT_FOUND);
    } else if (GetSelection() >= (int)items_.size()) {
        SetSelection(wxNOT_FOUND);
    }
    Refresh();
}

int NoteTreeCtrl::FindItem(Kind kind, const std::wstring& path) const {
    const auto& rows = kind == Kind::Group ? groupRows_ : noteRows_;
    const auto found = rows.find(path);
    return found == rows.end() ? wxNOT_FOUND : found->second;
}

const NoteTreeCtrl::Item* NoteTreeCtrl::SelectedItem() const {
    int selection = GetSelection();
    if (selection < 0 || selection >= (int)items_.size()) return nullptr;
    return &items_[(size_t)selection];
}

void NoteTreeCtrl::SelectItem(Kind kind, const std::wstring& path) {
    int index = FindItem(kind, path);
    if (index == wxNOT_FOUND) return;
    SetSelection(index);
    ScrollToRow((size_t)index);
    NotifySelection();
}

void NoteTreeCtrl::OnDrawBackground(wxDC& dc, const wxRect& rect, size_t n) const {
    wxRect row(0, rect.y, GetClientSize().x, rect.height);

    // 先铺一层主题底色，避免选中状态移动后残留旧胶囊
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(Theme::Surface()));
    dc.DrawRectangle(row);

    wxColour background;
    if (IsSelected(n)) {
        background = Theme::AccentSoft();
    } else if ((int)n == hovered_) {
        background = Theme::Hover();
    } else {
        return;
    }

    wxRect pill = row;
    pill.Deflate(FromDIP(3), FromDIP(2));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(background));
    dc.DrawRoundedRectangle(pill, FromDIP(4));
}

void NoteTreeCtrl::OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const {
    if (n >= items_.size()) return;
    const Item& item = items_[n];

    wxRect row(0, rect.y, GetClientSize().x, rect.height);
    wxDCClipper clip(dc, row);

    wxFont font = item.kind == Kind::Group ? groupFont_ : baseFont_;
    dc.SetFont(font);

    const int iconSize = FromDIP(item.kind == Kind::Group ? 14 : 13);
    const int chevronSize = FromDIP(10);
    const int iconY = rect.y + (rect.height - iconSize) / 2;

    if (item.kind == Kind::Group) {
        if (item.hasChildren) {
            const wxBitmap& chevron = CachedIcon(
                item.expanded ? AppIcon::ChevronDown : AppIcon::ChevronRight,
                chevronSize, Theme::TextMuted());
            if (chevron.IsOk()) {
                int y = rect.y + (rect.height - chevronSize) / 2;
                dc.DrawBitmap(chevron, ChevronX(item), y, true);
            }
        }
        const wxBitmap& folder =
            CachedIcon(AppIcon::Folder, iconSize, Theme::TextMuted());
        if (folder.IsOk()) dc.DrawBitmap(folder, IconX(item), iconY, true);
        dc.SetTextForeground(Theme::Text());
    } else {
        const wxColour& iconColour =
            IsSelected(n) ? Theme::TextMuted() : Theme::TextFaint();
        const wxBitmap& note =
            CachedIcon(AppIcon::Note, iconSize, iconColour);
        if (note.IsOk()) dc.DrawBitmap(note, IconX(item), iconY, true);
        dc.SetTextForeground(Theme::Text());
    }

    wxString text =
        item.text.empty() ? UiText("note.default_title") : FromW(item.text);
    int textX = TextX(item);
    int reserved = (item.kind == Kind::Group && item.count > 0) ? FromDIP(24) : FromDIP(10);
    int maxTextWidth = GetClientSize().x - textX - reserved;
    text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END,
                                ClampTextWidth(maxTextWidth));
    int textHeight = dc.GetCharHeight();
    int textY = rect.y + (rect.height - textHeight) / 2;
    dc.DrawText(text, textX, textY);

    if (item.kind == Kind::Group && item.count > 0) {
        // 分组右侧显示直属便签数，折叠时也能看出有没有内容
        dc.SetFont(countFont_);
        dc.SetTextForeground(Theme::TextFaint());
        wxString count = wxString::Format("%d", item.count);
        int countWidth = dc.GetTextExtent(count).x;
        dc.DrawText(count, GetClientSize().x - countWidth - FromDIP(8), textY);
    }
}

wxCoord NoteTreeCtrl::OnMeasureItem(size_t) const {
    return FromDIP(30);
}

int NoteTreeCtrl::ItemAt(const wxPoint& point) const {
    if (point.y < 0) return wxNOT_FOUND;
    return VirtualHitTest(point.y);
}

int NoteTreeCtrl::ChevronX(const Item& item) const {
    return FromDIP(6) + item.depth * FromDIP(14);
}

int NoteTreeCtrl::IconX(const Item& item) const {
    return ChevronX(item) + FromDIP(15);
}

int NoteTreeCtrl::TextX(const Item& item) const {
    return IconX(item) + FromDIP(20);
}

void NoteTreeCtrl::RefreshRow(int index) {
    if (index < 0 || index >= (int)items_.size()) return;
    const wxRect row = GetItemRect((size_t)index);
    if (!row.IsEmpty()) RefreshRect(row);
}

const wxBitmap& NoteTreeCtrl::CachedIcon(AppIcon icon, int size,
                                         const wxColour& colour) const {
    const auto key = std::make_tuple((int)icon, size, (unsigned long)colour.GetRGB());
    const auto found = iconCache_.find(key);
    if (found != iconCache_.end()) return found->second;
    return iconCache_.emplace(key, IconLibrary::Get(icon, size, colour)).first->second;
}

void NoteTreeCtrl::NotifySelection() {
    if (onSelection_) onSelection_();
}

void NoteTreeCtrl::OnLeftDown(wxMouseEvent& event) {
    SetFocus();
    int index = ItemAt(event.GetPosition());
    if (index == wxNOT_FOUND) {
        SetSelection(wxNOT_FOUND);
        NotifySelection();
        Refresh();
        return;
    }

    SetSelection(index);
    NotifySelection();
    const Item& item = items_[(size_t)index];
    if (item.kind == Kind::Group && event.GetX() < TextX(item) &&
        event.GetX() >= ChevronX(item) && onToggleGroup_) {
        onToggleGroup_(item.path);
    }
    Refresh();
}

void NoteTreeCtrl::OnLeftDoubleClick(wxMouseEvent& event) {
    int index = ItemAt(event.GetPosition());
    if (index == wxNOT_FOUND) return;
    const Item& item = items_[(size_t)index];
    if (item.kind == Kind::Note && onOpenNote_) {
        onOpenNote_(item.path);
    } else if (item.kind == Kind::Group && onToggleGroup_) {
        onToggleGroup_(item.path);
    }
}

void NoteTreeCtrl::OnMotion(wxMouseEvent& event) {
    int index = ItemAt(event.GetPosition());
    if (index != hovered_) {
        RefreshRow(hovered_);
        hovered_ = index;
        RefreshRow(hovered_);
    }
    event.Skip();
}

void NoteTreeCtrl::OnLeave(wxMouseEvent& event) {
    if (hovered_ != -1) {
        RefreshRow(hovered_);
        hovered_ = -1;
    }
    event.Skip();
}

void NoteTreeCtrl::OnContextMenu(wxContextMenuEvent& event) {
    wxPoint screen = event.GetPosition();
    if (screen == wxDefaultPosition) screen = wxGetMousePosition();
    wxPoint client = ScreenToClient(screen);

    bool hasItem = false;
    Kind kind = Kind::Group;
    std::wstring path;
    int index = ItemAt(client);
    if (index != wxNOT_FOUND) {
        SetSelection(index);
        NotifySelection();
        const Item& item = items_[(size_t)index];
        hasItem = true;
        kind = item.kind;
        path = item.path;
        Refresh();
    }
    if (onContext_) onContext_(hasItem, kind, path, screen);
}

void NoteTreeCtrl::OnKeyDown(wxKeyEvent& event) {
    const Item* item = SelectedItem();
    if (item) {
        switch (event.GetKeyCode()) {
            case WXK_RETURN:
                if (item->kind == Kind::Note && onOpenNote_) {
                    // 等按键事件结束后再开新窗口，避免回车落入新窗口正文
                    ItemHandler handler = onOpenNote_;
                    std::wstring notePath = item->path;
                    CallAfter([handler, notePath] {
                        if (handler) handler(notePath);
                    });
                } else if (item->kind == Kind::Group && onToggleGroup_) {
                    onToggleGroup_(item->path);
                }
                return;
            case WXK_SPACE:
                if (item->kind == Kind::Group && onToggleGroup_) {
                    onToggleGroup_(item->path);
                }
                return;
            case WXK_DELETE:
                if (onDelete_) onDelete_();
                return;
            case WXK_F2:
                if (onRename_) onRename_();
                return;
            case WXK_RIGHT:
                if (item->kind == Kind::Group && !item->expanded && onToggleGroup_) {
                    onToggleGroup_(item->path);
                    return;
                }
                break;
            case WXK_LEFT:
                if (item->kind == Kind::Group && item->expanded && onToggleGroup_) {
                    onToggleGroup_(item->path);
                    return;
                }
                break;
            default:
                break;
        }
    }
    event.Skip();
}

void NoteTreeCtrl::OnListBox(wxCommandEvent& event) {
    NotifySelection();
    event.Skip();
}
