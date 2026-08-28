#pragma execution_character_set("utf-8")
#include "ScreenTextExtractor.hpp"
#include "ClipboardHelper.hpp"
#include "WinUIAutomationHelper.hpp"
#include "WinMediaOcrHelper.hpp"
#include "Logger.hpp"

#include <algorithm>

namespace LinguaAlpaca {

bool ScreenTextExtractor::ExtractViaUIAutomation(int x, int y, std::string& outText, int& outAnchorX, int& outAnchorY) {
    return WinUIAutomationHelper::TryExtract(x, y, outText, outAnchorX, outAnchorY);
}

bool ScreenTextExtractor::ExtractViaScreenOcr(int startX, int startY, int endX, int endY, std::string& outText, int& outAnchorX, int& outAnchorY) {
    return WinMediaOcrHelper::TryExtract(startX, startY, endX, endY, outText, outAnchorX, outAnchorY);
}

ExtractedSelection ScreenTextExtractor::ExtractSelection(
    int startX, int startY, int endX, int endY,
    bool preserveClipboard
) {
    ExtractedSelection result;
    // 浮动图标锚点使用鼠标最终释放弹起时的位置 (endX, endY)
    result.anchorX = endX;
    result.anchorY = endY;

    // 阶段 1：首选 Windows UI Automation 无障碍选区精准查询 (非侵入式，完全不触碰/不污染剪贴板，不破坏选区)
    int uiaAnchorX = endX;
    int uiaAnchorY = endY;
    std::string text;
    if (ExtractViaUIAutomation(endX, endY, text, uiaAnchorX, uiaAnchorY)) {
        if (!text.empty() && text.size() <= 8000) {
            result.text = text;
            result.anchorX = endX;
            result.anchorY = endY;
            result.source = "UIAutomation";
            LOG_INFO("ScreenTextExtractor", "Extracted via UIAutomation: \"" + text + "\"");
            return result;
        }
    }

    // 阶段 1.1：极短微重试 (20ms) 应对极少数 XAML / 终端组件 MouseUp 瞬间可访问性树的短暂延迟
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (ExtractViaUIAutomation(endX, endY, text, uiaAnchorX, uiaAnchorY)) {
        if (!text.empty() && text.size() <= 8000) {
            result.text = text;
            result.anchorX = endX;
            result.anchorY = endY;
            result.source = "UIAutomation";
            LOG_INFO("ScreenTextExtractor", "Extracted via UIAutomation (retry): \"" + text + "\"");
            return result;
        }
    }

    // 阶段 2：UI Automation 未命中时（如 VS Code 终端 canvas/xterm.js、各类编辑器与终端），
    // 使用增强版数字剪贴板提取（优先发送无破坏性的 Ctrl+Insert，100% 精确获取原始数字字符且不取消选区）
    text = ClipboardHelper::GetSelectedTextViaSendInput(preserveClipboard);
    if (!text.empty() && text.size() <= 8000) {
        result.text = text;
        result.anchorX = endX;
        result.anchorY = endY;
        result.source = "Clipboard";
        LOG_INFO("ScreenTextExtractor", "Extracted via Clipboard (Ctrl+Insert/Ctrl+C): \"" + text + "\"");
        return result;
    }

    return result;
}

} // namespace LinguaAlpaca
