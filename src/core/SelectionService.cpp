#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif
#include "SelectionService.hpp"
#include "ClipboardHelper.hpp"
#include "ScreenTextExtractor.hpp"
#include "Logger.hpp"

#include <wx/app.h>
#include <wx/toplevel.h>
#include <iostream>
#include <thread>
#include <cmath>

#ifndef WM_LBUTTONDOWN
#define WM_LBUTTONDOWN 0x0201
#endif
#ifndef WM_LBUTTONUP
#define WM_LBUTTONUP 0x0202
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#include <unistd.h>
#endif

namespace LinguaAlpaca {

#ifdef _WIN32
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
#endif

SelectionService::SelectionService(std::shared_ptr<ConfigManager> configManager)
    : m_configManager(std::move(configManager))
    , m_aliveToken(std::make_shared<std::atomic<bool>>(true)) {
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

#ifdef _WIN32
    g_activeService = this;
    g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle(nullptr), 0);

    if (!g_mouseHook) {
        LOG_ERROR("SelectionService", "Failed to install WH_MOUSE_LL hook! Error: " + std::to_string(GetLastError()));
        return false;
    }

    m_hookHandle = g_mouseHook;
    m_isRunning.store(true);
    LOG_INFO("SelectionService", "Global mouse hook started successfully.");
    return true;
#elif defined(__APPLE__)
    @autoreleasepool {
        NSEventMask mask = NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp;
        id monitor = [NSEvent addGlobalMonitorForEventsMatchingMask:mask
                                                            handler:^(NSEvent* event) {
                                                                if (!m_isRunning.load()) {
                                                                    return;
                                                                }
                                                                CGPoint pt = CGEventGetLocation([event CGEvent]);
                                                                int x = static_cast<int>(std::round(pt.x));
                                                                int y = static_cast<int>(std::round(pt.y));

                                                                NSEventType type = [event type];
                                                                if (type == NSEventTypeLeftMouseDown) {
                                                                    OnLowLevelMouseEvent(WM_LBUTTONDOWN, x, y);
                                                                } else if (type == NSEventTypeLeftMouseUp) {
                                                                    OnLowLevelMouseEvent(WM_LBUTTONUP, x, y);
                                                                }
                                                            }];

        if (!monitor) {
            LOG_ERROR("SelectionService", "Failed to install macOS global mouse monitor.");
            return false;
        }

        m_hookHandle = (void*)[monitor retain];
        m_isRunning.store(true);
        LOG_INFO("SelectionService", "macOS global mouse monitor started successfully.");
        return true;
    }
#else
    return false;
#endif
}

