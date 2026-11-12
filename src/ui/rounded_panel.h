#pragma once

#include <wx/panel.h>

class wxBoxSizer;

// 浅色圆角容器：内部留一点内边距，外面画一圈很淡的圆角边框。
// 列表、搜索框等原生控件先去掉自己的方角边框，再放进这个容器里。
class RoundedPanel : public wxPanel {
public:
    explicit RoundedPanel(wxWindow* parent, int radius = 7, int padding = 6);
    // 允许上下与左右使用不同内边距，编辑区可以更贴边。
    RoundedPanel(wxWindow* parent, int radius, int paddingX, int paddingY);

    void AddContent(wxWindow* content, int proportion = 1);

    // 编辑区只需要上下两条分隔线，去掉左右边框和圆角。
    void SetHorizontalBordersOnly(bool only) {
        horizontalBordersOnly_ = only;
        Refresh();
    }

private:
    void OnPaint(wxPaintEvent& event);

    wxBoxSizer* contentSizer_ = nullptr;
    int radius_ = 10;
    bool horizontalBordersOnly_ = false;
};
