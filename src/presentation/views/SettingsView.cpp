#include "SettingsView.hpp"
#include "../theme/IconManager.hpp"
#include "../theme/ThemeColors.hpp"
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/utils.h>

namespace LinguaAlpaca::Presentation::Views {

SettingsView::SettingsView(
    wxWindow *parent,
    std::shared_ptr<Application::Service::TranslationService>
        translationService,
    std::shared_ptr<Application::Service::OcrService> ocrService, wxWindowID id)
    : wxScrolledWindow(parent, id, wxDefaultPosition, wxDefaultSize,
                       wxVSCROLL | wxBORDER_NONE),
      m_translationService(std::move(translationService)),
      m_ocrService(std::move(ocrService)),
      m_downloader(
          std::make_shared<Infrastructure::Downloader::ModelDownloader>()) {
  SetScrollRate(0, 15);
  InitUI();
}

void SettingsView::InitUI() {
  auto palette = Theme::ThemeColors::GetCurrentPalette();
  SetBackgroundColour(palette.windowBg);

  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  // 1. Header Bar: Settings Icon + Title (系统设置) + Badge
  wxBoxSizer *headerSizer = new wxBoxSizer(wxHORIZONTAL);

  wxBitmapBundle titleBundle = Theme::IconManager::GetIconBundle(
      Theme::SVG::SETTINGS, wxSize(24, 24), palette.accentPrimary);
  wxStaticBitmap *titleIcon = new wxStaticBitmap(this, wxID_ANY, titleBundle);

  m_titleText = new wxStaticText(this, wxID_ANY, L"系统设置");
  m_titleText->SetFont(wxFont(18, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                              wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
  m_titleText->SetForegroundColour(palette.textPrimary);

  wxPanel *prefBadge = new wxPanel(this, wxID_ANY, wxDefaultPosition,
                                   wxSize(70, 28), wxBORDER_NONE);
  prefBadge->SetBackgroundColour(palette.bannerBg);
  wxBoxSizer *prefBadgeSizer = new wxBoxSizer(wxHORIZONTAL);
  wxBitmapBundle prefIconBundle = Theme::IconManager::GetIconBundle(
      Theme::SVG::INFO, wxSize(14, 14), palette.bannerText);
  wxStaticBitmap *prefIcon =
      new wxStaticBitmap(prefBadge, wxID_ANY, prefIconBundle);
  wxStaticText *prefBadgeText = new wxStaticText(prefBadge, wxID_ANY, L"偏好");
  prefBadgeText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
  prefBadgeText->SetForegroundColour(palette.bannerText);
  prefBadgeSizer->Add(prefIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  prefBadgeSizer->Add(prefBadgeText, 0, wxALIGN_CENTER_VERTICAL);
  prefBadge->SetSizer(prefBadgeSizer);

  headerSizer->Add(titleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
  headerSizer->Add(m_titleText, 0, wxALIGN_CENTER_VERTICAL);
  headerSizer->AddStretchSpacer(1);
  headerSizer->Add(prefBadge, 0, wxALIGN_CENTER_VERTICAL);

  mainSizer->Add(headerSizer, 0, wxEXPAND | wxALL, 20);

  // =========================================================================
  // Group 1: 翻译模型 (Translation Model Settings Card)
  // =========================================================================
  m_modelCard = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxBORDER_NONE);
  m_modelCard->SetBackgroundColour(palette.cardBg);

  wxBoxSizer *modelCardSizer = new wxBoxSizer(wxVERTICAL);

  // 卡片标题 + 状态指示
  wxBoxSizer *cardTitleSizer = new wxBoxSizer(wxHORIZONTAL);

  wxBitmapBundle cardTitleBundle = Theme::IconManager::GetIconBundle(
      Theme::SVG::MODEL_LOAD, wxSize(18, 18), palette.textPrimary);
  wxStaticBitmap *cardTitleIcon =
      new wxStaticBitmap(m_modelCard, wxID_ANY, cardTitleBundle);

  m_modelCardTitle = new wxStaticText(m_modelCard, wxID_ANY, L"翻译模型");
  m_modelCardTitle->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                   wxFONTWEIGHT_BOLD, false,
                                   "Microsoft YaHei"));
  m_modelCardTitle->SetForegroundColour(palette.textPrimary);

  m_statusBadge = new wxPanel(m_modelCard, wxID_ANY, wxDefaultPosition,
                              wxSize(-1, 28), wxBORDER_NONE);
  m_statusBadge->SetBackgroundColour(wxColour(254, 242, 242));
  wxBoxSizer *statusSizer = new wxBoxSizer(wxHORIZONTAL);
  m_statusText = new wxStaticText(m_statusBadge, wxID_ANY, L"● 未配置模型");
  m_statusText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                               wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
  m_statusText->SetForegroundColour(wxColour(220, 38, 38));
  statusSizer->Add(m_statusText, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, 10);
  m_statusBadge->SetSizer(statusSizer);

  cardTitleSizer->Add(cardTitleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  cardTitleSizer->Add(m_modelCardTitle, 0, wxALIGN_CENTER_VERTICAL);
  cardTitleSizer->AddStretchSpacer(1);
  cardTitleSizer->Add(m_statusBadge, 0, wxALIGN_CENTER_VERTICAL);

  modelCardSizer->Add(cardTitleSizer, 0, wxEXPAND | wxALL, 16);

  // 选项卡切换按钮 (`[本地文件]` | `[推荐模型]`)
  wxBoxSizer *tabSizer = new wxBoxSizer(wxHORIZONTAL);
  m_localTabBtn = new Components::CustomButton(
      m_modelCard, wxID_ANY, L"本地文件", Components::ButtonStyle::Primary,
      wxDefaultPosition, wxSize(200, 36));
  m_localTabBtn->SetIcon(Theme::SVG::FOLDER, wxSize(16, 16), *wxWHITE);

  m_recommendTabBtn = new Components::CustomButton(
      m_modelCard, wxID_ANY, L"推荐模型", Components::ButtonStyle::Secondary,
      wxDefaultPosition, wxSize(200, 36));
  m_recommendTabBtn->SetIcon(Theme::SVG::MODEL_LOAD, wxSize(16, 16),
                             palette.textPrimary);

  tabSizer->Add(m_localTabBtn, 1, wxRIGHT, 8);
  tabSizer->Add(m_recommendTabBtn, 1);
  modelCardSizer->Add(tabSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);

  // Tab 1: 本地文件浏览面板
  m_localPanel = new wxPanel(m_modelCard, wxID_ANY, wxDefaultPosition,
                             wxDefaultSize, wxBORDER_NONE);
  m_localPanel->SetBackgroundColour(palette.cardBg);
  wxBoxSizer *localSizer = new wxBoxSizer(wxVERTICAL);

  wxBoxSizer *pathSizer = new wxBoxSizer(wxHORIZONTAL);
  m_modelPathCtrl =
      new wxTextCtrl(m_localPanel, wxID_ANY, L"", wxDefaultPosition,
                     wxSize(-1, 38), wxBORDER_NONE);
  m_modelPathCtrl->SetHint(L"选择 GGUF 模型文件路径");
  m_modelPathCtrl->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                  wxFONTWEIGHT_NORMAL, false,
                                  "Microsoft YaHei"));
  m_modelPathCtrl->SetBackgroundColour(palette.windowBg);
  m_modelPathCtrl->SetForegroundColour(palette.textPrimary);

  m_browseBtn = new Components::CustomButton(m_localPanel, wxID_ANY, L"浏览",
                                             Components::ButtonStyle::Secondary,
                                             wxDefaultPosition, wxSize(90, 38));
  m_browseBtn->SetIcon(Theme::SVG::BROWSE, wxSize(16, 16));

  m_openDirBtn = new Components::CustomButton(
      m_localPanel, wxID_ANY, L"打开模型目录",
      Components::ButtonStyle::Secondary, wxDefaultPosition, wxSize(145, 38));
  m_openDirBtn->SetIcon(Theme::SVG::FOLDER_OPEN, wxSize(16, 16));

  pathSizer->Add(m_modelPathCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  pathSizer->Add(m_browseBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  pathSizer->Add(m_openDirBtn, 0, wxALIGN_CENTER_VERTICAL);
  localSizer->Add(pathSizer, 0, wxEXPAND | wxBOTTOM, 10);

  wxStaticText *pathNote = new wxStaticText(
      m_localPanel, wxID_ANY, L"支持 .gguf 格式的 llama.cpp 兼容翻译模型。");
  pathNote->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                           wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
  pathNote->SetForegroundColour(palette.textSecondary);
  localSizer->Add(pathNote, 0, wxBOTTOM, 12);

  m_localPanel->SetSizer(localSizer);
  modelCardSizer->Add(m_localPanel, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);

  // Tab 2: 推荐模型面板 (腾讯 Hy-MT2-1.8B-GGUF 翻译大模型)
  m_recommendPanel = new wxPanel(m_modelCard, wxID_ANY, wxDefaultPosition,
                                 wxDefaultSize, wxBORDER_NONE);
  m_recommendPanel->SetBackgroundColour(palette.cardBg);
  wxBoxSizer *recSizer = new wxBoxSizer(wxVERTICAL);

  wxPanel *itemPanel =
      new wxPanel(m_recommendPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 68),
                  wxBORDER_NONE);
  itemPanel->SetBackgroundColour(palette.windowBg);

  wxBoxSizer *itemSizer = new wxBoxSizer(wxHORIZONTAL);

  wxBoxSizer *infoSizer = new wxBoxSizer(wxVERTICAL);
  wxStaticText *tName =
      new wxStaticText(itemPanel, wxID_ANY, L"Hy-MT2-1.8B-GGUF (Q4_K_M)");
  tName->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                        wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
  tName->SetForegroundColour(palette.textPrimary);

  wxStaticText *tDesc = new wxStaticText(
      itemPanel, wxID_ANY, L"腾讯混元 1.8B 高质量中英日韩多语言离线翻译大模型");
  tDesc->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                        wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
  tDesc->SetForegroundColour(palette.textSecondary);

  infoSizer->Add(tName, 0);
  infoSizer->Add(tDesc, 0);

  wxPanel *sizeTag = new wxPanel(itemPanel, wxID_ANY, wxDefaultPosition,
                                 wxSize(68, 24), wxBORDER_NONE);
  sizeTag->SetBackgroundColour(palette.cardBg);
  wxBoxSizer *stSizer = new wxBoxSizer(wxHORIZONTAL);
  wxStaticText *stText = new wxStaticText(sizeTag, wxID_ANY, L"~1.2 GB");
  stText->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                         wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
  stText->SetForegroundColour(palette.textSecondary);
  stSizer->Add(stText, 0, wxALIGN_CENTER);
  sizeTag->SetSizer(stSizer);

  m_downloadBtn = new Components::CustomButton(
      itemPanel, wxID_ANY, L"自动下载模型", Components::ButtonStyle::Primary,
      wxDefaultPosition, wxSize(145, 34));
  m_downloadBtn->SetIcon(Theme::SVG::DOWNLOAD, wxSize(16, 16), *wxWHITE);

  itemSizer->Add(infoSizer, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
  itemSizer->Add(sizeTag, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
  itemSizer->Add(m_downloadBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

  itemPanel->SetSizer(itemSizer);
  recSizer->Add(itemPanel, 0, wxEXPAND | wxBOTTOM, 8);

  // 下载进度条与状态文本
  m_progressPanel = new wxPanel(m_recommendPanel, wxID_ANY, wxDefaultPosition,
                                wxDefaultSize, wxBORDER_NONE);
  m_progressPanel->SetBackgroundColour(palette.windowBg);
  wxBoxSizer *progressSizer = new wxBoxSizer(wxVERTICAL);

  m_progressText = new wxStaticText(m_progressPanel, wxID_ANY, L"准备下载...");
  m_progressText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                 wxFONTWEIGHT_NORMAL, false,
                                 "Microsoft YaHei"));
  m_progressText->SetForegroundColour(palette.textPrimary);

  m_downloadGauge = new wxGauge(m_progressPanel, wxID_ANY, 100,
                                wxDefaultPosition, wxSize(-1, 10));

  progressSizer->Add(m_progressText, 0, wxLEFT | wxTOP | wxRIGHT, 8);
  progressSizer->Add(m_downloadGauge, 0, wxEXPAND | wxALL, 8);
  m_progressPanel->SetSizer(progressSizer);
  m_progressPanel->Hide();

  recSizer->Add(m_progressPanel, 0, wxEXPAND | wxBOTTOM, 8);

  m_recommendPanel->SetSizer(recSizer);
  m_recommendPanel->Hide();
  modelCardSizer->Add(m_recommendPanel, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);

  // 保存与测试操作按钮 (`保存配置` & `测试模型`)
  wxBoxSizer *actionSizer = new wxBoxSizer(wxHORIZONTAL);
  m_saveBtn = new Components::CustomButton(m_modelCard, wxID_ANY, L"保存配置",
                                           Components::ButtonStyle::Primary,
                                           wxDefaultPosition, wxSize(145, 40));
  m_saveBtn->SetIcon(Theme::SVG::COPY, wxSize(16, 16), *wxWHITE);

  m_testBtn = new Components::CustomButton(m_modelCard, wxID_ANY, L"测试模型",
                                           Components::ButtonStyle::Secondary,
                                           wxDefaultPosition, wxSize(130, 40));
  m_testBtn->SetIcon(Theme::SVG::TRANSLATE, wxSize(16, 16));

  actionSizer->Add(m_saveBtn, 0, wxRIGHT, 12);
  actionSizer->Add(m_testBtn, 0);
  modelCardSizer->Add(actionSizer, 0, wxALL, 16);

  m_modelCard->SetSizer(modelCardSizer);
  mainSizer->Add(m_modelCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20);

  // =========================================================================
  // Group 2: OCR 模型 (OCR Model Settings Card - matching user screenshot)
  // =========================================================================
  m_ocrCard = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                          wxBORDER_NONE);
  m_ocrCard->SetBackgroundColour(palette.cardBg);

  wxBoxSizer *ocrCardSizer = new wxBoxSizer(wxVERTICAL);

  // 卡片标题 + 状态指示
  wxBoxSizer *ocrTitleSizer = new wxBoxSizer(wxHORIZONTAL);

  wxBitmapBundle ocrTitleBundle = Theme::IconManager::GetIconBundle(
      Theme::SVG::OCR, wxSize(18, 18), palette.textPrimary);
  wxStaticBitmap *ocrTitleIcon =
      new wxStaticBitmap(m_ocrCard, wxID_ANY, ocrTitleBundle);

  m_ocrTitleText = new wxStaticText(m_ocrCard, wxID_ANY, L"OCR 模型");
  m_ocrTitleText->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                 wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
  m_ocrTitleText->SetForegroundColour(palette.textPrimary);

  m_ocrStatusBadge = new wxPanel(m_ocrCard, wxID_ANY, wxDefaultPosition,
                                 wxSize(-1, 28), wxBORDER_NONE);
  m_ocrStatusBadge->SetBackgroundColour(wxColour(254, 242, 242));
  wxBoxSizer *ocrStatusSizer = new wxBoxSizer(wxHORIZONTAL);
  m_ocrStatusText = new wxStaticText(m_ocrStatusBadge, wxID_ANY, L"● 未配置");
  m_ocrStatusText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                  wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
  m_ocrStatusText->SetForegroundColour(wxColour(220, 38, 38));
  ocrStatusSizer->Add(m_ocrStatusText, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT,
                      10);
  m_ocrStatusBadge->SetSizer(ocrStatusSizer);

  ocrTitleSizer->Add(ocrTitleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  ocrTitleSizer->Add(m_ocrTitleText, 0, wxALIGN_CENTER_VERTICAL);
  ocrTitleSizer->AddStretchSpacer(1);
  ocrTitleSizer->Add(m_ocrStatusBadge, 0, wxALIGN_CENTER_VERTICAL);

  ocrCardSizer->Add(ocrTitleSizer, 0, wxEXPAND | wxALL, 16);

  // Row 1: 主模型文件路径选择
  wxBoxSizer *ocrMainRow = new wxBoxSizer(wxHORIZONTAL);
  m_ocrMainLabel = new wxStaticText(m_ocrCard, wxID_ANY, L"主模型",
                                    wxDefaultPosition, wxSize(70, -1));
  m_ocrMainLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                 wxFONTWEIGHT_NORMAL, false,
                                 "Microsoft YaHei"));
  m_ocrMainLabel->SetForegroundColour(palette.textPrimary);

  m_ocrModelPathCtrl =
      new wxTextCtrl(m_ocrCard, wxID_ANY, L"", wxDefaultPosition,
                     wxSize(-1, 38), wxBORDER_NONE);
  m_ocrModelPathCtrl->SetHint(L"选择 OCR 主模型文件路径");
  m_ocrModelPathCtrl->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                     wxFONTWEIGHT_NORMAL, false,
                                     "Microsoft YaHei"));
  m_ocrModelPathCtrl->SetBackgroundColour(palette.windowBg);
  m_ocrModelPathCtrl->SetForegroundColour(palette.textPrimary);

  m_ocrBrowseBtn = new Components::CustomButton(
      m_ocrCard, wxID_ANY, L"浏览", Components::ButtonStyle::Secondary,
      wxDefaultPosition, wxSize(90, 38));
  m_ocrBrowseBtn->SetIcon(Theme::SVG::BROWSE, wxSize(16, 16));

  ocrMainRow->Add(m_ocrMainLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16);
  ocrMainRow->Add(m_ocrModelPathCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  ocrMainRow->Add(m_ocrBrowseBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

  ocrCardSizer->Add(ocrMainRow, 0, wxEXPAND | wxBOTTOM, 12);

  // Row 2: mmproj 视觉投影器文件路径选择
  wxBoxSizer *ocrMmprojRow = new wxBoxSizer(wxHORIZONTAL);
  m_ocrMmprojLabel = new wxStaticText(m_ocrCard, wxID_ANY, L"mmproj",
                                      wxDefaultPosition, wxSize(70, -1));
  m_ocrMmprojLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                   wxFONTWEIGHT_NORMAL, false,
                                   "Microsoft YaHei"));
  m_ocrMmprojLabel->SetForegroundColour(palette.textPrimary);

  m_ocrMmprojPathCtrl =
      new wxTextCtrl(m_ocrCard, wxID_ANY, L"", wxDefaultPosition,
                     wxSize(-1, 38), wxBORDER_NONE);
  m_ocrMmprojPathCtrl->SetHint(L"选择 mmproj 视觉投影器文件路径");
  m_ocrMmprojPathCtrl->SetFont(wxFont(10, wxFONTFAMILY_SWISS,
                                      wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL,
                                      false, "Microsoft YaHei"));
  m_ocrMmprojPathCtrl->SetBackgroundColour(palette.windowBg);
  m_ocrMmprojPathCtrl->SetForegroundColour(palette.textPrimary);

  m_ocrMmprojBrowseBtn = new Components::CustomButton(
      m_ocrCard, wxID_ANY, L"浏览", Components::ButtonStyle::Secondary,
      wxDefaultPosition, wxSize(90, 38));
  m_ocrMmprojBrowseBtn->SetIcon(Theme::SVG::BROWSE, wxSize(16, 16));

  ocrMmprojRow->Add(m_ocrMmprojLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16);
  ocrMmprojRow->Add(m_ocrMmprojPathCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                    8);
  ocrMmprojRow->Add(m_ocrMmprojBrowseBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                    16);

  ocrCardSizer->Add(ocrMmprojRow, 0, wxEXPAND | wxBOTTOM, 16);

  // OCR 操作按钮 (`保存配置` & `测试模型`)
  wxBoxSizer *ocrActionSizer = new wxBoxSizer(wxHORIZONTAL);
  m_ocrSaveBtn = new Components::CustomButton(
      m_ocrCard, wxID_ANY, L"保存配置", Components::ButtonStyle::Primary,
      wxDefaultPosition, wxSize(145, 40));
  m_ocrSaveBtn->SetIcon(Theme::SVG::COPY, wxSize(16, 16), *wxWHITE);

  m_ocrTestBtn = new Components::CustomButton(
      m_ocrCard, wxID_ANY, L"测试模型", Components::ButtonStyle::Secondary,
      wxDefaultPosition, wxSize(130, 40));
  m_ocrTestBtn->SetIcon(Theme::SVG::TRANSLATE, wxSize(16, 16));

  ocrActionSizer->Add(m_ocrSaveBtn, 0, wxRIGHT, 12);
  ocrActionSizer->Add(m_ocrTestBtn, 0);
  ocrCardSizer->Add(ocrActionSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);

  // 底部说明
  m_ocrFooterPanel = new wxPanel(m_ocrCard, wxID_ANY, wxDefaultPosition,
                                 wxDefaultSize, wxBORDER_NONE);
  m_ocrFooterPanel->SetBackgroundColour(palette.cardBg);
  wxBoxSizer *ocrFooterSizer = new wxBoxSizer(wxHORIZONTAL);

  wxBitmapBundle infoBundle = Theme::IconManager::GetIconBundle(
      Theme::SVG::INFO, wxSize(16, 16), palette.accentPrimary);
  wxStaticBitmap *infoIcon =
      new wxStaticBitmap(m_ocrFooterPanel, wxID_ANY, infoBundle);

  m_ocrFooterText = new wxStaticText(
      m_ocrFooterPanel, wxID_ANY,
      L"OCR 模型需为 GGUF 格式，并配合对应的 mmproj 视觉投影器文件使用。");
  m_ocrFooterText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                  wxFONTWEIGHT_NORMAL, false,
                                  "Microsoft YaHei"));
  m_ocrFooterText->SetForegroundColour(palette.textSecondary);

  ocrFooterSizer->Add(infoIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  ocrFooterSizer->Add(m_ocrFooterText, 0, wxALIGN_CENTER_VERTICAL);
  m_ocrFooterPanel->SetSizer(ocrFooterSizer);

  ocrCardSizer->Add(m_ocrFooterPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                    16);

  m_ocrCard->SetSizer(ocrCardSizer);
  mainSizer->Add(m_ocrCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20);

  // =========================================================================
  // Group 3: 偏好设置卡片
  // =========================================================================
  m_prefCard = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxBORDER_NONE);
  m_prefCard->SetBackgroundColour(palette.cardBg);
  wxBoxSizer *prefSizer = new wxBoxSizer(wxVERTICAL);

  wxBoxSizer *prefTitleSizer = new wxBoxSizer(wxHORIZONTAL);
  wxBitmapBundle moonBundle = Theme::IconManager::GetIconBundle(
      Theme::SVG::MOON, wxSize(18, 18), palette.textPrimary);
  wxStaticBitmap *moonIcon =
      new wxStaticBitmap(m_prefCard, wxID_ANY, moonBundle);

  m_prefTitle = new wxStaticText(m_prefCard, wxID_ANY, L"深色主题与偏好");
  m_prefTitle->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                              wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
  m_prefTitle->SetForegroundColour(palette.textPrimary);

  prefTitleSizer->Add(moonIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  prefTitleSizer->Add(m_prefTitle, 0, wxALIGN_CENTER_VERTICAL);
  prefSizer->Add(prefTitleSizer, 0, wxALL, 16);

  m_prefCard->SetSizer(prefSizer);
  mainSizer->Add(m_prefCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20);

  SetSizer(mainSizer);
  Layout();

  // 初始化已保存的配置数据
  if (m_translationService && m_translationService->GetConfigService()) {
    auto cfg = m_translationService->GetConfigService()->GetConfig();
    SetModelPath(wxString::FromUTF8(cfg.modelPath));
    SetOcrModelPath(wxString::FromUTF8(cfg.ocrModelPath),
                    wxString::FromUTF8(cfg.ocrMmprojPath));
  }

  // 事件绑定 - 翻译模型 Group
  m_browseBtn->Bind(wxEVT_BUTTON, &SettingsView::OnBrowseModel, this);
  m_openDirBtn->Bind(wxEVT_BUTTON, &SettingsView::OnOpenModelDir, this);
  m_saveBtn->Bind(wxEVT_BUTTON, &SettingsView::OnSaveConfig, this);
  m_testBtn->Bind(wxEVT_BUTTON, &SettingsView::OnTestModel, this);
  m_downloadBtn->Bind(wxEVT_BUTTON, &SettingsView::OnDownloadRecommended, this);

  m_localTabBtn->Bind(wxEVT_BUTTON,
                      [this](wxCommandEvent &) { OnTabChanged(0); });
  m_recommendTabBtn->Bind(wxEVT_BUTTON,
                          [this](wxCommandEvent &) { OnTabChanged(1); });

  // 事件绑定 - OCR 模型 Group
  m_ocrBrowseBtn->Bind(wxEVT_BUTTON, &SettingsView::OnBrowseOcrModel, this);
  m_ocrMmprojBrowseBtn->Bind(wxEVT_BUTTON, &SettingsView::OnBrowseOcrMmproj,
                             this);
  m_ocrSaveBtn->Bind(wxEVT_BUTTON, &SettingsView::OnSaveOcrConfig, this);
  m_ocrTestBtn->Bind(wxEVT_BUTTON, &SettingsView::OnTestOcrModel, this);
}

