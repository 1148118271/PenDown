#include "core/link_model.h"

#include <algorithm>
#include <utility>

void LinkModel::Clear() {
    links_.clear();
}

void LinkModel::Normalize() {
    if (links_.empty()) return;

    std::sort(links_.begin(), links_.end(),
              [](const LinkRun& a, const LinkRun& b) {
                  if (a.start != b.start) return a.start < b.start;
                  return a.end < b.end;
              });

    std::vector<LinkRun> merged;
    merged.reserve(links_.size());
    for (LinkRun link : links_) {
        if (link.end <= link.start || link.url.empty()) continue;
        if (!merged.empty() && link.start < merged.back().end) {
            // 正常编辑不会产生重叠；遇到异常数据时保留前面的链接。
            if (link.end <= merged.back().end) continue;
            link.start = merged.back().end;
        }
        if (!merged.empty() && merged.back().end == link.start &&
            merged.back().url == link.url) {
            merged.back().end = link.end;
        } else {
            merged.push_back(std::move(link));
        }
    }
    links_ = std::move(merged);
}

void LinkModel::SetLinks(std::vector<LinkRun> links, long textLength) {
    if (textLength >= 0) {
        for (LinkRun& link : links) {
            if (link.start < 0) link.start = 0;
            if (link.end > textLength) link.end = textLength;
        }
        links.erase(std::remove_if(links.begin(), links.end(),
                                   [](const LinkRun& link) {
                                       return link.end <= link.start ||
                                              link.url.empty();
                                   }),
                    links.end());
    }
    links_ = std::move(links);
    Normalize();
}

std::string LinkModel::UrlAt(long pos) const {
    if (links_.empty() || pos < 0) return {};
    auto it = std::upper_bound(
        links_.begin(), links_.end(), pos,
        [](long value, const LinkRun& link) { return value < link.start; });
    if (it == links_.begin()) return {};
    --it;
    if (pos >= it->start && pos < it->end) return it->url;
    return {};
}

bool LinkModel::RemoveRange(long start, long end) {
    if (end <= start || links_.empty()) return false;

    std::vector<LinkRun> result;
    result.reserve(links_.size() + 1);
    bool changed = false;
    for (const LinkRun& link : links_) {
        if (link.end <= start || link.start >= end) {
            result.push_back(link);
            continue;
        }
        changed = true;
        if (link.start < start) {
            result.push_back(LinkRun{link.start, start, link.url});
        }
        if (link.end > end) {
            result.push_back(LinkRun{end, link.end, link.url});
        }
    }
    if (changed) links_ = std::move(result);
    return changed;
}

bool LinkModel::SetRange(long start, long end, const std::string& url) {
    if (url.empty() || end <= start) return false;
    if (start < 0) start = 0;

    RemoveRange(start, end);
    const auto it = std::lower_bound(
        links_.begin(), links_.end(), start,
        [](const LinkRun& link, long value) { return link.start < value; });
    links_.insert(it, LinkRun{start, end, url});
    Normalize();
    return true;
}

bool LinkModel::ClearRange(long start, long end) {
    if (end <= start) return false;
    if (start < 0) start = 0;
    const bool changed = RemoveRange(start, end);
    Normalize();
    return changed;
}

bool LinkModel::OnInsert(long pos, long length, bool extend, bool keepTail) {
    if (length <= 0) return false;
    if (pos < 0) pos = 0;

    size_t containing = links_.size();
    for (size_t i = 0; i < links_.size(); ++i) {
        // 在链接内部或末尾继续输入时延长链接；在开头输入不算。
        if (links_[i].start < pos && pos <= links_[i].end) {
            containing = i;
            break;
        }
    }

    // 插入点之后的链接整体右移。
    for (LinkRun& link : links_) {
        if (link.start >= pos) {
            link.start += length;
            link.end += length;
        }
    }

    bool extended = false;
    if (containing < links_.size()) {
        LinkRun& link = links_[containing];
        if (extend) {
            link.end += length;
            extended = true;
        } else if (pos < link.end) {
            // 在链接中间插入不继承链接的内容：前段保留，后段按需拆出。
            // 多行粘贴时后半行样式会被重置为默认，拆出尾部只会得到看不见的
            // 链接，因此这种情况直接截断。
            if (keepTail) {
                LinkRun tail{pos + length, link.end + length, link.url};
                link.end = pos;
                links_.push_back(std::move(tail));
                Normalize();
            } else {
                link.end = pos;
            }
        }
    }
    return extended;
}

bool LinkModel::OnDelete(long pos, long length) {
    if (length <= 0) return false;
    if (pos < 0) pos = 0;
    const long end = pos + length;

    std::vector<LinkRun> result;
    result.reserve(links_.size() + 1);
    bool changed = false;
    for (const LinkRun& link : links_) {
        if (link.end <= pos || link.start >= end) {
            result.push_back(link);
            continue;
        }
        changed = true;
        if (link.start < pos) {
            result.push_back(LinkRun{link.start, pos, link.url});
        }
        if (link.end > end) {
            // 右侧片段随正文一起左移。
            result.push_back(LinkRun{end - length, link.end - length, link.url});
        }
    }
    if (!changed) return false;

    links_ = std::move(result);
    Normalize();
    return true;
}
