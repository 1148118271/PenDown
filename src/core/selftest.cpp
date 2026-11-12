#include "core/selftest.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/note_store.h"

namespace fs = std::filesystem;

namespace {

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0,
                                     nullptr, nullptr);
    if (length <= 0) return std::string();
    std::string out((size_t)length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], length, nullptr,
                        nullptr);
    return out;
}

std::wstring FromUtf8(const std::string& bytes) {
    if (bytes.empty()) return std::wstring();
    int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (length <= 0) return std::wstring();
    std::wstring out((size_t)length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), &out[0], length);
    return out;
}

bool WriteReport(const std::wstring& path, const std::wstring& text) {
    std::ofstream file(fs::path(path), std::ios::binary | std::ios::trunc);
    if (!file) return false;
    const char bom[] = {'\xEF', '\xBB', '\xBF'};
    file.write(bom, sizeof(bom));
    std::string utf8 = ToUtf8(text);
    file.write(utf8.data(), (std::streamsize)utf8.size());
    return file.good();
}

bool EndsWith(const std::wstring& path, const std::wstring& suffix) {
    if (path.size() < suffix.size()) return false;
    std::wstring end = path.substr(path.size() - suffix.size());
    for (wchar_t& ch : end) ch = (wchar_t)towlower(ch);
    std::wstring want = suffix;
    for (wchar_t& ch : want) ch = (wchar_t)towlower(ch);
    return end == want;
}

}  // namespace

