#pragma once
#include <wx/wx.h>
#include <wx/gauge.h>
#include <wx/scrolwin.h>
#include <memory>
#include "../../application/service/TranslationService.hpp"
#include "../../application/service/OcrService.hpp"
#include "../../infrastructure/downloader/ModelDownloader.hpp"
#include "../components/CustomButton.hpp"

namespace LinguaAlpaca::Presentation::Views {

class SettingsView : public wxScrolledWindow {
public:
    SettingsView(wxWindow* parent,
                 std::shared_ptr<Application::Service::TranslationService> translationService,
                 std::shared_ptr<Application::Service::OcrService> ocrService = nullptr,
                 wxWindowID id = wxID_ANY);

    void UpdateTheme();
    void SetModelPath(const wxString& path);
    void SetOcrModelPath(const wxString& mainPath, const wxString& mmprojPath);

private:
    void InitUI();
    void OnBrowseModel(wxCommandEvent& event);
    void OnOpenModelDir(wxCommandEvent& event);
    void OnSaveConfig(wxCommandEvent& event);
    void OnTestModel(wxCommandEvent& event);
    void OnDownloadRecommended(wxCommandEvent& event);
    void OnTabChanged(int tabIndex);

    void OnBrowseOcrModel(wxCommandEvent& event);
    void OnBrowseOcrMmproj(wxCommandEvent& event);
    void OnSaveOcrConfig(wxCommandEvent& event);
    void OnTestOcrModel(wxCommandEvent& event);
    void UpdateOcrStatus();

    std::shared_ptr<Application::Service::TranslationService> m_translationService;
    std::shared_ptr<Application::Service::OcrService> m_ocrService;
    std::shared_ptr<Infrastructure::Downloader::ModelDownloader> m_downloader;

    // UI Elements - 1. 翻译模型 Group
    wxStaticText* m_titleText{nullptr};
    wxPanel* m_modelCard{nullptr};
    wxStaticText* m_modelCardTitle{nullptr};
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

    // UI Elements - 2. OCR 模型 Group
    wxPanel* m_ocrCard{nullptr};
    wxStaticText* m_ocrTitleText{nullptr};
    wxPanel* m_ocrStatusBadge{nullptr};
    wxStaticText* m_ocrStatusText{nullptr};

    wxStaticText* m_ocrMainLabel{nullptr};
    wxTextCtrl* m_ocrModelPathCtrl{nullptr};
    Components::CustomButton* m_ocrBrowseBtn{nullptr};

    wxStaticText* m_ocrMmprojLabel{nullptr};
    wxTextCtrl* m_ocrMmprojPathCtrl{nullptr};
    Components::CustomButton* m_ocrMmprojBrowseBtn{nullptr};

    Components::CustomButton* m_ocrSaveBtn{nullptr};
    Components::CustomButton* m_ocrTestBtn{nullptr};

    wxPanel* m_ocrFooterPanel{nullptr};
    wxStaticText* m_ocrFooterText{nullptr};

    // UI Elements - 3. 首选项 Group
    wxPanel* m_prefCard{nullptr};
    wxStaticText* m_prefTitle{nullptr};

    int m_activeTab{0};
    wxString m_configuredPath;
};

} // namespace LinguaAlpaca::Presentation::Views