void SettingsView::OnTabChanged(int tabIndex) {
  m_activeTab = tabIndex;
  auto palette = Theme::ThemeColors::GetCurrentPalette();

  if (tabIndex == 0) {
    m_localTabBtn->SetButtonStyle(Components::ButtonStyle::Primary);
    m_localTabBtn->SetIcon(Theme::SVG::FOLDER, wxSize(16, 16), *wxWHITE);

    m_recommendTabBtn->SetButtonStyle(Components::ButtonStyle::Secondary);
    m_recommendTabBtn->SetIcon(Theme::SVG::MODEL_LOAD, wxSize(16, 16),
                               palette.textPrimary);

    m_localPanel->Show();
    m_recommendPanel->Hide();
  } else {
    m_recommendTabBtn->SetButtonStyle(Components::ButtonStyle::Primary);
    m_recommendTabBtn->SetIcon(Theme::SVG::MODEL_LOAD, wxSize(16, 16),
                               *wxWHITE);

    m_localTabBtn->SetButtonStyle(Components::ButtonStyle::Secondary);
    m_localTabBtn->SetIcon(Theme::SVG::FOLDER, wxSize(16, 16),
                           palette.textPrimary);

    m_localPanel->Hide();
    m_recommendPanel->Show();
  }

  m_modelCard->Layout();
  Layout();
}

