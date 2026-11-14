#include "core/note_storage.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    int64_t number = 0;
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;
};

void AppendUtf8(std::string& out, uint32_t code) {
    if (code <= 0x7F) {
        out.push_back((char)code);
    } else if (code <= 0x7FF) {
        out.push_back((char)(0xC0 | (code >> 6)));
        out.push_back((char)(0x80 | (code & 0x3F)));
    } else if (code <= 0xFFFF) {
        out.push_back((char)(0xE0 | (code >> 12)));
        out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (code & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (code >> 18)));
        out.push_back((char)(0x80 | ((code >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (code & 0x3F)));
    }
}

int HexDigit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    bool Parse(JsonValue& value, std::string& error) {
        Skip();
        if (!ParseValue(value, error)) return false;
        Skip();
        if (pos_ != text_.size()) {
            error = "trailing data";
            return false;
        }
        return true;
    }

private:
    void Skip() {
        while (pos_ < text_.size() &&
               std::isspace((unsigned char)text_[pos_])) {
            ++pos_;
        }
    }

    bool ParseValue(JsonValue& value, std::string& error) {
        if (pos_ >= text_.size()) {
            error = "unexpected end";
            return false;
        }
        const char ch = text_[pos_];
        if (ch == '{') return ParseObject(value, error);
        if (ch == '[') return ParseArray(value, error);
        if (ch == '"') {
            value.type = JsonValue::Type::String;
            return ParseString(value.string, error);
        }
        if (ch == 't') {
            if (!Consume("true")) {
                error = "invalid literal";
                return false;
            }
            value.type = JsonValue::Type::Bool;
            value.boolean = true;
            return true;
        }
        if (ch == 'f') {
            if (!Consume("false")) {
                error = "invalid literal";
                return false;
            }
            value.type = JsonValue::Type::Bool;
            value.boolean = false;
            return true;
        }
        if (ch == 'n') {
            if (!Consume("null")) {
                error = "invalid literal";
                return false;
            }
            value.type = JsonValue::Type::Null;
            return true;
        }
        if (ch == '-' || (ch >= '0' && ch <= '9')) {
            return ParseNumber(value, error);
        }
        error = "unexpected character";
        return false;
    }

    bool Consume(const char* literal) {
        const size_t length = std::strlen(literal);
        if (text_.compare(pos_, length, literal) != 0) return false;
        pos_ += length;
        return true;
    }

    bool ParseObject(JsonValue& value, std::string& error) {
        value.type = JsonValue::Type::Object;
        ++pos_;  // {
        Skip();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            Skip();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                error = "object key expected";
                return false;
            }
            std::string key;
            if (!ParseString(key, error)) return false;
            Skip();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                error = "':' expected";
                return false;
            }
            ++pos_;
            JsonValue child;
            if (!ParseValue(child, error)) return false;
            value.object.emplace(std::move(key), std::move(child));
            Skip();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            error = "'}' expected";
            return false;
        }
    }

    bool ParseArray(JsonValue& value, std::string& error) {
        value.type = JsonValue::Type::Array;
        ++pos_;  // [
        Skip();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            JsonValue child;
            if (!ParseValue(child, error)) return false;
            value.array.push_back(std::move(child));
            Skip();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            error = "']' expected";
            return false;
        }
    }

    bool ParseString(std::string& out, std::string& error) {
        ++pos_;  // opening quote
        out.clear();
        while (pos_ < text_.size()) {
            const unsigned char ch = (unsigned char)text_[pos_++];
            if (ch == '"') return true;
            if (ch != '\\') {
                out.push_back((char)ch);
                continue;
            }
            if (pos_ >= text_.size()) {
                error = "bad escape";
                return false;
            }
            const char escaped = text_[pos_++];
            switch (escaped) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos_ + 4 > text_.size()) {
                        error = "bad unicode escape";
                        return false;
                    }
                    uint32_t code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const int digit = HexDigit(text_[pos_++]);
                        if (digit < 0) {
                            error = "bad unicode escape";
                            return false;
                        }
                        code = code * 16 + (uint32_t)digit;
                    }
                    if (code >= 0xD800 && code <= 0xDBFF && pos_ + 6 <= text_.size() &&
                        text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        uint32_t low = 0;
                        for (int i = 0; i < 4; ++i) {
                            const int digit = HexDigit(text_[pos_++]);
                            if (digit < 0) {
                                error = "bad unicode escape";
                                return false;
                            }
                            low = low * 16 + (uint32_t)digit;
                        }
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, code);
                    break;
                }
                default:
                    error = "bad escape";
                    return false;
            }
        }
        error = "unterminated string";
        return false;
    }

    bool ParseNumber(JsonValue& value, std::string& error) {
        const size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
        while (pos_ < text_.size() &&
               std::isdigit((unsigned char)text_[pos_])) {
            ++pos_;
        }
        if (start == pos_) {
            error = "bad number";
            return false;
        }
        const std::string token = text_.substr(start, pos_ - start);
        char* end = nullptr;
        const long long parsed = std::strtoll(token.c_str(), &end, 10);
        if (!end || *end != '\0') {
            error = "bad number";
            return false;
        }
        value.type = JsonValue::Type::Number;
        value.number = parsed;
        return true;
    }

    const std::string& text_;
    size_t pos_ = 0;
};

