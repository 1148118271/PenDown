#pragma once

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>

// 单实例相关：主窗口上的属性名，以及第二个实例请求显示主窗口的消息名。
// 用 RegisterWindowMessageW 注册后，不同进程拿到的消息 ID 相同。
inline constexpr wchar_t kPenDownMainWindowProperty[] = L"PenDown.MainWindow";
inline constexpr wchar_t kPenDownShowMainMessage[] = L"PenDown.ShowMainWindow";
#endif
