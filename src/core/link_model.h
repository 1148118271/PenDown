#pragma once

#include <string>
#include <vector>

// 一段超链接文字。
//
// 坐标与样式区间一致，使用 UTF-8 字节偏移。URL 单独保存在这里，不再进入
// TextStyle；因此不同链接不会再各占一个 Scintilla style 槽。
struct LinkRun {
    long start = 0;  // 包含
    long end = 0;    // 不包含
    std::string url;
};

// 超链接区间模型。
//
// 链接和样式是两套独立的区间：样式决定“看起来像链接”，LinkModel 决定
// “点击时打开哪个地址”。这样同一篇便签里可以有很多不同 URL，而只占用
// 一个样式组合。
class LinkModel {
public:
    void Clear();
    bool Empty() const { return links_.empty(); }
    size_t Count() const { return links_.size(); }
    const std::vector<LinkRun>& Runs() const { return links_; }

    // 返回覆盖 pos 的链接地址；没有链接时返回空字符串。
    std::string UrlAt(long pos) const;

    // 把 [start, end) 设为同一个链接，替换范围内原有链接。
    bool SetRange(long start, long end, const std::string& url);
    // 清除范围内的链接。
    bool ClearRange(long start, long end);

    // 正文插入/删除后同步链接区间。
    // extend 为 true 时，在链接内部或末尾输入会继续延长链接。
    // keepTail 为 false 表示插入的是多行内容：链接在插入点截断，避免留下
    // 看不见但能点的尾部链接。
    // 返回 true 表示链接覆盖范围发生变化，需要刷新热点样式。
    bool OnInsert(long pos, long length, bool extend, bool keepTail);
    bool OnDelete(long pos, long length);

    // 加载或整篇替换：排序、去掉越界项并合并相邻的同地址链接。
    void SetLinks(std::vector<LinkRun> links, long textLength);

private:
    // 用户主动清除链接（不移动右侧区间）。
    bool RemoveRange(long start, long end);
    void Normalize();

    std::vector<LinkRun> links_;  // 按 start 排序
};
