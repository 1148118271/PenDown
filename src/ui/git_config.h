#pragma once

#include <wx/string.h>

#include "core/git_sync.h"

// Git 同步配置的读写。访问令牌使用 Windows DPAPI 加密后存入 wxConfig。
GitSyncConfig LoadGitSyncConfig();
bool SaveGitSyncConfig(const GitSyncConfig& config, wxString& error);