bool RunSelfTest(const std::wstring& reportPath) {
    std::vector<std::wstring> failures;
    int checks = 0;
    auto check = [&](bool condition, const wchar_t* name) {
        ++checks;
        if (!condition) failures.push_back(name);
    };

    std::error_code ec;
    fs::path tempRoot = fs::temp_directory_path(ec) /
                        (L"pendown-note-selftest-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(tempRoot, ec);
    fs::path dataDir = tempRoot / L"data";

    {
        NoteStore store;
        std::wstring error;
        check(store.Open(dataDir.wstring(), error), L"open store");
        check(fs::is_directory(dataDir / L"默认分组"), L"default group folder");

        std::vector<NoteGroupEntry> groups;
        std::vector<NoteFileEntry> notes;
        check(store.ListTree(groups, notes, error), L"list initial tree");
        check(groups.size() == 1 && groups[0].name == L"默认分组", L"initial default only");
        check(notes.empty(), L"initial notes empty");

        std::wstring workRel;
        check(store.CreateGroup(store.DefaultGroupPath(), L"工作", workRel, error),
              L"create group");
        check(workRel == L"默认分组\\工作", L"group relative path");
        check(fs::is_directory(dataDir / workRel), L"group folder exists");

        std::wstring projectRel;
        check(store.CreateGroup(workRel, L"项目 A", projectRel, error),
              L"create child group");
        check(store.ListTree(groups, notes, error) && groups.size() == 3,
              L"list three groups");

        std::wstring markdown = L"# 第四季度汇报\n\n- 补充数据\n- 核对口径";
        std::wstring notePath;
        check(store.SaveNote(L"", projectRel, ToUtf8(markdown), L"第四季度汇报",
                             NoteFormat::Markdown, notePath, error),
              L"create markdown note");
        check(EndsWith(notePath, L".md"), L"markdown extension");
        check(fs::is_regular_file(dataDir / notePath), L"markdown file exists");

        std::string bytes;
        check(store.ReadNote(notePath, bytes, error) && FromUtf8(bytes) == markdown,
              L"markdown roundtrip");
        check(NoteStore::DeriveTitle(markdown) == L"第四季度汇报", L"derive heading title");
        check(NoteStore::DeriveTitle(L"- 第一项\n第二行") == L"第一项", L"derive list title");
        check(NoteStore::DeriveTitle(L"") == L"无标题便签", L"empty title");

        check(store.ListTree(groups, notes, error) && notes.size() == 1, L"list one note");
        check(!notes.empty() && notes[0].title == L"第四季度汇报", L"note title in tree");
        check(!notes.empty() && notes[0].format == NoteFormat::Markdown,
              L"markdown format in tree");

        std::wstring renamedPath;
        std::wstring changed = L"## 新标题\n\n新的正文";
        check(store.SaveNote(notePath, projectRel, ToUtf8(changed), L"新标题",
                             NoteFormat::Markdown, renamedPath, error),
              L"rename note by first line");
        check(renamedPath != notePath, L"note path changed");
        check(!fs::exists(dataDir / notePath), L"old note file removed");
        check(fs::is_regular_file(dataDir / renamedPath), L"renamed note file exists");

        std::wstring duplicatePath;
        check(store.SaveNote(L"", projectRel, ToUtf8(changed), L"新标题",
                             NoteFormat::Markdown, duplicatePath, error),
              L"save duplicate title");
        check(duplicatePath != renamedPath, L"duplicate title unique");
        check(fs::is_regular_file(dataDir / duplicatePath), L"duplicate note file exists");
        check(store.DeleteNote(duplicatePath, error), L"delete duplicate note");

        std::wstring richPath;
        std::wstring richXml =
            L"<richtext version=\"1.0\"><paragraph><text>普通文本便签</text></paragraph></richtext>";
        check(store.SaveNote(L"", workRel, ToUtf8(richXml), L"普通文本便签",
                             NoteFormat::RichText, richPath, error),
              L"create rich text note");
        check(EndsWith(richPath, L".wxr"), L"rich text extension");
        check(fs::is_regular_file(dataDir / richPath), L"rich text file exists");
        check(store.ListTree(groups, notes, error) && notes.size() == 2,
              L"list markdown and rich text");
        bool richFound = false;
        for (const NoteFileEntry& note : notes) {
            if (note.rel_path == richPath && note.format == NoteFormat::RichText) richFound = true;
        }
        check(richFound, L"rich text format in tree");

        std::wstring renamedGroup;
        check(store.RenameGroup(projectRel, L"项目 B", renamedGroup, error),
              L"rename group");
        check(renamedGroup == L"默认分组\\工作\\项目 B", L"renamed group path");
        check(fs::is_directory(dataDir / renamedGroup), L"renamed group folder exists");
        check(!fs::exists(dataDir / renamedPath), L"old note path no longer present");
        check(store.ListTree(groups, notes, error) && notes.size() == 2,
              L"notes moved with group");
        // 分组改名后，便签文件也跟着换了上级目录
        renamedPath = renamedGroup + renamedPath.substr(projectRel.size());

        check(!store.RenameGroup(store.DefaultGroupPath(), L"X", renamedGroup, error),
              L"default group cannot rename");
        check(!store.DeleteGroup(store.DefaultGroupPath(), error),
              L"default group cannot delete");

        std::wstring movedNote;
        check(store.MoveNote(renamedPath, workRel, movedNote, error), L"move note");
        check(movedNote == L"默认分组\\工作\\新标题.md", L"moved note path");
        check(!fs::exists(dataDir / renamedPath) &&
                  fs::is_regular_file(dataDir / movedNote),
              L"moved note file");
        std::wstring movedNoteBack;
        check(store.MoveNote(movedNote, renamedGroup, movedNoteBack, error) &&
                  movedNoteBack == renamedPath,
              L"move note back");

        std::wstring movedGroup;
        check(store.MoveGroup(renamedGroup, store.DefaultGroupPath(), movedGroup, error),
              L"move group");
        check(movedGroup == L"默认分组\\项目 B", L"moved group path");
        check(fs::is_directory(dataDir / movedGroup), L"moved group folder");
        std::wstring movedGroupBack;
        check(store.MoveGroup(movedGroup, workRel, movedGroupBack, error) &&
                  movedGroupBack == renamedGroup,
              L"move group back");
        check(fs::is_directory(dataDir / renamedGroup), L"group back folder");

        check(store.DeleteNote(richPath, error), L"delete rich text note");
        check(store.DeleteGroup(workRel, error), L"delete group tree");
        check(!fs::exists(dataDir / workRel), L"group folder removed");
        check(store.ListTree(groups, notes, error) && groups.size() == 1 && notes.empty(),
              L"tree after delete");
    }

    fs::remove_all(tempRoot, ec);

    std::wstring report;
    if (failures.empty()) {
        report = L"OK checks=" + std::to_wstring(checks);
    } else {
        report = L"FAIL " + std::to_wstring(failures.size()) + L"/" +
                 std::to_wstring(checks) + L":";
        for (const auto& item : failures) report += L" " + item + L";";
    }
    WriteReport(reportPath, report);
    return failures.empty();
}
