#pragma once
#include <wx/wx.h>
#include <wx/gauge.h>
#include <wx/scrolwin.h>
#include <memory>
#include "core/ModelManager.hpp"
#include "core/Config.hpp"
#include "core/Downloader.hpp"
#include "widgets/CustomButton.hpp"
#include "widgets/CustomChoice.hpp"
#include "widgets/StatusBadge.hpp"

namespace LinguaAlpaca::UI {

class SettingsView : public wxScrolledWindow {
public:
    SettingsView(wxWindow* parent,
                 std::shared_ptr<ModelManager> modelManager,
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

    std::shared_ptr<ModelManager> m_modelManager;
    std::shared_ptr<ConfigManager> m_configManager;
    std::shared_ptr<Downloader> m_downloader;

    // UI Elements - 1. 翻译模型 Group
    wxStaticText* m_titleText{nullptr};
    wxPanel* m_modelCard{nullptr};
    wxStaticText* m_modelCardTitle{nullptr};
    StatusBadge* m_statusBadge{nullptr};

    CustomButton* m_localTabBtn{nullptr};
    CustomButton* m_recommendTabBtn{nullptr};

    wxPanel* m_localPanel{nullptr};
    wxPanel* m_recommendPanel{nullptr};

    wxTextCtrl* m_modelPathCtrl{nullptr};
    CustomButton* m_browseBtn{nullptr};
    CustomButton* m_openDirBtn{nullptr};
    CustomButton* m_saveBtn{nullptr};
    CustomButton* m_testBtn{nullptr};

    // 下载进度 UI
    wxPanel* m_progressPanel{nullptr};
    wxGauge* m_downloadGauge{nullptr};
    wxStaticText* m_progressText{nullptr};
    CustomButton* m_downloadBtn{nullptr};

    // UI Elements - 2. OCR 模型 Group
    wxPanel* m_ocrCard{nullptr};
    wxStaticText* m_ocrTitleText{nullptr};
    StatusBadge* m_ocrStatusBadge{nullptr};

    wxStaticText* m_ocrMainLabel{nullptr};
    wxTextCtrl* m_ocrModelPathCtrl{nullptr};
    CustomButton* m_ocrBrowseBtn{nullptr};

    wxStaticText* m_ocrMmprojLabel{nullptr};
    wxTextCtrl* m_ocrMmprojPathCtrl{nullptr};
    CustomButton* m_ocrMmprojBrowseBtn{nullptr};

    CustomButton* m_ocrSaveBtn{nullptr};
    CustomButton* m_ocrTestBtn{nullptr};

    wxPanel* m_ocrFooterPanel{nullptr};
    wxStaticText* m_ocrFooterText{nullptr};

    // UI Elements - 3. 划词翻译 Group
    wxPanel* m_selectionCard{nullptr};
    wxStaticText* m_selectionTitleText{nullptr};
    wxCheckBox* m_selectionEnableCheck{nullptr};
    wxRadioBox* m_selectionModeRadio{nullptr};
    wxStaticText* m_modifierKeyLabel{nullptr};
    CustomChoice* m_modifierKeyChoice{nullptr};
    wxCheckBox* m_preserveClipCheck{nullptr};
    CustomButton* m_selectionSaveBtn{nullptr};
    wxStaticText* m_selectionStatusText{nullptr};

    void OnSaveSelectionConfig(wxCommandEvent& event);
    void OnSelectionModeChanged(wxCommandEvent& event);
    void SetSelectionConfig(const AppConfig& cfg);

    // UI Elements - 4. 词典设置 Group
    wxPanel* m_dictCard{nullptr};
    wxStaticText* m_dictTitleText{nullptr};
    StatusBadge* m_dictStatusBadge{nullptr};
    wxStaticText* m_dictDirLabel{nullptr};
    wxTextCtrl* m_dictDirPathCtrl{nullptr};
    CustomButton* m_dictBrowseBtn{nullptr};
    CustomButton* m_dictOpenDirBtn{nullptr};
    CustomButton* m_dictSaveBtn{nullptr};
    CustomButton* m_dictReloadBtn{nullptr};
    wxStaticText* m_dictStatusText{nullptr};
    wxStaticText* m_dictListTitleText{nullptr};
    wxTextCtrl* m_dictListInfoCtrl{nullptr};

    void OnBrowseDictDir(wxCommandEvent& event);
    void OnOpenDictDir(wxCommandEvent& event);
    void OnSaveDictConfig(wxCommandEvent& event);
    void OnReloadDicts(wxCommandEvent& event);
    void UpdateDictListSummary();
    void SetDictConfig(const AppConfig& cfg);

    // UI Elements - 5. 日志与诊断设置 Group
    wxPanel* m_logCard{nullptr};
    wxStaticText* m_logTitleText{nullptr};
    wxCheckBox* m_saveLogToFileCheck{nullptr};
    wxStaticText* m_logPathInfoText{nullptr};
    CustomButton* m_openLogDirBtn{nullptr};
    CustomButton* m_logSaveBtn{nullptr};
    wxStaticText* m_logStatusText{nullptr};

    void OnSaveLogConfig(wxCommandEvent& event);
    void OnOpenLogDirFromSettings(wxCommandEvent& event);
    void SetLogConfig(const AppConfig& cfg);

    // UI Elements - 6. 偏好设置 Group
    wxPanel* m_prefCard{nullptr};
    wxStaticText* m_prefTitle{nullptr};

    int m_activeTab{0};
    wxString m_configuredPath;
};

} // namespace LinguaAlpaca::UI