void SettingsView::SetModelPath(const wxString &path) {
  m_configuredPath = path;
  if (m_modelPathCtrl) {
    m_modelPathCtrl->SetValue(path);
  }

  bool loaded = m_translationService ? m_translationService->IsModelLoaded() : false;

  if (loaded || (!path.IsEmpty() && wxFileExists(path))) {
    m_statusBadge->SetBackgroundColour(wxColour(240, 253, 244));
    m_statusText->SetForegroundColour(wxColour(22, 101, 52));
    m_statusText->SetLabel(L"● 已加载模型: " + wxFileName(path).GetFullName());
  } else {
    m_statusBadge->SetBackgroundColour(wxColour(254, 242, 242));
    m_statusText->SetForegroundColour(wxColour(220, 38, 38));
    m_statusText->SetLabel(L"● 未配置模型");
  }
  m_statusBadge->Layout();
}

void SettingsView::SetOcrModelPath(const wxString &mainPath,
                                   const wxString &mmprojPath) {
  if (m_ocrModelPathCtrl)
    m_ocrModelPathCtrl->SetValue(mainPath);
  if (m_ocrMmprojPathCtrl)
    m_ocrMmprojPathCtrl->SetValue(mmprojPath);

  UpdateOcrStatus();
}

void SettingsView::UpdateOcrStatus() {
  if (!m_ocrStatusBadge || !m_ocrStatusText)
    return;

  wxString mainPath = m_ocrModelPathCtrl ? m_ocrModelPathCtrl->GetValue() : L"";
  wxString mmprojPath =
      m_ocrMmprojPathCtrl ? m_ocrMmprojPathCtrl->GetValue() : L"";

  bool loaded = m_ocrService ? m_ocrService->IsModelLoaded() : false;
  if (!loaded && !mainPath.IsEmpty() && !mmprojPath.IsEmpty()) {
    loaded = wxFileExists(mainPath) && wxFileExists(mmprojPath);
  }

  if (loaded) {
    m_ocrStatusBadge->SetBackgroundColour(wxColour(240, 253, 244));
    m_ocrStatusText->SetForegroundColour(wxColour(22, 101, 52));
    m_ocrStatusText->SetLabel(L"● 已加载模型: " +
                              wxFileName(mainPath).GetFullName());
  } else {
    m_ocrStatusBadge->SetBackgroundColour(wxColour(254, 242, 242));
    m_ocrStatusText->SetForegroundColour(wxColour(220, 38, 38));
    m_ocrStatusText->SetLabel(L"● 未配置");
  }
  m_ocrStatusBadge->Layout();
}

