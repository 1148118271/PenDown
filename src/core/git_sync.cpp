#include "core/git_sync.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr DWORD kGitTimeoutMs = 10 * 60 * 1000;
constexpr int kKeepBackups = 5;

std::wstring Lower(std::wstring text) {
    for (wchar_t& ch : text) ch = (wchar_t)towlower(ch);
    return text;
}

std::wstring Trim(std::wstring text) {
    const auto isSpace = [](wchar_t ch) {
        return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
    };
    while (!text.empty() && isSpace(text.front())) text.erase(text.begin());
    while (!text.empty() && isSpace(text.back())) text.pop_back();
    return text;
}

std::wstring NormalizePath(const std::wstring& value) {
    std::error_code ec;
    fs::path path(value);
    fs::path absolute = fs::weakly_canonical(path, ec);
    if (ec) {
        ec.clear();
        absolute = fs::absolute(path, ec);
    }
    if (ec) absolute = path;
    return absolute.lexically_normal().wstring();
}

bool IsSameOrChild(const fs::path& child, const fs::path& parent) {
    const fs::path left = child.lexically_normal();
    const fs::path right = parent.lexically_normal();
    auto leftIt = left.begin();
    for (auto rightIt = right.begin(); rightIt != right.end(); ++rightIt, ++leftIt) {
        if (leftIt == left.end()) return false;
        if (Lower(leftIt->wstring()) != Lower(rightIt->wstring())) return false;
    }
    return true;
}

bool IsSafeDataDir(const fs::path& dir) {
    const fs::path normalized = dir.lexically_normal();
    return normalized.has_parent_path() && normalized != normalized.root_path();
}

bool IsDotGit(const fs::path& path) {
    return Lower(path.filename().wstring()) == L".git";
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                                              nullptr, 0, nullptr, nullptr);
    if (length <= 0) return std::string();
    std::string out((size_t)length, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], length,
                          nullptr, nullptr);
    return out;
}

std::wstring BytesToWide(const std::string& bytes) {
    if (bytes.empty()) return std::wstring();
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int length = ::MultiByteToWideChar(codePage, flags, bytes.data(), (int)bytes.size(),
                                       nullptr, 0);
    if (length <= 0) {
        codePage = CP_ACP;
        flags = 0;
        length = ::MultiByteToWideChar(codePage, flags, bytes.data(), (int)bytes.size(),
                                       nullptr, 0);
    }
    if (length <= 0) return std::wstring();
    std::wstring out((size_t)length, L'\0');
    ::MultiByteToWideChar(codePage, flags, bytes.data(), (int)bytes.size(), &out[0], length);
    return out;
}

