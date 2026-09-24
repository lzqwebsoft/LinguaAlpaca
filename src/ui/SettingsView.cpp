#include "SettingsView.hpp"
#include "widgets/AboutDialog.hpp"
#include "core/ClipboardHelper.hpp"
#include "core/Downloader.hpp"
#include "theme/IconManager.hpp"
#include "theme/Theme.hpp"
#include "theme/PlatformThemeHelper.hpp"
#include <wx/clipbrd.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/graphics.h>
#include <wx/utils.h>

namespace LinguaAlpaca::UI {

SettingsView::SettingsView(wxWindow* parent, std::shared_ptr<ModelManager> modelManager, wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_modelManager(std::move(modelManager)) {
    if (m_modelManager) {
        m_configManager = m_modelManager->GetConfigManager();
    }
    InitUI();

    m_statusTimer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        UpdateTranslationStatus();
        UpdateOcrStatus();
    });
}

SettingsView::~SettingsView() {
    if (m_statusTimer.IsRunning()) {
        m_statusTimer.Stop();
    }
}

bool SettingsView::Show(bool show) {
    bool res = wxPanel::Show(show);
    if (show) {
        UpdateTranslationStatus();
        UpdateOcrStatus();
        if (!m_statusTimer.IsRunning()) {
            m_statusTimer.Start(1500);
        }
    } else {
        if (m_statusTimer.IsRunning()) {
            m_statusTimer.Stop();
        }
    }
    return res;
}

void SettingsView::SetupRoundedPanelStyle(wxPanel* panel, float radiusDip, bool isInner) {
    if (!panel)
        return;
    panel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    panel->Bind(wxEVT_PAINT, [panel, radiusDip, isInner](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(panel);
        wxSize size = panel->GetClientSize();
        if (size.x <= 0 || size.y <= 0)
            return;
        auto palette = ThemeColors::GetCurrentPalette();
        dc.SetBackground(wxBrush(isInner ? palette.cardBg : palette.windowBg));
        dc.Clear();
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (gc) {
            gc->SetBrush(gc->CreateBrush(wxBrush(isInner ? palette.windowBg : palette.cardBg)));
            gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 1.0)));
            gc->DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, radiusDip);
        }
    });
}

void SettingsView::SetupCardStyle(wxPanel* card) {
    SetupRoundedPanelStyle(card, 12.0_dip, false);
}

void SettingsView::SetupInnerConsoleStyle(wxPanel* panel) {
    SetupRoundedPanelStyle(panel, 8.0_dip, true);
}

wxString SettingsView::FormatNumberWithCommas(uint32_t num) {
    wxString s = wxString::Format("%u", num);
    int insertPos = static_cast<int>(s.length()) - 3;
    while (insertPos > 0) {
        s.insert(insertPos, ",");
        insertPos -= 3;
    }
    return s;
}

