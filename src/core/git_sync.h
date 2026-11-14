#pragma once

#include <functional>
#include <string>
#include <vector>

// Git 同步配置。令牌只存在于内存中，保存时由 UI 层用 Windows DPAPI 加密。
struct GitSyncConfig {
    std::wstring remoteUrl;
    std::wstring username;
    std::wstring token;
    std::wstring branch = L"main";
    // 可选：本地 git.exe 的实际路径；为空时自动从 PATH 和常见安装位置查找。
    std::wstring gitExe;
};

struct GitSyncResult {
    bool ok = false;
    std::wstring message;
    std::wstring details;
    std::wstring backupPath;
};

std::wstring FindGitExecutable();
std::wstring DefaultGitBackupRoot();

// 直接让便签数据目录本身成为一个 Git 仓库：
//   推送：提交本地改动后强制推送，本地覆盖远程；
//   拉取：先备份本地便签，再用远程分支强制覆盖本地。
// 不做合并、rebase 或冲突处理；.git 目录不会出现在便签列表里。
class GitSync {
public:
    using ProgressFn = std::function<void(const std::wstring&)>;

    GitSync(std::wstring dataDir, GitSyncConfig config);

    GitSyncResult Push(const ProgressFn& progress = {});
    GitSyncResult Pull(const ProgressFn& progress = {});

private:
    struct ProcessResult {
        bool started = false;
        bool timedOut = false;
        int exitCode = 1;
        std::string stdoutText;
        std::string stderrText;
        std::wstring error;
    };

    ProcessResult RunGit(const std::vector<std::wstring>& args) const;
    std::wstring ErrorWithOutput(const std::wstring& message,
                                 const ProcessResult& result) const;
    bool EnsureRepository(std::wstring& error) const;
    bool Validate(std::wstring& error) const;
    std::vector<std::wstring> RemoteBranches() const;
    // 写入 .gitignore，并把历史上已被跟踪的临时文件移出索引。
    void EnsureTransientFilesIgnored() const;

    std::wstring dataDir_;
    std::wstring backupRoot_;
    std::wstring gitExe_;
    GitSyncConfig config_;
};
