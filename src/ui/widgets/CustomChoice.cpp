#include "CustomChoice.hpp"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/display.h>
#include <algorithm>

namespace LinguaAlpaca::UI {

// ============================================================================
// CustomChoicePopup 实现
// ============================================================================

CustomChoicePopup::CustomChoicePopup(CustomChoice* owner)
    : wxPopupTransientWindow(owner, wxPU_CONTAINS_CONTROLS | wxBORDER_NONE),
      m_owner(owner) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    InitUI();
}

void CustomChoicePopup::InitUI() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.cardBg);

    m_scrollBar = new ScrollBar(this, [this](int line) {
        ScrollToItem(line);
    });

    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->AddStretchSpacer(1);
    sizer->Add(m_scrollBar, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4_dip);
    SetSizer(sizer);

    Bind(wxEVT_PAINT, &CustomChoicePopup::OnPaint, this);
    Bind(wxEVT_SIZE, &CustomChoicePopup::OnSize, this);
    Bind(wxEVT_MOTION, &CustomChoicePopup::OnMouseMove, this);
    Bind(wxEVT_LEAVE_WINDOW, &CustomChoicePopup::OnMouseLeave, this);
    Bind(wxEVT_LEFT_DOWN, &CustomChoicePopup::OnLeftDown, this);
    Bind(wxEVT_MOUSEWHEEL, &CustomChoicePopup::OnMouseWheel, this);
    Bind(wxEVT_KEY_DOWN, &CustomChoicePopup::OnKeyDown, this);
}

void CustomChoicePopup::SetItems(const std::vector<ChoiceItem>& items, int selection) {
    m_items = items;
    m_selectedIndex = selection;
    m_hoverIndex = selection;
    m_firstVisibleIndex = 0;

    // 默认让当前选中项滚动在可见区域内
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_items.size())) {
        int visibleCount = 6;
        if (m_selectedIndex >= visibleCount) {
            m_firstVisibleIndex = m_selectedIndex - visibleCount + 1;
        }
    }
}

void CustomChoicePopup::ShowPopup(const wxPoint& pos, const wxSize& size) {
    SetSize(pos.x, pos.y, size.x, size.y);
    Layout();
    UpdateScrollParams();
    Popup(this);
    Refresh();
}

void CustomChoicePopup::OnDismiss() {
    if (m_owner) {
        m_owner->OnPopupDismissed();
    }
}

bool CustomChoicePopup::ProcessLeftDown(wxMouseEvent& event) {
    wxPoint pt = event.GetPosition();
    if (GetClientRect().Contains(pt)) {
        return false;
    }
    return wxPopupTransientWindow::ProcessLeftDown(event);
}

int CustomChoicePopup::GetItemAtPoint(const wxPoint& pt) const {
    int itemHeight = 32_dip;
    if (itemHeight <= 0 || m_items.empty()) return -1;

    int clientW = GetClientSize().GetWidth();
    if (pt.x < 2_dip || pt.x > clientW - 8_dip) return -1;
    if (pt.y < 4_dip) return -1;

    int relIndex = (pt.y - 4_dip) / itemHeight;
    int actualIndex = m_firstVisibleIndex + relIndex;

    if (actualIndex >= 0 && actualIndex < static_cast<int>(m_items.size())) {
        return actualIndex;
    }
    return -1;
}

void CustomChoicePopup::UpdateScrollParams() {
    int itemHeight = 32_dip;
    int clientH = GetClientSize().GetHeight() - 8_dip;
    if (itemHeight <= 0 || clientH <= 0) return;

    int visibleCount = std::max(1, clientH / itemHeight);
    int totalCount = static_cast<int>(m_items.size());

    int maxFirst = std::max(0, totalCount - visibleCount);
    m_firstVisibleIndex = std::clamp(m_firstVisibleIndex, 0, std::max(0, maxFirst));

    bool needsScroll = (totalCount > visibleCount);
    if (m_scrollBar) {
        if (needsScroll) {
            m_scrollBar->Show();
            m_scrollBar->SetScrollParams(m_firstVisibleIndex, visibleCount, totalCount);
        } else {
            m_scrollBar->Hide();
        }
    }
}