std::string Base64Encode(const std::string& input) {
    static const char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((input.size() + 2) / 3 * 4);
    int value = 0;
    int bits = -6;
    for (unsigned char ch : input) {
        value = (value << 8) + ch;
        bits += 8;
        while (bits >= 0) {
            out.push_back(kTable[(value >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) out.push_back(kTable[((value << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4 != 0) out.push_back('=');
    return out;
}

std::wstring QuoteArg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;

    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
            continue;
        }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(ch);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::vector<wchar_t> BuildEnvironment(
    const std::vector<std::pair<std::wstring, std::wstring>>& overrides) {
    std::vector<std::wstring> entries;
    LPWCH block = ::GetEnvironmentStringsW();
    if (block) {
        for (LPWCH current = block; *current; current += std::wcslen(current) + 1) {
            entries.emplace_back(current);
        }
        ::FreeEnvironmentStringsW(block);
    }

    const auto isBlocked = [](const std::wstring& entry) {
        const size_t equals = entry.find(L'=');
        if (equals == std::wstring::npos) return false;
        const std::wstring key = Lower(entry.substr(0, equals));
        if (key == L"git_dir" || key == L"git_work_tree" || key == L"git_index_file" ||
            key == L"git_common_dir" || key == L"git_object_directory" ||
            key == L"git_alternate_object_directories" || key == L"git_config_count" ||
            key == L"git_config_parameters") {
            return true;
        }
        return key.rfind(L"git_config_key_", 0) == 0 ||
               key.rfind(L"git_config_value_", 0) == 0;
    };
    entries.erase(std::remove_if(entries.begin(), entries.end(), isBlocked), entries.end());

    for (const auto& [key, value] : overrides) {
        const std::wstring prefix = key + L"=";
        bool replaced = false;
        for (std::wstring& entry : entries) {
            if (entry.size() >= prefix.size() &&
                _wcsnicmp(entry.c_str(), prefix.c_str(), prefix.size()) == 0) {
                entry = prefix + value;
                replaced = true;
                break;
            }
        }
        if (!replaced) entries.push_back(prefix + value);
    }

    std::vector<wchar_t> result;
    for (const std::wstring& entry : entries) {
        result.insert(result.end(), entry.begin(), entry.end());
        result.push_back(L'\0');
    }
    result.push_back(L'\0');
    return result;
}

struct RawProcessResult {
    bool started = false;
    bool timedOut = false;
    DWORD exitCode = 1;
    std::string stdoutText;
    std::string stderrText;
    std::wstring error;
};

void ReadPipe(HANDLE pipe, std::string& output) {
    char buffer[4096];
    DWORD read = 0;
    while (::ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        output.append(buffer, read);
    }
}

RawProcessResult RunProcess(const std::wstring& exe,
                            const std::vector<std::wstring>& args,
                            const std::wstring& workDir,
                            const std::vector<std::pair<std::wstring, std::wstring>>& env) {
    RawProcessResult result;
    if (exe.empty()) {
        result.error = L"未找到 git.exe";
        return result;
    }

    SECURITY_ATTRIBUTES security = {};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE outRead = nullptr;
    HANDLE outWrite = nullptr;
    HANDLE errRead = nullptr;
    HANDLE errWrite = nullptr;
    if (!::CreatePipe(&outRead, &outWrite, &security, 0)) {
        result.error = L"无法创建 Git 输出管道。";
        return result;
    }
    if (!::CreatePipe(&errRead, &errWrite, &security, 0)) {
        ::CloseHandle(outRead);
        ::CloseHandle(outWrite);
        result.error = L"无法创建 Git 错误管道。";
        return result;
    }
    ::SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE nullInput = ::CreateFileW(L"NUL", GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                     OPEN_EXISTING, 0, nullptr);
    if (nullInput == INVALID_HANDLE_VALUE) nullInput = nullptr;

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullInput;
    startup.hStdOutput = outWrite;
    startup.hStdError = errWrite;

    std::wstring command = QuoteArg(exe);
    for (const std::wstring& arg : args) {
        command += L' ';
        command += QuoteArg(arg);
    }

    std::vector<wchar_t> environment = BuildEnvironment(env);
    PROCESS_INFORMATION process = {};
    const BOOL created = ::CreateProcessW(
        exe.c_str(), &command[0], nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        environment.empty() ? nullptr : environment.data(),
        workDir.empty() ? nullptr : workDir.c_str(), &startup, &process);

    ::CloseHandle(outWrite);
    ::CloseHandle(errWrite);
    if (nullInput) ::CloseHandle(nullInput);

    if (!created) {
        result.error = L"无法启动 git.exe（错误代码 " +
                       std::to_wstring((unsigned long)::GetLastError()) + L"）。";
        ::CloseHandle(outRead);
        ::CloseHandle(errRead);
        return result;
    }

    std::thread outThread(ReadPipe, outRead, std::ref(result.stdoutText));
    std::thread errThread(ReadPipe, errRead, std::ref(result.stderrText));

    const DWORD wait = ::WaitForSingleObject(process.hProcess, kGitTimeoutMs);
    if (wait == WAIT_TIMEOUT) {
        result.timedOut = true;
        ::TerminateProcess(process.hProcess, 124);
        ::WaitForSingleObject(process.hProcess, 5000);
    }

    DWORD exitCode = 1;
    ::GetExitCodeProcess(process.hProcess, &exitCode);
    result.exitCode = result.timedOut ? 124 : (int)exitCode;
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);

    outThread.join();
    errThread.join();
    ::CloseHandle(outRead);
    ::CloseHandle(errRead);
    result.started = true;
    return result;
}

std::wstring NormalizeOutput(const std::string& bytes) {
    std::wstring text = BytesToWide(bytes);
    std::wstring normalized;
    normalized.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') {
            if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
            normalized.push_back(L'\n');
        } else {
            normalized.push_back(text[i]);
        }
    }
    while (!normalized.empty() &&
           (normalized.back() == L'\n' || normalized.back() == L'\r' ||
            normalized.back() == L' ' || normalized.back() == L'\t')) {
        normalized.pop_back();
    }
    return normalized;
}

std::wstring LimitText(std::wstring text, size_t limit) {
    if (text.size() <= limit) return text;
    text.resize(limit);
    text += L"\n...";
    return text;
}

std::wstring FormatTimestamp(bool forFolder) {
    SYSTEMTIME time = {};
    ::GetLocalTime(&time);
    wchar_t buffer[32] = {};
    if (forFolder) {
        swprintf_s(buffer, L"%04u%02u%02u-%02u%02u%02u", (unsigned)time.wYear,
                   (unsigned)time.wMonth, (unsigned)time.wDay, (unsigned)time.wHour,
                   (unsigned)time.wMinute, (unsigned)time.wSecond);
    } else {
        swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u:%02u", (unsigned)time.wYear,
                   (unsigned)time.wMonth, (unsigned)time.wDay, (unsigned)time.wHour,
                   (unsigned)time.wMinute, (unsigned)time.wSecond);
    }
    return buffer;
}

std::wstring ComputerName() {
    wchar_t buffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (::GetComputerNameW(buffer, &size)) return std::wstring(buffer, size);
    return L"PC";
}

bool IsBackupName(const std::wstring& name) {
    if (name.size() < 15) return false;
    for (size_t i = 0; i < 15; ++i) {
        if (i == 8) {
            if (name[i] != L'-') return false;
        } else if (!iswdigit(name[i])) {
            return false;
        }
    }
    if (name.size() == 15) return true;
    if (name[15] != L'-' || name.size() == 16) return false;
    for (size_t i = 16; i < name.size(); ++i) {
        if (!iswdigit(name[i])) return false;
    }
    return true;
}

void PruneBackups(const fs::path& root) {
    std::vector<fs::path> backups;
    std::error_code ec;
    fs::directory_iterator iterator(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) return;
    for (const fs::directory_entry& entry : iterator) {
        std::error_code entryError;
        if (!entry.is_directory(entryError)) continue;
        if (IsBackupName(entry.path().filename().wstring())) {
            backups.push_back(entry.path());
        }
    }
    std::sort(backups.begin(), backups.end());
    while (backups.size() > (size_t)kKeepBackups) {
        std::error_code removeError;
        fs::remove_all(backups.front(), removeError);
        backups.erase(backups.begin());
    }
}

bool MakeTreeWritable(const fs::path& root) {
    std::error_code ec;
    fs::permissions(root,
                    fs::perms::owner_write | fs::perms::group_write |
                        fs::perms::others_write,
                    fs::perm_options::add, ec);
    if (!fs::is_directory(root, ec)) return true;
    for (fs::recursive_directory_iterator it(root,
                                             fs::directory_options::skip_permission_denied,
                                             ec),
         end;
         it != end; it.increment(ec)) {
        if (ec) return false;
        std::error_code permissionError;
        fs::permissions(it->path(),
                        fs::perms::owner_write | fs::perms::group_write |
                            fs::perms::others_write,
                        fs::perm_options::add, permissionError);
    }
    return true;
}

bool ClearDirectory(const fs::path& dir, std::wstring& error) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        error = L"目录不存在：" + dir.wstring();
        return false;
    }
    fs::directory_iterator iterator(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        error = L"无法读取目录：" + dir.wstring();
        return false;
    }
    for (const fs::directory_entry& entry : iterator) {
        if (IsDotGit(entry.path())) continue;
        MakeTreeWritable(entry.path());
        std::error_code removeError;
        fs::remove_all(entry.path(), removeError);
        if (removeError) {
            error = L"无法删除：" + entry.path().wstring();
            return false;
        }
    }
    return true;
}

