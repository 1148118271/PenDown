#pragma once

#include <wx/colour.h>
#include <wx/minifram.h>
#include <wx/timer.h>

#include <functional>

class wxPanel;
class wxSlider;
class wxStaticText;
class wxToggleButton;

enum class ColorTarget {
    Foreground,
    Background,
};

// 浮动颜色盘：常用色 + 自定义 RGB。
// 没有“确定”按钮，点色块/拖动滑杆会直接回调当前选区和目标。
class ColorPopup : public wxMiniFrame {
public:
    using ApplyHandler =
        std::function<void(const wxColour& colour, bool clear, ColorTarget target)>;

    ColorPopup(wxWindow* parent, const wxColour& initial, ApplyHandler apply);

    void SetColour(const wxColour& colour);
    ColorTarget Target() const { return target_; }
    void SetTarget(ColorTarget target);
    // 便签窗口销毁前调用，避免定时器回调到已失效的目标。
    void StopPending() { applyTimer_.Stop(); }

private:
    void BuildUi();
    void SelectSwatch(const wxColour& colour, bool clear);
    void UpdatePreview();
    void ApplyCurrent();
    void OnClose(wxCloseEvent& event);
    void UpdateTargetButtons();

    ApplyHandler apply_;
    wxColour colour_;
    ColorTarget target_ = ColorTarget::Foreground;
    wxPanel* preview_ = nullptr;
    wxSlider* red_ = nullptr;
    wxSlider* green_ = nullptr;
    wxSlider* blue_ = nullptr;
    wxStaticText* values_ = nullptr;
    wxToggleButton* textButton_ = nullptr;
    wxToggleButton* backgroundButton_ = nullptr;
    wxTimer applyTimer_;
    bool updating_ = false;
};