const JsonValue* Field(const JsonValue& object, const char* key) {
    if (object.type != JsonValue::Type::Object) return nullptr;
    const auto found = object.object.find(key);
    return found == object.object.end() ? nullptr : &found->second;
}

bool AsInt(const JsonValue* value, int64_t& out) {
    if (!value || value->type != JsonValue::Type::Number) return false;
    out = value->number;
    return true;
}

bool AsString(const JsonValue* value, std::string& out) {
    if (!value || value->type != JsonValue::Type::String) return false;
    out = value->string;
    return true;
}

bool AsBool(const JsonValue* value, bool& out) {
    if (!value || value->type != JsonValue::Type::Bool) return false;
    out = value->boolean;
    return true;
}

bool IsUtf8Boundary(const std::string& text, long pos) {
    if (pos <= 0 || pos >= (long)text.size()) return true;
    const unsigned char ch = (unsigned char)text[(size_t)pos];
    return (ch & 0xC0) != 0x80;
}

std::string JsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04X", ch);
                    out += buffer;
                } else {
                    out.push_back((char)ch);
                }
                break;
        }
    }
    return out;
}

void WriteStyleObject(std::ostringstream& out, const TextStyle& style,
                      const TextStyle& defaultStyle) {
    out << '{';
    bool first = true;
    auto separator = [&]() {
        if (!first) out << ',';
        first = false;
    };
    if (!style.fontFamily.empty() &&
        style.fontFamily != defaultStyle.fontFamily) {
        separator();
        out << "\"font\":\"" << JsonEscape(style.fontFamily) << '"';
    }
    if (style.sizeHundredths != 0 &&
        style.sizeHundredths != defaultStyle.sizeHundredths) {
        separator();
        out << "\"size\":" << style.sizeHundredths;
    }
    if (!style.foreground.empty() &&
        style.foreground != defaultStyle.foreground) {
        separator();
        out << "\"fg\":\"" << JsonEscape(style.foreground) << '"';
    }
    if (!style.background.empty() &&
        style.background != defaultStyle.background) {
        separator();
        out << "\"bg\":\"" << JsonEscape(style.background) << '"';
    }
    if (style.bold) {
        separator();
        out << "\"b\":true";
    }
    if (style.italic) {
        separator();
        out << "\"i\":true";
    }
    if (style.underline) {
        separator();
        out << "\"u\":true";
    }
    if (style.strike) {
        separator();
        out << "\"s\":true";
    }
    out << '}';
}

std::string BuildStyleJson(const std::string& text, const StyleModel& styles) {
    std::ostringstream out;
    out << "{\"version\":1,\"body\":{\"length\":" << text.size()
        << ",\"hash\":\"" << std::hex << std::setw(16) << std::setfill('0')
        << NoteStorage::HashText(text) << std::dec << "\"},\"styles\":[";

    const std::vector<TextStyle>& styleTable = styles.Styles();
    for (size_t i = 1; i < styleTable.size(); ++i) {
        if (i > 1) out << ',';
        WriteStyleObject(out, styleTable[i], styles.DefaultStyle());
    }
    out << "],\"runs\":[";
    bool first = true;
    for (const StyleRun& run : styles.Runs()) {
        if (!first) out << ',';
        first = false;
        out << '[' << run.start << ',' << run.end << ',' << run.styleId << ']';
    }
    out << "],\"links\":[";
    bool firstLink = true;
    for (const LinkRun& link : styles.Links().Runs()) {
        if (!firstLink) out << ',';
        firstLink = false;
        out << '[' << link.start << ',' << link.end << ",\""
            << JsonEscape(link.url) << "\"]";
    }
    out << "]}\n";
    return out.str();
}

