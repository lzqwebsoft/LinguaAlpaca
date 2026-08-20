#include "SearchInputBox.hpp"
#include "../theme/IconManager.hpp"
#include "../theme/AppIcons.hpp"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>

namespace LinguaAlpaca::UI {

SearchInputBox::SearchInputBox(wxWindow* parent,
                               wxWindowID id,
                               const wxString& value,
                               const wxString& hint,
                               const wxPoint& pos,
                               const wxSize& size)
    : wxPanel(parent, id, pos, size, wxBORDER_NONE) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    InitUI(value, hint);
}

void SearchInputBox::InitUI(const wxString& value, const wxString& hint) {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // 左侧留出搜索图标间距 (34_dip)
    mainSizer->AddSpacer(36_dip);

    // 中间无边框文本框
    m_textCtrl = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxDefaultSize,
                                wxTE_PROCESS_ENTER | wxBORDER_NONE);
    m_textCtrl->SetHint(hint);
    m_textCtrl->SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_textCtrl->SetBackgroundColour(palette.cardBg);
    m_textCtrl->SetForegroundColour(palette.textPrimary);

    mainSizer->Add(m_textCtrl, 1, wxALIGN_CENTER_VERTICAL);

    // 右侧留出清除按钮间距 (32_dip)
    mainSizer->AddSpacer(32_dip);

    SetSizer(mainSizer);

    // 绑定事件
    Bind(wxEVT_PAINT, &SearchInputBox::OnPaint, this);
    Bind(wxEVT_SIZE, &SearchInputBox::OnSize, this);
    Bind(wxEVT_MOTION, &SearchInputBox::OnMouseMove, this);
    Bind(wxEVT_LEAVE_WINDOW, &SearchInputBox::OnMouseLeave, this);
    Bind(wxEVT_LEFT_DOWN, &SearchInputBox::OnLeftDown, this);

    m_textCtrl->Bind(wxEVT_TEXT, &SearchInputBox::OnTextChanged, this);
    m_textCtrl->Bind(wxEVT_TEXT_ENTER, &SearchInputBox::OnTextEnter, this);
    m_textCtrl->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
        m_isFocused = true;
        Refresh();
        event.Skip();
    });
    m_textCtrl->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        m_isFocused = false;
        Refresh();
        event.Skip();
    });
}

wxRect SearchInputBox::GetClearBtnRect() const {
    wxSize size = GetClientSize();
    int btnSize = 20_dip;
    int x = size.x - 26_dip - (btnSize / 2);
    int y = (size.y - btnSize) / 2;
    return wxRect(x, y, btnSize, btnSize);
}

void SearchInputBox::OnPaint(wxPaintEvent& WXUNUSED(event)) {
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetClientSize();
    if (size.x <= 0 || size.y <= 0) return;

    auto palette = ThemeColors::GetCurrentPalette();
    dc.SetBackground(wxBrush(GetParent() ? GetParent()->GetBackgroundColour() : palette.windowBg));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;

    // 1. 绘制圆角背景与外边框 (Radius: 8_dip)
    double radius = 8.0_dip;
    gc->SetBrush(gc->CreateBrush(wxBrush(palette.cardBg)));

    wxColour borderColor = m_isFocused ? palette.accentPrimary : (m_isHovered ? palette.cardBorderActive : palette.cardBorder);
    double borderWidth = m_isFocused ? 1.5 : 1.0;
    gc->SetPen(gc->CreatePen(wxPen(borderColor, borderWidth)));
    gc->DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, radius);

    // 2. 绘制左侧放大镜搜索图标
    wxColour iconColor = m_isFocused ? palette.accentPrimary : palette.textSecondary;
    wxSize iconSize = dip(16, 16);
    wxBitmapBundle searchBundle = IconManager::GetIconBundle(SVG::BROWSE, iconSize, iconColor);
    wxBitmap searchBmp = searchBundle.GetBitmap(iconSize);
    if (searchBmp.IsOk()) {
        int iconX = 12_dip;
        int iconY = (size.y - iconSize.y) / 2;
        gc->DrawBitmap(searchBmp, iconX, iconY, iconSize.x, iconSize.y);
    }

    // 3. 当有文本时，在右侧绘制 'x' 清除按钮
    if (m_textCtrl && !m_textCtrl->GetValue().IsEmpty()) {
        wxRect clearRect = GetClearBtnRect();

        // 悬浮时绘制微圆形背景高亮
        if (m_isClearHovered) {
            wxColour hoverBg = (ThemeColors::GetInstance().GetCurrentTheme() == ThemeMode::Light)
                ? wxColour(241, 245, 249) // #F1F5F9
                : wxColour(51, 65, 85);   // #334155
            gc->SetBrush(gc->CreateBrush(wxBrush(hoverBg)));
            gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 1.0)));
            gc->DrawRoundedRectangle(clearRect.x, clearRect.y, clearRect.width, clearRect.height, clearRect.width / 2.0);
        }

        wxColour clearColor = m_isClearHovered ? palette.accentPrimary : palette.textSecondary;
        wxSize clearIconSize = dip(12, 12);
        wxBitmapBundle closeBundle = IconManager::GetIconBundle(SVG::CLOSE, clearIconSize, clearColor);
        wxBitmap closeBmp = closeBundle.GetBitmap(clearIconSize);
        if (closeBmp.IsOk()) {
            int cx = clearRect.x + (clearRect.width - clearIconSize.x) / 2;
            int cy = clearRect.y + (clearRect.height - clearIconSize.y) / 2;
            gc->DrawBitmap(closeBmp, cx, cy, clearIconSize.x, clearIconSize.y);
        }
    }
}

