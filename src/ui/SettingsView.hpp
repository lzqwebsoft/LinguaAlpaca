#pragma once
#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/timer.h>
#include <wx/hyperlink.h>
#include <memory>
#include "core/ModelManager.hpp"
#include "core/Config.hpp"
#include "AsyncTrackable.hpp"
#include "widgets/CustomButton.hpp"
#include "widgets/CustomChoice.hpp"
#include "widgets/CustomInputBox.hpp"
#include "widgets/ScrollBar.hpp"
#include "widgets/StatusBadge.hpp"
#include "widgets/TextCtrl.hpp"

namespace LinguaAlpaca::UI {

class SettingsView : public wxPanel, public AsyncTrackable {
public:
    SettingsView(wxWindow* parent,
                 std::shared_ptr<ModelManager> modelManager,
                 wxWindowID id = wxID_ANY);
    ~SettingsView();

    void UpdateTheme();
    void SetModelPath(const wxString& path);
    void SetOcrModelPath(const wxString& mainPath, const wxString& mmprojPath);

private:
    void InitUI();
    void ScrollTo(int targetY);
    void UpdateLayoutAndScroll();
    void OnSize(wxSizeEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void BindMouseWheelRecursively(wxWindow* win);

    // 翻译模型事件
    void OnBrowseModel(wxCommandEvent& event);
    void OnOpenModelDir(wxCommandEvent& event);
    void OnSaveConfig(wxCommandEvent& event);
    void OnStartModel(wxCommandEvent& event);
    void OnStopModel(wxCommandEvent& event);
    void OnTestModel(wxCommandEvent& event);
    void OnCopyModelApiUrl(wxCommandEvent& event);
    void OnModelGpuModeChanged(wxCommandEvent& event);
    void UpdateTranslationStatus();

    // OCR 模型事件
    void OnBrowseOcrModel(wxCommandEvent& event);
    void OnBrowseOcrMmproj(wxCommandEvent& event);
    void OnSaveOcrConfig(wxCommandEvent& event);
    void OnStartOcrModel(wxCommandEvent& event);
    void OnStopOcrModel(wxCommandEvent& event);
    void OnTestOcrModel(wxCommandEvent& event);
    void OnCopyOcrApiUrl(wxCommandEvent& event);
    void OnOcrGpuModeChanged(wxCommandEvent& event);
    void UpdateOcrStatus();

    std::shared_ptr<ModelManager> m_modelManager;
    std::shared_ptr<ConfigManager> m_configManager;
    wxTimer m_statusTimer;

    // 视口容器、内容画板与自定义滚动条
    wxPanel* m_viewport{nullptr};
    wxPanel* m_contentPanel{nullptr};
    wxBoxSizer* m_mainSizer{nullptr};
    ScrollBar* m_scrollBar{nullptr};
    int m_scrollOffsetY{0};

    // UI Elements - 1. 翻译模型 Group
    wxStaticText* m_titleText{nullptr};
    wxPanel* m_modelCard{nullptr};
    wxStaticText* m_modelCardTitle{nullptr};
    StatusBadge* m_statusBadge{nullptr};

    CustomInputBox* m_modelPathCtrl{nullptr};
    CustomButton* m_browseBtn{nullptr};
    CustomButton* m_openDirBtn{nullptr};

    // 运行参数
    wxStaticText* m_modelGpuLabel{nullptr};
    CustomChoice* m_modelGpuModeChoice{nullptr};
    wxStaticText* m_modelNglLabel{nullptr};
    CustomInputBox* m_modelNglCtrl{nullptr};
    wxStaticText* m_modelPortLabel{nullptr};
    CustomInputBox* m_modelPortCtrl{nullptr};
    wxStaticText* m_modelCtxLabel{nullptr};
    CustomInputBox* m_modelCtxCtrl{nullptr};

    // 本地 API 访问端点展示面板
    wxPanel* m_modelApiPanel{nullptr};
    wxStaticText* m_modelApiStatusText{nullptr};
    wxStaticText* m_modelApiUrlText{nullptr};
    CustomButton* m_modelCopyApiBtn{nullptr};

    CustomButton* m_saveBtn{nullptr};
    CustomButton* m_startBtn{nullptr};
    CustomButton* m_stopBtn{nullptr};
    CustomButton* m_testBtn{nullptr};
    wxHyperlinkCtrl* m_transModelLink{nullptr};

    // UI Elements - 2. OCR 模型 Group
    wxPanel* m_ocrCard{nullptr};
    wxStaticText* m_ocrTitleText{nullptr};
    StatusBadge* m_ocrStatusBadge{nullptr};

    wxStaticText* m_ocrMainLabel{nullptr};
    CustomInputBox* m_ocrModelPathCtrl{nullptr};
    CustomButton* m_ocrBrowseBtn{nullptr};

    wxStaticText* m_ocrMmprojLabel{nullptr};
    CustomInputBox* m_ocrMmprojPathCtrl{nullptr};
    CustomButton* m_ocrMmprojBrowseBtn{nullptr};

    // OCR 运行参数
    wxStaticText* m_ocrGpuLabel{nullptr};
    CustomChoice* m_ocrGpuModeChoice{nullptr};
    wxStaticText* m_ocrNglLabel{nullptr};
    CustomInputBox* m_ocrNglCtrl{nullptr};
    wxStaticText* m_ocrPortLabel{nullptr};
    CustomInputBox* m_ocrPortCtrl{nullptr};
    wxStaticText* m_ocrCtxLabel{nullptr};
    CustomInputBox* m_ocrCtxCtrl{nullptr};
    wxCheckBox* m_ocrMmprojOffloadCheck{nullptr};

    // OCR 本地 API 访问端点展示面板
    wxPanel* m_ocrApiPanel{nullptr};
    wxStaticText* m_ocrApiStatusText{nullptr};
    wxStaticText* m_ocrApiUrlText{nullptr};
    CustomButton* m_ocrCopyApiBtn{nullptr};

    CustomButton* m_ocrSaveBtn{nullptr};
    CustomButton* m_ocrStartBtn{nullptr};
    CustomButton* m_ocrStopBtn{nullptr};
    CustomButton* m_ocrTestBtn{nullptr};

    wxPanel* m_ocrFooterPanel{nullptr};
    wxStaticText* m_ocrFooterText{nullptr};
    wxHyperlinkCtrl* m_ocrModelLink{nullptr};

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
    CustomInputBox* m_dictDirPathCtrl{nullptr};
    CustomButton* m_dictBrowseBtn{nullptr};
    CustomButton* m_dictOpenDirBtn{nullptr};
    CustomButton* m_dictSaveBtn{nullptr};
    CustomButton* m_dictReloadBtn{nullptr};
    wxStaticText* m_dictStatusText{nullptr};
    wxStaticText* m_dictListTitleText{nullptr};
    TextCtrl* m_dictListInfoCtrl{nullptr};
    wxHyperlinkCtrl* m_dictDownloadLink{nullptr};

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
    wxRadioBox* m_themeRadioBox{nullptr};
    CustomButton* m_aboutBtn{nullptr};
    wxStaticText* m_aboutDescText{nullptr};

    void OnThemeRadioChanged(wxCommandEvent& event);
    void OnShowAboutDialog(wxCommandEvent& event);
    void SetThemeConfig(const AppConfig& cfg);

    wxString m_configuredPath;
};

} // namespace LinguaAlpaca::UI
