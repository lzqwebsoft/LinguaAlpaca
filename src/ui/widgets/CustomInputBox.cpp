#pragma execution_character_set("utf-8")
#include "CustomInputBox.hpp"
#include "../theme/AppIcons.hpp"
#include "../theme/IconManager.hpp"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>

namespace LinguaAlpaca::UI {

CustomInputBox::CustomInputBox(wxWindow *parent, wxWindowID id,
                               const wxString &value, const wxString &hint,
                               const wxPoint &pos, const wxSize &size,
                               long style)
    : wxPanel(parent, id, pos, size, wxBORDER_NONE), m_hint(hint),
      m_isMultiline((style & wxTE_MULTILINE) != 0) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  InitUI(value, hint, style);
}

void CustomInputBox::InitUI(const wxString &value, const wxString &hint,
                            long style) {
  auto palette = ThemeColors::GetCurrentPalette();
  SetBackgroundColour(palette.cardBg);

  m_mainSizer = new wxBoxSizer(wxVERTICAL);

  long innerStyle =
      (style & (wxTE_READONLY | wxTE_PASSWORD | wxTE_MULTILINE | wxTE_RICH2)) |
      wxBORDER_NONE;
  if (!m_isMultiline) {
    innerStyle |= wxTE_PROCESS_ENTER;
  }

  m_textCtrl = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition,
                              wxDefaultSize, innerStyle);
  if (!hint.IsEmpty()) {
    m_textCtrl->SetHint(hint);
  }
  m_textCtrl->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                             wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
  m_textCtrl->SetBackgroundColour(palette.cardBg);
  m_textCtrl->SetForegroundColour(palette.textPrimary);

  RebuildLayout();

  // 绑定事件
  Bind(wxEVT_PAINT, &CustomInputBox::OnPaint, this);
  Bind(wxEVT_SIZE, &CustomInputBox::OnSize, this);
  Bind(wxEVT_MOTION, &CustomInputBox::OnMouseMove, this);
  Bind(wxEVT_LEAVE_WINDOW, &CustomInputBox::OnMouseLeave, this);
  Bind(wxEVT_LEFT_DOWN, &CustomInputBox::OnLeftDown, this);

  m_textCtrl->Bind(wxEVT_TEXT, &CustomInputBox::OnTextChanged, this);
  if (m_textCtrl->HasFlag(wxTE_PROCESS_ENTER)) {
    m_textCtrl->Bind(wxEVT_TEXT_ENTER, &CustomInputBox::OnTextEnter, this);
  }

  m_textCtrl->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &event) {
    m_isFocused = true;
    Refresh();
    event.Skip();
  });

  m_textCtrl->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &event) {
    m_isFocused = false;
    Refresh();
    event.Skip();
  });
}

void CustomInputBox::RebuildLayout() {
  if (!m_textCtrl)
    return;

  if (m_inputSizer) {
    m_mainSizer->Detach(m_inputSizer);
    delete m_inputSizer;
    m_inputSizer = nullptr;
  }

  m_inputSizer = new wxBoxSizer(wxHORIZONTAL);

  if (m_isMultiline) {
    m_inputSizer->Add(m_textCtrl, 1, wxEXPAND | wxALL, 8_dip);
  } else {
    // 左侧间距：若有前缀图标留 34_dip，否则留 12_dip
    int leftPad = m_prefixSvg ? 34_dip : 12_dip;
    m_inputSizer->AddSpacer(leftPad);

    m_inputSizer->Add(m_textCtrl, 1, wxALIGN_CENTER_VERTICAL);

    // 右侧间距：若开启清除按钮留 30_dip，否则留 12_dip
    int rightPad = m_showClearButton ? 30_dip : 12_dip;
    m_inputSizer->AddSpacer(rightPad);
  }

  m_mainSizer->Add(m_inputSizer, 1, wxEXPAND);
  SetSizer(m_mainSizer);
  Layout();
}

wxRect CustomInputBox::GetClearBtnRect() const {
  if (m_isMultiline || !m_showClearButton)
    return wxRect();

  wxSize size = GetClientSize();
  int btnSize = 18_dip;
  int x = size.x - 22_dip - (btnSize / 2);
  int y = (size.y - btnSize) / 2;
  return wxRect(x, y, btnSize, btnSize);
}

