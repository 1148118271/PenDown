#include "core/note_store.h"

#include "core/i18n.h"
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
            error = i18n::Tr("note_store.rename_failed") + target.wstring();
            return false;
        }
        return true;
    }

    fs::path temp = source.parent_path() /
                    (source.filename().wstring() + L".pendown-rn-" +
                     std::to_wstring(GetTickCount64()));
    fs::rename(source, temp, ec);
    if (ec) {
        error = i18n::Tr("note_store.rename_failed") + source.wstring();
        return false;
    }
    fs::rename(temp, target, ec);
    if (ec) {
        std::error_code ignored;
        fs::rename(temp, source, ignored);
        error = i18n::Tr("note_store.rename_failed") + target.wstring();
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
        error = i18n::Tr("note_store.cannot_determine_the_drive_for_the_deletion_targ");
        return false;
    }
    const UINT type = ::GetDriveTypeW(root.c_str());
    if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) return true;
    error = i18n::Tr("note_store.the_location_is_not_on_a_local_drive_so_the_recy");
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

    error = result == 0
                ? i18n::Tr("note_store.the_delete_operation_was_canceled")
                : i18n::Tr("note_store.failed_to_move_to_the_recycle_bin_error_code") +
                      std::to_wstring(result) + i18n::Tr("git_sync.text");
    return false;
}
#endif

}  // namespace

bool NoteStore::Open(const std::wstring& dataDir, std::wstring& error) {
    std::error_code ec;
    fs::path requested(dataDir);
    if (requested.empty()) {
        error = i18n::Tr("note_store.the_data_folder_is_empty");
        return false;
    }

    fs::create_directories(requested, ec);
    if (ec) {
        error = i18n::Tr("note_store.cannot_create_the_data_folder") +
                requested.wstring();
        return false;
    }

    fs::path absolute = fs::absolute(requested, ec);
    if (ec) absolute = requested;
    absolute = absolute.lexically_normal();
    if (absolute == absolute.root_path()) {
        // “C:\\” 去掉尾部分隔符会变成“C:”，即当前目录而不是根目录。
        error = i18n::Tr("note_store.the_data_folder_cannot_be_a_drive_root");
        return false;
    }
    dataDir_ = absolute.wstring();
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
        error = i18n::Tr("note_store.the_note_data_folder_is_not_initialized");
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
            error = i18n::Tr("git_sync.cannot_read_the_folder") +
                    directory.wstring();
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
        error = i18n::Tr("note_store.invalid_group_path");
        return false;
    }
    std::wstring clean = SanitizeName(name);
    if (clean.empty()) {
        error = i18n::Tr("note_store.the_group_name_cannot_be_empty");
        return false;
    }

    std::error_code ec;
    fs::path parent = parentRel.empty() ? fs::path(dataDir_) : fs::path(AbsolutePath(parentRel));
    if (!parentRel.empty() && !fs::is_directory(parent, ec)) {
        error = i18n::Tr("note_store.the_parent_group_does_not_exist");
        return false;
    }
    fs::create_directories(parent, ec);
    if (ec) {
        error = i18n::Tr("note_store.cannot_create_the_parent_group_folder");
        return false;
    }

    std::wstring actual = UniqueName(parent, clean, L"");
    fs::path target = parent / actual;
    if (!fs::create_directory(target, ec) || ec) {
        error = i18n::Tr("note_store.failed_to_create_the_group_folder") +
                target.wstring();
        return false;
    }
    createdRel = JoinRelative(parentRel, actual);
    return true;
}

