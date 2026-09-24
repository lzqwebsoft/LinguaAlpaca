#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif
#include "SelectionService.hpp"
#include "ClipboardHelper.hpp"
#include "ScreenTextExtractor.hpp"
#include "Logger.hpp"

#include <wx/app.h>
#include <wx/toplevel.h>
#include <wx/window.h>
#include <wx/textctrl.h>
#include <iostream>
#include <thread>
#include <cmath>
#include <algorithm>

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
#include <richedit.h>
#elif defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#include <unistd.h>
#include <mach-o/dyld.h>
#endif

namespace LinguaAlpaca {

namespace {
SelectionService* g_activeService = nullptr;
}

#ifdef _WIN32
namespace {
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

SelectionService* SelectionService::GetActiveService() {
    return g_activeService;
}

void SelectionService::RegisterAllowedWindow(wxWindow* window) {
    if (!window)
        return;
    std::lock_guard<std::mutex> lock(m_allowedWindowsMutex);
    for (auto* w : m_allowedWindows) {
        if (w == window)
            return;
    }
    m_allowedWindows.push_back(window);
}

void SelectionService::UnregisterAllowedWindow(wxWindow* window) {
    if (!window)
        return;
    std::lock_guard<std::mutex> lock(m_allowedWindowsMutex);
    auto it = std::remove(m_allowedWindows.begin(), m_allowedWindows.end(), window);
    if (it != m_allowedWindows.end()) {
        m_allowedWindows.erase(it, m_allowedWindows.end());
    }
}

bool SelectionService::IsInsideAllowedWindow(void* nativeHwnd, int screenX, int screenY) const {
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(m_allowedWindowsMutex);
    for (wxWindow* win : m_allowedWindows) {
        if (!win)
            continue;
        HWND winHwnd = reinterpret_cast<HWND>(win->GetHWND());
        if (!winHwnd || !::IsWindow(winHwnd) || !::IsWindowVisible(winHwnd)) {
            continue;
        }
        HWND targetHwnd = reinterpret_cast<HWND>(nativeHwnd);
        if (targetHwnd && (targetHwnd == winHwnd || ::IsChild(winHwnd, targetHwnd))) {
            return true;
        }
        HWND pointHwnd = WindowFromPoint(POINT{screenX, screenY});
        if (pointHwnd && (pointHwnd == winHwnd || ::IsChild(winHwnd, pointHwnd))) {
            return true;
        }
        RECT rc;
        if (::GetWindowRect(winHwnd, &rc)) {
            POINT pt = {screenX, screenY};
            if (::PtInRect(&rc, pt)) {
                return true;
            }
        }
    }
    return false;
#elif defined(__APPLE__)
    (void)nativeHwnd;
    std::vector<wxWindow*> windowsCopy;
    {
        std::lock_guard<std::mutex> lock(m_allowedWindowsMutex);
        windowsCopy = m_allowedWindows;
    }
    if (windowsCopy.empty()) {
        return false;
    }

    bool inside = false;
    auto checkInside = [&]() {
        for (wxWindow* win : windowsCopy) {
            if (!win)
                continue;
            if (win->IsShown() || win->IsShownOnScreen()) {
                wxRect r = win->GetScreenRect();
                if (r.Inflate(24, 24).Contains(screenX, screenY)) {
                    inside = true;
                    return;
                }
            }
        }
    };

    if ([NSThread isMainThread]) {
        checkInside();
    } else {
        dispatch_sync(dispatch_get_main_queue(), ^{
            checkInside();
        });
    }
    return inside;
#else
    (void)nativeHwnd;
    std::lock_guard<std::mutex> lock(m_allowedWindowsMutex);
    for (wxWindow* win : m_allowedWindows) {
        if (!win)
            continue;
        if (win->IsShown()) {
            wxRect r = win->GetScreenRect();
            if (r.Contains(screenX, screenY)) {
                return true;
            }
        }
    }
    return false;
#endif
}

#if defined(__APPLE__)
static wxTextCtrl* FindChildTextCtrl(wxWindow* win) {
    if (!win)
        return nullptr;
    if (auto* tc = dynamic_cast<wxTextCtrl*>(win)) {
        return tc;
    }
    const wxWindowList& children = win->GetChildren();
    for (wxWindowList::compatibility_iterator node = children.GetFirst(); node; node = node->GetNext()) {
        if (auto* tc = FindChildTextCtrl(node->GetData())) {
            return tc;
        }
    }
    return nullptr;
}
#endif

bool SelectionService::GetAllowedWindowSelection(void* nativeHwnd, int screenX, int screenY, std::string& outText) const {
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(m_allowedWindowsMutex);
    for (wxWindow* win : m_allowedWindows) {
        if (!win)
            continue;
        HWND winHwnd = reinterpret_cast<HWND>(win->GetHWND());
        if (!winHwnd || !::IsWindow(winHwnd) || !::IsWindowVisible(winHwnd)) {
            continue;
        }
        HWND targetHwnd = reinterpret_cast<HWND>(nativeHwnd);
        HWND pointHwnd = WindowFromPoint(POINT{screenX, screenY});

        bool isTargetMatched = (targetHwnd && (targetHwnd == winHwnd || ::IsChild(winHwnd, targetHwnd)));
        bool isPointMatched = (pointHwnd && (pointHwnd == winHwnd || ::IsChild(winHwnd, pointHwnd)));

        if (!isTargetMatched && !isPointMatched) {
            RECT rc;
            if (::GetWindowRect(winHwnd, &rc)) {
                POINT pt = {screenX, screenY};
                if (!::PtInRect(&rc, pt)) {
                    continue;
                }
            } else {
                continue;
            }
        }

        // 优先在释放点所在子窗口或目标子窗口上查询 RichEdit/Edit 选区
        HWND candidateHwnds[] = {pointHwnd, targetHwnd, winHwnd};
        for (HWND h : candidateHwnds) {
            if (!h || !::IsWindow(h))
                continue;
            DWORD selStart = 0, selEnd = 0;
            ::SendMessageW(h, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
            if (selEnd > selStart && (selEnd - selStart) <= 8000) {
                DWORD len = selEnd - selStart;
                std::vector<wchar_t> wbuf(len + 2, 0);
                LRESULT copied = ::SendMessageW(h, EM_GETSELTEXT, 0, reinterpret_cast<LPARAM>(wbuf.data()));
                if (copied > 0 && wbuf[0] != L'\0') {
                    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), static_cast<int>(copied), nullptr, 0, nullptr, nullptr);
                    if (utf8Len > 0) {
                        outText.resize(utf8Len);
                        WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), static_cast<int>(copied), &outText[0], utf8Len, nullptr, nullptr);
                        return true;
                    }
                } else {
                    // 标准 Edit 控件不支持 EM_GETSELTEXT，使用 WM_GETTEXT 提取选区
                    int fullLen = ::GetWindowTextLengthW(h);
                    if (fullLen > 0 && fullLen <= 32768) {
                        std::vector<wchar_t> fullBuf(fullLen + 1, 0);
                        if (::GetWindowTextW(h, fullBuf.data(), fullLen + 1) > 0) {
                            if (selStart < static_cast<DWORD>(fullBuf.size()) && selEnd <= static_cast<DWORD>(fullBuf.size())) {
                                std::wstring sub(fullBuf.data() + selStart, fullBuf.data() + selEnd);
                                int utf8Len = WideCharToMultiByte(CP_UTF8, 0, sub.data(), static_cast<int>(sub.size()), nullptr, 0, nullptr, nullptr);
                                if (utf8Len > 0) {
                                    outText.resize(utf8Len);
                                    WideCharToMultiByte(CP_UTF8, 0, sub.data(), static_cast<int>(sub.size()), &outText[0], utf8Len, nullptr, nullptr);
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
        }
        if (auto* tc = dynamic_cast<wxTextCtrl*>(win)) {
            wxString sel = tc->GetStringSelection();
            if (!sel.IsEmpty()) {
                outText = sel.ToUTF8().data();
                return true;
            }
        }
    }
    return false;
#elif defined(__APPLE__)
    (void)nativeHwnd;
    std::vector<wxWindow*> windowsCopy;
    {
        std::lock_guard<std::mutex> lock(m_allowedWindowsMutex);
        windowsCopy = m_allowedWindows;
    }
    if (windowsCopy.empty()) {
        return false;
    }

    bool found = false;
    auto querySelection = [&]() {
        for (wxWindow* win : windowsCopy) {
            if (!win)
                continue;
            if (!win->IsShown() && !win->IsShownOnScreen())
                continue;
            wxRect r = win->GetScreenRect();
            if (!r.Inflate(24, 24).Contains(screenX, screenY))
                continue;

            // 递归查找子控件中的 wxTextCtrl
            if (auto* textCtrl = FindChildTextCtrl(win)) {
                wxString sel = textCtrl->GetStringSelection();
                if (!sel.IsEmpty()) {
                    outText = sel.ToUTF8().data();
                    found = true;
                    return;
                }
            }
        }
    };

    if ([NSThread isMainThread]) {
        querySelection();
    } else {
        dispatch_sync(dispatch_get_main_queue(), ^{
            querySelection();
        });
    }
    return found;
#else
    (void)nativeHwnd;
    (void)screenX;
    (void)screenY;
    (void)outText;
    return false;
#endif
}

SelectionService::SelectionService(std::shared_ptr<ConfigManager> configManager)
    : m_configManager(std::move(configManager))
    , m_aliveToken(std::make_shared<std::atomic<bool>>(true)) {
    g_activeService = this;
    if (m_configManager) {
        ApplyConfig(m_configManager->GetConfig());
    }
}

SelectionService::~SelectionService() {
    Stop();
    if (g_activeService == this) {
        g_activeService = nullptr;
    }
}

#if defined(__APPLE__)
static CGEventRef SelectionCGEventTapCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void* refcon) {
    (void)proxy;
    auto* svc = static_cast<SelectionService*>(refcon);
    if (!svc) {
        return event;
    }

    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (svc->GetEventTap()) {
            CGEventTapEnable(static_cast<CFMachPortRef>(svc->GetEventTap()), true);
        }
        return event;
    }

    if (!svc->IsRunning()) {
        return event;
    }

    CGPoint pt = CGEventGetLocation(event);
    int x = static_cast<int>(std::round(pt.x));
    int y = static_cast<int>(std::round(pt.y));

    if (type == kCGEventLeftMouseDown) {
        svc->OnLowLevelMouseEvent(WM_LBUTTONDOWN, x, y);
    } else if (type == kCGEventLeftMouseUp) {
        svc->OnLowLevelMouseEvent(WM_LBUTTONUP, x, y);
    }

    return event;
}
#endif

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
        bool trusted = IsAccessibilityGranted();
        m_hadPermissionAtStartup.store(trusted);
        if (!trusted) {
            LOG_WARN("SelectionService", "macOS Accessibility permission not granted yet; prompted user via system dialog.");
            NSDictionary* options = @{(__bridge id)kAXTrustedCheckOptionPrompt: @YES};
            AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
        }

        // 1. 优先使用系统底层 CGEventTap (在 WindowServer 级别监听鼠标事件)
        //    彻底解决 NSTextView / wxTextCtrl 内部 modal drag tracking loop 吞噬 LeftMouseUp 导致划词失效的问题
        CGEventMask tapMask = (CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventLeftMouseUp));
        CFMachPortRef eventTap = CGEventTapCreate(
            kCGSessionEventTap,
            kCGHeadInsertEventTap,
            kCGEventTapOptionListenOnly,
            tapMask,
            SelectionCGEventTapCallback,
            this
        );

