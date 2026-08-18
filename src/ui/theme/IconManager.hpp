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
    static wxIcon GetAppIcon(const wxSize& targetSize = wxSize(32, 32));
    static wxIconBundle GetAppIconBundle();
};

} // namespace LinguaAlpaca::UI