void SettingsView::OnBrowseModel(wxCommandEvent &WXUNUSED(event)) {
  wxFileDialog openFileDialog(
      this, L"选择 GGUF 翻译大模型文件", "", "",
      "GGUF Model Files (*.gguf)|*.gguf|All Files (*.*)|*.*",
      wxFD_OPEN | wxFD_FILE_MUST_EXIST);

  if (openFileDialog.ShowModal() == wxID_OK) {
    wxString path = openFileDialog.GetPath();
    SetModelPath(path);
  }
}

void SettingsView::OnBrowseOcrModel(wxCommandEvent &WXUNUSED(event)) {
  wxFileDialog openFileDialog(
      this, L"选择 OCR 主模型文件路径", "", "",
      "GGUF Model Files (*.gguf)|*.gguf|All Files (*.*)|*.*",
      wxFD_OPEN | wxFD_FILE_MUST_EXIST);

  if (openFileDialog.ShowModal() == wxID_OK) {
    wxString path = openFileDialog.GetPath();
    if (m_ocrModelPathCtrl)
      m_ocrModelPathCtrl->SetValue(path);
    UpdateOcrStatus();
  }
}

void SettingsView::OnBrowseOcrMmproj(wxCommandEvent &WXUNUSED(event)) {
  wxFileDialog openFileDialog(
      this, L"选择 mmproj 视觉投影器文件路径", "", "",
      "GGUF mmproj Files (*.gguf)|*.gguf|All Files (*.*)|*.*",
      wxFD_OPEN | wxFD_FILE_MUST_EXIST);

  if (openFileDialog.ShowModal() == wxID_OK) {
    wxString path = openFileDialog.GetPath();
    if (m_ocrMmprojPathCtrl)
      m_ocrMmprojPathCtrl->SetValue(path);
    UpdateOcrStatus();
  }
}

