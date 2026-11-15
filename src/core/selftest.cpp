#include "core/selftest.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "core/i18n.h"
#include "core/note_store.h"
#include "core/note_storage.h"
#include "core/style_model.h"

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

        NoteStore rootStore;
        check(!rootStore.Open(fs::path(dataDir).root_path().wstring(), error),
              L"reject drive root data folder");

        std::vector<NoteGroupEntry> groups;
        std::vector<NoteFileEntry> notes;
        check(store.ListTree(groups, notes, error), L"list initial tree");
        check(groups.empty(), L"initial groups empty");
        check(notes.empty(), L"initial notes empty");

        std::wstring rootNote;
        std::wstring rootMarkdown = L"根便签\n\n根目录内容";
        check(store.SaveNote(L"", L"", ToUtf8(rootMarkdown), L"根便签", rootNote,
                             error),
              L"create root note");
        check(rootNote == L"根便签.txt", L"root note path");
        check(fs::is_regular_file(dataDir / rootNote), L"root note file exists");
        std::string rootBytes;
        check(store.ReadNote(rootNote, rootBytes, error) &&
                  FromUtf8(rootBytes) == rootMarkdown,
              L"root note roundtrip");
        check(store.ListTree(groups, notes, error) && notes.size() == 1,
              L"list root note");
        check(!notes.empty() && notes[0].group_path.empty(), L"root note has no group");
        check(NoteStore::DeriveTitle(rootMarkdown) == L"根便签", L"derive root title");

        std::wstring caseNotePath;
        check(store.SaveNote(L"", L"", ToUtf8(rootMarkdown), L"CaseNote",
                             caseNotePath, error),
              L"create case note");
        std::wstring caseNoteRenamed;
        check(store.SaveNote(caseNotePath, L"", ToUtf8(rootMarkdown), L"casenote",
                             caseNoteRenamed, error),
              L"rename case-only note");
        check(fs::path(caseNoteRenamed).filename().wstring() == L"casenote.txt",
              L"case-only note on disk");
        check(store.DeleteNote(caseNoteRenamed, error, NoteDeleteMode::Permanent),
              L"delete case note");

        std::wstring caseGroup;
        check(store.CreateGroup(L"", L"CaseGroup", caseGroup, error),
              L"create case group");
        std::wstring caseRenamed;
        check(store.RenameGroup(caseGroup, L"casegroup", caseRenamed, error),
              L"rename case-only group");
        bool caseExact = false;
        for (const fs::directory_entry& entry : fs::directory_iterator(dataDir)) {
            if (entry.path().filename().wstring() == L"casegroup") caseExact = true;
        }
        check(caseExact, L"case-only group on disk");
        check(store.DeleteGroup(caseRenamed, error, NoteDeleteMode::Permanent),
              L"delete case group");

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

        std::wstring markdown = L"第四季度汇报\n\n补充数据\n核对口径";
        std::wstring notePath;
        check(store.SaveNote(L"", projectRel, ToUtf8(markdown), L"第四季度汇报",
                             notePath, error),
              L"create text note");
        check(notePath == L"工作\\项目 A\\第四季度汇报.txt", L"text note path");
        check(EndsWith(notePath, L".txt"), L"text extension");
        check(fs::is_regular_file(dataDir / notePath), L"text file exists");

        std::string bytes;
        check(store.ReadNote(notePath, bytes, error) && FromUtf8(bytes) == markdown,
              L"text roundtrip");
        check(NoteStore::DeriveTitle(markdown) == L"第四季度汇报", L"derive plain title");
        check(NoteStore::DeriveTitle(L"- 第一项\n第二行") == L"- 第一项",
              L"derive literal first line");
        check(NoteStore::DeriveTitle(
                  L"<span style=\"color:#E45649\">彩色标题</span>") ==
                  L"<span style=\"color:#E45649\">彩色标题</span>",
              L"derive literal colour line");
        check(NoteStore::DeriveTitle(L"") == L"无标题便签", L"empty title");

        // 样式 sidecar 的保存/加载/删除。
        StyleModel styled;
        TextStyle red;
        red.foreground = "#E45649";
        red.bold = true;
        const uint32_t redId = styled.Intern(red);
        styled.SetRange(0, 3, redId);
        // 链接地址单独保存，不占样式槽；两个不同 URL 共用同一个样式。
        TextStyle linkStyle = red;
        linkStyle.foreground = "#2563EB";
        linkStyle.underline = true;
        const uint32_t linkId = styled.Intern(linkStyle);
        styled.SetRange(3, 9, linkId);
        styled.Links().SetRange(3, 6, "https://example.com");
        styled.Links().SetRange(6, 9, "https://second.example");
        std::wstring styledPath;
        check(store.SaveNote(L"", L"", ToUtf8(L"红色标题\n正文"), L"样式便签",
                             styled, styledPath, error),
              L"create styled note");
        std::wstring styledJson =
            std::wstring(NoteStorage::StylePath(dataDir.wstring() + L"\\" +
                                                styledPath));
        check(fs::is_regular_file(styledJson), L"style sidecar exists");
        StyleModel loadedStyles;
        std::wstring styleWarning;
        std::string styledText;
        check(store.LoadNoteDocument(styledPath, styledText, loadedStyles,
                                     styleWarning, error) &&
                  FromUtf8(styledText) == L"红色标题\n正文" &&
                  !loadedStyles.Empty() &&
                  loadedStyles.GetStyle(loadedStyles.StyleIdAt(0)).foreground ==
                      "#E45649" &&
                  loadedStyles.Links().UrlAt(3) == "https://example.com" &&
                  loadedStyles.Links().UrlAt(7) == "https://second.example" &&
                  loadedStyles.StyleCount() == 3,
              L"style and link roundtrip");
        // 新格式把链接写在独立的 links 数组里，样式表里不再有 url。
        std::string styledJsonText;
        check(NoteStorage::ReadTextFile(styledJson, styledJsonText, error) &&
                  styledJsonText.find("\"links\"") != std::string::npos &&
                  styledJsonText.find("\"url\"") == std::string::npos,
              L"style json stores links separately");

        // 旧格式的 url 保存在样式表里；加载时应迁移成独立链接区间。
        const std::string legacyText = ToUtf8(L"红色标题\n正文");
        std::ostringstream legacyHash;
        legacyHash << std::hex << std::setw(16) << std::setfill('0')
                   << NoteStorage::HashText(legacyText);
        std::ostringstream legacyJson;
        legacyJson << "{\"version\":1,\"body\":{\"length\":"
                   << legacyText.size() << ",\"hash\":\"" << legacyHash.str()
                   << "\"},\"styles\":[{\"fg\":\"#E45649\","
                      "\"url\":\"https://old.example\"},"
                      "{\"b\":true,\"url\":\"https://old2.example\"}],"
                      "\"runs\":[[0,3,1],[3,9,2]]}";
        const fs::path legacyBody = dataDir / L"旧格式链接.txt";
        const fs::path legacyStyle =
            NoteStorage::StylePath(legacyBody.wstring());
        StyleModel migrated;
        std::wstring migratedWarning;
        std::string migratedText;
        check(NoteStorage::WriteTextFile(legacyBody.wstring(), legacyText,
                                         error) &&
                  NoteStorage::WriteTextFile(legacyStyle.wstring(),
                                             legacyJson.str(), error) &&
                  NoteStorage::Load(legacyBody.wstring(), migratedText, migrated,
                                    migratedWarning) &&
                  migrated.Links().UrlAt(1) == "https://old.example" &&
                  migrated.Links().UrlAt(4) == "https://old2.example" &&
                  !migrated.StyleAt(0).bold && migrated.StyleAt(3).bold,
              L"legacy url migrates to links");
        std::error_code legacyIgnored;
        fs::remove(legacyBody, legacyIgnored);
        fs::remove(legacyStyle, legacyIgnored);

        // 旧文件里两个只差 URL 的样式，迁移后应合并成同一个样式槽。
        std::ostringstream duplicateJson;
        duplicateJson << "{\"version\":1,\"body\":{\"length\":"
                      << legacyText.size() << ",\"hash\":\""
                      << legacyHash.str()
                      << "\"},\"styles\":[{\"fg\":\"#2563EB\",\"u\":true,"
                         "\"url\":\"https://a.example\"},"
                         "{\"fg\":\"#2563EB\",\"u\":true,"
                         "\"url\":\"https://b.example\"}],"
                         "\"runs\":[[0,3,1],[3,6,2]]}";
        const fs::path duplicateBody = dataDir / L"旧格式重复样式.txt";
        const fs::path duplicateStyle =
            NoteStorage::StylePath(duplicateBody.wstring());
        StyleModel deduped;
        std::wstring dedupedWarning;
        std::string dedupedText;
        check(NoteStorage::WriteTextFile(duplicateBody.wstring(), legacyText,
                                         error) &&
                  NoteStorage::WriteTextFile(duplicateStyle.wstring(),
                                             duplicateJson.str(), error) &&
                  NoteStorage::Load(duplicateBody.wstring(), dedupedText,
                                    deduped, dedupedWarning) &&
                  deduped.Links().UrlAt(0) == "https://a.example" &&
                  deduped.Links().UrlAt(3) == "https://b.example" &&
                  deduped.StyleCount() == 2 &&
                  deduped.StyleIdAt(0) == deduped.StyleIdAt(3),
              L"legacy duplicate styles dedupe");
        fs::remove(duplicateBody, legacyIgnored);
        fs::remove(duplicateStyle, legacyIgnored);

        std::wstring renamedStyled;
        check(store.SaveNote(styledPath, L"", ToUtf8(L"新样式\n正文"),
                             L"新样式便签", styled, renamedStyled, error),
              L"rename styled note");
        check(renamedStyled != styledPath && !fs::exists(styledJson),
              L"old style sidecar removed");
        styledPath = renamedStyled;
        std::wstring renamedJson =
            NoteStorage::StylePath(dataDir.wstring() + L"\\" + styledPath);
        check(fs::is_regular_file(renamedJson), L"renamed style sidecar");
        styledJson = renamedJson;
        check(store.SaveNoteAutosave(styledPath, ToUtf8(L"自动保存正文"), styled,
                                     error),
              L"write styled autosave");
        check(store.HasNoteAutosave(styledPath), L"styled autosave exists");
        std::wstring movedStyled;
        check(store.MoveNote(styledPath, projectRel, movedStyled, error),
              L"move styled note");
        check(fs::is_regular_file(dataDir /
                                  (movedStyled + L".style.json")),
              L"style sidecar moved");
        check(store.HasNoteAutosave(movedStyled) &&
                  !store.HasNoteAutosave(styledPath),
              L"autosave moved with note");
        check(store.MoveNote(movedStyled, L"", styledPath, error),
              L"move styled note back");
        check(store.HasNoteAutosave(styledPath) &&
                  !store.HasNoteAutosave(movedStyled),
              L"autosave moved back with note");
        check(store.DeleteNote(styledPath, error, NoteDeleteMode::Permanent),
              L"delete styled note");
        check(!fs::exists(styledJson), L"style sidecar removed");
        check(!store.HasNoteAutosave(styledPath), L"autosave removed with note");

        // 删除链接前面的正文时，链接区间必须整体左移；否则 URL 会覆盖错文字。
        LinkModel shiftedLinks;
        shiftedLinks.SetLinks(
            {LinkRun{0, 3, "https://before.example"},
             LinkRun{10, 15, "https://after.example"}},
            20);
        check(shiftedLinks.OnDelete(0, 2), L"link delete reports change");
        check(shiftedLinks.Runs().size() == 2 &&
                  shiftedLinks.Runs()[0].start == 0 &&
                  shiftedLinks.Runs()[0].end == 1 &&
                  shiftedLinks.Runs()[1].start == 8 &&
                  shiftedLinks.Runs()[1].end == 13 &&
                  shiftedLinks.UrlAt(8) == "https://after.example",
              L"links shift after delete before range");

        check(store.ListTree(groups, notes, error) && notes.size() == 2,
              L"list root and group notes");
        bool projectFound = false;
        for (const NoteFileEntry& note : notes) {
            if (note.rel_path == notePath && note.title == L"第四季度汇报") {
                projectFound = true;
            }
        }
        check(projectFound, L"note title and format in tree");

        std::wstring renamedPath;
        std::wstring changed = L"新标题\n\n新的正文";
        check(store.SaveNote(notePath, projectRel, ToUtf8(changed), L"新标题",
                             renamedPath, error),
              L"rename note by first line");
        check(renamedPath != notePath, L"note path changed");
        check(!fs::exists(dataDir / notePath), L"old note file removed");
        check(fs::is_regular_file(dataDir / renamedPath), L"renamed note file exists");

        std::wstring duplicatePath;
        check(store.SaveNote(L"", projectRel, ToUtf8(changed), L"新标题",
                             duplicatePath, error),
              L"save duplicate title");
        check(duplicatePath != renamedPath, L"duplicate title unique");
        std::wstring duplicateResaved;
        check(store.SaveNote(duplicatePath, projectRel, ToUtf8(changed), L"新标题",
                             duplicateResaved, error) &&
                  duplicateResaved == duplicatePath,
              L"duplicate title keeps existing path");
        check(store.DeleteNote(duplicatePath, error, NoteDeleteMode::Permanent),
              L"delete duplicate note");

        // 旧版 .wxr 不再作为便签列出，但文件本身必须保留。
        const fs::path legacyRich = dataDir / L"工作" / L"旧版便签.wxr";
        {
            std::ofstream legacy(legacyRich, std::ios::binary | std::ios::trunc);
            legacy << "legacy";
        }
        check(fs::is_regular_file(legacyRich), L"legacy wxr preserved");
        check(store.ListTree(groups, notes, error) && notes.size() == 2,
              L"legacy wxr ignored");

        std::wstring renamedGroup;
        check(store.RenameGroup(projectRel, L"项目 B", renamedGroup, error),
              L"rename group");
        check(renamedGroup == L"工作\\项目 B", L"renamed group path");
        check(fs::is_directory(dataDir / renamedGroup), L"renamed group folder exists");
        check(!fs::exists(dataDir / renamedPath), L"old note path no longer present");
        check(store.ListTree(groups, notes, error) && notes.size() == 2,
              L"notes moved with group");
        renamedPath = renamedGroup + renamedPath.substr(projectRel.size());

        std::wstring movedNote;
        check(store.MoveNote(renamedPath, workRel, movedNote, error), L"move note");
        check(movedNote == L"工作\\新标题.txt", L"moved note path");
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

        check(store.DeleteGroup(workRel, error, NoteDeleteMode::Permanent),
              L"delete group tree");
        check(!fs::exists(dataDir / workRel), L"group folder removed");
        check(store.DeleteNote(rootNote, error, NoteDeleteMode::Permanent),
              L"delete root note");
        check(store.ListTree(groups, notes, error) && groups.empty() && notes.empty(),
              L"tree after delete");
    }

    const i18n::Language previousLanguage = i18n::Current();
    i18n::SetLanguage(i18n::Language::English);
    check(i18n::Tr("selftest.hello") == L"Hello", L"i18n english");
    check(i18n::Plural("count.notes", 1) == L"1 note", L"i18n singular");
    check(i18n::Plural("count.notes", 2) == L"2 notes", L"i18n plural");
    check(i18n::Plural("selftest.plural_format", 2) ==
              L"bad format %s / 2",
          L"i18n unsupported format stays literal");
    i18n::SetLanguage(i18n::Language::Chinese);
    check(i18n::Tr("selftest.hello") == L"你好", L"i18n chinese");
    check(i18n::Plural("count.notes", 2) == L"2 条", L"i18n chinese plural");
    i18n::SetLanguage(previousLanguage);

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
