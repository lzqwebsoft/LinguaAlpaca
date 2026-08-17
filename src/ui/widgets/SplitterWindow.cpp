#pragma execution_character_set("utf-8")
#include "SplitterWindow.hpp"
#include "../theme/Theme.hpp"
#include <algorithm>

namespace LinguaAlpaca::UI {

SplitterWindow::SplitterWindow(wxWindow* parent, wxWindowID id,
                               const wxPoint& pos,
                               const wxSize& size,
                               long style)
    : wxSplitterWindow(parent, id, pos, size, style) {
}

void SplitterWindow::DrawSash(wxDC& dc) {
    if (m_sashPosition == 0 || !m_windowTwo || IsSashInvisible()) {
        return;
    }

    ThemePalette palette = ThemeManager::GetCurrentPalette();
    wxSize sz = GetClientSize();
    int sashSize = GetSashSize();

    if (m_splitMode == wxSPLIT_HORIZONTAL) {
        wxRect sashRect(0, m_sashPosition, sz.GetWidth(), sashSize);

        // 分隔区域背景填充
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(palette.cardBg));
        dc.DrawRectangle(sashRect);

        // 分隔细线
        dc.SetPen(wxPen(palette.cardBorder, 1));
        int centerY = m_sashPosition + sashSize / 2;
        dc.DrawLine(0, centerY, sz.GetWidth(), centerY);

        // 居中圆角拖动手柄 (Pill Grab Handle)
        int handleW = 38_dip;
        int handleH = std::max(3_dip, sashSize - 4_dip);
        int handleX = (sz.GetWidth() - handleW) / 2;
        int handleY = m_sashPosition + (sashSize - handleH) / 2;

        wxColour handleColor = m_isHot ? palette.accentPrimary : palette.cardBorderActive;
        dc.SetBrush(wxBrush(handleColor));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRoundedRectangle(handleX, handleY, handleW, handleH, handleH / 2.0);
    } else {
        wxRect sashRect(m_sashPosition, 0, sashSize, sz.GetHeight());

        // 垂直分割模式下的绘制
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(palette.cardBg));
        dc.DrawRectangle(sashRect);

        dc.SetPen(wxPen(palette.cardBorder, 1));
        int centerX = m_sashPosition + sashSize / 2;
        dc.DrawLine(centerX, 0, centerX, sz.GetHeight());

        int handleW = std::max(3_dip, sashSize - 4_dip);
        int handleH = 38_dip;
        int handleX = m_sashPosition + (sashSize - handleW) / 2;
        int handleY = (sz.GetHeight() - handleH) / 2;

        wxColour handleColor = m_isHot ? palette.accentPrimary : palette.cardBorderActive;
        dc.SetBrush(wxBrush(handleColor));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRoundedRectangle(handleX, handleY, handleW, handleH, handleH / 2.0);
    }
}

void SplitterWindow::OnEnterSash() {
    SetResizeCursor();
    m_isHot = true;
    wxClientDC dc(this);
    DrawSash(dc);
}

void SplitterWindow::OnLeaveSash() {
    SetCursor(*wxSTANDARD_CURSOR);
    m_isHot = false;
    wxClientDC dc(this);
    DrawSash(dc);
}

} // namespace LinguaAlpaca::UI
