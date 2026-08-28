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
    : m_configManager(std::move(configManager)),
      m_aliveToken(std::make_shared<std::atomic<bool>>(true)) {
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

    if (!m_aliveToken) {
        m_aliveToken = std::make_shared<std::atomic<bool>>(true);
    } else {
        m_aliveToken->store(true);
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
    if (m_aliveToken) {
        m_aliveToken->store(false);
    }

    if (!m_isRunning.exchange(false)) {
        return;
    }

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
        // 综合过滤检测：如果操作发生在本项目自身窗口、或属于拖动标题栏/滑动滑条/调整窗口大小等非文本选中操作，则忽略
        if (ShouldIgnoreMouseEvent(m_ptDownX, m_ptDownY, x, y)) {
            return;
        }

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

#ifdef _WIN32
static bool IsIgnoredOrScreenshotWindow(HWND hwnd, DWORD currentPid) {
    if (!hwnd) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != 0) {
        if (pid == currentPid) {
            return true;
        }

        // 1. 检查进程可执行文件名称 (如 Windows 自带截图、Snipping Tool、Snipaste、PixPin、微信截图等)
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProc) {
            wchar_t fullPath[MAX_PATH] = { 0 };
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(hProc, 0, fullPath, &size)) {
                std::wstring exePath(fullPath);
                size_t slash = exePath.find_last_of(L"\\/");
                std::wstring exeName = (slash != std::wstring::npos) ? exePath.substr(slash + 1) : exePath;
                for (auto& ch : exeName) ch = towlower(ch);

                if (exeName == L"screenclippinghost.exe" ||
                    exeName == L"snippingtool.exe" ||
                    exeName == L"screensketch.exe" ||
                    exeName == L"snipaste.exe" ||
                    exeName == L"pixpin.exe" ||
                    exeName == L"sharex.exe" ||
                    exeName == L"lightshot.exe" ||
                    exeName == L"flameshot.exe") {
                    CloseHandle(hProc);
                    return true;
                }
            }
            CloseHandle(hProc);
        }
    }

    // 2. 检查窗口类名（截图工具、任务栏、桌面、原生滚动条等非文本区域）
    wchar_t className[128] = { 0 };
    if (GetClassNameW(hwnd, className, 128)) {
        // Windows 自带截图 / 任务栏 / 桌面 / 常见第三方截图工具 (Snipaste, PixPin, 微信/QQ截图等)
        if (_wcsicmp(className, L"ScreenClippingHost") == 0 ||
            _wcsicmp(className, L"Microsoft.ScreenSketch") == 0 ||
            _wcsicmp(className, L"SnippingTool") == 0 ||
            _wcsicmp(className, L"SnippingToolWindowAndCursorClass") == 0 ||
            _wcsicmp(className, L"Shell_TrayWnd") == 0 ||
            _wcsicmp(className, L"Progman") == 0 ||
            _wcsicmp(className, L"WorkerW") == 0 ||
            _wcsicmp(className, L"SnipasteClass") == 0 ||
            _wcsicmp(className, L"SnipasteWnd") == 0 ||
            _wcsicmp(className, L"PixPin") == 0 ||
            _wcsicmp(className, L"FLT_SCREENSHOT") == 0 ||
            _wcsicmp(className, L"ChatWnd_Screenshot") == 0 ||
            _wcsicmp(className, L"QQScreenshotWndClass") == 0 ||
            _wcsicmp(className, L"TXGuiFoundation_Screenshot") == 0 ||
            _wcsicmp(className, L"ScrollBar") == 0) {
            return true;
        }
    }

    // 3. 检查窗口标题
    wchar_t windowTitle[128] = { 0 };
    if (GetWindowTextW(hwnd, windowTitle, 128)) {
        if (wcsstr(windowTitle, L"Screen Clipping") != nullptr ||
            wcsstr(windowTitle, L"Snipping Tool") != nullptr ||
            wcsstr(windowTitle, L"截图") != nullptr) {
            return true;
        }
    }

    return false;
}
#endif