bool ParseStyleJson(const std::string& json, const std::string& text,
                    StyleModel& styles, std::wstring& warning) {
    JsonValue root;
    std::string error;
    JsonParser parser(json);
    if (!parser.Parse(root, error)) {
        warning = L"样式文件格式无效，已忽略样式。";
        styles.ClearRuns();
        return true;
    }

    const JsonValue* body = Field(root, "body");
    int64_t length = -1;
    std::string hash;
    const JsonValue* version = Field(root, "version");
    int64_t versionNumber = 0;
    if (version && AsInt(version, versionNumber) && versionNumber > 1) {
        warning = L"样式文件版本较新，已忽略样式。";
        styles.ClearRuns();
        return true;
    }
    if (!body || !AsInt(Field(*body, "length"), length) ||
        !AsString(Field(*body, "hash"), hash)) {
        warning = L"样式文件缺少正文校验信息，已忽略样式。";
        styles.ClearRuns();
        return true;
    }

    std::ostringstream hashText;
    hashText << std::hex << std::setw(16) << std::setfill('0')
             << NoteStorage::HashText(text);
    if (length != (int64_t)text.size() || hash != hashText.str()) {
        warning = L"样式文件与正文不匹配，已忽略样式。";
        styles.ClearRuns();
        return true;
    }

    std::vector<TextStyle> table;
    std::vector<std::string> styleUrls;  // 旧格式：URL 保存在样式表里
    table.push_back(styles.DefaultStyle());
    styleUrls.push_back(std::string());

    const JsonValue* styleArray = Field(root, "styles");
    if (styleArray && styleArray->type == JsonValue::Type::Array) {
        for (const JsonValue& item : styleArray->array) {
            if (table.size() >= StyleModel::kMaxStyles) break;
            TextStyle style;
            std::string value;
            std::string url;
            if (AsString(Field(item, "font"), value)) style.fontFamily = value;
            int64_t number = 0;
            if (AsInt(Field(item, "size"), number) && number > 0 &&
                number < 100000) {
                style.sizeHundredths = (int)number;
            }
            if (AsString(Field(item, "fg"), value)) style.foreground = value;
            if (AsString(Field(item, "bg"), value)) style.background = value;
            if (AsString(Field(item, "url"), value)) url = value;
            bool flag = false;
            if (AsBool(Field(item, "b"), flag)) style.bold = flag;
            if (AsBool(Field(item, "i"), flag)) style.italic = flag;
            if (AsBool(Field(item, "u"), flag)) style.underline = flag;
            if (AsBool(Field(item, "s"), flag)) style.strike = flag;
            table.push_back(std::move(style));
            styleUrls.push_back(std::move(url));
        }
    }

    std::vector<StyleRun> runs;
    const JsonValue* runArray = Field(root, "runs");
    if (runArray && runArray->type == JsonValue::Type::Array) {
        long lastEnd = 0;
        for (const JsonValue& item : runArray->array) {
            if (item.type != JsonValue::Type::Array || item.array.size() != 3) {
                warning = L"样式区间格式无效，已忽略样式。";
                styles.ClearRuns();
                return true;
            }
            if (item.array[0].type != JsonValue::Type::Number ||
                item.array[1].type != JsonValue::Type::Number ||
                item.array[2].type != JsonValue::Type::Number) {
                warning = L"样式区间格式无效，已忽略样式。";
                styles.ClearRuns();
                return true;
            }
            const long start = (long)item.array[0].number;
            const long end = (long)item.array[1].number;
            const long styleId = (long)item.array[2].number;
            if (start < lastEnd || end <= start || end > (long)text.size() ||
                !IsUtf8Boundary(text, start) || !IsUtf8Boundary(text, end)) {
                warning = L"样式区间越界或未对齐 UTF-8 字符，已忽略样式。";
                styles.ClearRuns();
                return true;
            }
            if (styleId <= 0 || styleId >= (long)table.size()) {
                // 超出当前样式上限：只丢这一段样式，不连累整篇便签。
                warning = L"部分样式超出上限，已忽略。";
                lastEnd = end;
                continue;
            }
            runs.push_back(StyleRun{start, end, (uint32_t)styleId});
            lastEnd = end;
        }
    }

    std::vector<LinkRun> links;
    bool linksValid = true;
    const JsonValue* linkArray = Field(root, "links");
    if (linkArray) {
        if (linkArray->type != JsonValue::Type::Array) {
            linksValid = false;
        } else {
            long lastEnd = 0;
            for (const JsonValue& item : linkArray->array) {
                if (item.type != JsonValue::Type::Array ||
                    item.array.size() != 3 ||
                    item.array[0].type != JsonValue::Type::Number ||
                    item.array[1].type != JsonValue::Type::Number ||
                    item.array[2].type != JsonValue::Type::String) {
                    linksValid = false;
                    break;
                }
                const long start = (long)item.array[0].number;
                const long end = (long)item.array[1].number;
                const std::string& url = item.array[2].string;
                if (start < lastEnd || end <= start ||
                    end > (long)text.size() || url.empty() ||
                    !IsUtf8Boundary(text, start) ||
                    !IsUtf8Boundary(text, end)) {
                    linksValid = false;
                    break;
                }
                links.push_back(LinkRun{start, end, url});
                lastEnd = end;
            }
        }
    } else {
        // 旧格式：每个带 url 的样式区间都是一个链接；迁移到独立链接表，
        // 下次保存时写成新格式。
        for (const StyleRun& run : runs) {
            if (run.styleId < styleUrls.size() &&
                !styleUrls[run.styleId].empty()) {
                links.push_back(
                    LinkRun{run.start, run.end, styleUrls[run.styleId]});
            }
        }
    }
    if (!linksValid) {
        links.clear();
        if (warning.empty()) warning = L"链接数据无效，已忽略链接。";
    }

    styles.SetLoadedData(table, runs);
    styles.Links().SetLinks(std::move(links), (long)text.size());
    return true;
}

