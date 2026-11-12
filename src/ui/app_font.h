#pragma once

#include <wx/font.h>

// 统一使用操作系统默认界面字体，不再随程序分发自定义字体。
// Markdown 代码块使用系统等宽字体。
class AppFont {
public:
    static int BasePointSize();

    static wxFont Make(int pointSize, bool bold = false, bool italic = false,
                       bool underline = false);

    // 系统默认等宽字体，用于 Markdown 代码块。
    static wxFont Mono(int pointSize = 0);

    // 正文保持比系统 UI 稍大一点，阅读更舒适。
    static wxFont Content(int pointSize = 0);

private:
    static void EnsureDefaults();

    static int basePointSize_;
};
