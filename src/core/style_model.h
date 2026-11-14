#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/link_model.h"

// 一段字符的样式。字段为空/0 表示使用文档默认样式。
// 颜色统一保存成 #RRGGBB，字号使用 1/100 磅，避免 JSON 浮点误差。
// 链接地址不在这里：它由 LinkModel 单独保存，不占用样式槽。
struct TextStyle {
    std::string fontFamily;
    int sizeHundredths = 0;
    std::string foreground;
    std::string background;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;

    bool operator==(const TextStyle& other) const;
    bool operator!=(const TextStyle& other) const { return !(*this == other); }
};

// 对现有样式做“只改指定字段”的补丁。
struct StylePatch {
    bool clearAll = false;
    bool setFont = false;
    std::string fontFamily;
    bool setSize = false;
    int sizeHundredths = 0;
    bool setForeground = false;
    std::string foreground;
    bool setBackground = false;
    std::string background;
    bool setBold = false;
    bool bold = false;
    bool setItalic = false;
    bool italic = false;
    bool setUnderline = false;
    bool underline = false;
    bool setStrike = false;
    bool strike = false;

    TextStyle Apply(const TextStyle& base) const;
};

struct StyleRun {
    long start = 0;  // UTF-8 字节偏移，包含
    long end = 0;    // UTF-8 字节偏移，不包含
    uint32_t styleId = 0;
};

// 正文样式区间模型。
//
// 坐标全部是 UTF-8 字节偏移，和 Scintilla 的文档位置一致。默认样式 id=0；
// 自定义样式最多 247 个（Scintilla 的 32–39 留给预定义样式）。
// 如果用户在一篇便签里制造出超过 247 种不同组合，新样式会退化成最接近的
// 已有样式，避免 Scintilla 显示错乱；正文本身不受影响。
class StyleModel {
public:
    static constexpr uint32_t kDefaultStyleId = 0;
    // Scintilla 的 32–39 是预定义样式（32 = 默认样式，33 = 行号），
    // 文本样式不能占用；0 默认 + 1–31 + 40–255 共 248 个。
    // 应用层 id 和 Scintilla 槽的映射由 NoteEditor 负责。
    static constexpr size_t kMaxStyles = 248;

    StyleModel();

    void ClearRuns();
    void SetDefaultStyle(const TextStyle& style);
    const TextStyle& DefaultStyle() const { return styles_[0]; }

    uint32_t Intern(const TextStyle& style);
    const TextStyle& GetStyle(uint32_t styleId) const;
    uint32_t StyleIdAt(long pos) const;
    TextStyle StyleAt(long pos) const;

    // 链接地址单独保存，不参与样式组合，因此不会占用文本样式槽。
    LinkModel& Links() { return links_; }
    const LinkModel& Links() const { return links_; }

    // 把 [start, end) 统一替换成 styleId；styleId=0 表示恢复默认。
    void SetRange(long start, long end, uint32_t styleId);
    void ApplyPatch(long start, long end, const StylePatch& patch);

    // 正文变化时同步样式区间。
    void OnInsert(long pos, long length, uint32_t styleId);
    void OnDelete(long pos, long length);

    // 换行符不应携带字符样式；把文本范围内的换行从样式区间中拆掉。
    void RemoveNewlineStyles(const std::string& text);
    void RemoveNewlineStylesInRange(long base, const std::string& text);

    const std::vector<StyleRun>& Runs() const { return runs_; }
    void ForEachRun(long start, long end,
                    const std::function<void(const StyleRun&)>& callback) const;
    const std::vector<TextStyle>& Styles() const { return styles_; }
    size_t StyleCount() const { return styles_.size(); }
    bool Empty() const { return runs_.empty() && links_.Empty(); }

    // 从 JSON 加载时使用。styles 下标 0 必须是默认样式。
    void SetLoadedData(const std::vector<TextStyle>& styles,
                       const std::vector<StyleRun>& runs);
    void SetRuns(const std::vector<StyleRun>& runs);
    // 删除没有被任何区间引用的样式并重映射 styleId，释放样式槽位。
    bool Compact();

private:
    static std::string KeyOf(const TextStyle& style);
    void SplitAt(long pos);
    void MergeAround(size_t index);
    void MergeAll();

    std::vector<StyleRun> runs_;  // 按 start 排序
    std::vector<TextStyle> styles_;
    std::map<std::string, uint32_t> styleKeys_;
    LinkModel links_;
};
