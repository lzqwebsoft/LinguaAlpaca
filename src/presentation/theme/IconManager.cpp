#include "IconManager.hpp"
#include <wx/mstream.h>

namespace LinguaAlpaca::Presentation::Theme {

wxBitmapBundle IconManager::GetIconBundle(
    const char* svgContent,
    const wxSize& size,
    const wxColour& tintColor) {

    if (!svgContent) {
        return wxBitmapBundle();
    }

    std::string svgStr(svgContent);

    if (tintColor.IsOk()) {
        std::string hexColor = wxString::Format("#%02X%02X%02X", tintColor.Red(), tintColor.Green(), tintColor.Blue()).ToStdString();
        
        size_t pos = 0;
        while ((pos = svgStr.find("currentColor", pos)) != std::string::npos) {
            svgStr.replace(pos, 12, hexColor);
            pos += hexColor.length();
        }
    }

    return wxBitmapBundle::FromSVG(svgStr.c_str(), size);
}

} // namespace LinguaAlpaca::Presentation::Theme
