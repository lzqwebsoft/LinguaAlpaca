#pragma once
#include <wx/wx.h>
#include "../theme/Theme.hpp"

namespace LinguaAlpaca::UI {

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

} // namespace LinguaAlpaca::UI
