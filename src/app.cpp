#include <wx/app.h>
#include <wx/config.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>

#include <memory>

#include "core/note_store.h"
#include "core/selftest.h"
#include "core/text.h"
#include "single_instance.h"
#include "ui/icon_library.h"
#include "ui/main_frame.h"

// PenDown：wxWidgets 的便签工具，便签以 .md / .wxr 文件保存在 data 目录。
//
// 启动参数（正常双击不需要任何参数）：
//   --data <dir>          指定便签数据目录（默认是 exe 同级的 data）
//   --selftest <report>   跑一遍文件存储自检
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

    wxString dataOverride;
    wxString selfTestReport;

    for (int i = 1; i < argc; ++i) {
        wxString arg = argv[i];
        if (arg == "--data" && i + 1 < argc) {
            dataOverride = argv[++i];
        } else if (arg == "--selftest" && i + 1 < argc) {
            selfTestReport = argv[++i];
        }
    }

    if (!selfTestReport.empty()) {
        RunSelfTest(ToW(selfTestReport));
        return false;
    }

#ifdef __WXMSW__
    if (!ClaimSingleInstance()) return false;
#endif

    wxFileName executable(wxStandardPaths::Get().GetExecutablePath());
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
            wxMessageBox(wxString(L"配置的数据目录不可用：\n") + dataDir +
                             L"\n\n已临时使用默认目录：\n" + defaultDataDir,
                         L"PenDown", wxOK | wxICON_WARNING);
            store = fallback;
            if (wxConfigBase* config = wxConfigBase::Get()) {
                config->Write("/Data/Directory", defaultDataDir);
                config->Flush();
            }
        } else {
            wxMessageBox(wxString(L"初始化便签目录失败：\n") + FromW(error) + L"\n\n" +
                             dataDir,
                         L"PenDown", wxOK | wxICON_ERROR);
            return false;
        }
    }

    auto* frame = new MainFrame(store);
    frame->Show();
    return true;
}