void CustomChoicePopup::ScrollToItem(int targetIndex) {
    int itemHeight = 32_dip;
    int clientH = GetClientSize().GetHeight() - 8_dip;
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

void CustomChoicePopup::OnPaint(wxPaintEvent& WXUNUSED(event)) {
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetClientSize();
    if (size.x <= 0 || size.y <= 0) return;

    auto palette = ThemeColors::GetCurrentPalette();
    dc.SetBackground(wxBrush(palette.cardBg));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;

    // 1. 绘制背景与外边框
    double radius = 6.0_dip;
    gc->SetBrush(gc->CreateBrush(wxBrush(palette.cardBg)));
    gc->SetPen(gc->CreatePen(wxPen(palette.cardBorderActive, 1.2)));
    gc->DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, radius);

    // 2. 绘制选项列表
    int itemHeight = 32_dip;
    int visibleCount = std::max(1, (size.y - 8_dip) / itemHeight);
    int endIndex = std::min(static_cast<int>(m_items.size()), m_firstVisibleIndex + visibleCount + 1);

    bool needsScroll = (static_cast<int>(m_items.size()) > visibleCount);
    int itemWidth = size.x - (needsScroll ? 18_dip : 8_dip);

    wxFont itemFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei");
    wxFont selectedFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei");

    for (int i = m_firstVisibleIndex; i < endIndex; ++i) {
        int y = 4_dip + (i - m_firstVisibleIndex) * itemHeight;
        if (y + itemHeight - 2_dip > size.y - 4_dip) break;

        int h = itemHeight - 2_dip;
        int x = 4_dip;
        double itemRadius = 4.0_dip;

        if (i == m_selectedIndex) {
            // 选中项背景高亮
            gc->SetBrush(gc->CreateBrush(wxBrush(palette.accentPrimary)));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->DrawRoundedRectangle(x, y, itemWidth, h, itemRadius);

            gc->SetFont(selectedFont, *wxWHITE);
            gc->DrawText(m_items[i].text, x + 10_dip, y + (h - 14_dip) / 2.0);
        } else if (i == m_hoverIndex) {
            // 悬浮项背景高亮
            gc->SetBrush(gc->CreateBrush(wxBrush(palette.bannerBg)));
            gc->SetPen(gc->CreatePen(wxPen(palette.bannerBorder, 1.0)));
            gc->DrawRoundedRectangle(x, y, itemWidth, h, itemRadius);

            gc->SetFont(itemFont, palette.accentPrimary);
            gc->DrawText(m_items[i].text, x + 10_dip, y + (h - 14_dip) / 2.0);
        } else {
            // 普通项
            gc->SetFont(itemFont, palette.textPrimary);
            gc->DrawText(m_items[i].text, x + 10_dip, y + (h - 14_dip) / 2.0);
        }
    }
}

void CustomChoicePopup::OnSize(wxSizeEvent& event) {
    UpdateScrollParams();
    Refresh();
    event.Skip();
}

void CustomChoicePopup::OnMouseMove(wxMouseEvent& event) {
    wxPoint pt = event.GetPosition();
    int newHover = GetItemAtPoint(pt);

    if (newHover != m_hoverIndex) {
        m_hoverIndex = newHover;
        SetCursor(m_hoverIndex >= 0 ? wxCursor(wxCURSOR_HAND) : wxCursor(wxCURSOR_ARROW));
        Refresh();
    }
}

void CustomChoicePopup::OnMouseLeave(wxMouseEvent& WXUNUSED(event)) {
    if (m_hoverIndex != -1) {
        m_hoverIndex = -1;
        SetCursor(wxCursor(wxCURSOR_ARROW));
        Refresh();
    }
}