        if (eventTap) {
            CFRunLoopSourceRef runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0);
            if (runLoopSource) {
                CFRunLoopAddSource(CFRunLoopGetMain(), runLoopSource, kCFRunLoopCommonModes);
                CGEventTapEnable(eventTap, true);
                m_eventTap = static_cast<void*>(eventTap);
                m_runLoopSource = static_cast<void*>(runLoopSource);
                m_isRunning.store(true);
                LOG_INFO("SelectionService", "macOS CGEventTap mouse monitor started successfully.");
                return true;
            }
            CFRelease(eventTap);
        }

        LOG_WARN("SelectionService", "CGEventTapCreate unavailable, falling back to NSEvent global & local monitors.");

        // 2. 备用兜底：若 CGEventTap 创建失败，回退到 NSEvent 监听器
        NSEventMask mask = NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp;
        id monitor = [NSEvent addGlobalMonitorForEventsMatchingMask:mask
                                                            handler:^(NSEvent* event) {
                                                                if (!m_isRunning.load()) {
                                                                    return;
                                                                }
                                                                CGPoint pt = [event CGEvent] ? CGEventGetLocation([event CGEvent]) : CGPointZero;
                                                                if (CGPointEqualToPoint(pt, CGPointZero)) {
                                                                    NSPoint loc = [NSEvent mouseLocation];
                                                                    NSScreen* primary = [NSScreen screens].firstObject;
                                                                    CGFloat screenH = primary ? primary.frame.size.height : 0;
                                                                    pt = CGPointMake(loc.x, screenH - loc.y);
                                                                }
                                                                int x = static_cast<int>(std::round(pt.x));
                                                                int y = static_cast<int>(std::round(pt.y));

                                                                NSEventType type = [event type];
                                                                if (type == NSEventTypeLeftMouseDown) {
                                                                    OnLowLevelMouseEvent(WM_LBUTTONDOWN, x, y);
                                                                } else if (type == NSEventTypeLeftMouseUp) {
                                                                    OnLowLevelMouseEvent(WM_LBUTTONUP, x, y);
                                                                }
                                                            }];