bool NoteStore::RenameGroup(const std::wstring& relPath, const std::wstring& newName,
                            std::wstring& newRel, std::wstring& error) {
    if (!IsSafeRelative(relPath) || relPath.empty()) {
        error = i18n::Tr("note_store.invalid_group_path");
        return false;
    }
    std::wstring clean = SanitizeName(newName);
    if (clean.empty()) {
        error = i18n::Tr("note_store.the_group_name_cannot_be_empty");
        return false;
    }

    fs::path current = fs::path(AbsolutePath(relPath));
    std::error_code ec;
    if (!fs::is_directory(current, ec)) {
        error = i18n::Tr("note_store.the_group_does_not_exist");
        return false;
    }
    fs::path parent = current.parent_path();
    fs::path target = parent / clean;
    if (!SamePath(current, target) && fs::exists(target)) {
        error = i18n::Tr("note_store.a_group_with_the_same_name_already_exists");
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
        error = i18n::Tr("note_store.invalid_group_path");
        return false;
    }

    const std::wstring targetParentRel = newParentRel;
    const std::wstring currentParentRel = fs::path(relPath).parent_path().wstring();
    if (Lower(targetParentRel) == Lower(currentParentRel)) {
        error = i18n::Tr("note_store.the_group_is_already_in_that_group");
        return false;
    }

    std::error_code ec;
    fs::path source(AbsolutePath(relPath));
    fs::path targetParent = targetParentRel.empty() ? fs::path(dataDir_)
                                                    : fs::path(AbsolutePath(targetParentRel));
    if (!fs::is_directory(source, ec)) {
        error = i18n::Tr("note_store.the_group_does_not_exist");
        return false;
    }
    if (!fs::is_directory(targetParent, ec)) {
        error = i18n::Tr("note_store.the_target_group_does_not_exist");
        return false;
    }

    const std::wstring sourcePath = Lower(source.lexically_normal().wstring());
    const std::wstring parentPath = Lower(targetParent.lexically_normal().wstring());
    if (parentPath == sourcePath ||
        (parentPath.size() > sourcePath.size() &&
         parentPath.compare(0, sourcePath.size(), sourcePath) == 0 &&
         (parentPath[sourcePath.size()] == L'\\' || parentPath[sourcePath.size()] == L'/'))) {
        error = i18n::Tr("note_store.a_group_cannot_be_moved_into_itself_or_one_of_it");
        return false;
    }

    const std::wstring name = source.filename().wstring();
    fs::path target = targetParent / name;
    if (fs::exists(target, ec)) {
        error = i18n::Tr("note_store.a_group_with_the_same_name_already_exists_in_the");
        return false;
    }

    fs::rename(source, target, ec);
    if (ec) {
        error = i18n::Tr("note_store.failed_to_move_the_group") +
                target.wstring();
        return false;
    }
    newRel = JoinRelative(targetParentRel, name);
    return true;
}

bool NoteStore::DeleteGroup(const std::wstring& relPath, std::wstring& error,
                            NoteDeleteMode mode) {
    if (!IsSafeRelative(relPath) || relPath.empty()) {
        error = i18n::Tr("note_store.invalid_group_path");
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
        error = i18n::Tr("note_store.failed_to_delete_the_group_folder") +
                target.wstring();
        return false;
    }
    return true;
}

bool NoteStore::ReadNote(const std::wstring& relPath, std::string& bytes,
                         std::wstring& error) const {
    if (!IsSafeRelative(relPath) || relPath.empty()) {
        error = i18n::Tr("note_store.invalid_note_path");
        return false;
    }
    return NoteStorage::ReadTextFile(AbsolutePath(relPath), bytes, error);
}