void EnsureGroupMarkers(const fs::path& dataDir) {
    // Git 不跟踪空目录；给空分组放一个隐藏标记文件，保证分组本身也能同步。
    std::error_code ec;
    fs::recursive_directory_iterator it(
        dataDir, fs::directory_options::skip_permission_denied, ec),
        end;
    if (ec) return;
    for (; it != end; it.increment(ec)) {
        if (ec) return;
        std::error_code typeError;
        if (it->is_symlink(typeError)) continue;
        if (!it->is_directory(typeError)) continue;

        const std::wstring name = it->path().filename().wstring();
        if (Lower(name) == L".git" || (!name.empty() && name[0] == L'.')) {
            it.disable_recursion_pending();
            continue;
        }

        std::error_code emptyError;
        if (!fs::is_empty(it->path(), emptyError) || emptyError) continue;

        std::ofstream marker(it->path() / L".pendown-group",
                             std::ios::binary | std::ios::trunc);
        if (marker) marker << "PenDown group\n";
    }
}

const char* const kGitIgnoreLines[] = {
    "# PenDown transient files",
    "*.autosave",
    "*.autosave.style.json",
    "*.bak",
    "*.tmp",
    "*.pendown-rn-*",
};

void EnsureGitIgnore(const fs::path& dataDir) {
    const fs::path path = dataDir / L".gitignore";
    std::string content;
    {
        std::ifstream file(path, std::ios::binary);
        if (file) {
            content.assign(std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>());
        }
    }

    std::vector<std::string> lines;
    {
        std::istringstream stream(content);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }

    bool changed = false;
    for (const char* required : kGitIgnoreLines) {
        if (std::find(lines.begin(), lines.end(), required) != lines.end()) {
            continue;
        }
        if (!content.empty() && content.back() != '\n') content.push_back('\n');
        content += required;
        content.push_back('\n');
        lines.emplace_back(required);
        changed = true;
    }
    if (!changed) return;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (file) file.write(content.data(), (std::streamsize)content.size());
}

