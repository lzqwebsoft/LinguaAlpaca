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

    // 阶段 1：首选 Windows UI Automation 无障碍选区精准查询 (非侵入式，完全不触碰/不污染剪贴板)
    // (macOS 对应实现 TODO: AXUIElementCopyAttributeValue with kAXSelectedTextAttribute)
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

    // 阶段 2：UIAutomation 未命中时，使用剪贴板 SendInput 模拟 Ctrl+C 提取 (完整全格式保护)
    text = ClipboardHelper::GetSelectedTextViaSendInput(preserveClipboard);
    if (!text.empty() && text.size() <= 8000) {
        result.text = text;
        result.anchorX = endX;
        result.anchorY = endY;
        result.source = "Clipboard";
        return result;
    }

    // 注意：不再在鼠标划词时使用盲目的屏幕切片 OCR 兜底，
    // 防止用户在截图、拖动窗口、框选桌面图标时误识别屏幕底图文字而产生误触发。
    return result;
}

} // namespace LinguaAlpaca