bool SelectionService::ShouldIgnoreMouseEvent(int startX, int startY, int endX, int endY) const {
#ifdef _WIN32
    DWORD currentPid = GetCurrentProcessId();

    // 1. 检查鼠标释放点所在的窗体
    POINT ptUp = { endX, endY };
    HWND hwndUp = WindowFromPoint(ptUp);
    if (IsIgnoredOrScreenshotWindow(hwndUp, currentPid)) {
        return true;
    }

    // 2. 检查鼠标按起点所在的窗体
    POINT ptDown = { startX, startY };
    HWND hwndDown = WindowFromPoint(ptDown);
    if (IsIgnoredOrScreenshotWindow(hwndDown, currentPid)) {
        return true;
    }

    // 3. 检查当前前景激活窗体
    HWND hwndForeground = GetForegroundWindow();
    if (IsIgnoredOrScreenshotWindow(hwndForeground, currentPid)) {
        return true;
    }

    // 4. 检查非客户区操作（如拖拽标题栏移动窗体、滑动滚动条、拖拉边框调整大小等与文本选中无关的操作）
    auto checkNcHit = [](HWND hwnd, int px, int py) -> bool {
        if (!hwnd) return false;
        DWORD_PTR hitResult = 0;
        // 使用安全超时调用（30ms），防止目标第三方宿主窗口无响应导致卡顿
        if (SendMessageTimeoutW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(px, py),
                                SMTO_ABORTIFHUNG | SMTO_NORMAL, 30, &hitResult)) {
            switch (hitResult) {
                case HTCAPTION:     // 标题栏（拖动窗口）
                case HTVSCROLL:    // 垂直滚动条
                case HTHSCROLL:    // 水平滚动条
                case HTLEFT:       // 调整窗口左边框
                case HTRIGHT:      // 调整窗口右边框
                case HTTOP:        // 调整窗口上边框
                case HTBOTTOM:     // 调整窗口下边框
                case HTTOPLEFT:    // 调整窗口左上角
                case HTTOPRIGHT:   // 调整窗口右上角
                case HTBOTTOMLEFT: // 调整窗口左下角
                case HTBOTTOMRIGHT:// 调整窗口右下角
                case HTGROWBOX:    // 调整大小手柄
                case HTMINBUTTON:  // 最小化按钮
                case HTMAXBUTTON:  // 最大化按钮
                case HTCLOSE:      // 关闭按钮
                case HTMENU:       // 菜单栏
                case HTSYSMENU:    // 系统菜单
                    return true;
                default:
                    break;
            }
        }
        return false;
    };

    if (checkNcHit(hwndDown, startX, startY) || checkNcHit(hwndUp, endX, endY)) {
        return true;
    }

    return false;
#else
    return false;
#endif
}

void SelectionService::ProcessSelectionAsync(int startX, int startY, int endX, int endY) {
    bool preserve = m_preserveClipboard.load();
    auto aliveToken = m_aliveToken;

    std::thread([this, aliveToken, startX, startY, endX, endY, preserve]() {
        // 短暂延迟 35ms 确保被划词的宿主窗口完成 MouseUp 并进入选中高亮状态
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
        if (!aliveToken->load()) {
            return;
        }

#ifdef _WIN32
        DWORD currentPid = GetCurrentProcessId();
        HWND fgWnd = GetForegroundWindow();
        if (IsIgnoredOrScreenshotWindow(fgWnd, currentPid)) {
            return;
        }
#endif

        ExtractedSelection extracted = ScreenTextExtractor::ExtractSelection(startX, startY, endX, endY, preserve);
        if (!aliveToken->load()) {
            return;
        }

        if (extracted.text.empty() || extracted.text.size() > 8000) {
            return;
        }

        // 投递回调到 UI 线程
        if (wxTheApp && aliveToken->load()) {
            wxTheApp->CallAfter([this, aliveToken, extracted]() {
                if (!aliveToken->load()) {
                    return;
                }
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
