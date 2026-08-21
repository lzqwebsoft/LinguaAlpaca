#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include <wx/popupwin.h>
#include <vector>
#include <string>
#include "../theme/Theme.hpp"
#include "ScrollBar.hpp"

namespace LinguaAlpaca::UI {

struct ChoiceItem {
    wxString text;
    void* clientData{nullptr};
};

class CustomChoice;

/**
 * @brief 自定义现代化下拉浮层 (ChoicePopup)
 */
class CustomChoicePopup : public wxPopupTransientWindow {
public:
    explicit CustomChoicePopup(CustomChoice* owner);
    ~CustomChoicePopup() override = default;

    void SetItems(const std::vector<ChoiceItem>& items, int selection);
    void UpdateTheme();
    void ShowPopup(const wxPoint& pos, const wxSize& size);

protected:
    void OnDismiss() override;
    bool ProcessLeftDown(wxMouseEvent& event) override;

private:
    void InitUI();
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);

    int GetItemAtPoint(const wxPoint& pt) const;
    void ScrollToItem(int targetIndex);
    void UpdateScrollParams();

    CustomChoice* m_owner{nullptr};
    std::vector<ChoiceItem> m_items;
    int m_selectedIndex{-1};
    int m_hoverIndex{-1};
    int m_firstVisibleIndex{0};
    ScrollBar* m_scrollBar{nullptr};
};

/**
 * @brief 现代化自定义下拉选择框 (CustomChoice)
 * 
 * 完全对齐 wxChoice 接口规范，支持：
 * - 优雅的圆角卡片边框与悬停/焦点状态微动效
 * - Per-Monitor High-DPI 缩放与深浅主题自适应
 * - 基于 wxPopupTransientWindow 的全自绘平滑滚动弹出菜单
 * - 派发标准 wxEVT_CHOICE 事件，做到 100% 兼容替换
 */
class CustomChoice : public wxControl {
public:
    CustomChoice(wxWindow* parent,
                 wxWindowID id = wxID_ANY,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize,
                 const wxArrayString& choices = wxArrayString(),
                 long style = 0);

    CustomChoice(wxWindow* parent,
                 wxWindowID id,
                 const wxPoint& pos,
                 const wxSize& size,
                 int n,
                 const wxString choices[],
                 long style = 0);

    ~CustomChoice() override;

    // wxChoice / wxControlWithItems 标准数据操作接口
    unsigned int Append(const wxString& item);
    unsigned int Append(const wxString& item, void* clientData);
    void Clear();
    void Delete(unsigned int n);
    unsigned int GetCount() const { return static_cast<unsigned int>(m_items.size()); }
    bool IsEmpty() const { return m_items.empty(); }

    wxString GetString(unsigned int n) const;
    void SetString(unsigned int n, const wxString& s);
    int FindString(const wxString& s, bool bCase = false) const;

    // 选择与索引
    int GetSelection() const { return m_selection; }
    void SetSelection(int n);
    wxString GetStringSelection() const;
    bool SetStringSelection(const wxString& s);

    void* GetClientData(unsigned int n) const;
    void SetClientData(unsigned int n, void* data);

    void SetItems(const wxArrayString& items);
    void SetItems(const std::vector<wxString>& items);

    // 交互与状态
    bool Enable(bool enable = true) override;
    void UpdateTheme();
    void Popup();
    void Dismiss();
    bool IsPopupShown() const { return m_isPopupOpen; }

    void OnPopupDismissed();
    void OnItemSelectedFromPopup(int index);

protected:
    wxSize DoGetBestSize() const override;

private:
    void InitControl();
    void OnPaint(wxPaintEvent& event);
    void OnMouseEnter(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnSetFocus(wxFocusEvent& event);
    void OnKillFocus(wxFocusEvent& event);

    void SendChoiceEvent();

    std::vector<ChoiceItem> m_items;
    int m_selection{-1};

    bool m_isHovered{false};
    bool m_isFocused{false};
    bool m_isPopupOpen{false};
    wxLongLong m_lastDismissTime{0};

    CustomChoicePopup* m_popup{nullptr};
};

} // namespace LinguaAlpaca::UI
