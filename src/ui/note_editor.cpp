#include "ui/note_editor.h"

#include "core/note_storage.h"
#include "ui/app_font.h"
#include "ui/theme.h"

#include <wx/fontdlg.h>
#include <wx/frame.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/stopwatch.h>
#include <wx/textdlg.h>
#include <wx/utils.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

namespace {

constexpr int kStrikeIndicator = 20;

std::string Utf8Of(const wxString& text) {
    const wxScopedCharBuffer buffer = text.ToUTF8();
    return std::string(buffer.data(), buffer.length());
}

wxString WideOfUtf8(const std::string& text) {
    return wxString::FromUTF8(text.c_str(), text.size());
}

wxColour ParseColour(const std::string& value, const wxColour& fallback) {
    if (value.size() == 7 && value[0] == '#') {
        auto nibble = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            return -1;
        };
        const int r = nibble(value[1]) * 16 + nibble(value[2]);
        const int g = nibble(value[3]) * 16 + nibble(value[4]);
        const int b = nibble(value[5]) * 16 + nibble(value[6]);
        if (r >= 0 && g >= 0 && b >= 0) {
            return wxColour((unsigned char)r, (unsigned char)g,
                            (unsigned char)b);
        }
    }
    return fallback;
}

}  // namespace

NoteEditor::NoteEditor(wxWindow* parent, wxWindowID id)
    : wxStyledTextCtrl(parent, id, wxDefaultPosition, wxDefaultSize,
                       wxBORDER_NONE) {
    baseFont_ = AppFont::Content();
    styles_.SetDefaultStyle(MakeDefaultStyle());
    ConfigureEditor();
    ConfigureStyles();

    Bind(wxEVT_STC_MODIFIED, &NoteEditor::OnModified, this);
    Bind(wxEVT_STC_HOTSPOT_CLICK, &NoteEditor::OnHotspotClick, this);
    Bind(wxEVT_KEY_DOWN, &NoteEditor::OnKeyDown, this);
    Bind(wxEVT_LEFT_DOWN, &NoteEditor::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &NoteEditor::OnLeftUp, this);
    Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
        // 跨显示器后 DPI 变化，边距宽度和字体像素尺寸都要重新计算。
        SetMarginWidth(1, FromDIP(1));
        lineNumberDigits_ = 0;
        UpdateLineNumberMargin();
        event.Skip();
    });
}

TextStyle NoteEditor::MakeDefaultStyle() const {
    TextStyle style;
    style.fontFamily = Utf8Of(baseFont_.GetFaceName());
    style.sizeHundredths = baseFont_.GetPointSize() * 100;
    style.foreground = HexOf(Theme::Text());
    style.background = HexOf(Theme::Surface());
    return style;
}
void NoteEditor::ConfigureEditor() {
    SetLexer(wxSTC_LEX_NULL);
    SetCodePage(wxSTC_CP_UTF8);
    // 默认 5 个 style bit 只能表示 32 个样式；便签需要完整的 0–255 槽，
    // 其中 32–39 保留给 Scintilla 预定义样式。
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    SetStyleBits(8);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    SetWrapMode(wxSTC_WRAP_WORD);
    SetWrapVisualFlags(0);
    SetWrapIndentMode(wxSTC_WRAPINDENT_FIXED);
    SetUseHorizontalScrollBar(false);
    SetScrollWidthTracking(true);
    SetMarginLeft(FromDIP(4));
    SetMarginRight(FromDIP(4));
    for (int margin = 0; margin < 5; ++margin) SetMarginWidth(margin, 0);

    // 行号使用 Scintilla 内置的数字边距：只绘制可见行，不会随正文规模增加
    // 绘制成本。宽度按当前行数位数计算，位数不变时不做任何调整。
    SetMarginType(0, wxSTC_MARGIN_NUMBER);
    SetMarginSensitive(0, false);
    SetMarginCursor(0, wxSTC_CURSORARROW);
    SetMarginBackground(0, Theme::Surface());

    // 行号右边加 1 像素分隔线：使用纯色 margin，不参与文本排版。
    SetMarginType(1, wxSTC_MARGIN_COLOUR);
    SetMarginWidth(1, FromDIP(1));
    SetMarginBackground(1, Theme::Border());
    SetMarginCursor(1, wxSTC_CURSORARROW);
    ConfigureLineNumberStyle();
    lineNumberDigits_ = 0;
    UpdateLineNumberMargin();
    SetCaretLineVisible(false);
    SetEdgeMode(wxSTC_EDGE_NONE);
    SetUseTabs(false);
    SetTabWidth(4);
    SetEOLMode(wxSTC_EOL_LF);
    SetUndoCollection(true);
    SetModEventMask(wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT |
                    wxSTC_MOD_BEFOREDELETE);
    SetBackgroundColour(Theme::Surface());
    SetForegroundColour(Theme::Text());
    SetCaretForeground(Theme::Text());
    SetSelForeground(true, Theme::Text());
    SetSelBackground(true, Theme::AccentSoft());

    SetIndicatorCurrent(kStrikeIndicator);
    IndicatorSetStyle(kStrikeIndicator, wxSTC_INDIC_STRIKE);
    IndicatorSetForeground(kStrikeIndicator, Theme::TextMuted());
    IndicatorSetUnder(kStrikeIndicator, true);
}

uint32_t NoteEditor::ScintillaSlotForStyle(uint32_t styleId) {
    // 32–39 是 Scintilla 的预定义样式，正文样式必须跳过。
    if (styleId < 32) return styleId;
    return styleId + 8;
}

uint32_t NoteEditor::StyleForScintillaSlot(uint32_t slot) {
    if (slot < 32) return slot;
    if (slot < 40) return StyleModel::kDefaultStyleId;
    return slot - 8;
}

void NoteEditor::UpdateLineNumberMargin() {
    int lines = GetLineCount();
    if (lines < 1) lines = 1;
    int digits = 1;
    for (int n = lines; n >= 10; n /= 10) ++digits;
    if (digits == lineNumberDigits_) return;
    lineNumberDigits_ = digits;

    // 用等宽数字估算宽度；位数不变时这里直接返回，不会影响输入。
    const wxString sample(wxT('9'), (size_t)digits);
    const int textWidth = TextWidth(wxSTC_STYLE_LINENUMBER, sample);
    const int width = std::max(textWidth + FromDIP(10), FromDIP(22));
    SetMarginWidth(0, width);
}

void NoteEditor::ConfigureLineNumberStyle() {
    StyleSetFont(wxSTC_STYLE_LINENUMBER,
                 AppFont::Make(std::max(8, baseFont_.GetPointSize() - 2)));
    StyleSetForeground(wxSTC_STYLE_LINENUMBER, Theme::TextFaint());
    StyleSetBackground(wxSTC_STYLE_LINENUMBER, Theme::Surface());
}

void NoteEditor::ConfigureSlot(uint32_t styleId) {
    const uint32_t slot = ScintillaSlotForStyle(styleId);
    if (slot >= 256) return;
    ConfigureSlotStyle(slot, styles_.GetStyle(styleId));
    slotConfigured_[slot] = true;
}

void NoteEditor::ConfigureSlotStyle(uint32_t slot, const TextStyle& style) {
    if (slot >= 256) return;
    const TextStyle& fallback = styles_.DefaultStyle();

    std::string face = style.fontFamily.empty() ? fallback.fontFamily
                                                : style.fontFamily;
    int size = style.sizeHundredths > 0 ? style.sizeHundredths
                                        : fallback.sizeHundredths;
    if (size <= 0) size = baseFont_.GetPointSize() * 100;

    StyleSetFaceName((int)slot, WideOfUtf8(face));
    StyleSetSizeFractional((int)slot, size);
    StyleSetBold((int)slot, style.bold);
    StyleSetItalic((int)slot, style.italic);
    StyleSetUnderline((int)slot, style.underline);
    StyleSetForeground(
        (int)slot,
        ColourOf(style.foreground.empty() ? fallback.foreground : style.foreground,
                 Theme::Text()));
    StyleSetBackground(
        (int)slot,
        ColourOf(style.background.empty() ? fallback.background : style.background,
                 Theme::Surface()));
}

