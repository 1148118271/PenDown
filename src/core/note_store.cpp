#include "core/note_store.h"

#include "core/note_storage.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace {

std::wstring Trim(const std::wstring& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && iswspace(text[begin])) ++begin;
    while (end > begin && iswspace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

std::wstring Lower(std::wstring text) {
    for (wchar_t& ch : text) ch = (wchar_t)towlower(ch);
    return text;
}

std::wstring JoinRelative(const std::wstring& parent, const std::wstring& name) {
    if (parent.empty()) return name;
    return parent + L"\\" + name;
}

bool SamePath(const fs::path& a, const fs::path& b) {
    return Lower(a.lexically_normal().wstring()) == Lower(b.lexically_normal().wstring());
}

// Windows 文件名不区分大小写，直接 fs::rename("Work", "work") 不会改变大小写；
// 用临时名做两步重命名，保证大小写修改也能落盘。
bool RenamePath(const fs::path& source, const fs::path& target, std::wstring& error) {
    if (source.filename() == target.filename()) return true;
    std::error_code ec;
    if (!SamePath(source, target)) {
        fs::rename(source, target, ec);
        if (ec) {
            error = L"重命名失败：" + target.wstring();
            return false;
        }
        return true;
    }

    fs::path temp = source.parent_path() /
                    (source.filename().wstring() + L".pendown-rn-" +
                     std::to_wstring(GetTickCount64()));
    fs::rename(source, temp, ec);
    if (ec) {
        error = L"重命名失败：" + source.wstring();
        return false;
    }
    fs::rename(temp, target, ec);
    if (ec) {
        std::error_code ignored;
        fs::rename(temp, source, ignored);
        error = L"重命名失败：" + target.wstring();
        return false;
    }
    return true;
}

bool IsSafeRelative(const std::wstring& relPath) {
    if (relPath.empty()) return true;
    fs::path path(relPath);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const fs::path& part : path) {
        if (part == L"..") return false;
    }
    return true;
}

std::wstring UniqueName(const fs::path& directory, const std::wstring& base,
                        const std::wstring& extension) {
    fs::path candidate = directory / (base + extension);
    if (!fs::exists(candidate)) return base;
    for (int index = 2; index < 10000; ++index) {
        std::wstring name = base + L" (" + std::to_wstring(index) + L")";
        candidate = directory / (name + extension);
        if (!fs::exists(candidate)) return name;
    }
    return base + L" (" + std::to_wstring(GetTickCount64()) + L")";
}

bool IsReservedFileName(const std::wstring& name) {
    std::wstring upper = Lower(name);
    std::wstring stem = upper;
    size_t dot = stem.find(L'.');
    if (dot != std::wstring::npos) stem = stem.substr(0, dot);

    const wchar_t* reserved[] = {L"con", L"prn", L"aux", L"nul", L"com1", L"com2", L"com3",
                                 L"com4", L"com5", L"com6", L"com7", L"com8", L"com9",
                                 L"lpt1", L"lpt2", L"lpt3", L"lpt4", L"lpt5", L"lpt6",
                                 L"lpt7", L"lpt8", L"lpt9"};
    for (const wchar_t* item : reserved) {
        if (stem == item) return true;
    }
    return false;
}

// 标题没变时，当前文件名可能只是上次重名时生成的 “标题 (2).txt”。
// 判断 stem 是否等于 base 或 base + " (n)"，避免同名便签每次保存都换成新的 (n)。
bool IsSameTitleVariant(const std::wstring& stem, const std::wstring& base) {
    const std::wstring lowerStem = Lower(stem);
    const std::wstring lowerBase = Lower(base);
    if (lowerStem == lowerBase) return true;

    const std::wstring prefix = lowerBase + L" (";
    if (lowerStem.size() <= prefix.size() + 1) return false;
    if (lowerStem.compare(0, prefix.size(), prefix) != 0) return false;
    if (lowerStem.back() != L')') return false;

    const std::wstring digits =
        lowerStem.substr(prefix.size(), lowerStem.size() - prefix.size() - 1);
    for (wchar_t ch : digits) {
        if (ch < L'0' || ch > L'9') return false;
    }
    return true;
}

