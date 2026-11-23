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

    /**
     * @brief 从 wxBitmapBundle 提取适配目标窗口内容缩放倍率 (Retina / High-DPI) 的物理像素位图
     */
    static wxBitmap GetBitmap(
        const wxWindow* window,
        const wxBitmapBundle& bundle,
        const wxSize& drawSize
    );

    /**
     * @brief 获取适配目标窗口内容缩放倍率 (Retina / High-DPI) 的 SVG 物理像素位图
     */
    static wxBitmap GetIconBitmap(
        const wxWindow* window,
        const char* svgContent,
        const wxSize& drawSize = wxSize(16, 16),
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
