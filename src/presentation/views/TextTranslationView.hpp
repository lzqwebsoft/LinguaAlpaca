#pragma once
#include <wx/wx.h>
#include <memory>
#include "../../application/service/TranslationService.hpp"
#include "../components/LanguageSelectorBar.hpp"
#include "../components/CustomButton.hpp"
#include "../components/CardPanel.hpp"

namespace LinguaAlpaca::Presentation::Views {

class TextTranslationView : public wxPanel {
public:
    TextTranslationView(wxWindow* parent, 
                        std::shared_ptr<Application::Service::TranslationService> translationService,
                        wxWindowID id = wxID_ANY);

    void UpdateTheme();

private:
    void InitUI();
    void OnTranslateClicked(wxCommandEvent& event);
    void OnStopClicked(wxCommandEvent& event);
    void OnClearClicked(wxCommandEvent& event);
    void OnCopyTargetClicked(wxCommandEvent& event);
    void OnSwapClicked(wxCommandEvent& event);
    void OnSourceTextChanged(wxCommandEvent& event);

    std::shared_ptr<Application::Service::TranslationService> m_translationService;

    // Controls
    Components::LanguageSelectorBar* m_langSelector{nullptr};
    Components::CardPanel* m_sourceCard{nullptr};
    Components::CardPanel* m_targetCard{nullptr};

    wxStaticText* m_titleText{nullptr};
    wxPanel* m_statusBadge{nullptr};
    wxStaticText* m_badgeText{nullptr};

    wxPanel* m_bannerPanel{nullptr};
    wxStaticText* m_bannerText{nullptr};
    wxPanel* m_selectedTagPanel{nullptr};
    wxStaticText* m_tagText{nullptr};
    wxPanel* m_langPanel{nullptr};

    Components::CustomButton* m_instantTransBtn{nullptr};
    Components::CustomButton* m_translateBtn{nullptr};
    Components::CustomButton* m_stopBtn{nullptr};
    Components::CustomButton* m_clearBtn{nullptr};
    Components::CustomButton* m_swapBtn{nullptr};
    Components::CustomButton* m_copyBtn{nullptr};
};

} // namespace LinguaAlpaca::Presentation::Views