void CustomChoicePopup::OnLeftDown(wxMouseEvent& event) {
    wxPoint pt = event.GetPosition();
    int clickedIndex = GetItemAtPoint(pt);
    if (clickedIndex >= 0 && clickedIndex < static_cast<int>(m_items.size())) {
        Dismiss();
        if (m_owner) {
            m_owner->OnItemSelectedFromPopup(clickedIndex);
        }
    }
}

void CustomChoicePopup::OnMouseWheel(wxMouseEvent& event) {
    int rotation = event.GetWheelRotation();
    if (rotation > 0) {
        ScrollToItem(m_firstVisibleIndex - 1);
    } else if (rotation < 0) {
        ScrollToItem(m_firstVisibleIndex + 1);
    }
    if (m_scrollBar) {
        m_scrollBar->NotifyActivity();
    }
}

void CustomChoicePopup::OnKeyDown(wxKeyEvent& event) {
    int keyCode = event.GetKeyCode();
    if (keyCode == WXK_ESCAPE) {
        Dismiss();
    } else if (keyCode == WXK_UP) {
        if (m_hoverIndex > 0) {
            m_hoverIndex--;
            ScrollToItem(m_hoverIndex);
            Refresh();
        }
    } else if (keyCode == WXK_DOWN) {
        if (m_hoverIndex < static_cast<int>(m_items.size()) - 1) {
            m_hoverIndex++;
            ScrollToItem(m_hoverIndex);
            Refresh();
        }
    } else if (keyCode == WXK_RETURN || keyCode == WXK_NUMPAD_ENTER || keyCode == WXK_SPACE) {
        if (m_hoverIndex >= 0 && m_hoverIndex < static_cast<int>(m_items.size())) {
            Dismiss();
            if (m_owner) {
                m_owner->OnItemSelectedFromPopup(m_hoverIndex);
            }
        }
    } else {
        event.Skip();
    }
}

void CustomChoicePopup::UpdateTheme() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.cardBg);
    Refresh();
}

// ============================================================================
// CustomChoice 实现
// ============================================================================

CustomChoice::CustomChoice(wxWindow* parent,
                           wxWindowID id,
                           const wxPoint& pos,
                           const wxSize& size,
                           const wxArrayString& choices,
                           long style)
    : wxControl(parent, id, pos, size, style | wxBORDER_NONE | wxTAB_TRAVERSAL) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetItems(choices);
    InitControl();
}

CustomChoice::CustomChoice(wxWindow* parent,
                           wxWindowID id,
                           const wxPoint& pos,
                           const wxSize& size,
                           int n,
                           const wxString choices[],
                           long style)
    : wxControl(parent, id, pos, size, style | wxBORDER_NONE | wxTAB_TRAVERSAL) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    for (int i = 0; i < n; ++i) {
        m_items.push_back({choices[i], nullptr});
    }
    if (!m_items.empty()) {
        m_selection = 0;
    }
    InitControl();
}

CustomChoice::~CustomChoice() {
    if (m_popup) {
        m_popup->Destroy();
        m_popup = nullptr;
    }
}

void CustomChoice::InitControl() {
    SetCanFocus(true);
    SetCursor(wxCursor(wxCURSOR_HAND));

    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.cardBg);

    Bind(wxEVT_PAINT, &CustomChoice::OnPaint, this);
    Bind(wxEVT_ENTER_WINDOW, &CustomChoice::OnMouseEnter, this);
    Bind(wxEVT_LEAVE_WINDOW, &CustomChoice::OnMouseLeave, this);
    Bind(wxEVT_LEFT_DOWN, &CustomChoice::OnLeftDown, this);
    Bind(wxEVT_KEY_DOWN, &CustomChoice::OnKeyDown, this);
    Bind(wxEVT_SET_FOCUS, &CustomChoice::OnSetFocus, this);
    Bind(wxEVT_KILL_FOCUS, &CustomChoice::OnKillFocus, this);
}

