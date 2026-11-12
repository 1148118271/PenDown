#include "ui/markdown_preview.h"

#include "ui/app_font.h"
#include "ui/theme.h"

#include <wx/richtext/richtextbuffer.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/string.h>

#include <set>
#include <vector>

namespace {

int RichUnits(int dip) {
    // wxRichText 的缩进 / 段间距单位是 1/10 毫米，不能直接传 DIP 像素
    return (int)(dip * 10.0 / 96.0 * 25.4);
}

wxRichTextAttr BaseAttr(int pointSize) {
    wxRichTextAttr attr;
    attr.SetFont(AppFont::Content(pointSize));
    attr.SetTextColour(Theme::Text());
    attr.SetLeftIndent(0);
    attr.SetParagraphSpacingBefore(0);
    attr.SetParagraphSpacingAfter(RichUnits(5));
    return attr;
}

wxColour CodeBackgroundColour() {
    return wxColour(0xF7, 0xF6, 0xF4);
}

wxRichTextAttr StyledAttr(const wxRichTextAttr& base, bool bold, bool italic, bool strike,
                          bool code, bool link) {
    wxRichTextAttr attr = base;
    wxFont font = attr.GetFont();
    if (!font.IsOk()) font = AppFont::Content(11);
    if (bold) font.SetWeight(wxFONTWEIGHT_BOLD);
    if (italic) font.SetStyle(wxFONTSTYLE_ITALIC);
    if (strike) font.SetStrikethrough(true);
    if (code) {
        // 行内代码使用系统等宽字体和浅色背景
        font = AppFont::Mono(font.GetPointSize());
        attr.SetTextColour(Theme::Text());
        attr.SetBackgroundColour(CodeBackgroundColour());
    }
    attr.SetFont(font);
    if (link) {
        attr.SetTextColour(Theme::Accent());
        attr.SetFontUnderlined(true);
    }
    return attr;
}

void WriteStyled(wxRichTextCtrl* ctrl, const wxString& text, const wxRichTextAttr& attr) {
    if (text.empty()) return;
    ctrl->BeginStyle(attr);
    ctrl->WriteText(text);
    ctrl->EndStyle();
}

// 代码块语法高亮：按语言选择注释规则和关键字表。
enum class CodeDialect {
    Plain,
    Generic,
    CLike,
    Python,
    Shell,
    Json,
    Sql,
    Markup,
    Css,
};

struct CodeHighlightState {
    bool blockComment = false;
    bool htmlComment = false;
};

bool IsSpaceChar(wxUniChar ch) {
    return ch == ' ' || ch == '\t';
}

bool OnlyWhitespaceBefore(const wxString& line, long index) {
    for (long i = 0; i < index; ++i) {
        if (!IsSpaceChar(line[i])) return false;
    }
    return true;
}

wxString NormalizeLanguageInfo(const wxString& info) {
    wxString lang = info;
    lang.Trim(true).Trim(false);
    // 兼容 ```{.cpp} 这种写法
    if (lang.StartsWith(L"{.") && lang.EndsWith(L"}")) {
        lang = lang.Mid(2, lang.length() - 3);
    } else if (lang.StartsWith(L"{") && lang.EndsWith(L"}")) {
        lang = lang.Mid(1, lang.length() - 2);
    }
    const size_t cut = lang.find_first_of(L" \t,");
    if (cut != wxString::npos) lang = lang.Left(cut);
    lang.MakeLower();
    return lang;
}

CodeDialect DialectForLanguage(const wxString& lang) {
    if (lang.empty()) return CodeDialect::Generic;
    if (lang == L"text" || lang == L"plain" || lang == L"txt" || lang == L"log" ||
        lang == L"output" || lang == L"md" || lang == L"markdown") {
        return CodeDialect::Plain;
    }
    if (lang == L"json" || lang == L"jsonc" || lang == L"json5") {
        return CodeDialect::Json;
    }
    if (lang == L"py" || lang == L"python" || lang == L"python3" || lang == L"py3") {
        return CodeDialect::Python;
    }
    if (lang == L"sh" || lang == L"bash" || lang == L"shell" || lang == L"zsh" ||
        lang == L"console" || lang == L"powershell" || lang == L"ps1" ||
        lang == L"bat" || lang == L"batch" || lang == L"cmd") {
        return CodeDialect::Shell;
    }
    if (lang == L"sql" || lang == L"mysql" || lang == L"pgsql" || lang == L"sqlite") {
        return CodeDialect::Sql;
    }
    if (lang == L"html" || lang == L"htm" || lang == L"xml" || lang == L"svg" ||
        lang == L"xaml" || lang == L"vue") {
        return CodeDialect::Markup;
    }
    if (lang == L"css" || lang == L"scss" || lang == L"less" || lang == L"sass") {
        return CodeDialect::Css;
    }
    if (lang == L"c" || lang == L"h" || lang == L"cpp" || lang == L"c++" ||
        lang == L"cc" || lang == L"cxx" || lang == L"hpp" || lang == L"hxx" ||
        lang == L"cs" || lang == L"csharp" || lang == L"c#" || lang == L"java" ||
        lang == L"kt" || lang == L"kotlin" || lang == L"js" || lang == L"javascript" ||
        lang == L"jsx" || lang == L"ts" || lang == L"typescript" || lang == L"tsx" ||
        lang == L"go" || lang == L"golang" || lang == L"rs" || lang == L"rust" ||
        lang == L"php" || lang == L"swift" || lang == L"objc" || lang == L"m") {
        return CodeDialect::CLike;
    }
    return CodeDialect::Generic;
}

const std::set<wxString>& KeywordsFor(const wxString& lang, CodeDialect dialect) {
    static const std::set<wxString> kCpp = {
        L"alignas", L"alignof", L"auto", L"bool", L"break", L"case", L"catch",
        L"char", L"class", L"const", L"constexpr", L"continue", L"default",
        L"delete", L"do", L"double", L"else", L"enum", L"explicit", L"export",
        L"extern", L"false", L"float", L"for", L"friend", L"goto", L"if",
        L"inline", L"int", L"long", L"mutable", L"namespace", L"new",
        L"noexcept", L"nullptr", L"operator", L"private", L"protected",
        L"public", L"return", L"short", L"signed", L"sizeof", L"static",
        L"struct", L"switch", L"template", L"this", L"throw", L"true", L"try",
        L"typedef", L"typename", L"union", L"unsigned", L"using", L"virtual",
        L"void", L"volatile", L"wchar_t", L"while", L"override", L"final",
        L"concept", L"requires",
    };
    static const std::set<wxString> kJs = {
        L"as", L"async", L"await", L"break", L"case", L"catch", L"class",
        L"const", L"continue", L"debugger", L"default", L"delete", L"do",
        L"else", L"export", L"extends", L"false", L"finally", L"for", L"from",
        L"function", L"get", L"if", L"import", L"in", L"instanceof",
        L"interface", L"let", L"new", L"null", L"of", L"return", L"set",
        L"static", L"super", L"switch", L"this", L"throw", L"true", L"try",
        L"typeof", L"undefined", L"var", L"void", L"while", L"with", L"yield",
        L"type", L"enum", L"implements", L"namespace", L"declare", L"readonly",
        L"abstract", L"keyof", L"infer",
    };
    static const std::set<wxString> kPython = {
        L"False", L"None", L"True", L"and", L"as", L"assert", L"async",
        L"await", L"break", L"class", L"continue", L"def", L"del", L"elif",
        L"else", L"except", L"finally", L"for", L"from", L"global", L"if",
        L"import", L"in", L"is", L"lambda", L"nonlocal", L"not", L"or",
        L"pass", L"raise", L"return", L"try", L"while", L"with", L"yield",
        L"match", L"case",
    };
    static const std::set<wxString> kShell = {
        L"if", L"then", L"else", L"elif", L"fi", L"for", L"while", L"do",
        L"done", L"case", L"esac", L"function", L"in", L"local", L"export",
        L"return", L"exit", L"break", L"continue", L"shift", L"source",
        L"alias", L"echo", L"printf", L"cd", L"pwd", L"read", L"set", L"unset",
    };
    static const std::set<wxString> kJson = {
        L"true", L"false", L"null",
    };
    static const std::set<wxString> kSql = {
        L"select", L"from", L"where", L"insert", L"into", L"values", L"update",
        L"set", L"delete", L"create", L"table", L"drop", L"alter", L"add",
        L"primary", L"key", L"foreign", L"references", L"join", L"inner",
        L"left", L"right", L"outer", L"full", L"on", L"group", L"by",
        L"order", L"having", L"limit", L"offset", L"as", L"and", L"or",
        L"not", L"null", L"is", L"distinct", L"union", L"all", L"case",
        L"when", L"then", L"else", L"end", L"asc", L"desc",
    };
    static const std::set<wxString> kCss = {
        L"color", L"background", L"background-color", L"margin", L"padding",
        L"border", L"display", L"flex", L"grid", L"position", L"width",
        L"height", L"font", L"font-size", L"font-weight", L"line-height",
        L"text-align", L"overflow", L"opacity", L"transition", L"transform",
        L"content", L"gap", L"top", L"left", L"right", L"bottom",
    };
    static const std::set<wxString> kCSharp = {
        L"namespace", L"using", L"class", L"public", L"private", L"protected",
        L"internal", L"static", L"void", L"int", L"string", L"bool", L"double",
        L"float", L"decimal", L"var", L"new", L"return", L"if", L"else",
        L"for", L"foreach", L"while", L"switch", L"case", L"break", L"continue",
        L"try", L"catch", L"finally", L"throw", L"async", L"await", L"override",
        L"virtual", L"abstract", L"sealed", L"interface", L"enum", L"struct",
        L"record", L"object", L"null", L"true", L"false", L"this", L"base",
        L"out", L"ref", L"in", L"params",
    };
    static const std::set<wxString> kJava = {
        L"package", L"import", L"public", L"private", L"protected", L"class",
        L"interface", L"extends", L"implements", L"static", L"final", L"void",
        L"int", L"long", L"double", L"float", L"boolean", L"char", L"String",
        L"new", L"return", L"if", L"else", L"for", L"while", L"switch",
        L"case", L"break", L"continue", L"try", L"catch", L"finally", L"throw",
        L"throws", L"abstract", L"synchronized", L"volatile", L"transient",
        L"native", L"instanceof", L"this", L"super", L"null", L"true", L"false",
        L"enum", L"record", L"var",
    };
    static const std::set<wxString> kGo = {
        L"package", L"import", L"func", L"return", L"if", L"else", L"for",
        L"range", L"switch", L"case", L"break", L"continue", L"go", L"defer",
        L"chan", L"map", L"struct", L"interface", L"type", L"var", L"const",
        L"nil", L"true", L"false", L"iota", L"select", L"default", L"fallthrough",
    };
    static const std::set<wxString> kRust = {
        L"fn", L"let", L"mut", L"pub", L"use", L"mod", L"impl", L"trait",
        L"struct", L"enum", L"match", L"if", L"else", L"for", L"while", L"loop",
        L"return", L"break", L"continue", L"const", L"static", L"ref", L"move",
        L"async", L"await", L"where", L"self", L"Self", L"crate", L"super",
        L"dyn", L"true", L"false", L"unsafe", L"extern", L"type",
    };
    static const std::set<wxString> kPhp = {
        L"function", L"class", L"public", L"private", L"protected", L"static",
        L"echo", L"return", L"if", L"else", L"elseif", L"foreach", L"while",
        L"for", L"switch", L"case", L"break", L"continue", L"new", L"true",
        L"false", L"null", L"array", L"isset", L"unset", L"namespace", L"use",
    };
    static const std::set<wxString> kSwift = {
        L"func", L"class", L"struct", L"enum", L"protocol", L"extension",
        L"import", L"let", L"var", L"if", L"else", L"for", L"while", L"switch",
        L"case", L"break", L"continue", L"return", L"true", L"false", L"nil",
        L"self", L"super", L"guard", L"defer", L"async", L"await", L"throws",
        L"try", L"catch",
    };
    static const std::set<wxString> kLiterals = {
        L"true", L"false", L"null", L"nil", L"None", L"True", L"False",
        L"undefined", L"NaN", L"Infinity",
    };
    static const std::set<wxString> kEmpty;

    switch (dialect) {
        case CodeDialect::Plain:
        case CodeDialect::Markup:
            return kEmpty;
        case CodeDialect::Generic:
            return kLiterals;
        case CodeDialect::Json:
            return kJson;
        case CodeDialect::Python:
            return kPython;
        case CodeDialect::Shell:
            return kShell;
        case CodeDialect::Sql:
            return kSql;
        case CodeDialect::Css:
            return kCss;
        case CodeDialect::CLike:
            break;
    }

    if (lang == L"js" || lang == L"javascript" || lang == L"jsx" || lang == L"ts" ||
        lang == L"typescript" || lang == L"tsx") {
        return kJs;
    }
    if (lang == L"cs" || lang == L"csharp" || lang == L"c#") return kCSharp;
    if (lang == L"java" || lang == L"kt" || lang == L"kotlin") return kJava;
    if (lang == L"go" || lang == L"golang") return kGo;
    if (lang == L"rs" || lang == L"rust") return kRust;
    if (lang == L"php") return kPhp;
    if (lang == L"swift") return kSwift;
    return kCpp;
}

wxString ExpandTabs(const wxString& line, int tabSize) {
    wxString expanded;
    int column = 0;
    for (wxUniChar ch : line) {
        if (ch == '\r') continue;
        if (ch == '\t') {
            const int spaces = tabSize - (column % tabSize);
            for (int i = 0; i < spaces; ++i) expanded += ' ';
            column += spaces;
        } else {
            expanded += ch;
            ++column;
        }
    }
    return expanded;
}

void WriteCodeLine(wxRichTextCtrl* ctrl, const wxString& line, const wxRichTextAttr& base,
                   CodeDialect dialect, const std::set<wxString>& keywords,
                   CodeHighlightState& state) {
    // One Light 浅色配色，和代码块背景搭配。
    wxRichTextAttr keywordAttr = base;
    keywordAttr.SetTextColour(wxColour(0xA6, 0x26, 0xA4));

    wxRichTextAttr stringAttr = base;
    stringAttr.SetTextColour(wxColour(0x50, 0xA1, 0x4F));

    wxRichTextAttr numberAttr = base;
    numberAttr.SetTextColour(wxColour(0x98, 0x68, 0x01));

    wxRichTextAttr commentAttr = base;
    commentAttr.SetTextColour(wxColour(0x9A, 0x9B, 0xA1));
    wxFont commentFont = commentAttr.GetFont();
    if (commentFont.IsOk()) {
        commentFont.SetStyle(wxFONTSTYLE_ITALIC);
        commentAttr.SetFont(commentFont);
    }

    wxRichTextAttr functionAttr = base;
    functionAttr.SetTextColour(wxColour(0x40, 0x78, 0xF2));

    wxRichTextAttr variableAttr = base;
    variableAttr.SetTextColour(wxColour(0xE4, 0x56, 0x49));

    wxRichTextAttr tagAttr = base;
    tagAttr.SetTextColour(wxColour(0x9A, 0x9B, 0xA1));

    wxString plain;
    auto flush = [&]() {
        if (!plain.empty()) {
            WriteStyled(ctrl, plain, base);
            plain.clear();
        }
    };
    auto write = [&](const wxString& text, const wxRichTextAttr& attr) {
        if (text.empty()) return;
        flush();
        WriteStyled(ctrl, text, attr);
    };

    const long length = (long)line.length();
    long i = 0;
    while (i < length) {
        if (state.htmlComment) {
            const long end = (long)line.find(L"-->", (size_t)i);
            if (end == (long)wxString::npos) {
                write(line.Mid(i), commentAttr);
                return;
            }
            write(line.Mid(i, end + 3 - i), commentAttr);
            state.htmlComment = false;
            i = end + 3;
            continue;
        }
        if (state.blockComment) {
            const long end = (long)line.find(L"*/", (size_t)i);
            if (end == (long)wxString::npos) {
                write(line.Mid(i), commentAttr);
                return;
            }
            write(line.Mid(i, end + 2 - i), commentAttr);
            state.blockComment = false;
            i = end + 2;
            continue;
        }

        const wxUniChar ch = line[i];
        const bool hasNext = i + 1 < length;
        const bool slash2 = hasNext && ch == '/' && line[i + 1] == '/';
        const bool star2 = hasNext && ch == '*' && line[i + 1] == '*';
        const bool dash2 = hasNext && ch == '-' && line[i + 1] == '-';

        if (dialect == CodeDialect::Markup && ch == '<' && i + 3 < length &&
            line[i + 1] == '!' && line[i + 2] == '-' && line[i + 3] == '-') {
            const long end = (long)line.find(L"-->", (size_t)(i + 4));
            if (end == (long)wxString::npos) {
                write(line.Mid(i), commentAttr);
                state.htmlComment = true;
                return;
            }
            write(line.Mid(i, end + 3 - i), commentAttr);
            i = end + 3;
            continue;
        }

        if (star2 &&
            (dialect == CodeDialect::CLike || dialect == CodeDialect::Css ||
             dialect == CodeDialect::Sql || dialect == CodeDialect::Generic)) {
            const long end = (long)line.find(L"*/", (size_t)(i + 2));
            if (end == (long)wxString::npos) {
                write(line.Mid(i), commentAttr);
                state.blockComment = true;
                return;
            }
            write(line.Mid(i, end + 2 - i), commentAttr);
            i = end + 2;
            continue;
        }

        if ((dialect == CodeDialect::CLike || dialect == CodeDialect::Css ||
             dialect == CodeDialect::Generic) &&
            slash2) {
            write(line.Mid(i), commentAttr);
            return;
        }
        if (dialect == CodeDialect::Sql && dash2) {
            write(line.Mid(i), commentAttr);
            return;
        }
        if (dialect == CodeDialect::Python && ch == '#') {
            write(line.Mid(i), commentAttr);
            return;
        }
        if (dialect == CodeDialect::Shell && ch == '#' &&
            (i == 0 || IsSpaceChar(line[i - 1]))) {
            write(line.Mid(i), commentAttr);
            return;
        }
        if (dialect == CodeDialect::Generic && ch == '#' &&
            OnlyWhitespaceBefore(line, i)) {
            write(line.Mid(i), commentAttr);
            return;
        }

        // C/C++ 预处理指令
        if (dialect == CodeDialect::CLike && ch == '#' && OnlyWhitespaceBefore(line, i)) {
            long end = i + 1;
            while (end < length && (wxIsalnum(line[end]) || line[end] == '_')) ++end;
            write(line.Mid(i, end - i), keywordAttr);
            i = end;
            continue;
        }

        if (ch == '"' || ch == '\'' || ch == '`') {
            long end = i + 1;
            while (end < length) {
                if (line[end] == '\\') {
                    end += 2;
                    continue;
                }
                if (line[end] == ch) {
                    ++end;
                    break;
                }
                ++end;
            }
            if (end > length) end = length;
            write(line.Mid(i, end - i), stringAttr);
            i = end;
            continue;
        }

        if (dialect == CodeDialect::Markup && ch == '<') {
            long end = i + 1;
            if (end < length &&
                (line[end] == '/' || line[end] == '!' || line[end] == '?')) {
                ++end;
            }
            const long nameStart = end;
            while (end < length &&
                   (wxIsalnum(line[end]) || line[end] == '-' || line[end] == ':')) {
                ++end;
            }
            if (end > nameStart) {
                write(line.Mid(i, nameStart - i), tagAttr);
                write(line.Mid(nameStart, end - nameStart), keywordAttr);
                i = end;
                continue;
            }
        }

        if (wxIsdigit(ch)) {
            long end = i;
            while (end < length &&
                   (wxIsalnum(line[end]) || line[end] == '.' || line[end] == '_' ||
                    line[end] == '\'')) {
                ++end;
            }
            write(line.Mid(i, end - i), numberAttr);
            i = end;
            continue;
        }

        const bool dollarVariable = dialect == CodeDialect::Shell && ch == '$';
        const bool annotation = dialect == CodeDialect::CLike && ch == '@';
        if (wxIsalpha(ch) || ch == '_' || dollarVariable || annotation) {
            long end = i;
            if (dollarVariable || annotation) ++end;
            while (end < length &&
                   (wxIsalnum(line[end]) || line[end] == '_' || line[end] == '$')) {
                ++end;
            }
            const wxString word = line.Mid(i, end - i);
            if (dollarVariable) {
                write(word, variableAttr);
            } else if (annotation) {
                write(word, keywordAttr);
            } else if (keywords.count(word)) {
                write(word, keywordAttr);
            } else {
                long next = end;
                while (next < length && IsSpaceChar(line[next])) ++next;
                if (next < length && line[next] == '(' &&
                    (dialect == CodeDialect::CLike || dialect == CodeDialect::Python ||
                     dialect == CodeDialect::Shell || dialect == CodeDialect::Sql)) {
                    write(word, functionAttr);
                } else {
                    plain += word;
                }
            }
            i = end;
            continue;
        }

        plain += ch;
        ++i;
    }
    flush();
}

void RenderCodeBlock(wxRichTextCtrl* ctrl, const std::vector<wxString>& lines,
                     const wxString& language, CodeDialect dialect,
                     CodeHighlightState state, const wxRichTextAttr& base) {
    // 用单列表格承载代码块，让背景铺满整行，而不是只覆盖文字。
    const int paddingX = ctrl->FromDIP(12);
    const int paddingY = ctrl->FromDIP(8);
    const int cornerRadius = ctrl->FromDIP(6);

    wxRichTextAttr tableAttr;
    tableAttr.SetBackgroundColour(CodeBackgroundColour());
    tableAttr.GetTextBoxAttr().GetWidth().SetValue(100, wxTEXT_ATTR_UNITS_PERCENTAGE);
    tableAttr.GetTextBoxAttr().GetCornerRadius().SetValue(cornerRadius, wxTEXT_ATTR_UNITS_PIXELS);
    // 默认表格边框即使设为 NONE 也可能画出一条细线，把宽度也清零。
    tableAttr.GetTextBoxAttr().GetBorder().SetStyle(wxTEXT_BOX_ATTR_BORDER_NONE);
    tableAttr.GetTextBoxAttr().GetBorder().SetWidth(0, wxTEXT_ATTR_UNITS_PIXELS);
    tableAttr.GetTextBoxAttr().GetBorder().SetColour(CodeBackgroundColour());
    tableAttr.GetTextBoxAttr().GetOutline().SetStyle(wxTEXT_BOX_ATTR_BORDER_NONE);
    tableAttr.GetTextBoxAttr().GetOutline().SetWidth(0, wxTEXT_ATTR_UNITS_PIXELS);
    tableAttr.GetTextBoxAttr().GetShadow().Reset();

    wxRichTextAttr cellAttr = tableAttr;
    cellAttr.SetBackgroundColour(CodeBackgroundColour());
    cellAttr.GetTextBoxAttr().GetBorder().SetStyle(wxTEXT_BOX_ATTR_BORDER_NONE);
    cellAttr.GetTextBoxAttr().GetBorder().SetWidth(0, wxTEXT_ATTR_UNITS_PIXELS);
    cellAttr.GetTextBoxAttr().GetBorder().SetColour(CodeBackgroundColour());
    cellAttr.GetTextBoxAttr().GetOutline().SetStyle(wxTEXT_BOX_ATTR_BORDER_NONE);
    cellAttr.GetTextBoxAttr().GetOutline().SetWidth(0, wxTEXT_ATTR_UNITS_PIXELS);
    cellAttr.GetTextBoxAttr().GetShadow().Reset();
    cellAttr.GetTextBoxAttr().GetPadding().GetLeft().SetValue(paddingX, wxTEXT_ATTR_UNITS_PIXELS);
    cellAttr.GetTextBoxAttr().GetPadding().GetRight().SetValue(paddingX, wxTEXT_ATTR_UNITS_PIXELS);
    cellAttr.GetTextBoxAttr().GetPadding().GetTop().SetValue(paddingY, wxTEXT_ATTR_UNITS_PIXELS);
    cellAttr.GetTextBoxAttr().GetPadding().GetBottom().SetValue(paddingY, wxTEXT_ATTR_UNITS_PIXELS);

    wxRichTextTable* table = ctrl->WriteTable(1, 1, tableAttr, cellAttr);
    if (!table) return;
    wxRichTextCell* cell = table->GetCell(0, 0);
    if (!cell) return;

    wxRichTextAttr codeAttr = StyledAttr(base, false, false, false, true, false);
    // 背景由单元格统一绘制，文字本身不再带背景，避免出现一块一块的色条。
    codeAttr.RemoveFlag(wxTEXT_ATTR_BACKGROUND_COLOUR);
    codeAttr.SetLeftIndent(0);
    codeAttr.SetParagraphSpacingAfter(0);

    ctrl->SetFocusObject(cell);
    ctrl->SetDefaultStyle(codeAttr);
    for (size_t index = 0; index < lines.size(); ++index) {
        const wxString codeLine = ExpandTabs(lines[index], 4);
        if (!codeLine.empty()) {
            if (dialect == CodeDialect::Plain) {
                WriteStyled(ctrl, codeLine, codeAttr);
            } else {
                WriteCodeLine(ctrl, codeLine, codeAttr, dialect,
                              KeywordsFor(language, dialect), state);
            }
        }
        if (index + 1 < lines.size()) ctrl->Newline();
    }

    ctrl->SetFocusObject(nullptr);
    ctrl->SetInsertionPointEnd();
    ctrl->Newline();
}

void WriteInline(wxRichTextCtrl* ctrl, const wxString& text, const wxRichTextAttr& base) {
    wxString plain;
    auto flush = [&]() {
        if (!plain.empty()) {
            WriteStyled(ctrl, plain, base);
            plain.clear();
        }
    };

    long length = (long)text.length();
    long i = 0;
    while (i < length) {
        const wxUniChar ch = text[i];
        if (i + 1 < length) {
            const wxUniChar next = text[i + 1];
            const bool strong = (ch == '*' && next == '*') ||
                                (ch == '_' && next == '_') ||
                                (ch == '~' && next == '~');
            if (strong) {
                const wxString marker = text.Mid(i, 2);
                long close = (long)text.find(marker, (size_t)(i + 2));
                if (close != (long)wxString::npos) {
                    flush();
                    wxRichTextAttr attr =
                        StyledAttr(base, ch != '~', false, ch == '~', false, false);
                    WriteInline(ctrl, text.Mid(i + 2, close - i - 2), attr);
                    i = close + 2;
                    continue;
                }
                plain += ch;
                ++i;
                continue;
            }
            if (ch == '`' && next == '`') {
                // 行内连续两个反引号在 Markdown 里不常见，按普通文本处理
                plain += ch;
                ++i;
                continue;
            }
        }

        if (ch == '`') {
            long close = (long)text.find('`', (size_t)(i + 1));
            if (close != (long)wxString::npos) {
                flush();
                WriteStyled(ctrl, text.Mid(i + 1, close - i - 1),
                            StyledAttr(base, false, false, false, true, false));
                i = close + 1;
                continue;
            }
        } else if (ch == '*' || ch == '_') {
            wxString marker(ch);
            long close = (long)text.find(marker, (size_t)(i + 1));
            if (close != (long)wxString::npos && close > i + 1) {
                flush();
                WriteInline(ctrl, text.Mid(i + 1, close - i - 1),
                            StyledAttr(base, false, true, false, false, false));
                i = close + 1;
                continue;
            }
        } else if (ch == '[') {
            long labelEnd = (long)text.find(L"](", (size_t)(i + 1));
            if (labelEnd != (long)wxString::npos) {
                long urlEnd = (long)text.find(')', (size_t)(labelEnd + 2));
                if (urlEnd != (long)wxString::npos) {
                    flush();
                    WriteStyled(ctrl, text.Mid(i + 1, labelEnd - i - 1),
                                StyledAttr(base, false, false, false, false, true));
                    i = urlEnd + 1;
                    continue;
                }
            }
        }

        plain += ch;
        ++i;
    }
    flush();
}

std::vector<wxString> SplitLines(const wxString& text) {
    std::vector<wxString> lines;
    long start = 0;
    while (start <= (long)text.length()) {
        long newline = (long)text.find('\n', (size_t)start);
        if (newline == (long)wxString::npos) {
            lines.push_back(text.Mid(start));
            break;
        }
        lines.push_back(text.Mid(start, newline - start));
        start = newline + 1;
    }
    return lines;
}

int HeadingLevel(const wxString& line) {
    int level = 0;
    while (level < (int)line.length() && level < 6 && line[level] == '#') ++level;
    if (level > 0 && level < (int)line.length() && line[level] == ' ') return level;
    return 0;
}

bool HasBullet(const wxString& line) {
    return line.StartsWith(L"- ") || line.StartsWith(L"* ") || line.StartsWith(L"+ ");
}

wxString StripBullet(const wxString& line) {
    if (HasBullet(line)) return line.Mid(2);
    return line;
}

bool HasNumbered(const wxString& line) {
    int i = 0;
    while (i < (int)line.length() && wxIsdigit(line[i])) ++i;
    return i > 0 && i + 1 < (int)line.length() && line[i] == '.' && line[i + 1] == ' ';
}

wxString StripNumbered(const wxString& line) {
    if (!HasNumbered(line)) return line;
    int i = 0;
    while (i < (int)line.length() && wxIsdigit(line[i])) ++i;
    return line.Mid(i + 2);
}

bool HasQuote(const wxString& line) {
    return line.StartsWith(L"> ");
}

wxString StripQuote(const wxString& line) {
    if (HasQuote(line)) return line.Mid(2);
    if (line == L">") return wxString();
    return line;
}

bool IsHorizontalRule(const wxString& line) {
    wxString value = line;
    value.Trim(true).Trim(false);
    if (value.length() < 3) return false;
    wxUniChar marker = value[0];
    if (marker != '-' && marker != '*' && marker != '_') return false;
    for (wxUniChar ch : value) {
        if (ch != marker) return false;
    }
    return true;
}


}  // namespace

