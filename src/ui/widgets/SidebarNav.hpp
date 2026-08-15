#pragma once
#include <wx/wx.h>
#include <vector>
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

private:
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);

    std::vector<SidebarNavItem> m_items;
    SidebarNavItem m_bottomItem;
    int m_selectedIndex{0};
    int m_hoverIndex{-1};
};

} // namespace LinguaAlpaca::UI