std::vector<fs::path> ExistingPaths(const std::vector<fs::path>& candidates) {
    std::vector<fs::path> existing;
    existing.reserve(candidates.size());
    for (const fs::path& path : candidates) {
        std::error_code ec;
        if (!path.empty() && fs::exists(path, ec)) existing.push_back(path);
    }
    return existing;
}

#ifdef _WIN32
// 网络位置没有回收站；提前拒绝，避免 SHFileOperation 静默永久删除。
bool IsRecycleBinAvailable(const fs::path& path, std::wstring& error) {
    const std::wstring root = path.root_path().wstring();
    if (root.empty()) {
        error = L"无法确定删除位置所在的磁盘。";
        return false;
    }
    const UINT type = ::GetDriveTypeW(root.c_str());
    if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) return true;
    error = L"当前位置不在本地磁盘上，回收站不可用；请在资源管理器中手动删除。";
    return false;
}

// 把一组文件/目录一起移到回收站，保证正文和样式 sidecar 可以一起还原。
// 失败时不会删除任何内容，由调用方决定是否提示用户手动处理。
bool MoveToRecycleBin(const std::vector<fs::path>& paths, std::wstring& error) {
    if (paths.empty()) return true;
    for (const fs::path& path : paths) {
        if (!IsRecycleBinAvailable(path, error)) return false;
    }

    std::wstring from;
    for (const fs::path& path : paths) {
        from += path.wstring();
        from.push_back(L'\0');
    }
    from.push_back(L'\0');

    SHFILEOPSTRUCTW operation = {};
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.c_str();
    operation.fFlags = static_cast<FILEOP_FLAGS>(
        FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI);
    const int result = ::SHFileOperationW(&operation);
    if (result == 0 && !operation.fAnyOperationsAborted) return true;

    error = result == 0 ? L"删除操作已取消"
                        : L"移入回收站失败（错误代码 " + std::to_wstring(result) + L"）";
    return false;
}
#endif

}  // namespace

bool NoteStore::Open(const std::wstring& dataDir, std::wstring& error) {
    std::error_code ec;
    fs::path requested(dataDir);
    if (requested.empty()) {
        error = L"数据目录为空";
        return false;
    }

    fs::create_directories(requested, ec);
    if (ec) {
        error = L"无法创建数据目录：" + requested.wstring();
        return false;
    }

    fs::path absolute = fs::absolute(requested, ec);
    if (ec) absolute = requested;
    dataDir_ = absolute.lexically_normal().wstring();
    while (!dataDir_.empty() &&
           (dataDir_.back() == L'\\' || dataDir_.back() == L'/')) {
        dataDir_.pop_back();
    }
    return true;
}

std::wstring NoteStore::AbsolutePath(const std::wstring& relPath) const {
    if (relPath.empty()) return dataDir_;
    return (fs::path(dataDir_) / fs::path(relPath)).wstring();
}

