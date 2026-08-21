#include "SuggestListBox.hpp"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <algorithm>

namespace LinguaAlpaca::UI {

SuggestListBox::SuggestListBox(wxWindow* parent,
                               wxWindowID id,
                               const wxPoint& pos,
                               const wxSize& size)
    : wxPanel(parent, id, pos, size, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    InitUI();
}

void SuggestListBox::InitUI() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.cardBg);

    m_scrollBar = new ScrollBar(this, [this](int line) {
        ScrollToItem(line);
    });

    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->AddStretchSpacer(1);
    sizer->Add(m_scrollBar, 0, wxEXPAND | wxTOP | wxBOTTOM, 4_dip);
    SetSizer(sizer);

    Bind(wxEVT_PAINT, &SuggestListBox::OnPaint, this);
    Bind(wxEVT_SIZE, &SuggestListBox::OnSize, this);
    Bind(wxEVT_MOTION, &SuggestListBox::OnMouseMove, this);
    Bind(wxEVT_LEAVE_WINDOW, &SuggestListBox::OnMouseLeave, this);
    Bind(wxEVT_LEFT_DOWN, &SuggestListBox::OnLeftDown, this);
    Bind(wxEVT_MOUSEWHEEL, &SuggestListBox::OnMouseWheel, this);
}

int SuggestListBox::GetItemAtPoint(const wxPoint& pt) const {
    int itemHeight = 34_dip;
    if (itemHeight <= 0 || m_items.empty()) return -1;

    int clientW = GetClientSize().GetWidth();
    if (pt.x < 2_dip || pt.x > clientW - 12_dip) return -1;

    int relIndex = pt.y / itemHeight;
    int actualIndex = m_firstVisibleIndex + relIndex;

    if (actualIndex >= 0 && actualIndex < static_cast<int>(m_items.size())) {
        return actualIndex;
    }
    return -1;
}

void SuggestListBox::UpdateScrollParams() {
    int itemHeight = 34_dip;
    int clientH = GetClientSize().GetHeight();
    if (itemHeight <= 0 || clientH <= 0) return;

    int visibleCount = std::max(1, clientH / itemHeight);
    int totalCount = static_cast<int>(m_items.size());

    int maxFirst = std::max(0, totalCount - visibleCount);
    m_firstVisibleIndex = std::clamp(m_firstVisibleIndex, 0, std::max(0, maxFirst));

    if (m_scrollBar) {
        m_scrollBar->SetScrollParams(m_firstVisibleIndex, visibleCount, totalCount);
    }
}

void SuggestListBox::ScrollToItem(int targetIndex) {
    int itemHeight = 34_dip;
    int clientH = GetClientSize().GetHeight();
    if (itemHeight <= 0 || clientH <= 0) return;

    int visibleCount = std::max(1, clientH / itemHeight);
    int totalCount = static_cast<int>(m_items.size());
    int maxFirst = std::max(0, totalCount - visibleCount);

    int newFirst = std::clamp(targetIndex, 0, std::max(0, maxFirst));
    if (newFirst != m_firstVisibleIndex) {
        m_firstVisibleIndex = newFirst;
        UpdateScrollParams();
        Refresh();
    }
}

void SuggestListBox::Clear() {
    m_items.clear();
    m_selectedIndex = -1;
    m_hoverIndex = -1;
    m_firstVisibleIndex = 0;
    UpdateScrollParams();
    Refresh();
}

void SuggestListBox::Append(const wxString& item) {
    m_items.push_back(item);
    UpdateScrollParams();
    Refresh();
}

void SuggestListBox::SetItems(const std::vector<std::string>& items) {
    m_items.clear();
    m_items.reserve(items.size());
    for (const auto& it : items) {
        m_items.push_back(wxString::FromUTF8(it));
    }
    m_selectedIndex = -1;
    m_hoverIndex = -1;
    m_firstVisibleIndex = 0;
    UpdateScrollParams();
    Refresh();
}

wxString SuggestListBox::GetString(int index) const {
    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        return m_items[index];
    }
    return wxString();
}

void SuggestListBox::SetSelection(int index) {
    if (m_selectedIndex != index) {
        m_selectedIndex = index;
        Refresh();
    }
}

