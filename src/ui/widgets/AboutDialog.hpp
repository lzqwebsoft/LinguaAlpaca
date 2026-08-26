#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include "../theme/Theme.hpp"

namespace LinguaAlpaca::UI {

class AboutDialog : public wxDialog {
public:
    explicit AboutDialog(wxWindow* parent);
    ~AboutDialog() override = default;

private:
    void InitUI();
    void OnVisitGithub(wxCommandEvent& event);
};

} // namespace LinguaAlpaca::UI
