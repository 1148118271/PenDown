#pragma once

#include <string>

// 隐藏测试入口：验证纯文件便签存储的目录、分组、Markdown 文件和重命名逻辑。
bool RunSelfTest(const std::wstring& reportPath);

// Git 同步集成测试：返回 0 通过、1 失败、77 跳过（未找到 git.exe）。
int RunGitSelfTest(const std::wstring& reportPath);
