#include "CustomButton.hpp"
#include "../theme/IconManager.hpp"
#include <algorithm>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>

namespace LinguaAlpaca::UI {

CustomButton::CustomButton(wxWindow *parent, wxWindowID id,
                           const wxString &label, ButtonStyle style,
                           const wxPoint &pos, const wxSize &size)
    : wxControl(parent, id, pos, size, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE), m_label(label),
      m_buttonStyle(style), m_explicitSize(size) {

  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetCursor(wxCursor(wxCURSOR_HAND));

  if (size.x > 0 && size.y > 0) {
    SetMinSize(size);
    SetMaxSize(size);
    SetSize(size);
  }

  Bind(wxEVT_PAINT, &CustomButton::OnPaint, this);
  Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
    Refresh();
    event.Skip();
  });
  Bind(wxEVT_ENTER_WINDOW, &CustomButton::OnMouseEnter, this);
  Bind(wxEVT_LEAVE_WINDOW, &CustomButton::OnMouseLeave, this);
  Bind(wxEVT_LEFT_DOWN, &CustomButton::OnLeftDown, this);
  Bind(wxEVT_LEFT_UP, &CustomButton::OnLeftUp, this);
}

void CustomButton::SetLabel(const wxString &label) {
  m_label = label;
  InvalidateBestSize();
  if (m_explicitSize.x <= 0 || m_explicitSize.y <= 0) {
    wxSize best = DoGetBestSize();
    SetMinSize(best);
    SetSize(best);
  }
  if (GetParent()) {
    GetParent()->Layout();
  }
  Refresh();
}

void CustomButton::SetIcon(const char *svgContent, const wxSize &iconSize,
                           const wxColour &tintColor) {
  m_svgContent = svgContent;
  m_iconReqSize = iconSize;
  m_tintColor = tintColor;
  m_iconBundle =
      IconManager::GetIconBundle(svgContent, iconSize, tintColor);
  InvalidateBestSize();
  if (m_explicitSize.x <= 0 || m_explicitSize.y <= 0) {
    wxSize best = DoGetBestSize();
    SetMinSize(best);
    SetSize(best);
  }
  if (GetParent()) {
    GetParent()->Layout();
  }
  Refresh();
}

wxSize CustomButton::DoGetBestSize() const {
  if (m_explicitSize.x > 0 && m_explicitSize.y > 0) {
    return m_explicitSize;
  }
  if (m_label.IsEmpty()) {
    int s = 28_dip;
    return wxSize(s, s);
  }
  wxClientDC dc(const_cast<CustomButton *>(this));
  wxFont font = GetFont().IsOk() ? GetFont() : ThemeFont::GetFont(FontRole::Control, true);
  dc.SetFont(font);
  wxSize extent = dc.GetTextExtent(m_label);
  int iconW = m_iconBundle.IsOk() ? (16_dip + 8_dip) : 0;
  return wxSize(extent.x + 44_dip + iconW, 42_dip);
}

