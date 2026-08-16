#pragma once
#pragma execution_character_set("utf-8")

#include <string>

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

private:
    static bool SendCtrlC();
};

} // namespace LinguaAlpaca
