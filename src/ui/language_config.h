#pragma once

#include "core/i18n.h"

// 语言设置保存在 wxConfig 的 /Language 下：auto / zh / en。
i18n::Language LoadLanguageSetting();
bool SaveLanguageSetting(i18n::Language language);

// 解析 Auto 并设置当前进程使用的语言；运行期间不再改变。
void ApplyLanguageSetting(i18n::Language language);
