#pragma once
#include <wx/wx.h>
#include "theme/Theme.hpp"

namespace LinguaAlpaca::UI {

class PlaceholderView : public wxPanel {
public:
    PlaceholderView(wxWindow* parent, const wxString& titleName, wxWindowID id = wxID_ANY)
        : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE) {
        auto palette = ThemeColors::GetCurrentPalette();
        SetBackgroundColour(palette.windowBg);

        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        wxStaticText* text = new wxStaticText(this, wxID_ANY, titleName + L" 功能模块构建中...");
        text->SetFont(wxFont(16, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
        text->SetForegroundColour(palette.textSecondary);

        sizer->AddStretchSpacer(1);
        sizer->Add(text, 0, wxALIGN_CENTER);
        sizer->AddStretchSpacer(1);
        SetSizer(sizer);
    }
};

} // namespace LinguaAlpaca::UI