void SelectionService::Stop() {
    if (m_aliveToken) {
        m_aliveToken->store(false);
    }

    if (!m_isRunning.exchange(false)) {
        return;
    }

#ifdef _WIN32
    if (g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
        m_hookHandle = nullptr;
    }
    if (g_activeService == this) {
        g_activeService = nullptr;
    }
    LOG_INFO("SelectionService", "Global mouse hook stopped.");
#elif defined(__APPLE__)
    if (m_hookHandle) {
        id monitor = (id)m_hookHandle;
        [NSEvent removeMonitor:monitor];
        [monitor release];
        m_hookHandle = nullptr;
    }
    LOG_INFO("SelectionService", "macOS global mouse monitor stopped.");
#endif
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
        long long doubleClickMs = 500;
#ifdef _WIN32
        doubleClickMs = static_cast<long long>(GetDoubleClickTime());
#elif defined(__APPLE__)
        doubleClickMs = static_cast<long long>([NSEvent doubleClickInterval] * 1000.0);
#endif
        auto clickIntervalMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastClickTime).count();
        if ((clickIntervalMs <= doubleClickMs) && (dx * dx + dy * dy <= 36)) {
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
            // 模式 ①：鼠标直接划词（位移 >= 5px 且 耗时 40ms~5000ms，或双击/三击选词）
            if ((distSq >= 25 && durationMs >= 40 && durationMs <= 5000) || (m_clickCount >= 2 && distSq <= 36)) {
                shouldTrigger = true;
            }
        } else if (mode == 1) {
            // 模式 ②：划词 + 辅助按键
            int modKey = m_modifierKey.load();
            bool isModDown = false;
#ifdef _WIN32
            int vk = VK_CONTROL;
            if (modKey == 1)
                vk = VK_MENU; // Alt
            else if (modKey == 2)
                vk = VK_SHIFT; // Shift
            isModDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
#elif defined(__APPLE__)
            CGEventFlags flags = CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
            if (modKey == 0) { // Ctrl 或 Cmd 均可触发
                isModDown = (flags & kCGEventFlagMaskControl) != 0 || (flags & kCGEventFlagMaskCommand) != 0;
            } else if (modKey == 1) { // Option / Alt
                isModDown = (flags & kCGEventFlagMaskAlternate) != 0;
            } else if (modKey == 2) { // Shift
                isModDown = (flags & kCGEventFlagMaskShift) != 0;
            }
#endif
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
            SelectionContext ctx;
            ctx.startX = m_ptDownX;
            ctx.startY = m_ptDownY;
            ctx.endX = x;
            ctx.endY = y;
            ctx.clickCount = m_clickCount;
#ifdef _WIN32
            ctx.targetHwnd = GetForegroundWindow();
#elif defined(__APPLE__)
            @autoreleasepool {
                NSRunningApplication* frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
                if (frontApp) {
                    ctx.targetPid = [frontApp processIdentifier];
                }
            }
#endif
            CheckAndNotifyIfTextSelectedAsync(ctx);
        }
    }
}

