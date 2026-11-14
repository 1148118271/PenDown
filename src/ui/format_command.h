#pragma once

// 样式库和快捷键共用的格式命令。
// 标题（H1–H6）和颜色入口已从样式表移除；颜色仍通过标题栏颜色盘设置。
enum class FormatCommand {
    Body,
    Bold,
    Italic,
    Underline,
    Strike,
    FontSmaller,
    FontLarger,
    ChooseFont,
    ClearFormatting,
    Hyperlink,
};
