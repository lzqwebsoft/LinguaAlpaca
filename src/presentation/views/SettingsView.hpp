#pragma once
#include <wx/wx.h>
#include <wx/gauge.h>
#include <wx/scrolwin.h>
#include <memory>
#include "../../application/service/TranslationService.hpp"
#include "../../infrastructure/downloader/ModelDownloader.hpp"
#include "../components/CustomButton.hpp"

namespace LinguaAlpaca::Presentation::Views {

class SettingsView : public wxScrolledWindow {
public:
    SettingsView(wxWindow* parent,
                 std::shared_ptr<Application::Service::TranslationService> translationService,
                 wxWindowID id = wxID_ANY);

    void UpdateTheme();
    void SetModelPath(const wxString& path);

private:
    void InitUI();
    void OnBrowseModel(wxCommandEvent& event);
    void OnOpenModelDir(wxCommandEvent& event);
    void OnSaveConfig(wxCommandEvent& event);
    void OnTestModel(wxCommandEvent& event);
    void OnDownloadRecommended(wxCommandEvent& event);
    void OnTabChanged(int tabIndex);

    std::shared_ptr<Application::Service::TranslationService> m_translationService;
    std::shared_ptr<Infrastructure::Downloader::ModelDownloader> m_downloader;

    // UI Elements
    wxStaticText* m_titleText{nullptr};
    wxPanel* m_modelCard{nullptr};
    wxPanel* m_statusBadge{nullptr};
    wxStaticText* m_statusText{nullptr};

    Components::CustomButton* m_localTabBtn{nullptr};
    Components::CustomButton* m_recommendTabBtn{nullptr};

    wxPanel* m_localPanel{nullptr};
    wxPanel* m_recommendPanel{nullptr};

    wxTextCtrl* m_modelPathCtrl{nullptr};
    Components::CustomButton* m_browseBtn{nullptr};
    Components::CustomButton* m_openDirBtn{nullptr};
    Components::CustomButton* m_saveBtn{nullptr};
    Components::CustomButton* m_testBtn{nullptr};

    // 下载进度 UI
    wxPanel* m_progressPanel{nullptr};
    wxGauge* m_downloadGauge{nullptr};
    wxStaticText* m_progressText{nullptr};
    Components::CustomButton* m_downloadBtn{nullptr};

    wxPanel* m_prefCard{nullptr};
    wxStaticText* m_prefTitle{nullptr};

    int m_activeTab{0};
    wxString m_configuredPath;
};

} // namespace LinguaAlpaca::Presentation::Views