void NoteEditor::RecycleUnusedStyles() {
    // Scintilla 的全局行高取所有 style 定义的最大值。字体放大后即使文本已经
    // 缩回默认样式，那个更大的 style 槽仍会把光标和行距撑高，所以这里把
    // 没有被任何区间使用的槽恢复成默认定义；下次真正用到时再重新配置。
    std::vector<bool> used(256, false);
    used[0] = true;
    for (const StyleRun& run : styles_.Runs()) {
        const uint32_t slot = ScintillaSlotForStyle(run.styleId);
        if (slot < 256) used[slot] = true;
    }
    for (uint32_t slot = 1; slot < 256; ++slot) {
        if (!used[slot] && slotConfigured_[slot]) {
            ConfigureSlotStyle(slot, styles_.DefaultStyle());
            slotConfigured_[slot] = false;
        }
    }
}

bool NoteEditor::CompactStyles() {
    if (!styles_.Compact()) return false;
    hasPending_ = false;
    pendingStyleId_ = 0;
    ConfigureStyles();
    ApplyRange(0, GetTextLength());
    Refresh();
    return true;
}

void NoteEditor::ConfigureStyles() {
    slotConfigured_.assign(256, false);
    StyleClearAll();
    // StyleClearAll 会把行号样式恢复到默认值，必须重新设置。
    ConfigureLineNumberStyle();
    lineNumberDigits_ = 0;
    UpdateLineNumberMargin();

    const std::vector<TextStyle>& table = styles_.Styles();
    for (size_t i = 0; i < table.size() && i < StyleModel::kMaxStyles; ++i) {
        ConfigureSlot((uint32_t)i);
    }
    EnsureSlots();
    RefreshLinkHotspots();
}

void NoteEditor::RefreshLinkHotspots() {
    // 热点是样式槽属性，不是区间属性。一个链接可能横跨多个样式槽
    // （例如链接中间一部分被加粗），必须把范围内每个槽都标记出来。
    std::vector<bool> isLink(256, false);
    for (const LinkRun& link : styles_.Links().Runs()) {
        styles_.ForEachRun(link.start, link.end, [&](const StyleRun& run) {
            const uint32_t slot = ScintillaSlotForStyle(run.styleId);
            if (slot < 256) isLink[slot] = true;
        });
    }
    for (uint32_t slot = 1; slot < 256; ++slot) {
        StyleSetHotSpot((int)slot, isLink[slot]);
    }
    // 0 号槽是全文默认样式，不能因为链接数据异常把整篇正文变成热点。
    StyleSetHotSpot(0, false);
}

void NoteEditor::EnsureSlots() {
    const std::vector<TextStyle>& table = styles_.Styles();
    for (size_t i = 0; i < table.size() && i < StyleModel::kMaxStyles; ++i) {
        const uint32_t slot = ScintillaSlotForStyle((uint32_t)i);
        if (!slotConfigured_[slot]) ConfigureSlot((uint32_t)i);
    }
}

void NoteEditor::SetDocument(const std::string& text, const StyleModel& styles) {
    loading_ = true;
    Freeze();
    SetUndoCollection(false);
    ClearAll();
    if (!text.empty()) {
        AddTextRaw(text.data(), (int)text.size());
    }
    UpdateLineNumberMargin();
    styles_ = styles;
    styles_.RemoveNewlineStyles(text);
    styles_.Compact();
    // 正文可能被外部改动过；去掉越界的链接，避免点击到错误位置。
    styles_.Links().SetLinks(styles_.Links().Runs(), (long)text.size());
    SetUndoCollection(true);
    EmptyUndoBuffer();
    SetSavePoint();

    dirty_ = false;
    hasPending_ = false;
    pendingStyleId_ = 0;
    charCount_ = CodepointCount(text);

    ConfigureStyles();
    SetIndicatorCurrent(kStrikeIndicator);
    IndicatorClearRange(0, GetTextLength());
    for (const StyleRun& run : styles_.Runs()) {
        if (run.start < 0 || run.end <= run.start ||
            run.end > GetTextLength() ||
            run.styleId >= StyleModel::kMaxStyles) {
            continue;
        }
        ApplyRun(run.start, run.end, run.styleId);
    }
    RecycleUnusedStyles();

    SetInsertionPoint(0);
    ScrollToLine(0);
    loading_ = false;
    Thaw();
    Refresh();
}

std::string NoteEditor::GetTextUtf8() const {
    NoteEditor* self = const_cast<NoteEditor*>(this);
    const wxCharBuffer buffer =
        self->GetTextRangeRaw(0, self->GetTextLength());
    return std::string(buffer.data(), buffer.length());
}

bool NoteEditor::HasSelection() const {
    return GetSelectionStart() != GetSelectionEnd();
}

TextStyle NoteEditor::StyleAtCaret() const {
    long pos = GetCurrentPos();
    const long start = GetSelectionStart();
    const long end = GetSelectionEnd();
    if (start != end) pos = std::min(start, end);
    return styles_.StyleAt(pos);
}

uint32_t NoteEditor::StyleIdForInsertion(long pos) const {
    if (hasPending_) return pendingStyleId_;
    uint32_t id = styles_.StyleIdAt(pos);
    if (id == 0 && pos > 0) {
        // 只继承同一行前一个字符的样式；不跨行继承上一行标题。
        const int line = LineFromPosition((int)pos);
        const long lineStart = PositionFromLine(line);
        if (pos > lineStart) id = styles_.StyleIdAt(pos - 1);
    }
    return id;
}

void NoteEditor::ApplyRun(long start, long end, uint32_t styleId) {
    if (end <= start || start < 0) return;
    if (styleId >= StyleModel::kMaxStyles) styleId = 0;
    const uint32_t slot = ScintillaSlotForStyle(styleId);
    StartStyling((int)start);
    SetStyling((int)(end - start), (int)slot);

    SetIndicatorCurrent(kStrikeIndicator);
    IndicatorClearRange((int)start, (int)(end - start));
    if (styles_.GetStyle(styleId).strike) {
        IndicatorFillRange((int)start, (int)(end - start));
    }
}

void NoteEditor::ApplyStrike(long start, long end) {
    if (end <= start) return;
    SetIndicatorCurrent(kStrikeIndicator);
    IndicatorClearRange((int)start, (int)(end - start));
    styles_.ForEachRun(start, end, [this](const StyleRun& run) {
        if (styles_.GetStyle(run.styleId).strike) {
            IndicatorFillRange((int)run.start, (int)(run.end - run.start));
        }
    });
}

void NoteEditor::ApplyRange(long start, long end) {
    if (start < 0) start = 0;
    const long length = GetTextLength();
    if (end > length) end = length;
    if (end <= start) return;

    StartStyling((int)start);
    long cursor = start;
    styles_.ForEachRun(start, end, [&](const StyleRun& run) {
        const long runStart = std::max(run.start, start);
        const long runEnd = std::min(run.end, end);
        if (runEnd <= runStart) return;
        if (runStart > cursor) SetStyling((int)(runStart - cursor), 0);
        const uint32_t slot = ScintillaSlotForStyle(run.styleId);
        SetStyling((int)(runEnd - runStart), (int)slot);
        cursor = std::max(cursor, runEnd);
    });
    if (cursor < end) SetStyling((int)(end - cursor), 0);
    ApplyStrike(start, end);
}

