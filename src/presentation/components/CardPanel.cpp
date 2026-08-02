#include "CardPanel.hpp"
#include <wx/graphics.h>
#include <wx/dcbuffer.h>

namespace LinguaAlpaca::Presentation::Components {

CardPanel::CardPanel(wxWindow* parent, const wxString& title, bool isActiveBorder, wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_title(title)
    , m_isActiveBorder(isActiveBorder) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    InitUI();
}

void CardPanel::InitUI() {
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    auto palette = Theme::ThemeColors::GetCurrentPalette();

    sizer->AddSpacer(42);

    long textStyle = wxTE_MULTILINE | wxBORDER_NONE;
    if (m_isActiveBorder) {
        textStyle |= wxTE_READONLY;
    }

    m_textCtrl = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, textStyle);
    m_textCtrl->SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_textCtrl->SetBackgroundColour(palette.cardBg);
    m_textCtrl->SetForegroundColour(m_isActiveBorder ? palette.accentPrimary : palette.textPrimary);

    sizer->Add(m_textCtrl, 1, wxEXPAND | wxLEFT | wxRIGHT, 14);
    sizer->AddSpacer(32);

    SetSizer(sizer);

    Bind(wxEVT_PAINT, &CardPanel::OnPaint, this);
    Bind(wxEVT_MOTION, &CardPanel::OnMouseMove, this);
    Bind(wxEVT_LEFT_DOWN, &CardPanel::OnLeftDown, this);
}

void CardPanel::UpdateTheme() {
    auto palette = Theme::ThemeColors::GetCurrentPalette();
    if (m_textCtrl) {
        m_textCtrl->SetBackgroundColour(palette.cardBg);
        m_textCtrl->SetForegroundColour(m_isActiveBorder ? palette.accentPrimary : palette.textPrimary);
        m_textCtrl->Refresh();
    }
    Refresh();
}

void CardPanel::AddToolIcon(int id, const wxString& iconStr, const wxString& tooltip, std::function<void()> onClick) {
    m_tools.push_back({ id, iconStr, tooltip, onClick });
    Refresh();
}

void CardPanel::SetCharacterCount(size_t count) {
    if (m_charCount != count) {
        m_charCount = count;
        Refresh();
    }
}

void CardPanel::OnPaint(wxPaintEvent& WXUNUSED(event)) {
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetClientSize();
    if (size.x <= 0 || size.y <= 0) return;

    auto palette = Theme::ThemeColors::GetCurrentPalette();
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;

    // 1. 绘制圆角卡片背景与边框
    double radius = 12.0;
    gc->SetBrush(gc->CreateBrush(wxBrush(palette.cardBg)));
    
    wxColour borderColor = m_isActiveBorder ? palette.cardBorderActive : palette.cardBorder;
    double borderWidth = m_isActiveBorder ? 1.5 : 1.0;
    gc->SetPen(gc->CreatePen(wxPen(borderColor, borderWidth)));
    gc->DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, radius);

    // 2. 绘制 Card Header Title
    wxFont titleFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei");
    gc->SetFont(titleFont, m_isActiveBorder ? palette.accentPrimary : palette.textPrimary);
    gc->DrawText(m_title, 16, 12);

    // 3. 绘制右侧工具图标 (📄, 🧹, 🔊 等)
    int toolX = size.x - 24;
    wxFont toolFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI Emoji");

    for (int i = (int)m_tools.size() - 1; i >= 0; --i) {
        double tw, th;
        gc->GetTextExtent(m_tools[i].iconStr, &tw, &th);
        toolX -= tw;

        wxColour toolColor = (m_hoverToolIndex == i) ? palette.accentPrimary : palette.textSecondary;
        gc->SetFont(toolFont, toolColor);
        gc->DrawText(m_tools[i].iconStr, toolX, 12);

        toolX -= 14;
    }

    // 4. 绘制 Footer 字符数
    wxFont countFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei");
    gc->SetFont(countFont, palette.textSecondary);
    
    wxString countText = wxString::Format(L"%zu 字符", m_charCount);
    double cw, ch;
    gc->GetTextExtent(countText, &cw, &ch);
    gc->DrawText(countText, size.x - cw - 16, size.y - ch - 10);
}

void CardPanel::OnMouseMove(wxMouseEvent& event) {
    int x = event.GetX();
    int y = event.GetY();
    int sizeX = GetClientSize().x;

    int oldHover = m_hoverToolIndex;
    m_hoverToolIndex = -1;

    if (y >= 8 && y <= 32) {
        int toolX = sizeX - 24;
        wxClientDC dc(this);
        dc.SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI Emoji"));

        for (int i = (int)m_tools.size() - 1; i >= 0; --i) {
            wxSize extent = dc.GetTextExtent(m_tools[i].iconStr);
            toolX -= extent.x;

            if (x >= toolX - 4 && x <= toolX + extent.x + 4) {
                m_hoverToolIndex = i;
                break;
            }
            toolX -= 14;
        }
    }

    if (oldHover != m_hoverToolIndex) {
        SetCursor(m_hoverToolIndex != -1 ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_DEFAULT));
        Refresh();
    }
}

void CardPanel::OnLeftDown(wxMouseEvent& WXUNUSED(event)) {
    if (m_hoverToolIndex >= 0 && m_hoverToolIndex < (int)m_tools.size()) {
        if (m_tools[m_hoverToolIndex].onClick) {
            m_tools[m_hoverToolIndex].onClick();
        }
    }
}

} // namespace LinguaAlpaca::Presentation::Components
