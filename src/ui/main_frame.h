#pragma once

#include <wx/frame.h>

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/note_store.h"
#include "ui/note_tree_ctrl.h"

class NoteWindow;
class IconButton;
class TrayIcon;
class wxStaticText;

// 便签主窗口：窄而高的分组树，顶部图标工具栏。
// 数据全部来自 NoteStore（data 目录 / 文件夹 / Markdown 文件）。
class MainFrame : public wxFrame {
public:
    explicit MainFrame(std::shared_ptr<NoteStore> store);
    ~MainFrame() override;

#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

private:
    void BuildUi();
    void BindEvents();
    void RestoreLayout();
    void SaveLayout();
    void OnClose(wxCloseEvent& event);
    void CreateTrayIcon();
    void HideToTray();
    void RestoreFromTray();
    void ExitFromTray();
    void OpenSettings();
    bool ChangeDataDirectory(const wxString& newDir);
    void LoadHotkeys();
    void RegisterHotkeys();
    void UnregisterHotkeys();
    bool ApplyHotkeys(const wxString& showMain, const wxString& newNote,
                      const wxString& newMarkdown);

    void LoadData();
    void RebuildTree();
    void RebuildIndexes();
    void UpdateCounts();
    void UpdateSelectionState();
    void SetBottomStatus(const wxString& text);

    void AddGroup();
    void AddGroupUnder(const std::wstring& parentPath);
    void AddNote(NoteFormat format);
    void AddNoteToGroup(const std::wstring& groupPath, NoteFormat format);
    void OpenNote(const std::wstring& notePath);
    void RenameSelected();
    void RenameGroup(const std::wstring& groupPath);
    void MoveToGroup(NoteTreeCtrl::Kind kind, const std::wstring& path);
    void DeleteSelected();
    void DeleteGroup(const std::wstring& groupPath);
    void DeleteNote(const std::wstring& notePath);
    void OnToggleGroup(const std::wstring& groupPath);
    void OnTreeContext(bool hasItem, NoteTreeCtrl::Kind kind, const std::wstring& path,
                       const wxPoint& screen);
    void OnTreeSelection();
    void OnNoteSaved(const std::wstring& oldPath, const std::wstring& notePath,
                     const std::wstring& groupPath, NoteFormat format);
    void OnNoteDeleted(const std::wstring& notePath, const std::wstring& groupPath);
    void OnNoteWindowClosed(NoteWindow* window);

    void RemoveGroupSubtreeFromModel(const std::wstring& groupPath);
    void ReplaceGroupPrefixInModel(const std::wstring& oldPath,
                                   const std::wstring& newPath);

    std::wstring ResolveNewNoteGroup() const;
    std::wstring GroupParent(const std::wstring& groupPath) const;
    std::wstring GroupName(const std::wstring& groupPath) const;
    const NoteFileEntry* FindNote(const std::wstring& notePath) const;
    NoteWindow* FindNoteWindow(const std::wstring& notePath) const;
    void ExpandGroup(const std::wstring& groupPath);
    bool IsGroupExpanded(const std::wstring& groupPath) const;
    void CollectGroupTree(const std::wstring& groupPath, std::set<std::wstring>& paths) const;
    void CloseNoteWindowsInGroups(const std::set<std::wstring>& groupPaths);

    std::shared_ptr<NoteStore> store_;
    NoteTreeCtrl* tree_ = nullptr;
    IconButton* addGroupButton_ = nullptr;
    IconButton* addNoteButton_ = nullptr;
    IconButton* addMarkdownButton_ = nullptr;
    IconButton* deleteButton_ = nullptr;
    wxStaticText* countLabel_ = nullptr;
    wxStaticText* bottomStatus_ = nullptr;
    TrayIcon* trayIcon_ = nullptr;
    bool trayAvailable_ = false;
    bool reallyExit_ = false;
    bool hiddenToTray_ = false;
    wxString hotkeyShowMain_;
    wxString hotkeyNewNote_;
    wxString hotkeyNewMarkdown_;

    std::vector<NoteGroupEntry> groups_;
    std::vector<NoteFileEntry> notes_;
    std::vector<std::wstring> groupLowerPaths_;
    std::vector<std::wstring> groupLowerNames_;
    std::vector<std::wstring> groupLowerParents_;
    std::unordered_map<std::wstring, size_t> groupIndex_;
    std::unordered_map<std::wstring, size_t> noteIndex_;
    std::unordered_map<std::wstring, std::vector<size_t>> groupChildren_;
    std::unordered_map<std::wstring, std::vector<size_t>> notesByGroup_;
    bool indexesDirty_ = true;
    std::set<std::wstring> expandedGroups_;
    std::vector<NoteWindow*> noteWindows_;

    std::wstring selectedGroupPath_;
    std::wstring selectedNotePath_;
    bool expansionInitialized_ = false;
    bool shuttingDown_ = false;
};
