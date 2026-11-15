#include "ui/git_config.h"

#include "core/i18n.h"
#include "core/text.h"

#include <wx/base64.h>
#include <wx/config.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <wincrypt.h>
#endif

namespace {

#ifdef __WXMSW__

wxString ProtectToken(const wxString& token) {
    if (token.empty()) return wxString();

    const wxScopedCharBuffer utf8 = token.ToUTF8();
    DATA_BLOB input = {};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(utf8.data()));
    input.cbData = (DWORD)utf8.length();

    DATA_BLOB output = {};
    if (!::CryptProtectData(&input, L"PenDown Git token", nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return wxString();
    }
    const wxString encoded = wxBase64Encode(output.pbData, output.cbData);
    ::LocalFree(output.pbData);
    return L"dpapi:" + encoded;
}

wxString UnprotectToken(const wxString& stored) {
    if (stored.empty()) return wxString();
    if (!stored.StartsWith(L"dpapi:")) return stored;  // 兼容旧版明文

    wxMemoryBuffer buffer = wxBase64Decode(stored.Mid(6));
    if (buffer.GetDataLen() == 0) return wxString();

    DATA_BLOB input = {};
    input.pbData = reinterpret_cast<BYTE*>(buffer.GetData());
    input.cbData = (DWORD)buffer.GetDataLen();
    DATA_BLOB output = {};
    if (!::CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return wxString();
    }
    const wxString token =
        wxString::FromUTF8(reinterpret_cast<const char*>(output.pbData), output.cbData);
    ::LocalFree(output.pbData);
    return token;
}

#else

wxString ProtectToken(const wxString& token) { return token; }
wxString UnprotectToken(const wxString& stored) { return stored; }

#endif

}  // namespace

GitSyncConfig LoadGitSyncConfig() {
    GitSyncConfig config;
    wxConfigBase* store = wxConfigBase::Get();
    if (!store) return config;

    wxString url;
    wxString username;
    wxString token;
    wxString branch = L"main";
    wxString gitExe;
    store->Read("/Git/RemoteUrl", &url, wxEmptyString);
    store->Read("/Git/Username", &username, wxEmptyString);
    store->Read("/Git/Token", &token, wxEmptyString);
    store->Read("/Git/Branch", &branch, L"main");
    store->Read("/Git/Exe", &gitExe, wxEmptyString);

    config.remoteUrl = ToW(url);
    config.username = ToW(username);
    config.token = ToW(UnprotectToken(token));
    config.branch = ToW(branch.empty() ? wxString(L"main") : branch);
    config.gitExe = ToW(gitExe);
    return config;
}

bool SaveGitSyncConfig(const GitSyncConfig& config, wxString& error) {
    wxConfigBase* store = wxConfigBase::Get();
    if (!store) {
        error = i18n::Tr("git_config.cannot_access_the_application_configuration");
        return false;
    }

    const wxString token = FromW(config.token);
    wxString storedToken;
    if (!token.empty()) {
        storedToken = ProtectToken(token);
        if (storedToken.empty()) {
            error = i18n::Tr("git_config.failed_to_encrypt_the_access_token_the_configura");
            return false;
        }
    }

    store->Write("/Git/RemoteUrl", FromW(config.remoteUrl));
    store->Write("/Git/Username", FromW(config.username));
    store->Write("/Git/Token", storedToken);
    store->Write("/Git/Branch",
                 config.branch.empty() ? wxString(L"main") : FromW(config.branch));
    store->Write("/Git/Exe", FromW(config.gitExe));
    if (!store->Flush()) {
        error = i18n::Tr("git_config.failed_to_save_the_git_configuration");
        return false;
    }
    return true;
}
