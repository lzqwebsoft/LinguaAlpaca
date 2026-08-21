#pragma once
#include <wx/wx.h>
#include "CustomChoice.hpp"
#include "core/Types.hpp"

namespace LinguaAlpaca::UI {

wxDECLARE_EVENT(EVT_LANGUAGE_CHANGED, wxCommandEvent);

class LanguageBar : public wxPanel {
public:
    LanguageBar(wxWindow* parent, wxWindowID id = wxID_ANY);

    LanguageCode GetSourceLanguage() const;
    LanguageCode GetTargetLanguage() const;

    void SetSourceLanguage(LanguageCode code);
    void SetTargetLanguage(LanguageCode code);
    void SwapLanguages();
    void UpdateTheme();

private:
    void InitUI();
    void OnSwapClicked(wxCommandEvent& event);
    void OnChoiceSelected(wxCommandEvent& event);

    CustomChoice* m_sourceChoice{nullptr};
    CustomChoice* m_targetChoice{nullptr};
    wxButton* m_swapBtn{nullptr};

    wxStaticText* m_srcLabel{nullptr};
    wxStaticText* m_targetLabel{nullptr};
};

} // namespace LinguaAlpaca::UI
