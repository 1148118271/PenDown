#pragma once

#include <wx/colour.h>
#include <wx/stc/stc.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/style_model.h"
#include "ui/format_command.h"

// wxStyledTextCtrl 的便签编辑器封装。
//
// 这里负责：
// - 把正文写入 Scintilla；
// - 把 StyleModel 的样式区间映射到 Scintilla 的 style 槽；
// - 监听文本插入/删除，增量维护样式区间；
// - 把工具栏命令应用到选区或后续输入。
class NoteEditor : public wxStyledTextCtrl {
public:
    using ChangeHandler = std::function<void()>;

    NoteEditor(wxWindow* parent, wxWindowID id = wxID_ANY);

    // 用正文和样式重建编辑器内容。调用方负责在加载前设置默认样式。
    void SetDocument(const std::string& text, const StyleModel& styles);
    std::string GetTextUtf8() const;

    long CharCount() const { return charCount_; }
    bool HasSelection() const;

    StyleModel& Styles() { return styles_; }
    const StyleModel& Styles() const { return styles_; }
    TextStyle StyleAtCaret() const;
    // 应用层样式 id 与 Scintilla 样式槽的映射：跳过 32–39 预定义样式。
    static uint32_t ScintillaSlotForStyle(uint32_t styleId);
    static uint32_t StyleForScintillaSlot(uint32_t slot);
    // 删除未使用的样式并重映射 styleId，避免 255 个槽位被历史样式占满。
    bool CompactStyles();

    void ApplyCommand(FormatCommand command);
    void ApplyForeground(const wxColour& colour, bool clear);
    void ApplyBackground(const wxColour& colour, bool clear);
    // 把 [start, end) 设置为链接：蓝色下划线样式 + 独立 URL 区间。
    void ApplyHyperlink(long start, long end, const std::string& url);
    enum class FormatPainterResult { Copied, Applied, Nothing };
    FormatPainterResult ApplyFormatPainter();
    // 取消“已复制格式”的等待状态（再次点击标题栏格式刷按钮时）。
    void CancelFormatPainter() { SetPainterArmed(false); }
    bool FormatPainterArmed() const { return formatPainterArmed_; }
    using PainterAppliedHandler = std::function<void()>;
    void SetPainterAppliedHandler(PainterAppliedHandler handler) {
        painterAppliedHandler_ = std::move(handler);
    }
    // 格式刷的“已复制格式”状态变化时通知窗口，保证标题栏按钮同步。
    using PainterArmedHandler = std::function<void(bool armed)>;
    void SetPainterArmedHandler(PainterArmedHandler handler) {
        painterArmedHandler_ = std::move(handler);
    }

    bool FindNext(const wxString& text, bool matchCase, bool wholeWord);
    int ReplaceAll(const wxString& text, const wxString& replacement,
                   bool matchCase, bool wholeWord);
    bool ReplaceCurrent(const wxString& replacement);

    void SetChangeHandler(ChangeHandler handler) {
        changeHandler_ = std::move(handler);
    }

    void MarkSaved();
    bool IsDirty() const { return dirty_; }
    void FocusEditor();

private:
    void ConfigureEditor();
    void ConfigureStyles();
    // 行号样式必须在 StyleClearAll 之后重新设置，否则会被重置成默认颜色。
    void ConfigureLineNumberStyle();
    void ConfigureSlot(uint32_t styleId);
    void ConfigureSlotStyle(uint32_t styleId, const TextStyle& style);
    void RecycleUnusedStyles();
    void EnsureSlots();
    // 行号宽度只在位数变化时重设，避免每次输入都触发重新布局。
    void UpdateLineNumberMargin();

    void ApplyRun(long start, long end, uint32_t styleId);
    void ApplyRange(long start, long end);
    void ApplyStrike(long start, long end);
    void ApplyPatchToRange(long start, long end, const StylePatch& patch,
                           const std::string* linkUrl = nullptr);
    void SetPending(const StylePatch& patch);
    // 链接文字需要 Scintilla 的 hotspot 标记才能 Ctrl+点击；按当前链接反推样式槽。
    void RefreshLinkHotspots();

    TextStyle MakeDefaultStyle() const;
    uint32_t StyleIdForInsertion(long pos) const;

    wxColour ColourOf(const std::string& value, const wxColour& fallback) const;
    static std::string HexOf(const wxColour& colour);
    static size_t CodepointCount(const std::string& text);

    void NotifyChange();
    void SetPainterArmed(bool armed);
    void RebuildStylesFromEditor();
    void OnModified(wxStyledTextEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnHotspotClick(wxStyledTextEvent& event);

    StyleModel styles_;
    std::vector<bool> slotConfigured_;
    wxFont baseFont_;

    long charCount_ = 0;
    size_t pendingDeleteChars_ = 0;
    uint32_t pendingStyleId_ = 0;
    bool hasPending_ = false;
    bool loading_ = false;
    bool applying_ = false;
    bool dirty_ = false;
    bool formatPainterArmed_ = false;
    TextStyle formatPainterStyle_;
    int lineNumberDigits_ = 0;

    ChangeHandler changeHandler_;
    PainterAppliedHandler painterAppliedHandler_;
    PainterArmedHandler painterArmedHandler_;
};

// 开发用：验证 wxStyledTextCtrl 编辑、样式区间、查找替换和 UTF-8 偏移。
bool RunNoteEditorSelfTest(wxString& report);

// 开发用：加载/保存指定文件并输出耗时，用于 1/10/50 MB 压力测试。
bool RunNoteEditorBench(const std::wstring& inputPath,
                        const std::wstring& reportPath);
