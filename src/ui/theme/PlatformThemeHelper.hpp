#pragma once
#include <wx/wx.h>
#include "Theme.hpp"

namespace LinguaAlpaca::UI {

/**
 * @brief 跨平台原生主题外观辅助类 (封装 macOS NSAppearance / Win32 DarkMode 主题细节)
 */
class PlatformThemeHelper {
public:
    /**
     * @brief 设置应用级原生系统外观 (在 macOS 上切换 NSAppearanceNameDarkAqua / NSAppearanceNameAqua)
     */
    static void ApplyAppAppearance(ThemeMode mode);

    /**
     * @brief 设置特定顶级窗口的外观 (macOS NSWindow / Win32 DWMWA_USE_IMMERSIVE_DARK_MODE)
     */
    static void ApplyWindowAppearance(wxWindow* window, ThemeMode mode);

    /**
     * @brief 为原生控件 (如 wxCheckBox, wxRadioBox, wxRadioButton) 应用暗黑/浅色主题文字与控件外观
     */
    static void ApplyControlTheme(wxWindow* control, const ThemePalette& palette);
};

} // namespace LinguaAlpaca::UI
