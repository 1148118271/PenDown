#pragma once

#include <wx/bitmap.h>
#include <wx/font.h>
#include <wx/vlbox.h>

#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

enum class AppIcon;

// 自绘的多级便签树：分组可以嵌套，便签只能作为叶子节点。
// 用 wxVListBox 而不是原生 wxTreeCtrl，是为了让选中和悬浮色铺满整行，
// 并且和圆角浅色容器的排版保持一致。
class NoteTreeCtrl : public wxVListBox {
public:
    enum class Kind {
        Group,
        Note,
    };

    struct Item {
        Kind kind = Kind::Group;
        std::wstring path;
        int depth = 0;
        std::wstring text;
        int count = 0;
        bool expanded = false;
        bool hasChildren = false;
        bool markdown = false;
    };

    using SelectionHandler = std::function<void()>;
    using ItemHandler = std::function<void(const std::wstring&)>;
    using ContextHandler = std::function<void(bool hasItem, Kind kind,
                                              const std::wstring& path, const wxPoint& screen)>;

    NoteTreeCtrl(wxWindow* parent, wxWindowID id);

    void SetItems(std::vector<Item> items);
    int FindItem(Kind kind, const std::wstring& path) const;
    const Item* SelectedItem() const;
    void SelectItem(Kind kind, const std::wstring& path);
    void SetSelectionHandler(SelectionHandler handler) {
        onSelection_ = std::move(handler);
    }
    void SetOpenNoteHandler(ItemHandler handler) { onOpenNote_ = std::move(handler); }
    void SetToggleGroupHandler(ItemHandler handler) { onToggleGroup_ = std::move(handler); }
    void SetContextHandler(ContextHandler handler) { onContext_ = std::move(handler); }
    void SetDeleteHandler(std::function<void()> handler) {
        onDelete_ = std::move(handler);
    }
    void SetRenameHandler(std::function<void()> handler) {
        onRename_ = std::move(handler);
    }

protected:
    void OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const override;
    void OnDrawBackground(wxDC& dc, const wxRect& rect, size_t n) const override;
    wxCoord OnMeasureItem(size_t) const override;

private:
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftDoubleClick(wxMouseEvent& event);
    void OnMotion(wxMouseEvent& event);
    void OnLeave(wxMouseEvent& event);
    void OnContextMenu(wxContextMenuEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnListBox(wxCommandEvent& event);

    int ItemAt(const wxPoint& point) const;
    int ChevronX(const Item& item) const;
    int IconX(const Item& item) const;
    int TextX(const Item& item) const;
    void NotifySelection();
    void RefreshRow(int index);
    const wxBitmap& CachedIcon(AppIcon icon, int size, const wxColour& colour) const;

    std::vector<Item> items_;
    std::unordered_map<std::wstring, int> groupRows_;
    std::unordered_map<std::wstring, int> noteRows_;
    wxFont baseFont_;
    wxFont groupFont_;
    wxFont countFont_;
    mutable std::map<std::tuple<int, int, unsigned long>, wxBitmap> iconCache_;
    int hovered_ = -1;
    SelectionHandler onSelection_;
    ItemHandler onOpenNote_;
    ItemHandler onToggleGroup_;
    ContextHandler onContext_;
    std::function<void()> onDelete_;
    std::function<void()> onRename_;
};
