#include "StatusBadge.hpp"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <memory>
#include <algorithm>

namespace LinguaAlpaca::UI {

StatusBadge::StatusBadge(wxWindow* parent, wxWindowID id,
                         const wxPoint& pos, const wxSize& size)
    : wxControl(parent, id, pos, size, wxBORDER_NONE) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &StatusBadge::OnPaint, this);
}

wxSize StatusBadge::DoGetBestSize() const {
    wxFont font(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei");
    wxCoord tw = 0, th = 0;
    GetTextExtent(m_label, &tw, &th, nullptr, nullptr, &font);
    int w = tw + 22_dip; // 左右各 11_dip 内边距
    int h = 26_dip;
    return wxSize(w, h);
}

void StatusBadge::SetStatus(const wxString& label, const wxColour& fg,
                           const wxColour& bg, const wxColour& border) {
    if (m_label == label && m_fgColour == fg && m_bgColour == bg && m_borderColour == border) {
        return;
    }
    m_label = label;
    m_fgColour = fg;
    m_bgColour = bg;
    m_borderColour = border;

    InvalidateBestSize();
    SetSize(DoGetBestSize());
    if (GetParent()) {
        GetParent()->Layout();
    }
    Refresh();
}

void StatusBadge::SetStatus(ServerHealthState state, const wxString& customLabel) {
    auto palette = ThemeColors::GetCurrentPalette();
    bool isDark = (palette.windowBg.Red() < 100);

    wxString label = customLabel;
    wxColour fg, bg, border;

    switch (state) {
    case ServerHealthState::Ready:
        if (label.IsEmpty()) label = L"●  已就绪";
        if (isDark) {
            fg = wxColour(74, 222, 128);      // 亮绿
            bg = wxColour(20, 83, 45, 180);   // 暗绿
            border = wxColour(34, 197, 94, 140);
        } else {
            fg = wxColour(22, 101, 52);       // 深绿
            bg = wxColour(240, 253, 244);     // 淡绿
            border = wxColour(187, 247, 208); // 柔绿边框
        }
        break;

    case ServerHealthState::Loading:
        if (label.IsEmpty()) label = L"●  正在加载模型...";
        if (isDark) {
            fg = wxColour(250, 204, 21);      // 亮琥珀
            bg = wxColour(113, 63, 18, 180);
            border = wxColour(202, 138, 4, 140);
        } else {
            fg = wxColour(161, 98, 7);        // 深琥珀
            bg = wxColour(254, 249, 195);     // 淡黄
            border = wxColour(253, 230, 138); // 柔黄边框
        }
        break;

    case ServerHealthState::Unconfigured:
        if (label.IsEmpty()) label = L"●  模型未配置";
        if (isDark) {
            fg = wxColour(248, 113, 113);     // 亮红
            bg = wxColour(127, 29, 29, 180);
            border = wxColour(220, 38, 38, 140);
        } else {
            fg = wxColour(220, 38, 38);       // 红色
            bg = wxColour(254, 242, 242);     // 淡红
            border = wxColour(254, 202, 202); // 柔红边框
        }
        break;

    case ServerHealthState::Offline:
        if (label.IsEmpty()) label = L"●  服务离线";
        if (isDark) {
            fg = wxColour(148, 163, 184);     // 浅石板灰
            bg = wxColour(51, 65, 85, 180);
            border = wxColour(71, 85, 105, 140);
        } else {
            fg = wxColour(100, 116, 139);     // 石板灰
            bg = wxColour(241, 245, 249);     // 浅灰
            border = wxColour(226, 232, 240); // 灰边框
        }
        break;

    case ServerHealthState::Error:
    default:
        if (label.IsEmpty()) label = L"●  服务异常";
        if (isDark) {
            fg = wxColour(248, 113, 113);
            bg = wxColour(127, 29, 29, 180);
            border = wxColour(220, 38, 38, 140);
        } else {
            fg = wxColour(220, 38, 38);
            bg = wxColour(254, 242, 242);
            border = wxColour(254, 202, 202);
        }
        break;
    }

    SetStatus(label, fg, bg, border);
}

void StatusBadge::OnPaint(wxPaintEvent& WXUNUSED(event)) {
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetClientSize();
    if (size.x <= 0 || size.y <= 0) return;

    // 清除背景 (使用父级窗口背景色)
    wxColour parentBg = GetParent() ? GetParent()->GetBackgroundColour() : ThemeColors::GetCurrentPalette().windowBg;
    if (!parentBg.IsOk()) {
        parentBg = ThemeColors::GetCurrentPalette().windowBg;
    }
    dc.SetBackground(wxBrush(parentBg));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;

    // 绘制药丸胶囊形状圆角背景
    double radius = size.y / 2.0;
    gc->SetBrush(gc->CreateBrush(wxBrush(m_bgColour)));
    if (m_borderColour.IsOk()) {
        gc->SetPen(gc->CreatePen(wxPen(m_borderColour, 1.0)));
    } else {
        gc->SetPen(*wxTRANSPARENT_PEN);
    }
    gc->DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, radius);

    // 绘制文字 (居中，并裁剪防止极端窄窗口溢出)
    wxFont font(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei");
    gc->SetFont(font, m_fgColour);

    double tw = 0, th = 0;
    gc->GetTextExtent(m_label, &tw, &th);

    double textX = (size.x - tw) / 2.0;
    if (textX < 8.0_dip) {
        textX = 8.0_dip;
    }
    double textY = (size.y - th) / 2.0;

    gc->Clip(4_dip, 1, size.x - 8_dip, size.y - 2);
    gc->DrawText(m_label, textX, textY);
    gc->ResetClip();
}

} // namespace LinguaAlpaca::UI
