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
    // 默认几何锚点：选区包围盒最右下角
    int minX = (std::min)(startX, endX);
    int maxX = (std::max)(startX, endX);
    int minY = (std::min)(startY, endY);
    int maxY = (std::max)(startY, endY);

    result.anchorX = maxX + 6;
    result.anchorY = maxY + 8;

    // 阶段 1：首选高速剪贴板提取 (SendInput)
    std::string text = ClipboardHelper::GetSelectedTextViaSendInput(preserveClipboard);
    if (!text.empty() && text.size() <= 8000) {
        result.text = text;
        result.source = "Clipboard";
        return result;
    }

    // 阶段 2：剪贴板未命中时，尝试 Windows UI Automation 无障碍查询
    // (macOS 对应实现 TODO: AXUIElementCopyAttributeValue with kAXSelectedTextAttribute)
    int uiaAnchorX = result.anchorX;
    int uiaAnchorY = result.anchorY;
    if (ExtractViaUIAutomation(endX, endY, text, uiaAnchorX, uiaAnchorY)) {
        result.text = text;
        result.anchorX = uiaAnchorX;
        result.anchorY = uiaAnchorY;
        result.source = "UIAutomation";
        LOG_INFO("ScreenTextExtractor", "Extracted via UIAutomation: \"" + text + "\"");
        return result;
    }

    // 阶段 3：轻量级屏幕切片极速 OCR 识别兜底 (Windows.Media.Ocr 约 5~15ms)
    // (macOS 对应实现 TODO: Vision Framework VNRecognizeTextRequest)
    int ocrAnchorX = result.anchorX;
    int ocrAnchorY = result.anchorY;
    if (ExtractViaScreenOcr(startX, startY, endX, endY, text, ocrAnchorX, ocrAnchorY)) {
        result.text = text;
        result.anchorX = ocrAnchorX;
        result.anchorY = ocrAnchorY;
        result.source = "ScreenOCR";
        LOG_INFO("ScreenTextExtractor", "Extracted via Windows Media OCR: \"" + text + "\"");
        return result;
    }

    return result;
}

} // namespace LinguaAlpaca
