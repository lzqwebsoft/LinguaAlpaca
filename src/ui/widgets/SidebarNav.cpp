#include "SidebarNav.hpp"
#include "../theme/IconManager.hpp"
#include <wx/graphics.h>
#include <wx/dcbuffer.h>

namespace LinguaAlpaca::UI {

wxDEFINE_EVENT(EVT_SIDEBAR_NAV_CHANGED, wxCommandEvent);

SidebarNav::SidebarNav(wxWindow* parent, wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxSize(80_dip, -1), wxBORDER_NONE) {
    
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    m_items = {
        { 0, L"文本", SVG::TEXT },
        { 1, L"OCR", SVG::OCR },
        { 2, L"词典", SVG::DICTIONARY },
        { 3, L"日志", SVG::LOG }
    };
    m_bottomItem = { 4, L"设置", SVG::SETTINGS };

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
        wxBitmapBundle bundle = IconManager::GetIconBundle(item.svgContent, dip(20, 20), iconTextColour);
        wxBitmap bmp = bundle.GetBitmap(dip(20, 20));
        if (bmp.IsOk()) {
            gc->DrawBitmap(bmp, (size.x - bmp.GetWidth()) / 2.0, yPos + 8_dip, bmp.GetWidth(), bmp.GetHeight());
        }

        // Label
        wxFont labelFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, isSelected ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei");
        gc->SetFont(labelFont, iconTextColour);
        double lw, lh;
        gc->GetTextExtent(item.label, &lw, &lh);
        gc->DrawText(item.label, (size.x - lw) / 2.0, yPos + 34_dip);
    };

    auto getItemY = [topOffset, itemHeight](size_t i) -> int {
        return (i <= 1) ? (topOffset + (int)i * itemHeight) : (topOffset + (int)i * itemHeight + 16_dip);
    };

    for (size_t i = 0; i < m_items.size(); ++i) {
        int y = getItemY(i);
        drawItem(m_items[i], y, m_selectedIndex == (int)i, m_hoverIndex == (int)i);
    }

    // 绘制居中的横向短分割线 (位于“OCR”与“历史”按钮之间)
    double dividerY = topOffset + 2 * itemHeight + 4.0_dip;
    double lineLen = 28.0_dip;
    gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 1)));
    gc->StrokeLine((size.x - lineLen) / 2.0, dividerY, (size.x + lineLen) / 2.0, dividerY);

    // 底部设置按钮
    int bottomY = size.y - 74_dip;
    drawItem(m_bottomItem, bottomY, m_selectedIndex == 4, m_hoverIndex == 4);
}

void SidebarNav::OnLeftDown(wxMouseEvent& event) {
    int y = event.GetY();
    int sizeY = GetClientSize().y;
    int topOffset = 16_dip;
    int itemHeight = 66_dip;

    int newIndex = -1;
    for (size_t i = 0; i < m_items.size(); ++i) {
        int itemY = (i <= 1) ? (topOffset + (int)i * itemHeight) : (topOffset + (int)i * itemHeight + 16_dip);
        if (y >= itemY && y <= itemY + 58_dip) {
            newIndex = (int)i;
            break;
        }
    }

    if (y >= sizeY - 74_dip && y <= sizeY - 16_dip) {
        newIndex = 4;
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
    int topOffset = 16_dip;
    int itemHeight = 66_dip;

    int oldHover = m_hoverIndex;
    m_hoverIndex = -1;

    for (size_t i = 0; i < m_items.size(); ++i) {
        int itemY = (i <= 1) ? (topOffset + (int)i * itemHeight) : (topOffset + (int)i * itemHeight + 16_dip);
        if (y >= itemY && y <= itemY + 58_dip) {
            m_hoverIndex = (int)i;
            break;
        }
    }

    if (y >= sizeY - 74_dip && y <= sizeY - 16_dip) {
        m_hoverIndex = 4;
    }

    if (oldHover != m_hoverIndex) {
        SetCursor(m_hoverIndex != -1 ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_DEFAULT));
        Refresh();
    }
}

} // namespace LinguaAlpaca::UI
