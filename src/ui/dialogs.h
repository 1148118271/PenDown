#pragma once

#include <wx/string.h>

class wxWindow;

namespace dialogs {

// 与 wxMessageBox 用法一致，但会按当前语言设置确定/是/否/取消按钮。
int Message(wxWindow* parent, const wxString& message, const wxString& title,
            long style);

// 系统自带对话框（wxSingleChoiceDialog、wxTextEntryDialog 等）的按钮文字
// 由 wxWidgets 创建，创建后调用一次即可统一成当前语言。
void LocalizeStandardButtons(wxWindow* dialog);

}  // namespace dialogs