wxSize CustomChoice::DoGetBestSize() const {
    // 默认高度 32_dip，最小宽度 120_dip
    int defaultHeight = 32_dip;
    int defaultWidth = 120_dip;

    wxFont font(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei");
    wxScreenDC dc;
    dc.SetFont(font);

    int maxTextWidth = 0;
    for (const auto& item : m_items) {
        wxSize textSize = dc.GetTextExtent(item.text);
        if (textSize.x > maxTextWidth) {
            maxTextWidth = textSize.x;
        }
    }

    int bestW = std::max(defaultWidth, maxTextWidth + 40_dip);
    return wxSize(bestW, defaultHeight);
}

unsigned int CustomChoice::Append(const wxString& item) {
    m_items.push_back({item, nullptr});
    if (m_selection < 0 && !m_items.empty()) {
        m_selection = 0;
    }
    InvalidateBestSize();
    Refresh();
    return static_cast<unsigned int>(m_items.size() - 1);
}

unsigned int CustomChoice::Append(const wxString& item, void* clientData) {
    m_items.push_back({item, clientData});
    if (m_selection < 0 && !m_items.empty()) {
        m_selection = 0;
    }
    InvalidateBestSize();
    Refresh();
    return static_cast<unsigned int>(m_items.size() - 1);
}

void CustomChoice::Clear() {
    m_items.clear();
    m_selection = -1;
    InvalidateBestSize();
    Refresh();
}

void CustomChoice::Delete(unsigned int n) {
    if (n < m_items.size()) {
        m_items.erase(m_items.begin() + n);
        if (m_selection == static_cast<int>(n)) {
            m_selection = m_items.empty() ? -1 : 0;
        } else if (m_selection > static_cast<int>(n)) {
            m_selection--;
        }
        InvalidateBestSize();
        Refresh();
    }
}

wxString CustomChoice::GetString(unsigned int n) const {
    if (n < m_items.size()) {
        return m_items[n].text;
    }
    return wxEmptyString;
}

void CustomChoice::SetString(unsigned int n, const wxString& s) {
    if (n < m_items.size()) {
        m_items[n].text = s;
        InvalidateBestSize();
        Refresh();
    }
}

int CustomChoice::FindString(const wxString& s, bool bCase) const {
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (bCase ? m_items[i].text.IsSameAs(s, true) : m_items[i].text.CmpNoCase(s) == 0) {
            return static_cast<int>(i);
        }
    }
    return wxNOT_FOUND;
}

void CustomChoice::SetSelection(int n) {
    if (n >= 0 && n < static_cast<int>(m_items.size())) {
        m_selection = n;
    } else if (n == -1 || m_items.empty()) {
        m_selection = -1;
    }
    Refresh();
}

wxString CustomChoice::GetStringSelection() const {
    if (m_selection >= 0 && m_selection < static_cast<int>(m_items.size())) {
        return m_items[m_selection].text;
    }
    return wxEmptyString;
}

bool CustomChoice::SetStringSelection(const wxString& s) {
    int idx = FindString(s);
    if (idx != wxNOT_FOUND) {
        SetSelection(idx);
        return true;
    }
    return false;
}

void* CustomChoice::GetClientData(unsigned int n) const {
    if (n < m_items.size()) {
        return m_items[n].clientData;
    }
    return nullptr;
}

void CustomChoice::SetClientData(unsigned int n, void* data) {
    if (n < m_items.size()) {
        m_items[n].clientData = data;
    }
}

void CustomChoice::SetItems(const wxArrayString& items) {
    m_items.clear();
    for (const auto& item : items) {
        m_items.push_back({item, nullptr});
    }
    m_selection = m_items.empty() ? -1 : 0;
    InvalidateBestSize();
    Refresh();
}

void CustomChoice::SetItems(const std::vector<wxString>& items) {
    m_items.clear();
    for (const auto& item : items) {
        m_items.push_back({item, nullptr});
    }
    m_selection = m_items.empty() ? -1 : 0;
    InvalidateBestSize();
    Refresh();
}

