#pragma once
#include <wx/wx.h>
#include <wx/timer.h>
#include <memory>
#include "core/ModelManager.hpp"
#include "widgets/LanguageBar.hpp"
#include "widgets/CustomButton.hpp"
#include "widgets/CardPanel.hpp"
#include "widgets/StatusBadge.hpp"

#include "AsyncTrackable.hpp"

namespace LinguaAlpaca::UI {

class TextView : public wxPanel, public AsyncTrackable {
public:
    TextView(wxWindow* parent, 
             std::shared_ptr<ModelManager> modelManager,
             wxWindowID id = wxID_ANY);
    ~TextView() override;

    void UpdateTheme();
    void UpdateStatusBadge();

private:
    void InitUI();
    void OnTranslateClicked(wxCommandEvent& event);
    void OnStopClicked(wxCommandEvent& event);
    void OnClearClicked(wxCommandEvent& event);
    void OnCopyTargetClicked(wxCommandEvent& event);
    void OnSwapClicked(wxCommandEvent& event);
    void OnSourceTextChanged(wxCommandEvent& event);

    std::shared_ptr<ModelManager> m_modelManager;
    wxTimer m_healthTimer;

    // Controls
    LanguageBar* m_langSelector{nullptr};
    CardPanel* m_sourceCard{nullptr};
    CardPanel* m_targetCard{nullptr};

    wxStaticText* m_titleText{nullptr};
    StatusBadge* m_statusBadge{nullptr};

    wxPanel* m_bannerPanel{nullptr};
    wxStaticText* m_bannerText{nullptr};
    wxPanel* m_selectedTagPanel{nullptr};
    wxStaticText* m_tagText{nullptr};
    wxPanel* m_langPanel{nullptr};

    CustomButton* m_instantTransBtn{nullptr};
    CustomButton* m_translateBtn{nullptr};
    CustomButton* m_stopBtn{nullptr};
    CustomButton* m_clearBtn{nullptr};
    CustomButton* m_swapBtn{nullptr};
    CustomButton* m_copyBtn{nullptr};
};

} // namespace LinguaAlpaca::UI