bool NoteStore::ListTree(std::vector<NoteGroupEntry>& groups, std::vector<NoteFileEntry>& notes,
                         std::wstring& error) const {
    groups.clear();
    notes.clear();
    if (dataDir_.empty()) {
        error = L"便签数据目录未初始化";
        return false;
    }

    struct ScannedNote {
        NoteFileEntry entry;
        fs::file_time_type modified{};
    };
    std::vector<ScannedNote> scannedNotes;

    std::function<bool(const fs::path&, const std::wstring&)> scan =
        [&](const fs::path& directory, const std::wstring& relDir) -> bool {
        std::error_code ec;
        fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied,
                                        ec);
        if (ec) {
            error = L"无法读取目录：" + directory.wstring();
            return false;
        }

        for (const fs::directory_entry& entry : iterator) {
            std::error_code entryError;
            if (entry.is_symlink(entryError)) continue;
            std::wstring name = entry.path().filename().wstring();
            if (name.empty() || name[0] == L'.') continue;

            if (entry.is_directory(entryError)) {
                // 旧版任务附件目录不再作为分组显示
                if (relDir.empty() && Lower(name) == L"attachments") continue;
                std::wstring childRel = JoinRelative(relDir, name);
                NoteGroupEntry group;
                group.rel_path = childRel;
                group.parent_path = relDir;
                group.name = name;
                groups.push_back(std::move(group));
                if (!scan(entry.path(), childRel)) return false;
                continue;
            }

            if (!entry.is_regular_file(entryError)) continue;
            if (Lower(entry.path().extension().wstring()) != L".txt") continue;

            ScannedNote note;
            note.entry.rel_path = JoinRelative(relDir, name);
            note.entry.group_path = relDir;
            note.entry.title = entry.path().stem().wstring();
            note.modified = entry.last_write_time(entryError);
            scannedNotes.push_back(std::move(note));
        }
        return true;
    };

    if (!scan(fs::path(dataDir_), L"")) return false;

    std::vector<std::wstring> groupKeys;
    groupKeys.reserve(groups.size());
    for (const NoteGroupEntry& group : groups) groupKeys.push_back(Lower(group.rel_path));
    std::vector<size_t> groupOrder(groups.size());
    for (size_t i = 0; i < groupOrder.size(); ++i) groupOrder[i] = i;
    std::sort(groupOrder.begin(), groupOrder.end(),
              [&](size_t a, size_t b) { return groupKeys[a] < groupKeys[b]; });
    if (!groupOrder.empty()) {
        std::vector<NoteGroupEntry> sortedGroups;
        sortedGroups.reserve(groups.size());
        for (size_t index : groupOrder) sortedGroups.push_back(std::move(groups[index]));
        groups = std::move(sortedGroups);
    }
    std::sort(scannedNotes.begin(), scannedNotes.end(),
              [](const ScannedNote& a, const ScannedNote& b) {
                  return a.modified > b.modified;
              });

    notes.reserve(scannedNotes.size());
    for (ScannedNote& note : scannedNotes) notes.push_back(std::move(note.entry));
    return true;
}

bool NoteStore::CreateGroup(const std::wstring& parentRel, const std::wstring& name,
                            std::wstring& createdRel, std::wstring& error) {
    if (!IsSafeRelative(parentRel)) {
        error = L"分组路径无效";
        return false;
    }
    std::wstring clean = SanitizeName(name);
    if (clean.empty()) {
        error = L"分组名称不能为空";
        return false;
    }

    std::error_code ec;
    fs::path parent = parentRel.empty() ? fs::path(dataDir_) : fs::path(AbsolutePath(parentRel));
    if (!parentRel.empty() && !fs::is_directory(parent, ec)) {
        error = L"上级分组不存在";
        return false;
    }
    fs::create_directories(parent, ec);
    if (ec) {
        error = L"无法创建上级分组目录";
        return false;
    }

    std::wstring actual = UniqueName(parent, clean, L"");
    fs::path target = parent / actual;
    if (!fs::create_directory(target, ec) || ec) {
        error = L"创建分组目录失败：" + target.wstring();
        return false;
    }
    createdRel = JoinRelative(parentRel, actual);
    return true;
}

bool NoteStore::RenameGroup(const std::wstring& relPath, const std::wstring& newName,
                            std::wstring& newRel, std::wstring& error) {
    if (!IsSafeRelative(relPath) || relPath.empty()) {
        error = L"分组路径无效";
        return false;
    }
    std::wstring clean = SanitizeName(newName);
    if (clean.empty()) {
        error = L"分组名称不能为空";
        return false;
    }

    fs::path current = fs::path(AbsolutePath(relPath));
    std::error_code ec;
    if (!fs::is_directory(current, ec)) {
        error = L"分组不存在";
        return false;
    }
    fs::path parent = current.parent_path();
    fs::path target = parent / clean;
    if (!SamePath(current, target) && fs::exists(target)) {
        error = L"同名分组已存在";
        return false;
    }
    if (!RenamePath(current, target, error)) return false;

    std::wstring parentRel = fs::path(relPath).parent_path().wstring();
    newRel = JoinRelative(parentRel, clean);
    return true;
}

