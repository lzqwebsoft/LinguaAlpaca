#pragma execution_character_set("utf-8")
#include "ScrollBar.hpp"
#include "TextCtrl.hpp"
#include "../theme/Theme.hpp"
#include <algorithm>
#include <wx/dcbuffer.h>

namespace LinguaAlpaca::UI {

ScrollBar::ScrollBar(TextCtrl* parentTextCtrl)
    : wxWindow(parentTextCtrl, wxID_ANY, wxDefaultPosition, wxSize(8_dip, -1),
               wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE),
      m_parentTextCtrl(parentTextCtrl) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    Bind(wxEVT_PAINT, &ScrollBar::OnPaint, this);
    Bind(wxEVT_ENTER_WINDOW, &ScrollBar::OnMouseEnter, this);
    Bind(wxEVT_LEAVE_WINDOW, &ScrollBar::OnMouseLeave, this);
    Bind(wxEVT_LEFT_DOWN, &ScrollBar::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &ScrollBar::OnLeftUp, this);
    Bind(wxEVT_MOTION, &ScrollBar::OnMouseMove, this);
    Bind(wxEVT_MOUSEWHEEL, &ScrollBar::OnMouseWheel, this);

    m_hideTimer.Bind(wxEVT_TIMER, &ScrollBar::OnTimer, this);
}

ScrollBar::~ScrollBar() {
    if (m_hideTimer.IsRunning()) {
        m_hideTimer.Stop();
    }
}

void ScrollBar::SetScrollParams(int firstVisibleLine, int visibleLines, int totalLines) {
    m_firstVisibleLine = firstVisibleLine;
    m_visibleLines = visibleLines;
    m_totalLines = totalLines;
    m_needed = (m_totalLines > m_visibleLines);
    if (m_needed) {
        NotifyActivity();
    } else {
        m_isVisible = false;
        Refresh();
    }
}

void ScrollBar::NotifyActivity() {
    if (!m_needed) return;
    m_isVisible = true;
    Refresh();

    // 重新启动自动隐藏定时器 (1200ms 后自动隐藏)
    m_hideTimer.StartOnce(1200);
}

void ScrollBar::OnTimer(wxTimerEvent& WXUNUSED(event)) {
    // 鼠标悬停或正在拖拽时不隐藏
    if (m_isHovered || m_isDragging) {
        m_hideTimer.StartOnce(1200);
        return;
    }
    m_isVisible = false;
    Refresh();
}

void ScrollBar::OnPaint(wxPaintEvent& WXUNUSED(event)) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();

    if (!m_needed || (!m_isVisible && !m_isHovered && !m_isDragging)) {
        return;
    }

    int clientH = GetClientSize().GetHeight();
    if (clientH <= 0) return;

    int thumbH = std::clamp((clientH * m_visibleLines) / m_totalLines, 20_dip, clientH - 4_dip);
    int availableTrack = clientH - thumbH;
    int maxScroll = m_totalLines - m_visibleLines;
    int thumbY = (maxScroll > 0) ? (availableTrack * m_firstVisibleLine / maxScroll) : 0;
    thumbY = std::clamp(thumbY, 0, availableTrack);

    ThemePalette palette = ThemeManager::GetCurrentPalette();
    wxColour thumbColor;
    if (m_isDragging) {
        thumbColor = palette.accentHover;
    } else if (m_isHovered) {
        thumbColor = palette.accentPrimary;
    } else {
        thumbColor = palette.cardBorderActive;
    }

    int thumbW = (m_isHovered || m_isDragging) ? 6_dip : 4_dip;
    int thumbX = (GetClientSize().GetWidth() - thumbW) / 2;

    dc.SetBrush(wxBrush(thumbColor));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRoundedRectangle(thumbX, thumbY, thumbW, thumbH, thumbW / 2.0);
}

void ScrollBar::OnMouseEnter(wxMouseEvent& WXUNUSED(event)) {
    m_isHovered = true;
    NotifyActivity();
}

void ScrollBar::OnMouseLeave(wxMouseEvent& WXUNUSED(event)) {
    m_isHovered = false;
    if (!m_isDragging) {
        m_hideTimer.StartOnce(800);
    }
}

void ScrollBar::OnLeftDown(wxMouseEvent& event) {
    if (!m_needed) return;

    NotifyActivity();

    int clientH = GetClientSize().GetHeight();
    int thumbH = std::clamp((clientH * m_visibleLines) / m_totalLines, 20_dip, clientH - 4_dip);
    int availableTrack = clientH - thumbH;
    int maxScroll = m_totalLines - m_visibleLines;
    int thumbY = (maxScroll > 0) ? (availableTrack * m_firstVisibleLine / maxScroll) : 0;
    thumbY = std::clamp(thumbY, 0, availableTrack);

    int mouseY = event.GetPosition().y;
    if (mouseY >= thumbY && mouseY <= thumbY + thumbH) {
        m_isDragging = true;
        m_dragStartMouseY = mouseY;
        m_dragStartFirstLine = m_firstVisibleLine;
        CaptureMouse();
        Refresh();
    } else if (mouseY < thumbY) {
        // Page Up
        int newFirst = std::max(0, m_firstVisibleLine - m_visibleLines);
        if (m_parentTextCtrl) {
            m_parentTextCtrl->ScrollToLine(newFirst);
        }
    } else {
        // Page Down
        int newFirst = std::min(maxScroll, m_firstVisibleLine + m_visibleLines);
        if (m_parentTextCtrl) {
            m_parentTextCtrl->ScrollToLine(newFirst);
        }
    }
}

void ScrollBar::OnLeftUp(wxMouseEvent& WXUNUSED(event)) {
    if (m_isDragging) {
        m_isDragging = false;
        if (HasCapture()) {
            ReleaseMouse();
        }
        m_hideTimer.StartOnce(1200);
        Refresh();
    }
}

void ScrollBar::OnMouseMove(wxMouseEvent& event) {
    if (m_isDragging && m_needed) {
        NotifyActivity();
        int deltaY = event.GetPosition().y - m_dragStartMouseY;
        int clientH = GetClientSize().GetHeight();
        int thumbH = std::clamp((clientH * m_visibleLines) / m_totalLines, 20_dip, clientH - 4_dip);
        int availableTrack = clientH - thumbH;
        int maxScroll = m_totalLines - m_visibleLines;
        if (availableTrack > 0 && maxScroll > 0) {
            int deltaLines = (deltaY * maxScroll) / availableTrack;
            int targetLine = std::clamp(m_dragStartFirstLine + deltaLines, 0, maxScroll);
            if (m_parentTextCtrl) {
                m_parentTextCtrl->ScrollToLine(targetLine);
            }
        }
    }
}

void ScrollBar::OnMouseWheel(wxMouseEvent& event) {
    if (!m_needed) return;
    NotifyActivity();
    int rotation = event.GetWheelRotation();
    int lines = (rotation > 0) ? -3 : 3;
    if (m_parentTextCtrl) {
        m_parentTextCtrl->ScrollToLine(m_firstVisibleLine + lines);
    }
}

} // namespace LinguaAlpaca::UI
