#include <wx/app.h>
#include <wx/config.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>

#include <memory>

#include <wx/file.h>

#include "core/i18n.h"
#include "core/note_store.h"
#include "core/selftest.h"
#include "core/text.h"
#include "single_instance.h"
#include "ui/dialogs.h"
#include "ui/icon_library.h"
#include "ui/language_config.h"
#include "ui/main_frame.h"
#include "ui/note_editor.h"
#include "ui/ui_text.h"

// PenDown：wxWidgets 的便签工具，便签正文以 .txt 保存在 data 目录，
// 样式保存在同名 .txt.style.json。
//
// 启动参数（正常双击不需要任何参数）：
//   --data <dir>              指定便签数据目录（默认是 exe 同级的 data）
//   --selftest <report>       跑一遍文件存储 / 样式自检
//   --editor-selftest <report> 跑一遍 wxStyledTextCtrl 编辑自检
class PenDownApp : public wxApp {
public:
    bool OnInit() override;
};

wxIMPLEMENT_APP(PenDownApp);

#ifdef __WXMSW__
namespace {

HANDLE g_singleInstanceMutex = nullptr;

HWND FindMainWindowForActivation() {
    HWND found = nullptr;
    ::EnumWindows(
        [](HWND window, LPARAM param) -> BOOL {
            if (::GetPropW(window, kPenDownMainWindowProperty) == nullptr) {
                return TRUE;
            }
            *reinterpret_cast<HWND*>(param) = window;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found;
}

// 返回 false 表示已有实例在运行，当前进程应退出。
bool ClaimSingleInstance() {
    // 句柄故意不关闭：进程退出时由系统自动释放。
    g_singleInstanceMutex = ::CreateMutexW(nullptr, FALSE, L"Local\\PenDown.SingleInstance");
    if (!g_singleInstanceMutex) return true;
    if (::GetLastError() != ERROR_ALREADY_EXISTS) return true;

    // 已有实例的主窗口可能还在创建中，稍等片刻再查找。
    HWND existing = nullptr;
    for (int attempt = 0; attempt < 40 && !existing; ++attempt) {
        existing = FindMainWindowForActivation();
        if (!existing) ::Sleep(50);
    }

    const UINT message = ::RegisterWindowMessageW(kPenDownShowMainMessage);
    if (message != 0) {
        if (existing) {
            // 先让已有实例恢复主窗口，再由当前进程把前台权限交给它。
            DWORD_PTR result = 0;
            ::SendMessageTimeoutW(existing, message, 0, 0, SMTO_ABORTIFHUNG, 1000, &result);
            ::SetForegroundWindow(existing);
        } else {
            // 主窗口还没建好时广播一次，已有实例创建窗口后会收到。
            ::PostMessageW(HWND_BROADCAST, message, 0, 0);
        }
    }
    return false;
}

}  // namespace
#endif

bool PenDownApp::OnInit() {
    SetVendorName("PenDown");
    SetAppName("PenDown");
    wxInitAllImageHandlers();

    wxFileName executable(wxStandardPaths::Get().GetExecutablePath());
    const std::wstring languageDir = ToW(executable.GetPathWithSep() + L"lang");
    i18n::LoadLanguageFile(languageDir + L"\\zh-CN.lang", i18n::Language::Chinese);
    i18n::LoadLanguageFile(languageDir + L"\\en-US.lang", i18n::Language::English);

    // 语言只影响界面文案；磁盘上的默认标题和文件名保持稳定。
    ApplyLanguageSetting(LoadLanguageSetting());

    wxString dataOverride;
    wxString selfTestReport;
    wxString editorSelfTestReport;
    wxString benchInput;
    wxString benchReport;

    for (int i = 1; i < argc; ++i) {
        wxString arg = argv[i];
        if (arg == "--data" && i + 1 < argc) {
            dataOverride = argv[++i];
        } else if (arg == "--selftest" && i + 1 < argc) {
            selfTestReport = argv[++i];
        } else if (arg == "--editor-selftest" && i + 1 < argc) {
            editorSelfTestReport = argv[++i];
        } else if (arg == "--editor-bench" && i + 2 < argc) {
            benchInput = argv[++i];
            benchReport = argv[++i];
        }
    }

    if (!selfTestReport.empty()) {
        RunSelfTest(ToW(selfTestReport));
        return false;
    }

    if (!editorSelfTestReport.empty()) {
        wxString report;
        RunNoteEditorSelfTest(report);
        wxFile file(editorSelfTestReport, wxFile::write);
        if (file.IsOpened()) {
            const char bom[] = {'\xEF', '\xBB', '\xBF'};
            file.Write(bom, sizeof(bom));
            const wxScopedCharBuffer utf8 = report.ToUTF8();
            file.Write(utf8.data(), utf8.length());
        }
        return false;
    }

    if (!benchInput.empty() && !benchReport.empty()) {
        RunNoteEditorBench(ToW(benchInput), ToW(benchReport));
        return false;
    }

#ifdef __WXMSW__
    if (!ClaimSingleInstance()) return false;
#endif

    const wxString defaultDataDir = executable.GetPathWithSep() + L"data";
    wxString dataDir = defaultDataDir;
    if (!dataOverride.empty()) {
        dataDir = dataOverride;
    } else if (wxConfigBase* config = wxConfigBase::Get()) {
        config->Read("/Data/Directory", &dataDir, defaultDataDir);
    }

    IconLibrary::Load(executable.GetPathWithSep() + L"icons");

    auto store = std::make_shared<NoteStore>();
    std::wstring error;
    if (!store->Open(ToW(dataDir), error)) {
        // 配置的目录可能位于已移除的磁盘；先回退到默认目录，保证程序还能启动。
        auto fallback = std::make_shared<NoteStore>();
        std::wstring fallbackError;
        if (dataOverride.empty() && !dataDir.IsSameAs(defaultDataDir, false) &&
            fallback->Open(ToW(defaultDataDir), fallbackError)) {
            dialogs::Message(
                nullptr,
                wxString(UiText("app.the_configured_data_folder_is_unavailable")) +
                    dataDir +
                    UiText("app.using_the_default_folder_for_now") +
                    defaultDataDir,
                L"PenDown", wxOK | wxICON_WARNING);
            store = fallback;
            if (wxConfigBase* config = wxConfigBase::Get()) {
                config->Write("/Data/Directory", defaultDataDir);
                config->Flush();
            }
        } else {
            dialogs::Message(
                nullptr,
                wxString(UiText("app.failed_to_initialize_the_notes_folder")) +
                    FromW(error) + L"\n\n" + dataDir,
                L"PenDown", wxOK | wxICON_ERROR);
            return false;
        }
    }

    auto* frame = new MainFrame(store);
    frame->Show();
    return true;
}