bool NoteStore::MoveGroup(const std::wstring& relPath, const std::wstring& newParentRel,
                          std::wstring& newRel, std::wstring& error) {
    if (!IsSafeRelative(relPath) || relPath.empty() || !IsSafeRelative(newParentRel)) {
        error = L"分组路径无效";
        return false;
    }

    const std::wstring targetParentRel = newParentRel;
    const std::wstring currentParentRel = fs::path(relPath).parent_path().wstring();
    if (Lower(targetParentRel) == Lower(currentParentRel)) {
        error = L"分组已经在该分组下";
        return false;
    }

    std::error_code ec;
    fs::path source(AbsolutePath(relPath));
    fs::path targetParent = targetParentRel.empty() ? fs::path(dataDir_)
                                                    : fs::path(AbsolutePath(targetParentRel));
    if (!fs::is_directory(source, ec)) {
        error = L"分组不存在";
        return false;
    }
    if (!fs::is_directory(targetParent, ec)) {
        error = L"目标分组不存在";
        return false;
    }

    const std::wstring sourcePath = Lower(source.lexically_normal().wstring());
    const std::wstring parentPath = Lower(targetParent.lexically_normal().wstring());
    if (parentPath == sourcePath ||
        (parentPath.size() > sourcePath.size() &&
         parentPath.compare(0, sourcePath.size(), sourcePath) == 0 &&
         (parentPath[sourcePath.size()] == L'\\' || parentPath[sourcePath.size()] == L'/'))) {
        error = L"不能把分组移动到自身或它的子分组下";
        return false;
    }

    const std::wstring name = source.filename().wstring();
    fs::path target = targetParent / name;
    if (fs::exists(target, ec)) {
        error = L"目标分组下已存在同名分组";
        return false;
    }

    fs::rename(source, target, ec);
    if (ec) {
        error = L"移动分组失败：" + target.wstring();
        return false;
    }
    newRel = JoinRelative(targetParentRel, name);
    return true;
}

bool NoteStore::DeleteGroup(const std::wstring& relPath, std::wstring& error,
                            NoteDeleteMode mode) {
    if (!IsSafeRelative(relPath) || relPath.empty()) {
        error = L"分组路径无效";
        return false;
    }

    std::error_code ec;
    fs::path target = fs::path(AbsolutePath(relPath));
    if (!fs::exists(target, ec)) return true;

#ifdef _WIN32
    if (mode == NoteDeleteMode::RecycleBin) {
        return MoveToRecycleBin({target}, error);
    }
#else
    (void)mode;
#endif

    fs::remove_all(target, ec);
    if (ec) {
        error = L"删除分组目录失败：" + target.wstring();
        return false;
    }
    return true;
}

bool NoteStore::ReadNote(const std::wstring& relPath, std::string& bytes,
                         std::wstring& error) const {
    if (!IsSafeRelative(relPath) || relPath.empty()) {
        error = L"便签路径无效";
        return false;
    }
    return NoteStorage::ReadTextFile(AbsolutePath(relPath), bytes, error);
}

bool NoteStore::LoadNoteDocument(const std::wstring& relPath, std::string& text,
                                 StyleModel& styles, std::wstring& warning,
                                 std::wstring& error) const {
    if (!IsSafeRelative(relPath) || relPath.empty()) {
        error = L"便签路径无效";
        return false;
    }
    if (!NoteStorage::Load(AbsolutePath(relPath), text, styles, warning)) {
        error = L"读取便签失败：" + relPath;
        return false;
    }
    return true;
}

bool NoteStore::SaveNote(const std::wstring& relPath, const std::wstring& groupRel,
                         const std::string& bytes, const std::wstring& title,
                         std::wstring& savedRel, std::wstring& error) {
    StyleModel empty;
    return SaveNote(relPath, groupRel, bytes, title, empty, savedRel, error);
}