void CustomInputBox::OnPaint(wxPaintEvent &WXUNUSED(event)) {
  wxAutoBufferedPaintDC dc(this);
  wxSize size = GetClientSize();
  if (size.x <= 0 || size.y <= 0)
    return;

  auto palette = ThemeColors::GetCurrentPalette();
  wxColour parentBg =
      GetParent() ? GetParent()->GetBackgroundColour() : palette.cardBg;
  dc.SetBackground(wxBrush(parentBg));
  dc.Clear();

  std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
  if (!gc)
    return;

  // 1. 绘制圆角背景与外边框 (背景底色使用白色 cardBg)
  double radius = m_cornerRadius * (double)dip(1, 1).y;
  gc->SetBrush(gc->CreateBrush(wxBrush(palette.cardBg)));

  wxColour borderColor = m_isFocused ? palette.accentPrimary
                                     : (m_isHovered ? palette.cardBorderActive
                                                    : palette.cardBorder);
  double borderWidth = m_isFocused ? 1.5_dip : 1.0_dip;
  gc->SetPen(gc->CreatePen(wxPen(borderColor, borderWidth)));
  gc->DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, radius);

  // 2. 绘制前缀 SVG 图标 (仅单行模式支持)
  if (!m_isMultiline && m_prefixSvg) {
    wxColour iconColor =
        m_isFocused ? palette.accentPrimary : palette.textSecondary;
    wxBitmapBundle iconBundle =
        IconManager::GetIconBundle(m_prefixSvg, m_prefixIconSize, iconColor);
    wxBitmap iconBmp = iconBundle.GetBitmap(m_prefixIconSize);
    if (iconBmp.IsOk()) {
      int iconX = 11_dip;
      int iconY = (size.y - m_prefixIconSize.y) / 2;
      gc->DrawBitmap(iconBmp, iconX, iconY, m_prefixIconSize.x,
                     m_prefixIconSize.y);
    }
  }

  // 3. 绘制右侧快速清除 'x' 按钮 (仅单行非空时显示)
  if (!m_isMultiline && m_showClearButton && m_textCtrl &&
      !m_textCtrl->GetValue().IsEmpty()) {
    wxRect clearRect = GetClearBtnRect();

    if (m_isClearHovered) {
      wxColour hoverBg =
          (ThemeColors::GetInstance().GetCurrentTheme() == ThemeMode::Light)
              ? wxColour(241, 245, 249) // #F1F5F9
              : wxColour(51, 65, 85);   // #334155
      gc->SetBrush(gc->CreateBrush(wxBrush(hoverBg)));
      gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 1.0)));
      gc->DrawRoundedRectangle(clearRect.x, clearRect.y, clearRect.width,
                               clearRect.height, clearRect.width / 2.0);
    }

    wxColour clearColor =
        m_isClearHovered ? palette.accentPrimary : palette.textSecondary;
    wxSize clearIconSize = dip(11, 11);
    wxBitmapBundle closeBundle =
        IconManager::GetIconBundle(SVG::CLOSE, clearIconSize, clearColor);
    wxBitmap closeBmp = closeBundle.GetBitmap(clearIconSize);
    if (closeBmp.IsOk()) {
      int cx = clearRect.x + (clearRect.width - clearIconSize.x) / 2;
      int cy = clearRect.y + (clearRect.height - clearIconSize.y) / 2;
      gc->DrawBitmap(closeBmp, cx, cy, clearIconSize.x, clearIconSize.y);
    }
  }
}

void CustomInputBox::OnSize(wxSizeEvent &event) {
  Layout();
  Refresh();
  event.Skip();
}

void CustomInputBox::OnMouseMove(wxMouseEvent &event) {
  wxPoint pt = event.GetPosition();
  bool insideClear = false;

  if (!m_isMultiline && m_showClearButton && m_textCtrl &&
      !m_textCtrl->GetValue().IsEmpty()) {
    insideClear = GetClearBtnRect().Contains(pt);
  }

  if (insideClear != m_isClearHovered) {
    m_isClearHovered = insideClear;
    SetCursor(m_isClearHovered ? wxCursor(wxCURSOR_HAND)
                               : wxCursor(wxCURSOR_ARROW));
    Refresh();
  }

  if (!m_isHovered) {
    m_isHovered = true;
    Refresh();
  }
}

void CustomInputBox::OnMouseLeave(wxMouseEvent &WXUNUSED(event)) {
  if (m_isHovered || m_isClearHovered) {
    m_isHovered = false;
    m_isClearHovered = false;
    SetCursor(wxCursor(wxCURSOR_ARROW));
    Refresh();
  }
}

void CustomInputBox::OnLeftDown(wxMouseEvent &event) {
  wxPoint pt = event.GetPosition();
  if (!m_isMultiline && m_showClearButton && m_textCtrl &&
      !m_textCtrl->GetValue().IsEmpty() && GetClearBtnRect().Contains(pt)) {
    Clear();
    if (m_onClearCallback) {
      m_onClearCallback();
    }
    return;
  }

  if (m_textCtrl) {
    m_textCtrl->SetFocus();
  }
}

