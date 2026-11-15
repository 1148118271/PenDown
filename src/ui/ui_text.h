#pragma once

#include <wx/string.h>

#include <string>

#include "core/i18n.h"
#include "core/note_store.h"
#include "core/text.h"

// UI 层按消息 key 取文案，返回 wxString，方便直接传给控件和 wxString::Format。
inline wxString UiText(const char* key) { return FromW(i18n::Tr(key)); }

// 磁盘上的默认标题保持稳定；英文界面只改变显示文字。
inline std::wstring DisplayNoteTitle(const std::wstring& title) {
    if (title == kDefaultNoteTitle) return i18n::Tr("note.default_title");
    return title;
}

inline std::wstring DisplayGroupName(const std::wstring& name) {
    if (name == kDefaultGroupName) return i18n::Tr("note.default_group");
    return name;
}
