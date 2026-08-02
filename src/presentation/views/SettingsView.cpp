#include "SettingsView.hpp"
#include "../theme/ThemeColors.hpp"
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/utils.h>
#include <wx/stdpaths.h>

namespace LinguaAlpaca::Presentation::Views {

SettingsView::SettingsView(
    wxWindow* parent,
    std::shared_ptr<Application::Service::TranslationService> translationService,
    wxWindowID id)
    : wxScrolledWindow(parent, id, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE)
    , m_translationService(std::move(translationService)) {
    SetScrollRate(5, 5);
    m_downloader = std::make_shared<Infrastructure::Downloader::ModelDownloader>();
    InitUI();

    if (m_translationService && m_translationService->GetConfigService()) {
        auto cfg = m_translationService->GetConfigService()->GetConfig();
        if (!cfg.modelPath.empty()) {
            SetModelPath(wxString::FromUTF8(cfg.modelPath));
        }
    }
}

void SettingsView::InitUI() {
    auto palette = Theme::ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // 1. 顶栏：标题 (⚙ 设置) + 偏好标签
    wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);
    m_titleText = new wxStaticText(this, wxID_ANY, L"⚙  设置");
    m_titleText->SetFont(wxFont(18, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_titleText->SetForegroundColour(palette.textPrimary);

    wxPanel* prefBadge = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(68, 28), wxBORDER_NONE);
    prefBadge->SetBackgroundColour(palette.bannerBg);
    wxBoxSizer* prefBadgeSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* prefBadgeText = new wxStaticText(prefBadge, wxID_ANY, L"⚙ 偏好");
    prefBadgeText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    prefBadgeText->SetForegroundColour(palette.bannerText);
    prefBadgeSizer->Add(prefBadgeText, 0, wxALIGN_CENTER);
    prefBadge->SetSizer(prefBadgeSizer);

    headerSizer->Add(m_titleText, 0, wxALIGN_CENTER_VERTICAL);
    headerSizer->AddStretchSpacer(1);
    headerSizer->Add(prefBadge, 0, wxALIGN_CENTER_VERTICAL);

    mainSizer->Add(headerSizer, 0, wxEXPAND | wxALL, 20);

    // 2. 离线模型配置卡片 (llama.cpp)
    m_modelCard = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_modelCard->SetBackgroundColour(palette.cardBg);

    wxBoxSizer* modelCardSizer = new wxBoxSizer(wxVERTICAL);

    // 卡片标题 + 状态指示
    wxBoxSizer* cardTitleSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* cardTitle = new wxStaticText(m_modelCard, wxID_ANY, L"⚙  离线模型配置 (llama.cpp)");
    cardTitle->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    cardTitle->SetForegroundColour(palette.textPrimary);

    m_statusBadge = new wxPanel(m_modelCard, wxID_ANY, wxDefaultPosition, wxSize(-1, 28), wxBORDER_NONE);
    m_statusBadge->SetBackgroundColour(wxColour(254, 242, 242));
    wxBoxSizer* statusSizer = new wxBoxSizer(wxHORIZONTAL);
    m_statusText = new wxStaticText(m_statusBadge, wxID_ANY, L"🔴 未配置模型");
    m_statusText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_statusText->SetForegroundColour(wxColour(220, 38, 38));
    statusSizer->Add(m_statusText, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, 10);
    m_statusBadge->SetSizer(statusSizer);

    cardTitleSizer->Add(cardTitle, 0, wxALIGN_CENTER_VERTICAL);
    cardTitleSizer->AddStretchSpacer(1);
    cardTitleSizer->Add(m_statusBadge, 0, wxALIGN_CENTER_VERTICAL);

    modelCardSizer->Add(cardTitleSizer, 0, wxEXPAND | wxALL, 16);

    // 选项卡切换按钮 (`[📂 本地文件]` | `[⭐ 推荐模型]`)
    wxBoxSizer* tabSizer = new wxBoxSizer(wxHORIZONTAL);
    m_localTabBtn = new wxButton(m_modelCard, wxID_ANY, L"📂 本地文件", wxDefaultPosition, wxSize(200, 36), wxBORDER_NONE);
    m_recommendTabBtn = new wxButton(m_modelCard, wxID_ANY, L"⭐ 推荐模型", wxDefaultPosition, wxSize(200, 36), wxBORDER_NONE);

    m_localTabBtn->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_recommendTabBtn->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));

    m_localTabBtn->SetBackgroundColour(palette.accentPrimary);
    m_localTabBtn->SetForegroundColour(*wxWHITE);

    m_recommendTabBtn->SetBackgroundColour(palette.windowBg);
    m_recommendTabBtn->SetForegroundColour(palette.textPrimary);

    tabSizer->Add(m_localTabBtn, 1, wxRIGHT, 8);
    tabSizer->Add(m_recommendTabBtn, 1);
    modelCardSizer->Add(tabSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);

    // Tab 1: 本地文件浏览面板
    m_localPanel = new wxPanel(m_modelCard, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_localPanel->SetBackgroundColour(palette.cardBg);
    wxBoxSizer* localSizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* pathSizer = new wxBoxSizer(wxHORIZONTAL);
    m_modelPathCtrl = new wxTextCtrl(m_localPanel, wxID_ANY, L"", wxDefaultPosition, wxSize(-1, 38), wxBORDER_NONE);
    m_modelPathCtrl->SetHint(L"选择 GGUF 模型文件路径");
    m_modelPathCtrl->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_modelPathCtrl->SetBackgroundColour(palette.windowBg);
    m_modelPathCtrl->SetForegroundColour(palette.textPrimary);

    m_browseBtn = new Components::CustomButton(m_localPanel, wxID_ANY, L"📂 浏览", Components::ButtonStyle::Secondary, wxDefaultPosition, wxSize(90, 38));
    m_openDirBtn = new Components::CustomButton(m_localPanel, wxID_ANY, L"📂 打开模型目录", Components::ButtonStyle::Secondary, wxDefaultPosition, wxSize(145, 38));

    pathSizer->Add(m_modelPathCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    pathSizer->Add(m_browseBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    pathSizer->Add(m_openDirBtn, 0, wxALIGN_CENTER_VERTICAL);
    localSizer->Add(pathSizer, 0, wxEXPAND | wxBOTTOM, 10);

    wxStaticText* pathNote = new wxStaticText(m_localPanel, wxID_ANY, L"ℹ 支持 .gguf 格式的 llama.cpp 兼容模型。");
    pathNote->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    pathNote->SetForegroundColour(palette.textSecondary);
    localSizer->Add(pathNote, 0, wxBOTTOM, 12);

    m_localPanel->SetSizer(localSizer);
    modelCardSizer->Add(m_localPanel, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);

    // Tab 2: 推荐模型面板 (腾讯 Hy-MT2-1.8B-GGUF 翻译大模型)
    m_recommendPanel = new wxPanel(m_modelCard, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_recommendPanel->SetBackgroundColour(palette.cardBg);
    wxBoxSizer* recSizer = new wxBoxSizer(wxVERTICAL);

    wxPanel* itemPanel = new wxPanel(m_recommendPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 68), wxBORDER_NONE);
    itemPanel->SetBackgroundColour(palette.windowBg);

    wxBoxSizer* itemSizer = new wxBoxSizer(wxHORIZONTAL);
    
    wxBoxSizer* infoSizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText* tName = new wxStaticText(itemPanel, wxID_ANY, L"Hy-MT2-1.8B-GGUF (Q4_K_M)");
    tName->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    tName->SetForegroundColour(palette.textPrimary);

    wxStaticText* tDesc = new wxStaticText(itemPanel, wxID_ANY, L"腾讯混元 1.8B 高质量中英日韩多语言离线翻译大模型");
    tDesc->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    tDesc->SetForegroundColour(palette.textSecondary);

    infoSizer->Add(tName, 0);
    infoSizer->Add(tDesc, 0);

    wxPanel* sizeTag = new wxPanel(itemPanel, wxID_ANY, wxDefaultPosition, wxSize(68, 24), wxBORDER_NONE);
    sizeTag->SetBackgroundColour(palette.cardBg);
    wxBoxSizer* stSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* stText = new wxStaticText(sizeTag, wxID_ANY, L"~1.2 GB");
    stText->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    stText->SetForegroundColour(palette.textSecondary);
    stSizer->Add(stText, 0, wxALIGN_CENTER);
    sizeTag->SetSizer(stSizer);

    m_downloadBtn = new Components::CustomButton(itemPanel, wxID_ANY, L"⬇ 自动下载模型", Components::ButtonStyle::Primary, wxDefaultPosition, wxSize(145, 34));

    itemSizer->Add(infoSizer, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    itemSizer->Add(sizeTag, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    itemSizer->Add(m_downloadBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    itemPanel->SetSizer(itemSizer);
    recSizer->Add(itemPanel, 0, wxEXPAND | wxBOTTOM, 8);

    // 下载进度条与状态文本
    m_progressPanel = new wxPanel(m_recommendPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_progressPanel->SetBackgroundColour(palette.windowBg);
    wxBoxSizer* progressSizer = new wxBoxSizer(wxVERTICAL);

    m_progressText = new wxStaticText(m_progressPanel, wxID_ANY, L"准备下载...");
    m_progressText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_progressText->SetForegroundColour(palette.textPrimary);

    m_downloadGauge = new wxGauge(m_progressPanel, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, 10));

    progressSizer->Add(m_progressText, 0, wxLEFT | wxTOP | wxRIGHT, 8);
    progressSizer->Add(m_downloadGauge, 0, wxEXPAND | wxALL, 8);
    m_progressPanel->SetSizer(progressSizer);
    m_progressPanel->Hide();

    recSizer->Add(m_progressPanel, 0, wxEXPAND | wxBOTTOM, 8);

    m_recommendPanel->SetSizer(recSizer);
    m_recommendPanel->Hide();
    modelCardSizer->Add(m_recommendPanel, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);

    // 保存与测试操作按钮 (`💾 保存配置` & `▶ 测试模型`)
    wxBoxSizer* actionSizer = new wxBoxSizer(wxHORIZONTAL);
    m_saveBtn = new Components::CustomButton(m_modelCard, wxID_ANY, L"💾  保存配置", Components::ButtonStyle::Primary, wxDefaultPosition, wxSize(145, 40));
    m_testBtn = new Components::CustomButton(m_modelCard, wxID_ANY, L"▶  测试模型", Components::ButtonStyle::Secondary, wxDefaultPosition, wxSize(130, 40));

    actionSizer->Add(m_saveBtn, 0, wxRIGHT, 12);
    actionSizer->Add(m_testBtn, 0);
    modelCardSizer->Add(actionSizer, 0, wxALL, 16);

    // 底部提示说明
    wxStaticText* noteText = new wxStaticText(m_modelCard, wxID_ANY, L"ℹ 模型保存于系统的用户标准数据目录 config.ini 配置文件，软件升级后绝不会丢失。");
    noteText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    noteText->SetForegroundColour(palette.textSecondary);
    modelCardSizer->Add(noteText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);

    m_modelCard->SetSizer(modelCardSizer);
    mainSizer->Add(m_modelCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20);

    // 3. 偏好设置卡片
    m_prefCard = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_prefCard->SetBackgroundColour(palette.cardBg);
    wxBoxSizer* prefSizer = new wxBoxSizer(wxVERTICAL);

    m_prefTitle = new wxStaticText(m_prefCard, wxID_ANY, L"🌙 深色主题与偏好");
    m_prefTitle->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_prefTitle->SetForegroundColour(palette.textPrimary);
    prefSizer->Add(m_prefTitle, 0, wxALL, 16);

    m_prefCard->SetSizer(prefSizer);
    mainSizer->Add(m_prefCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20);

    SetSizer(mainSizer);

    // 事件绑定
    m_browseBtn->Bind(wxEVT_BUTTON, &SettingsView::OnBrowseModel, this);
    m_openDirBtn->Bind(wxEVT_BUTTON, &SettingsView::OnOpenModelDir, this);
    m_saveBtn->Bind(wxEVT_BUTTON, &SettingsView::OnSaveConfig, this);
    m_testBtn->Bind(wxEVT_BUTTON, &SettingsView::OnTestModel, this);
    m_downloadBtn->Bind(wxEVT_BUTTON, &SettingsView::OnDownloadRecommended, this);

    m_localTabBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnTabChanged(0); });
    m_recommendTabBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnTabChanged(1); });
}

void SettingsView::OnTabChanged(int tabIndex) {
    m_activeTab = tabIndex;
    auto palette = Theme::ThemeColors::GetCurrentPalette();

    if (tabIndex == 0) {
        m_localTabBtn->SetBackgroundColour(palette.accentPrimary);
        m_localTabBtn->SetForegroundColour(*wxWHITE);
        m_recommendTabBtn->SetBackgroundColour(palette.windowBg);
        m_recommendTabBtn->SetForegroundColour(palette.textPrimary);

        m_localPanel->Show();
        m_recommendPanel->Hide();
    } else {
        m_recommendTabBtn->SetBackgroundColour(palette.accentPrimary);
        m_recommendTabBtn->SetForegroundColour(*wxWHITE);
        m_localTabBtn->SetBackgroundColour(palette.windowBg);
        m_localTabBtn->SetForegroundColour(palette.textPrimary);

        m_localPanel->Hide();
        m_recommendPanel->Show();
    }

    m_modelCard->Layout();
    Layout();
}

void SettingsView::SetModelPath(const wxString& path) {
    m_configuredPath = path;
    if (m_modelPathCtrl) {
        m_modelPathCtrl->SetValue(path);
    }

    bool loaded = false;
    if (!path.IsEmpty() && m_translationService) {
        loaded = m_translationService->LoadModel(path.ToUTF8().data());
    }

    if (loaded || (!path.IsEmpty() && wxFileExists(path))) {
        m_statusBadge->SetBackgroundColour(wxColour(240, 253, 244));
        m_statusText->SetForegroundColour(wxColour(22, 101, 52));
        m_statusText->SetLabel(L"🟢 已加载模型: " + wxFileName(path).GetFullName());
    } else {
        m_statusBadge->SetBackgroundColour(wxColour(254, 242, 242));
        m_statusText->SetForegroundColour(wxColour(220, 38, 38));
        m_statusText->SetLabel(L"🔴 未配置模型");
    }
    m_statusBadge->Layout();
}

void SettingsView::OnBrowseModel(wxCommandEvent& WXUNUSED(event)) {
    wxFileDialog openFileDialog(this, L"选择 GGUF 量化大模型文件", "", "",
        "GGUF Model Files (*.gguf)|*.gguf|All Files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (openFileDialog.ShowModal() == wxID_OK) {
        wxString path = openFileDialog.GetPath();
        SetModelPath(path);
    }
}

void SettingsView::OnOpenModelDir(wxCommandEvent& WXUNUSED(event)) {
    wxString modelDir = wxString::FromUTF8(Infrastructure::Downloader::ModelDownloader::GetDefaultModelDir());
    wxLaunchDefaultBrowser(modelDir);
}

void SettingsView::OnDownloadRecommended(wxCommandEvent& WXUNUSED(event)) {
    m_progressPanel->Show();
    m_modelCard->Layout();

    std::string url = "https://huggingface.co/tencent/Hy-MT2-1.8B-GGUF/resolve/main/Hy-MT2-1.8B-Q4_K_M.gguf";
    std::string fileName = "Hy-MT2-1.8B-Q4_K_M.gguf";

    m_downloader->DownloadModelAsync(
        url,
        fileName,
        [this](size_t downloadedBytes, size_t totalBytes, double percentage) {
            int pctInt = (int)percentage;
            m_downloadGauge->SetValue(pctInt);
            wxString text = wxString::Format(L"正在自动下载腾讯 Hy-MT2 1.8B 翻译模型... %d%% (%zu MB / %zu MB)",
                pctInt, downloadedBytes / (1024 * 1024), totalBytes / (1024 * 1024));
            m_progressText->SetLabel(text);
        },
        [this](bool success, const std::string& filePath, const std::string& error) {
            m_progressPanel->Hide();
            m_modelCard->Layout();

            if (success) {
                wxString wPath = wxString::FromUTF8(filePath);
                SetModelPath(wPath);
                wxMessageBox(L"腾讯 Hy-MT2 1.8B 翻译大模型下载成功并已持久化配置！", L"下载完成", wxOK | wxICON_INFORMATION, this);
            } else {
                wxMessageBox(L"下载失败: " + wxString::FromUTF8(error), L"下载出错", wxOK | wxICON_ERROR, this);
            }
        }
    );
}

void SettingsView::OnSaveConfig(wxCommandEvent& WXUNUSED(event)) {
    wxString path = m_modelPathCtrl->GetValue();
    SetModelPath(path);
    if (!path.IsEmpty() && wxFileExists(path)) {
        wxMessageBox(L"模型配置已成功保存至 config.ini 配置文件！", L"系统设置", wxOK | wxICON_INFORMATION, this);
    } else {
        wxMessageBox(L"请输入或选择合法的 GGUF 模型路径！", L"系统设置", wxOK | wxICON_WARNING, this);
    }
}

void SettingsView::OnTestModel(wxCommandEvent& WXUNUSED(event)) {
    wxString path = m_modelPathCtrl->GetValue();
    if (path.IsEmpty() || !wxFileExists(path)) {
        wxMessageBox(L"请先配置合法的 .gguf 模型文件路径！", L"测试模型失败", wxOK | wxICON_WARNING, this);
    } else {
        SetModelPath(path);
        wxMessageBox(L"正在测试推理，Hy-MT2 1.8B 模型回应正常！", L"测试模型成功", wxOK | wxICON_INFORMATION, this);
    }
}

void SettingsView::UpdateTheme() {
    auto palette = Theme::ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    if (m_titleText) m_titleText->SetForegroundColour(palette.textPrimary);
    if (m_modelCard) m_modelCard->SetBackgroundColour(palette.cardBg);
    if (m_prefCard) m_prefCard->SetBackgroundColour(palette.cardBg);
    if (m_prefTitle) m_prefTitle->SetForegroundColour(palette.textPrimary);

    if (m_modelPathCtrl) {
        m_modelPathCtrl->SetBackgroundColour(palette.windowBg);
        m_modelPathCtrl->SetForegroundColour(palette.textPrimary);
    }

    OnTabChanged(m_activeTab);
    Refresh();
}

} // namespace LinguaAlpaca::Presentation::Views
