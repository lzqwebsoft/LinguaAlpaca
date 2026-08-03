#pragma once
#include <wx/wx.h>
#include "../theme/ThemeColors.hpp"

namespace LinguaAlpaca::Presentation::Components {

enum class ButtonStyle {
    Primary,
    Secondary,
    Green,
    Danger
};

class CustomButton : public wxControl {
public:
    CustomButton(wxWindow* parent, wxWindowID id, const wxString& label,
                 ButtonStyle style = ButtonStyle::Primary,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize);

    void SetLabel(const wxString& label) override;
    void SetButtonStyle(ButtonStyle style) { m_buttonStyle = style; Refresh(); }
    void SetIcon(const char* svgContent, const wxSize& iconSize = wxSize(16, 16), const wxColour& tintColor = wxNullColour);
    void SetIconBundle(const wxBitmapBundle& bundle) { m_iconBundle = bundle; Refresh(); }

protected:
    wxSize DoGetBestSize() const override;

private:
    void OnPaint(wxPaintEvent& event);
    void OnMouseEnter(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);

    wxString m_label;
    ButtonStyle m_buttonStyle;
    wxBitmapBundle m_iconBundle;
    bool m_isHovered{false};
    bool m_isPressed{false};
};

} // namespace LinguaAlpaca::Presentation::Components
