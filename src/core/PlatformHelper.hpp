#pragma once
#pragma execution_character_set("utf-8")

#include <wx/string.h>

#ifdef __WXMSW__
#include <windows.h>
#endif

namespace LinguaAlpaca {

/**
 * @brief 跨平台原生操作系统支持辅助类 (单例进程唤醒、前台窗口焦点等)
 */
class PlatformHelper {
public:
    /**
     * @brief 激活已在运行的 LinguaAlpaca 实例窗口并置顶 (跨平台支持 Windows 与 macOS)
     */
    static void ActivateExistingInstance();

#ifdef __WXMSW__
    /**
     * @brief 获取 Windows 平台下单例唤醒的系统注册消息 ID (LinguaAlpaca_SingleInstance_Activate)
     */
    static UINT GetSingleInstanceActivateMsg();
#endif
};

} // namespace LinguaAlpaca
