#pragma once
#pragma execution_character_set("utf-8")

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace LinguaAlpaca {

class ClipboardHelper {
public:
    // 获取当前剪贴板中的纯文本 (UTF-8 编码)
    static std::string GetClipboardText();

    // 将 UTF-8 文本设置到系统剪贴板
    static bool SetClipboardText(const std::string& text);

    // 跨进程通过 SendInput(Ctrl+C) 提取选中文本
    // preserveClipboard 为 true 时，在提取后自动恢复原来的剪贴板内容
    static std::string GetSelectedTextViaSendInput(bool preserveClipboard = true);

    // 检查剪贴板是否包含有效文本
    static bool HasText();

#ifdef _WIN32
    // 检查指定窗口是否属于 PDF 阅读器 (如 Adobe Acrobat/Reader, Foxit, SumatraPDF 等)
    static bool IsPdfReaderWindow(HWND hwnd);

    // 检查指定窗口是否属于已知无障碍 (UIA/TextPattern) 受限的目标应用
    // (包括各类 PDF 阅读器、VS Code、Cursor、Sublime、JetBrains、WPS、各类终端等)
    static bool IsNonAxTargetWindow(HWND hwnd);
#endif

private:
    static bool SendCtrlC();
};

} // namespace LinguaAlpaca