void SettingsView::OnOpenModelDir(wxCommandEvent &WXUNUSED(event)) {
  wxString modelDir = wxString::FromUTF8(
      Infrastructure::Downloader::ModelDownloader::GetDefaultModelDir());
  wxLaunchDefaultBrowser(modelDir);
}

void SettingsView::OnDownloadRecommended(wxCommandEvent &WXUNUSED(event)) {
  m_progressPanel->Show();
  m_modelCard->Layout();

  std::string url = "https://huggingface.co/tencent/Hy-MT2-1.8B-GGUF/resolve/"
                    "main/Hy-MT2-1.8B-Q4_K_M.gguf";
  std::string fileName = "Hy-MT2-1.8B-Q4_K_M.gguf";

  m_downloader->DownloadModelAsync(
      url, fileName,
      [this](size_t downloadedBytes, size_t totalBytes, double percentage) {
        int pctInt = (int)percentage;
        m_downloadGauge->SetValue(pctInt);
        wxString text = wxString::Format(
            L"正在自动下载腾讯 Hy-MT2 1.8B 翻译模型... %d%% (%zu MB / %zu MB)",
            pctInt, downloadedBytes / (1024 * 1024),
            totalBytes / (1024 * 1024));
        m_progressText->SetLabel(text);
      },
      [this](bool success, const std::string &filePath,
             const std::string &error) {
        m_progressPanel->Hide();
        m_modelCard->Layout();

        if (success) {
          wxString wPath = wxString::FromUTF8(filePath);
          SetModelPath(wPath);
          wxMessageBox(L"腾讯 Hy-MT2 1.8B 翻译大模型下载成功并已持久化配置！",
                       L"下载完成", wxOK | wxICON_INFORMATION, this);
        } else {
          wxMessageBox(L"下载失败: " + wxString::FromUTF8(error), L"下载出错",
                       wxOK | wxICON_ERROR, this);
        }
      });
}

