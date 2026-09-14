#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include "../theme/Theme.hpp"
#include "../AsyncTrackable.hpp"
#include "core/VersionChecker.hpp"

namespace LinguaAlpaca::UI {

class CustomButton;

class AboutDialog : public wxDialog, public AsyncTrackable {
public:
    explicit AboutDialog(wxWindow* parent, const wxString& version = wxEmptyString);
    ~AboutDialog() override = default;

private:
    void InitUI();
    void StartVersionCheck();
    void OnCheckUpdate(wxCommandEvent& event);
    void OnVisitGithub(wxCommandEvent& event);
    void OnOpenReleases(wxCommandEvent& event);

    wxString m_version;
    wxString m_latestVersion;
    wxString m_releaseUrl{"https://github.com/lzqwebsoft/LinguaAlpaca/releases"};

    wxStaticText* m_updateStatusText{nullptr};
    CustomButton* m_checkBtn{nullptr};

    wxPanel* m_updateCard{nullptr};
    wxStaticText* m_updateCardTitle{nullptr};
    wxStaticText* m_updateCardNotes{nullptr};
    CustomButton* m_updateActionBtn{nullptr};
};

} // namespace LinguaAlpaca::UI
