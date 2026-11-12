#pragma once

#include <wx/panel.h>

class wxWindow;

// Windows 11 的原生圆角 + 阴影：给无边框窗口补上可缩放边框，并让 DWM 绘制阴影。
// 成功启用 DWM 圆角后，ApplyRoundedWindowRegion 会自动跳过，避免窗口区域裁掉阴影。
void PrepareFramelessWindow(wxWindow* window);

// 无边框顶层窗口的圆角裁切；仅在系统不支持 DWM 圆角时作为回退。
void ApplyRoundedWindowRegion(wxWindow* window, int radiusDip);

#ifdef __WXMSW__
// 处理无边框窗口需要的 WM_NCCALCSIZE / WM_GETMINMAXINFO；已处理时返回 true。
bool HandleFramelessWindowMessage(wxWindow* window, WXUINT message, WXWPARAM wParam,
                                  WXLPARAM lParam, WXLRESULT& result);
#endif

// 顶层窗口的纯色内容面板；边框和圆角由 DWM 阴影提供。
class WindowFramePanel : public wxPanel {
public:
    explicit WindowFramePanel(wxWindow* parent);
    int ContentInset() const { return FromDIP(2); }

private:
    void OnPaint(wxPaintEvent& event);
};
