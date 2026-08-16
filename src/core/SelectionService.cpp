#pragma execution_character_set("utf-8")
#include "SelectionService.hpp"
#include "ClipboardHelper.hpp"
#include "ScreenTextExtractor.hpp"
#include "Logger.hpp"

#include <wx/app.h>
#include <iostream>
#include <thread>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace LinguaAlpaca {

namespace {
SelectionService* g_activeService = nullptr;
HHOOK g_mouseHook = nullptr;

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_activeService && g_activeService->IsRunning()) {
        MSLLHOOKSTRUCT* pMouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        if (pMouse) {
            g_activeService->OnLowLevelMouseEvent((int)wParam, pMouse->pt.x, pMouse->pt.y);
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}
} // namespace

SelectionService::SelectionService(std::shared_ptr<ConfigManager> configManager)
    : m_configManager(std::move(configManager)) {
    if (m_configManager) {
        ApplyConfig(m_configManager->GetConfig());
    }
}

SelectionService::~SelectionService() {
    Stop();
}

bool SelectionService::Start() {
    if (m_isRunning.load()) {
        return true;
    }

    g_activeService = this;
    g_mouseHook = SetWindowsHookEx(
        WH_MOUSE_LL,
        LowLevelMouseProc,
        GetModuleHandle(nullptr),
        0
    );

    if (!g_mouseHook) {
        LOG_ERROR("SelectionService", "Failed to install WH_MOUSE_LL hook! Error: " + std::to_string(GetLastError()));
        return false;
    }

    m_hookHandle = g_mouseHook;
    m_isRunning.store(true);
    LOG_INFO("SelectionService", "Global mouse hook started successfully.");
    return true;
}

void SelectionService::Stop() {
    if (!m_isRunning.load()) {
        return;
    }

    m_isRunning.store(false);
    if (g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
        m_hookHandle = nullptr;
    }
    if (g_activeService == this) {
        g_activeService = nullptr;
    }
    LOG_INFO("SelectionService", "Global mouse hook stopped.");
}

void SelectionService::SetCallback(SelectionDetectedCallback callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_callback = std::move(callback);
}

void SelectionService::ApplyConfig(const AppConfig& config) {
    m_enabled.store(config.selectionTranslateEnabled);
    m_triggerMode.store(config.selectionTriggerMode);
    m_modifierKey.store(config.selectionModifierKey);
    m_preserveClipboard.store(config.preserveClipboard);
}

void SelectionService::OnLowLevelMouseEvent(int message, int x, int y) {
    if (!m_enabled.load()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();

    if (message == WM_LBUTTONDOWN) {
        m_ptDownX = x;
        m_ptDownY = y;
        m_timeDown = now;

        // 连击检测 (双击/三击)
        int dx = x - m_lastClickX;
        int dy = y - m_lastClickY;
        UINT doubleClickTime = GetDoubleClickTime();
        auto clickIntervalMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastClickTime).count();
        if ((clickIntervalMs <= static_cast<long long>(doubleClickTime)) && (dx * dx + dy * dy <= 36)) {
            m_clickCount++;
        } else {
            m_clickCount = 1;
        }

        m_lastClickTime = now;
        m_lastClickX = x;
        m_lastClickY = y;
        return;
    }

    if (message == WM_LBUTTONUP) {
        int dx = x - m_ptDownX;
        int dy = y - m_ptDownY;
        int distSq = dx * dx + dy * dy;
        auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_timeDown).count();

        int mode = m_triggerMode.load();
        bool shouldTrigger = false;

        if (mode == 0) {
            // 模式 ①：鼠标直接划词（位移 >= 6px 且 耗时 70ms~5000ms）
            if (distSq >= 36 && durationMs >= 70 && durationMs <= 5000) {
                shouldTrigger = true;
            }
        } else if (mode == 1) {
            // 模式 ②：划词 + 辅助按键
            int modKey = m_modifierKey.load();
            int vk = VK_CONTROL;
            if (modKey == 1) vk = VK_MENU;       // Alt
            else if (modKey == 2) vk = VK_SHIFT; // Shift

            bool isModDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
            if (isModDown && (distSq >= 16 || m_clickCount >= 2)) {
                shouldTrigger = true;
            }
        } else if (mode == 2) {
            // 模式 ③：双击划词 / 三击划段
            if (m_clickCount >= 2 && distSq <= 36) {
                shouldTrigger = true;
            }
        }

        if (shouldTrigger) {
            ProcessSelectionAsync(m_ptDownX, m_ptDownY, x, y);
        }
    }
}

void SelectionService::ProcessSelectionAsync(int startX, int startY, int endX, int endY) {
    bool preserve = m_preserveClipboard.load();

    std::thread([this, startX, startY, endX, endY, preserve]() {
        // 短暂延迟 30ms 确保被划词的宿主窗口完成 MouseUp 并进入选中高亮状态
        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        ExtractedSelection extracted = ScreenTextExtractor::ExtractSelection(startX, startY, endX, endY, preserve);

        if (extracted.text.empty() || extracted.text.size() > 8000) {
            return;
        }

        // 投递回调到 UI 线程
        if (wxTheApp) {
            wxTheApp->CallAfter([this, extracted]() {
                SelectionDetectedCallback cb;
                {
                    std::lock_guard<std::mutex> lock(m_callbackMutex);
                    cb = m_callback;
                }
                LOG_INFO("SelectionService", "Trigger callback [Source=" + extracted.source
                         + ", x=" + std::to_string(extracted.anchorX)
                         + ", y=" + std::to_string(extracted.anchorY)
                         + ", text=\"" + extracted.text + "\"]");
                if (cb) {
                    cb(extracted.anchorX, extracted.anchorY, extracted.text);
                }
            });
        }
    }).detach();
}

} // namespace LinguaAlpaca

#else // Non-Windows fallback

namespace LinguaAlpaca {
SelectionService::SelectionService(std::shared_ptr<ConfigManager> configManager) : m_configManager(std::move(configManager)) {}
SelectionService::~SelectionService() {}
bool SelectionService::Start() { return false; }
void SelectionService::Stop() {}
void SelectionService::SetCallback(SelectionDetectedCallback) {}
void SelectionService::ApplyConfig(const AppConfig&) {}
void SelectionService::OnLowLevelMouseEvent(int, int, int) {}
void SelectionService::ProcessSelectionAsync(int, int, int, int) {}
} // namespace LinguaAlpaca

#endif
