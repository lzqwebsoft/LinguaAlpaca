#pragma once
#include <wx/wx.h>
#include "../theme/ThemeColors.hpp"

namespace LinguaAlpaca::Presentation::Components {

class WelcomeModelDialog : public wxDialog {
public:
    WelcomeModelDialog(wxWindow* parent);

    bool ShouldNavigateToSettings() const { return m_goToSettings; }

private:
    void InitUI();
    void OnGoToSettings(wxCommandEvent& event);
    void OnLater(wxCommandEvent& event);

    bool m_goToSettings{false};
};

} // namespace LinguaAlpaca::Presentation::Components
