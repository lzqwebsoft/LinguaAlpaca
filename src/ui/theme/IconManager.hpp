#pragma once
#include <wx/wx.h>
#include <wx/bmpbndl.h>
#include <string>
#include "AppIcons.hpp"

namespace LinguaAlpaca::UI {

class IconManager {
public:
    static wxBitmapBundle GetIconBundle(
        const char* svgContent,
        const wxSize& size = wxSize(16, 16),
        const wxColour& tintColor = wxNullColour
    );

    static wxImage GetAppLogoImage();
    static wxImage GetAppWindowIconImage();
    static wxBitmapBundle GetAppLogoBundle(const wxSize& targetSize = wxSize(28, 28));
    static wxBitmapBundle GetAppStatusBarBundle();
    static wxIcon GetAppIcon(const wxSize& targetSize = wxSize(32, 32));
    static wxIconBundle GetAppIconBundle();

    /**
     * @brief 设置应用级全局系统图标 (例如 macOS 程序的 Dock 坞图标)
     */
    static void SetupApplicationIcon();
};

} // namespace LinguaAlpaca::UI