bool NoteStore::LoadNoteDocument(const std::wstring& relPath, std::string& text,
                                 StyleModel& styles, std::wstring& warning,
                                 std::wstring& error) const {
    if (!IsSafeRelative(relPath) || relPath.empty()) {
        error = i18n::Tr("note_store.invalid_note_path");
        return false;
    }
    if (!NoteStorage::Load(AbsolutePath(relPath), text, styles, warning)) {
        error = i18n::Tr("note_store.failed_to_read_the_note") + relPath;
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
        error = i18n::Tr("note_store.invalid_note_path");
        return false;
    }

    const std::wstring groupPath = groupRel;
    std::error_code ec;
    fs::path groupDir = groupPath.empty() ? fs::path(dataDir_)
                                          : fs::path(AbsolutePath(groupPath));
    if (!fs::is_directory(groupDir, ec)) {
        fs::create_directories(groupDir, ec);
        if (ec) {
            error = i18n::Tr("note_store.the_group_folder_does_not_exist") +
                    groupPath;
            return false;
        }
    }

    std::wstring base = SanitizeName(title.empty() ? kDefaultNoteTitle : title);
    if (base.empty()) base = kDefaultNoteTitle;
    const std::wstring extension = L".txt";
    const std::wstring lowerBase = Lower(base);
    if (lowerBase.size() > extension.size() &&
        lowerBase.substr(lowerBase.size() - extension.size()) == extension) {
        base = base.substr(0, base.size() - extension.size());
    }
    base = Trim(base);
    if (base.empty()) base = kDefaultNoteTitle;

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
            error = i18n::Tr("note_store.failed_to_delete_the_old_note_file") +
                    current.wstring();
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
        error = i18n::Tr("note_store.invalid_note_path");
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
        error = i18n::Tr("note_store.invalid_note_path");
        return false;
    }

    const std::wstring targetGroupRel = newGroupRel;
    const std::wstring currentGroupRel = fs::path(relPath).parent_path().wstring();
    if (Lower(targetGroupRel) == Lower(currentGroupRel)) {
        error = i18n::Tr("note_store.the_note_is_already_in_that_group");
        return false;
    }

    std::error_code ec;
    fs::path source(AbsolutePath(relPath));
    fs::path targetGroup = targetGroupRel.empty() ? fs::path(dataDir_)
                                                  : fs::path(AbsolutePath(targetGroupRel));
    if (!fs::is_regular_file(source, ec)) {
        error = i18n::Tr("note_store.the_note_file_does_not_exist");
        return false;
    }
    if (!fs::is_directory(targetGroup, ec)) {
        error = i18n::Tr("note_store.the_target_group_does_not_exist");
        return false;
    }

    const std::wstring name = source.filename().wstring();
    fs::path target = targetGroup / name;
    if (fs::exists(target, ec)) {
        error = i18n::Tr("note_store.a_note_with_the_same_name_already_exists_in_the");
        return false;
    }

    fs::rename(source, target, ec);
    if (ec) {
        error = i18n::Tr("note_store.failed_to_move_the_note") +
                target.wstring();
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
            error = i18n::Tr("note_store.failed_to_move_the_style_file") +
                    targetStyle.wstring();
            return false;
        }
    }

    // 自动保存只是崩溃恢复用的 sidecar；随正文一起迁移。迁移失败时保留
    // 旧文件，不阻断正文移动，下一次自动保存会写到新路径。
    const auto moveSidecar = [](const std::wstring& from, const std::wstring& to) {
        std::error_code sidecarError;
        if (!fs::is_regular_file(fs::path(from), sidecarError)) return;
        fs::rename(fs::path(from), fs::path(to), sidecarError);
        if (!sidecarError) return;
        sidecarError.clear();
        fs::copy_file(fs::path(from), fs::path(to),
                      fs::copy_options::overwrite_existing, sidecarError);
        if (!sidecarError) {
            std::error_code ignored;
            fs::remove(fs::path(from), ignored);
        }
    };
    moveSidecar(NoteStorage::AutosavePath(source.wstring()),
                NoteStorage::AutosavePath(target.wstring()));
    moveSidecar(NoteStorage::AutosaveStylePath(source.wstring()),
                NoteStorage::AutosaveStylePath(target.wstring()));

    newRel = JoinRelative(targetGroupRel, name);
    return true;
}

bool NoteStore::DeleteNote(const std::wstring& relPath, std::wstring& error,
                           NoteDeleteMode mode) {
    if (!IsSafeRelative(relPath) || relPath.empty()) {
        error = i18n::Tr("note_store.invalid_note_path");
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
            error = i18n::Tr("note_store.failed_to_delete_the_file") +
                    file.wstring();
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
    return kDefaultNoteTitle;
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
    if (out.empty()) return kDefaultGroupName;
    if (IsReservedFileName(out)) out += L"_";
    return out;
}