        id localMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask
                                                                handler:^NSEvent*(NSEvent* event) {
                                                                    if (!m_isRunning.load()) {
                                                                        return event;
                                                                    }
                                                                    CGPoint pt = [event CGEvent] ? CGEventGetLocation([event CGEvent]) : CGPointZero;
                                                                    if (CGPointEqualToPoint(pt, CGPointZero)) {
                                                                        NSPoint loc = [NSEvent mouseLocation];
                                                                        NSScreen* primary = [NSScreen screens].firstObject;
                                                                        CGFloat screenH = primary ? primary.frame.size.height : 0;
                                                                        pt = CGPointMake(loc.x, screenH - loc.y);
                                                                    }
                                                                    int x = static_cast<int>(std::round(pt.x));
                                                                    int y = static_cast<int>(std::round(pt.y));

                                                                    NSEventType type = [event type];
                                                                    if (type == NSEventTypeLeftMouseDown) {
                                                                        OnLowLevelMouseEvent(WM_LBUTTONDOWN, x, y);
                                                                    } else if (type == NSEventTypeLeftMouseUp) {
                                                                        OnLowLevelMouseEvent(WM_LBUTTONUP, x, y);
                                                                    }
                                                                    return event;
                                                                }];

        if (!monitor && !localMonitor) {
            LOG_ERROR("SelectionService", "Failed to install macOS mouse monitors.");
            return false;
        }

        if (monitor) {
            m_hookHandle = (void*)[monitor retain];
        }
        if (localMonitor) {
            m_localHookHandle = (void*)[localMonitor retain];
        }
        m_isRunning.store(true);
        LOG_INFO("SelectionService", "macOS global & local mouse monitors started successfully.");
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
    LOG_INFO("SelectionService", "Global mouse hook stopped.");
