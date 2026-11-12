#pragma once

#include <wx/string.h>

class wxRichTextCtrl;

// 把 Markdown 渲染到只读富文本控件；用于 Markdown 便签的预览模式单窗渲染。
void RenderMarkdownPreview(wxRichTextCtrl* target, const wxString& markdown);