void RenderMarkdownPreview(wxRichTextCtrl* ctrl, const wxString& markdown) {
    ctrl->Freeze();
    ctrl->Clear();
    ctrl->SetEditable(false);
    // 默认会在表格/单元格周围画灰色辅助线，预览里不需要。
    ctrl->SetExtraStyle(ctrl->GetExtraStyle() | wxRICHTEXT_EX_NO_GUIDELINES);

    wxRichTextAttr base = BaseAttr(11);
    ctrl->SetDefaultStyle(base);

    wxString trimmedInput = markdown;
    trimmedInput.Trim(true).Trim(false);
    if (trimmedInput.empty()) {
        ctrl->Thaw();
        ctrl->SetInsertionPoint(0);
        return;
    }

    std::vector<wxString> lines = SplitLines(markdown);
    bool inCode = false;
    wxString codeLanguage;
    CodeDialect codeDialect = CodeDialect::Plain;
    std::vector<wxString> codeLines;

    for (const wxString& sourceLine : lines) {
        wxString line = sourceLine;
        wxString trimmed = sourceLine;
        trimmed.Trim(true).Trim(false);

        if (trimmed.StartsWith(L"```")) {
            if (!inCode) {
                inCode = true;
                codeLanguage = NormalizeLanguageInfo(trimmed.Mid(3));
                codeDialect = DialectForLanguage(codeLanguage);
                codeLines.clear();
            } else {
                RenderCodeBlock(ctrl, codeLines, codeLanguage, codeDialect,
                                CodeHighlightState(), base);
                inCode = false;
                codeLines.clear();
            }
            continue;
        }

        if (inCode) {
            codeLines.push_back(line);
            continue;
        }

        if (trimmed.empty()) {
            ctrl->SetDefaultStyle(BaseAttr(11));
            ctrl->Newline();
            continue;
        }

        if (IsHorizontalRule(trimmed)) {
            wxRichTextAttr rule = BaseAttr(10);
            rule.SetTextColour(Theme::BorderStrong());
            ctrl->SetDefaultStyle(rule);
            WriteStyled(ctrl, L"────────────────────────", rule);
            ctrl->Newline();
            continue;
        }

        int heading = HeadingLevel(sourceLine);
        if (heading > 0) {
            static const int kSizes[] = {22, 19, 17, 15, 14, 13};
            wxRichTextAttr attr = BaseAttr(kSizes[heading - 1]);
            wxFont font = attr.GetFont();
            font.SetWeight(wxFONTWEIGHT_BOLD);
            attr.SetFont(font);
            attr.SetParagraphSpacingBefore(RichUnits(8));
            attr.SetParagraphSpacingAfter(RichUnits(6));
            ctrl->SetDefaultStyle(attr);
            WriteInline(ctrl, sourceLine.Mid(heading + 1), attr);
            ctrl->Newline();
            continue;
        }

        if (HasQuote(sourceLine)) {
            wxRichTextAttr attr = BaseAttr(11);
            attr.SetLeftIndent(RichUnits(22));
            attr.SetTextColour(Theme::TextMuted());
            wxFont font = attr.GetFont();
            font.SetStyle(wxFONTSTYLE_ITALIC);
            attr.SetFont(font);
            ctrl->SetDefaultStyle(attr);
            WriteInline(ctrl, StripQuote(sourceLine), attr);
            ctrl->Newline();
            continue;
        }

        if (HasBullet(sourceLine) || HasNumbered(sourceLine)) {
            wxRichTextAttr attr = BaseAttr(11);
            attr.SetLeftIndent(RichUnits(22));
            ctrl->SetDefaultStyle(attr);
            wxString marker;
            wxString content;
            if (HasBullet(sourceLine)) {
                marker = L"•  ";
                content = StripBullet(sourceLine);
            } else {
                int i = 0;
                while (i < (int)sourceLine.length() && wxIsdigit(sourceLine[i])) ++i;
                marker = sourceLine.Mid(0, i + 1) + L"  ";
                content = StripNumbered(sourceLine);
            }
            wxRichTextAttr markerAttr = attr;
            markerAttr.SetTextColour(Theme::TextFaint());
            WriteStyled(ctrl, marker, markerAttr);
            WriteInline(ctrl, content, attr);
            ctrl->Newline();
            continue;
        }

        ctrl->SetDefaultStyle(BaseAttr(11));
        WriteInline(ctrl, sourceLine, base);
        ctrl->Newline();
    }

    if (inCode) {
        // 没有闭合的围栏也按代码块显示，避免内容丢失
        RenderCodeBlock(ctrl, codeLines, codeLanguage, codeDialect,
                        CodeHighlightState(), base);
    }

    ctrl->Thaw();
    ctrl->SetInsertionPoint(0);
}
