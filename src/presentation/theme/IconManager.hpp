#pragma once
#include <wx/wx.h>
#include <wx/bmpbndl.h>
#include <string>
#include "AppIcons.hpp"

namespace LinguaAlpaca::Presentation::Theme {

class IconManager {
public:
    static wxBitmapBundle GetIconBundle(
        const char* svgContent,
        const wxSize& size = wxSize(16, 16),
        const wxColour& tintColor = wxNullColour
    );
};

} // namespace LinguaAlpaca::Presentation::Theme