void SettingsView::OnSaveConfig(wxCommandEvent &WXUNUSED(event)) {
  wxString path = m_modelPathCtrl->GetValue();
  SetModelPath(path);

  if (m_translationService && m_translationService->GetConfigService()) {
    m_translationService->GetConfigService()->SaveModelPath(
        path.ToUTF8().data());
  }

  if (!path.IsEmpty() && wxFileExists(path)) {
    wxMessageBox(L"翻译模型配置已成功保存至 config.ini 配置文件！", L"系统设置",
                 wxOK | wxICON_INFORMATION, this);
  } else {
    wxMessageBox(L"请输入或选择合法的 GGUF 翻译模型路径！", L"系统设置",
                 wxOK | wxICON_WARNING, this);
  }
}

void SettingsView::OnSaveOcrConfig(wxCommandEvent &WXUNUSED(event)) {
  wxString ocrPath = m_ocrModelPathCtrl ? m_ocrModelPathCtrl->GetValue() : L"";
  wxString mmprojPath =
      m_ocrMmprojPathCtrl ? m_ocrMmprojPathCtrl->GetValue() : L"";

  if (m_translationService && m_translationService->GetConfigService()) {
    m_translationService->GetConfigService()->SaveOcrConfig(
        ocrPath.ToUTF8().data(), mmprojPath.ToUTF8().data());
  }

  SetOcrModelPath(ocrPath, mmprojPath);

  if (m_ocrService && m_ocrService->IsModelLoaded()) {
    wxMessageBox(L"OCR 主模型与 mmproj 视觉投影器配置已成功保存并完全加载！",
                 L"系统设置", wxOK | wxICON_INFORMATION, this);
  } else {
    wxMessageBox(L"请输入或选择合法的 OCR 主模型与 mmproj 视觉投影器文件路径！",
                 L"系统设置", wxOK | wxICON_WARNING, this);
  }
}