#ifdef _WIN32
static bool IsIgnoredOrScreenshotWindow(HWND hwnd, DWORD currentPid) {
    if (!hwnd)
        return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != 0) {
        if (pid == currentPid) {
            return true;
        }

        // 1. 检查进程可执行文件名称 (如 Windows 自带截图、Snipping Tool、Snipaste、PixPin、微信截图等)
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProc) {
            wchar_t fullPath[MAX_PATH] = {0};
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(hProc, 0, fullPath, &size)) {
                std::wstring exePath(fullPath);
                size_t slash = exePath.find_last_of(L"\\/");
                std::wstring exeName = (slash != std::wstring::npos) ? exePath.substr(slash + 1) : exePath;
                for (auto& ch : exeName)
                    ch = towlower(ch);

                if (exeName == L"screenclippinghost.exe" || exeName == L"snippingtool.exe" || exeName == L"screensketch.exe" || exeName == L"snipaste.exe" || exeName == L"pixpin.exe" ||
                    exeName == L"sharex.exe" || exeName == L"lightshot.exe" || exeName == L"flameshot.exe") {
                    CloseHandle(hProc);
                    return true;
                }
            }
            CloseHandle(hProc);
        }
    }

    // 2. 检查窗口类名（截图工具、任务栏、桌面、原生滚动条等非文本区域）
    wchar_t className[128] = {0};
    if (GetClassNameW(hwnd, className, 128)) {
        // Windows 自带截图 / 任务栏 / 桌面 / 常见第三方截图工具 (Snipaste, PixPin, 微信/QQ截图等)
        if (_wcsicmp(className, L"ScreenClippingHost") == 0 || _wcsicmp(className, L"Microsoft.ScreenSketch") == 0 || _wcsicmp(className, L"SnippingTool") == 0 ||
            _wcsicmp(className, L"SnippingToolWindowAndCursorClass") == 0 || _wcsicmp(className, L"Shell_TrayWnd") == 0 || _wcsicmp(className, L"Progman") == 0 ||
            _wcsicmp(className, L"WorkerW") == 0 || _wcsicmp(className, L"SnipasteClass") == 0 || _wcsicmp(className, L"SnipasteWnd") == 0 || _wcsicmp(className, L"PixPin") == 0 ||
            _wcsicmp(className, L"FLT_SCREENSHOT") == 0 || _wcsicmp(className, L"ChatWnd_Screenshot") == 0 || _wcsicmp(className, L"QQScreenshotWndClass") == 0 ||
            _wcsicmp(className, L"TXGuiFoundation_Screenshot") == 0 || _wcsicmp(className, L"ScrollBar") == 0) {
            return true;
        }
    }

    // 3. 检查窗口标题
    wchar_t windowTitle[128] = {0};
    if (GetWindowTextW(hwnd, windowTitle, 128)) {
        if (wcsstr(windowTitle, L"Screen Clipping") != nullptr || wcsstr(windowTitle, L"Snipping Tool") != nullptr || wcsstr(windowTitle, L"截图") != nullptr) {
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
    POINT ptUp = {endX, endY};
    HWND hwndUp = WindowFromPoint(ptUp);
    if (IsIgnoredOrScreenshotWindow(hwndUp, currentPid)) {
        return true;
    }

    // 2. 检查鼠标按起点所在的窗体
    POINT ptDown = {startX, startY};
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
        if (!hwnd)
            return false;
        DWORD_PTR hitResult = 0;
        // 使用安全超时调用（30ms），防止目标第三方宿主窗口无响应导致卡顿
        if (SendMessageTimeoutW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(px, py), SMTO_ABORTIFHUNG | SMTO_NORMAL, 30, &hitResult)) {
            switch (hitResult) {
            case HTCAPTION:     // 标题栏（拖动窗口）
            case HTVSCROLL:     // 垂直滚动条
            case HTHSCROLL:     // 水平滚动条
            case HTLEFT:        // 调整窗口左边框
            case HTRIGHT:       // 调整窗口右边框
            case HTTOP:         // 调整窗口上边框
            case HTBOTTOM:      // 调整窗口下边框
            case HTTOPLEFT:     // 调整窗口左上角
            case HTTOPRIGHT:    // 调整窗口右上角
            case HTBOTTOMLEFT:  // 调整窗口左下角
            case HTBOTTOMRIGHT: // 调整窗口右下角
            case HTGROWBOX:     // 调整大小手柄
            case HTMINBUTTON:   // 最小化按钮
            case HTMAXBUTTON:   // 最大化按钮
            case HTCLOSE:       // 关闭按钮
            case HTMENU:        // 菜单栏
            case HTSYSMENU:     // 系统菜单
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
#elif defined(__APPLE__)
    // 1. 检查是否在 LinguaAlpaca 自身的主窗口、悬浮图标或气泡等顶层窗口内
    wxWindowList& windows = wxTopLevelWindows;
    for (wxWindowList::compatibility_iterator node = windows.GetFirst(); node; node = node->GetNext()) {
        wxWindow* win = node->GetData();
        if (win && win->IsShown()) {
            wxRect r = win->GetScreenRect();
            if (r.Contains(startX, startY) || r.Contains(endX, endY)) {
                return true;
            }
        }
    }

    // 2. 检查当前前台激活的应用是否属于本项目进程或屏幕截图/录屏工具
    @autoreleasepool {
        NSRunningApplication* frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (frontApp) {
            if (frontApp.processIdentifier == getpid()) {
                return true;
            }
            NSString* bundleId = [frontApp bundleIdentifier];
            if (bundleId) {
                if ([bundleId containsString:@"screencapture"] || [bundleId containsString:@"Snipaste"] || [bundleId containsString:@"CleanShot"] || [bundleId containsString:@"Shottr"] ||
                    [bundleId containsString:@"Flameshot"] || [bundleId containsString:@"Kap"]) {
                    return true;
                }
            }
            NSString* appName = [frontApp localizedName];
            if (appName) {
                if ([appName containsString:@"截图"] || [appName containsString:@"截屏"]) {
                    return true;
                }
            }
        }
    }

    return false;
#else
    return false;
#endif
}

void SelectionService::NotifySelectionDetected(const SelectionContext& ctx) {
    if (!m_aliveToken || !m_aliveToken->load()) {
        return;
    }

    auto aliveToken = m_aliveToken;
    if (wxTheApp) {
        wxTheApp->CallAfter([this, aliveToken, ctx]() {
            if (!aliveToken->load()) {
                return;
            }
            SelectionDetectedCallback cb;
            {
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                cb = m_callback;
            }
            if (cb) {
                LOG_INFO("SelectionService", "Trigger selection callback at (" + std::to_string(ctx.endX) + ", " + std::to_string(ctx.endY) + ")");
                cb(ctx.endX, ctx.endY, ctx);
            }
        });
    }
}

void SelectionService::CheckAndNotifyIfTextSelectedAsync(const SelectionContext& ctx) {
    if (!m_aliveToken || !m_aliveToken->load()) {
        return;
    }

    auto aliveToken = m_aliveToken;

    std::thread([this, aliveToken, ctx]() {
        // 微延时 30ms 避开目标应用响应 MouseUp 与选区高亮渲染的时间差
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        if (!aliveToken->load()) {
            return;
        }

        std::string text;
        int anchorX = ctx.endX;
        int anchorY = ctx.endY;

        // 1. 仅通过 Windows UIA 或 macOS AXUIElement 进行非侵入式选区预检（绝不发送复制快捷键，不触碰剪贴板）
        bool detected = ScreenTextExtractor::ExtractViaUIAutomation(ctx.endX, ctx.endY, text, anchorX, anchorY);
        LOG_INFO("SelectionService", "ExtractViaUIAutomation result: " + std::to_string(detected) + ", text: " + text);
        if (!detected || text.empty()) {
            // 极短微重试 (25ms) 应对极少数 XAML / Webview / 终端组件 MouseUp 瞬间可访问性树更新的短暂延迟
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            if (!aliveToken->load()) {
                return;
            }
            detected = ScreenTextExtractor::ExtractViaUIAutomation(ctx.endX, ctx.endY, text, anchorX, anchorY);
        }

        // 2. 纯非侵入式手势放行（针对 VS Code、Cursor、WPS Office、Sublime、JetBrains、各类终端等无障碍受限应用）
        //    绝不发送任何复制快捷键，绝不触碰或污染剪贴板！
        //    仅当目标应用属于已知自绘编辑器，或者用户做出了明确的强意图选词手势（双击选词 clickCount >= 2，或明确拖拽）时，
        //    信任用户的手势意图，在光标旁静默弹出悬浮图标。真正的复制提取严格延后到用户“主动点击悬浮图标”时才按需触发。
        if (!detected || text.empty()) {
            bool isNonAxTarget = false;
#if defined(__APPLE__)
            @autoreleasepool {
                NSRunningApplication* frontApp = [NSRunningApplication runningApplicationWithProcessIdentifier:ctx.targetPid];
                if (!frontApp) {
                    frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
                }
                if (frontApp) {
                    NSString* bid = [[frontApp bundleIdentifier] lowercaseString];
                    NSString* name = [[frontApp localizedName] lowercaseString];
                    if (bid && ([bid containsString:@"code"] || [bid containsString:@"wps"] || [bid containsString:@"sublime"] || [bid containsString:@"terminal"] || [bid containsString:@"iterm"] ||
                                [bid containsString:@"jetbrains"] || [bid containsString:@"antigravity"] || [bid containsString:@"cursor"] || [bid containsString:@"idea"] ||
                                [bid containsString:@"clion"] || [bid containsString:@"pycharm"] || [bid containsString:@"webstorm"])) {
                        isNonAxTarget = true;
                    }
                    if (!isNonAxTarget && name &&
                        ([name containsString:@"code"] || [name containsString:@"wps"] || [name containsString:@"terminal"] || [name containsString:@"iterm"] || [name containsString:@"sublime"] ||
                         [name containsString:@"antigravity"])) {
                        isNonAxTarget = true;
                    }
                }
            }
#elif defined(_WIN32)
            if (ctx.targetHwnd) {
                DWORD pid = 0;
                GetWindowThreadProcessId(ctx.targetHwnd, &pid);
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                if (hProc) {
                    wchar_t path[MAX_PATH] = {0};
                    DWORD sz = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProc, 0, path, &sz)) {
                        std::wstring exe(path);
                        for (auto& c : exe)
                            c = towlower(c);
                        if (exe.find(L"code.exe") != std::wstring::npos || exe.find(L"wps.exe") != std::wstring::npos || exe.find(L"sublime_text.exe") != std::wstring::npos ||
                            exe.find(L"windowsterminal.exe") != std::wstring::npos || exe.find(L"idea64.exe") != std::wstring::npos || exe.find(L"clion64.exe") != std::wstring::npos) {
                            isNonAxTarget = true;
                        }
                    }
                    CloseHandle(hProc);
                }
            }
#endif
            if (isNonAxTarget) {
                detected = true;
                text.clear(); // 纯非侵入：预检阶段绝不发送按键，不触碰剪贴板
                anchorX = ctx.endX;
                anchorY = ctx.endY;
                LOG_INFO("SelectionService", "Optimistic non-intrusive gesture trigger for Non-AX App (zero keys sent).");
            }
        }

        if (detected) {
            if (!aliveToken->load()) {
                return;
            }
            SelectionContext validCtx = ctx;
            validCtx.endX = anchorX;
            validCtx.endY = anchorY;
            validCtx.preExtractedText = std::move(text);
            NotifySelectionDetected(validCtx);
        }
    }).detach();
}

