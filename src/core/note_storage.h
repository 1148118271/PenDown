#pragma once

#include <cstdint>
#include <string>

#include "core/style_model.h"

// TXT + JSON 样式文件存储。
//
// 正文：UTF-8 .txt，不包含 Markdown 语法。
// 样式：<正文路径>.style.json，记录 UTF-8 字节区间、颜色、字体等属性。
//
// 正文和样式分别写临时文件，再原子替换；样式文件带正文长度和 hash，
// 任何一边不匹配时只丢样式，不把样式套到错误的位置。
class NoteStorage {
public:
    static std::wstring StylePath(const std::wstring& bodyPath);
    static std::wstring AutosavePath(const std::wstring& bodyPath);
    static std::wstring AutosaveStylePath(const std::wstring& bodyPath);

    static bool ReadTextFile(const std::wstring& path, std::string& text,
                             std::wstring& error);
    static bool WriteTextFile(const std::wstring& path, const std::string& text,
                              std::wstring& error);

    static bool Load(const std::wstring& bodyPath, std::string& text,
                     StyleModel& styles, std::wstring& warning);
    static bool Save(const std::wstring& bodyPath, const std::string& text,
                     const StyleModel& styles, std::wstring& error);
    static bool SaveAutosave(const std::wstring& bodyPath, const std::string& text,
                             const StyleModel& styles, std::wstring& error);
    static bool HasAutosave(const std::wstring& bodyPath);
    static bool LoadAutosave(const std::wstring& bodyPath, std::string& text,
                             StyleModel& styles, std::wstring& warning);
    static void RemoveAutosave(const std::wstring& bodyPath);

    static uint64_t HashText(const std::string& text);

private:
    static bool ReplaceFileAtomic(const std::wstring& source,
                                  const std::wstring& target, std::wstring& error);
    static bool BackupFile(const std::wstring& path);
    static bool WriteBytes(const std::wstring& path, const std::string& bytes,
                           std::wstring& error);
};
