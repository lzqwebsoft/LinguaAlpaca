#pragma once
#pragma execution_character_set("utf-8")

#include <string>

namespace LinguaAlpaca {

struct ExtractedSelection {
    std::string text;
    int anchorX{0};     // 建议的悬浮图标弹出 X 坐标
    int anchorY{0};     // 建议的悬浮图标弹出 Y 坐标
    std::string source; // "Clipboard", "UIAutomation", "ScreenOCR", etc.
};

class ScreenTextExtractor {
public:
    // 三级综合提取：
    // 1. 剪贴板 (SendInput Ctrl+C)
    // 2. Windows UI Automation (无障碍模式)
    // 3. Windows 原生 Media.Ocr / GDI 屏幕切片识别 (毫秒级 OCR 兜底)
    static ExtractedSelection ExtractSelection(
        int startX, int startY, int endX, int endY,
        bool preserveClipboard = true
    );

    // 单独的 UI Automation 提取
    static bool ExtractViaUIAutomation(int x, int y, std::string& outText, int& outAnchorX, int& outAnchorY);

    // 单独的屏幕切片 OCR 提取
    static bool ExtractViaScreenOcr(int startX, int startY, int endX, int endY, std::string& outText, int& outAnchorX, int& outAnchorY);
};

} // namespace LinguaAlpaca
