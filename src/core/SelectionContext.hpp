#pragma once
#pragma execution_character_set("utf-8")

#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <unistd.h>
#include <sys/types.h>
#endif

namespace LinguaAlpaca {

/**
 * @brief 划词触发时的屏幕位置及宿主上下文信息
 */
struct SelectionContext {
    int startX{0};
    int startY{0};
    int endX{0};
    int endY{0};
    int clickCount{1};            // 触发时的点击次数（1: 划词拖拽, 2: 双击选词, 3: 三击选句）
    std::string preExtractedText; // 预检阶段提取到的选中文本（若非空，点击悬浮图标时直接复用）

#ifdef _WIN32
    HWND targetHwnd{nullptr};
#elif defined(__APPLE__)
    pid_t targetPid{0};
#endif
};

} // namespace LinguaAlpaca