bool LoadPair(const std::wstring& bodyPath, const std::wstring& stylePath,
              std::string& text, StyleModel& styles, std::wstring& warning) {
    std::wstring error;
    if (!NoteStorage::ReadTextFile(bodyPath, text, error)) return false;

    std::error_code ec;
    if (!fs::is_regular_file(fs::path(stylePath), ec)) {
        styles.ClearRuns();
        return true;
    }
    std::string json;
    if (!NoteStorage::ReadTextFile(stylePath, json, error)) {
        warning = L"样式文件读取失败，已忽略样式。";
        styles.ClearRuns();
        return true;
    }
    return ParseStyleJson(json, text, styles, warning);
}

}  // namespace

std::wstring NoteStorage::StylePath(const std::wstring& bodyPath) {
    return bodyPath + L".style.json";
}

std::wstring NoteStorage::AutosavePath(const std::wstring& bodyPath) {
    return bodyPath + L".autosave";
}

std::wstring NoteStorage::AutosaveStylePath(const std::wstring& bodyPath) {
    return bodyPath + L".autosave.style.json";
}

uint64_t NoteStorage::HashText(const std::string& text) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : text) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool NoteStorage::ReadTextFile(const std::wstring& path, std::string& text,
                               std::wstring& error) {
    std::ifstream file(fs::path(path), std::ios::binary);
    if (!file) {
        error = L"无法读取文件：" + path;
        return false;
    }
    text.assign((std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        text.erase(0, 3);
    }
    return true;
}

bool NoteStorage::WriteBytes(const std::wstring& path, const std::string& bytes,
                             std::wstring& error) {
    std::ofstream file(fs::path(path), std::ios::binary | std::ios::trunc);
    if (!file) {
        error = L"无法写入文件：" + path;
        return false;
    }
    file.write(bytes.data(), (std::streamsize)bytes.size());
    file.flush();
    if (!file.good()) {
        error = L"写入文件失败：" + path;
        return false;
    }
    return true;
}

bool NoteStorage::WriteTextFile(const std::wstring& path, const std::string& text,
                                std::wstring& error) {
    return WriteBytes(path, text, error);
}

bool NoteStorage::ReplaceFileAtomic(const std::wstring& source,
                                    const std::wstring& target,
                                    std::wstring& error) {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = L"替换文件失败：" + target;
        return false;
    }
#else
    std::error_code ec;
    fs::rename(fs::path(source), fs::path(target), ec);
    if (ec) {
        error = L"替换文件失败：" + target;
        return false;
    }
#endif
    return true;
}

