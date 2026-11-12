#include "ui/window_effects.h"

#include "ui/theme.h"

#include <wx/dcbuffer.h>
#include <wx/toplevel.h>
#include <wx/window.h>

#include <algorithm>
#include <map>

#ifdef __WXMSW__
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {

// 这些属性在旧版 SDK 里没有定义，写死数值以兼容。
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

#ifdef __WXMSW__
constexpr DWORD kDwmColorNone = 0xFFFFFFFE;
constexpr wchar_t kDwmRoundProp[] = L"PenDownDwmRound";
constexpr wchar_t kResizeHookProp[] = L"PenDownResizeHook";

HHOOK g_resizeHook = nullptr;
HWND g_resizeHwnd = nullptr;
int g_resizeHit = 0;
POINT g_resizeStartCursor = {};
RECT g_resizeStartRect = {};
wxSize g_resizeMinSize;
std::map<HWND, wxSize> g_windowMinSizes;

int ResizeBorderForWindow(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static GetDpiForWindowFn getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    UINT dpi = getDpiForWindow ? getDpiForWindow(hwnd) : 96;
    if (dpi == 0) dpi = 96;
    const int border = ::MulDiv(5, (int)dpi, 96);
    return border < 4 ? 4 : border;
}

int ResizeHitTest(HWND hwnd, POINT pt) {
    if (!hwnd || ::IsZoomed(hwnd) || ::IsIconic(hwnd)) return 0;

    RECT rect = {};
    if (!::GetWindowRect(hwnd, &rect)) return 0;
    const int border = ResizeBorderForWindow(hwnd);
    const bool left = pt.x >= rect.left && pt.x < rect.left + border;
    const bool right = pt.x < rect.right && pt.x >= rect.right - border;
    const bool top = pt.y >= rect.top && pt.y < rect.top + border;
    const bool bottom = pt.y < rect.bottom && pt.y >= rect.bottom - border;

    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    return 0;
}

void SetResizeCursor(int hit) {
    LPCTSTR cursor = IDC_ARROW;
    switch (hit) {
        case HTLEFT:
        case HTRIGHT: cursor = IDC_SIZEWE; break;
        case HTTOP:
        case HTBOTTOM: cursor = IDC_SIZENS; break;
        case HTTOPLEFT:
        case HTBOTTOMRIGHT: cursor = IDC_SIZENWSE; break;
        case HTTOPRIGHT:
        case HTBOTTOMLEFT: cursor = IDC_SIZENESW; break;
        default: return;
    }
    ::SetCursor(::LoadCursorW(nullptr, cursor));
}

void ResetResize() {
    if (g_resizeHwnd) ::ReleaseCapture();
    g_resizeHwnd = nullptr;
    g_resizeHit = 0;
    g_resizeMinSize = wxSize();
}

void ApplyResize(POINT cursor) {
    if (!g_resizeHwnd) return;
    const int dx = cursor.x - g_resizeStartCursor.x;
    const int dy = cursor.y - g_resizeStartCursor.y;
    RECT rect = g_resizeStartRect;

    if (g_resizeHit == HTLEFT || g_resizeHit == HTTOPLEFT ||
        g_resizeHit == HTBOTTOMLEFT) {
        rect.left += dx;
    }
    if (g_resizeHit == HTRIGHT || g_resizeHit == HTTOPRIGHT ||
        g_resizeHit == HTBOTTOMRIGHT) {
        rect.right += dx;
    }
    if (g_resizeHit == HTTOP || g_resizeHit == HTTOPLEFT ||
        g_resizeHit == HTTOPRIGHT) {
        rect.top += dy;
    }
    if (g_resizeHit == HTBOTTOM || g_resizeHit == HTBOTTOMLEFT ||
        g_resizeHit == HTBOTTOMRIGHT) {
        rect.bottom += dy;
    }

    if (g_resizeMinSize.x > 0) {
        if (g_resizeHit == HTLEFT || g_resizeHit == HTTOPLEFT ||
            g_resizeHit == HTBOTTOMLEFT) {
            rect.left = std::min(rect.left, rect.right - g_resizeMinSize.x);
        } else {
            rect.right = std::max(rect.right, rect.left + g_resizeMinSize.x);
        }
    }
    if (g_resizeMinSize.y > 0) {
        if (g_resizeHit == HTTOP || g_resizeHit == HTTOPLEFT ||
            g_resizeHit == HTTOPRIGHT) {
            rect.top = std::min(rect.top, rect.bottom - g_resizeMinSize.y);
        } else {
            rect.bottom = std::max(rect.bottom, rect.top + g_resizeMinSize.y);
        }
    }

    ::SetWindowPos(g_resizeHwnd, nullptr, rect.left, rect.top, rect.right - rect.left,
                   rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK ResizeMouseProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);

        if (wParam == WM_LBUTTONDOWN) {
            ResetResize();
            HWND child = ::WindowFromPoint(info->pt);
            HWND root = child ? ::GetAncestor(child, GA_ROOT) : nullptr;
            if (root && ::GetPropW(root, kResizeHookProp) && !::IsZoomed(root) &&
                !::IsIconic(root)) {
                const int hit = ResizeHitTest(root, info->pt);
                if (hit != 0) {
                    g_resizeHwnd = root;
                    g_resizeHit = hit;
                    g_resizeStartCursor = info->pt;
                    ::GetWindowRect(root, &g_resizeStartRect);
                    const auto found = g_windowMinSizes.find(root);
                    g_resizeMinSize =
                        found != g_windowMinSizes.end() ? found->second : wxSize();
                    ::SetForegroundWindow(root);
                    ::SetCapture(root);
                    SetResizeCursor(hit);
                    return 1;
                }
            }
        }

        if (g_resizeHwnd) {
            if (::GetCapture() != g_resizeHwnd) {
                ResetResize();
            } else if (wParam == WM_MOUSEMOVE) {
                POINT cursor = {};
                ::GetCursorPos(&cursor);
                ApplyResize(cursor);
                SetResizeCursor(g_resizeHit);
                return 1;
            } else if (wParam == WM_LBUTTONUP) {
                ResetResize();
                return 1;
            }
        } else if (wParam == WM_MOUSEMOVE) {
            HWND child = ::WindowFromPoint(info->pt);
            HWND root = child ? ::GetAncestor(child, GA_ROOT) : nullptr;
            if (root && ::GetPropW(root, kResizeHookProp)) {
                const int hit = ResizeHitTest(root, info->pt);
                if (hit != 0) {
                    SetResizeCursor(hit);
                    return 1;
                }
            }
        }
    }
    return ::CallNextHookEx(g_resizeHook, code, wParam, lParam);
}