bool IsTransientGitFile(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) { return (char)std::tolower(ch); });
    const auto endsWith = [&](const char* suffix) {
        const std::string text(suffix);
        return lower.size() >= text.size() &&
               lower.compare(lower.size() - text.size(), text.size(), text) == 0;
    };
    return endsWith(".autosave") || endsWith(".autosave.style.json") ||
           endsWith(".bak") || endsWith(".tmp") ||
           lower.find(".pendown-rn-") != std::string::npos;
}

bool CopyTree(const fs::path& source, const fs::path& target, std::wstring& error) {
    std::error_code ec;
    if (!fs::is_directory(source, ec)) {
        error = L"目录不存在：" + source.wstring();
        return false;
    }
    fs::create_directories(target, ec);
    if (ec) {
        error = L"无法创建目录：" + target.wstring();
        return false;
    }

    for (fs::recursive_directory_iterator it(
             source, fs::directory_options::skip_permission_denied, ec),
         end;
         it != end; it.increment(ec)) {
        if (ec) {
            error = L"读取目录失败：" + source.wstring();
            return false;
        }
        const fs::path& from = it->path();
        if (IsDotGit(from)) {
            std::error_code dirError;
            if (it->is_directory(dirError)) it.disable_recursion_pending();
            continue;
        }
        std::error_code typeError;
        if (it->is_symlink(typeError)) continue;

        const fs::path relative = from.lexically_relative(source);
        if (relative.empty()) continue;
        const fs::path to = target / relative;

        if (it->is_directory(typeError)) {
            fs::create_directories(to, ec);
        } else if (it->is_regular_file(typeError)) {
            fs::create_directories(to.parent_path(), ec);
            if (!ec) {
                fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
            }
        }
        if (ec) {
            error = L"复制文件失败：" + from.wstring();
            return false;
        }
    }
    return true;
}

