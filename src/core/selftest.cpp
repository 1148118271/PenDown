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
        check(fs::is_directory(dataDir), L"data folder");
        check(store.DataDir() == dataDir.lexically_normal().wstring(), L"data dir path");

        std::vector<NoteGroupEntry> groups;
        std::vector<NoteFileEntry> notes;
        check(store.ListTree(groups, notes, error), L"list initial tree");
        check(groups.empty(), L"initial groups empty");
        check(notes.empty(), L"initial notes empty");

        std::wstring rootNote;
        std::wstring rootMarkdown = L"# 根便签\n\n根目录内容";
        check(store.SaveNote(L"", L"", ToUtf8(rootMarkdown), L"根便签",
                             NoteFormat::Markdown, rootNote, error),
              L"create root note");
        check(rootNote == L"根便签.md", L"root note path");
        check(fs::is_regular_file(dataDir / rootNote), L"root note file exists");
        std::string rootBytes;
        check(store.ReadNote(rootNote, rootBytes, error) &&
                  FromUtf8(rootBytes) == rootMarkdown,
              L"root note roundtrip");
        check(store.ListTree(groups, notes, error) && notes.size() == 1,
              L"list root note");
        check(!notes.empty() && notes[0].group_path.empty(), L"root note has no group");
        check(NoteStore::DeriveTitle(rootMarkdown) == L"根便签", L"derive root title");

        std::wstring workRel;
        check(store.CreateGroup(L"", L"工作", workRel, error), L"create root group");
        check(workRel == L"工作", L"root group relative path");
        check(fs::is_directory(dataDir / workRel), L"root group folder exists");

        std::wstring projectRel;
        check(store.CreateGroup(workRel, L"项目 A", projectRel, error),
              L"create child group");
        check(projectRel == L"工作\\项目 A", L"child group path");
        check(store.ListTree(groups, notes, error) && groups.size() == 2,
              L"list two groups");

        std::wstring markdown = L"# 第四季度汇报\n\n- 补充数据\n- 核对口径";
        std::wstring notePath;
        check(store.SaveNote(L"", projectRel, ToUtf8(markdown), L"第四季度汇报",
                             NoteFormat::Markdown, notePath, error),
              L"create markdown note");
        check(notePath == L"工作\\项目 A\\第四季度汇报.md", L"markdown note path");
        check(EndsWith(notePath, L".md"), L"markdown extension");
        check(fs::is_regular_file(dataDir / notePath), L"markdown file exists");

        std::string bytes;
        check(store.ReadNote(notePath, bytes, error) && FromUtf8(bytes) == markdown,
              L"markdown roundtrip");
        check(NoteStore::DeriveTitle(markdown) == L"第四季度汇报", L"derive heading title");
        check(NoteStore::DeriveTitle(L"- 第一项\n第二行") == L"第一项", L"derive list title");
        check(NoteStore::DeriveTitle(L"") == L"无标题便签", L"empty title");

        check(store.ListTree(groups, notes, error) && notes.size() == 2,
              L"list root and group notes");
        bool projectFound = false;
        for (const NoteFileEntry& note : notes) {
            if (note.rel_path == notePath && note.title == L"第四季度汇报" &&
                note.format == NoteFormat::Markdown) {
                projectFound = true;
            }
        }
        check(projectFound, L"note title and format in tree");

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
        check(store.DeleteNote(duplicatePath, error), L"delete duplicate note");

        std::wstring richPath;
        std::wstring richXml =
            L"<richtext version=\"1.0\"><paragraph><text>普通文本便签</text></paragraph></richtext>";
        check(store.SaveNote(L"", workRel, ToUtf8(richXml), L"普通文本便签",
                             NoteFormat::RichText, richPath, error),
              L"create rich text note");
        check(richPath == L"工作\\普通文本便签.wxr", L"rich text note path");
        check(EndsWith(richPath, L".wxr"), L"rich text extension");
        check(fs::is_regular_file(dataDir / richPath), L"rich text file exists");
        check(store.ListTree(groups, notes, error) && notes.size() == 3,
              L"list markdown and rich text");
        bool richFound = false;
        for (const NoteFileEntry& note : notes) {
            if (note.rel_path == richPath && note.format == NoteFormat::RichText) {
                richFound = true;
            }
        }
        check(richFound, L"rich text format in tree");

        std::wstring renamedGroup;
        check(store.RenameGroup(projectRel, L"项目 B", renamedGroup, error),
              L"rename group");
        check(renamedGroup == L"工作\\项目 B", L"renamed group path");
        check(fs::is_directory(dataDir / renamedGroup), L"renamed group folder exists");
        check(!fs::exists(dataDir / renamedPath), L"old note path no longer present");
        check(store.ListTree(groups, notes, error) && notes.size() == 3,
              L"notes moved with group");
        renamedPath = renamedGroup + renamedPath.substr(projectRel.size());

        std::wstring movedNote;
        check(store.MoveNote(renamedPath, workRel, movedNote, error), L"move note");
        check(movedNote == L"工作\\新标题.md", L"moved note path");
        check(!fs::exists(dataDir / renamedPath) &&
                  fs::is_regular_file(dataDir / movedNote),
              L"moved note file");
        std::wstring movedNoteBack;
        check(store.MoveNote(movedNote, renamedGroup, movedNoteBack, error) &&
                  movedNoteBack == renamedPath,
              L"move note back");

        std::wstring movedGroup;
        check(store.MoveGroup(renamedGroup, L"", movedGroup, error),
              L"move group to root");
        check(movedGroup == L"项目 B", L"moved group root path");
        check(fs::is_directory(dataDir / movedGroup), L"moved group root folder");
        std::wstring movedGroupBack;
        check(store.MoveGroup(movedGroup, workRel, movedGroupBack, error) &&
                  movedGroupBack == renamedGroup,
              L"move group back");
        check(fs::is_directory(dataDir / renamedGroup), L"group back folder");

        check(store.DeleteNote(richPath, error), L"delete rich text note");
        check(store.DeleteGroup(workRel, error), L"delete group tree");
        check(!fs::exists(dataDir / workRel), L"group folder removed");
        check(store.DeleteNote(rootNote, error), L"delete root note");
        check(store.ListTree(groups, notes, error) && groups.empty() && notes.empty(),
              L"tree after delete");    }

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