void EnsureResizeHook() {
    if (g_resizeHook) return;
    // 内容面板覆盖了窗口边缘，顶层窗口收不到边缘命中测试；
    // 用线程鼠标钩子接管边缘拖动，避免在窗口内部留一圈异色边框。
    g_resizeHook = ::SetWindowsHookExW(WH_MOUSE, ResizeMouseProc, nullptr,
                                       ::GetCurrentThreadId());
}
#endif

}  // namespace

void PrepareFramelessWindow(wxWindow* window) {
    if (!window) return;

#ifdef __WXMSW__
    HWND hwnd = (HWND)window->GetHandle();
    if (!hwnd) return;

    // 无边框窗口没有 WS_THICKFRAME 时 DWM 不会绘制阴影。
    LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
    ::SetWindowLongPtrW(hwnd, GWL_STYLE, style | WS_THICKFRAME);

    // WS_POPUP + WS_THICKFRAME 会带上系统 3D 描边，可能残留黑色边线，去掉它。
    LONG_PTR exStyle = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                        exStyle & ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE |
                                    WS_EX_STATICEDGE));

    // 启动阶段可能已经套过旧尺寸的窗口区域，必须先清掉，否则会裁掉窗口。
    ::SetWindowRgn(hwnd, nullptr, TRUE);

    ::SetPropW(hwnd, kResizeHookProp, reinterpret_cast<HANDLE>(1));
    g_windowMinSizes[hwnd] = window->GetMinSize();
    EnsureResizeHook();

    // Windows 11：使用系统圆角，并隐藏系统自带描边，改由窗口内部自绘。
    int cornerPreference = 2;  // DWMWCP_ROUND
    HRESULT hr = ::DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                                         &cornerPreference, sizeof(cornerPreference));
    if (SUCCEEDED(hr)) {
        DWORD borderColor = kDwmColorNone;
        ::DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor,
                                sizeof(borderColor));
        ::SetPropW(hwnd, kDwmRoundProp, reinterpret_cast<HANDLE>(1));
    }

    ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                       SWP_FRAMECHANGED);

    // 样式/DWM 状态变化后强制重绘一次，避免边框残影留在客户区。
    ::RedrawWindow(hwnd, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);

    // 系统不支持 DWM 圆角时退回自绘圆角区域（此时没有原生阴影）。
    if (!::GetPropW(hwnd, kDwmRoundProp)) {
        ApplyRoundedWindowRegion(window, 10);
    }