std::wstring CreateBackup(const fs::path& dataDir, const fs::path& backupRoot,
                          std::wstring& error) {
    std::error_code ec;
    fs::create_directories(backupRoot, ec);
    if (ec) {
        error = L"无法创建备份目录：" + backupRoot.wstring();
        return std::wstring();
    }

    const std::wstring stamp = FormatTimestamp(true);
    fs::path target = backupRoot / stamp;
    int suffix = 2;
    while (fs::exists(target, ec)) {
        target = backupRoot / (stamp + L"-" + std::to_wstring(suffix++));
    }
    if (!CopyTree(dataDir, target, error)) return std::wstring();
    PruneBackups(backupRoot);
    return target.wstring();
}

GitSyncResult Failure(const std::wstring& message, const std::wstring& details = {}) {
    GitSyncResult result;
    result.message = message;
    result.details = details;
    return result;
}

std::wstring JoinBranches(const std::vector<std::wstring>& branches) {
    std::wstring text;
    for (const std::wstring& branch : branches) {
        if (!text.empty()) text += L", ";
        text += branch;
    }
    return text;
}

std::wstring LocalAppDataDir() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) return std::wstring(buffer, length);
    std::error_code ec;
    const fs::path temp = fs::temp_directory_path(ec);
    if (!ec) return temp.wstring();
    return L".";
}

}  // namespace

std::wstring FindGitExecutable() {
    wchar_t searchPath[32768] = {};
    const DWORD found = ::SearchPathW(nullptr, L"git.exe", nullptr,
                                      (DWORD)std::size(searchPath), searchPath, nullptr);
    if (found > 0 && found < std::size(searchPath)) {
        std::error_code ec;
        if (fs::is_regular_file(searchPath, ec)) return searchPath;
    }

    std::vector<std::wstring> candidates;
    const auto addCandidate = [&candidates](const wchar_t* variable,
                                            const wchar_t* suffix) {
        wchar_t base[MAX_PATH] = {};
        const DWORD length = ::GetEnvironmentVariableW(variable, base, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            candidates.push_back((fs::path(base) / suffix).wstring());
        }
    };
    addCandidate(L"ProgramFiles", L"Git\\cmd\\git.exe");
    addCandidate(L"ProgramW6432", L"Git\\cmd\\git.exe");
    addCandidate(L"ProgramFiles(x86)", L"Git\\cmd\\git.exe");
    addCandidate(L"LOCALAPPDATA", L"Programs\\Git\\cmd\\git.exe");
    for (const std::wstring& candidate : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }
    return std::wstring();
}

std::wstring DefaultGitBackupRoot() {
    return (fs::path(LocalAppDataDir()) / L"PenDown" / L"backups").wstring();
}

GitSync::GitSync(std::wstring dataDir, GitSyncConfig config)
    : dataDir_(NormalizePath(dataDir)),
      backupRoot_(DefaultGitBackupRoot()),
      gitExe_(config.gitExe.empty() ? FindGitExecutable()
                                    : NormalizePath(config.gitExe)),
      config_(std::move(config)) {
    config_.remoteUrl = Trim(config_.remoteUrl);
    config_.username = Trim(config_.username);
    config_.token = Trim(config_.token);
    config_.branch = Trim(config_.branch);
    config_.gitExe = Trim(config_.gitExe);
    if (config_.branch.empty()) config_.branch = L"main";
}

bool GitSync::Validate(std::wstring& error) const {
    if (config_.remoteUrl.empty()) {
        error = L"请先填写 Git 仓库地址。";
        return false;
    }
    if (config_.branch.empty()) {
        error = L"请填写 Git 分支名称。";
        return false;
    }
    if (!config_.token.empty() && config_.username.empty()) {
        error = L"使用密码或访问令牌时，必须填写用户名。";
        return false;
    }

    std::error_code ec;
    if (gitExe_.empty() || !fs::is_regular_file(gitExe_, ec)) {
        if (!config_.gitExe.empty()) {
            error = L"配置的 Git 程序不存在：\n" + config_.gitExe;
        } else {
            error = L"未找到 git.exe。\n请安装 Git for Windows，或在设置中填写 "
                    L"git.exe 的实际路径。";
        }
        return false;
    }
    if (!fs::is_directory(dataDir_, ec)) {
        error = L"便签数据目录不存在：" + dataDir_;
        return false;
    }
    if (!IsSafeDataDir(dataDir_)) {
        error = L"便签数据目录不能是磁盘根目录。";
        return false;
    }
    if (IsSameOrChild(fs::path(backupRoot_), fs::path(dataDir_))) {
        error = L"Git 备份目录不能位于便签数据目录内部。";
        return false;
    }
    return true;
}