bool NoteStore::SaveNote(const std::wstring& relPath, const std::wstring& groupRel,
                         const std::string& bytes, const std::wstring& title,
                         const StyleModel& styles, std::wstring& savedRel,
                         std::wstring& error) {
    if (!IsSafeRelative(relPath) || !IsSafeRelative(groupRel)) {
        error = L"便签路径无效";
        return false;
    }

    const std::wstring groupPath = groupRel;
    std::error_code ec;
    fs::path groupDir = groupPath.empty() ? fs::path(dataDir_)
                                          : fs::path(AbsolutePath(groupPath));
    if (!fs::is_directory(groupDir, ec)) {
        fs::create_directories(groupDir, ec);
        if (ec) {
            error = L"分组目录不存在：" + groupPath;
            return false;
        }
    }

    std::wstring base = SanitizeName(title.empty() ? L"无标题便签" : title);
    if (base.empty()) base = L"无标题便签";
    const std::wstring extension = L".txt";
    const std::wstring lowerBase = Lower(base);
    if (lowerBase.size() > extension.size() &&
        lowerBase.substr(lowerBase.size() - extension.size()) == extension) {
        base = base.substr(0, base.size() - extension.size());
    }
    base = Trim(base);
    if (base.empty()) base = L"无标题便签";

    fs::path current = relPath.empty() ? fs::path() : fs::path(AbsolutePath(relPath));
    bool currentExists = !current.empty() && fs::is_regular_file(current, ec);

    fs::path desired = groupDir / (base + extension);
    fs::path target = desired;
    bool caseOnlyRename = false;
    const bool currentInGroup =
        currentExists && SamePath(current.parent_path(), groupDir);
    if (currentExists && SamePath(desired, current)) {
        // 先写回原文件，写完后再单独处理大小写改名。
        caseOnlyRename =
            desired.filename().wstring() != current.filename().wstring();
        target = current;
    } else if (currentInGroup &&
               IsSameTitleVariant(current.stem().wstring(), base)) {
        // 标题没变，当前文件名只是上次重名时自动生成的 “标题 (2).txt”：
        // 继续沿用，避免每次保存都换成 “标题 (3)”“标题 (4)”。
        target = current;
    } else if (fs::exists(desired, ec)) {
        std::wstring unique = UniqueName(groupDir, base, extension);
        desired = groupDir / (unique + extension);
        target = desired;
    }

    if (!NoteStorage::Save(target.wstring(), bytes, styles, error)) return false;

    if (caseOnlyRename) {
        const std::wstring oldStyle = NoteStorage::StylePath(target.wstring());
        if (!RenamePath(target, desired, error)) return false;
        target = desired;
        // 样式 sidecar 跟随正文一起改大小写。
        if (fs::exists(fs::path(oldStyle), ec)) {
            const std::wstring newStyle =
                NoteStorage::StylePath(target.wstring());
            RenamePath(fs::path(oldStyle), fs::path(newStyle), error);
        }
    } else if (currentExists && !SamePath(current, target)) {
        const std::wstring oldStyle = NoteStorage::StylePath(current.wstring());
        fs::remove(current, ec);
        if (ec) {
            error = L"旧便签文件删除失败：" + current.wstring();
            return false;
        }
        fs::remove(fs::path(oldStyle), ec);
    }

    fs::path relative = target.lexically_relative(fs::path(dataDir_));
    savedRel = relative.wstring();
    return true;
}

bool NoteStore::SaveNoteAutosave(const std::wstring& relPath,
                                 const std::string& bytes,
                                 const StyleModel& styles,
                                 std::wstring& error) {
    if (!IsSafeRelative(relPath) || relPath.empty()) {
        error = L"便签路径无效";
        return false;
    }
    return NoteStorage::SaveAutosave(AbsolutePath(relPath), bytes, styles, error);
}

void NoteStore::RemoveNoteAutosave(const std::wstring& relPath) {
    if (!IsSafeRelative(relPath) || relPath.empty()) return;
    NoteStorage::RemoveAutosave(AbsolutePath(relPath));
}

bool NoteStore::HasNoteAutosave(const std::wstring& relPath) const {
    if (!IsSafeRelative(relPath) || relPath.empty()) return false;
    return NoteStorage::HasAutosave(AbsolutePath(relPath));
}

bool NoteStore::LoadNoteAutosave(const std::wstring& relPath, std::string& text,
                                 StyleModel& styles, std::wstring& warning) const {
    if (!IsSafeRelative(relPath) || relPath.empty()) return false;
    return NoteStorage::LoadAutosave(AbsolutePath(relPath), text, styles, warning);
}

