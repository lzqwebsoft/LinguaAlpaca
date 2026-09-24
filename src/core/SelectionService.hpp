#pragma once
#pragma execution_character_set("utf-8")

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <vector>
#include "Config.hpp"
#include "SelectionContext.hpp"

class wxWindow;

namespace LinguaAlpaca {

// 划词触发后的轻量回调函数 (屏幕坐标 X, Y, 划词上下文)
using SelectionDetectedCallback = std::function<void(int screenX, int screenY, const SelectionContext& ctx)>;

class SelectionService {
public:
    explicit SelectionService(std::shared_ptr<ConfigManager> configManager);
    ~SelectionService();

    SelectionService(const SelectionService&) = delete;
    SelectionService& operator=(const SelectionService&) = delete;

    // 静态访问全局活跃服务实例
    static SelectionService* GetActiveService();

    // 注册 / 注销允许在自身进程内划词的白名单窗口 (例如 DictView 释义卡片)
    void RegisterAllowedWindow(wxWindow* window);
    void UnregisterAllowedWindow(wxWindow* window);
    bool IsInsideAllowedWindow(void* nativeHwnd, int screenX, int screenY) const;
    bool GetAllowedWindowSelection(void* nativeHwnd, int screenX, int screenY, std::string& outText) const;

    // 启动全局鼠标监听
    bool Start();

    // 停止全局鼠标监听
    void Stop();

    // 检查当前是否在运行
    bool IsRunning() const { return m_isRunning.load(); }

    // 权限与平台辅助方法
    static bool IsAccessibilityGranted();
    static bool OpenAccessibilitySettings();
    static void RestartApplication();

    // 检查是否需要重启应用以完全应用辅助功能权限 (针对 Ad-hoc 签名与运行时后赋权场景)
    bool NeedsRestartForAccessibility() const;

    // 尝试在运行时热恢复 CGEventTap (若用户授权后无需重启即可生效)
    bool TryRecoverEventTap();

    // 注册划词手势触发回调 (单纯显示悬浮按钮)
    void SetCallback(SelectionDetectedCallback callback);

    // 异步提取划词文本 (当用户主动点击悬浮按钮后调用)
    void ExtractSelectionAsync(
        const SelectionContext& ctx,
        std::function<void(const std::string& text)> onComplete
    );

    // 动态同步最新配置
    void ApplyConfig(const AppConfig& config);

    // 内部钩子处理函数（Win32 静态转接）
    void OnLowLevelMouseEvent(int message, int x, int y);

    // 划词触发通知分发函数（公开以支持单元测试与显式注入验证）
    void NotifySelectionDetected(const SelectionContext& ctx);

private:
    void CheckAndNotifyIfTextSelectedAsync(const SelectionContext& ctx);

    // 检查当前的鼠标操作是否应被忽略（如自身窗口、拖拽窗口标题栏、滑动滚动条、调节窗体尺寸等非文本选中操作）
    bool ShouldIgnoreMouseEvent(int startX, int startY, int endX, int endY) const;

    std::shared_ptr<ConfigManager> m_configManager;
    std::atomic<bool> m_isRunning{false};
    std::atomic<bool> m_hadPermissionAtStartup{false};

    // 配置缓存（线程安全读取）
    std::atomic<bool> m_enabled{true};
    std::atomic<int> m_triggerMode{0};    // 0: 直接划词, 1: 划词+辅助按键, 2: 双击/三击
    std::atomic<int> m_modifierKey{0};    // 0: Ctrl, 1: Alt, 2: Shift
    std::atomic<bool> m_preserveClipboard{true};

    // 鼠标状态跟踪
    int m_ptDownX{0};
    int m_ptDownY{0};
    std::chrono::steady_clock::time_point m_timeDown{};

    // 连击检测
    int m_lastClickX{0};
    int m_lastClickY{0};
    std::chrono::steady_clock::time_point m_lastClickTime{};
    int m_clickCount{0};

    std::mutex m_callbackMutex;
    SelectionDetectedCallback m_callback;

    std::shared_ptr<std::atomic<bool>> m_aliveToken;
    void* m_hookHandle{nullptr};
#if defined(__APPLE__)
    void* m_localHookHandle{nullptr};
    void* m_eventTap{nullptr};
    void* m_runLoopSource{nullptr};
public:
    void* GetEventTap() const { return m_eventTap; }
private:
#endif

    mutable std::mutex m_allowedWindowsMutex;
    std::vector<wxWindow*> m_allowedWindows;
};

} // namespace LinguaAlpaca
