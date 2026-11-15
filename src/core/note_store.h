#pragma once

#include <string>
#include <vector>

#include "core/style_model.h"

// 磁盘上的默认标题/分组名保持稳定，不随界面语言变化，避免同一篇便签
// 在不同语言环境下生成不同的文件名。英文界面只在显示层做本地化。
inline constexpr wchar_t kDefaultNoteTitle[] = L"无标题便签";
inline constexpr wchar_t kDefaultGroupName[] = L"未命名";

// 文件系统里的一个分组目录。rel_path 相对 data 目录，例如「工作/项目 A」。
struct NoteGroupEntry {
    std::wstring rel_path;
    std::wstring parent_path;
    std::wstring name;
};

// 文件系统里的一条便签文件：正文为 UTF-8 .txt，样式保存在同名 .style.json。
struct NoteFileEntry {
    std::wstring rel_path;
    std::wstring group_path;
    std::wstring title;
};

// 删除方式。默认把便签/分组移到 Windows 回收站，误删后可以找回；
// 自检等批量场景可以显式使用 Permanent。
enum class NoteDeleteMode {
    RecycleBin,
    Permanent,
};

// 便签存储：data 目录下每个文件夹就是一个分组，每个 .txt 文件就是一条便签。
// 正文不写入 Markdown 语法；颜色/字体等样式保存在 sidecar JSON。
// 旧版 .md 文件不再列出，也不会被删除，方便用户自行转换。
class NoteStore {
public:
    bool Open(const std::wstring& dataDir, std::wstring& error);

    const std::wstring& DataDir() const { return dataDir_; }

    bool ListTree(std::vector<NoteGroupEntry>& groups, std::vector<NoteFileEntry>& notes,
                  std::wstring& error) const;

    bool CreateGroup(const std::wstring& parentRel, const std::wstring& name,
                     std::wstring& createdRel, std::wstring& error);
    bool RenameGroup(const std::wstring& relPath, const std::wstring& newName,
                     std::wstring& newRel, std::wstring& error);
    bool MoveGroup(const std::wstring& relPath, const std::wstring& newParentRel,
                   std::wstring& newRel, std::wstring& error);
    bool DeleteGroup(const std::wstring& relPath, std::wstring& error,
                     NoteDeleteMode mode = NoteDeleteMode::RecycleBin);

    bool ReadNote(const std::wstring& relPath, std::string& bytes, std::wstring& error) const;
    bool LoadNoteDocument(const std::wstring& relPath, std::string& text,
                          StyleModel& styles, std::wstring& warning,
                          std::wstring& error) const;
    // relPath 为空表示新建；标题来自正文第一行，标题变化会重命名文件。
    // 正文和样式文件一起保存；空样式不会生成 sidecar。
    bool SaveNote(const std::wstring& relPath, const std::wstring& groupRel,
                  const std::string& bytes, const std::wstring& title,
                  const StyleModel& styles, std::wstring& savedRel,
                  std::wstring& error);
    bool SaveNote(const std::wstring& relPath, const std::wstring& groupRel,
                  const std::string& bytes, const std::wstring& title,
                  std::wstring& savedRel, std::wstring& error);
    bool SaveNoteAutosave(const std::wstring& relPath, const std::string& bytes,
                          const StyleModel& styles, std::wstring& error);
    void RemoveNoteAutosave(const std::wstring& relPath);
    bool HasNoteAutosave(const std::wstring& relPath) const;
    bool LoadNoteAutosave(const std::wstring& relPath, std::string& text,
                          StyleModel& styles, std::wstring& warning) const;
    bool MoveNote(const std::wstring& relPath, const std::wstring& newGroupRel,
                  std::wstring& newRel, std::wstring& error);
    bool DeleteNote(const std::wstring& relPath, std::wstring& error,
                    NoteDeleteMode mode = NoteDeleteMode::RecycleBin);

    static std::wstring DeriveTitle(const std::wstring& text);

private:
    static std::wstring SanitizeName(const std::wstring& name);
    std::wstring AbsolutePath(const std::wstring& relPath) const;

    std::wstring dataDir_;
};