bool NoteStore::MoveNote(const std::wstring& relPath, const std::wstring& newGroupRel,
                         std::wstring& newRel, std::wstring& error) {
    if (!IsSafeRelative(relPath) || relPath.empty() || !IsSafeRelative(newGroupRel)) {
        error = L"便签路径无效";
        return false;
    }

    const std::wstring targetGroupRel = newGroupRel;
    const std::wstring currentGroupRel = fs::path(relPath).parent_path().wstring();
    if (Lower(targetGroupRel) == Lower(currentGroupRel)) {
        error = L"便签已经在该分组下";
        return false;
    }

    std::error_code ec;
    fs::path source(AbsolutePath(relPath));
    fs::path targetGroup = targetGroupRel.empty() ? fs::path(dataDir_)
                                                  : fs::path(AbsolutePath(targetGroupRel));
    if (!fs::is_regular_file(source, ec)) {
        error = L"便签文件不存在";
        return false;
    }
    if (!fs::is_directory(targetGroup, ec)) {
        error = L"目标分组不存在";
        return false;
    }

    const std::wstring name = source.filename().wstring();
    fs::path target = targetGroup / name;
    if (fs::exists(target, ec)) {
        error = L"目标分组下已存在同名便签";
        return false;
    }

    fs::rename(source, target, ec);
    if (ec) {
        error = L"移动便签失败：" + target.wstring();
        return false;
    }

    const fs::path sourceStyle(NoteStorage::StylePath(source.wstring()));
    if (fs::is_regular_file(sourceStyle, ec)) {
        const fs::path targetStyle(NoteStorage::StylePath(target.wstring()));
        fs::rename(sourceStyle, targetStyle, ec);
        if (ec) {
            // 正文已经移动，尽量回滚，避免留下没有样式的半成品。
            std::error_code ignored;
            fs::rename(target, source, ignored);
            error = L"移动样式文件失败：" + targetStyle.wstring();
            return false;
        }
    }

    newRel = JoinRelative(targetGroupRel, name);
    return true;
}

bool NoteStore::DeleteNote(const std::wstring& relPath, std::wstring& error,
                           NoteDeleteMode mode) {
    if (!IsSafeRelative(relPath) || relPath.empty()) {
        error = L"便签路径无效";
        return false;
    }

    fs::path target = fs::path(AbsolutePath(relPath));
    const std::wstring stylePath = NoteStorage::StylePath(target.wstring());

    // 正文、样式、自动保存和上一版备份一起处理，避免删除后留下孤儿文件，
    // 也保证从回收站还原时样式仍然跟着正文。
    std::vector<fs::path> files = ExistingPaths({
        target,
        fs::path(stylePath),
        fs::path(NoteStorage::AutosavePath(target.wstring())),
        fs::path(NoteStorage::AutosaveStylePath(target.wstring())),
        fs::path(target.wstring() + L".tmp"),
        fs::path(stylePath + L".tmp"),
        fs::path(target.wstring() + L".bak"),
        fs::path(stylePath + L".bak"),
    });
    if (files.empty()) return true;

#ifdef _WIN32
    if (mode == NoteDeleteMode::RecycleBin) {
        return MoveToRecycleBin(files, error);
    }
#else
    (void)mode;
#endif

    bool ok = true;
    for (const fs::path& file : files) {
        std::error_code removeError;
        fs::remove(file, removeError);
        if (removeError && ok) {
            error = L"删除文件失败：" + file.wstring();
            ok = false;
        }
    }
    return ok;
}

std::wstring NoteStore::DeriveTitle(const std::wstring& text) {
    size_t start = 0;
    while (start <= text.size()) {
        const size_t newline = text.find(L'\n', start);
        std::wstring line = newline == std::wstring::npos
                                ? text.substr(start)
                                : text.substr(start, newline - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        line = Trim(line);
        if (!line.empty()) {
            // 纯文本正文：第一行就是标题，不再剥离 Markdown 标记。
            if (line.size() > 120) line.resize(120);
            return line;
        }
        if (newline == std::wstring::npos) break;
        start = newline + 1;
    }
    return L"无标题便签";
}

std::wstring NoteStore::SanitizeName(const std::wstring& name) {
    std::wstring out;
    out.reserve(name.size());
    for (wchar_t ch : name) {
        if (ch < 32 || wcschr(L"\\/:*?\"<>|", ch)) {
            out.push_back(L' ');
        } else {
            out.push_back(ch);
        }
    }

    while (!out.empty() && (iswspace(out.back()) || out.back() == L'.')) out.pop_back();
    while (!out.empty() && (iswspace(out.front()) || out.front() == L'.')) {
        out.erase(out.begin());
    }
    if (out.size() > 60) out.resize(60);
    while (!out.empty() && (iswspace(out.back()) || out.back() == L'.')) out.pop_back();
    if (out.empty()) return L"未命名";
    if (IsReservedFileName(out)) out += L"_";
    return out;
}