void NoteEditor::ApplyPatchToRange(long start, long end, const StylePatch& patch,
                                   const std::string* linkUrl) {
    if (end <= start) return;
    if (styles_.StyleCount() >= StyleModel::kMaxStyles) CompactStyles();
    applying_ = true;
    styles_.ApplyPatch(start, end, patch);
    if (patch.clearAll) {
        // 清除格式同时清除链接；否则会留下“看不见但能点”的链接文字。
        styles_.Links().ClearRange(start, end);
    }
    if (linkUrl) {
        styles_.Links().SetRange(start, end, *linkUrl);
    }
    EnsureSlots();
    ApplyRange(start, end);
    const wxCharBuffer rangeText = GetTextRangeRaw((int)start, (int)end);
    styles_.RemoveNewlineStylesInRange(
        start, std::string(rangeText.data(), rangeText.length()));
    RecycleUnusedStyles();
    // 改字号/粗体/颜色会生成新的样式槽，链接文字也可能换槽；
    // 每轮样式操作后都重新标记热点，保证链接仍然可点。
    RefreshLinkHotspots();
    applying_ = false;
    dirty_ = true;
    NotifyChange();
    Refresh();
}

void NoteEditor::SetPending(const StylePatch& patch) {
    if (styles_.StyleCount() >= StyleModel::kMaxStyles) CompactStyles();
    const TextStyle next = patch.Apply(styles_.StyleAt(GetCurrentPos()));
    pendingStyleId_ = styles_.Intern(next);
    EnsureSlots();
    RecycleUnusedStyles();
    hasPending_ = true;
}

void NoteEditor::ApplyCommand(FormatCommand command) {
    // 应用一次明确命令后，之前“无选区时设置后续输入样式”的 pending 状态
    // 必须失效；否则先设大字号、再清除选区样式，后续输入仍会继承大字号。
    hasPending_ = false;
    pendingStyleId_ = 0;
    SetPainterArmed(false);

    const bool selection = HasSelection();
    const int selectionStart = GetSelectionStart();
    const int selectionEnd = GetSelectionEnd();
    const long start = selection ? std::min(selectionStart, selectionEnd)
                                 : GetCurrentPos();
    const long end = selection ? std::max(selectionStart, selectionEnd)
                               : GetCurrentPos();
    const TextStyle base = styles_.StyleAt(start > 0 ? start : 0);
    const long lineStart = PositionFromLine(GetCurrentLine());
    const long lineEnd = GetLineEndPosition(GetCurrentLine());

    auto applyLineOrSelection = [&](const StylePatch& patch) {
        if (selection) {
            ApplyPatchToRange(start, end, patch);
        } else if (lineEnd > lineStart) {
            ApplyPatchToRange(lineStart, lineEnd, patch);
        } else {
            SetPending(patch);
        }
    };

    auto applySelectionOrPending = [&](const StylePatch& patch) {
        if (selection) {
            ApplyPatchToRange(start, end, patch);
        } else {
            SetPending(patch);
        }
    };

    switch (command) {
        case FormatCommand::Body: {
            StylePatch patch;
            patch.clearAll = true;
            applyLineOrSelection(patch);
            break;
        }
        case FormatCommand::Bold: {
            StylePatch patch;
            patch.setBold = true;
            patch.bold = !base.bold;
            applySelectionOrPending(patch);
            break;
        }
        case FormatCommand::Italic: {
            StylePatch patch;
            patch.setItalic = true;
            patch.italic = !base.italic;
            applySelectionOrPending(patch);
            break;
        }
        case FormatCommand::Underline: {
            StylePatch patch;
            patch.setUnderline = true;
            patch.underline = !base.underline;
            applySelectionOrPending(patch);
            break;
        }
        case FormatCommand::Strike: {
            StylePatch patch;
            patch.setStrike = true;
            patch.strike = !base.strike;
            applySelectionOrPending(patch);
            break;
        }
        case FormatCommand::FontSmaller:
        case FormatCommand::FontLarger: {
            int size = base.sizeHundredths > 0
                           ? base.sizeHundredths
                           : styles_.DefaultStyle().sizeHundredths;
            if (size <= 0) size = baseFont_.GetPointSize() * 100;
            size += (command == FormatCommand::FontLarger) ? 100 : -100;
            size = std::max(600, std::min(7200, size));
            StylePatch patch;
            patch.setSize = true;
            patch.sizeHundredths = size;
            applySelectionOrPending(patch);
            break;
        }
        case FormatCommand::ChooseFont: {
            wxFont current = baseFont_;
            if (!base.fontFamily.empty()) {
                current.SetFaceName(WideOfUtf8(base.fontFamily));
            }
            if (base.sizeHundredths > 0) {
                current.SetPointSize(std::max(1, base.sizeHundredths / 100));
            }
            current.SetWeight(base.bold ? wxFONTWEIGHT_BOLD
                                        : wxFONTWEIGHT_NORMAL);
            current.SetStyle(base.italic ? wxFONTSTYLE_ITALIC
                                         : wxFONTSTYLE_NORMAL);
            current.SetUnderlined(base.underline);

            wxFontData fontData;
            fontData.SetInitialFont(current);
            wxFontDialog dialog(this, fontData);
            if (dialog.ShowModal() != wxID_OK) break;
            const wxFont chosen = dialog.GetFontData().GetChosenFont();
            StylePatch patch;
            patch.setFont = true;
            patch.fontFamily = Utf8Of(chosen.GetFaceName());
            patch.setSize = true;
            patch.sizeHundredths = chosen.GetPointSize() * 100;
            patch.setBold = true;
            patch.bold = chosen.GetWeight() >= wxFONTWEIGHT_BOLD;
            patch.setItalic = true;
            patch.italic = chosen.GetStyle() != wxFONTSTYLE_NORMAL;
            patch.setUnderline = true;
            patch.underline = chosen.GetUnderlined();
            applySelectionOrPending(patch);
            break;
        }
        case FormatCommand::Hyperlink: {
            if (!HasSelection()) {
                wxMessageBox(L"请先选择要设置为链接的文字。", L"插入链接",
                             wxOK | wxICON_INFORMATION, this);
                break;
            }
            wxTextEntryDialog dialog(this, L"链接地址", L"插入链接", L"https://");
            if (dialog.ShowModal() != wxID_OK) break;
            wxString url = dialog.GetValue();
            url.Trim(true).Trim(false);
            if (url.empty()) break;
            // 链接样式（蓝色下划线）和 URL 分开保存：URL 不进入样式表。
            ApplyHyperlink(start, end, Utf8Of(url));
            break;
        }
        case FormatCommand::ClearFormatting: {
            StylePatch patch;
            patch.clearAll = true;
            applyLineOrSelection(patch);
            break;
        }
        default:
            break;
    }
}

