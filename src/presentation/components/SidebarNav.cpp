#include "SidebarNav.hpp"
#include "../theme/IconManager.hpp"
#include <wx/graphics.h>
#include <wx/dcbuffer.h>

namespace LinguaAlpaca::Presentation::Components {

wxDEFINE_EVENT(EVT_SIDEBAR_NAV_CHANGED, wxCommandEvent);

SidebarNav::SidebarNav(wxWindow* parent, wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxSize(80, -1), wxBORDER_NONE) {
    
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    m_items = {
        { 0, L"文本", Theme::SVG::TEXT },
        { 1, L"历史", Theme::SVG::HISTORY }
    };
    m_bottomItem = { 2, L"设置", Theme::SVG::SETTINGS };

    Bind(wxEVT_PAINT, &SidebarNav::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &SidebarNav::OnLeftDown, this);
    Bind(wxEVT_MOTION, &SidebarNav::OnMouseMove, this);
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

    auto palette = Theme::ThemeColors::GetCurrentPalette();
    dc.SetBackground(wxBrush(palette.sidebarBg));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;

    // 分割边框线
    gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 1)));
    gc->StrokeLine(size.x - 1, 0, size.x - 1, size.y);

    int itemHeight = 66;
    int topOffset = 16;

    auto drawItem = [&](const SidebarNavItem& item, int yPos, bool isSelected, bool isHovered) {
        if (isSelected) {
            gc->SetBrush(gc->CreateBrush(wxBrush(palette.accentPrimary)));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->DrawRoundedRectangle(12, yPos, size.x - 24, 58, 10);
        } else if (isHovered) {
            gc->SetBrush(gc->CreateBrush(wxBrush(palette.windowBg)));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->DrawRoundedRectangle(12, yPos, size.x - 24, 58, 10);
        }

        wxColour iconTextColour = isSelected ? *wxWHITE : (isHovered ? palette.textPrimary : palette.textSecondary);

        // SVG Vector Icon
        wxBitmapBundle bundle = Theme::IconManager::GetIconBundle(item.svgContent, wxSize(20, 20), iconTextColour);
        wxBitmap bmp = bundle.GetBitmap(wxSize(20, 20));
        if (bmp.IsOk()) {
            gc->DrawBitmap(bmp, (size.x - 20) / 2.0, yPos + 8, 20, 20);
        }

        // Label
        wxFont labelFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, isSelected ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei");
        gc->SetFont(labelFont, iconTextColour);
        double lw, lh;
        gc->GetTextExtent(item.label, &lw, &lh);
        gc->DrawText(item.label, (size.x - lw) / 2.0, yPos + 34);
    };

    for (size_t i = 0; i < m_items.size(); ++i) {
        int y = topOffset + i * itemHeight;
        drawItem(m_items[i], y, m_selectedIndex == (int)i, m_hoverIndex == (int)i);
    }

    // 绘制横向细分割线 (保持在“历史”按钮的上方，位于“文本”与“历史”之间)
    // int dividerY = topOffset + 1 * itemHeight - 4;
    // gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 1)));
    // gc->StrokeLine(16, dividerY, size.x - 16, dividerY);

    // 底部设置按钮
    int bottomY = size.y - 74;
    drawItem(m_bottomItem, bottomY, m_selectedIndex == 2, m_hoverIndex == 2);
}

void SidebarNav::OnLeftDown(wxMouseEvent& event) {
    int y = event.GetY();
    int sizeY = GetClientSize().y;
    int topOffset = 16;
    int itemHeight = 66;

    int newIndex = -1;
    for (size_t i = 0; i < m_items.size(); ++i) {
        int itemY = topOffset + i * itemHeight;
        if (y >= itemY && y <= itemY + 58) {
            newIndex = (int)i;
            break;
        }
    }

    if (y >= sizeY - 74 && y <= sizeY - 16) {
        newIndex = 2;
    }

    if (newIndex != -1) {
        SetActiveItem(newIndex);
        wxCommandEvent evt(EVT_SIDEBAR_NAV_CHANGED, GetId());
        evt.SetInt(newIndex);
        evt.SetEventObject(this);
        ProcessWindowEvent(evt);
    }
}

void SidebarNav::OnMouseMove(wxMouseEvent& event) {
    int y = event.GetY();
    int sizeY = GetClientSize().y;
    int topOffset = 16;
    int itemHeight = 66;

    int oldHover = m_hoverIndex;
    m_hoverIndex = -1;

    for (size_t i = 0; i < m_items.size(); ++i) {
        int itemY = topOffset + i * itemHeight;
        if (y >= itemY && y <= itemY + 58) {
            m_hoverIndex = (int)i;
            break;
        }
    }

    if (y >= sizeY - 74 && y <= sizeY - 16) {
        m_hoverIndex = 2;
    }

    if (oldHover != m_hoverIndex) {
        SetCursor(m_hoverIndex != -1 ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_DEFAULT));
        Refresh();
    }
}

} // namespace LinguaAlpaca::Presentation::Components