#elif defined(__APPLE__)
    if (m_eventTap) {
        auto tap = static_cast<CFMachPortRef>(m_eventTap);
        CGEventTapEnable(tap, false);
        if (m_runLoopSource) {
            auto src = static_cast<CFRunLoopSourceRef>(m_runLoopSource);
            CFRunLoopRemoveSource(CFRunLoopGetMain(), src, kCFRunLoopCommonModes);
            CFRelease(src);
            m_runLoopSource = nullptr;
        }
        CFRelease(tap);
        m_eventTap = nullptr;
        LOG_INFO("SelectionService", "macOS CGEventTap stopped.");
    }
    if (m_hookHandle) {
        id monitor = (id)m_hookHandle;
        [NSEvent removeMonitor:monitor];
        [monitor release];
        m_hookHandle = nullptr;
    }
    if (m_localHookHandle) {
        id localMon = (id)m_localHookHandle;
        [NSEvent removeMonitor:localMon];
        [localMon release];
        m_localHookHandle = nullptr;
    }
    LOG_INFO("SelectionService", "macOS global & local mouse monitors stopped.");
#endif
    if (g_activeService == this) {
        g_activeService = nullptr;
    }
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
        LOG_DEBUG("SelectionService", "LBUTTONDOWN at (" + std::to_string(x) + ", " + std::to_string(y) + "), clickCount=" + std::to_string(m_clickCount));
        return;
    }

    if (message == WM_LBUTTONUP) {
        bool ignored = ShouldIgnoreMouseEvent(m_ptDownX, m_ptDownY, x, y);
        int dx = x - m_ptDownX;
        int dy = y - m_ptDownY;
        int distSq = dx * dx + dy * dy;
        auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_timeDown).count();

        LOG_DEBUG("SelectionService", "LBUTTONUP at (" + std::to_string(x) + ", " + std::to_string(y) +
                  "), down=(" + std::to_string(m_ptDownX) + ", " + std::to_string(m_ptDownY) +
                  "), distSq=" + std::to_string(distSq) + ", durationMs=" + std::to_string(durationMs) +
                  ", clickCount=" + std::to_string(m_clickCount) + ", ignored=" + std::to_string(ignored));

        // 综合过滤检测：如果操作发生在本项目自身窗口、或属于拖动标题栏/滑动滑条/调整窗口大小等非文本选中操作，则忽略
        if (ignored) {
            return;
        }

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
                const char* prog = getprogname();
                if (!prog || !strstr(prog, "unit_tests")) {
                    NSRunningApplication* frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
                    if (frontApp) {
                        ctx.targetPid = [frontApp processIdentifier];
                    }
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

    POINT ptUp = {endX, endY};
    HWND hwndUp = WindowFromPoint(ptUp);

    POINT ptDown = {startX, startY};
    HWND hwndDown = WindowFromPoint(ptDown);

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

    // 优先检查是否在允许划词的自身白名单窗口内部（例如 DictView 释义卡片）
    bool isAllowedSelf = IsInsideAllowedWindow(reinterpret_cast<void*>(hwndUp), endX, endY) &&
                         IsInsideAllowedWindow(reinterpret_cast<void*>(hwndDown), startX, startY);

    if (isAllowedSelf) {
        // 自选区域在白名单控件内，跳过进程 ID 拦截与前台窗口拦截，仅检查是否误触非客户区（如滚动条）
        if (checkNcHit(hwndDown, startX, startY) || checkNcHit(hwndUp, endX, endY)) {
            return true;
        }
        return false;
    }

    // 1. 检查鼠标释放点所在的窗体
    if (IsIgnoredOrScreenshotWindow(hwndUp, currentPid)) {
        return true;
    }

    // 2. 检查鼠标按起点所在的窗体
    if (IsIgnoredOrScreenshotWindow(hwndDown, currentPid)) {
        return true;
    }

    // 3. 检查当前前景激活窗体
    HWND hwndForeground = GetForegroundWindow();
    if (IsIgnoredOrScreenshotWindow(hwndForeground, currentPid)) {
        return true;
    }

    // 4. 检查非客户区操作（如拖拽标题栏移动窗体、滑动滚动条、拖拉边框调整大小等与文本选中无关的操作）
    if (checkNcHit(hwndDown, startX, startY) || checkNcHit(hwndUp, endX, endY)) {
        return true;
    }

    return false;
#elif defined(__APPLE__)
    bool isAllowedSelf = IsInsideAllowedWindow(nullptr, endX, endY) ||
                         IsInsideAllowedWindow(nullptr, startX, startY);
    if (!isAllowedSelf) {
        // 1. 检查是否在 LinguaAlpaca 自身的主窗口、悬浮图标或气泡等顶层窗口内
        wxWindowList& windows = wxTopLevelWindows;
        for (wxWindowList::compatibility_iterator node = windows.GetFirst(); node; node = node->GetNext()) {
            wxWindow* win = node->GetData();
            if (win && win->IsShown()) {
                wxRect r = win->GetScreenRect();
                if (r.Contains(startX, startY) || r.Contains(endX, endY)) {
                    LOG_DEBUG("SelectionService", "ShouldIgnoreMouseEvent: ignored because inside own TLW rect and not in allowed window");
                    return true;
                }
            }
        }

        // 2. 检查当前前台激活的应用是否属于本项目进程或屏幕截图/录屏工具
        @autoreleasepool {
            NSRunningApplication* frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
            if (frontApp) {
                if (frontApp.processIdentifier == getpid()) {
                    LOG_DEBUG("SelectionService", "ShouldIgnoreMouseEvent: ignored because frontmost application is self and not in allowed window");
                    return true;
                }
                NSString* bundleId = [frontApp bundleIdentifier];
                if (bundleId) {
                    if ([bundleId containsString:@"screencapture"] || [bundleId containsString:@"Snipaste"] || [bundleId containsString:@"CleanShot"] || [bundleId containsString:@"Shottr"] ||
                        [bundleId containsString:@"Flameshot"] || [bundleId containsString:@"Kap"]) {
                        LOG_DEBUG("SelectionService", "ShouldIgnoreMouseEvent: ignored because screenshot tool active");
                        return true;
                    }
                }
                NSString* appName = [frontApp localizedName];
                if (appName) {
                    if ([appName containsString:@"截图"] || [appName containsString:@"截屏"]) {
                        LOG_DEBUG("SelectionService", "ShouldIgnoreMouseEvent: ignored because screenshot tool active");
                        return true;
                    }
                }
            }
        }
    } else {
        LOG_DEBUG("SelectionService", "ShouldIgnoreMouseEvent: permitted inside allowed self window!");
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

        // 1.1 若 UIA 未命中，检查是否属于自身白名单窗口，尝试直接从 Win32/RichEdit 控件直接提取选区
        if (!detected || text.empty()) {
            if (!aliveToken->load()) {
                return;
            }
            if (GetAllowedWindowSelection(ctx.targetHwnd, ctx.endX, ctx.endY, text)) {
                if (!text.empty()) {
                    detected = true;
                    LOG_INFO("SelectionService", "Extracted text directly from allowed window: \"" + text + "\"");
                }
            }
        }

        // 2. 纯非侵入式手势放行（针对 Chrome/Safari/Edge等浏览器、VS Code/Cursor等自绘编辑器、各类终端与办公软件、以及自身白名单窗口）
        //    绝不发送任何复制快捷键，绝不触碰或污染剪贴板！
        //    当目标应用属于已知受限应用，或者用户做出了明确的强意图选词手势（双击选词 clickCount >= 2，或拖拽划词）时，
        //    信任用户的手势意图，在光标旁静默弹出悬浮图标。真正的复制提取严格延后到用户“主动点击悬浮图标”时才按需触发。
        if (!detected || text.empty()) {
            if (!aliveToken->load()) {
                return;
            }
            bool isNonAxTarget = false;
            if (IsInsideAllowedWindow(ctx.targetHwnd, ctx.endX, ctx.endY)) {
                isNonAxTarget = true;
            }
#if defined(__APPLE__)
            @autoreleasepool {
                const char* prog = getprogname();
                bool isUnitTest = prog && strstr(prog, "unit_tests");
                NSRunningApplication* frontApp = nullptr;
                if (ctx.targetPid > 0) {
                    frontApp = [NSRunningApplication runningApplicationWithProcessIdentifier:ctx.targetPid];
                }
                if (!frontApp && !isUnitTest) {
                    frontApp = [[NSWorkspace sharedWorkspace] frontmostApplication];
                }
                if (frontApp) {
                    NSString* bid = [[frontApp bundleIdentifier] lowercaseString];
                    NSString* name = [[frontApp localizedName] lowercaseString];
                    // 1. 主流浏览器（Chrome、Safari、Edge、Firefox、Arc、Brave、Opera、各类国产浏览器等）
                    // 2. 自绘编辑器与终端（VS Code、Sublime、Terminal、iTerm、JetBrains、Cursor、Zed、Xcode 等）
                    // 3. 办公与阅读器（WPS、Office、Adobe Acrobat、PDF 阅读器、Notion、Obsidian、Typora 等）
                    // 4. 即时通讯（微信、QQ、钉钉、飞书、Telegram、Slack、Discord 等）
                    if (bid && ([bid containsString:@"chrome"] || [bid containsString:@"safari"] || [bid containsString:@"edge"] ||
                                [bid containsString:@"firefox"] || [bid containsString:@"arc"] || [bid containsString:@"browser"] ||
                                [bid containsString:@"opera"] || [bid containsString:@"brave"] || [bid containsString:@"code"] ||
                                [bid containsString:@"wps"] || [bid containsString:@"sublime"] || [bid containsString:@"terminal"] ||
                                [bid containsString:@"iterm"] || [bid containsString:@"jetbrains"] || [bid containsString:@"antigravity"] ||
                                [bid containsString:@"cursor"] || [bid containsString:@"idea"] || [bid containsString:@"clion"] ||
                                [bid containsString:@"pycharm"] || [bid containsString:@"webstorm"] || [bid containsString:@"goland"] ||
                                [bid containsString:@"zed"] || [bid containsString:@"adobe"] || [bid containsString:@"acrobat"] ||
                                [bid containsString:@"reader"] || [bid containsString:@"wechat"] || [bid containsString:@"xinwechat"] ||
                                [bid containsString:@"dingtalk"] || [bid containsString:@"feishu"] || [bid containsString:@"lark"] ||
                                [bid containsString:@"telegram"] || [bid containsString:@"slack"] || [bid containsString:@"notion"] ||
                                [bid containsString:@"obsidian"] || [bid containsString:@"typora"] || [bid containsString:@"preview"])) {
                        isNonAxTarget = true;
                    }
                    if (!isNonAxTarget && name &&
                        ([name containsString:@"chrome"] || [name containsString:@"safari"] || [name containsString:@"edge"] ||
                         [name containsString:@"firefox"] || [name containsString:@"arc"] || [name containsString:@"browser"] ||
                         [name containsString:@"code"] || [name containsString:@"wps"] || [name containsString:@"terminal"] ||
                         [name containsString:@"iterm"] || [name containsString:@"sublime"] || [name containsString:@"antigravity"] ||
                         [name containsString:@"acrobat"] || [name containsString:@"reader"] || [name containsString:@"微信"] ||
                         [name containsString:@"钉钉"] || [name containsString:@"飞书"] || [name containsString:@"预览"])) {
                        isNonAxTarget = true;
                    }
                }
            }
#elif defined(_WIN32)
            if (!isNonAxTarget) {
                if (ctx.targetHwnd && ClipboardHelper::IsNonAxTargetWindow(ctx.targetHwnd)) {
                    isNonAxTarget = true;
                } else {
                    POINT ptEnd = { ctx.endX, ctx.endY };
                    HWND hwndUnderMouse = WindowFromPoint(ptEnd);
                    if (hwndUnderMouse && ClipboardHelper::IsNonAxTargetWindow(hwndUnderMouse)) {
                        isNonAxTarget = true;
                    }
                }
            }
#endif
            // 对非 AX 目标应用（浏览器、各类自绘编辑器、终端、办公软件等）或自身白名单控件，放行手势意图
            if (isNonAxTarget) {
                detected = true;
                text.clear(); // 纯非侵入：预检阶段绝不发送按键，不触碰剪贴板
                anchorX = ctx.endX;
                anchorY = ctx.endY;
                LOG_INFO("SelectionService", "Optimistic non-intrusive gesture trigger for Non-AX/Browser target (zero keys sent, gesture confirmed).");
            }
        }

        if (detected) {
            if (!aliveToken->load()) {
                return;
            }
            SelectionContext validCtx = ctx;
            // 划词/选词时，悬浮图标弹出位置严格以鼠标最终弹起释放的物理坐标为基准 (ctx.endX, ctx.endY)
            // 确保浮动图标准确呈现在用户鼠标释放的光标位置，杜绝被任何跨行无障碍外接矩形偏移
            validCtx.endX = ctx.endX;
            validCtx.endY = ctx.endY;
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

    // 1. 若预检阶段已通过 UIA/AX 或白名单直读获取到选中文本，直接回调，0ms 秒开，免去再次激活窗口及复制开销
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

#if defined(__APPLE__)
    // 1. 在主线程立即将焦点与激活状态交还给原宿主应用
    if (ctx.targetPid > 0) {
        @autoreleasepool {
            NSRunningApplication* targetApp = [NSRunningApplication runningApplicationWithProcessIdentifier:ctx.targetPid];
            if (targetApp) {
                if (@available(macOS 14.0, *)) {
                    [[NSApplication sharedApplication] yieldActivationToApplication:targetApp];
                }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                [targetApp activateWithOptions:NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps];
#pragma clang diagnostic pop
            }
        }
    }
#endif

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
                if (targetApp) {
                    LOG_INFO("SelectionService", "Ensuring target app is active: PID=" + std::to_string(ctx.targetPid) +
                             " (" + std::string([[targetApp localizedName] UTF8String] ?: "") + "), isActive=" + std::to_string([targetApp isActive]));
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

        std::string directAllowedText;
        if (GetAllowedWindowSelection(ctx.targetHwnd, ctx.endX, ctx.endY, directAllowedText) && !directAllowedText.empty()) {
            ExtractedSelection extracted;
            extracted.text = std::move(directAllowedText);
            extracted.anchorX = ctx.endX;
            extracted.anchorY = ctx.endY;
            extracted.source = "AllowedWindow";
            LOG_INFO("SelectionService", "Extracted text on button click from allowed window: \"" + extracted.text + "\"");
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

bool SelectionService::IsAccessibilityGranted() {
#if defined(__APPLE__)
    @autoreleasepool {
        NSDictionary* options = @{(__bridge id)kAXTrustedCheckOptionPrompt: @NO};
        return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
    }
#else
    return true;
#endif
}

bool SelectionService::OpenAccessibilitySettings() {
#if defined(__APPLE__)
    @autoreleasepool {
        // 1. 弹出系统权限请求对话框 (若尚未记录)
        NSDictionary* options = @{(__bridge id)kAXTrustedCheckOptionPrompt: @YES};
        AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);

        // 2. 打开系统设置对应辅助功能页面
        NSURL* url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"];
        if (!url || ![[NSWorkspace sharedWorkspace] openURL:url]) {
            url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension"];
            if (url) {
                [[NSWorkspace sharedWorkspace] openURL:url];
            }
        }
        return true;
    }
#else
    return false;
#endif
}

void SelectionService::RestartApplication() {
    LOG_INFO("SelectionService", "RestartApplication requested, relaunching app...");
#if defined(__APPLE__)
    @autoreleasepool {
        NSString* bundlePath = [[NSBundle mainBundle] bundlePath];
        if (bundlePath && [bundlePath hasSuffix:@".app"]) {
            std::string path = [bundlePath UTF8String];
            std::string cmd = "sh -c 'sleep 0.3; open -n \"" + path + "\"' &";
            system(cmd.c_str());
        } else {
            char exePath[PATH_MAX] = {0};
            uint32_t size = sizeof(exePath);
            if (_NSGetExecutablePath(exePath, &size) == 0) {
                std::string path(exePath);
                std::string cmd = "sh -c 'sleep 0.3; \"" + path + "\"' &";
                system(cmd.c_str());
            }
        }
    }
#elif defined(_WIN32)
    wchar_t szPath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, szPath, MAX_PATH)) {
        std::wstring cmd = L"cmd /c timeout /t 1 /nobreak >nul & start \"\" \"" + std::wstring(szPath) + L"\"";
        _wsystem(cmd.c_str());
    }
#endif

    if (wxTheApp) {
        wxTheApp->CallAfter([]() {
            if (wxTheApp->GetTopWindow()) {
                wxTheApp->GetTopWindow()->Close(true);
            } else {
                wxTheApp->ExitMainLoop();
            }
        });
    }
}

bool SelectionService::NeedsRestartForAccessibility() const {
#if defined(__APPLE__)
    if (IsAccessibilityGranted()) {
        // 如果当前系统已授予辅助功能权限，但本次启动时未被允许（说明是当前运行期间被允许的），
        // 或者底层 CGEventTap 仍未能建立（Ad-hoc 签名导致 WindowServer 尚未刷新凭证），
        // 则需要重启应用以使权限完全生效
        if (!m_hadPermissionAtStartup.load() || m_eventTap == nullptr) {
            return true;
        }
    }
    return false;
#else
    return false;
#endif
}

bool SelectionService::TryRecoverEventTap() {
#if defined(__APPLE__)
    if (m_eventTap) {
        return true;
    }
    if (!IsAccessibilityGranted()) {
        return false;
    }
    CGEventMask tapMask = (CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventLeftMouseUp));
    CFMachPortRef eventTap = CGEventTapCreate(
        kCGSessionEventTap,
        kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly,
        tapMask,
        SelectionCGEventTapCallback,
        this
    );
    if (eventTap) {
        CFRunLoopSourceRef runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0);
        if (runLoopSource) {
            CFRunLoopAddSource(CFRunLoopGetMain(), runLoopSource, kCFRunLoopCommonModes);
            CGEventTapEnable(eventTap, true);
            m_eventTap = static_cast<void*>(eventTap);
            m_runLoopSource = static_cast<void*>(runLoopSource);
            m_isRunning.store(true);
            LOG_INFO("SelectionService", "macOS CGEventTap recovered dynamically on the fly!");
            return true;
        }
        CFRelease(eventTap);
    }
    return false;
#else
    return true;
#endif
}

} // namespace LinguaAlpaca