NoteEditor::FormatPainterResult NoteEditor::ApplyFormatPainter() {
    hasPending_ = false;
    pendingStyleId_ = 0;

    if (!formatPainterArmed_) {
        long pos = GetCurrentPos();
        if (HasSelection()) {
            pos = std::min(GetSelectionStart(), GetSelectionEnd());
        }
        formatPainterStyle_ = styles_.StyleAt(pos);
        // 格式刷只复制字符格式；链接地址保存在 LinkModel 里，本来就不会被复制。
        SetPainterArmed(true);
        return FormatPainterResult::Copied;
    }

    long start = 0;
    long end = 0;
    if (HasSelection()) {
        start = std::min(GetSelectionStart(), GetSelectionEnd());
        end = std::max(GetSelectionStart(), GetSelectionEnd());
    } else {
        start = PositionFromLine(GetCurrentLine());
        end = GetLineEndPosition(GetCurrentLine());
    }
    if (end <= start) return FormatPainterResult::Nothing;
    if (styles_.StyleCount() >= StyleModel::kMaxStyles) CompactStyles();

    applying_ = true;
    const uint32_t styleId = styles_.Intern(formatPainterStyle_);
    EnsureSlots();
    styles_.SetRange(start, end, styleId);
    // 格式刷只改外观；目标上的链接地址一起清除，避免样式被覆盖后留下
    // 看不见、点不到的链接。
    styles_.Links().ClearRange(start, end);
    ApplyRange(start, end);
    const wxCharBuffer rangeText = GetTextRangeRaw((int)start, (int)end);
    styles_.RemoveNewlineStylesInRange(
        start, std::string(rangeText.data(), rangeText.length()));
    RecycleUnusedStyles();
    // 格式刷也可能改变链接文字的样式槽，重新标记热点。
    RefreshLinkHotspots();
    applying_ = false;
    dirty_ = true;
    NotifyChange();
    SetPainterArmed(false);
    Refresh();
    return FormatPainterResult::Applied;
}

void NoteEditor::ApplyForeground(const wxColour& colour, bool clear) {
    hasPending_ = false;
    pendingStyleId_ = 0;
    SetPainterArmed(false);
    StylePatch patch;
    patch.setForeground = true;
    patch.foreground = clear ? std::string() : HexOf(colour);
    if (HasSelection()) {
        const int a = GetSelectionStart();
        const int b = GetSelectionEnd();
        ApplyPatchToRange(std::min(a, b), std::max(a, b), patch);
    } else {
        SetPending(patch);
    }
}

bool NoteEditor::FindNext(const wxString& text, bool matchCase, bool wholeWord) {
    if (text.empty()) return false;
    const wxCharBuffer findUtf8 = text.ToUTF8();
    const int findLength = (int)findUtf8.length();
    if (findLength <= 0) return false;

    int flags = 0;
    if (matchCase) flags |= wxSTC_FIND_MATCHCASE;
    if (wholeWord) flags |= wxSTC_FIND_WHOLEWORD;

    int start = GetSelectionEnd();
    const int textLength = GetTextLength();
    if (start >= textLength) start = 0;

    for (int attempt = 0; attempt < 2; ++attempt) {
        SetTargetStart(start);
        SetTargetEnd(textLength);
        SetSearchFlags(flags);
        const int found = SearchInTarget(text);
        if (found >= 0) {
            SetSelection(found, GetTargetEnd());
            EnsureCaretVisible();
            SetFocus();
            return true;
        }
        if (start == 0) break;
        start = 0;
    }
    return false;
}

bool NoteEditor::ReplaceCurrent(const wxString& replacement) {
    const int start = GetSelectionStart();
    const int end = GetSelectionEnd();
    if (start == end) return false;
    const wxCharBuffer replacementUtf8 = replacement.ToUTF8();
    SetTargetStart(start);
    SetTargetEnd(end);
    ReplaceTarget(replacement);
    const int next = start + (int)replacementUtf8.length();
    SetSelection(next, next);
    return true;
}

int NoteEditor::ReplaceAll(const wxString& text, const wxString& replacement,
                           bool matchCase, bool wholeWord) {
    if (text.empty()) return 0;
    const wxCharBuffer findUtf8 = text.ToUTF8();
    const wxCharBuffer replacementUtf8 = replacement.ToUTF8();
    const int findLength = (int)findUtf8.length();
    const int replacementLength = (int)replacementUtf8.length();
    if (findLength <= 0) return 0;

    int flags = 0;
    if (matchCase) flags |= wxSTC_FIND_MATCHCASE;
    if (wholeWord) flags |= wxSTC_FIND_WHOLEWORD;

    int count = 0;
    int start = 0;
    BeginUndoAction();
    while (start <= GetTextLength()) {
        SetTargetStart(start);
        SetTargetEnd(GetTextLength());
        SetSearchFlags(flags);
        const int found = SearchInTarget(text);
        if (found < 0) break;
        SetTargetStart(found);
        SetTargetEnd(found + findLength);
        ReplaceTarget(replacement);
        ++count;
        start = found + replacementLength;
        if (replacementLength == 0 && start == found) ++start;
    }
    EndUndoAction();
    if (count > 0) {
        SetSelection(start, start);
        EnsureCaretVisible();
        SetFocus();
    }
    return count;
}


void NoteEditor::ApplyBackground(const wxColour& colour, bool clear) {
    hasPending_ = false;
    pendingStyleId_ = 0;
    SetPainterArmed(false);
    StylePatch patch;
    patch.setBackground = true;
    patch.background = clear ? std::string() : HexOf(colour);
    if (HasSelection()) {
        const int a = GetSelectionStart();
        const int b = GetSelectionEnd();
        ApplyPatchToRange(std::min(a, b), std::max(a, b), patch);
    } else {
        SetPending(patch);
    }
}

void NoteEditor::ApplyHyperlink(long start, long end, const std::string& url) {
    if (url.empty() || end <= start) return;
    hasPending_ = false;
    pendingStyleId_ = 0;
    SetPainterArmed(false);
    StylePatch patch;
    patch.setForeground = true;
    patch.foreground = HexOf(Theme::Accent());
    patch.setUnderline = true;
    patch.underline = true;
    ApplyPatchToRange(start, end, patch, &url);
}

void NoteEditor::MarkSaved() {
    dirty_ = false;
    SetSavePoint();
}

void NoteEditor::FocusEditor() {
    SetFocus();
}

void NoteEditor::NotifyChange() {
    if (changeHandler_) changeHandler_();
}

void NoteEditor::SetPainterArmed(bool armed) {
    if (formatPainterArmed_ == armed) return;
    formatPainterArmed_ = armed;
    if (painterArmedHandler_) painterArmedHandler_(armed);
}

void NoteEditor::RebuildStylesFromEditor() {
    if (loading_ || applying_) return;
    const long length = GetTextLength();

    // 大文件逐字节扫描会卡住数秒；插入/删除事件已经增量维护区间，
    // 这里只对小文档按 style byte 做一次校正。
    constexpr long kRebuildLimit = 2 * 1024 * 1024;
    if (length > kRebuildLimit) {
        EnsureSlots();
        RecycleUnusedStyles();
        RefreshLinkHotspots();
        return;
    }

    std::vector<StyleRun> runs;
    long runStart = 0;
    uint32_t runStyle = 0;
    if (length > 0) {
        runStyle = StyleForScintillaSlot((uint32_t)GetStyleAt(0));
        for (long pos = 1; pos < length; ++pos) {
            const uint32_t style =
                StyleForScintillaSlot((uint32_t)GetStyleAt((int)pos));
            if (style != runStyle) {
                if (runStyle != 0) {
                    runs.push_back(StyleRun{runStart, pos, runStyle});
                }
                runStart = pos;
                runStyle = style;
            }
        }
        if (runStyle != 0) {
            runs.push_back(StyleRun{runStart, length, runStyle});
        }
    }
    styles_.SetRuns(runs);
    const wxCharBuffer buffer = GetTextRangeRaw(0, (int)length);
    styles_.RemoveNewlineStylesInRange(
        0, std::string(buffer.data(), buffer.length()));
    EnsureSlots();
    RecycleUnusedStyles();
    // 撤销/重做会按 style byte 重建样式区间；仍存在的链接需要重新标记热点。
    RefreshLinkHotspots();
}

