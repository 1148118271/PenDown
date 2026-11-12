#pragma once

#include <wx/colour.h>

// 一套温暖的浅色主题：底、面板、输入框统一用同一个主题底色，
// 只通过悬浮 / 选中两种状态色区分层次，避免到处出现不同的填充色。
namespace Theme {

inline wxColour Background() { return wxColour(255, 255, 255); }   // #FFFFFF
inline wxColour Surface() { return wxColour(255, 255, 255); }      // #FFFFFF
inline wxColour Border() { return wxColour(216, 208, 199); }       // #D8D0C7
inline wxColour BorderStrong() { return wxColour(199, 189, 178); } // #C7BDB2
inline wxColour Text() { return wxColour(43, 39, 35); }            // #2B2723
inline wxColour TextMuted() { return wxColour(110, 103, 95); }     // #6E675F
inline wxColour TextFaint() { return wxColour(166, 158, 148); }    // #A69E94
inline wxColour TextSubtle() { return wxColour(123, 116, 107); }   // #7B746B
inline wxColour Accent() { return wxColour(37, 99, 235); }         // 主操作蓝
inline wxColour AccentSoft() { return wxColour(238, 235, 232); }   // #EEEBE8 选中
inline wxColour AccentHover() { return wxColour(241, 240, 238); }  // #F1F0EE 悬浮
inline wxColour Danger() { return wxColour(190, 60, 60); }
inline wxColour DangerSoft() { return wxColour(246, 228, 224); }
inline wxColour Hover() { return wxColour(241, 240, 238); }        // #F1F0EE
inline wxColour Pressed() { return wxColour(238, 235, 232); }      // #EEEBE8
inline wxColour Disabled() { return wxColour(185, 178, 169); }

}  // namespace Theme
