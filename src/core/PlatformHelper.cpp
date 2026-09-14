#pragma execution_character_set("utf-8")
#include "PlatformHelper.hpp"
#include "Logger.hpp"

#ifdef __WXMSW__
#include <windows.h>
#elif defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#endif

namespace LinguaAlpaca {

#ifdef __WXMSW__
UINT PlatformHelper::GetSingleInstanceActivateMsg() {
    static const UINT s_msgActivate = ::RegisterWindowMessageW(L"LinguaAlpaca_SingleInstance_Activate");
    return s_msgActivate;
}
#endif

void PlatformHelper::ActivateExistingInstance() {
    LOG_INFO("PlatformHelper", "Attempting to activate existing running instance...");
#ifdef __WXMSW__
    // 1. 允许目标实例获取前台焦点（打破 Windows 前台窗口锁定保护机制）
    ::AllowSetForegroundWindow(ASFW_ANY);

    // 2. 广播系统注册消息，通知主窗体调用 RestoreAndFocus()
    UINT msg = GetSingleInstanceActivateMsg();
    if (msg != 0) {
        ::PostMessageW(HWND_BROADCAST, msg, 0, 0);
    }

    // 3. 备用兜底策略：直接查找主窗体句柄并恢复显示
    HWND hwnd = ::FindWindowW(nullptr, L"译灵驼 · LinguaAlpaca");
    if (hwnd) {
        if (::IsIconic(hwnd)) {
            ::ShowWindow(hwnd, SW_RESTORE);
        } else {
            ::ShowWindow(hwnd, SW_SHOW);
        }
        ::SetForegroundWindow(hwnd);
    }
#elif defined(__APPLE__)
    @autoreleasepool {
        NSString* bundleId = [[NSBundle mainBundle] bundleIdentifier];
        NSArray<NSRunningApplication*>* apps = nil;
        if (bundleId && [bundleId length] > 0) {
            apps = [NSRunningApplication runningApplicationsWithBundleIdentifier:bundleId];
        }
        pid_t currentPid = [[NSProcessInfo processInfo] processIdentifier];
        NSRunningApplication* targetApp = nil;
        if (apps && [apps count] > 0) {
            for (NSRunningApplication* app in apps) {
                if ([app processIdentifier] != currentPid) {
                    targetApp = app;
                    break;
                }
            }
        }
        if (!targetApp) {
            NSString* processName = [[NSProcessInfo processInfo] processName];
            for (NSRunningApplication* app in [[NSWorkspace sharedWorkspace] runningApplications]) {
                if ([[app localizedName] isEqualToString:processName] && [app processIdentifier] != currentPid) {
                    targetApp = app;
                    break;
                }
            }
        }

        if (targetApp) {
            // 1. 激活已运行的应用进程并展示所有窗口
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            [targetApp activateWithOptions:(NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps)];
#pragma clang diagnostic pop

            // 2. 发送原生 kAEReopenApplication AppleEvent，促使已运行实例触发 MacReopenApp()
            NSAppleEventDescriptor* targetDesc = [NSAppleEventDescriptor descriptorWithProcessIdentifier:[targetApp processIdentifier]];
            NSAppleEventDescriptor* appleEvent = [NSAppleEventDescriptor
                appleEventWithEventClass:kCoreEventClass
                                 eventID:kAEReopenApplication
                        targetDescriptor:targetDesc
                                returnID:kAutoGenerateReturnID
                           transactionID:kAnyTransactionID];
            [appleEvent sendEventWithOptions:NSAppleEventSendNoReply timeout:0.0 error:nil];
        }
    }
#endif
}

} // namespace LinguaAlpaca
