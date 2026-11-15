#include "core/i18n.h"

#include <windows.h>

#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

using MessageMap = std::unordered_map<std::string, std::wstring>;

MessageMap g_chinese;
MessageMap g_english;
i18n::Language g_language = i18n::Language::Chinese;

MessageMap& MapFor(i18n::Language language) {
    return language == i18n::Language::English ? g_english : g_chinese;
}

const MessageMap& ConstMapFor(i18n::Language language) {
    return language == i18n::Language::English ? g_english : g_chinese;
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int length =
        ::MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    if (length <= 0) return std::wstring();
    std::wstring out((size_t)length, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), &out[0], length);
    return out;
}

std::string TrimAscii(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end &&
           (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
        ++begin;
    }
    while (end > begin &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

int HexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool ParseQuotedValue(const std::string& line, size_t& pos, std::string& bytes) {
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos >= line.size() || line[pos] != '"') return false;
    ++pos;
    while (pos < line.size()) {
        const char ch = line[pos++];
        if (ch == '"') return true;
        if (ch != '\\') {
            bytes.push_back(ch);
            continue;
        }
        if (pos >= line.size()) return false;
        const char escaped = line[pos++];
        switch (escaped) {
            case 'n':
                bytes.push_back('\n');
                break;
            case 'r':
                bytes.push_back('\r');
                break;
            case 't':
                bytes.push_back('\t');
                break;
            case '"':
                bytes.push_back('"');
                break;
            case '\\':
                bytes.push_back('\\');
                break;
            case 'x': {
                int value = 0;
                int digits = 0;
                while (digits < 2 && pos < line.size()) {
                    const int digit = HexValue(line[pos]);
                    if (digit < 0) break;
                    value = value * 16 + digit;
                    ++pos;
                    ++digits;
                }
                if (digits == 0) return false;
                bytes.push_back((char)value);
                break;
            }
            default:
                // 未知转义按原样保留，方便手写文件时排查。
                bytes.push_back(escaped);
                break;
        }
    }
    return false;
}

bool ParseLanguageFile(const std::wstring& path, MessageMap& out) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) return false;
    std::string data((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
        data.erase(0, 3);
    }

    MessageMap parsed;
    size_t start = 0;
    while (start <= data.size()) {
        const size_t end = data.find('\n', start);
        const std::string raw = data.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? data.size() + 1 : end + 1;

        const std::string line = TrimAscii(raw);
        if (line.empty() || line[0] == '#') continue;

        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = TrimAscii(line.substr(0, equals));
        if (key.empty()) continue;

        size_t pos = equals + 1;
        std::string bytes;
        if (!ParseQuotedValue(line, pos, bytes)) continue;
        parsed[key] = Utf8ToWide(bytes);
    }

    if (parsed.empty()) return false;
    out = std::move(parsed);
    return true;
}

const std::wstring* FindMessage(const MessageMap& map, const std::string& key) {
    const auto found = map.find(key);
    return found == map.end() ? nullptr : &found->second;
}

const std::wstring* FindInLanguages(const std::string& key) {
    const MessageMap& current = ConstMapFor(g_language);
    if (const std::wstring* value = FindMessage(current, key)) return value;
    const MessageMap& fallback = ConstMapFor(g_language == i18n::Language::English
                                                 ? i18n::Language::Chinese
                                                 : i18n::Language::English);
    return FindMessage(fallback, key);
}

}  // namespace

namespace i18n {

Language DetectSystemLanguage() {
    const LANGID language = ::GetUserDefaultUILanguage();
    if (PRIMARYLANGID(language) == LANG_CHINESE) return Language::Chinese;
    return Language::English;
}

Language Resolve(Language language) {
    return language == Language::Auto ? DetectSystemLanguage() : language;
}

void SetLanguage(Language language) { g_language = Resolve(language); }

Language Current() { return g_language; }

bool LoadLanguageFile(const std::wstring& path, Language language) {
    if (language == Language::Auto) return false;
    return ParseLanguageFile(path, MapFor(language));
}

std::wstring Tr(const char* key) {
    if (!key || !*key) return std::wstring();
    const std::string id(key);
    if (const std::wstring* value = FindInLanguages(id)) return *value;
    return Utf8ToWide(id);
}

std::wstring Plural(const char* key, long count) {
    if (!key || !*key) return std::wstring();
    const std::string base(key);
    const std::string suffix = count == 1 ? ".one" : ".many";
    const MessageMap& current = ConstMapFor(g_language);
    const MessageMap& fallback = ConstMapFor(g_language == Language::English
                                                 ? Language::Chinese
                                                 : Language::English);
    const std::wstring* format = FindMessage(current, base + suffix);
    if (!format) format = FindMessage(current, base);
    if (!format) format = FindMessage(fallback, base + suffix);
    if (!format) format = FindMessage(fallback, base);
    if (!format) return Utf8ToWide(base);

    wchar_t buffer[512] = {};
    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), format->c_str(), count);
    return buffer;
}

}  // namespace i18n
