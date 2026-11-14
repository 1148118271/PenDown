#include "core/style_model.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace {

int ParseChannel(char high, char low) {
    auto nibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    const int h = nibble(high);
    const int l = nibble(low);
    if (h < 0 || l < 0) return -1;
    return h * 16 + l;
}

bool ParseColour(const std::string& value, int& r, int& g, int& b) {
    if (value.size() != 7 || value[0] != '#') return false;
    r = ParseChannel(value[1], value[2]);
    g = ParseChannel(value[3], value[4]);
    b = ParseChannel(value[5], value[6]);
    return r >= 0 && g >= 0 && b >= 0;
}

int ColourDistance(const std::string& a, const std::string& b) {
    int ar = 0, ag = 0, ab = 0;
    int br = 0, bg = 0, bb = 0;
    const bool av = ParseColour(a, ar, ag, ab);
    const bool bv = ParseColour(b, br, bg, bb);
    if (!av && !bv) return 0;
    if (!av || !bv) return 256 * 3;
    return std::abs(ar - br) + std::abs(ag - bg) + std::abs(ab - bb);
}

int StyleDistance(const TextStyle& a, const TextStyle& b) {
    int distance = 0;
    if (a.fontFamily != b.fontFamily) distance += 600;
    distance += std::abs(a.sizeHundredths - b.sizeHundredths) / 10;
    distance += ColourDistance(a.foreground, b.foreground);
    distance += ColourDistance(a.background, b.background);
    distance += (a.bold != b.bold) ? 200 : 0;
    distance += (a.italic != b.italic) ? 200 : 0;
    distance += (a.underline != b.underline) ? 200 : 0;
    distance += (a.strike != b.strike) ? 200 : 0;
    return distance;
}

}  // namespace

bool TextStyle::operator==(const TextStyle& other) const {
    return fontFamily == other.fontFamily && sizeHundredths == other.sizeHundredths &&
           foreground == other.foreground && background == other.background &&
           bold == other.bold && italic == other.italic &&
           underline == other.underline && strike == other.strike;
}

TextStyle StylePatch::Apply(const TextStyle& base) const {
    TextStyle result = clearAll ? TextStyle() : base;
    if (setFont) result.fontFamily = fontFamily;
    if (setSize) result.sizeHundredths = sizeHundredths;
    if (setForeground) result.foreground = foreground;
    if (setBackground) result.background = background;
    if (setBold) result.bold = bold;
    if (setItalic) result.italic = italic;
    if (setUnderline) result.underline = underline;
    if (setStrike) result.strike = strike;
    return result;
}

StyleModel::StyleModel() {
    styles_.push_back(TextStyle());
    styleKeys_[KeyOf(styles_[0])] = 0;
}

void StyleModel::ClearRuns() {
    runs_.clear();
    links_.Clear();
}

void StyleModel::SetDefaultStyle(const TextStyle& style) {
    styles_[0] = style;
    styleKeys_.clear();
    for (size_t i = 0; i < styles_.size(); ++i) {
        styleKeys_.emplace(KeyOf(styles_[i]), (uint32_t)i);
    }
}

std::string StyleModel::KeyOf(const TextStyle& style) {
    std::string key;
    key.reserve(style.fontFamily.size() + style.foreground.size() +
                style.background.size() + 48);
    key.append(style.fontFamily);
    key.push_back('\x1f');
    key.append(std::to_string(style.sizeHundredths));
    key.push_back('\x1f');
    key.append(style.foreground);
    key.push_back('\x1f');
    key.append(style.background);
    key.push_back('\x1f');
    key.push_back(style.bold ? '1' : '0');
    key.push_back(style.italic ? '1' : '0');
    key.push_back(style.underline ? '1' : '0');
    key.push_back(style.strike ? '1' : '0');
    return key;
}

