#pragma once

#include <wx/minifram.h>
#include <wx/string.h>

#include <functional>

#include "core/style_model.h"
#include "ui/format_command.h"

enum class AppIcon;
class IconButton;
class wxScrolledWindow;
class wxSizer;

// 浮动样式库：加粗、斜体、下划线、删除线、字号、超链接、字体字号选择。
// 清除样式和格式刷在便签标题栏上；颜色入口交给颜色盘。
class StylePopup : public wxMiniFrame {
public:
    using ApplyHandler = std::function<void(FormatCommand command)>;

    explicit StylePopup(wxWindow* parent, ApplyHandler apply);

private:
    void BuildUi();
    void AddCommandButton(wxSizer* sizer, AppIcon icon, FormatCommand command,
                          const wxString& tip);
    void OnClose(wxCloseEvent& event);

    ApplyHandler apply_;
    wxScrolledWindow* scroller_ = nullptr;
};
