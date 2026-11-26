#pragma once
#include <wx/wx.h>
#include "../theme/Theme.hpp"
#include "../theme/ThemeBinder.hpp"

namespace LinguaAlpaca::UI {

class WelcomeModelDialog : public wxDialog, public IThemeable {
public:
    WelcomeModelDialog(wxWindow* parent);
    ~WelcomeModelDialog() override;

    void UpdateTheme() override;
    bool ShouldNavigateToSettings() const { return m_goToSettings; }

private:
    void InitUI();
    void OnGoToSettings(wxCommandEvent& event);
    void OnLater(wxCommandEvent& event);

    bool m_goToSettings{false};
    size_t m_themeCallbackId{0};
    ThemeBinder m_theme;
};

} // namespace LinguaAlpaca::UI
