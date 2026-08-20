#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include <vector>
#include <string>
#include "../theme/Theme.hpp"
#include "ScrollBar.hpp"

namespace LinguaAlpaca::UI {

class SuggestListBox : public wxPanel {
public:
    SuggestListBox(wxWindow* parent,
                   wxWindowID id = wxID_ANY,
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxDefaultSize);
    ~SuggestListBox() override = default;

    void Clear();
    void Append(const wxString& item);
    void SetItems(const std::vector<std::string>& items);
    
    int GetSelection() const { return m_selectedIndex; }
    wxString GetString(int index) const;
    void SetSelection(int index);
    size_t GetCount() const { return m_items.size(); }
    bool IsEmpty() const { return m_items.empty(); }

    void ScrollToItem(int targetIndex);
    void UpdateTheme();

private:
    void InitUI();
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);

    void UpdateScrollParams();
    int GetItemAtPoint(const wxPoint& pt) const;

    std::vector<wxString> m_items;
    int m_selectedIndex{-1};
    int m_hoverIndex{-1};
    int m_firstVisibleIndex{0};

    ScrollBar* m_scrollBar{nullptr};
};

} // namespace LinguaAlpaca::UI