bool CustomChoice::Enable(bool enable) {
    bool res = wxControl::Enable(enable);
    Refresh();
    return res;
}

void CustomChoice::UpdateTheme() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.cardBg);
    if (m_popup) {
        m_popup->UpdateTheme();
    }
    Refresh();
}

void CustomChoice::Popup() {
    if (!IsEnabled() || m_items.empty()) return;

    if (!m_popup) {
        m_popup = new CustomChoicePopup(this);
    }

    m_popup->SetItems(m_items, m_selection);

    wxPoint screenPos = ClientToScreen(wxPoint(0, 0));
    wxSize clientSize = GetClientSize();

    int itemHeight = 32_dip;
    int visibleItemCount = std::min(static_cast<int>(m_items.size()), 7);
    int popupHeight = visibleItemCount * itemHeight + 10_dip;
    int popupWidth = std::max(clientSize.x, 130_dip);

    // 屏幕边缘碰撞检测 (防止超出屏幕下边界)
    wxDisplay display(this);
    wxRect monRect = display.GetClientArea();

    int popupX = screenPos.x;
    int popupY = screenPos.y + clientSize.y + 2_dip;

    // 若下方空间不足，则向上弹出
    if (popupY + popupHeight > monRect.GetBottom() && screenPos.y - popupHeight > monRect.GetTop()) {
        popupY = screenPos.y - popupHeight - 2_dip;
    }

    // 防止右侧超出显示器
    if (popupX + popupWidth > monRect.GetRight()) {
        popupX = monRect.GetRight() - popupWidth - 4_dip;
    }

    m_isPopupOpen = true;
    Refresh();

    m_popup->ShowPopup(wxPoint(popupX, popupY), wxSize(popupWidth, popupHeight));
}

void CustomChoice::Dismiss() {
    if (m_popup && m_isPopupOpen) {
        m_popup->Dismiss();
    }
}

void CustomChoice::OnPopupDismissed() {
    m_isPopupOpen = false;
    m_lastDismissTime = wxGetLocalTimeMillis();
    Refresh();
}

void CustomChoice::OnItemSelectedFromPopup(int index) {
    if (index >= 0 && index < static_cast<int>(m_items.size()) && index != m_selection) {
        m_selection = index;
        Refresh();
        SendChoiceEvent();
    }
}

void CustomChoice::SendChoiceEvent() {
    wxCommandEvent evt(wxEVT_CHOICE, GetId());
    evt.SetEventObject(this);
    evt.SetInt(m_selection);
    evt.SetString(GetStringSelection());
    ProcessWindowEvent(evt);
}