void SettingsView::InitUI() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    // 视口容器 (启用 wxCLIP_CHILDREN 防止向上平移时绘制溢出)
    m_viewport = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxCLIP_CHILDREN | wxBORDER_NONE);
    m_viewport->SetBackgroundColour(palette.windowBg);

    // 实际内容画板 (承载所有设置卡片控件)
    m_contentPanel = new wxPanel(m_viewport, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_contentPanel->SetBackgroundColour(palette.windowBg);

    // 现代化细条滑动条
    m_scrollBar = new ScrollBar(this, [this](int pixelY) { ScrollTo(pixelY); });

    // 吸顶固定顶部面板 (Sticky Header Bar + Segmented Capsule Tabs)
    m_topStickyPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_topStickyPanel->SetBackgroundColour(palette.windowBg);
    m_topStickyPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_topStickyPanel->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(m_topStickyPanel);
        wxSize size = m_topStickyPanel->GetClientSize();
        if (size.x <= 0 || size.y <= 0)
            return;
        auto palette = ThemeColors::GetCurrentPalette();
        dc.SetBackground(wxBrush(palette.windowBg));
        dc.Clear();
        dc.SetPen(wxPen(palette.cardBorder, 1));
        dc.DrawLine(0, size.y - 1, size.x, size.y - 1);
    });

    wxBoxSizer* topStickySizer = new wxBoxSizer(wxVERTICAL);

    // 顶部标题 + 副标题行
    wxBoxSizer* titleRowSizer = new wxBoxSizer(wxHORIZONTAL);
    wxBitmapBundle titleBundle = IconManager::GetIconBundle(SVG::SETTINGS, wxSize(22, 22), palette.accentPrimary);
    m_headerTitleIcon = new wxStaticBitmap(m_topStickyPanel, wxID_ANY, titleBundle);

    wxBoxSizer* titleTextSizer = new wxBoxSizer(wxVERTICAL);
    m_titleText = new wxStaticText(m_topStickyPanel, wxID_ANY, L"系统偏好设置");
    m_titleText->SetFont(wxFont(15, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_titleText->SetForegroundColour(palette.textPrimary);

    m_subTitleText = new wxStaticText(m_topStickyPanel, wxID_ANY, L"管理离线模型、划词交互、词典与系统偏好");
    m_subTitleText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_subTitleText->SetForegroundColour(palette.textSecondary);

    titleTextSizer->Add(m_titleText, 0, wxBOTTOM, 2_dip);
    titleTextSizer->Add(m_subTitleText, 0);

    titleRowSizer->Add(m_headerTitleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);
    titleRowSizer->Add(titleTextSizer, 1, wxALIGN_CENTER_VERTICAL);
    topStickySizer->Add(titleRowSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 16_dip);

    // 吸顶 Segmented Bar 分段胶囊选项卡
    std::vector<SegmentItem> segmentItems = {{0, L"模型服务", SVG::MODEL_LOAD}, {1, L"划词翻译", SVG::TRANSLATE}, {2, L"本地词典", SVG::DICTIONARY}, {3, L"常规偏好", SVG::SETTINGS}};
    m_segmentedBar = new SegmentedBar(m_topStickyPanel, segmentItems, [this](int index) { OnSegmentChanged(index); });
    topStickySizer->Add(m_segmentedBar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 12_dip);
    m_topStickyPanel->SetSizer(topStickySizer);

    // 主布局：顶部固定 Header + 底部 (视口 + 滚动条)
    wxBoxSizer* rootSizer = new wxBoxSizer(wxVERTICAL);
    rootSizer->Add(m_topStickyPanel, 0, wxEXPAND);

    wxBoxSizer* bodySizer = new wxBoxSizer(wxHORIZONTAL);
    bodySizer->Add(m_viewport, 1, wxEXPAND);
    bodySizer->Add(m_scrollBar, 0, wxEXPAND | wxTOP | wxBOTTOM, 4_dip);
    rootSizer->Add(bodySizer, 1, wxEXPAND);

    SetSizer(rootSizer);

    m_mainSizer = new wxBoxSizer(wxVERTICAL);
    m_mainSizer->AddSpacer(16_dip);

    // ====================================================================
    // Group 1: 翻译大模型 (Translation Model Settings Card)
    // ====================================================================
    m_modelCard = new wxPanel(m_contentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_modelCard->SetBackgroundColour(palette.cardBg);
    SetupCardStyle(m_modelCard);

    wxBoxSizer* modelCardSizer = new wxBoxSizer(wxVERTICAL);

    // 卡片标题 + 状态指示
    wxBoxSizer* cardTitleSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBitmapBundle cardTitleBundle = IconManager::GetIconBundle(SVG::MODEL_LOAD, wxSize(18, 18), palette.accentPrimary);
    m_modelCardIcon = new wxStaticBitmap(m_modelCard, wxID_ANY, cardTitleBundle);

    m_modelCardTitle = new wxStaticText(m_modelCard, wxID_ANY, L"翻译模型 (Text Translation Model)");
    m_modelCardTitle->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_modelCardTitle->SetForegroundColour(palette.textPrimary);

    m_statusBadge = new StatusBadge(m_modelCard);

    cardTitleSizer->Add(m_modelCardIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    cardTitleSizer->Add(m_modelCardTitle, 0, wxALIGN_CENTER_VERTICAL);
    cardTitleSizer->AddStretchSpacer(1);
    cardTitleSizer->Add(m_statusBadge, 0, wxALIGN_CENTER_VERTICAL);

    modelCardSizer->Add(cardTitleSizer, 0, wxEXPAND | wxALL, 16_dip);

    // 模型文件路径选择行 (统一 70_dip 标签对齐)
    wxBoxSizer* pathSizer = new wxBoxSizer(wxHORIZONTAL);
    m_modelPathLabel = new wxStaticText(m_modelCard, wxID_ANY, L"模型文件", wxDefaultPosition, wxSize(70_dip, -1));
    m_modelPathLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_modelPathLabel->SetForegroundColour(palette.textPrimary);

    m_modelPathCtrl = new CustomInputBox(m_modelCard, wxID_ANY, L"", L"选择 GGUF 模型文件路径", wxDefaultPosition, wxSize(-1, 38_dip));
    m_modelPathCtrl->SetPrefixIcon(SVG::MODEL_LOAD, dip(16, 16));

    m_browseBtn = new CustomButton(m_modelCard, wxID_ANY, L"浏览", ButtonStyle::Secondary, wxDefaultPosition, dip(90, 38));
    m_browseBtn->SetIcon(SVG::BROWSE, dip(16, 16));

    m_openDirBtn = new CustomButton(m_modelCard, wxID_ANY, L"打开模型目录", ButtonStyle::Secondary, wxDefaultPosition, dip(135, 38));
    m_openDirBtn->SetIcon(SVG::FOLDER_OPEN, dip(16, 16));

    pathSizer->Add(m_modelPathLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16_dip);
    pathSizer->Add(m_modelPathCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    pathSizer->Add(m_browseBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    pathSizer->Add(m_openDirBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16_dip);
    modelCardSizer->Add(pathSizer, 0, wxEXPAND | wxBOTTOM, 12_dip);

    // 运行参数设置行
    wxBoxSizer* modelParamSizer = new wxBoxSizer(wxHORIZONTAL);

    m_modelGpuLabel = new wxStaticText(m_modelCard, wxID_ANY, L"计算加速：");
    m_modelGpuLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_modelGpuLabel->SetForegroundColour(palette.textPrimary);

    wxArrayString gpuModes;
    gpuModes.Add(L"GPU 硬件加速 (-ngl 99)");
    gpuModes.Add(L"纯 CPU 计算模式 (-ngl 0)");
    gpuModes.Add(L"自定义 GPU 层数");
    m_modelGpuModeChoice = new CustomChoice(m_modelCard, wxID_ANY, wxDefaultPosition, dip(180, 34), gpuModes);
    m_modelGpuModeChoice->SetSelection(0);

    m_modelNglLabel = new wxStaticText(m_modelCard, wxID_ANY, L"-ngl 层数：");
    m_modelNglLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_modelNglLabel->SetForegroundColour(palette.textPrimary);
    m_modelNglLabel->Show(false);

    m_modelNglCtrl = new CustomInputBox(m_modelCard, wxID_ANY, L"99", L"99", wxDefaultPosition, dip(70, 34));
    m_modelNglCtrl->Show(false);

    m_modelPortLabel = new wxStaticText(m_modelCard, wxID_ANY, L"端口：");
    m_modelPortLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_modelPortLabel->SetForegroundColour(palette.textPrimary);

    m_modelPortCtrl = new CustomInputBox(m_modelCard, wxID_ANY, L"0", L"0(自动)", wxDefaultPosition, dip(80, 34));

    m_modelCtxLabel = new wxStaticText(m_modelCard, wxID_ANY, L"上下文：");
    m_modelCtxLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_modelCtxLabel->SetForegroundColour(palette.textPrimary);

    m_modelCtxCtrl = new CustomInputBox(m_modelCard, wxID_ANY, L"2048", L"2048", wxDefaultPosition, dip(80, 34));

    modelParamSizer->Add(m_modelGpuLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16_dip);
    modelParamSizer->Add(m_modelGpuModeChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14_dip);
    modelParamSizer->Add(m_modelNglLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6_dip);
    modelParamSizer->Add(m_modelNglCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14_dip);
    modelParamSizer->Add(m_modelPortLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6_dip);
    modelParamSizer->Add(m_modelPortCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14_dip);
    modelParamSizer->Add(m_modelCtxLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6_dip);
    modelParamSizer->Add(m_modelCtxCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16_dip);

    modelCardSizer->Add(modelParamSizer, 0, wxEXPAND | wxBOTTOM, 12_dip);

    // 本地 API 访问端点展示卡片
    m_modelApiPanel = new wxPanel(m_modelCard, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_modelApiPanel->SetBackgroundColour(palette.windowBg);
    SetupInnerConsoleStyle(m_modelApiPanel);

    wxBoxSizer* apiPanelSizer = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* apiTextSizer = new wxBoxSizer(wxVERTICAL);

    m_modelApiStatusText = new wxStaticText(m_modelApiPanel, wxID_ANY, L"● 服务状态: 离线 (未启动)");
    m_modelApiStatusText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_modelApiStatusText->SetForegroundColour(palette.textSecondary);

    m_modelApiUrlText = new wxStaticText(m_modelApiPanel, wxID_ANY, L"OpenAI 兼容 API 接口: 待启动后分配");
    m_modelApiUrlText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Consolas, Microsoft YaHei"));
    m_modelApiUrlText->SetForegroundColour(palette.textSecondary);

    apiTextSizer->Add(m_modelApiStatusText, 0, wxBOTTOM, 3_dip);
    apiTextSizer->Add(m_modelApiUrlText, 0);

    m_modelCopyApiBtn = new CustomButton(m_modelApiPanel, wxID_ANY, L"复制 API 地址", ButtonStyle::Secondary, wxDefaultPosition, dip(120, 32));
    m_modelCopyApiBtn->SetIcon(SVG::COPY, dip(14, 14));

    apiPanelSizer->Add(apiTextSizer, 1, wxALIGN_CENTER_VERTICAL | wxALL, 10_dip);
    apiPanelSizer->Add(m_modelCopyApiBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);
    m_modelApiPanel->SetSizer(apiPanelSizer);

    modelCardSizer->Add(m_modelApiPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14_dip);

    // 操作按钮 (`保存配置` / `启动/重启服务` / `停止服务` / `测试推理`)
    wxBoxSizer* actionSizer = new wxBoxSizer(wxHORIZONTAL);
    m_saveBtn = new CustomButton(m_modelCard, wxID_ANY, L"保存配置", ButtonStyle::Primary, wxDefaultPosition, dip(115, 38));
    m_saveBtn->SetIcon(SVG::SAVE, dip(15, 15), *wxWHITE);

    m_startBtn = new CustomButton(m_modelCard, wxID_ANY, L"启动 / 重启服务", ButtonStyle::Secondary, wxDefaultPosition, dip(140, 38));
    m_startBtn->SetIcon(SVG::START, dip(15, 15));

    m_stopBtn = new CustomButton(m_modelCard, wxID_ANY, L"停止服务", ButtonStyle::Danger, wxDefaultPosition, dip(115, 38));
    m_stopBtn->SetIcon(SVG::STOP, dip(15, 15), *wxWHITE);

    m_testBtn = new CustomButton(m_modelCard, wxID_ANY, L"测试推理", ButtonStyle::Secondary, wxDefaultPosition, dip(115, 38));
    m_testBtn->SetIcon(SVG::TEST, dip(15, 15));

    actionSizer->Add(m_saveBtn, 0, wxRIGHT, 10_dip);
    actionSizer->Add(m_startBtn, 0, wxRIGHT, 10_dip);
    actionSizer->Add(m_stopBtn, 0, wxRIGHT, 10_dip);
    actionSizer->Add(m_testBtn, 0);
    modelCardSizer->Add(actionSizer, 0, wxLEFT | wxBOTTOM, 14_dip);

    // 底部模型下载链接说明
    wxBoxSizer* modelFooterSizer = new wxBoxSizer(wxHORIZONTAL);
    wxBitmapBundle modelInfoBundle = IconManager::GetIconBundle(SVG::INFO, wxSize(15, 15), palette.accentPrimary);
    m_modelInfoIcon = new wxStaticBitmap(m_modelCard, wxID_ANY, modelInfoBundle);

    wxStaticText* modelFooterLabel = new wxStaticText(m_modelCard, wxID_ANY, L"模型下载：");
    modelFooterLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    modelFooterLabel->SetForegroundColour(palette.textSecondary);

    m_transModelLink = new wxHyperlinkCtrl(m_modelCard, wxID_ANY, L"Tencent Hy-MT2-1.8B-GGUF (HuggingFace)", "https://huggingface.co/tencent/Hy-MT2-1.8B-GGUF");
    m_transModelLink->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_transModelLink->SetNormalColour(palette.accentPrimary);
    m_transModelLink->SetHoverColour(palette.accentHover);

    modelFooterSizer->Add(m_modelInfoIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6_dip);
    modelFooterSizer->Add(modelFooterLabel, 0, wxALIGN_CENTER_VERTICAL);
    modelFooterSizer->Add(m_transModelLink, 0, wxALIGN_CENTER_VERTICAL);
    modelCardSizer->Add(modelFooterSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    m_modelCard->SetSizer(modelCardSizer);
    m_mainSizer->Add(m_modelCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    // ====================================================================
    // Group 2: OCR 视觉识别模型 (OCR Model Settings Card)
    // ====================================================================
    m_ocrCard = new wxPanel(m_contentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_ocrCard->SetBackgroundColour(palette.cardBg);
    SetupCardStyle(m_ocrCard);

    wxBoxSizer* ocrCardSizer = new wxBoxSizer(wxVERTICAL);

    // 卡片标题 + 状态指示
    wxBoxSizer* ocrTitleSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBitmapBundle ocrTitleBundle = IconManager::GetIconBundle(SVG::OCR, wxSize(18, 18), palette.accentPrimary);
    m_ocrTitleIcon = new wxStaticBitmap(m_ocrCard, wxID_ANY, ocrTitleBundle);

    m_ocrTitleText = new wxStaticText(m_ocrCard, wxID_ANY, L"OCR 视觉识别模型 (Vision OCR Model)");
    m_ocrTitleText->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_ocrTitleText->SetForegroundColour(palette.textPrimary);

    m_ocrStatusBadge = new StatusBadge(m_ocrCard);

    ocrTitleSizer->Add(m_ocrTitleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    ocrTitleSizer->Add(m_ocrTitleText, 0, wxALIGN_CENTER_VERTICAL);
    ocrTitleSizer->AddStretchSpacer(1);
    ocrTitleSizer->Add(m_ocrStatusBadge, 0, wxALIGN_CENTER_VERTICAL);

    ocrCardSizer->Add(ocrTitleSizer, 0, wxEXPAND | wxALL, 16_dip);

    // Row 1: 主模型文件路径选择
    wxBoxSizer* ocrMainRow = new wxBoxSizer(wxHORIZONTAL);
    m_ocrMainLabel = new wxStaticText(m_ocrCard, wxID_ANY, L"主模型", wxDefaultPosition, wxSize(70_dip, -1));
    m_ocrMainLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_ocrMainLabel->SetForegroundColour(palette.textPrimary);

    m_ocrModelPathCtrl = new CustomInputBox(m_ocrCard, wxID_ANY, L"", L"选择 OCR 主模型文件路径", wxDefaultPosition, wxSize(-1, 38_dip));
    m_ocrModelPathCtrl->SetPrefixIcon(SVG::OCR, dip(16, 16));

    m_ocrBrowseBtn = new CustomButton(m_ocrCard, wxID_ANY, L"浏览", ButtonStyle::Secondary, wxDefaultPosition, dip(90, 38));
    m_ocrBrowseBtn->SetIcon(SVG::BROWSE, dip(16, 16));

    ocrMainRow->Add(m_ocrMainLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16_dip);
    ocrMainRow->Add(m_ocrModelPathCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    ocrMainRow->Add(m_ocrBrowseBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16_dip);

    ocrCardSizer->Add(ocrMainRow, 0, wxEXPAND | wxBOTTOM, 12_dip);

    // Row 2: mmproj 视觉投影器文件路径选择
    wxBoxSizer* ocrMmprojRow = new wxBoxSizer(wxHORIZONTAL);
    m_ocrMmprojLabel = new wxStaticText(m_ocrCard, wxID_ANY, L"mmproj", wxDefaultPosition, wxSize(70_dip, -1));
    m_ocrMmprojLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_ocrMmprojLabel->SetForegroundColour(palette.textPrimary);

    m_ocrMmprojPathCtrl = new CustomInputBox(m_ocrCard, wxID_ANY, L"", L"选择 mmproj 视觉投影器文件路径", wxDefaultPosition, wxSize(-1, 38_dip));
    m_ocrMmprojPathCtrl->SetPrefixIcon(SVG::INFO, dip(16, 16));

    m_ocrMmprojBrowseBtn = new CustomButton(m_ocrCard, wxID_ANY, L"浏览", ButtonStyle::Secondary, wxDefaultPosition, dip(90, 38));
    m_ocrMmprojBrowseBtn->SetIcon(SVG::BROWSE, dip(16, 16));

    ocrMmprojRow->Add(m_ocrMmprojLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16_dip);
    ocrMmprojRow->Add(m_ocrMmprojPathCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    ocrMmprojRow->Add(m_ocrMmprojBrowseBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16_dip);

    ocrCardSizer->Add(ocrMmprojRow, 0, wxEXPAND | wxBOTTOM, 12_dip);

    // OCR 运行参数设置行
    wxBoxSizer* ocrParamSizer = new wxBoxSizer(wxHORIZONTAL);

    m_ocrGpuLabel = new wxStaticText(m_ocrCard, wxID_ANY, L"计算加速：");
    m_ocrGpuLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_ocrGpuLabel->SetForegroundColour(palette.textPrimary);

    wxArrayString ocrGpuModes;
    ocrGpuModes.Add(L"纯 CPU 计算模式 (-ngl 0, 推荐)");
    ocrGpuModes.Add(L"GPU 硬件加速 (-ngl 99)");
    ocrGpuModes.Add(L"自定义 GPU 层数");
    m_ocrGpuModeChoice = new CustomChoice(m_ocrCard, wxID_ANY, wxDefaultPosition, dip(210, 34), ocrGpuModes);
    m_ocrGpuModeChoice->SetSelection(0);

    m_ocrNglLabel = new wxStaticText(m_ocrCard, wxID_ANY, L"-ngl 层数：");
    m_ocrNglLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_ocrNglLabel->SetForegroundColour(palette.textPrimary);
    m_ocrNglLabel->Show(false);

    m_ocrNglCtrl = new CustomInputBox(m_ocrCard, wxID_ANY, L"0", L"0", wxDefaultPosition, dip(70, 34));
    m_ocrNglCtrl->Show(false);

    m_ocrPortLabel = new wxStaticText(m_ocrCard, wxID_ANY, L"端口：");
    m_ocrPortLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_ocrPortLabel->SetForegroundColour(palette.textPrimary);

    m_ocrPortCtrl = new CustomInputBox(m_ocrCard, wxID_ANY, L"0", L"0(自动)", wxDefaultPosition, dip(80, 34));

    m_ocrCtxLabel = new wxStaticText(m_ocrCard, wxID_ANY, L"上下文：");
    m_ocrCtxLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_ocrCtxLabel->SetForegroundColour(palette.textPrimary);

    m_ocrCtxCtrl = new CustomInputBox(m_ocrCard, wxID_ANY, L"4096", L"4096", wxDefaultPosition, dip(80, 34));

    ocrParamSizer->Add(m_ocrGpuLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16_dip);
    ocrParamSizer->Add(m_ocrGpuModeChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14_dip);
    ocrParamSizer->Add(m_ocrNglLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6_dip);
    ocrParamSizer->Add(m_ocrNglCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14_dip);
    ocrParamSizer->Add(m_ocrPortLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6_dip);
    ocrParamSizer->Add(m_ocrPortCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14_dip);
    ocrParamSizer->Add(m_ocrCtxLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6_dip);
    ocrParamSizer->Add(m_ocrCtxCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16_dip);

    ocrCardSizer->Add(ocrParamSizer, 0, wxEXPAND | wxBOTTOM, 10_dip);

    // mmproj GPU 加速开关
    m_ocrMmprojOffloadCheck = new wxCheckBox(m_ocrCard, wxID_ANY, L"启用 mmproj 视觉投影器 GPU 硬件加速（显存 < 8GB 建议取消勾选，使用 CPU 运行）");
    m_ocrMmprojOffloadCheck->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    PlatformThemeHelper::ApplyControlTheme(m_ocrMmprojOffloadCheck, palette);
    ocrCardSizer->Add(m_ocrMmprojOffloadCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12_dip);

    // OCR 本地 API 访问端点展示卡片
    m_ocrApiPanel = new wxPanel(m_ocrCard, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_ocrApiPanel->SetBackgroundColour(palette.windowBg);
    SetupInnerConsoleStyle(m_ocrApiPanel);

    wxBoxSizer* ocrApiPanelSizer = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* ocrApiTextSizer = new wxBoxSizer(wxVERTICAL);

    m_ocrApiStatusText = new wxStaticText(m_ocrApiPanel, wxID_ANY, L"● 服务状态: 离线 (未启动)");
    m_ocrApiStatusText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_ocrApiStatusText->SetForegroundColour(palette.textSecondary);

    m_ocrApiUrlText = new wxStaticText(m_ocrApiPanel, wxID_ANY, L"OpenAI 兼容 API 接口: 待启动后分配");
    m_ocrApiUrlText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Consolas, Microsoft YaHei"));
    m_ocrApiUrlText->SetForegroundColour(palette.textSecondary);

    ocrApiTextSizer->Add(m_ocrApiStatusText, 0, wxBOTTOM, 3_dip);
    ocrApiTextSizer->Add(m_ocrApiUrlText, 0);

    m_ocrCopyApiBtn = new CustomButton(m_ocrApiPanel, wxID_ANY, L"复制 API 地址", ButtonStyle::Secondary, wxDefaultPosition, dip(120, 32));
    m_ocrCopyApiBtn->SetIcon(SVG::COPY, dip(14, 14));

    ocrApiPanelSizer->Add(ocrApiTextSizer, 1, wxALIGN_CENTER_VERTICAL | wxALL, 10_dip);
    ocrApiPanelSizer->Add(m_ocrCopyApiBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);
    m_ocrApiPanel->SetSizer(ocrApiPanelSizer);

    ocrCardSizer->Add(m_ocrApiPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14_dip);

    // OCR 操作按钮 (`保存配置` / `启动/重启服务` / `停止服务` / `测试模型`)
    wxBoxSizer* ocrActionSizer = new wxBoxSizer(wxHORIZONTAL);
    m_ocrSaveBtn = new CustomButton(m_ocrCard, wxID_ANY, L"保存配置", ButtonStyle::Primary, wxDefaultPosition, dip(115, 38));
    m_ocrSaveBtn->SetIcon(SVG::SAVE, dip(15, 15), *wxWHITE);

    m_ocrStartBtn = new CustomButton(m_ocrCard, wxID_ANY, L"启动 / 重启服务", ButtonStyle::Secondary, wxDefaultPosition, dip(140, 38));
    m_ocrStartBtn->SetIcon(SVG::START, dip(15, 15));

    m_ocrStopBtn = new CustomButton(m_ocrCard, wxID_ANY, L"停止服务", ButtonStyle::Danger, wxDefaultPosition, dip(115, 38));
    m_ocrStopBtn->SetIcon(SVG::STOP, dip(15, 15), *wxWHITE);

    m_ocrTestBtn = new CustomButton(m_ocrCard, wxID_ANY, L"测试模型", ButtonStyle::Secondary, wxDefaultPosition, dip(115, 38));
    m_ocrTestBtn->SetIcon(SVG::TEST, dip(15, 15));

    ocrActionSizer->Add(m_ocrSaveBtn, 0, wxRIGHT, 10_dip);
    ocrActionSizer->Add(m_ocrStartBtn, 0, wxRIGHT, 10_dip);
    ocrActionSizer->Add(m_ocrStopBtn, 0, wxRIGHT, 10_dip);
    ocrActionSizer->Add(m_ocrTestBtn, 0);
    ocrCardSizer->Add(ocrActionSizer, 0, wxLEFT | wxBOTTOM, 14_dip);

    // 底部说明与模型下载链接
    m_ocrFooterPanel = new wxPanel(m_ocrCard, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_ocrFooterPanel->SetBackgroundColour(palette.cardBg);
    wxBoxSizer* ocrFooterSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBitmapBundle infoBundle = IconManager::GetIconBundle(SVG::INFO, wxSize(15, 15), palette.accentPrimary);
    m_ocrInfoIcon = new wxStaticBitmap(m_ocrFooterPanel, wxID_ANY, infoBundle);

    m_ocrFooterText = new wxStaticText(m_ocrFooterPanel, wxID_ANY, L"提示: 视觉模型需配合 mmproj 使用 (<= 8GB 显存建议 CPU 模式)。");
    m_ocrFooterText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_ocrFooterText->SetForegroundColour(palette.textSecondary);

    wxStaticText* ocrLinkSep = new wxStaticText(m_ocrFooterPanel, wxID_ANY, L"  |  模型下载：");
    ocrLinkSep->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    ocrLinkSep->SetForegroundColour(palette.textSecondary);

    m_ocrModelLink = new wxHyperlinkCtrl(m_ocrFooterPanel, wxID_ANY, L"PaddleOCR-VL-1.6-GGUF (HuggingFace)", "https://huggingface.co/PaddlePaddle/PaddleOCR-VL-1.6-GGUF");
    m_ocrModelLink->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_ocrModelLink->SetNormalColour(palette.accentPrimary);
    m_ocrModelLink->SetHoverColour(palette.accentHover);

    ocrFooterSizer->Add(m_ocrInfoIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6_dip);
    ocrFooterSizer->Add(m_ocrFooterText, 0, wxALIGN_CENTER_VERTICAL);
    ocrFooterSizer->Add(ocrLinkSep, 0, wxALIGN_CENTER_VERTICAL);
    ocrFooterSizer->Add(m_ocrModelLink, 0, wxALIGN_CENTER_VERTICAL);
    m_ocrFooterPanel->SetSizer(ocrFooterSizer);

    ocrCardSizer->Add(m_ocrFooterPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    m_ocrCard->SetSizer(ocrCardSizer);
    m_mainSizer->Add(m_ocrCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    // ====================================================================
    // Group 3: 划词翻译设置卡片
    // ====================================================================
    m_selectionCard = new wxPanel(m_contentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_selectionCard->SetBackgroundColour(palette.cardBg);
    SetupCardStyle(m_selectionCard);

    wxBoxSizer* selSizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* selTitleSizer = new wxBoxSizer(wxHORIZONTAL);
    wxBitmapBundle selBundle = IconManager::GetIconBundle(SVG::TRANSLATE, wxSize(18, 18), palette.accentPrimary);
    m_selectionTitleIcon = new wxStaticBitmap(m_selectionCard, wxID_ANY, selBundle);

    m_selectionTitleText = new wxStaticText(m_selectionCard, wxID_ANY, L"全局划词翻译设置");
    m_selectionTitleText->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_selectionTitleText->SetForegroundColour(palette.textPrimary);

    selTitleSizer->Add(m_selectionTitleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    selTitleSizer->Add(m_selectionTitleText, 0, wxALIGN_CENTER_VERTICAL);
    selSizer->Add(selTitleSizer, 0, wxALL, 16_dip);

    // 1. 启用开关
    m_selectionEnableCheck = new wxCheckBox(m_selectionCard, wxID_ANY, L"启用全局划词翻译（选中文本后在光标旁显示悬浮按钮）");
    m_selectionEnableCheck->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    PlatformThemeHelper::ApplyControlTheme(m_selectionEnableCheck, palette);
    selSizer->Add(m_selectionEnableCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    // 2. 触发模式选择
    wxArrayString modes;
    modes.Add(L"① 鼠标直接划词（拖拽选中文本后自动检测）");
    modes.Add(L"② 划词 + 辅助按键（按住辅助按键划词触发）");
    modes.Add(L"③ 双击划词 / 三击划段（双击选词或三击选段触发）");
    m_selectionModeRadio = new LeftAlignedRadioBox(m_selectionCard, wxID_ANY, L"划词触发模式", wxDefaultPosition, wxDefaultSize, modes, 1, wxRA_SPECIFY_COLS);
    m_selectionModeRadio->SetFont(ThemeFont::GetFont(FontRole::Control));
    PlatformThemeHelper::ApplyControlTheme(m_selectionModeRadio, palette);
    selSizer->Add(m_selectionModeRadio, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    // 3. 辅助按键选择
    wxBoxSizer* modKeySizer = new wxBoxSizer(wxHORIZONTAL);
    m_modifierKeyLabel = new wxStaticText(m_selectionCard, wxID_ANY, L"辅助触发按键：");
    m_modifierKeyLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_modifierKeyLabel->SetForegroundColour(palette.textPrimary);

    wxArrayString keys;
    keys.Add(L"Ctrl 键 (推荐)");
    keys.Add(L"Alt 键");
    keys.Add(L"Shift 键");
    m_modifierKeyChoice = new CustomChoice(m_selectionCard, wxID_ANY, wxDefaultPosition, dip(150, 32), keys);
    m_modifierKeyChoice->SetSelection(0);

    modKeySizer->Add(m_modifierKeyLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    modKeySizer->Add(m_modifierKeyChoice, 0, wxALIGN_CENTER_VERTICAL);
    selSizer->Add(modKeySizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    // 4. 保护剪贴板复选框
    m_preserveClipCheck = new wxCheckBox(m_selectionCard, wxID_ANY, L"保护剪贴板（划词提取完成后自动恢复系统原剪贴板内容，避免污染复制历史）");
    m_preserveClipCheck->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    PlatformThemeHelper::ApplyControlTheme(m_preserveClipCheck, palette);
    selSizer->Add(m_preserveClipCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    // 5. 保存按钮与状态
    wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    m_selectionSaveBtn = new CustomButton(m_selectionCard, wxID_ANY, L"保存划词设置", ButtonStyle::Primary, wxDefaultPosition, dip(145, 38));
    m_selectionSaveBtn->SetIcon(SVG::SAVE, dip(15, 15), *wxWHITE);
    m_selectionStatusText = new wxStaticText(m_selectionCard, wxID_ANY, "");
    m_selectionStatusText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_selectionStatusText->SetForegroundColour(palette.accentGreen);

    btnSizer->Add(m_selectionSaveBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12_dip);
    btnSizer->Add(m_selectionStatusText, 0, wxALIGN_CENTER_VERTICAL);
    selSizer->Add(btnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    m_selectionCard->SetSizer(selSizer);
    m_mainSizer->Add(m_selectionCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    // ====================================================================
    // Group 4: StarDict 词典设置卡片
    // ====================================================================
    m_dictCard = new wxPanel(m_contentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_dictCard->SetBackgroundColour(palette.cardBg);
    SetupCardStyle(m_dictCard);

    wxBoxSizer* dictSizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* dictTitleSizer = new wxBoxSizer(wxHORIZONTAL);
    wxBitmapBundle dictBundle = IconManager::GetIconBundle(SVG::DICTIONARY, wxSize(18, 18), palette.accentPrimary);
    m_dictTitleIcon = new wxStaticBitmap(m_dictCard, wxID_ANY, dictBundle);

    m_dictTitleText = new wxStaticText(m_dictCard, wxID_ANY, L"本地 StarDict 词典设置");
    m_dictTitleText->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_dictTitleText->SetForegroundColour(palette.textPrimary);

    m_dictStatusBadge = new StatusBadge(m_dictCard);

    dictTitleSizer->Add(m_dictTitleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    dictTitleSizer->Add(m_dictTitleText, 0, wxALIGN_CENTER_VERTICAL);
    dictTitleSizer->AddStretchSpacer(1);
    dictTitleSizer->Add(m_dictStatusBadge, 0, wxALIGN_CENTER_VERTICAL);
    dictSizer->Add(dictTitleSizer, 0, wxEXPAND | wxALL, 16_dip);

    // 目录选择行
    wxBoxSizer* dictDirRow = new wxBoxSizer(wxHORIZONTAL);
    m_dictDirLabel = new wxStaticText(m_dictCard, wxID_ANY, L"词典目录", wxDefaultPosition, wxSize(70_dip, -1));
    m_dictDirLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_dictDirLabel->SetForegroundColour(palette.textPrimary);

    m_dictDirPathCtrl = new CustomInputBox(m_dictCard, wxID_ANY, L"", L"指定包含 StarDict 词典 (.ifo / .idx / .dict) 的文件夹路径", wxDefaultPosition, wxSize(-1, 38_dip));
    m_dictDirPathCtrl->SetPrefixIcon(SVG::DICTIONARY, dip(16, 16));

    m_dictBrowseBtn = new CustomButton(m_dictCard, wxID_ANY, L"浏览", ButtonStyle::Secondary, wxDefaultPosition, dip(90, 38));
    m_dictBrowseBtn->SetIcon(SVG::BROWSE, dip(16, 16));

    m_dictOpenDirBtn = new CustomButton(m_dictCard, wxID_ANY, L"打开目录", ButtonStyle::Secondary, wxDefaultPosition, dip(110, 38));
    m_dictOpenDirBtn->SetIcon(SVG::FOLDER_OPEN, dip(16, 16));

    dictDirRow->Add(m_dictDirLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16_dip);
    dictDirRow->Add(m_dictDirPathCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    dictDirRow->Add(m_dictBrowseBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    dictDirRow->Add(m_dictOpenDirBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16_dip);
    dictSizer->Add(dictDirRow, 0, wxEXPAND | wxBOTTOM, 12_dip);

    // 操作按钮行
    wxBoxSizer* dictActionRow = new wxBoxSizer(wxHORIZONTAL);
    m_dictSaveBtn = new CustomButton(m_dictCard, wxID_ANY, L"保存词典设置", ButtonStyle::Primary, wxDefaultPosition, dip(145, 38));
    m_dictSaveBtn->SetIcon(SVG::SAVE, dip(15, 15), *wxWHITE);

    m_dictReloadBtn = new CustomButton(m_dictCard, wxID_ANY, L"重新扫描词典", ButtonStyle::Secondary, wxDefaultPosition, dip(145, 38));
    m_dictReloadBtn->SetIcon(SVG::REPLACE, dip(15, 15));

    m_dictStatusText = new wxStaticText(m_dictCard, wxID_ANY, "");
    m_dictStatusText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_dictStatusText->SetForegroundColour(palette.accentGreen);

    dictActionRow->Add(m_dictSaveBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);
    dictActionRow->Add(m_dictReloadBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12_dip);
    dictActionRow->Add(m_dictStatusText, 0, wxALIGN_CENTER_VERTICAL);
    dictSizer->Add(dictActionRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    // 已识别词典摘要展示
    m_dictListTitleText = new wxStaticText(m_dictCard, wxID_ANY, L"当前已识别加载的词典：");
    m_dictListTitleText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_dictListTitleText->SetForegroundColour(palette.textSecondary);
    dictSizer->Add(m_dictListTitleText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6_dip);

    m_dictListContainer = new wxPanel(m_dictCard, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_dictListContainer->SetBackgroundColour(palette.cardBg);
    m_dictListSizer = new wxBoxSizer(wxVERTICAL);
    m_dictListContainer->SetSizer(m_dictListSizer);
    dictSizer->Add(m_dictListContainer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12_dip);

    // 底部词典下载链接说明
    wxBoxSizer* dictFooterSizer = new wxBoxSizer(wxHORIZONTAL);
    wxBitmapBundle dictInfoBundle = IconManager::GetIconBundle(SVG::INFO, wxSize(15, 15), palette.accentPrimary);
    m_dictInfoIcon = new wxStaticBitmap(m_dictCard, wxID_ANY, dictInfoBundle);

    wxStaticText* dictFooterLabel = new wxStaticText(m_dictCard, wxID_ANY, L"免费词典库下载：");
    dictFooterLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    dictFooterLabel->SetForegroundColour(palette.textSecondary);

    m_dictDownloadLink = new wxHyperlinkCtrl(m_dictCard, wxID_ANY, L"StarDict 免费离线词典库 (stardict.uber.space)", "https://stardict.uber.space/");
    m_dictDownloadLink->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_dictDownloadLink->SetNormalColour(palette.accentPrimary);
    m_dictDownloadLink->SetHoverColour(palette.accentHover);

    dictFooterSizer->Add(m_dictInfoIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6_dip);
    dictFooterSizer->Add(dictFooterLabel, 0, wxALIGN_CENTER_VERTICAL);
    dictFooterSizer->Add(m_dictDownloadLink, 0, wxALIGN_CENTER_VERTICAL);
    dictSizer->Add(dictFooterSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    m_dictCard->SetSizer(dictSizer);
    m_mainSizer->Add(m_dictCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    // ====================================================================
    // Group 5: 日志与诊断设置卡片
    // ====================================================================
    m_logCard = new wxPanel(m_contentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_logCard->SetBackgroundColour(palette.cardBg);
    SetupCardStyle(m_logCard);

    wxBoxSizer* logSizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* logTitleSizer = new wxBoxSizer(wxHORIZONTAL);
    wxBitmapBundle logBundle = IconManager::GetIconBundle(SVG::LOG, wxSize(18, 18), palette.accentPrimary);
    m_logTitleIcon = new wxStaticBitmap(m_logCard, wxID_ANY, logBundle);

    m_logTitleText = new wxStaticText(m_logCard, wxID_ANY, L"运行日志与诊断设置");
    m_logTitleText->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_logTitleText->SetForegroundColour(palette.textPrimary);

    logTitleSizer->Add(m_logTitleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    logTitleSizer->Add(m_logTitleText, 0, wxALIGN_CENTER_VERTICAL);
    logSizer->Add(logTitleSizer, 0, wxALL, 16_dip);

    // 1. 保存日志到文件开关
    m_saveLogToFileCheck = new wxCheckBox(m_logCard, wxID_ANY, L"保存运行日志到本地文件（便于问题排查与诊断）");
    m_saveLogToFileCheck->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    PlatformThemeHelper::ApplyControlTheme(m_saveLogToFileCheck, palette);
    logSizer->Add(m_saveLogToFileCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12_dip);

    // 2. 日志文件保存路径说明
    wxString logPathStr = wxString::FromUTF8(ConfigManager::GetDefaultLogFilePath());
    m_logPathInfoText = new wxStaticText(m_logCard, wxID_ANY, L"日志文件保存位置：" + logPathStr);
    m_logPathInfoText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_logPathInfoText->SetForegroundColour(palette.textSecondary);
    logSizer->Add(m_logPathInfoText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    // 3. 按钮行
    wxBoxSizer* logBtnSizer = new wxBoxSizer(wxHORIZONTAL);
    m_logSaveBtn = new CustomButton(m_logCard, wxID_ANY, L"保存日志设置", ButtonStyle::Primary, wxDefaultPosition, dip(145, 38));
    m_logSaveBtn->SetIcon(SVG::SAVE, dip(15, 15), *wxWHITE);
    m_openLogDirBtn = new CustomButton(m_logCard, wxID_ANY, L"打开日志目录", ButtonStyle::Secondary, wxDefaultPosition, dip(145, 38));
    m_openLogDirBtn->SetIcon(SVG::FOLDER_OPEN, dip(15, 15));
    m_logStatusText = new wxStaticText(m_logCard, wxID_ANY, "");
    m_logStatusText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_logStatusText->SetForegroundColour(palette.accentGreen);

    logBtnSizer->Add(m_logSaveBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    logBtnSizer->Add(m_openLogDirBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12_dip);
    logBtnSizer->Add(m_logStatusText, 0, wxALIGN_CENTER_VERTICAL);
    logSizer->Add(logBtnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    m_logCard->SetSizer(logSizer);
    m_mainSizer->Add(m_logCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    // ====================================================================
    // Group 6: 偏好设置与关于卡片
    // ====================================================================
    m_prefCard = new wxPanel(m_contentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_prefCard->SetBackgroundColour(palette.cardBg);
    SetupCardStyle(m_prefCard);

    wxBoxSizer* prefSizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* prefTitleSizer = new wxBoxSizer(wxHORIZONTAL);
    wxBitmapBundle moonBundle = IconManager::GetIconBundle(SVG::MOON, wxSize(18, 18), palette.accentPrimary);
    m_prefTitleIcon = new wxStaticBitmap(m_prefCard, wxID_ANY, moonBundle);

    m_prefTitle = new wxStaticText(m_prefCard, wxID_ANY, L"界面外观与关于");
    m_prefTitle->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_prefTitle->SetForegroundColour(palette.textPrimary);

    prefTitleSizer->Add(m_prefTitleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    prefTitleSizer->Add(m_prefTitle, 0, wxALIGN_CENTER_VERTICAL);
    prefSizer->Add(prefTitleSizer, 0, wxALL, 16_dip);

    // 主题选择单选选项组 (浅色、暗色、跟随系统)
    wxArrayString themeOptions;
    themeOptions.Add(L"浅色模式");
    themeOptions.Add(L"暗色模式");
    themeOptions.Add(L"跟随系统");
    m_themeRadioBox = new LeftAlignedRadioBox(m_prefCard, wxID_ANY, L"主题外观选择", wxDefaultPosition, wxDefaultSize, themeOptions, 3, wxRA_SPECIFY_COLS);
    m_themeRadioBox->SetFont(ThemeFont::GetFont(FontRole::Control));
    PlatformThemeHelper::ApplyControlTheme(m_themeRadioBox, palette);
    prefSizer->Add(m_themeRadioBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    // 关于说明栏
    wxBoxSizer* aboutRowSizer = new wxBoxSizer(wxHORIZONTAL);
    m_aboutBtn = new CustomButton(m_prefCard, wxID_ANY, L"关于 LinguaAlpaca", ButtonStyle::Secondary, wxDefaultPosition, dip(160, 36));
    m_aboutBtn->SetIcon(SVG::INFO, dip(15, 15));

    m_aboutDescText = new wxStaticText(m_prefCard, wxID_ANY, L"查看应用版本、项目初衷、开源协议与主页链接");
    m_aboutDescText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_aboutDescText->SetForegroundColour(palette.textSecondary);

    aboutRowSizer->Add(m_aboutBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14_dip);
    aboutRowSizer->Add(m_aboutDescText, 0, wxALIGN_CENTER_VERTICAL);
    prefSizer->Add(aboutRowSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

    m_prefCard->SetSizer(prefSizer);
    m_mainSizer->Add(m_prefCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20_dip);

    m_contentPanel->SetSizer(m_mainSizer);
    Layout();
    UpdateLayoutAndScroll();

    // 绑定窗口尺寸调整事件
    Bind(wxEVT_SIZE, &SettingsView::OnSize, this);
    m_viewport->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        UpdateLayoutAndScroll();
        event.Skip();
    });

    // 递归绑定鼠标滚轮事件到所有子控件，确保在任意卡片或控件上滚动均可平滑翻页
    BindMouseWheelRecursively(m_viewport);
    BindMouseWheelRecursively(m_topStickyPanel);

    // 初始化已保存的配置数据
    if (m_configManager) {
        auto cfg = m_configManager->GetConfig();
        SetModelPath(wxString::FromUTF8(cfg.modelPath));
        SetOcrModelPath(wxString::FromUTF8(cfg.ocrModelPath), wxString::FromUTF8(cfg.ocrMmprojPath));

        // 翻译模型参数回显
        if (m_modelNglCtrl)
            m_modelNglCtrl->SetValue(wxString::Format("%d", cfg.gpuLayers));
        if (m_modelPortCtrl)
            m_modelPortCtrl->SetValue(wxString::Format("%d", cfg.translationPort));
        if (m_modelCtxCtrl)
            m_modelCtxCtrl->SetValue(wxString::Format("%d", cfg.translationCtxSize));
        if (m_modelGpuModeChoice) {
            int sel = (cfg.gpuLayers >= 99) ? 0 : (cfg.gpuLayers == 0 ? 1 : 2);
            m_modelGpuModeChoice->SetSelection(sel);
            bool isCustom = (sel == 2);
            if (m_modelNglLabel)
                m_modelNglLabel->Show(isCustom);
            if (m_modelNglCtrl)
                m_modelNglCtrl->Show(isCustom);
        }

        // OCR 模型参数回显
        if (m_ocrNglCtrl)
            m_ocrNglCtrl->SetValue(wxString::Format("%d", cfg.ocrGpuLayers));
        if (m_ocrPortCtrl)
            m_ocrPortCtrl->SetValue(wxString::Format("%d", cfg.ocrPort));
        if (m_ocrCtxCtrl)
            m_ocrCtxCtrl->SetValue(wxString::Format("%d", cfg.ocrCtxSize));
        if (m_ocrMmprojOffloadCheck)
            m_ocrMmprojOffloadCheck->SetValue(cfg.ocrMmprojOffload);
        if (m_ocrGpuModeChoice) {
            int sel = (cfg.ocrGpuLayers == 0) ? 0 : (cfg.ocrGpuLayers >= 99 ? 1 : 2);
            m_ocrGpuModeChoice->SetSelection(sel);
            bool isCustom = (sel == 2);
            if (m_ocrNglLabel)
                m_ocrNglLabel->Show(isCustom);
            if (m_ocrNglCtrl)
                m_ocrNglCtrl->Show(isCustom);
        }

        SetSelectionConfig(cfg);
        SetDictConfig(cfg);
        SetLogConfig(cfg);
        SetThemeConfig(cfg);
    }

    // 事件绑定 - 翻译模型 Group
    m_browseBtn->Bind(wxEVT_BUTTON, &SettingsView::OnBrowseModel, this);
    m_openDirBtn->Bind(wxEVT_BUTTON, &SettingsView::OnOpenModelDir, this);
    m_modelGpuModeChoice->Bind(wxEVT_CHOICE, &SettingsView::OnModelGpuModeChanged, this);
    m_modelCopyApiBtn->Bind(wxEVT_BUTTON, &SettingsView::OnCopyModelApiUrl, this);
    m_saveBtn->Bind(wxEVT_BUTTON, &SettingsView::OnSaveConfig, this);
    m_startBtn->Bind(wxEVT_BUTTON, &SettingsView::OnStartModel, this);
    m_stopBtn->Bind(wxEVT_BUTTON, &SettingsView::OnStopModel, this);
    m_testBtn->Bind(wxEVT_BUTTON, &SettingsView::OnTestModel, this);

    // 事件绑定 - OCR 模型 Group
    m_ocrBrowseBtn->Bind(wxEVT_BUTTON, &SettingsView::OnBrowseOcrModel, this);
    m_ocrMmprojBrowseBtn->Bind(wxEVT_BUTTON, &SettingsView::OnBrowseOcrMmproj, this);
    m_ocrGpuModeChoice->Bind(wxEVT_CHOICE, &SettingsView::OnOcrGpuModeChanged, this);
    m_ocrCopyApiBtn->Bind(wxEVT_BUTTON, &SettingsView::OnCopyOcrApiUrl, this);
    m_ocrSaveBtn->Bind(wxEVT_BUTTON, &SettingsView::OnSaveOcrConfig, this);
    m_ocrStartBtn->Bind(wxEVT_BUTTON, &SettingsView::OnStartOcrModel, this);
    m_ocrStopBtn->Bind(wxEVT_BUTTON, &SettingsView::OnStopOcrModel, this);
    m_ocrTestBtn->Bind(wxEVT_BUTTON, &SettingsView::OnTestOcrModel, this);

    // 事件绑定 - 划词翻译 Group
    m_selectionModeRadio->Bind(wxEVT_RADIOBOX, &SettingsView::OnSelectionModeChanged, this);
    m_selectionSaveBtn->Bind(wxEVT_BUTTON, &SettingsView::OnSaveSelectionConfig, this);

    // 事件绑定 - 词典设置 Group
    m_dictBrowseBtn->Bind(wxEVT_BUTTON, &SettingsView::OnBrowseDictDir, this);
    m_dictOpenDirBtn->Bind(wxEVT_BUTTON, &SettingsView::OnOpenDictDir, this);
    m_dictSaveBtn->Bind(wxEVT_BUTTON, &SettingsView::OnSaveDictConfig, this);
    m_dictReloadBtn->Bind(wxEVT_BUTTON, &SettingsView::OnReloadDicts, this);

    // 事件绑定 - 日志与诊断 Group
    m_logSaveBtn->Bind(wxEVT_BUTTON, &SettingsView::OnSaveLogConfig, this);
    m_openLogDirBtn->Bind(wxEVT_BUTTON, &SettingsView::OnOpenLogDirFromSettings, this);

    // 事件绑定 - 偏好与关于 Group
    m_themeRadioBox->Bind(wxEVT_RADIOBOX, &SettingsView::OnThemeRadioChanged, this);
    m_aboutBtn->Bind(wxEVT_BUTTON, &SettingsView::OnShowAboutDialog, this);

    UpdateTranslationStatus();
    UpdateOcrStatus();
}

void SettingsView::SetModelPath(const wxString& path) {
    m_configuredPath = path;
    if (m_modelPathCtrl) {
        m_modelPathCtrl->SetValue(path);
    }
    m_lastTransPath.clear();
    UpdateTranslationStatus();
}

void SettingsView::SetOcrModelPath(const wxString& mainPath, const wxString& mmprojPath) {
    if (m_ocrModelPathCtrl)
        m_ocrModelPathCtrl->SetValue(mainPath);
    if (m_ocrMmprojPathCtrl)
        m_ocrMmprojPathCtrl->SetValue(mmprojPath);

    m_lastOcrMainPath.clear();
    UpdateOcrStatus();
}

void SettingsView::UpdateTranslationStatus() {
    if (!m_statusBadge || !m_modelManager)
        return;

    auto info = m_modelManager->GetHealthStatus(TargetModelType::Translation);
    wxString curPath = m_modelPathCtrl ? m_modelPathCtrl->GetValue() : L"";

    // 磁盘 I/O 缓存：仅在模型路径变化时检查文件是否存在
    if (curPath != m_lastTransPath) {
        m_lastTransPath = curPath;
        m_lastTransFileExists = (!curPath.IsEmpty() && wxFileExists(curPath));
    }

    // 状态与端口无变化时直接跳过，避免每 1500ms 重复刷新 UI 控件
    if (info.state == m_lastTransState && info.port == m_lastTransPort) {
        return;
    }
    m_lastTransState = info.state;
    m_lastTransPort = info.port;

    auto palette = ThemeColors::GetCurrentPalette();

    if (info.state == ServerHealthState::Ready) {
        wxString label = wxString::Format(L"● 已就绪 (端口: %d)", info.port);
        m_statusBadge->SetStatus(ServerHealthState::Ready, label);

        if (m_modelApiStatusText) {
            m_modelApiStatusText->SetLabel(wxString::Format(L"● 运行中 (端口: %d)", info.port));
            m_modelApiStatusText->SetForegroundColour(palette.accentGreen);
        }
        if (m_modelApiUrlText) {
            m_modelApiUrlText->SetLabel(wxString::Format(L"OpenAI 兼容接口: http://127.0.0.1:%d/v1/chat/completions", info.port));
            m_modelApiUrlText->SetForegroundColour(palette.textPrimary);
        }
    } else if (info.state == ServerHealthState::Loading) {
        m_statusBadge->SetStatus(ServerHealthState::Loading, L"● 正在加载中...");
        if (m_modelApiStatusText) {
            m_modelApiStatusText->SetLabel(L"● 正在拉起或装载权重中...");
            m_modelApiStatusText->SetForegroundColour(palette.accentPrimary);
        }
    } else if (info.state == ServerHealthState::Unconfigured) {
        m_statusBadge->SetStatus(ServerHealthState::Unconfigured, L"● 未配置模型");
        if (m_modelApiStatusText) {
            m_modelApiStatusText->SetLabel(L"● 未配置模型路径");
            m_modelApiStatusText->SetForegroundColour(palette.textSecondary);
        }
        if (m_modelApiUrlText) {
            m_modelApiUrlText->SetLabel(L"OpenAI 兼容接口: 待配置并启动后分配");
            m_modelApiUrlText->SetForegroundColour(palette.textSecondary);
        }
    } else {
        if (m_lastTransFileExists) {
            m_statusBadge->SetStatus(ServerHealthState::Offline, L"● 服务离线 (已就绪)");
            if (m_modelApiStatusText) {
                m_modelApiStatusText->SetLabel(L"● 服务离线 (调用或点击「启动服务」时自动运行)");
                m_modelApiStatusText->SetForegroundColour(palette.textSecondary);
            }
        } else {
            m_statusBadge->SetStatus(ServerHealthState::Unconfigured, L"● 未配置模型");
            if (m_modelApiStatusText) {
                m_modelApiStatusText->SetLabel(L"● 模型未配置");
                m_modelApiStatusText->SetForegroundColour(palette.textSecondary);
            }
        }
        if (m_modelApiUrlText) {
            m_modelApiUrlText->SetLabel(L"OpenAI 兼容接口: 待启动后分配");
            m_modelApiUrlText->SetForegroundColour(palette.textSecondary);
        }
    }
}

void SettingsView::UpdateOcrStatus() {
    if (!m_ocrStatusBadge || !m_modelManager)
        return;

    auto info = m_modelManager->GetHealthStatus(TargetModelType::Ocr);
    wxString curMainPath = m_ocrModelPathCtrl ? m_ocrModelPathCtrl->GetValue() : L"";
    wxString curMmprojPath = m_ocrMmprojPathCtrl ? m_ocrMmprojPathCtrl->GetValue() : L"";

    // 磁盘 I/O 缓存：仅在模型或 mmproj 路径变化时才检查文件是否存在
    if (curMainPath != m_lastOcrMainPath || curMmprojPath != m_lastOcrMmprojPath) {
        m_lastOcrMainPath = curMainPath;
        m_lastOcrMmprojPath = curMmprojPath;
        m_lastOcrFileExists = (!curMainPath.IsEmpty() && !curMmprojPath.IsEmpty() &&
                               wxFileExists(curMainPath) && wxFileExists(curMmprojPath));
    }

    // 状态与端口无变化时直接跳过
    if (info.state == m_lastOcrState && info.port == m_lastOcrPort) {
        return;
    }
    m_lastOcrState = info.state;
    m_lastOcrPort = info.port;

    auto palette = ThemeColors::GetCurrentPalette();

    if (info.state == ServerHealthState::Ready) {
        wxString label = wxString::Format(L"● 已就绪 (端口: %d)", info.port);
        m_ocrStatusBadge->SetStatus(ServerHealthState::Ready, label);

        if (m_ocrApiStatusText) {
            m_ocrApiStatusText->SetLabel(wxString::Format(L"● 运行中 (端口: %d)", info.port));
            m_ocrApiStatusText->SetForegroundColour(palette.accentGreen);
        }
        if (m_ocrApiUrlText) {
            m_ocrApiUrlText->SetLabel(wxString::Format(L"OpenAI 兼容接口: http://127.0.0.1:%d/v1/chat/completions", info.port));
            m_ocrApiUrlText->SetForegroundColour(palette.textPrimary);
        }
    } else if (info.state == ServerHealthState::Loading) {
        m_ocrStatusBadge->SetStatus(ServerHealthState::Loading, L"● 正在加载中...");
        if (m_ocrApiStatusText) {
            m_ocrApiStatusText->SetLabel(L"● 正在拉起或装载 OCR 模型中...");
            m_ocrApiStatusText->SetForegroundColour(palette.accentPrimary);
        }
    } else if (info.state == ServerHealthState::Unconfigured) {
        m_ocrStatusBadge->SetStatus(ServerHealthState::Unconfigured, L"● 未配置模型");
        if (m_ocrApiStatusText) {
            m_ocrApiStatusText->SetLabel(L"● 未配置 OCR 主模型或 mmproj");
            m_ocrApiStatusText->SetForegroundColour(palette.textSecondary);
        }
        if (m_ocrApiUrlText) {
            m_ocrApiUrlText->SetLabel(L"OpenAI 兼容接口: 待配置并启动后分配");
            m_ocrApiUrlText->SetForegroundColour(palette.textSecondary);
        }
    } else {
        if (m_lastOcrFileExists) {
            m_ocrStatusBadge->SetStatus(ServerHealthState::Offline, L"● 服务离线 (已就绪)");
            if (m_ocrApiStatusText) {
                m_ocrApiStatusText->SetLabel(L"● 服务离线 (调用或点击「启动服务」时自动运行)");
                m_ocrApiStatusText->SetForegroundColour(palette.textSecondary);
            }
        } else {
            m_ocrStatusBadge->SetStatus(ServerHealthState::Unconfigured, L"● 未配置");
            if (m_ocrApiStatusText) {
                m_ocrApiStatusText->SetLabel(L"● 模型未配置");
                m_ocrApiStatusText->SetForegroundColour(palette.textSecondary);
            }
        }
        if (m_ocrApiUrlText) {
            m_ocrApiUrlText->SetLabel(L"OpenAI 兼容接口: 待启动后分配");
            m_ocrApiUrlText->SetForegroundColour(palette.textSecondary);
        }
    }
}

void SettingsView::OnBrowseModel(wxCommandEvent& WXUNUSED(event)) {
    wxFileDialog openFileDialog(this, L"选择 GGUF 翻译大模型文件", "", "", "GGUF Model Files (*.gguf)|*.gguf|All Files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (openFileDialog.ShowModal() == wxID_OK) {
        wxString path = openFileDialog.GetPath();
        SetModelPath(path);
    }
}

void SettingsView::OnBrowseOcrModel(wxCommandEvent& WXUNUSED(event)) {
    wxFileDialog openFileDialog(this, L"选择 OCR 主模型文件路径", "", "", "GGUF Model Files (*.gguf)|*.gguf|All Files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (openFileDialog.ShowModal() == wxID_OK) {
        wxString path = openFileDialog.GetPath();
        if (m_ocrModelPathCtrl)
            m_ocrModelPathCtrl->SetValue(path);
        UpdateOcrStatus();
    }
}

void SettingsView::OnBrowseOcrMmproj(wxCommandEvent& WXUNUSED(event)) {
    wxFileDialog openFileDialog(this, L"选择 mmproj 视觉投影器文件路径", "", "",
                                "mmproj Files (*.mmproj;*.gguf)|*.mmproj;*.gguf|mmproj Files (*.mmproj)|*.mmproj|GGUF Files (*.gguf)|*.gguf|All Files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (openFileDialog.ShowModal() == wxID_OK) {
        wxString path = openFileDialog.GetPath();
        if (m_ocrMmprojPathCtrl)
            m_ocrMmprojPathCtrl->SetValue(path);
        UpdateOcrStatus();
    }
}

void SettingsView::OnOpenModelDir(wxCommandEvent& WXUNUSED(event)) {
    wxString modelDir = wxString::FromUTF8(Downloader::GetDefaultModelDir());
    wxLaunchDefaultBrowser(modelDir);
}

void SettingsView::OnModelGpuModeChanged(wxCommandEvent& WXUNUSED(event)) {
    if (!m_modelGpuModeChoice || !m_modelNglCtrl)
        return;
    int sel = m_modelGpuModeChoice->GetSelection();
    bool isCustom = (sel == 2);
    if (sel == 0) {
        m_modelNglCtrl->SetValue("99");
    } else if (sel == 1) {
        m_modelNglCtrl->SetValue("0");
    }
    if (m_modelNglLabel)
        m_modelNglLabel->Show(isCustom);
    m_modelNglCtrl->Show(isCustom);
    if (m_modelCard)
        m_modelCard->Layout();
    UpdateLayoutAndScroll();
}

void SettingsView::OnOcrGpuModeChanged(wxCommandEvent& WXUNUSED(event)) {
    if (!m_ocrGpuModeChoice || !m_ocrNglCtrl)
        return;
    int sel = m_ocrGpuModeChoice->GetSelection();
    bool isCustom = (sel == 2);
    if (sel == 0) {
        m_ocrNglCtrl->SetValue("0");
        if (m_ocrMmprojOffloadCheck)
            m_ocrMmprojOffloadCheck->SetValue(false);
    } else if (sel == 1) {
        m_ocrNglCtrl->SetValue("99");
        if (m_ocrMmprojOffloadCheck)
            m_ocrMmprojOffloadCheck->SetValue(true);
    }
    if (m_ocrNglLabel)
        m_ocrNglLabel->Show(isCustom);
    m_ocrNglCtrl->Show(isCustom);
    if (m_ocrCard)
        m_ocrCard->Layout();
    UpdateLayoutAndScroll();
}

void SettingsView::CopyApiUrl(TargetModelType type, const wxString& modelTypeName) {
    if (!m_modelManager)
        return;
    auto info = m_modelManager->GetHealthStatus(type);
    int port = info.port;
    if (port <= 0 && m_configManager) {
        auto cfg = m_configManager->GetConfig();
        port = (type == TargetModelType::Translation) ? cfg.translationPort : cfg.ocrPort;
    }
    wxString url = (port > 0) ? wxString::Format("http://127.0.0.1:%d/v1/chat/completions", port) : "http://127.0.0.1:<port>/v1/chat/completions";

    if (ClipboardHelper::SetClipboardText(url.ToUTF8().data())) {
        wxMessageBox(modelTypeName + L" API 接口端点已复制到剪贴板：\n" + url, L"复制成功", wxOK | wxICON_INFORMATION, this);
    }
}

void SettingsView::OnCopyModelApiUrl(wxCommandEvent& WXUNUSED(event)) {
    CopyApiUrl(TargetModelType::Translation, L"翻译模型");
}

void SettingsView::OnCopyOcrApiUrl(wxCommandEvent& WXUNUSED(event)) {
    CopyApiUrl(TargetModelType::Ocr, L"OCR 视觉模型");
}

void SettingsView::OnSaveConfig(wxCommandEvent& WXUNUSED(event)) {
    wxString path = m_modelPathCtrl ? m_modelPathCtrl->GetValue() : "";
    long ngl = 99;
    if (m_modelNglCtrl)
        m_modelNglCtrl->GetValue().ToLong(&ngl);
    long port = 0;
    if (m_modelPortCtrl)
        m_modelPortCtrl->GetValue().ToLong(&port);
    long ctxSize = 2048;
    if (m_modelCtxCtrl)
        m_modelCtxCtrl->GetValue().ToLong(&ctxSize);

    if (m_configManager) {
        m_configManager->SaveModelConfig(path.ToUTF8().data(), (int)ngl, (int)port, (int)ctxSize);
    }

    m_lastTransPath.clear();
    UpdateTranslationStatus();

    if (!path.IsEmpty() && wxFileExists(path)) {
        wxMessageBox(L"翻译模型配置已成功保存！", L"系统设置", wxOK | wxICON_INFORMATION, this);
    } else {
        wxMessageBox(L"配置已保存。提示：当前指定的 GGUF 翻译模型文件路径尚未找到有效文件。", L"系统设置", wxOK | wxICON_WARNING, this);
    }
}

void SettingsView::OnSaveOcrConfig(wxCommandEvent& WXUNUSED(event)) {
    wxString ocrPath = m_ocrModelPathCtrl ? m_ocrModelPathCtrl->GetValue() : L"";
    wxString mmprojPath = m_ocrMmprojPathCtrl ? m_ocrMmprojPathCtrl->GetValue() : L"";
    long ocrNgl = 0;
    if (m_ocrNglCtrl)
        m_ocrNglCtrl->GetValue().ToLong(&ocrNgl);
    long ocrPort = 0;
    if (m_ocrPortCtrl)
        m_ocrPortCtrl->GetValue().ToLong(&ocrPort);
    long ocrCtx = 4096;
    if (m_ocrCtxCtrl)
        m_ocrCtxCtrl->GetValue().ToLong(&ocrCtx);
    bool mmprojOffload = m_ocrMmprojOffloadCheck ? m_ocrMmprojOffloadCheck->GetValue() : false;

    if (m_configManager) {
        m_configManager->SaveOcrConfig(ocrPath.ToUTF8().data(), mmprojPath.ToUTF8().data(), (int)ocrNgl, (int)ocrPort, (int)ocrCtx, 0, mmprojOffload);
    }

    m_lastOcrMainPath.clear();
    UpdateOcrStatus();

    if (wxFileExists(ocrPath) && wxFileExists(mmprojPath)) {
        wxMessageBox(L"OCR 主模型与 mmproj 视觉投影器配置已成功保存！", L"系统设置", wxOK | wxICON_INFORMATION, this);
    } else {
        wxMessageBox(L"配置已保存。提示：请检查 OCR 主模型与 mmproj 视觉投影器文件路径是否有效。", L"系统设置", wxOK | wxICON_WARNING, this);
    }
}

void SettingsView::OnStartModel(wxCommandEvent& WXUNUSED(event)) {
    if (!m_modelManager)
        return;
    wxCommandEvent dummy;
    OnSaveConfig(dummy);

    m_modelManager->EnsureModelAsync(TargetModelType::Translation, BindUi([this](const std::string& statusMsg) {
                                         if (m_modelApiStatusText) {
                                             m_modelApiStatusText->SetLabel(L"● " + wxString::FromUTF8(statusMsg));
                                         }
                                         UpdateTranslationStatus();
                                     }),
                                     BindUi([this](bool ok, const ServerStatusInfo& info) {
                                         UpdateTranslationStatus();
                                         if (ok) {
                                             wxMessageBox(wxString::Format(L"翻译模型服务已成功启动！\n运行端口: %d\nAPI端点: http://127.0.0.1:%d/v1/chat/completions", info.port, info.port),
                                                          L"服务启动成功", wxOK | wxICON_INFORMATION, this);
                                         } else {
                                             wxMessageBox(L"翻译模型服务启动失败: " + wxString::FromUTF8(info.message), L"服务启动失败", wxOK | wxICON_ERROR, this);
                                         }
                                     }));
}

void SettingsView::OnStopModel(wxCommandEvent& WXUNUSED(event)) {
    if (!m_modelManager)
        return;
    m_modelManager->StopModelAsync(TargetModelType::Translation, BindUi([this]() {
                                       UpdateTranslationStatus();
                                       wxMessageBox(L"翻译模型服务已停止。", L"提示", wxOK | wxICON_INFORMATION, this);
                                   }));
}

void SettingsView::OnStartOcrModel(wxCommandEvent& WXUNUSED(event)) {
    if (!m_modelManager)
        return;
    wxCommandEvent dummy;
    OnSaveOcrConfig(dummy);

    m_modelManager->EnsureModelAsync(TargetModelType::Ocr, BindUi([this](const std::string& statusMsg) {
                                         if (m_ocrApiStatusText) {
                                             m_ocrApiStatusText->SetLabel(L"● " + wxString::FromUTF8(statusMsg));
                                         }
                                         UpdateOcrStatus();
                                     }),
                                     BindUi([this](bool ok, const ServerStatusInfo& info) {
                                         UpdateOcrStatus();
                                         if (ok) {
                                             wxMessageBox(wxString::Format(L"OCR 视觉识别服务已成功启动！\n运行端口: %d\nAPI端点: http://127.0.0.1:%d/v1/chat/completions", info.port, info.port),
                                                          L"服务启动成功", wxOK | wxICON_INFORMATION, this);
                                         } else {
                                             wxMessageBox(L"OCR 视觉模型服务启动失败: " + wxString::FromUTF8(info.message), L"服务启动失败", wxOK | wxICON_ERROR, this);
                                         }
                                     }));
}

void SettingsView::OnStopOcrModel(wxCommandEvent& WXUNUSED(event)) {
    if (!m_modelManager)
        return;
    m_modelManager->StopModelAsync(TargetModelType::Ocr, BindUi([this]() {
                                       UpdateOcrStatus();
                                       wxMessageBox(L"OCR 视觉模型服务已停止。", L"提示", wxOK | wxICON_INFORMATION, this);
                                   }));
}

void SettingsView::OnTestModel(wxCommandEvent& WXUNUSED(event)) {
    wxString path = m_modelPathCtrl ? m_modelPathCtrl->GetValue() : "";
    if (path.IsEmpty() || !wxFileExists(path)) {
        wxMessageBox(L"请先配置合法的 .gguf 翻译模型文件路径！", L"测试模型失败", wxOK | wxICON_WARNING, this);
        return;
    }

    if (!m_modelManager)
        return;
    m_modelManager->EnsureModelAsync(TargetModelType::Translation, nullptr, BindUi([this](bool ok, const ServerStatusInfo& info) {
                                         UpdateTranslationStatus();
                                         if (ok) {
                                             wxMessageBox(wxString::Format(L"翻译模型运行正常！\n端口: %d\n端点: http://127.0.0.1:%d/v1/chat/completions", info.port, info.port), L"测试模型成功",
                                                          wxOK | wxICON_INFORMATION, this);
                                         } else {
                                             wxMessageBox(L"翻译模型测试失败: " + wxString::FromUTF8(info.message), L"测试失败", wxOK | wxICON_ERROR, this);
                                         }
                                     }));
}

void SettingsView::OnTestOcrModel(wxCommandEvent& WXUNUSED(event)) {
    wxString mainPath = m_ocrModelPathCtrl ? m_ocrModelPathCtrl->GetValue() : L"";
    wxString mmprojPath = m_ocrMmprojPathCtrl ? m_ocrMmprojPathCtrl->GetValue() : L"";

    bool mainOk = !mainPath.IsEmpty() && wxFileExists(mainPath);
    bool mmprojOk = !mmprojPath.IsEmpty() && wxFileExists(mmprojPath);

    if (!mainOk || !mmprojOk) {
        wxString msg = L"OCR 模型配置校验失败:\n";
        if (!mainOk)
            msg += L" - 主模型路径无效或文件缺失\n";
        if (!mmprojOk)
            msg += L" - mmproj 视觉投影器路径无效或文件缺失\n";
        wxMessageBox(msg, L"测试 OCR 模型提示", wxOK | wxICON_WARNING, this);
        return;
    }

    if (!m_modelManager)
        return;
    m_modelManager->EnsureModelAsync(TargetModelType::Ocr, nullptr, BindUi([this](bool ok, const ServerStatusInfo& info) {
                                         UpdateOcrStatus();
                                         if (ok) {
                                             wxMessageBox(wxString::Format(L"OCR 视觉模型运行正常！\n端口: %d\n端点: http://127.0.0.1:%d/v1/chat/completions", info.port, info.port), L"测试模型成功",
                                                          wxOK | wxICON_INFORMATION, this);
                                         } else {
                                             wxMessageBox(L"OCR 视觉模型测试失败: " + wxString::FromUTF8(info.message), L"测试失败", wxOK | wxICON_ERROR, this);
                                         }
                                     }));
}

void SettingsView::UpdateTheme() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    if (m_topStickyPanel) {
        m_topStickyPanel->SetBackgroundColour(palette.windowBg);
        m_topStickyPanel->Refresh();
    }
    if (m_headerTitleIcon) {
        m_headerTitleIcon->SetBitmap(IconManager::GetIconBundle(SVG::SETTINGS, wxSize(22, 22), palette.accentPrimary));
    }
    if (m_titleText)
        m_titleText->SetForegroundColour(palette.textPrimary);
    if (m_subTitleText)
        m_subTitleText->SetForegroundColour(palette.textSecondary);
    if (m_segmentedBar)
        m_segmentedBar->UpdateTheme();

    if (m_modelCard) {
        m_modelCard->SetBackgroundColour(palette.cardBg);
        m_modelCard->Refresh();
    }
    if (m_modelCardIcon) {
        m_modelCardIcon->SetBitmap(IconManager::GetIconBundle(SVG::MODEL_LOAD, wxSize(18, 18), palette.accentPrimary));
    }
    if (m_modelCardTitle)
        m_modelCardTitle->SetForegroundColour(palette.textPrimary);
    if (m_modelPathLabel)
        m_modelPathLabel->SetForegroundColour(palette.textPrimary);
    if (m_modelInfoIcon) {
        m_modelInfoIcon->SetBitmap(IconManager::GetIconBundle(SVG::INFO, wxSize(15, 15), palette.accentPrimary));
    }

    if (m_ocrCard) {
        m_ocrCard->SetBackgroundColour(palette.cardBg);
        m_ocrCard->Refresh();
    }
    if (m_ocrTitleIcon) {
        m_ocrTitleIcon->SetBitmap(IconManager::GetIconBundle(SVG::OCR, wxSize(18, 18), palette.accentPrimary));
    }
    if (m_ocrTitleText)
        m_ocrTitleText->SetForegroundColour(palette.textPrimary);
    if (m_ocrMainLabel)
        m_ocrMainLabel->SetForegroundColour(palette.textPrimary);
    if (m_ocrMmprojLabel)
        m_ocrMmprojLabel->SetForegroundColour(palette.textPrimary);
    if (m_ocrFooterPanel)
        m_ocrFooterPanel->SetBackgroundColour(palette.cardBg);
    if (m_ocrFooterText)
        m_ocrFooterText->SetForegroundColour(palette.textSecondary);
    if (m_ocrInfoIcon) {
        m_ocrInfoIcon->SetBitmap(IconManager::GetIconBundle(SVG::INFO, wxSize(15, 15), palette.accentPrimary));
    }

    if (m_prefCard) {
        m_prefCard->SetBackgroundColour(palette.cardBg);
        m_prefCard->Refresh();
    }
    if (m_prefTitleIcon) {
        m_prefTitleIcon->SetBitmap(IconManager::GetIconBundle(SVG::MOON, wxSize(18, 18), palette.accentPrimary));
    }
    if (m_prefTitle)
        m_prefTitle->SetForegroundColour(palette.textPrimary);
    if (m_themeRadioBox)
        m_themeRadioBox->SetForegroundColour(palette.textPrimary);
    if (m_aboutDescText)
        m_aboutDescText->SetForegroundColour(palette.textSecondary);
    if (m_aboutBtn)
        m_aboutBtn->Refresh();

    if (m_modelGpuLabel)
        m_modelGpuLabel->SetForegroundColour(palette.textPrimary);
    if (m_modelNglLabel)
        m_modelNglLabel->SetForegroundColour(palette.textPrimary);
    if (m_modelPortLabel)
        m_modelPortLabel->SetForegroundColour(palette.textPrimary);
    if (m_modelCtxLabel)
        m_modelCtxLabel->SetForegroundColour(palette.textPrimary);

    if (m_ocrGpuLabel)
        m_ocrGpuLabel->SetForegroundColour(palette.textPrimary);
    if (m_ocrNglLabel)
        m_ocrNglLabel->SetForegroundColour(palette.textPrimary);
    if (m_ocrPortLabel)
        m_ocrPortLabel->SetForegroundColour(palette.textPrimary);
    if (m_ocrCtxLabel)
        m_ocrCtxLabel->SetForegroundColour(palette.textPrimary);
    if (m_ocrMmprojOffloadCheck)
        PlatformThemeHelper::ApplyControlTheme(m_ocrMmprojOffloadCheck, palette);

    if (m_transModelLink) {
        m_transModelLink->SetNormalColour(palette.accentPrimary);
        m_transModelLink->SetHoverColour(palette.accentHover);
    }
    if (m_ocrModelLink) {
        m_ocrModelLink->SetNormalColour(palette.accentPrimary);
        m_ocrModelLink->SetHoverColour(palette.accentHover);
    }
    if (m_dictDownloadLink) {
        m_dictDownloadLink->SetNormalColour(palette.accentPrimary);
        m_dictDownloadLink->SetHoverColour(palette.accentHover);
    }

    if (m_modelApiPanel) {
        m_modelApiPanel->SetBackgroundColour(palette.windowBg);
        m_modelApiPanel->Refresh();
    }
    if (m_ocrApiPanel) {
        m_ocrApiPanel->SetBackgroundColour(palette.windowBg);
        m_ocrApiPanel->Refresh();
    }

    if (m_selectionCard) {
        m_selectionCard->SetBackgroundColour(palette.cardBg);
        m_selectionCard->Refresh();
    }
    if (m_selectionTitleIcon) {
        m_selectionTitleIcon->SetBitmap(IconManager::GetIconBundle(SVG::TRANSLATE, wxSize(18, 18), palette.accentPrimary));
    }
    if (m_selectionTitleText)
        m_selectionTitleText->SetForegroundColour(palette.textPrimary);
    if (m_selectionEnableCheck)
        PlatformThemeHelper::ApplyControlTheme(m_selectionEnableCheck, palette);
    if (m_selectionModeRadio)
        PlatformThemeHelper::ApplyControlTheme(m_selectionModeRadio, palette);
    if (m_modifierKeyLabel)
        m_modifierKeyLabel->SetForegroundColour(palette.textPrimary);
    if (m_preserveClipCheck)
        PlatformThemeHelper::ApplyControlTheme(m_preserveClipCheck, palette);
    if (m_selectionSaveBtn)
        m_selectionSaveBtn->Refresh();

    if (m_dictCard) {
        m_dictCard->SetBackgroundColour(palette.cardBg);
        m_dictCard->Refresh();
    }
    if (m_dictTitleIcon) {
        m_dictTitleIcon->SetBitmap(IconManager::GetIconBundle(SVG::DICTIONARY, wxSize(18, 18), palette.accentPrimary));
    }
    if (m_dictInfoIcon) {
        m_dictInfoIcon->SetBitmap(IconManager::GetIconBundle(SVG::INFO, wxSize(15, 15), palette.accentPrimary));
    }
    if (m_dictTitleText)
        m_dictTitleText->SetForegroundColour(palette.textPrimary);
    if (m_dictDirLabel)
        m_dictDirLabel->SetForegroundColour(palette.textPrimary);
    if (m_dictDirPathCtrl) {
        m_dictDirPathCtrl->SetBackgroundColour(palette.windowBg);
        m_dictDirPathCtrl->SetForegroundColour(palette.textPrimary);
    }
    if (m_dictListTitleText)
        m_dictListTitleText->SetForegroundColour(palette.textSecondary);
    if (m_dictListContainer) {
        m_dictListContainer->SetBackgroundColour(palette.cardBg);
        m_dictListContainer->Refresh();
    }

    if (m_dictBrowseBtn)
        m_dictBrowseBtn->Refresh();
    if (m_dictOpenDirBtn)
        m_dictOpenDirBtn->Refresh();
    if (m_dictSaveBtn)
        m_dictSaveBtn->Refresh();
    if (m_dictReloadBtn)
        m_dictReloadBtn->Refresh();

    if (m_logCard) {
        m_logCard->SetBackgroundColour(palette.cardBg);
        m_logCard->Refresh();
    }
    if (m_logTitleIcon) {
        m_logTitleIcon->SetBitmap(IconManager::GetIconBundle(SVG::LOG, wxSize(18, 18), palette.accentPrimary));
    }
    if (m_logTitleText)
        m_logTitleText->SetForegroundColour(palette.textPrimary);
    if (m_saveLogToFileCheck)
        PlatformThemeHelper::ApplyControlTheme(m_saveLogToFileCheck, palette);
    if (m_themeRadioBox)
        PlatformThemeHelper::ApplyControlTheme(m_themeRadioBox, palette);
    if (m_logPathInfoText)
        m_logPathInfoText->SetForegroundColour(palette.textSecondary);
    if (m_logSaveBtn)
        m_logSaveBtn->Refresh();
    if (m_openLogDirBtn)
        m_openLogDirBtn->Refresh();

    if (m_viewport)
        m_viewport->SetBackgroundColour(palette.windowBg);
    if (m_contentPanel)
        m_contentPanel->SetBackgroundColour(palette.windowBg);
    if (m_scrollBar) {
        m_scrollBar->SetBackgroundColour(palette.windowBg);
        m_scrollBar->Refresh();
    }

    if (m_modelPathCtrl)
        m_modelPathCtrl->UpdateTheme();
    if (m_modelNglCtrl)
        m_modelNglCtrl->UpdateTheme();
    if (m_modelPortCtrl)
        m_modelPortCtrl->UpdateTheme();
    if (m_modelCtxCtrl)
        m_modelCtxCtrl->UpdateTheme();
    if (m_modelGpuModeChoice)
        m_modelGpuModeChoice->UpdateTheme();
    if (m_modelCopyApiBtn)
        m_modelCopyApiBtn->Refresh();

    if (m_browseBtn)
        m_browseBtn->Refresh();
    if (m_openDirBtn)
        m_openDirBtn->Refresh();
    if (m_saveBtn)
        m_saveBtn->Refresh();
    if (m_startBtn)
        m_startBtn->Refresh();
    if (m_stopBtn)
        m_stopBtn->Refresh();
    if (m_testBtn)
        m_testBtn->Refresh();

    if (m_ocrModelPathCtrl)
        m_ocrModelPathCtrl->UpdateTheme();
    if (m_ocrMmprojPathCtrl)
        m_ocrMmprojPathCtrl->UpdateTheme();
    if (m_ocrNglCtrl)
        m_ocrNglCtrl->UpdateTheme();
    if (m_ocrPortCtrl)
        m_ocrPortCtrl->UpdateTheme();
    if (m_ocrCtxCtrl)
        m_ocrCtxCtrl->UpdateTheme();
    if (m_ocrGpuModeChoice)
        m_ocrGpuModeChoice->UpdateTheme();
    if (m_ocrCopyApiBtn)
        m_ocrCopyApiBtn->Refresh();

    if (m_dictDirPathCtrl)
        m_dictDirPathCtrl->UpdateTheme();

    if (m_ocrBrowseBtn)
        m_ocrBrowseBtn->Refresh();
    if (m_ocrMmprojBrowseBtn)
        m_ocrMmprojBrowseBtn->Refresh();
    if (m_ocrSaveBtn)
        m_ocrSaveBtn->Refresh();
    if (m_ocrStartBtn)
        m_ocrStartBtn->Refresh();
    if (m_ocrStopBtn)
        m_ocrStopBtn->Refresh();
    if (m_ocrTestBtn)
        m_ocrTestBtn->Refresh();

    // 强制重置状态缓存，以便根据新主题调色板重新绘制颜色
    m_lastTransState = ServerHealthState::Unconfigured;
    m_lastTransPort = -1;
    m_lastTransPath.clear();
    m_lastOcrState = ServerHealthState::Unconfigured;
    m_lastOcrPort = -1;
    m_lastOcrMainPath.clear();
    m_lastOcrMmprojPath.clear();

    UpdateTranslationStatus();
    UpdateOcrStatus();
    UpdateDictListSummary();
    UpdateLayoutAndScroll();
    Refresh();
}

void SettingsView::OnSize(wxSizeEvent& event) {
    Layout();
    UpdateLayoutAndScroll();
    event.Skip();
}

void SettingsView::OnMouseWheel(wxMouseEvent& event) {
    int rotation = event.GetWheelRotation();
    if (rotation == 0)
        return;
    int delta = (rotation > 0 ? -50_dip : 50_dip);
    ScrollTo(m_scrollOffsetY + delta);
    if (m_scrollBar) {
        m_scrollBar->NotifyActivity();
    }
}

void SettingsView::BindMouseWheelRecursively(wxWindow* win) {
    if (!win)
        return;
    if (dynamic_cast<TextCtrl*>(win)) {
        // TextCtrl 拥有独立的内部文本滚动与专属 ScrollBar，不应被父级设置页面滚轮拦截
        return;
    }
    win->Unbind(wxEVT_MOUSEWHEEL, &SettingsView::OnMouseWheel, this);
    win->Bind(wxEVT_MOUSEWHEEL, &SettingsView::OnMouseWheel, this);
    for (wxWindowList::compatibility_iterator node = win->GetChildren().GetFirst(); node; node = node->GetNext()) {
        BindMouseWheelRecursively(node->GetData());
    }
}

void SettingsView::OnSegmentChanged(int index) {
    if (!m_contentPanel)
        return;

    int targetY = 0;
    switch (index) {
    case 0:
        targetY = 0;
        break;
    case 1:
        targetY = std::max(0, m_cachedSelY - 12_dip);
        break;
    case 2:
        targetY = std::max(0, m_cachedDictY - 12_dip);
        break;
    case 3:
        targetY = std::max(0, m_cachedLogY - 12_dip);
        break;
    default:
        break;
    }

    m_isProgrammaticScrolling = true;
    ScrollTo(targetY);
    m_isProgrammaticScrolling = false;
}

void SettingsView::UpdateSegmentFromScroll() {
    if (m_isProgrammaticScrolling || !m_segmentedBar)
        return;

    int currentY = m_scrollOffsetY;
    int targetIndex = 0;

    if (m_cachedMaxScroll > 0 && currentY >= m_cachedMaxScroll - 30_dip) {
        targetIndex = 3;
    } else if (currentY + 60_dip >= m_cachedLogY) {
        targetIndex = 3;
    } else if (currentY + 60_dip >= m_cachedDictY) {
        targetIndex = 2;
    } else if (currentY + 60_dip >= m_cachedSelY) {
        targetIndex = 1;
    } else {
        targetIndex = 0;
    }

    if (m_segmentedBar->GetActiveIndex() != targetIndex) {
        m_segmentedBar->SetActiveIndex(targetIndex, false);
    }
}

void SettingsView::ScrollTo(int targetY) {
    if (!m_viewport || !m_contentPanel)
        return;

    int newOffsetY = std::clamp(targetY, 0, m_cachedMaxScroll);
    if (newOffsetY == m_scrollOffsetY)
        return;

    m_scrollOffsetY = newOffsetY;
    m_contentPanel->Move(0, -m_scrollOffsetY);

    if (m_scrollBar) {
        int vpHeight = m_viewport->GetClientSize().y;
        int contentHeight = m_contentPanel->GetSize().y;
        m_scrollBar->SetScrollParams(m_scrollOffsetY, vpHeight, contentHeight);
    }

    UpdateSegmentFromScroll();
}

void SettingsView::UpdateLayoutAndScroll() {
    if (!m_viewport || !m_contentPanel)
        return;

    int vpWidth = m_viewport->GetClientSize().x;
    int vpHeight = m_viewport->GetClientSize().y;
    if (vpWidth <= 0 || vpHeight <= 0)
        return;

    m_contentPanel->SetSize(vpWidth, -1);
    m_contentPanel->Layout();

    int contentHeight = m_mainSizer ? m_mainSizer->GetMinSize().y : m_contentPanel->GetMinSize().y;
    contentHeight = std::max(contentHeight, vpHeight);

    m_cachedMaxScroll = std::max(0, contentHeight - vpHeight);
    m_scrollOffsetY = std::clamp(m_scrollOffsetY, 0, m_cachedMaxScroll);

    // 缓存卡片相对于内容面板的垂直绝对坐标 (消除高频滚轮滚动时 Win32 IPC 查询开销)
    m_cachedSelY = m_selectionCard ? m_selectionCard->GetPosition().y : 999999;
    m_cachedDictY = m_dictCard ? m_dictCard->GetPosition().y : 999999;
    m_cachedLogY = m_logCard ? m_logCard->GetPosition().y : 999999;

    m_contentPanel->SetSize(0, -m_scrollOffsetY, vpWidth, contentHeight);

    if (m_scrollBar) {
        m_scrollBar->SetScrollParams(m_scrollOffsetY, vpHeight, contentHeight);
    }

    UpdateSegmentFromScroll();
}

void SettingsView::SetSelectionConfig(const AppConfig& cfg) {
    if (m_selectionEnableCheck) {
        m_selectionEnableCheck->SetValue(cfg.selectionTranslateEnabled);
    }
    if (m_selectionModeRadio) {
        m_selectionModeRadio->SetSelection(cfg.selectionTriggerMode);
    }
    if (m_modifierKeyChoice) {
        m_modifierKeyChoice->SetSelection(cfg.selectionModifierKey);
        m_modifierKeyChoice->Enable(cfg.selectionTriggerMode == 1);
    }
    if (m_preserveClipCheck) {
        m_preserveClipCheck->SetValue(cfg.preserveClipboard);
    }
}

void SettingsView::OnSelectionModeChanged(wxCommandEvent& WXUNUSED(event)) {
    if (m_selectionModeRadio && m_modifierKeyChoice) {
        int sel = m_selectionModeRadio->GetSelection();
        m_modifierKeyChoice->Enable(sel == 1);
    }
}

void SettingsView::OnSaveSelectionConfig(wxCommandEvent& WXUNUSED(event)) {
    if (!m_configManager)
        return;

    bool enabled = m_selectionEnableCheck ? m_selectionEnableCheck->GetValue() : true;
    int mode = m_selectionModeRadio ? m_selectionModeRadio->GetSelection() : 0;
    int modifierKey = m_modifierKeyChoice ? m_modifierKeyChoice->GetSelection() : 0;
    bool preserveClip = m_preserveClipCheck ? m_preserveClipCheck->GetValue() : true;

    m_configManager->SaveSelectionConfig(enabled, mode, modifierKey, preserveClip);

    if (m_selectionStatusText) {
        m_selectionStatusText->SetLabel(L"划词配置已保存并即时生效！");
    }
}

void SettingsView::SetDictConfig(const AppConfig& cfg) {
    if (m_dictDirPathCtrl) {
        m_dictDirPathCtrl->SetValue(wxString::FromUTF8(cfg.dictDirPath));
    }
    UpdateDictListSummary();
}

void SettingsView::OnBrowseDictDir(wxCommandEvent& WXUNUSED(event)) {
    wxString defaultDir = m_dictDirPathCtrl ? m_dictDirPathCtrl->GetValue() : wxString::FromUTF8(ConfigManager::GetDefaultDictDir());
    wxDirDialog dirDialog(this, L"选择 StarDict 词典存放目录", defaultDir, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);

    if (dirDialog.ShowModal() == wxID_OK) {
        wxString path = dirDialog.GetPath();
        if (m_dictDirPathCtrl) {
            m_dictDirPathCtrl->SetValue(path);
        }
    }
}

void SettingsView::OnOpenDictDir(wxCommandEvent& WXUNUSED(event)) {
    wxString dirPath = m_dictDirPathCtrl ? m_dictDirPathCtrl->GetValue() : wxString::FromUTF8(ConfigManager::GetDefaultDictDir());
    if (dirPath.IsEmpty() || !wxDirExists(dirPath)) {
        dirPath = wxString::FromUTF8(ConfigManager::GetDefaultDictDir());
    }
    if (!wxDirExists(dirPath)) {
        wxFileName::Mkdir(dirPath, 0777, wxPATH_MKDIR_FULL);
    }
    wxLaunchDefaultApplication(dirPath);
}

void SettingsView::OnSaveDictConfig(wxCommandEvent& WXUNUSED(event)) {
    if (!m_configManager)
        return;

    wxString dirPath = m_dictDirPathCtrl ? m_dictDirPathCtrl->GetValue() : "";
    m_configManager->SaveDictDir(dirPath.ToUTF8().data());

    if (m_modelManager && m_modelManager->GetDictEngine()) {
        m_modelManager->GetDictEngine()->LoadDictionaries(dirPath.ToUTF8().data());
    }

    m_renderedDictKeys.clear();
    UpdateDictListSummary();

    if (m_dictStatusText) {
        m_dictStatusText->SetLabel(L"词典目录设置已保存并重新扫描！");
    }
}

void SettingsView::OnReloadDicts(wxCommandEvent& WXUNUSED(event)) {
    wxString dirPath = m_dictDirPathCtrl ? m_dictDirPathCtrl->GetValue() : "";
    if (m_modelManager && m_modelManager->GetDictEngine()) {
        size_t count = m_modelManager->GetDictEngine()->LoadDictionaries(dirPath.ToUTF8().data());
        m_renderedDictKeys.clear();
        UpdateDictListSummary();
        if (m_dictStatusText) {
            m_dictStatusText->SetLabel(wxString::Format(L"扫描完成，已加载 %zu 本词典！", count));
        }
    }
}

void SettingsView::UpdateDictListSummary() {
    if (!m_modelManager || !m_modelManager->GetDictEngine())
        return;

    auto dictEngine = m_modelManager->GetDictEngine();
    auto dicts = dictEngine->GetLoadedDictionaries();
    size_t totalWords = dictEngine->GetTotalWordCount();

    if (m_dictStatusBadge) {
        if (dicts.empty()) {
            m_dictStatusBadge->SetStatus(ServerHealthState::Unconfigured, L"● 未加载词典");
        } else {
            m_dictStatusBadge->SetStatus(ServerHealthState::Ready, wxString::Format(L"● 已就绪: %zu 本词典 (%zu 词)", dicts.size(), totalWords));
        }
    }

    if (!m_dictListContainer || !m_dictListSizer)
        return;

    auto palette = ThemeColors::GetCurrentPalette();
    ThemeMode currentTheme = (palette.windowBg.Red() < 100) ? ThemeMode::Dark : ThemeMode::Light;

    // 脏状态检查：若词典列表与主题外观均无变化，跳过高开销的销毁重建与递归绑定
    bool isDirty = (m_renderedTheme != currentTheme) || (m_renderedDictKeys.size() != dicts.size());
    if (!isDirty) {
        for (size_t i = 0; i < dicts.size(); ++i) {
            if (m_renderedDictKeys[i].bookName != dicts[i].bookName ||
                m_renderedDictKeys[i].ifoPath != dicts[i].ifoPath ||
                m_renderedDictKeys[i].wordCount != dicts[i].wordCount) {
                isDirty = true;
                break;
            }
        }
    }

    if (!isDirty) {
        return;
    }

    m_renderedTheme = currentTheme;
    m_renderedDictKeys.clear();
    m_renderedDictKeys.reserve(dicts.size());
    for (const auto& d : dicts) {
        m_renderedDictKeys.push_back({d.bookName, d.ifoPath, d.wordCount});
    }

    if (m_dictListContainer) {
        m_dictListContainer->SetBackgroundColour(palette.cardBg);
    }
    if (m_dictListTitleText) {
        m_dictListTitleText->SetForegroundColour(palette.textSecondary);
    }
    m_dictListContainer->Freeze();
    m_dictListContainer->DestroyChildren();
    m_dictListSizer->Clear();

    if (dicts.empty()) {
        wxPanel* emptyBox = new wxPanel(m_dictListContainer, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        emptyBox->SetBackgroundColour(palette.cardBg);
        SetupInnerConsoleStyle(emptyBox);

        wxBoxSizer* emptySizer = new wxBoxSizer(wxHORIZONTAL);
        wxBitmapBundle infoBundle = IconManager::GetIconBundle(SVG::INFO, wxSize(18, 18), palette.textSecondary);
        wxStaticBitmap* infoIcon = new wxStaticBitmap(emptyBox, wxID_ANY, infoBundle);

        wxBoxSizer* emptyTextSizer = new wxBoxSizer(wxVERTICAL);
        wxStaticText* emptyTitle = new wxStaticText(emptyBox, wxID_ANY, L"暂无已识别加载的词典");
        emptyTitle->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
        emptyTitle->SetForegroundColour(palette.textSecondary);

        wxStaticText* emptyDesc = new wxStaticText(emptyBox, wxID_ANY, L"请将包含 StarDict 词典（.ifo / .idx / .dict）的文件夹放入词典目录中，并点击上方「重新扫描词典」");
        emptyDesc->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
        emptyDesc->SetForegroundColour(palette.textSecondary);

        emptyTextSizer->Add(emptyTitle, 0, wxBOTTOM, 2_dip);
        emptyTextSizer->Add(emptyDesc, 0);

        emptySizer->Add(infoIcon, 0, wxALIGN_CENTER_VERTICAL | wxALL, 12_dip);
        emptySizer->Add(emptyTextSizer, 1, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM | wxRIGHT, 10_dip);
        emptyBox->SetSizer(emptySizer);

        m_dictListSizer->Add(emptyBox, 0, wxEXPAND | wxBOTTOM, 4_dip);
    } else {
        int cols = 1;
        if (dicts.size() >= 3) {
            cols = 3;
        } else if (dicts.size() == 2) {
            cols = 2;
        }

        wxFlexGridSizer* gridSizer = new wxFlexGridSizer(0, cols, 8_dip, 8_dip);
        for (int c = 0; c < cols; ++c) {
            gridSizer->AddGrowableCol(c, 1);
        }

        bool isDark = (palette.windowBg.Red() < 100);

        for (size_t i = 0; i < dicts.size(); ++i) {
            const auto& d = dicts[i];
            wxPanel* itemCard = new wxPanel(m_dictListContainer, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
            itemCard->SetBackgroundColour(palette.windowBg);
            SetupRoundedPanelStyle(itemCard, 6.0_dip, true);

            wxBoxSizer* cardSizer = new wxBoxSizer(wxVERTICAL);

            // 1. 顶部主要信息行: [图标] [#1  书名] ... 伸缩 ... [词条数徽章]
            wxBoxSizer* topRow = new wxBoxSizer(wxHORIZONTAL);
            wxBitmapBundle dictBundle = IconManager::GetIconBundle(SVG::DICTIONARY, wxSize(14, 14), palette.accentPrimary);
            wxStaticBitmap* dictIcon = new wxStaticBitmap(itemCard, wxID_ANY, dictBundle);
            topRow->Add(dictIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6_dip);

            wxString bookTitle = wxString::FromUTF8(d.bookName);
            if (bookTitle.IsEmpty()) {
                bookTitle = L"未命名词典";
            }
            wxString titleStr = wxString::Format(L"#%zu  %s", i + 1, bookTitle);
            wxStaticText* nameText = new wxStaticText(itemCard, wxID_ANY, titleStr, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
            nameText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
            nameText->SetForegroundColour(palette.textPrimary);
            nameText->SetBackgroundColour(palette.windowBg);
            topRow->Add(nameText, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6_dip);

            // 词条数字格式化（如 435,468 词）
            wxString countNumStr = FormatNumberWithCommas(d.wordCount);
            wxString countLabel = countNumStr + L" 词";

            // 词条数精致胶囊徽章（全卡片唯一保留背景色高亮）
            wxPanel* countPill = new wxPanel(itemCard, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
            countPill->SetBackgroundStyle(wxBG_STYLE_PAINT);
            countPill->Bind(wxEVT_PAINT, [countPill, isDark](wxPaintEvent&) {
                wxAutoBufferedPaintDC dc(countPill);
                wxSize size = countPill->GetClientSize();
                if (size.x <= 0 || size.y <= 0)
                    return;
                auto palette = ThemeColors::GetCurrentPalette();
                dc.SetBackground(wxBrush(palette.windowBg));
                dc.Clear();
                std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
                if (gc) {
                    wxColour pillBg = isDark ? wxColour(20, 48, 85) : wxColour(238, 242, 255);
                    wxColour pillBorder = isDark ? wxColour(35, 70, 125) : wxColour(218, 226, 253);
                    gc->SetBrush(gc->CreateBrush(wxBrush(pillBg)));
                    gc->SetPen(gc->CreatePen(wxPen(pillBorder, 1.0)));
                    gc->DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, 4.0_dip);
                }
            });
            wxColour pillBg = isDark ? wxColour(20, 48, 85) : wxColour(238, 242, 255);
            countPill->SetBackgroundColour(pillBg);
            wxBoxSizer* countPillSizer = new wxBoxSizer(wxHORIZONTAL);
            wxStaticText* countLabelText = new wxStaticText(countPill, wxID_ANY, countLabel);
            countLabelText->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
            countLabelText->SetForegroundColour(palette.accentPrimary);
            countLabelText->SetBackgroundColour(pillBg);
            countPillSizer->Add(countLabelText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5_dip);
            countPill->SetSizer(countPillSizer);

            topRow->Add(countPill, 0, wxALIGN_CENTER_VERTICAL);
            cardSizer->Add(topRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8_dip);

            // 2. 底部信息行: [文件夹图标] [简写路径] ... 伸缩 ... [格式文本] [版本文本] (背景统一透明/卡片底色)
            wxBoxSizer* bottomRow = new wxBoxSizer(wxHORIZONTAL);

            // 路径简写: 取所属文件夹名称 + ifo 文件名 (如 "stardict-langdao-ec-gb/langdao-ec-gb.ifo")
            wxFileName fn(wxString::FromUTF8(d.ifoPath));
            wxString fileName = fn.GetFullName();
            const auto& dirs = fn.GetDirs();
            wxString shortPath;
            if (!dirs.IsEmpty()) {
                shortPath = dirs.Last() + "/" + fileName;
            } else {
                shortPath = fileName;
            }

            wxBitmapBundle folderBundle = IconManager::GetIconBundle(SVG::FOLDER_OPEN, wxSize(12, 12), palette.textSecondary);
            wxStaticBitmap* folderIcon = new wxStaticBitmap(itemCard, wxID_ANY, folderBundle);
            bottomRow->Add(folderIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4_dip);

            wxStaticText* pathText = new wxStaticText(itemCard, wxID_ANY, shortPath, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
            pathText->SetFont(wxFont(7.5, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Consolas, Microsoft YaHei"));
            pathText->SetForegroundColour(palette.textSecondary);
            pathText->SetBackgroundColour(palette.windowBg);
            bottomRow->Add(pathText, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6_dip);

            // 格式文本标签 (无背景色，自然融入卡片)
            wxStaticText* formatLabel = new wxStaticText(itemCard, wxID_ANY, d.isDz ? L"DZ" : L"纯文本");
            formatLabel->SetFont(wxFont(7.5, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
            formatLabel->SetForegroundColour(palette.textSecondary);
            formatLabel->SetBackgroundColour(palette.windowBg);
            bottomRow->Add(formatLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, (d.version.empty() ? 0 : 5_dip));

            // 版本文本标签 (无背景色，自然融入卡片)
            wxStaticText* verLabel = nullptr;
            if (!d.version.empty()) {
                verLabel = new wxStaticText(itemCard, wxID_ANY, "v" + wxString::FromUTF8(d.version));
                verLabel->SetFont(wxFont(7.5, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Consolas, Microsoft YaHei"));
                verLabel->SetForegroundColour(palette.textSecondary);
                verLabel->SetBackgroundColour(palette.windowBg);
                bottomRow->Add(verLabel, 0, wxALIGN_CENTER_VERTICAL);
            }

            cardSizer->Add(bottomRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM | wxTOP, 6_dip);
            itemCard->SetSizer(cardSizer);

            // 悬停提示: 显示完整文件路径及全部元数据
            wxString fullInfoTooltip = wxString::Format(
                L"【%s】\n"
                L"• 词条数量: %s 词条\n"
                L"• 存储格式: %s\n"
                L"%s"
                L"• 描述文件: %s",
                bookTitle,
                countNumStr,
                (d.isDz ? L"DictZip 压缩格式 (.dz)" : L"纯文本未压缩 (.dict)"),
                (d.version.empty() ? L"" : wxString::Format(L"• 词典版本: v%s\n", wxString::FromUTF8(d.version))),
                wxString::FromUTF8(d.ifoPath)
            );
            itemCard->SetToolTip(fullInfoTooltip);
            nameText->SetToolTip(fullInfoTooltip);
            pathText->SetToolTip(fullInfoTooltip);
            folderIcon->SetToolTip(fullInfoTooltip);
            dictIcon->SetToolTip(fullInfoTooltip);
            countPill->SetToolTip(fullInfoTooltip);
            countLabelText->SetToolTip(fullInfoTooltip);
            formatLabel->SetToolTip(fullInfoTooltip);
            if (verLabel) {
                verLabel->SetToolTip(fullInfoTooltip);
            }

            gridSizer->Add(itemCard, 1, wxEXPAND);
        }

        m_dictListSizer->Add(gridSizer, 0, wxEXPAND | wxBOTTOM, 6_dip);
    }

    m_dictListContainer->Layout();
    m_dictListContainer->Thaw();

    BindMouseWheelRecursively(m_dictListContainer);

    if (m_contentPanel) {
        m_contentPanel->Layout();
    }
    UpdateLayoutAndScroll();
}

void SettingsView::SetLogConfig(const AppConfig& cfg) {
    if (m_saveLogToFileCheck) {
        m_saveLogToFileCheck->SetValue(cfg.saveLogToFile);
    }
}

void SettingsView::OnSaveLogConfig(wxCommandEvent& WXUNUSED(event)) {
    if (!m_configManager)
        return;

    bool saveToFile = m_saveLogToFileCheck ? m_saveLogToFileCheck->GetValue() : false;
    m_configManager->SaveLogConfig(saveToFile);

    if (m_logStatusText) {
        m_logStatusText->SetLabel(saveToFile ? L"已开启日志文件记录！" : L"已关闭日志文件记录！");
    }
}

void SettingsView::OnOpenLogDirFromSettings(wxCommandEvent& WXUNUSED(event)) {
    std::string logDir = ConfigManager::GetDefaultLogDir();
    wxString dirPath = wxString::FromUTF8(logDir);
    if (!wxDirExists(dirPath)) {
        wxFileName::Mkdir(dirPath, 0777, wxPATH_MKDIR_FULL);
    }
    wxLaunchDefaultApplication(dirPath);
}

void SettingsView::SetThemeConfig(const AppConfig& cfg) {
    if (!m_themeRadioBox)
        return;
    if (cfg.themeMode == "Dark") {
        m_themeRadioBox->SetSelection(1);
    } else if (cfg.themeMode == "System") {
        m_themeRadioBox->SetSelection(2);
    } else {
        m_themeRadioBox->SetSelection(0);
    }
}

void SettingsView::OnThemeRadioChanged(wxCommandEvent& WXUNUSED(event)) {
    if (!m_themeRadioBox)
        return;
    int sel = m_themeRadioBox->GetSelection();
    std::string themeModeStr = "Light";
    if (sel == 1) {
        themeModeStr = "Dark";
    } else if (sel == 2) {
        themeModeStr = "System";
    }

    if (m_configManager) {
        m_configManager->SaveThemeModeAsync(themeModeStr);
    }

    ThemeManager::GetInstance().SetPreferenceByString(themeModeStr);
}

void SettingsView::OnShowAboutDialog(wxCommandEvent& WXUNUSED(event)) {
    AboutDialog dlg(this);
    dlg.ShowModal();
}

} // namespace LinguaAlpaca::UI
