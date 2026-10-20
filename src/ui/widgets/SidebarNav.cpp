#include "SidebarNav.hpp"
#include "../theme/IconManager.hpp"
#include <wx/graphics.h>
#include <wx/dcbuffer.h>
#include <cmath>

namespace LinguaAlpaca::UI {

wxDEFINE_EVENT(EVT_SIDEBAR_NAV_CHANGED, wxCommandEvent);

SidebarNav::SidebarNav(wxWindow* parent, wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxSize(80_dip, -1), wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE) {
    
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    m_items = {
        { 0, L"文本", SVG::TEXT },
        { 1, L"OCR", SVG::OCR },
        { 2, L"词典", SVG::DICTIONARY },
        { 3, L"日志", SVG::LOG }
    };
    m_bottomItem = { 4, L"设置", SVG::SETTINGS };

    Bind(wxEVT_PAINT, &SidebarNav::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        Refresh();
        event.Skip();
    });
    Bind(wxEVT_LEFT_DOWN, &SidebarNav::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &SidebarNav::OnLeftUp, this);
    Bind(wxEVT_MOTION, &SidebarNav::OnMouseMove, this);
    Bind(wxEVT_LEFT_DCLICK, &SidebarNav::OnLeftDClick, this);
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) {
        if (!m_isDraggingWindow && m_hoverIndex != -1) {
            m_hoverIndex = -1;
            SetCursor(wxCursor(wxCURSOR_DEFAULT));
            Refresh();
        }
        event.Skip();
    });
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) {
        m_isDraggingWindow = false;
        m_itemPressed = false;
        if (m_onDragEnd) {
            m_onDragEnd();
        }
    });
}

void SidebarNav::SetDragCallbacks(
    std::function<void(wxWindow*)> onStartDrag,
    std::function<void(wxMouseEvent&)> onDragging,
    std::function<void()> onEndDrag,
    std::function<void()> onDoubleClick) {
    m_onDragStart = std::move(onStartDrag);
    m_onDragging = std::move(onDragging);
    m_onDragEnd = std::move(onEndDrag);
    m_onDoubleClick = std::move(onDoubleClick);
}

int SidebarNav::HitTest(const wxPoint& pt) const {
    wxSize size = GetClientSize();
    if (size.x <= 0 || size.y <= 0) return -1;

    int itemMarginX = 12_dip;
    if (pt.x < itemMarginX || pt.x > size.x - itemMarginX) {
        return -1;
    }

    int itemHeight = 66_dip;
    int topOffset = 16_dip;
    int itemBoxHeight = 58_dip;

    for (size_t i = 0; i < m_items.size(); ++i) {
        int itemY = (i <= 2) ? (topOffset + (int)i * itemHeight) : (topOffset + (int)i * itemHeight + 16_dip);
        if (pt.y >= itemY && pt.y <= itemY + itemBoxHeight) {
            return (int)i;
        }
    }

    int bottomY = size.y - 74_dip;
    if (pt.y >= bottomY && pt.y <= bottomY + itemBoxHeight) {
        return 4;
    }

    return -1;
}

void SidebarNav::SetActiveItem(int index) {
    if (m_selectedIndex != index) {
        m_selectedIndex = index;
        Refresh();
    }
}