void CustomInputBox::OnTextChanged(wxCommandEvent &event) {
  Refresh();

  wxCommandEvent parentEvent(wxEVT_TEXT, GetId());
  parentEvent.SetEventObject(this);
  parentEvent.SetString(event.GetString());
  ProcessWindowEvent(parentEvent);
}

void CustomInputBox::OnTextEnter(wxCommandEvent &event) {
  wxCommandEvent parentEvent(wxEVT_TEXT_ENTER, GetId());
  parentEvent.SetEventObject(this);
  parentEvent.SetString(event.GetString());
  ProcessWindowEvent(parentEvent);
}

wxString CustomInputBox::GetValue() const {
  return m_textCtrl ? m_textCtrl->GetValue() : wxString();
}

void CustomInputBox::SetValue(const wxString &value) {
  if (m_textCtrl) {
    m_textCtrl->SetValue(value);
    Refresh();
  }
}

void CustomInputBox::ChangeValue(const wxString &value) {
  if (m_textCtrl) {
    m_textCtrl->ChangeValue(value);
    Refresh();
  }
}

void CustomInputBox::Clear() {
  if (m_textCtrl) {
    m_textCtrl->Clear();
    m_textCtrl->SetFocus();
    Refresh();

    wxCommandEvent parentEvent(wxEVT_TEXT, GetId());
    parentEvent.SetEventObject(this);
    parentEvent.SetString(wxEmptyString);
    ProcessWindowEvent(parentEvent);
  }
}

void CustomInputBox::SetHint(const wxString &hint) {
  m_hint = hint;
  if (m_textCtrl) {
    m_textCtrl->SetHint(hint);
  }
}

void CustomInputBox::AppendText(const wxString &text) {
  if (m_textCtrl) {
    m_textCtrl->AppendText(text);
    Refresh();
  }
}

void CustomInputBox::SetEditable(bool editable) {
  if (m_textCtrl) {
    m_textCtrl->SetEditable(editable);
  }
}

bool CustomInputBox::IsEditable() const {
  return m_textCtrl ? m_textCtrl->IsEditable() : false;
}

void CustomInputBox::SetFocus() {
  if (m_textCtrl) {
    m_textCtrl->SetFocus();
  } else {
    wxPanel::SetFocus();
  }
}

void CustomInputBox::SetInsertionPoint(long pos) {
  if (m_textCtrl) {
    m_textCtrl->SetInsertionPoint(pos);
  }
}

void CustomInputBox::SetInsertionPointEnd() {
  if (m_textCtrl) {
    m_textCtrl->SetInsertionPointEnd();
  }
}

long CustomInputBox::GetInsertionPoint() const {
  return m_textCtrl ? m_textCtrl->GetInsertionPoint() : 0;
}

void CustomInputBox::SelectAll() {
  if (m_textCtrl) {
    m_textCtrl->SelectAll();
  }
}

void CustomInputBox::SetPrefixIcon(const char *svgContent, const wxSize &size) {
  m_prefixSvg = svgContent;
  m_prefixIconSize = size;
  RebuildLayout();
  Refresh();
}

void CustomInputBox::SetShowClearButton(bool show) {
  if (m_showClearButton != show) {
    m_showClearButton = show;
    RebuildLayout();
    Refresh();
  }
}

bool CustomInputBox::SetFont(const wxFont &font) {
  bool res = wxPanel::SetFont(font);
  if (m_textCtrl) {
    m_textCtrl->SetFont(font);
  }
  return res;
}

bool CustomInputBox::SetBackgroundColour(const wxColour &colour) {
  bool res = wxPanel::SetBackgroundColour(colour);
  if (m_textCtrl) {
    m_textCtrl->SetBackgroundColour(colour);
  }
  Refresh();
  return res;
}

bool CustomInputBox::SetForegroundColour(const wxColour &colour) {
  bool res = wxPanel::SetForegroundColour(colour);
  if (m_textCtrl) {
    m_textCtrl->SetForegroundColour(colour);
  }
  Refresh();
  return res;
}

void CustomInputBox::UpdateTheme() {
  auto palette = ThemeColors::GetCurrentPalette();
  SetBackgroundColour(palette.cardBg);
  if (m_textCtrl) {
    m_textCtrl->SetBackgroundColour(palette.cardBg);
    m_textCtrl->SetForegroundColour(palette.textPrimary);
    m_textCtrl->Refresh();
  }
  Refresh();
}

} // namespace LinguaAlpaca::UI
