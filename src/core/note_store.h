#pragma once

#include <string>
#include <vector>

// 文件系统里的一个分组目录。rel_path 相对 data 目录，例如「默认分组/工作」。
struct NoteGroupEntry {
    std::wstring rel_path;
    std::wstring parent_path;
    std::wstring name;
};

// 便签正文的两种存储格式：Markdown 或 wxRichText XML
enum class NoteFormat {
    Markdown,
    RichText,
};

// 文件系统里的一条便签文件：.md 或 .wxr
struct NoteFileEntry {
    std::wstring rel_path;
    std::wstring group_path;
    std::wstring title;
    NoteFormat format = NoteFormat::Markdown;
};

// 便签存储：data 目录下每个文件夹就是一个分组，每个文件就是一条便签。
// 正文保存为 Markdown（.md）或 wxRichText XML（.wxr），不经过 SQLite。
class NoteStore {
public:
    bool Open(const std::wstring& dataDir, std::wstring& error);

    const std::wstring& DataDir() const { return dataDir_; }
    const std::wstring& DefaultGroupPath() const { return defaultGroupPath_; }

    bool EnsureDefaultGroup(std::wstring& error);
    bool ListTree(std::vector<NoteGroupEntry>& groups, std::vector<NoteFileEntry>& notes,
                  std::wstring& error) const;

    bool CreateGroup(const std::wstring& parentRel, const std::wstring& name,
                     std::wstring& createdRel, std::wstring& error);
    bool RenameGroup(const std::wstring& relPath, const std::wstring& newName,
                     std::wstring& newRel, std::wstring& error);
    bool MoveGroup(const std::wstring& relPath, const std::wstring& newParentRel,
                   std::wstring& newRel, std::wstring& error);
    bool DeleteGroup(const std::wstring& relPath, std::wstring& error);

    bool ReadNote(const std::wstring& relPath, std::string& bytes, std::wstring& error) const;
    // relPath 为空表示新建；标题来自正文第一行，标题变化会重命名文件。
    bool SaveNote(const std::wstring& relPath, const std::wstring& groupRel,
                  const std::string& bytes, const std::wstring& title,
                  NoteFormat format, std::wstring& savedRel, std::wstring& error);
    bool MoveNote(const std::wstring& relPath, const std::wstring& newGroupRel,
                  std::wstring& newRel, std::wstring& error);
    bool DeleteNote(const std::wstring& relPath, std::wstring& error);

    static std::wstring DeriveTitle(const std::wstring& markdown);

private:
    static std::wstring SanitizeName(const std::wstring& name);
    std::wstring AbsolutePath(const std::wstring& relPath) const;

    std::wstring dataDir_;
    std::wstring defaultGroupPath_;
};