void NoteEditor::OnModified(wxStyledTextEvent& event) {
    event.Skip();
    if (loading_) return;

    const int type = event.GetModificationType();
    const int pos = event.GetPosition();
    const int length = event.GetLength();
    if (pos < 0 || length <= 0) return;

    if (type & wxSTC_MOD_BEFOREDELETE) {
        const wxCharBuffer buffer = GetTextRangeRaw(pos, pos + length);
        pendingDeleteChars_ =
            CodepointCount(std::string(buffer.data(), buffer.length()));
        return;
    }

    if (type & wxSTC_MOD_DELETETEXT) {
        styles_.OnDelete(pos, length);
        const bool linksChanged = styles_.Links().OnDelete(pos, length);
        if (linksChanged) RefreshLinkHotspots();
        RecycleUnusedStyles();
        if (charCount_ >= (long)pendingDeleteChars_) {
            charCount_ -= (long)pendingDeleteChars_;
        }
        pendingDeleteChars_ = 0;
        if (LineLength(GetCurrentLine()) == 0) {
            // 删空一行后，后续输入回到正文，避免继承上一行标题/颜色。
            hasPending_ = true;
            pendingStyleId_ = 0;
        }
        UpdateLineNumberMargin();
        dirty_ = true;
        NotifyChange();
    }

    if (type & wxSTC_MOD_INSERTTEXT) {
        const wxCharBuffer buffer = GetTextRangeRaw(pos, pos + length);
        const std::string inserted(buffer.data(), buffer.length());
        const uint32_t styleId = StyleIdForInsertion(pos);
        const bool multiline = inserted.find('\n') != std::string::npos;
        // 在链接内部或末尾继续输入时延长链接；多行粘贴和默认样式不延长，
        // 否则会把整段粘贴内容都变成链接。
        const bool linksChanged = styles_.Links().OnInsert(
            pos, length, styleId != 0 && !multiline, !multiline);
        styles_.OnInsert(pos, length, styleId);
        EnsureSlots();
        ApplyRun(pos, pos + length, styleId);
        if (linksChanged) RefreshLinkHotspots();
        charCount_ += (long)CodepointCount(inserted);

        if (multiline) {
            // 换行符本身不继承上一行的大字号/标题样式，否则即使新行是正文，
            // 这个换行符仍会把全局行高撑大。批量从样式区间中移除换行，
            // 避免多行粘贴时逐个换行做区间插入。
            styles_.RemoveNewlineStylesInRange(pos, inserted);
            ApplyRange(pos, pos + length);

            // 标题/大字号不应自动延续到下一行；回车后新行回到默认样式。
            // 强制把后续输入设为默认样式，避免在空行行首继承上一行标题。
            hasPending_ = true;
            pendingStyleId_ = 0;
            const size_t lastNewline = inserted.rfind('\n');
            const long newLineStart = pos + (long)lastNewline + 1;
            const int line = LineFromPosition((int)newLineStart);
            const long lineEnd = GetLineEndPosition(line);
            if (lineEnd > newLineStart) {
                styles_.SetRange(newLineStart, lineEnd, 0);
                ApplyRun(newLineStart, lineEnd, 0);
            }
            RecycleUnusedStyles();
        }

        UpdateLineNumberMargin();
        dirty_ = true;
        NotifyChange();
    }

    if (type & (wxSTC_PERFORMED_UNDO | wxSTC_PERFORMED_REDO)) {
        RebuildStylesFromEditor();
    }
}

void NoteEditor::OnKeyDown(wxKeyEvent& event) {
    const int key = event.GetKeyCode();
    if (event.ControlDown() && !event.AltDown()) {
        if (key == '0') {
            ApplyCommand(FormatCommand::Body);
            return;
        }
        if (key == 'B') {
            ApplyCommand(FormatCommand::Bold);
            return;
        }
        if (key == 'I') {
            ApplyCommand(FormatCommand::Italic);
            return;
        }
        if (key == 'U') {
            ApplyCommand(FormatCommand::Underline);
            return;
        }
    }
    const bool navigation =
        key == WXK_UP || key == WXK_DOWN || key == WXK_LEFT ||
        key == WXK_RIGHT || key == WXK_HOME || key == WXK_END ||
        key == WXK_PAGEUP || key == WXK_PAGEDOWN || key == WXK_ESCAPE;
    if (navigation) hasPending_ = false;
    event.Skip();
}

void NoteEditor::OnLeftDown(wxMouseEvent& event) {
    hasPending_ = false;
    event.Skip();
}

void NoteEditor::OnLeftUp(wxMouseEvent& event) {
    event.Skip();
    if (!formatPainterArmed_ || !HasSelection()) return;
    if (ApplyFormatPainter() == FormatPainterResult::Applied &&
        painterAppliedHandler_) {
        painterAppliedHandler_();
    }
}

void NoteEditor::OnHotspotClick(wxStyledTextEvent& event) {
    const std::string url = styles_.Links().UrlAt(event.GetPosition());
    if (!url.empty()) {
        wxLaunchDefaultBrowser(wxString::FromUTF8(url));
    }
    event.Skip();
}

wxColour NoteEditor::ColourOf(const std::string& value,
                              const wxColour& fallback) const {
    return ParseColour(value, fallback);
}

std::string NoteEditor::HexOf(const wxColour& colour) {
    return wxString::Format("#%02X%02X%02X", colour.Red(), colour.Green(),
                            colour.Blue())
        .ToStdString();
}

size_t NoteEditor::CodepointCount(const std::string& text) {
    size_t count = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        const unsigned char ch = (unsigned char)text[pos];
        size_t step = 1;
        if ((ch & 0xE0) == 0xC0) step = 2;
        else if ((ch & 0xF0) == 0xE0) step = 3;
        else if ((ch & 0xF8) == 0xF0) step = 4;
        pos += std::min(step, text.size() - pos);
        ++count;
    }
    return count;
}