void SuggestListBox::OnPaint(wxPaintEvent& WXUNUSED(event)) {
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetClientSize();
    if (size.x <= 0 || size.y <= 0) return;

    auto palette = ThemeColors::GetCurrentPalette();
    dc.SetBackground(wxBrush(palette.cardBg));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;

    gc->Clip(0, 0, size.x, size.y);

    int itemHeight = 34_dip;
    int visibleCount = (size.y / itemHeight) + 1;

    // 空状态提示
    if (m_items.empty()) {
        wxFont hintFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei");
        gc->SetFont(hintFont, palette.textSecondary);
        wxString hint = L"输入单词开始联想...";
        double tw = 0, th = 0;
        gc->GetTextExtent(hint, &tw, &th);
        gc->DrawText(hint, (size.x - tw) / 2.0, (size.y - th) / 2.0);
        return;
    }

    int endIndex = std::min(static_cast<int>(m_items.size()), m_firstVisibleIndex + visibleCount);
    int itemWidth = size.x - 16_dip;

    wxFont itemFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei");
    wxFont selectedFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei");

    for (int i = m_firstVisibleIndex; i < endIndex; ++i) {
        int y = (i - m_firstVisibleIndex) * itemHeight + 2_dip;
        if (y >= size.y) break;

        int h = itemHeight - 4_dip;
        if (y + h > size.y) {
            h = size.y - y;
            if (h <= 4) break;
        }

        int x = 4_dip;
        double radius = 6.0_dip;

        if (i == m_selectedIndex) {
            // 选中项高亮
            gc->SetBrush(gc->CreateBrush(wxBrush(palette.accentPrimary)));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->DrawRoundedRectangle(x, y, itemWidth, h, radius);

            gc->SetFont(selectedFont, *wxWHITE);
            gc->DrawText(m_items[i], x + 10_dip, y + (itemHeight - 4_dip - 14_dip) / 2);
        } else if (i == m_hoverIndex) {
            // 悬停项高亮
            gc->SetBrush(gc->CreateBrush(wxBrush(palette.bannerBg)));
            gc->SetPen(gc->CreatePen(wxPen(palette.bannerBorder, 1.0)));
            gc->DrawRoundedRectangle(x, y, itemWidth, h, radius);

            gc->SetFont(itemFont, palette.accentPrimary);
            gc->DrawText(m_items[i], x + 10_dip, y + (itemHeight - 4_dip - 14_dip) / 2);
        } else {
            // 普通项
            gc->SetFont(itemFont, palette.textPrimary);
            gc->DrawText(m_items[i], x + 10_dip, y + (itemHeight - 4_dip - 14_dip) / 2);
        }
    }
}

void SuggestListBox::OnSize(wxSizeEvent& event) {
    UpdateScrollParams();
    Refresh();
    event.Skip();
}

void SuggestListBox::OnMouseMove(wxMouseEvent& event) {
    wxPoint pt = event.GetPosition();
    int newHover = GetItemAtPoint(pt);

    if (newHover != m_hoverIndex) {
        m_hoverIndex = newHover;
        SetCursor(m_hoverIndex >= 0 ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_ARROW));
        Refresh();
    }
}

void SuggestListBox::OnMouseLeave(wxMouseEvent& WXUNUSED(event)) {
    if (m_hoverIndex != -1) {
        m_hoverIndex = -1;
        SetCursor(wxCursor(wxCURSOR_ARROW));
        Refresh();
    }
}

void SuggestListBox::OnLeftDown(wxMouseEvent& event) {
    wxPoint pt = event.GetPosition();
    int clickedIndex = GetItemAtPoint(pt);

    if (clickedIndex >= 0 && clickedIndex < static_cast<int>(m_items.size())) {
        SetSelection(clickedIndex);

        // 向上派发标准 wxEVT_LISTBOX 事件
        wxCommandEvent cmdEvent(wxEVT_LISTBOX, GetId());
        cmdEvent.SetEventObject(this);
        cmdEvent.SetInt(clickedIndex);
        cmdEvent.SetString(m_items[clickedIndex]);
        ProcessWindowEvent(cmdEvent);
    }
}

void SuggestListBox::OnMouseWheel(wxMouseEvent& event) {
    if (m_scrollBar) {
        m_scrollBar->NotifyActivity();
    }
    int lines = (event.GetWheelRotation() > 0) ? -3 : 3;
    ScrollToItem(m_firstVisibleIndex + lines);
}

void SuggestListBox::UpdateTheme() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.cardBg);
    if (m_scrollBar) {
        m_scrollBar->SetBackgroundColour(palette.cardBg);
        m_scrollBar->Refresh();
    }
    Refresh();
}

} // namespace LinguaAlpaca::UI