void SearchInputBox::OnSize(wxSizeEvent& event) {
    Layout();
    Refresh();
    event.Skip();
}

void SearchInputBox::OnMouseMove(wxMouseEvent& event) {
    wxPoint pt = event.GetPosition();
    bool insideClear = false;

    if (m_textCtrl && !m_textCtrl->GetValue().IsEmpty()) {
        insideClear = GetClearBtnRect().Contains(pt);
    }

    if (insideClear != m_isClearHovered) {
        m_isClearHovered = insideClear;
        SetCursor(m_isClearHovered ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_ARROW));
        Refresh();
    }

    if (!m_isHovered) {
        m_isHovered = true;
        Refresh();
    }
}

void SearchInputBox::OnMouseLeave(wxMouseEvent& WXUNUSED(event)) {
    if (m_isHovered || m_isClearHovered) {
        m_isHovered = false;
        m_isClearHovered = false;
        SetCursor(wxCursor(wxCURSOR_ARROW));
        Refresh();
    }
}

void SearchInputBox::OnLeftDown(wxMouseEvent& event) {
    wxPoint pt = event.GetPosition();
    if (m_textCtrl && !m_textCtrl->GetValue().IsEmpty() && GetClearBtnRect().Contains(pt)) {
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

void SearchInputBox::OnTextChanged(wxCommandEvent& event) {
    Refresh(); // 触发清除按钮可见性更新与重绘

    // 向父容器派发文本改变事件
    wxCommandEvent parentEvent(wxEVT_TEXT, GetId());
    parentEvent.SetEventObject(this);
    parentEvent.SetString(event.GetString());
    ProcessWindowEvent(parentEvent);
}

void SearchInputBox::OnTextEnter(wxCommandEvent& event) {
    wxCommandEvent parentEvent(wxEVT_TEXT_ENTER, GetId());
    parentEvent.SetEventObject(this);
    parentEvent.SetString(event.GetString());
    ProcessWindowEvent(parentEvent);
}

wxString SearchInputBox::GetValue() const {
    return m_textCtrl ? m_textCtrl->GetValue() : wxString();
}

void SearchInputBox::SetValue(const wxString& value) {
    if (m_textCtrl) {
        m_textCtrl->SetValue(value);
        Refresh();
    }
}

void SearchInputBox::ChangeValue(const wxString& value) {
    if (m_textCtrl) {
        m_textCtrl->ChangeValue(value);
        Refresh();
    }
}

void SearchInputBox::Clear() {
    if (m_textCtrl) {
        m_textCtrl->Clear();
        m_textCtrl->SetFocus();
        Refresh();
    }
}

void SearchInputBox::SetHint(const wxString& hint) {
    if (m_textCtrl) {
        m_textCtrl->SetHint(hint);
    }
}

void SearchInputBox::SetFocus() {
    if (m_textCtrl) {
        m_textCtrl->SetFocus();
    } else {
        wxPanel::SetFocus();
    }
}

void SearchInputBox::UpdateTheme() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);
    if (m_textCtrl) {
        m_textCtrl->SetBackgroundColour(palette.cardBg);
        m_textCtrl->SetForegroundColour(palette.textPrimary);
        m_textCtrl->Refresh();
    }
    Refresh();
}

} // namespace LinguaAlpaca::UI