void CustomButton::OnPaint(wxPaintEvent &WXUNUSED(event)) {
  wxAutoBufferedPaintDC dc(this);
  wxSize size = GetClientSize();
  if (size.x <= 0 || size.y <= 0)
    return;

  auto palette = ThemeColors::GetCurrentPalette();
  dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
  dc.Clear();

  std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
  if (!gc)
    return;

  wxColour bgColour;
  wxColour textColour;
  wxColour borderColour = wxNullColour;

  if (!IsEnabled()) {
    bgColour = palette.cardBg;
    textColour = palette.textSecondary;
    borderColour = palette.cardBorder;
  } else {
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
      bgColour = m_isHovered ? wxColour(220, 38, 38)
                             : wxColour(239, 68, 68); // 鲜艳警示红
      textColour = *wxWHITE;
      break;
    case ButtonStyle::Close:
      // 关闭按钮常态极简隐蔽，悬停时呈现圆角警示红
      bgColour = m_isHovered
                     ? (m_isPressed ? wxColour(220, 38, 38) : wxColour(239, 68, 68))
                     : palette.sidebarBg;
      textColour = m_isHovered ? *wxWHITE : palette.textSecondary;
      borderColour = wxNullColour;
      break;
    case ButtonStyle::Secondary:
      bool isIconOnly = m_label.IsEmpty();
      if (isIconOnly) {
        // 无文字纯图标控制按钮（如最小化/最大化），常态融入底色，悬停呈现轻柔遮罩
        bool isLight = (palette.sidebarBg.Red() > 128);
        bgColour = m_isHovered
                       ? (isLight ? wxColour(0, 0, 0, m_isPressed ? 35 : 18)
                                  : wxColour(255, 255, 255, m_isPressed ? 45 : 24))
                       : palette.sidebarBg;
        textColour = m_isHovered ? palette.textPrimary : palette.textSecondary;
        borderColour = wxNullColour;
      } else {
        bool isLight = (palette.sidebarBg.Red() > 128);
        if (m_isHovered) {
          bgColour = m_isPressed
                         ? (isLight ? wxColour(226, 232, 240) : wxColour(71, 85, 105))
                         : (isLight ? wxColour(241, 245, 249) : wxColour(51, 65, 85));
          borderColour = palette.cardBorderActive;
          textColour = palette.textPrimary;
        } else {
          bgColour = palette.cardBg;
          borderColour = palette.cardBorder;
          textColour = palette.textPrimary;
        }
      }
      break;
    }
  }

  // 圆角矩形绘制 (小尺寸控件自动调整圆角半径，边框内嵌 halfPen 避免右/下边缘被裁切)
  double radius = (size.y < 32_dip) ? 6.0_dip : 10.0_dip;
  gc->SetBrush(gc->CreateBrush(wxBrush(bgColour)));
  if (borderColour.IsOk()) {
    double penWidth = 1.0;
    double halfPen = penWidth / 2.0;
    gc->SetPen(gc->CreatePen(wxPen(borderColour, penWidth)));
    gc->DrawRoundedRectangle(halfPen, halfPen, size.x - penWidth, size.y - penWidth, radius);
  } else {
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRoundedRectangle(0, 0, size.x, size.y, radius);
  }

  // 绘制 SVG 图标与文字
  wxFont font = GetFont().IsOk() ? GetFont() : ThemeFont::GetFont(FontRole::Control, true);
  gc->SetFont(font, textColour);

  double tw = 0, th = 0;
  if (!m_label.IsEmpty()) {
    gc->GetTextExtent(m_label, &tw, &th);
  }

  double iconW = 0, iconH = 0;
  wxBitmap bmp;
  if (m_svgContent) {
    wxSize reqSize = (size.y <= 30_dip) ? dip(13, 13) : m_iconReqSize;
    wxColour effectiveIconColor = m_tintColor;
    if (m_buttonStyle == ButtonStyle::Close) {
      effectiveIconColor = m_isHovered ? *wxWHITE : (m_tintColor.IsOk() ? m_tintColor : palette.textSecondary);
    } else if (m_buttonStyle == ButtonStyle::Secondary && m_label.IsEmpty()) {
      effectiveIconColor = m_isHovered ? palette.textPrimary : (m_tintColor.IsOk() ? m_tintColor : palette.textSecondary);
    } else if (!effectiveIconColor.IsOk()) {
      effectiveIconColor = textColour;
    }
    wxBitmapBundle bundle = IconManager::GetIconBundle(m_svgContent, reqSize, effectiveIconColor);
    bmp = bundle.GetBitmap(reqSize);
    if (bmp.IsOk()) {
      iconW = bmp.GetWidth();
      iconH = bmp.GetHeight();
    }
  } else if (m_iconBundle.IsOk()) {
    wxSize reqIconSize = (size.y <= 30_dip) ? dip(13, 13) : dip(16, 16);
    bmp = m_iconBundle.GetBitmap(reqIconSize);
    if (bmp.IsOk()) {
      iconW = bmp.GetWidth();
      iconH = bmp.GetHeight();
    }
  }

  double spacing = (iconW > 0 && tw > 0) ? 6.0_dip : 0.0;
  double totalW = iconW + spacing + tw;
  double startX = std::max(0.0, (size.x - totalW) / 2.0);

  if (bmp.IsOk()) {
    double iconY = (size.y - iconH) / 2.0;
    gc->DrawBitmap(bmp, startX, iconY, iconW, iconH);
    startX += iconW + spacing;
  }

  if (!m_label.IsEmpty()) {
    double textY = (size.y - th) / 2.0;
    if (textY < 1.0)
      textY = 1.0;
    gc->DrawText(m_label, startX, textY);
  }
}

void CustomButton::OnMouseEnter(wxMouseEvent &WXUNUSED(event)) {
  if (!IsEnabled()) {
    SetCursor(wxCursor(wxCURSOR_ARROW));
    return;
  }
  m_isHovered = true;
  SetCursor(wxCursor(wxCURSOR_HAND));
  Refresh();
}

void CustomButton::OnMouseLeave(wxMouseEvent &WXUNUSED(event)) {
  m_isHovered = false;
  m_isPressed = false;
  SetCursor(wxCursor(wxCURSOR_DEFAULT));
  Refresh();
}

void CustomButton::OnLeftDown(wxMouseEvent &WXUNUSED(event)) {
  if (!IsEnabled()) return;
  m_isPressed = true;
  Refresh();
}

void CustomButton::OnLeftUp(wxMouseEvent &event) {
  if (!IsEnabled()) {
    m_isPressed = false;
    return;
  }
  if (m_isPressed) {
    m_isPressed = false;
    Refresh();

    // 触发按钮事件
    wxCommandEvent evt(wxEVT_BUTTON, GetId());
    evt.SetEventObject(this);
    ProcessWindowEvent(evt);
  }
}

} // namespace LinguaAlpaca::UI
