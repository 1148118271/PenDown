#pragma once

#include <string>

// 轻量双语文案：不依赖 gettext / wxWidgets，core 和 ui 都可以直接使用。
// 代码里只保留消息 key；实际文案从 exe 同级的 lang/*.lang 文件加载。
namespace i18n {

enum class Language {
    Auto,
    Chinese,
    English,
};

// Auto 按系统 UI 语言解析：中文系统用中文，其余用英文。
Language DetectSystemLanguage();
Language Resolve(Language language);
void SetLanguage(Language language);
Language Current();

// 加载一个语言文件。文件格式为 UTF-8 文本：
//   # 注释
//   key="带转义的文案"
// 支持 \n、\r、\t、\"、\\ 转义；同 key 后出现的条目覆盖先出现的条目。
// 中文文件可直接写 key="..."；英文复数写 key.one / key.many。
bool LoadLanguageFile(const std::wstring& path, Language language);

// 查找当前语言文案；缺失时回退到另一种语言，再缺失时返回 key 本身。
std::wstring Tr(const char* key);

// 复数文案：优先查找 key.one / key.many，再回退到 key。
// 格式串里使用 %ld 表示数量。
std::wstring Plural(const char* key, long count);

}  // namespace i18n
