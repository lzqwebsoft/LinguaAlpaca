#pragma once
#include <wx/wx.h>
#include <vector>
#include <functional>
#include "../theme/Theme.hpp"

namespace LinguaAlpaca::UI {

wxDECLARE_EVENT(EVT_SIDEBAR_NAV_CHANGED, wxCommandEvent);

struct SidebarNavItem {
    int id;
    wxString label;
    const char* svgContent;
};

class SidebarNav : public wxPanel {
public:
    SidebarNav(wxWindow* parent, wxWindowID id = wxID_ANY);

    void SetActiveItem(int index);
    int GetActiveItem() const { return m_selectedIndex; }

    void SetDragCallbacks(
        std::function<void(wxWindow*)> onStartDrag,
        std::function<void(wxMouseEvent&)> onDragging,
        std::function<void()> onEndDrag,
        std::function<void()> onDoubleClick = nullptr
    );

    int HitTest(const wxPoint& pt) const;

private:
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnLeftDClick(wxMouseEvent& event);

    std::vector<SidebarNavItem> m_items;
    SidebarNavItem m_bottomItem;
    int m_selectedIndex{0};
    int m_hoverIndex{-1};

    // 窗口拖拽状态与回调
    std::function<void(wxWindow*)> m_onDragStart;
    std::function<void(wxMouseEvent&)> m_onDragging;
    std::function<void()> m_onDragEnd;
    std::function<void()> m_onDoubleClick;

    bool m_isDraggingWindow{false};
    bool m_itemPressed{false};
    wxPoint m_mouseDownPos;
};

} // namespace LinguaAlpaca::UI