GitSync::ProcessResult GitSync::RunGit(const std::vector<std::wstring>& args) const {
    std::vector<std::wstring> fullArgs;
    fullArgs.reserve(args.size() + 4);
    fullArgs.push_back(L"-c");
    fullArgs.push_back(L"core.quotepath=false");
    fullArgs.push_back(L"-c");
    fullArgs.push_back(L"core.autocrlf=false");
    fullArgs.push_back(L"-c");
    fullArgs.push_back(L"color.ui=false");
    fullArgs.insert(fullArgs.end(), args.begin(), args.end());

    std::vector<std::pair<std::wstring, std::wstring>> environment;
    environment.emplace_back(L"GIT_TERMINAL_PROMPT", L"0");
    environment.emplace_back(L"GCM_INTERACTIVE", L"Never");
    environment.emplace_back(L"GIT_SSH_COMMAND", L"ssh -o BatchMode=yes");

    int configCount = 0;
    const auto addConfig = [&](const std::wstring& key, const std::wstring& value) {
        environment.emplace_back(L"GIT_CONFIG_KEY_" + std::to_wstring(configCount), key);
        environment.emplace_back(L"GIT_CONFIG_VALUE_" + std::to_wstring(configCount), value);
        ++configCount;
    };
    if (!config_.token.empty()) {
        const std::string encoded =
            Base64Encode(WideToUtf8(config_.username + L":" + config_.token));
        addConfig(L"http.extraHeader",
                  L"Authorization: Basic " + std::wstring(encoded.begin(), encoded.end()));
        environment.emplace_back(L"GIT_CONFIG_COUNT", std::to_wstring(configCount));
    }

    const RawProcessResult raw = RunProcess(gitExe_, fullArgs, dataDir_, environment);
    ProcessResult result;
    result.started = raw.started;
    result.timedOut = raw.timedOut;
    result.exitCode = (int)raw.exitCode;
    result.stdoutText = raw.stdoutText;
    result.stderrText = raw.stderrText;
    result.error = raw.error;
    return result;
}

std::wstring GitSync::ErrorWithOutput(const std::wstring& message,
                                      const ProcessResult& result) const {
    if (!result.started) {
        return result.error.empty() ? message : message + L"\n\n" + result.error;
    }
    std::wstring output = NormalizeOutput(result.stdoutText);
    const std::wstring errors = NormalizeOutput(result.stderrText);
    if (!errors.empty()) {
        if (!output.empty()) output += L"\n";
        output += errors;
    }
    if (result.timedOut) {
        output = L"操作超时（超过 10 分钟）。\n" + output;
    }
    output = LimitText(output, 2400);
    if (output.empty()) {
        if (result.exitCode != 0) {
            output = L"Git 退出码：" + std::to_wstring(result.exitCode);
        } else {
            return message;
        }
    }
    return message + L"\n\n" + output;
}

bool GitSync::EnsureRepository(std::wstring& error) const {
    std::error_code ec;
    if (!fs::is_directory(dataDir_, ec)) {
        error = L"便签数据目录不存在：" + dataDir_;
        return false;
    }

    if (!fs::exists(fs::path(dataDir_) / L".git", ec)) {
        const ProcessResult init = RunGit({L"init", L"-q"});
        if (!init.started || init.exitCode != 0) {
            error = ErrorWithOutput(L"初始化本地 Git 仓库失败。", init);
            return false;
        }
    }

    const ProcessResult setUrl =
        RunGit({L"remote", L"set-url", L"origin", config_.remoteUrl});
    if (!setUrl.started || setUrl.exitCode != 0) {
        const ProcessResult addUrl =
            RunGit({L"remote", L"add", L"origin", config_.remoteUrl});
        if (!addUrl.started || addUrl.exitCode != 0) {
            error = ErrorWithOutput(L"配置远程仓库失败。", addUrl);
            return false;
        }
    }
    // 保证 fetch 能看到远程全部分支，避免旧仓库的单分支配置影响拉取。
    RunGit({L"config", L"remote.origin.fetch",
            L"+refs/heads/*:refs/remotes/origin/*"});
    return true;
}