void SidebarNav::OnPaint(wxPaintEvent& WXUNUSED(event)) {
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetClientSize();
    if (size.x <= 0 || size.y <= 0) return;

    auto palette = ThemeColors::GetCurrentPalette();
    dc.SetBackground(wxBrush(palette.sidebarBg));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;

    // 分割边框线
    gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 1)));
    gc->StrokeLine(size.x - 1, 0, size.x - 1, size.y);

    int itemHeight = 66_dip;
    int topOffset = 16_dip;
    int itemBoxHeight = 58_dip;
    int itemRadius = 10_dip;
    int itemMarginX = 12_dip;

    auto drawItem = [&](const SidebarNavItem& item, int yPos, bool isSelected, bool isHovered) {
        if (isSelected) {
            gc->SetBrush(gc->CreateBrush(wxBrush(palette.accentPrimary)));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->DrawRoundedRectangle(itemMarginX, yPos, size.x - itemMarginX * 2, itemBoxHeight, itemRadius);
        } else if (isHovered) {
            gc->SetBrush(gc->CreateBrush(wxBrush(palette.windowBg)));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->DrawRoundedRectangle(itemMarginX, yPos, size.x - itemMarginX * 2, itemBoxHeight, itemRadius);
        }

        wxColour iconTextColour = isSelected ? *wxWHITE : (isHovered ? palette.textPrimary : palette.textSecondary);

        // SVG Vector Icon
        wxBitmapBundle bundle = IconManager::GetIconBundle(item.svgContent, wxSize(20, 20), iconTextColour);
        wxBitmap bmp = bundle.GetBitmap(dip(20, 20));
        if (bmp.IsOk()) {
            gc->DrawBitmap(bmp, (size.x - bmp.GetWidth()) / 2.0, yPos + 8_dip, bmp.GetWidth(), bmp.GetHeight());
        }

        // Label
        wxFont labelFont = ThemeFont::GetFont(FontRole::Control, isSelected);
        gc->SetFont(labelFont, iconTextColour);
        double lw, lh;
        gc->GetTextExtent(item.label, &lw, &lh);
        gc->DrawText(item.label, (size.x - lw) / 2.0, yPos + 34_dip);
    };

    auto getItemY = [topOffset, itemHeight](size_t i) -> int {
        return (i <= 2) ? (topOffset + (int)i * itemHeight) : (topOffset + (int)i * itemHeight + 16_dip);
    };

    for (size_t i = 0; i < m_items.size(); ++i) {
        int y = getItemY(i);
        drawItem(m_items[i], y, m_selectedIndex == (int)i, m_hoverIndex == (int)i);
    }

    // 绘制居中的横向短分割线 (位于“词典”与“日志”按钮之间)
    double dividerY = topOffset + 3 * itemHeight + 4.0_dip;
    double lineLen = 28.0_dip;
    gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 1)));
    gc->StrokeLine((size.x - lineLen) / 2.0, dividerY, (size.x + lineLen) / 2.0, dividerY);

    // 底部设置按钮
    int bottomY = size.y - 74_dip;
    drawItem(m_bottomItem, bottomY, m_selectedIndex == 4, m_hoverIndex == 4);
}

void SidebarNav::OnLeftDown(wxMouseEvent& event) {
    wxPoint pos = event.GetPosition();
    int hitIndex = HitTest(pos);

    if (hitIndex != -1) {
        if (m_selectedIndex != hitIndex) {
            SetActiveItem(hitIndex);
            wxCommandEvent evt(EVT_SIDEBAR_NAV_CHANGED, GetId());
            evt.SetInt(hitIndex);
            evt.SetEventObject(this);
            ProcessWindowEvent(evt);
        }
        m_mouseDownPos = pos;
        m_itemPressed = true;
        m_isDraggingWindow = false;
    } else {
        m_itemPressed = false;
        m_isDraggingWindow = true;
        if (m_hoverIndex != -1) {
            m_hoverIndex = -1;
            SetCursor(wxCursor(wxCURSOR_DEFAULT));
            Refresh();
        }
        if (m_onDragStart) {
            m_onDragStart(this);
        }
    }
    event.Skip();
}

void SidebarNav::OnLeftUp(wxMouseEvent& event) {
    m_itemPressed = false;
    if (m_isDraggingWindow) {
        m_isDraggingWindow = false;
        if (m_onDragEnd) {
            m_onDragEnd();
        }
    }

    int oldHover = m_hoverIndex;
    m_hoverIndex = HitTest(event.GetPosition());
    if (oldHover != m_hoverIndex) {
        SetCursor(m_hoverIndex != -1 ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_DEFAULT));
        Refresh();
    }
    event.Skip();
}

void SidebarNav::OnMouseMove(wxMouseEvent& event) {
    wxPoint pos = event.GetPosition();

    if (m_isDraggingWindow) {
        if (m_onDragging) {
            m_onDragging(event);
        }
        return;
    }

    if (m_itemPressed && event.Dragging() && event.LeftIsDown()) {
        int dx = std::abs(pos.x - m_mouseDownPos.x);
        int dy = std::abs(pos.y - m_mouseDownPos.y);
        if (dx >= 4_dip || dy >= 4_dip) {
            m_itemPressed = false;
            m_isDraggingWindow = true;
            if (m_hoverIndex != -1) {
                m_hoverIndex = -1;
                SetCursor(wxCursor(wxCURSOR_DEFAULT));
                Refresh();
            }
            if (m_onDragStart) {
                m_onDragStart(this);
            }
            if (m_onDragging) {
                m_onDragging(event);
            }
            return;
        }
    }

    int oldHover = m_hoverIndex;
    m_hoverIndex = HitTest(pos);

    if (oldHover != m_hoverIndex) {
        SetCursor(m_hoverIndex != -1 ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_DEFAULT));
        Refresh();
    }
}

void SidebarNav::OnLeftDClick(wxMouseEvent& event) {
    int hitIndex = HitTest(event.GetPosition());
    if (hitIndex == -1) {
        if (m_onDoubleClick) {
            m_onDoubleClick();
        }
    }
    event.Skip();
}

} // namespace LinguaAlpaca::UI
