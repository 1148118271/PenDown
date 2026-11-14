#pragma once

#include <wx/defs.h>

// 主界面、便签窗口用到的命令 ID
enum {
    ID_NOTE_TREE = wxID_HIGHEST + 100,

    ID_NOTE_ADD_GROUP,
    ID_NOTE_ADD,

    ID_NOTE_SAVE,
    ID_NOTE_DELETE,
    ID_NOTE_COLOR,
    ID_NOTE_STYLES,
    ID_NOTE_CLEAR_FORMAT,
    ID_NOTE_FORMAT_PAINTER,

    ID_WINDOW_MINIMIZE,
    ID_WINDOW_MAXIMIZE,
    ID_WINDOW_CLOSE,

    ID_SETTINGS,
};