std::vector<std::wstring> GitSync::RemoteBranches() const {
    std::vector<std::wstring> branches;
    const ProcessResult result =
        RunGit({L"for-each-ref", L"--format=%(refname:short)", L"refs/remotes/origin"});
    if (!result.started || result.exitCode != 0) return branches;

    std::wstring text = NormalizeOutput(result.stdoutText);
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(L'\n', start);
        std::wstring line = text.substr(start, end == std::wstring::npos
                                                  ? std::wstring::npos
                                                  : end - start);
        line = Trim(line);
        if (line.rfind(L"origin/", 0) == 0) line = line.substr(7);
        if (!line.empty() && line != L"HEAD" &&
            std::find(branches.begin(), branches.end(), line) == branches.end()) {
            branches.push_back(line);
        }
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return branches;
}

void GitSync::EnsureTransientFilesIgnored() const {
    EnsureGitIgnore(fs::path(dataDir_));

    const ProcessResult listed = RunGit({L"ls-files", L"-z"});
    if (!listed.started || listed.exitCode != 0) return;

    std::vector<std::wstring> victims;
    size_t start = 0;
    while (start < listed.stdoutText.size()) {
        const size_t end = listed.stdoutText.find('\0', start);
        const size_t stop =
            end == std::string::npos ? listed.stdoutText.size() : end;
        const std::string path = listed.stdoutText.substr(start, stop - start);
        start = stop + 1;
        if (IsTransientGitFile(path)) victims.push_back(BytesToWide(path));
    }

    constexpr size_t kBatch = 100;
    for (size_t offset = 0; offset < victims.size(); offset += kBatch) {
        std::vector<std::wstring> args = {L"rm", L"--cached", L"--quiet",
                                          L"--ignore-unmatch", L"--"};
        const size_t limit = std::min(victims.size(), offset + kBatch);
        args.insert(args.end(), victims.begin() + (long)offset,
                    victims.begin() + (long)limit);
        RunGit(args);
    }
}

GitSyncResult GitSync::Push(const ProgressFn& progress) {
    std::wstring error;
    if (!Validate(error)) return Failure(error);
    if (progress) progress(L"正在初始化本地 Git 仓库...");
    if (!EnsureRepository(error)) return Failure(error);

    if (progress) progress(L"正在提交本地更改...");
    EnsureGroupMarkers(dataDir_);
    EnsureTransientFilesIgnored();
    const ProcessResult add = RunGit({L"add", L"-A"});
    if (!add.started || add.exitCode != 0) {
        return Failure(ErrorWithOutput(L"暂存本地文件失败。", add));
    }

    const ProcessResult status = RunGit({L"status", L"--porcelain"});
    if (!status.started || status.exitCode != 0) {
        return Failure(ErrorWithOutput(L"读取同步状态失败。", status));
    }
    const bool changed = !NormalizeOutput(status.stdoutText).empty();
    std::wstring commitMessage;
    if (changed) {
        commitMessage = L"PenDown sync from " + ComputerName() + L" at " +
                        FormatTimestamp(false);
        const ProcessResult commit =
            RunGit({L"-c", L"user.name=PenDown", L"-c",
                    L"user.email=pendown@localhost", L"commit", L"-q", L"-m",
                    commitMessage});
        if (!commit.started || commit.exitCode != 0) {
            return Failure(ErrorWithOutput(L"创建本地提交失败。", commit));
        }
    }

    const ProcessResult head =
        RunGit({L"rev-parse", L"--verify", L"--quiet", L"HEAD"});
    if (!head.started || head.exitCode != 0) {
        return Failure(L"本地没有可推送的便签文件。");
    }

    if (progress) progress(L"正在推送到远程仓库...");
    const std::wstring refspec = L"HEAD:refs/heads/" + config_.branch;
    const ProcessResult push = RunGit({L"push", L"--force", L"origin", refspec});
    if (!push.started || push.exitCode != 0) {
        return Failure(ErrorWithOutput(L"推送到远程仓库失败。", push));
    }

    GitSyncResult result;
    result.ok = true;
    result.message = changed ? L"已推送到远程仓库，远程内容已变为本地状态。"
                             : L"本地没有新改动，远程分支已强制更新为本地状态。";
    result.details = L"分支：" + config_.branch;
    if (changed) result.details += L"\n提交：" + commitMessage;
    return result;
}