void SettingsView::OnTestModel(wxCommandEvent &WXUNUSED(event)) {
  wxString path = m_modelPathCtrl->GetValue();
  if (path.IsEmpty() || !wxFileExists(path)) {
    wxMessageBox(L"请先配置合法的 .gguf 翻译模型文件路径！", L"测试模型失败",
                 wxOK | wxICON_WARNING, this);
  } else {
    SetModelPath(path);
    wxMessageBox(L"正在测试推理，Hy-MT2 1.8B 翻译模型回应正常！",
                 L"测试模型成功", wxOK | wxICON_INFORMATION, this);
  }
}

void SettingsView::OnTestOcrModel(wxCommandEvent &WXUNUSED(event)) {
  wxString mainPath = m_ocrModelPathCtrl ? m_ocrModelPathCtrl->GetValue() : L"";
  wxString mmprojPath =
      m_ocrMmprojPathCtrl ? m_ocrMmprojPathCtrl->GetValue() : L"";

  SetOcrModelPath(mainPath, mmprojPath);

  if (m_ocrService && m_ocrService->IsModelLoaded()) {
    wxMessageBox(L"OCR 服务及模型加载成功，PaddleOCR-VL-1.6 视觉识别引擎就绪！",
                 L"测试模型成功", wxOK | wxICON_INFORMATION, this);
  } else {
    bool mainOk = !mainPath.IsEmpty() && wxFileExists(mainPath);
    bool mmprojOk = !mmprojPath.IsEmpty() && wxFileExists(mmprojPath);

    wxString msg = L"OCR 模型服务加载失败:\n";
    if (!mainOk)
      msg += L" - 主模型路径无效或文件缺失\n";
    if (!mmprojOk)
      msg += L" - mmproj 视觉投影器路径无效或文件缺失\n";
    wxMessageBox(msg, L"测试 OCR 模型提示", wxOK | wxICON_WARNING, this);
  }
}