void CustomChoice::OnPaint(wxPaintEvent& WXUNUSED(event)) {
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetClientSize();
    if (size.x <= 0 || size.y <= 0) return;

    auto palette = ThemeColors::GetCurrentPalette();
    dc.SetBackground(wxBrush(GetParent() ? GetParent()->GetBackgroundColour() : palette.windowBg));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;

    // 1. 绘制圆角背景与外边框
    double radius = 6.0_dip;
    gc->SetBrush(gc->CreateBrush(wxBrush(IsEnabled() ? palette.cardBg : palette.windowBg)));

    wxColour borderColor = !IsEnabled() ? palette.cardBorder
        : (m_isPopupOpen || m_isFocused ? palette.accentPrimary
        : (m_isHovered ? palette.cardBorderActive : palette.cardBorder));

    double borderWidth = (m_isPopupOpen || m_isFocused) ? 1.5 : 1.0;
    gc->SetPen(gc->CreatePen(wxPen(borderColor, borderWidth)));
    gc->DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, radius);

    // 2. 绘制当前选中的文字
    wxString text = GetStringSelection();
    if (!text.IsEmpty()) {
        wxFont font(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei");
        wxColour textColor = IsEnabled() ? palette.textPrimary : palette.textSecondary;
        gc->SetFont(font, textColor);

        // 裁剪保护，避免文字覆盖右侧指示箭头
        int maxTextWidth = size.x - 32_dip;
        double tw = 0, th = 0;
        gc->GetTextExtent(text, &tw, &th);

        wxString displayText = text;
        if (tw > maxTextWidth && maxTextWidth > 20_dip) {
            while (!displayText.IsEmpty() && tw > maxTextWidth) {
                displayText.RemoveLast();
                gc->GetTextExtent(displayText + "...", &tw, &th);
            }
            displayText += "...";
        }

        double textY = (size.y - th) / 2.0;
        gc->DrawText(displayText, 10_dip, textY);
    }

    // 3. 绘制右侧 Chevron Down 矢量下拉指示器
    int arrowX = size.x - 16_dip;
    int arrowY = size.y / 2;
    int arrowSize = 4_dip;

    wxColour arrowColor = !IsEnabled() ? palette.textSecondary
        : (m_isHovered || m_isPopupOpen ? palette.accentPrimary : palette.textSecondary);

    wxGraphicsPath path = gc->CreatePath();
    if (m_isPopupOpen) {
        // 展开时向上箭头
        path.MoveToPoint(arrowX - arrowSize, arrowY + arrowSize / 2);
        path.AddLineToPoint(arrowX, arrowY - arrowSize / 2);
        path.AddLineToPoint(arrowX + arrowSize, arrowY + arrowSize / 2);
    } else {
        // 常态向下箭头
        path.MoveToPoint(arrowX - arrowSize, arrowY - arrowSize / 2);
        path.AddLineToPoint(arrowX, arrowY + arrowSize / 2);
        path.AddLineToPoint(arrowX + arrowSize, arrowY - arrowSize / 2);
    }

    wxPen arrowPen(arrowColor, 1.6);
    arrowPen.SetCap(wxCAP_ROUND);
    arrowPen.SetJoin(wxJOIN_ROUND);
    gc->SetPen(gc->CreatePen(arrowPen));
    gc->StrokePath(path);
}

void CustomChoice::OnMouseEnter(wxMouseEvent& WXUNUSED(event)) {
    if (!IsEnabled()) return;
    m_isHovered = true;
    Refresh();
}

void CustomChoice::OnMouseLeave(wxMouseEvent& WXUNUSED(event)) {
    m_isHovered = false;
    Refresh();
}

void CustomChoice::OnLeftDown(wxMouseEvent& WXUNUSED(event)) {
    if (!IsEnabled()) return;
    SetFocus();

    auto now = wxGetLocalTimeMillis();
    if ((now - m_lastDismissTime).GetValue() < 250) {
        return;
    }

    if (m_isPopupOpen) {
        Dismiss();
    } else {
        Popup();
    }
}

void CustomChoice::OnKeyDown(wxKeyEvent& event) {
    int keyCode = event.GetKeyCode();

    if (keyCode == WXK_DOWN || keyCode == WXK_NUMPAD_DOWN) {
        if (!m_isPopupOpen) {
            if (m_selection < static_cast<int>(m_items.size()) - 1) {
                SetSelection(m_selection + 1);
                SendChoiceEvent();
            }
        }
    } else if (keyCode == WXK_UP || keyCode == WXK_NUMPAD_UP) {
        if (!m_isPopupOpen) {
            if (m_selection > 0) {
                SetSelection(m_selection - 1);
                SendChoiceEvent();
            }
        }
    } else if (keyCode == WXK_RETURN || keyCode == WXK_NUMPAD_ENTER || keyCode == WXK_SPACE) {
        Popup();
    } else {
        event.Skip();
    }
}

void CustomChoice::OnSetFocus(wxFocusEvent& event) {
    m_isFocused = true;
    Refresh();
    event.Skip();
}

void CustomChoice::OnKillFocus(wxFocusEvent& event) {
    m_isFocused = false;
    Refresh();
    event.Skip();
}

} // namespace LinguaAlpaca::UI