GitSyncResult GitSync::Pull(const ProgressFn& progress) {
    std::wstring error;
    if (!Validate(error)) return Failure(error);
    if (progress) progress(L"正在初始化本地 Git 仓库...");
    if (!EnsureRepository(error)) return Failure(error);

    if (progress) progress(L"正在读取远程仓库...");
    const ProcessResult fetch = RunGit({L"fetch", L"--prune", L"origin"});
    if (!fetch.started || fetch.exitCode != 0) {
        return Failure(ErrorWithOutput(
            L"无法连接远程仓库。请检查仓库地址、账号和网络。", fetch));
    }

    const std::wstring remoteRef = L"refs/remotes/origin/" + config_.branch;
    const ProcessResult exists =
        RunGit({L"show-ref", L"--verify", L"--quiet", remoteRef});
    if (!exists.started || exists.exitCode != 0) {
        std::wstring message = L"远程仓库中不存在分支 " + config_.branch + L"。";
        const std::vector<std::wstring> branches = RemoteBranches();
        if (!branches.empty()) message += L"\n可用分支：" + JoinBranches(branches);
        return Failure(message);
    }

    if (progress) progress(L"正在备份本地便签...");
    const std::wstring backup = CreateBackup(dataDir_, backupRoot_, error);
    if (backup.empty()) {
        GitSyncResult result = Failure(L"创建本地备份失败，已取消拉取。", error);
        return result;
    }

    if (progress) progress(L"正在用远程内容覆盖本地...");
    if (!ClearDirectory(dataDir_, error)) {
        std::wstring restoreError;
        CopyTree(backup, dataDir_, restoreError);
        GitSyncResult result =
            Failure(L"清空本地便签目录失败，已尝试从备份还原。", error);
        result.backupPath = backup;
        if (!restoreError.empty()) {
            result.details += L"\n\n备份还原失败：" + restoreError;
        }
        return result;
    }

    const ProcessResult checkout =
        RunGit({L"checkout", L"-f", L"-B", config_.branch,
                L"origin/" + config_.branch});
    const ProcessResult reset =
        RunGit({L"reset", L"--hard", L"-q", L"origin/" + config_.branch});
    const ProcessResult clean = RunGit({L"clean", L"-fdq"});

    if (!checkout.started || checkout.exitCode != 0 || !reset.started ||
        reset.exitCode != 0 || !clean.started || clean.exitCode != 0) {
        std::wstring restoreError;
        ClearDirectory(dataDir_, restoreError);
        if (restoreError.empty()) {
            CopyTree(backup, dataDir_, restoreError);
        } else {
            restoreError.clear();
            CopyTree(backup, dataDir_, restoreError);
        }
        GitSyncResult result =
            Failure(L"从远程更新本地文件失败，已尝试从备份还原。",
                    ErrorWithOutput(L"Git 拉取失败。", checkout));
        result.backupPath = backup;
        if (!restoreError.empty()) {
            result.details += L"\n\n备份还原失败：" + restoreError;
        }
        return result;
    }

    GitSyncResult result;
    result.ok = true;
    result.message = L"已从远程仓库拉取，本地内容已变为远程状态。";
    result.backupPath = backup;
    return result;
}
