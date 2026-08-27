#pragma once
#pragma execution_character_set("utf-8")

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include "Config.hpp"

namespace LinguaAlpaca {

// 划词触发后的回调函数 (屏幕坐标 X, Y, 提取到的文本)
using SelectionDetectedCallback = std::function<void(int screenX, int screenY, const std::string& text)>;

class SelectionService {
public:
    explicit SelectionService(std::shared_ptr<ConfigManager> configManager);
    ~SelectionService();

    SelectionService(const SelectionService&) = delete;
    SelectionService& operator=(const SelectionService&) = delete;

    // 启动全局鼠标监听
    bool Start();

    // 停止全局鼠标监听
    void Stop();

    // 检查当前是否在运行
    bool IsRunning() const { return m_isRunning.load(); }

    // 注册划词选中文本回调
    void SetCallback(SelectionDetectedCallback callback);

    // 动态同步最新配置
    void ApplyConfig(const AppConfig& config);

    // 内部钩子处理函数（Win32 静态转接）
    void OnLowLevelMouseEvent(int message, int x, int y);

private:
    void ProcessSelectionAsync(int startX, int startY, int endX, int endY);

    // 检查当前的鼠标操作是否应被忽略（如自身窗口、拖拽窗口标题栏、滑动滚动条、调节窗体尺寸等非文本选中操作）
    bool ShouldIgnoreMouseEvent(int startX, int startY, int endX, int endY) const;

    std::shared_ptr<ConfigManager> m_configManager;
    std::atomic<bool> m_isRunning{false};

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
};

} // namespace LinguaAlpaca