uint32_t StyleModel::Intern(const TextStyle& style) {
    if (style == styles_[0]) return kDefaultStyleId;
    // 空样式在语义上就是默认样式；不要为“清除格式”生成无意义的 style 槽。
    if (style.fontFamily.empty() && style.sizeHundredths == 0 &&
        style.foreground.empty() && style.background.empty() &&
        !style.bold && !style.italic && !style.underline && !style.strike) {
        return kDefaultStyleId;
    }

    const std::string key = KeyOf(style);
    const auto found = styleKeys_.find(key);
    if (found != styleKeys_.end()) return found->second;

    if (styles_.size() < kMaxStyles) {
        const uint32_t id = (uint32_t)styles_.size();
        styles_.push_back(style);
        styleKeys_.emplace(key, id);
        return id;
    }

    // Scintilla 的 32–39 是预定义样式，正文样式会跳过这一段，因此最多
    // 247 个自定义组合。超出时退化为最接近的已有样式：正文内容不受影响，
    // 但新组合不会进入样式表，保存后的 JSON 也是退化后的样式。
    uint32_t best = 1;
    int bestDistance = std::numeric_limits<int>::max();
    for (uint32_t i = 1; i < styles_.size(); ++i) {
        const int distance = StyleDistance(style, styles_[i]);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

const TextStyle& StyleModel::GetStyle(uint32_t styleId) const {
    if (styleId < styles_.size()) return styles_[styleId];
    return styles_[0];
}

uint32_t StyleModel::StyleIdAt(long pos) const {
    if (pos < 0 || runs_.empty()) return kDefaultStyleId;
    auto it = std::upper_bound(
        runs_.begin(), runs_.end(), pos,
        [](long value, const StyleRun& run) { return value < run.start; });
    if (it == runs_.begin()) return kDefaultStyleId;
    --it;
    if (pos >= it->start && pos < it->end) return it->styleId;
    return kDefaultStyleId;
}

TextStyle StyleModel::StyleAt(long pos) const {
    return GetStyle(StyleIdAt(pos));
}

void StyleModel::SplitAt(long pos) {
    if (pos <= 0 || runs_.empty()) return;
    auto it = std::lower_bound(
        runs_.begin(), runs_.end(), pos,
        [](const StyleRun& run, long value) { return run.start < value; });
    if (it == runs_.end() || it->start > pos) {
        if (it == runs_.begin()) return;
        --it;
    }
    if (pos <= it->start || pos >= it->end) return;

    StyleRun right{pos, it->end, it->styleId};
    it->end = pos;
    runs_.insert(it + 1, right);
}

void StyleModel::MergeAround(size_t index) {
    if (runs_.size() < 2) return;
    size_t start = index > 0 ? index - 1 : 0;
    size_t i = start;
    while (i + 1 < runs_.size()) {
        if (runs_[i].end == runs_[i + 1].start &&
            runs_[i].styleId == runs_[i + 1].styleId) {
            runs_[i].end = runs_[i + 1].end;
            runs_.erase(runs_.begin() + (long)i + 1);
        } else {
            ++i;
        }
        if (i > start + 2) break;
    }
}

void StyleModel::MergeAll() {
    if (runs_.size() < 2) return;
    size_t i = 0;
    while (i + 1 < runs_.size()) {
        if (runs_[i].end == runs_[i + 1].start &&
            runs_[i].styleId == runs_[i + 1].styleId) {
            runs_[i].end = runs_[i + 1].end;
            runs_.erase(runs_.begin() + (long)i + 1);
        } else {
            ++i;
        }
    }
}

void StyleModel::SetRange(long start, long end, uint32_t styleId) {
    if (start < 0) start = 0;
    if (end <= start) return;

    SplitAt(start);
    SplitAt(end);

    auto first = std::lower_bound(
        runs_.begin(), runs_.end(), start,
        [](const StyleRun& run, long value) { return run.start < value; });
    auto last = std::lower_bound(
        runs_.begin(), runs_.end(), end,
        [](const StyleRun& run, long value) { return run.start < value; });
    size_t index = (size_t)(first - runs_.begin());
    if (first != last) first = runs_.erase(first, last);

    if (styleId != kDefaultStyleId && styleId < styles_.size()) {
        runs_.insert(first, StyleRun{start, end, styleId});
    }
    MergeAround(index);
}

void StyleModel::ApplyPatch(long start, long end, const StylePatch& patch) {
    if (start < 0) start = 0;
    if (end <= start) return;

    // 先把 [start, end) 拆成连续的“同样式片段”，包括默认样式空隙。
    std::vector<StyleRun> segments;
    long pos = start;
    while (pos < end) {
        const uint32_t id = StyleIdAt(pos);
        long next = end;

        auto it = std::upper_bound(
            runs_.begin(), runs_.end(), pos,
            [](long value, const StyleRun& run) { return value < run.start; });
        if (it != runs_.begin()) {
            auto prev = it - 1;
            if (pos >= prev->start && pos < prev->end) {
                next = std::min(end, prev->end);
            }
        }
        if (it != runs_.end()) next = std::min(next, it->start);
        if (next <= pos) next = pos + 1;

        segments.push_back(StyleRun{pos, next, id});
        pos = next;
    }

    for (const StyleRun& segment : segments) {
        const TextStyle style = patch.Apply(GetStyle(segment.styleId));
        SetRange(segment.start, segment.end, Intern(style));
    }
}

void StyleModel::OnInsert(long pos, long length, uint32_t styleId) {
    if (length <= 0) return;
    if (pos < 0) pos = 0;

    SplitAt(pos);
    auto first = std::lower_bound(
        runs_.begin(), runs_.end(), pos,
        [](const StyleRun& run, long value) { return run.start < value; });
    const size_t index = (size_t)(first - runs_.begin());
    for (auto it = first; it != runs_.end(); ++it) {
        it->start += length;
        it->end += length;
    }
    if (styleId != kDefaultStyleId && styleId < styles_.size()) {
        runs_.insert(runs_.begin() + (long)index,
                     StyleRun{pos, pos + length, styleId});
    }
    MergeAround(index);
}

void StyleModel::OnDelete(long pos, long length) {
    if (length <= 0) return;
    if (pos < 0) pos = 0;
    const long end = pos + length;

    SplitAt(pos);
    SplitAt(end);

    auto first = std::lower_bound(
        runs_.begin(), runs_.end(), pos,
        [](const StyleRun& run, long value) { return run.start < value; });
    auto last = std::lower_bound(
        runs_.begin(), runs_.end(), end,
        [](const StyleRun& run, long value) { return run.start < value; });
    const size_t index = (size_t)(first - runs_.begin());
    if (first != last) first = runs_.erase(first, last);

    for (auto it = first; it != runs_.end(); ++it) {
        it->start -= length;
        it->end -= length;
    }
    MergeAround(index);
}

void StyleModel::RemoveNewlineStyles(const std::string& text) {
    RemoveNewlineStylesInRange(0, text);
}

void StyleModel::RemoveNewlineStylesInRange(long base, const std::string& text) {
    if (text.empty() || runs_.empty()) return;
    const long end = base + (long)text.size();
    std::vector<StyleRun> result;
    result.reserve(runs_.size());

    for (const StyleRun& run : runs_) {
        if (run.end <= base || run.start >= end) {
            result.push_back(run);
            continue;
        }

        const long scanStart = std::max(run.start, base);
        const long scanEnd = std::min(run.end, end);
        long segmentStart = run.start;

        for (long pos = scanStart; pos < scanEnd; ++pos) {
            if (text[(size_t)(pos - base)] != '\n') continue;
            if (pos > segmentStart) {
                result.push_back(StyleRun{segmentStart, pos, run.styleId});
            }
            segmentStart = pos + 1;
        }
        if (segmentStart < run.end) {
            result.push_back(StyleRun{segmentStart, run.end, run.styleId});
        }
    }

    runs_ = std::move(result);
    MergeAll();
}

void StyleModel::ForEachRun(long start, long end,
                            const std::function<void(const StyleRun&)>& callback) const {
    if (!callback || end <= start || runs_.empty()) return;
    auto it = std::lower_bound(
        runs_.begin(), runs_.end(), start,
        [](const StyleRun& run, long value) { return run.start < value; });
    if (it != runs_.begin()) {
        auto prev = it - 1;
        if (prev->end > start) it = prev;
    }
    while (it != runs_.end() && it->start < end) {
        if (it->end > start) callback(*it);
        ++it;
    }
}

void StyleModel::SetLoadedData(const std::vector<TextStyle>& styles,
                               const std::vector<StyleRun>& runs) {
    links_.Clear();

    // 旧文件把 URL 放在样式表里；拆出链接后会出现完全相同的样式组合。
    // 这里按完整属性去重并重映射 runs，避免旧便签继续一个 URL 占一个槽。
    std::vector<uint32_t> remap(styles.empty() ? 1 : styles.size(), 0);
    styles_.clear();
    styles_.push_back(styles.empty() ? TextStyle() : styles[0]);
    styleKeys_.clear();
    styleKeys_.emplace(KeyOf(styles_[0]), 0u);

    for (size_t i = 1; i < styles.size(); ++i) {
        const std::string key = KeyOf(styles[i]);
        const auto found = styleKeys_.find(key);
        if (found != styleKeys_.end()) {
            remap[i] = found->second;
            continue;
        }
        if (styles_.size() >= kMaxStyles) {
            remap[i] = kDefaultStyleId;
            continue;
        }
        remap[i] = (uint32_t)styles_.size();
        styleKeys_.emplace(key, remap[i]);
        styles_.push_back(styles[i]);
    }

    std::vector<StyleRun> sorted = runs;
    std::sort(sorted.begin(), sorted.end(),
              [](const StyleRun& a, const StyleRun& b) {
                  return a.start < b.start;
              });

    runs_.clear();
    runs_.reserve(sorted.size());
    long lastEnd = 0;
    for (const StyleRun& run : sorted) {
        if (run.end <= run.start || run.start < lastEnd) continue;
        if (run.styleId == kDefaultStyleId ||
            run.styleId >= remap.size()) {
            continue;
        }
        const uint32_t mapped = remap[run.styleId];
        if (mapped == kDefaultStyleId) continue;
        runs_.push_back(StyleRun{run.start, run.end, mapped});
        lastEnd = run.end;
    }
    MergeAll();
}

void StyleModel::SetRuns(const std::vector<StyleRun>& runs) {
    runs_ = runs;
    std::sort(runs_.begin(), runs_.end(),
              [](const StyleRun& a, const StyleRun& b) {
                  return a.start < b.start;
              });
    runs_.erase(std::remove_if(runs_.begin(), runs_.end(),
                               [&](const StyleRun& run) {
                                   return run.end <= run.start ||
                                          run.styleId == kDefaultStyleId ||
                                          run.styleId >= styles_.size();
                               }),
                runs_.end());
    long lastEnd = 0;
    runs_.erase(std::remove_if(runs_.begin(), runs_.end(),
                               [&](const StyleRun& run) {
                                   if (run.start < lastEnd) return true;
                                   lastEnd = run.end;
                                   return false;
                               }),
                runs_.end());
    MergeAll();
}

bool StyleModel::Compact() {
    if (styles_.size() <= 1) return false;

    std::vector<bool> used(styles_.size(), false);
    used[0] = true;
    for (const StyleRun& run : runs_) {
        if (run.styleId < styles_.size()) used[run.styleId] = true;
    }

    size_t usedCount = 0;
    for (bool value : used) {
        if (value) ++usedCount;
    }
    if (usedCount == styles_.size()) return false;

    std::vector<TextStyle> compacted;
    compacted.reserve(usedCount);
    std::vector<uint32_t> remap(styles_.size(), 0);
    for (size_t i = 0; i < styles_.size(); ++i) {
        if (!used[i]) continue;
        remap[i] = (uint32_t)compacted.size();
        compacted.push_back(styles_[i]);
    }

    for (StyleRun& run : runs_) {
        if (run.styleId < remap.size()) run.styleId = remap[run.styleId];
    }
    styles_ = std::move(compacted);
    styleKeys_.clear();
    for (size_t i = 0; i < styles_.size(); ++i) {
        styleKeys_.emplace(KeyOf(styles_[i]), (uint32_t)i);
    }
    MergeAll();
    return true;
}
