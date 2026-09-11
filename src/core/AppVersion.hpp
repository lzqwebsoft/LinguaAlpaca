#pragma once
#include <string>

namespace LinguaAlpaca {

/**
 * @brief 跨平台获取当前应用程序发布版本号
 * - macOS: 优先读取 App Bundle 内 Info.plist 中的 CFBundleShortVersionString 配置
 * - Windows / Linux / 未打包环境: 自动回退到 CMakeLists.txt 统一配置的 APP_VERSION 宏
 */
std::string GetAppVersion();

} // namespace LinguaAlpaca