void SelectionService::ExtractSelectionAsync(const SelectionContext& ctx, std::function<void(const std::string& text)> onComplete) {
    if (!m_aliveToken || !m_aliveToken->load()) {
        return;
    }

    auto aliveToken = m_aliveToken;

    // 1. 若预检阶段已通过 UIA/AX 获取到选中文本，直接回调，0ms 秒开，免去再次激活窗口及复制开销
    if (!ctx.preExtractedText.empty()) {
        if (wxTheApp) {
            wxTheApp->CallAfter([aliveToken, text = ctx.preExtractedText, onComplete = std::move(onComplete)]() {
                if (aliveToken->load() && onComplete) {
                    onComplete(text);
                }
            });
        }
        return;
    }

    bool preserve = m_preserveClipboard.load();

    std::thread([this, aliveToken, ctx, preserve, onComplete = std::move(onComplete)]() {
    // 确保原宿主窗口处于激活前台，以保证模拟按键或 UI Automation 能够正确定位
#ifdef _WIN32
        if (ctx.targetHwnd && IsWindow(ctx.targetHwnd)) {
            if (GetForegroundWindow() != ctx.targetHwnd) {
                SetForegroundWindow(ctx.targetHwnd);
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
        }
#elif defined(__APPLE__)
        if (ctx.targetPid > 0) {
            @autoreleasepool {
                NSRunningApplication* targetApp = [NSRunningApplication runningApplicationWithProcessIdentifier:ctx.targetPid];
                if (targetApp && ![targetApp isActive]) {
                    if (@available(macOS 14.0, *)) {
                        [[NSApplication sharedApplication] yieldActivationToApplication:targetApp];
                    }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                    [targetApp activateWithOptions:NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps];
#pragma clang diagnostic pop
                    for (int i = 0; i < 20; ++i) {
                        if ([targetApp isActive])
                            break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(40));
                }
            }
        }
#endif

        if (!aliveToken->load()) {
            return;
        }

        ExtractedSelection extracted = ScreenTextExtractor::ExtractSelection(ctx.startX, ctx.startY, ctx.endX, ctx.endY, preserve);

        if (!aliveToken->load()) {
            return;
        }

        LOG_INFO("SelectionService", "Extracted text on button click: [Source=" + extracted.source + ", length=" + std::to_string(extracted.text.size()) + ", text=\"" + extracted.text + "\"]");

        if (extracted.text.empty() || extracted.text.size() > 8000) {
            return;
        }

        if (wxTheApp && aliveToken->load()) {
            wxTheApp->CallAfter([aliveToken, text = extracted.text, onComplete]() {
                if (!aliveToken->load()) {
                    return;
                }
                if (onComplete) {
                    onComplete(text);
                }
            });
        }
    }).detach();
}
} // namespace LinguaAlpaca