#endif
}

#ifdef __WXMSW__
bool HandleFramelessWindowMessage(wxWindow* window, WXUINT message, WXWPARAM wParam,
                                  WXLPARAM lParam, WXLRESULT& result) {
    if (!window) return false;
    HWND hwnd = (HWND)window->GetHandle();
    if (!hwnd) return false;

    if (message == WM_NCCALCSIZE && wParam == TRUE) {
        // 客户区铺满整个窗口；缩放在鼠标钩子里处理。
        result = 0;
        return true;
    }

    if (message == WM_NCPAINT) {
        // 无边框窗口不需要系统绘制非客户区，避免残留原生边框线。
        result = 0;
        return true;
    }

    if (message == WM_NCACTIVATE) {
        // 同样跳过原生标题栏/边框的激活重绘。
        result = TRUE;
        return true;
    }

    if (message == WM_GETMINMAXINFO) {
        // 最大化时窗口恰好覆盖工作区，避免客户区被屏幕边缘裁掉。
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        HMONITOR monitor = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo = {sizeof(MONITORINFO)};
        if (::GetMonitorInfoW(monitor, &monitorInfo)) {
            info->ptMaxPosition.x = monitorInfo.rcWork.left - monitorInfo.rcMonitor.left;
            info->ptMaxPosition.y = monitorInfo.rcWork.top - monitorInfo.rcMonitor.top;
            info->ptMaxSize.x = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
            info->ptMaxSize.y = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
        }

        // 接管 WM_GETMINMAXINFO 后要自己带上窗口的最小/最大尺寸限制。
        wxSize minSize = window->GetMinSize();
        if (minSize.x > 0) info->ptMinTrackSize.x = minSize.x;
        if (minSize.y > 0) info->ptMinTrackSize.y = minSize.y;
        wxSize maxSize = window->GetMaxSize();
        if (maxSize.x > 0) info->ptMaxTrackSize.x = maxSize.x;
        if (maxSize.y > 0) info->ptMaxTrackSize.y = maxSize.y;

        result = 0;
        return true;
    }

    if (message == WM_DESTROY) {
        g_windowMinSizes.erase(hwnd);
        if (g_resizeHwnd == hwnd) ResetResize();
    }

    if (message == WM_EXITSIZEMOVE) {
        // 拖动/缩放结束时整窗重绘，清掉 DWM 原生边框可能留下的残线。
        ::RedrawWindow(hwnd, nullptr, nullptr,
                       RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }

    return false;
}
#endif

void ApplyRoundedWindowRegion(wxWindow* window, int radiusDip) {
    if (!window) return;

#ifdef __WXMSW__
    HWND hwnd = (HWND)window->GetHandle();
    if (!hwnd) return;

    // DWM 圆角已经生效时不能再设置窗口区域，否则原生阴影会消失。
    if (::GetPropW(hwnd, kDwmRoundProp)) return;

    wxWindow* topWindow = wxGetTopLevelParent(window);
    if (auto* top = wxDynamicCast(topWindow, wxTopLevelWindow)) {
        if (top->IsIconized() || top->IsMaximized()) {
            ::SetWindowRgn(hwnd, nullptr, TRUE);
            return;
        }
    }

    int width = 0;
    int height = 0;
    window->GetSize(&width, &height);
    if (width <= 0 || height <= 0) return;

    int radius = window->FromDIP(radiusDip);
    HRGN region = ::CreateRoundRectRgn(0, 0, width + 1, height + 1, radius * 2, radius * 2);
    if (region) {
        ::SetWindowRgn(hwnd, region, TRUE);
    }
#else
    (void)window;
    (void)radiusDip;
#endif
}

WindowFramePanel::WindowFramePanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxFULL_REPAINT_ON_RESIZE) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &WindowFramePanel::OnPaint, this);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
}

void WindowFramePanel::OnPaint(wxPaintEvent&) {
    // 只保留纯白底和系统阴影，不画边框线。
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(Theme::Background()));
    dc.Clear();
}
