#pragma once
#include <wx/wx.h>
#include <vector>
#include <functional>
#include "../theme/Theme.hpp"
#include "TextCtrl.hpp"

namespace LinguaAlpaca::UI {

struct CardToolIcon {
    int id;
    const char* svgContent;
    wxString tooltip;
    std::function<void()> onClick;
};

class CardPanel : public wxPanel {
public:
    CardPanel(wxWindow* parent, const wxString& title, bool isActiveBorder = false, wxWindowID id = wxID_ANY);

    void AddToolIcon(int id, const char* svgContent, const wxString& tooltip, std::function<void()> onClick);
    void SetCharacterCount(size_t count);
    void UpdateTheme();
    TextCtrl* GetTextCtrl() const { return m_textCtrl; }

private:
    void InitUI();
    void OnPaint(wxPaintEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);

    wxString m_title;
    bool m_isActiveBorder;
    size_t m_charCount{0};

    TextCtrl* m_textCtrl{nullptr};
    std::vector<CardToolIcon> m_tools;
    int m_hoverToolIndex{-1};
};

} // namespace LinguaAlpaca::UI
