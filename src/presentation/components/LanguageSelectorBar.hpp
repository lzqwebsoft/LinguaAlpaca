#pragma once
#include <wx/wx.h>
#include "../../domain/model/Language.hpp"

namespace LinguaAlpaca::Presentation::Components {

wxDECLARE_EVENT(EVT_LANGUAGE_CHANGED, wxCommandEvent);

class LanguageSelectorBar : public wxPanel {
public:
    LanguageSelectorBar(wxWindow* parent, wxWindowID id = wxID_ANY);

    Domain::Model::LanguageCode GetSourceLanguage() const;
    Domain::Model::LanguageCode GetTargetLanguage() const;

    void SetSourceLanguage(Domain::Model::LanguageCode code);
    void SetTargetLanguage(Domain::Model::LanguageCode code);
    void SwapLanguages();
    void UpdateTheme();

private:
    void InitUI();
    void OnSwapClicked(wxCommandEvent& event);
    void OnChoiceSelected(wxCommandEvent& event);

    wxChoice* m_sourceChoice{nullptr};
    wxChoice* m_targetChoice{nullptr};
    wxButton* m_swapBtn{nullptr};

    wxStaticText* m_srcLabel{nullptr};
    wxStaticText* m_targetLabel{nullptr};
};

} // namespace LinguaAlpaca::Presentation::Components
