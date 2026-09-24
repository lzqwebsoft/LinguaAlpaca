#pragma once
#include <wx/wx.h>
#include <vector>
#include <functional>
#include "../theme/Theme.hpp"

namespace LinguaAlpaca::UI {

struct SegmentItem {
    int id;
    wxString label;
    const char* svgIcon{nullptr};
};

class SegmentedBar : public wxPanel {
public:
    SegmentedBar(wxWindow* parent,
                 const std::vector<SegmentItem>& items,
                 std::function<void(int index)> onSelect = nullptr,
                 wxWindowID id = wxID_ANY);

    void SetActiveIndex(int index, bool triggerCallback = false);
    int GetActiveIndex() const { return m_selectedIndex; }
    void UpdateTheme();

    wxSize DoGetBestSize() const override;

private:
    void InitUI();
    void OnPaint(wxPaintEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    int HitTestItem(const wxPoint& pt) const;

    std::vector<SegmentItem> m_items;
    std::function<void(int index)> m_onSelect;
    int m_selectedIndex{0};
    int m_hoverIndex{-1};
    mutable std::vector<wxRect> m_itemRects;
};

} // namespace LinguaAlpaca::UI