bool NoteStorage::BackupFile(const std::wstring& path) {
    std::error_code ec;
    if (!fs::is_regular_file(fs::path(path), ec)) return true;
    fs::path backup(path);
    backup += L".bak";
    fs::copy_file(fs::path(path), backup,
                  fs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool NoteStorage::Load(const std::wstring& bodyPath, std::string& text,
                       StyleModel& styles, std::wstring& warning) {
    return LoadPair(bodyPath, StylePath(bodyPath), text, styles, warning);
}

bool NoteStorage::Save(const std::wstring& bodyPath, const std::string& text,
                       const StyleModel& styles, std::wstring& error) {
    const std::wstring stylePath = StylePath(bodyPath);
    const bool hasStyles = !styles.Empty();
    const std::string styleJson = hasStyles ? BuildStyleJson(text, styles)
                                            : std::string();

    const std::wstring tempBody = bodyPath + L".tmp";
    const std::wstring tempStyle = stylePath + L".tmp";

    if (!WriteBytes(tempBody, text, error)) return false;
    if (hasStyles && !WriteBytes(tempStyle, styleJson, error)) {
        std::error_code ignored;
        fs::remove(fs::path(tempBody), ignored);
        return false;
    }

    // 备份失败不阻止保存；它只是额外的一层恢复手段。
    BackupFile(bodyPath);
    if (hasStyles) BackupFile(stylePath);

    if (hasStyles && !ReplaceFileAtomic(tempStyle, stylePath, error)) {
        std::error_code ignored;
        fs::remove(fs::path(tempBody), ignored);
        return false;
    }
    if (!ReplaceFileAtomic(tempBody, bodyPath, error)) return false;

    if (!hasStyles) {
        std::error_code ignored;
        fs::remove(fs::path(stylePath), ignored);
    }
    return true;
}

bool NoteStorage::SaveAutosave(const std::wstring& bodyPath,
                               const std::string& text,
                               const StyleModel& styles, std::wstring& error) {
    const std::wstring body = AutosavePath(bodyPath);
    const std::wstring style = AutosaveStylePath(bodyPath);
    const std::wstring tempBody = body + L".tmp";
    const std::wstring tempStyle = style + L".tmp";
    const bool hasStyles = !styles.Empty();
    if (!WriteBytes(tempBody, text, error)) return false;
    if (hasStyles) {
        if (!WriteBytes(tempStyle, BuildStyleJson(text, styles), error)) {
            std::error_code ignored;
            fs::remove(fs::path(tempBody), ignored);
            return false;
        }
    } else {
        std::error_code ignored;
        fs::remove(fs::path(style), ignored);
    }

    if (hasStyles && !ReplaceFileAtomic(tempStyle, style, error)) {
        std::error_code ignored;
        fs::remove(fs::path(tempBody), ignored);
        return false;
    }
    return ReplaceFileAtomic(tempBody, body, error);
}

bool NoteStorage::HasAutosave(const std::wstring& bodyPath) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(AutosavePath(bodyPath)), ec);
}

bool NoteStorage::LoadAutosave(const std::wstring& bodyPath, std::string& text,
                               StyleModel& styles, std::wstring& warning) {
    return LoadPair(AutosavePath(bodyPath), AutosaveStylePath(bodyPath), text,
                    styles, warning);
}

void NoteStorage::RemoveAutosave(const std::wstring& bodyPath) {
    std::error_code ignored;
    fs::remove(fs::path(AutosavePath(bodyPath)), ignored);
    fs::remove(fs::path(AutosaveStylePath(bodyPath)), ignored);
}
