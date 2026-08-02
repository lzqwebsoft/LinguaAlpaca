#include "CustomButton.hpp"
#include <wx/graphics.h>
#include <wx/dcbuffer.h>
#include <algorithm>

namespace LinguaAlpaca::Presentation::Components {

CustomButton::CustomButton(wxWindow* parent, wxWindowID id, const wxString& label,
                           ButtonStyle style, const wxPoint& pos, const wxSize& size)
    : wxControl(parent, id, pos, size, wxBORDER_NONE)
    , m_label(label)
    , m_buttonStyle(style) {
    
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    Bind(wxEVT_PAINT, &CustomButton::OnPaint, this);
    Bind(wxEVT_ENTER_WINDOW, &CustomButton::OnMouseEnter, this);
    Bind(wxEVT_LEAVE_WINDOW, &CustomButton::OnMouseLeave, this);
    Bind(wxEVT_LEFT_DOWN, &CustomButton::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &CustomButton::OnLeftUp, this);
}

void CustomButton::SetLabel(const wxString& label) {
    m_label = label;
    Refresh();
}

wxSize CustomButton::DoGetBestSize() const {
    wxClientDC dc(const_cast<CustomButton*>(this));
    wxFont font = wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei");
    dc.SetFont(font);
    wxSize extent = dc.GetTextExtent(m_label);
    return wxSize(extent.x + 36, 40);
}

void CustomButton::OnPaint(wxPaintEvent& WXUNUSED(event)) {
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetClientSize();
    if (size.x <= 0 || size.y <= 0) return;

    auto palette = Theme::ThemeColors::GetCurrentPalette();
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;

    wxColour bgColour;
    wxColour textColour;
    wxColour borderColour = wxNullColour;

    switch (m_buttonStyle) {
    case ButtonStyle::Primary:
        bgColour = m_isHovered ? palette.accentHover : palette.accentPrimary;
        textColour = *wxWHITE;
        break;
    case ButtonStyle::Green:
        bgColour = m_isHovered ? wxColour(22, 163, 74) : palette.accentGreen;
        textColour = *wxWHITE;
        break;
    case ButtonStyle::Danger:
        bgColour = m_isHovered ? wxColour(220, 38, 38) : wxColour(239, 68, 68); // 鲜艳警示红
        textColour = *wxWHITE;
        break;
    case ButtonStyle::Secondary:
        bgColour = m_isHovered ? (palette.sidebarBg == *wxWHITE ? wxColour(241, 245, 249) : wxColour(51, 65, 85)) : palette.cardBg;
        textColour = palette.textPrimary;
        borderColour = palette.cardBorder;
        break;
    }

    // 圆角矩形绘制
    double radius = 10.0;
    gc->SetBrush(gc->CreateBrush(wxBrush(bgColour)));
    if (borderColour.IsOk()) {
        gc->SetPen(gc->CreatePen(wxPen(borderColour, 1)));
    } else {
        gc->SetPen(*wxTRANSPARENT_PEN);
    }
    
    gc->DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, radius);

    // 绘制文字
    wxFont font = wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei");
    gc->SetFont(font, textColour);

    double tw, th;
    gc->GetTextExtent(m_label, &tw, &th);
    double x = (size.x - tw) / 2.0;
    if (x < 6.0) x = 6.0;
    double y = (size.y - th) / 2.0;
    if (y < 1.0) y = 1.0;
    gc->DrawText(m_label, x, y);
}

void CustomButton::OnMouseEnter(wxMouseEvent& WXUNUSED(event)) {
    m_isHovered = true;
    SetCursor(wxCursor(wxCURSOR_HAND));
    Refresh();
}

void CustomButton::OnMouseLeave(wxMouseEvent& WXUNUSED(event)) {
    m_isHovered = false;
    m_isPressed = false;
    SetCursor(wxCursor(wxCURSOR_DEFAULT));
    Refresh();
}

void CustomButton::OnLeftDown(wxMouseEvent& WXUNUSED(event)) {
    m_isPressed = true;
    Refresh();
}

void CustomButton::OnLeftUp(wxMouseEvent& event) {
    if (m_isPressed) {
        m_isPressed = false;
        Refresh();

        // 触发按钮事件
        wxCommandEvent evt(wxEVT_BUTTON, GetId());
        evt.SetEventObject(this);
        ProcessWindowEvent(evt);
    }
}

} // namespace LinguaAlpaca::Presentation::Components