bool RunNoteEditorBench(const std::wstring& inputPath,
                        const std::wstring& reportPath) {
    std::ofstream report(std::filesystem::path(reportPath),
                         std::ios::binary | std::ios::trunc);
    auto write = [&](const std::string& line) {
        report.write(line.data(), (std::streamsize)line.size());
        report.write("\n", 1);
        report.flush();
    };

    std::string text;
    std::wstring error;
    wxStopWatch readWatch;
    if (!NoteStorage::ReadTextFile(inputPath, text, error)) {
        write("FAIL read");
        return false;
    }
    const long readMs = readWatch.Time();

    wxFrame frame(nullptr, wxID_ANY, L"PenDown editor bench");
    frame.Hide();
    NoteEditor editor(&frame);

    StyleModel styles = editor.Styles();
    styles.ClearRuns();
    const char* colours[] = {"#E45649", "#2563EB", "#50A14F", "#A626A4",
                             "#986801", "#0F9D8A", "#D9468F", "#6B21A8"};
    std::vector<uint32_t> ids;
    for (int i = 0; i < 8; ++i) {
        TextStyle style;
        style.foreground = colours[i];
        style.bold = (i % 3) == 0;
        style.italic = (i % 4) == 0;
        style.sizeHundredths = (11 + i % 4) * 100;
        ids.push_back(styles.Intern(style));
    }
    // 每 1 KB 放一段 100 字节样式，模拟大量但有限的样式区间。
    for (long pos = 0; pos + 100 < (long)text.size(); pos += 1000) {
        styles.SetRange(pos, pos + 100, ids[(size_t)(pos / 1000) % ids.size()]);
    }

    wxStopWatch loadWatch;
    editor.SetDocument(text, styles);
    const long loadMs = loadWatch.Time();

    wxStopWatch getWatch;
    const std::string copied = editor.GetTextUtf8();
    const long getMs = getWatch.Time();

    const long middle = (long)text.size() / 2;
    wxStopWatch insertWatch;
    editor.InsertText(middle, L"X");
    const long insertMs = insertWatch.Time();
    if (editor.GetTextLength() > middle) editor.DeleteRange(middle, 1);

    wxStopWatch beginWatch;
    for (int i = 0; i < 10; ++i) {
        editor.InsertText(0, L"x");
        editor.DeleteRange(0, 1);
    }
    const long beginMs = beginWatch.Time() / 10;

    wxStopWatch newlineWatch;
    for (int i = 0; i < 5; ++i) {
        editor.InsertText((int)middle, L"\n");
        editor.DeleteRange((int)middle, 1);
    }
    const long newlineMs = newlineWatch.Time() / 5;

    editor.SetSelection(0, 1000);
    wxStopWatch styleWatch;
    editor.ApplyCommand(FormatCommand::Bold);
    const long styleMs = styleWatch.Time();

    // 恢复原始正文，避免压力测试改动影响保存结果。
    editor.SetDocument(text, styles);

    const std::wstring tempPath = reportPath + L".bench.txt";
    wxStopWatch saveWatch;
    const bool saved = NoteStorage::Save(tempPath, copied, styles, error);
    const long saveMs = saveWatch.Time();
    std::error_code ignored;
    std::filesystem::remove(std::filesystem::path(tempPath), ignored);
    std::filesystem::remove(std::filesystem::path(tempPath + L".style.json"),
                            ignored);

    std::ostringstream out;
    out << (saved ? "OK" : "FAIL") << " bytes=" << text.size()
        << " runs=" << styles.Runs().size() << " read_ms=" << readMs
        << " load_ms=" << loadMs << " get_ms=" << getMs
        << " insert_ms=" << insertMs << " begin_ms=" << beginMs
        << " newline_ms=" << newlineMs << " style_ms=" << styleMs
        << " save_ms=" << saveMs << " copied_bytes=" << copied.size()
        << " lines=" << editor.GetLineCount()
        << " margin_px=" << editor.GetMarginWidth(0);
    write(out.str());
    return saved;
}

