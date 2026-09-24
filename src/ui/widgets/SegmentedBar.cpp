#include "SegmentedBar.hpp"
#include "../theme/IconManager.hpp"
#include <wx/graphics.h>
#include <wx/dcbuffer.h>

namespace LinguaAlpaca::UI {

SegmentedBar::SegmentedBar(wxWindow* parent,
                           const std::vector<SegmentItem>& items,
                           std::function<void(int index)> onSelect,
                           wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE)
    , m_items(items)
    , m_onSelect(std::move(onSelect)) {
    InitUI();
}

void SegmentedBar::InitUI() {
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    Bind(wxEVT_PAINT, &SegmentedBar::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        Refresh();
        event.Skip();
    });
    Bind(wxEVT_MOTION, &SegmentedBar::OnMouseMove, this);
    Bind(wxEVT_LEAVE_WINDOW, &SegmentedBar::OnMouseLeave, this);
    Bind(wxEVT_LEFT_DOWN, &SegmentedBar::OnLeftDown, this);
}

wxSize SegmentedBar::DoGetBestSize() const {
    return dip(-1, 38);
}

void SegmentedBar::SetActiveIndex(int index, bool triggerCallback) {
    if (index >= 0 && index < static_cast<int>(m_items.size()) && m_selectedIndex != index) {
        m_selectedIndex = index;
        Refresh();
        if (triggerCallback && m_onSelect) {
            m_onSelect(m_selectedIndex);
        }
    }
}

void SegmentedBar::UpdateTheme() {
    Refresh();
}

int SegmentedBar::HitTestItem(const wxPoint& pt) const {
    for (size_t i = 0; i < m_itemRects.size(); ++i) {
        if (m_itemRects[i].Contains(pt)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void SegmentedBar::OnMouseMove(wxMouseEvent& event) {
    wxPoint pos = event.GetPosition();
    int hit = HitTestItem(pos);
    if (hit != m_hoverIndex) {
        m_hoverIndex = hit;
        SetCursor(hit >= 0 ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_ARROW));
        Refresh();
    }
}

void SegmentedBar::OnMouseLeave(wxMouseEvent& WXUNUSED(event)) {
    if (m_hoverIndex != -1) {
        m_hoverIndex = -1;
        SetCursor(wxCursor(wxCURSOR_ARROW));
        Refresh();
    }
}

void SegmentedBar::OnLeftDown(wxMouseEvent& event) {
    int hit = HitTestItem(event.GetPosition());
    if (hit >= 0 && hit < static_cast<int>(m_items.size())) {
        if (m_selectedIndex != hit) {
            m_selectedIndex = hit;
            Refresh();
            if (m_onSelect) {
                m_onSelect(m_selectedIndex);
            }
        }
    }
}

void SegmentedBar::OnPaint(wxPaintEvent& WXUNUSED(event)) {
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetClientSize();
    if (size.x <= 0 || size.y <= 0)
        return;

    auto palette = ThemeColors::GetCurrentPalette();
    dc.SetBackground(wxBrush(palette.windowBg));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc)
        return;

    int padOuter = 3_dip;
    double barRadius = 9.0_dip;

    // 1. 绘制外部底色槽（微淡卡片色 + 极细边框）
    gc->SetBrush(gc->CreateBrush(wxBrush(palette.cardBg)));
    gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 1.0)));
    gc->DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, barRadius);

    if (m_items.empty())
        return;

    size_t count = m_items.size();
    int availWidth = size.x - padOuter * 2;
    int availHeight = size.y - padOuter * 2;
    int itemWidth = availWidth / static_cast<int>(count);
    double itemRadius = 7.0_dip;

    m_itemRects.resize(count);

    wxFont font = ThemeFont::GetFont(FontRole::Control, false);
    wxFont boldFont = ThemeFont::GetFont(FontRole::Control, true);

    for (size_t i = 0; i < count; ++i) {
        int x = padOuter + static_cast<int>(i) * itemWidth;
        int w = (i == count - 1) ? (availWidth - static_cast<int>(i) * itemWidth) : itemWidth;
        int y = padOuter;
        int h = availHeight;

        m_itemRects[i] = wxRect(x, y, w, h);

        bool isSelected = (m_selectedIndex == static_cast<int>(i));
        bool isHovered = (m_hoverIndex == static_cast<int>(i) && !isSelected);

        // 2. 绘制分段背景
        if (isSelected) {
            gc->SetBrush(gc->CreateBrush(wxBrush(palette.accentPrimary)));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->DrawRoundedRectangle(x, y, w, h, itemRadius);
        } else if (isHovered) {
            gc->SetBrush(gc->CreateBrush(wxBrush(palette.bannerBg)));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->DrawRoundedRectangle(x, y, w, h, itemRadius);
        }

        // 3. 图标与文字颜色
        wxColour itemColour;
        if (isSelected) {
            itemColour = *wxWHITE;
        } else if (isHovered) {
            itemColour = palette.accentPrimary;
        } else {
            itemColour = palette.textSecondary;
        }

        // 4. 计算内容（图标 + 间距 + 文字）的居中位置
        const auto& item = m_items[i];
        gc->SetFont(isSelected ? boldFont : font, itemColour);

        double textW = 0, textH = 0;
        gc->GetTextExtent(item.label, &textW, &textH);

        int iconSz = 16_dip;
        int gap = 6_dip;
        bool hasIcon = (item.svgIcon != nullptr);

        double totalContentW = textW + (hasIcon ? (iconSz + gap) : 0);
        double startX = x + (w - totalContentW) / 2.0;

        if (hasIcon) {
            wxBitmapBundle bundle = IconManager::GetIconBundle(item.svgIcon, wxSize(16, 16), itemColour);
            wxBitmap bmp = bundle.GetBitmap(dip(16, 16));
            if (bmp.IsOk()) {
                double iconY = y + (h - bmp.GetHeight()) / 2.0;
                gc->DrawBitmap(bmp, startX, iconY, bmp.GetWidth(), bmp.GetHeight());
            }
            startX += iconSz + gap;
        }

        double textY = y + (h - textH) / 2.0;
        gc->DrawText(item.label, startX, textY);
    }
}

} // namespace LinguaAlpaca::UI