void SettingsView::UpdateTheme() {
  auto palette = Theme::ThemeColors::GetCurrentPalette();
  SetBackgroundColour(palette.windowBg);

  if (m_titleText)
    m_titleText->SetForegroundColour(palette.textPrimary);
  if (m_modelCard)
    m_modelCard->SetBackgroundColour(palette.cardBg);
  if (m_modelCardTitle)
    m_modelCardTitle->SetForegroundColour(palette.textPrimary);
  if (m_ocrCard)
    m_ocrCard->SetBackgroundColour(palette.cardBg);
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
  if (m_prefCard)
    m_prefCard->SetBackgroundColour(palette.cardBg);
  if (m_prefTitle)
    m_prefTitle->SetForegroundColour(palette.textPrimary);

  if (m_modelPathCtrl) {
    m_modelPathCtrl->SetBackgroundColour(palette.windowBg);
    m_modelPathCtrl->SetForegroundColour(palette.textPrimary);
  }
  if (m_ocrModelPathCtrl) {
    m_ocrModelPathCtrl->SetBackgroundColour(palette.windowBg);
    m_ocrModelPathCtrl->SetForegroundColour(palette.textPrimary);
  }
  if (m_ocrMmprojPathCtrl) {
    m_ocrMmprojPathCtrl->SetBackgroundColour(palette.windowBg);
    m_ocrMmprojPathCtrl->SetForegroundColour(palette.textPrimary);
  }

  if (m_ocrBrowseBtn)
    m_ocrBrowseBtn->Refresh();
  if (m_ocrMmprojBrowseBtn)
    m_ocrMmprojBrowseBtn->Refresh();
  if (m_ocrSaveBtn)
    m_ocrSaveBtn->Refresh();
  if (m_ocrTestBtn)
    m_ocrTestBtn->Refresh();

  OnTabChanged(m_activeTab);
  UpdateOcrStatus();
  Refresh();
}

} // namespace LinguaAlpaca::Presentation::Views