bool RunNoteEditorSelfTest(wxString& report) {
    std::vector<wxString> failures;
    int checks = 0;
    auto check = [&](bool condition, const wxString& name) {
        ++checks;
        if (!condition) failures.push_back(name);
    };

    wxFrame frame(nullptr, wxID_ANY, L"PenDown editor selftest");
    frame.Hide();
    NoteEditor editor(&frame);

    const std::string text = "Hello 中文 😀\nSecond line";
    StyleModel styles = editor.Styles();
    styles.ClearRuns();
    TextStyle accent;
    accent.bold = true;
    accent.foreground = "#E45649";
    const uint32_t accentId = styles.Intern(accent);
    styles.SetRange(0, 5, accentId);
    styles.SetRange(6, 12, accentId);  // 中文
    styles.SetRange(13, 17, accentId);  // Emoji
    editor.SetDocument(text, styles);

    check(editor.GetTextUtf8() == text, L"editor text roundtrip");
    check(editor.CharCount() == 22, L"editor char count");
    check(editor.Styles().StyleAt(0).bold, L"loaded style");
    check(editor.Styles().StyleAt(6).foreground == "#E45649",
          L"loaded utf8 offset");
    check(editor.Styles().StyleAt(13).foreground == "#E45649",
          L"loaded emoji offset");
    check(editor.StyleGetForeground(1) == wxColour(0xE4, 0x56, 0x49),
          L"style slot foreground");

    editor.SetSelection(0, 5);
    editor.ApplyCommand(FormatCommand::Bold);
    check(editor.Styles().StyleAt(0).bold == false, L"toggle bold off");
    editor.ApplyForeground(wxColour(0x25, 0x63, 0xEB), false);
    check(editor.Styles().StyleAt(0).foreground == "#2563EB",
          L"apply foreground");
    editor.ApplyBackground(wxColour(0xFF, 0xF3, 0xCD), false);
    check(editor.Styles().StyleAt(0).background == "#FFF3CD",
          L"apply background");
    editor.ApplyCommand(FormatCommand::Underline);
    check(editor.Styles().StyleAt(0).underline, L"apply underline");
    editor.ApplyCommand(FormatCommand::Strike);
    check(editor.Styles().StyleAt(0).strike, L"apply strike");
    check(editor.IndicatorValueAt(20, 0) > 0, L"strike indicator");
    editor.ApplyCommand(FormatCommand::ClearFormatting);
    check(!editor.Styles().StyleAt(0).bold &&
              !editor.Styles().StyleAt(0).underline &&
              !editor.Styles().StyleAt(0).strike,
          L"clear formatting");

    StyleModel plain = editor.Styles();
    plain.ClearRuns();
    editor.SetDocument("abc\ndef", plain);
    editor.SetSelection(0, 7);
    const int heightBefore = editor.TextHeight(0);
    editor.ApplyCommand(FormatCommand::FontLarger);
    const int largeSize = editor.Styles().StyleAt(0).sizeHundredths;
    const int heightLarge = editor.TextHeight(0);
    editor.ApplyCommand(FormatCommand::FontSmaller);
    const int backSize = editor.Styles().StyleAt(0).sizeHundredths;
    const int heightBack = editor.TextHeight(0);
    check(largeSize > 1100 && backSize == 1100, L"font size shrinks");
    std::string styleTrace;
    for (int i = 0; i < 6; ++i) {
        styleTrace += std::to_string(editor.GetStyleAt(i));
    }
    check(heightLarge > heightBefore && heightBack == heightBefore,
          wxString::Format(
              L"line height shrinks before=%d large=%d back=%d style=%d size0=%d trace=%s",
              heightBefore, heightLarge, heightBack, editor.GetStyleAt(0),
              editor.StyleGetSizeFractional(0),
              wxString::FromUTF8(styleTrace)));

    editor.SetDocument(text, styles);
    editor.SetInsertionPoint(0);
    editor.SetSelection(0, 0);
    check(editor.FindNext(L"Second", false, false), L"find next");
    check(editor.GetSelectedText() == L"Second", L"find selection");
    check(editor.ReplaceAll(L"line", L"row", false, false) == 1,
          L"replace all");
    check(editor.GetTextUtf8() == "Hello 中文 😀\nSecond row",
          L"replace text");

    StyleModel painter = editor.Styles();
    painter.ClearRuns();
    TextStyle painted;
    painted.foreground = "#A626A4";
    painted.bold = true;
    const uint32_t paintedId = painter.Intern(painted);
    painter.SetRange(0, 1, paintedId);
    editor.SetDocument("abcdef", painter);
    editor.SetSelection(0, 1);
    const auto firstPainter = editor.ApplyFormatPainter();
    editor.SetSelection(1, 2);
    const auto secondPainter = editor.ApplyFormatPainter();
    check(firstPainter == NoteEditor::FormatPainterResult::Copied &&
              secondPainter == NoteEditor::FormatPainterResult::Applied &&
              editor.Styles().StyleAt(1).bold &&
              editor.Styles().StyleAt(1).foreground == "#A626A4",
          L"format painter");

    StyleModel linkStyles;
    linkStyles.SetDefaultStyle(editor.Styles().DefaultStyle());
    TextStyle linkStyle;
    linkStyle.foreground = "#2563EB";
    linkStyle.underline = true;
    const uint32_t linkId = linkStyles.Intern(linkStyle);
    linkStyles.SetRange(0, 3, linkId);
    linkStyles.SetRange(4, 7, linkId);
    linkStyles.Links().SetRange(0, 3, "https://example.com");
    linkStyles.Links().SetRange(4, 7, "https://second.example");
    editor.SetDocument("abc def", linkStyles);
    check(editor.Styles().Links().UrlAt(0) == "https://example.com" &&
              editor.Styles().Links().UrlAt(4) == "https://second.example",
          L"hyperlink urls");
    // 两个不同 URL 共用同一个样式槽，这正是拆分链接的目的。
    check(editor.Styles().StyleCount() == 2, L"links share one style slot");
    check(editor.StyleGetHotSpot(
              (int)NoteEditor::ScintillaSlotForStyle(linkId)),
          L"link hotspot");
    check(!editor.StyleGetHotSpot(0), L"default slot not hotspot");

    // 在链接内部和末尾输入会延长链接。
    editor.SetSelection(2, 2);
    editor.InsertText(2, L"X");
    check(editor.Styles().Links().UrlAt(2) == "https://example.com",
          L"typing inside link extends it");
    editor.SetSelection(8, 8);
    editor.InsertText(8, L"Y");
    check(editor.Styles().Links().UrlAt(8) == "https://second.example",
          L"typing at link end extends it");

    // 在链接中间粘贴多行内容：链接在插入点截断，不会留下看不见的尾部链接。
    StyleModel splitModel;
    splitModel.SetDefaultStyle(editor.Styles().DefaultStyle());
    const uint32_t splitLinkId = splitModel.Intern(linkStyle);
    splitModel.SetRange(0, 3, splitLinkId);
    splitModel.Links().SetRange(0, 3, "https://example.com");
    editor.SetDocument("abc", splitModel);
    editor.InsertText(1, L"X\nY");
    check(editor.Styles().Links().UrlAt(0) == "https://example.com" &&
              editor.Styles().Links().UrlAt(1).empty() &&
              editor.Styles().Links().UrlAt(4).empty() &&
              editor.Styles().Links().Count() == 1,
          L"multiline insert truncates link");

    // 删除链接中间的文字：前后两段合并，链接和 URL 都保留。
    StyleModel deleteLink;
    deleteLink.SetDefaultStyle(editor.Styles().DefaultStyle());
    const uint32_t deleteLinkId = deleteLink.Intern(linkStyle);
    deleteLink.SetRange(0, 4, deleteLinkId);
    deleteLink.Links().SetRange(0, 4, "https://example.com");
    editor.SetDocument("abcd", deleteLink);
    editor.DeleteRange(1, 2);
    check(editor.GetTextUtf8() == "ad" &&
              editor.Styles().Links().UrlAt(0) == "https://example.com" &&
              editor.Styles().Links().UrlAt(1) == "https://example.com" &&
              editor.Styles().Links().UrlAt(2).empty(),
          L"delete inside link keeps link");

    // 删除整段链接：URL 随正文一起移除。
    StyleModel deleteWholeLink;
    deleteWholeLink.SetDefaultStyle(editor.Styles().DefaultStyle());
    const uint32_t deleteWholeId = deleteWholeLink.Intern(linkStyle);
    deleteWholeLink.SetRange(0, 3, deleteWholeId);
    deleteWholeLink.Links().SetRange(0, 3, "https://example.com");
    editor.SetDocument("abc", deleteWholeLink);
    editor.SetSelection(0, 3);
    editor.DeleteRange(0, 3);
    check(editor.Styles().Links().Empty(), L"delete removes link");

    // 格式刷只复制可见样式，不会把 URL 刷到目标文字上。
    StyleModel painterLink;
    painterLink.SetDefaultStyle(editor.Styles().DefaultStyle());
    const uint32_t painterLinkId = painterLink.Intern(linkStyle);
    painterLink.SetRange(0, 3, painterLinkId);
    painterLink.Links().SetRange(0, 3, "https://example.com");
    editor.SetDocument("abcdef", painterLink);
    editor.SetSelection(0, 1);
    editor.ApplyFormatPainter();
    editor.SetSelection(4, 5);
    editor.ApplyFormatPainter();
    check(editor.Styles().Links().UrlAt(4).empty() &&
              editor.Styles().StyleAt(4).underline,
          L"format painter does not copy link");

    // 格式刷把普通格式刷到链接文字上：目标 URL 一起清除，避免隐形链接。
    StyleModel painterOverLink;
    painterOverLink.SetDefaultStyle(editor.Styles().DefaultStyle());
    TextStyle linkAppearance;
    linkAppearance.foreground = "#2563EB";
    linkAppearance.underline = true;
    const uint32_t linkAppearanceId = painterOverLink.Intern(linkAppearance);
    painterOverLink.SetRange(0, 1, linkAppearanceId);
    painterOverLink.SetRange(2, 6, linkAppearanceId);
    painterOverLink.Links().SetRange(2, 6, "https://target.example");
    editor.SetDocument("a bcde", painterOverLink);
    editor.SetSelection(0, 1);
    editor.ApplyFormatPainter();
    editor.SetSelection(2, 6);
    editor.ApplyFormatPainter();
    check(editor.Styles().Links().UrlAt(2).empty(),
          L"format painter clears target link");

    // 键盘快捷键取消格式刷时，要通过回调通知窗口更新标题栏按钮。
    bool painterDisarmed = false;
    editor.SetPainterArmedHandler([&](bool armed) {
        if (!armed) painterDisarmed = true;
    });
    editor.SetSelection(0, 1);
    editor.ApplyFormatPainter();
    editor.ApplyCommand(FormatCommand::Bold);
    check(painterDisarmed, L"painter armed callback");

    // 走真实命令路径：应用链接，再用清除格式移除。
    StyleModel hyperlinkModel;
    hyperlinkModel.SetDefaultStyle(editor.Styles().DefaultStyle());
    editor.SetDocument("alpha beta", hyperlinkModel);
    editor.ApplyHyperlink(0, 5, "https://a.example");
    editor.ApplyHyperlink(6, 10, "https://b.example");
    check(editor.Styles().Links().UrlAt(0) == "https://a.example" &&
              editor.Styles().Links().UrlAt(6) == "https://b.example" &&
              editor.Styles().StyleCount() == 2 &&
              editor.StyleGetHotSpot(1),
          L"apply hyperlink command");
    editor.SetSelection(0, 5);
    editor.ApplyCommand(FormatCommand::ClearFormatting);
    check(editor.Styles().Links().UrlAt(0).empty() &&
              editor.Styles().Links().UrlAt(6) == "https://b.example",
          L"clear formatting removes link");

    // 给链接文字加粗后会生成新的样式槽，新槽也必须是热点，否则链接失效。
    StyleModel styleLinkModel;
    styleLinkModel.SetDefaultStyle(editor.Styles().DefaultStyle());
    const uint32_t styleLinkId = styleLinkModel.Intern(linkStyle);
    styleLinkModel.SetRange(0, 3, styleLinkId);
    styleLinkModel.Links().SetRange(0, 3, "https://example.com");
    editor.SetDocument("abc", styleLinkModel);
    editor.SetSelection(0, 3);
    editor.ApplyCommand(FormatCommand::Bold);
    const int boldLinkStyle = editor.GetStyleAt(0);
    check(boldLinkStyle > 0 && editor.StyleGetHotSpot(boldLinkStyle) &&
              editor.Styles().Links().UrlAt(0) == "https://example.com",
          L"bold link stays clickable");

    // 只加粗链接中间一部分：链接范围内每个样式槽都必须是热点。
    StyleModel partialLink;
    partialLink.SetDefaultStyle(editor.Styles().DefaultStyle());
    const uint32_t partialLinkId = partialLink.Intern(linkStyle);
    partialLink.SetRange(0, 4, partialLinkId);
    partialLink.Links().SetRange(0, 4, "https://example.com");
    editor.SetDocument("abcd", partialLink);
    editor.SetSelection(1, 3);
    editor.ApplyCommand(FormatCommand::Bold);
    const int linkStartSlot = editor.GetStyleAt(0);
    const int linkMiddleSlot = editor.GetStyleAt(1);
    check(linkStartSlot != linkMiddleSlot &&
              editor.StyleGetHotSpot(linkStartSlot) &&
              editor.StyleGetHotSpot(linkMiddleSlot) &&
              editor.Styles().Links().UrlAt(2) == "https://example.com",
          L"partial link styles are hotspots");

    // 多个不同 URL 共用同一个可见样式：样式槽数量不随链接数量增长。
    StyleModel manyLinks;
    manyLinks.SetDefaultStyle(editor.Styles().DefaultStyle());
    const uint32_t manyLinkId = manyLinks.Intern(linkStyle);
    std::string manyLinkText;
    for (int i = 0; i < 30; ++i) {
        manyLinkText.push_back('a');
        manyLinks.SetRange(i, i + 1, manyLinkId);
        manyLinks.Links().SetRange(i, i + 1,
                                   "https://example.com/" + std::to_string(i));
    }
    editor.SetDocument(manyLinkText, manyLinks);
    check(editor.Styles().StyleCount() == 2 &&
              editor.Styles().Links().Count() == 30 &&
              editor.Styles().Links().UrlAt(29) == "https://example.com/29",
          L"many urls share one style slot");

    StyleModel pendingModel = editor.Styles();
    pendingModel.ClearRuns();
    editor.SetDocument("abcdef", pendingModel);
    editor.SetSelection(0, 0);
    editor.ApplyCommand(FormatCommand::FontLarger);
    editor.SetSelection(0, 3);
    editor.ApplyCommand(FormatCommand::ClearFormatting);
    editor.InsertText(3, L"X");
    check(editor.Styles().StyleAt(3).sizeHundredths == 1100,
          L"clear formatting clears pending style");

    StyleModel shifted = editor.Styles();
    shifted.ClearRuns();
    TextStyle bold;
    bold.bold = true;
    const uint32_t boldId = shifted.Intern(bold);
    shifted.SetRange(0, 3, boldId);
    editor.SetDocument("abcdef", shifted);
    editor.InsertText(1, L"X");
    check(editor.GetTextUtf8() == "aXbcdef", L"insert text");
    check(editor.Styles().StyleAt(0).bold &&
              editor.Styles().StyleAt(3).bold &&
              !editor.Styles().StyleAt(4).bold,
          L"style shifts on insert");
    editor.DeleteRange(0, 2);
    check(editor.GetTextUtf8() == "bcdef", L"delete text");
    check(editor.Styles().StyleAt(0).bold &&
              !editor.Styles().StyleAt(3).bold,
          L"style shifts on delete");

    StyleModel undoStyles = editor.Styles();
    undoStyles.ClearRuns();
    const uint32_t undoId = undoStyles.Intern(bold);
    undoStyles.SetRange(0, 3, undoId);
    editor.SetDocument("abcdef", undoStyles);
    editor.InsertText(0, L"X");
    editor.Undo();
    check(editor.GetTextUtf8() == "abcdef" &&
              editor.Styles().StyleAt(0).bold &&
              !editor.Styles().StyleAt(3).bold,
          L"undo restores styles");

    StyleModel many;
    many.SetDefaultStyle(editor.Styles().DefaultStyle());
    uint32_t lastId = 0;
    std::string manyText;
    for (int i = 0; i < 200; ++i) {
        TextStyle style;
        style.foreground = wxString::Format("#%02X%02X%02X", i, (i * 3) % 255,
                                            (i * 7) % 255)
                               .ToStdString();
        lastId = many.Intern(style);
        manyText.push_back('a');
        many.SetRange(i, i + 1, lastId);
    }
    editor.SetDocument(manyText, many);
    const int expectedSlot =
        (int)NoteEditor::ScintillaSlotForStyle(lastId);
    check(editor.GetStyleAt(199) == expectedSlot,
          wxString::Format(L"style slot above 31 got=%d want=%d count=%d",
                           editor.GetStyleAt(199), expectedSlot,
                           (int)editor.Styles().StyleCount()));

    StyleModel heading = editor.Styles();
    heading.ClearRuns();
    TextStyle headingStyle;
    headingStyle.bold = true;
    headingStyle.sizeHundredths = 2800;
    const uint32_t headingId = heading.Intern(headingStyle);
    heading.SetRange(0, 7, headingId);
    editor.SetDocument("Heading", heading);
    editor.InsertText(7, L"\nBody");
    const bool headingBefore = editor.Styles().StyleAt(0).bold;
    const bool headingAfter = editor.Styles().StyleAt(8).bold;
    check(headingBefore && !headingAfter,
          wxString::Format(L"new line leaves heading h0=%d h8=%d text=[%s]",
                           headingBefore, headingAfter,
                           wxString::FromUTF8(editor.GetTextUtf8())));
    check(editor.GetStyleAt(7) == 0, L"newline style reset");

    // 行号边距：类型是 Scintilla 数字边距，行数进入两位数后自动加宽。
    StyleModel lineModel;
    lineModel.SetDefaultStyle(editor.Styles().DefaultStyle());
    editor.SetDocument("one", lineModel);
    const int oneDigitWidth = editor.GetMarginWidth(0);
    std::string tenLines;
    for (int i = 0; i < 10; ++i) tenLines += "x\n";
    editor.SetDocument(tenLines, lineModel);
    check(editor.GetMarginType(0) == wxSTC_MARGIN_NUMBER &&
              editor.GetMarginCursor(0) == wxSTC_CURSORARROW &&
              editor.GetMarginType(1) == wxSTC_MARGIN_COLOUR &&
              editor.GetMarginWidth(1) > 0 &&
              editor.GetLineCount() >= 10 &&
              editor.GetMarginWidth(0) > oneDigitWidth,
          L"line number margin grows");

    // 40 种样式时，应用层 id 32/33 必须跳过 Scintilla 的预定义槽；
    // 行号使用的 33 号槽不能被正文样式覆盖。
    StyleModel reserved;
    reserved.SetDefaultStyle(editor.Styles().DefaultStyle());
    std::string reservedText;
    for (int i = 0; i < 40; ++i) {
        TextStyle style;
        style.foreground =
            wxString::Format("#%02X0000", (i * 5) % 255).ToStdString();
        const uint32_t id = reserved.Intern(style);
        reservedText.push_back('a');
        reserved.SetRange(i, i + 1, id);
    }
    editor.SetDocument(reservedText, reserved);
    check(editor.GetStyleAt(31) ==
                  (int)NoteEditor::ScintillaSlotForStyle(32) &&
              editor.GetStyleAt(32) ==
                  (int)NoteEditor::ScintillaSlotForStyle(33) &&
              editor.StyleGetForeground(wxSTC_STYLE_LINENUMBER) ==
                  wxColour(166, 158, 148),
          wxString::Format(
              L"reserved scintilla styles slot32=%d slot33=%d lineFg=%s",
              editor.GetStyleAt(31), editor.GetStyleAt(32),
              editor.StyleGetForeground(wxSTC_STYLE_LINENUMBER)
                  .GetAsString(wxC2S_HTML_SYNTAX)));

    if (failures.empty()) {
        report = wxString::Format(L"OK checks=%d", checks);
        return true;
    }
    report = wxString::Format(L"FAIL %d/%d: ", (int)failures.size(), checks);
    for (const wxString& failure : failures) {
        report += failure + L"; ";
    }
    return false;
}


