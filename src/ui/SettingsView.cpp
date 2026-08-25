#include "SettingsView.hpp"
#include "core/Downloader.hpp"
#include "theme/IconManager.hpp"
#include "theme/Theme.hpp"
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/utils.h>

namespace LinguaAlpaca::UI {

	SettingsView::SettingsView(wxWindow* parent, std::shared_ptr<ModelManager> modelManager, wxWindowID id)
		: wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
		  m_modelManager(std::move(modelManager)) {
		if (m_modelManager) {
			m_configManager = m_modelManager->GetConfigManager();
		}
		InitUI();
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
		m_scrollBar = new ScrollBar(this, [this](int pixelY) {
			ScrollTo(pixelY);
		});

		wxBoxSizer* rootSizer = new wxBoxSizer(wxHORIZONTAL);
		rootSizer->Add(m_viewport, 1, wxEXPAND);
		rootSizer->Add(m_scrollBar, 0, wxEXPAND | wxTOP | wxBOTTOM, 4_dip);
		SetSizer(rootSizer);

		m_mainSizer = new wxBoxSizer(wxVERTICAL);

		// 1. Header Bar: Settings Icon + Title (系统设置)
		wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

		wxBitmapBundle titleBundle = IconManager::GetIconBundle(SVG::SETTINGS, dip(24, 24), palette.accentPrimary);
		wxStaticBitmap* titleIcon = new wxStaticBitmap(m_contentPanel, wxID_ANY, titleBundle);

		m_titleText = new wxStaticText(m_contentPanel, wxID_ANY, L"系统设置");
		m_titleText->SetFont(wxFont(18, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
		m_titleText->SetForegroundColour(palette.textPrimary);

		headerSizer->Add(titleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);
		headerSizer->Add(m_titleText, 0, wxALIGN_CENTER_VERTICAL);

		m_mainSizer->Add(headerSizer, 0, wxEXPAND | wxALL, 20_dip);

		// Group 1: 翻译模型 (Translation Model Settings Card)
		m_modelCard = new wxPanel(m_contentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
		m_modelCard->SetBackgroundColour(palette.cardBg);

		wxBoxSizer* modelCardSizer = new wxBoxSizer(wxVERTICAL);

		// 卡片标题 + 状态指示
		wxBoxSizer* cardTitleSizer = new wxBoxSizer(wxHORIZONTAL);

		wxBitmapBundle cardTitleBundle = IconManager::GetIconBundle(SVG::MODEL_LOAD, dip(18, 18), palette.textPrimary);
		wxStaticBitmap* cardTitleIcon = new wxStaticBitmap(m_modelCard, wxID_ANY, cardTitleBundle);

		m_modelCardTitle = new wxStaticText(m_modelCard, wxID_ANY, L"翻译模型");
		m_modelCardTitle->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
		m_modelCardTitle->SetForegroundColour(palette.textPrimary);

		m_statusBadge = new StatusBadge(m_modelCard);

		cardTitleSizer->Add(cardTitleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
		cardTitleSizer->Add(m_modelCardTitle, 0, wxALIGN_CENTER_VERTICAL);
		cardTitleSizer->AddStretchSpacer(1);
		cardTitleSizer->Add(m_statusBadge, 0, wxALIGN_CENTER_VERTICAL);

		modelCardSizer->Add(cardTitleSizer, 0, wxEXPAND | wxALL, 16_dip);

		// 模型文件路径选择行
		wxBoxSizer* pathSizer = new wxBoxSizer(wxHORIZONTAL);
		m_modelPathCtrl = new CustomInputBox(m_modelCard, wxID_ANY, L"", L"选择 GGUF 模型文件路径", wxDefaultPosition, wxSize(-1, 38_dip));
		m_modelPathCtrl->SetPrefixIcon(SVG::MODEL_LOAD, dip(16, 16));

		m_browseBtn = new CustomButton(m_modelCard, wxID_ANY, L"浏览", ButtonStyle::Secondary, wxDefaultPosition, dip(90, 38));
		m_browseBtn->SetIcon(SVG::BROWSE, dip(16, 16));

		m_openDirBtn = new CustomButton(m_modelCard, wxID_ANY, L"打开模型目录", ButtonStyle::Secondary, wxDefaultPosition, dip(145, 38));
		m_openDirBtn->SetIcon(SVG::FOLDER_OPEN, dip(16, 16));

		pathSizer->Add(m_modelPathCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
		pathSizer->Add(m_browseBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
		pathSizer->Add(m_openDirBtn, 0, wxALIGN_CENTER_VERTICAL);
		modelCardSizer->Add(pathSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12_dip);

		wxStaticText* pathNote = new wxStaticText(m_modelCard, wxID_ANY, L"支持 .gguf 格式的 llama.cpp 兼容翻译模型。");
		pathNote->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
		pathNote->SetForegroundColour(palette.textSecondary);
		modelCardSizer->Add(pathNote, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

		// 保存与测试操作按钮 (`保存配置` & `测试模型`)
		wxBoxSizer* actionSizer = new wxBoxSizer(wxHORIZONTAL);
		m_saveBtn = new CustomButton(m_modelCard, wxID_ANY, L"保存配置", ButtonStyle::Primary, wxDefaultPosition, dip(145, 40));
		m_saveBtn->SetIcon(SVG::COPY, dip(16, 16), *wxWHITE);

		m_testBtn = new CustomButton(m_modelCard, wxID_ANY, L"测试模型", ButtonStyle::Secondary, wxDefaultPosition, dip(130, 40));
		m_testBtn->SetIcon(SVG::TRANSLATE, dip(16, 16));

		actionSizer->Add(m_saveBtn, 0, wxRIGHT, 12_dip);
		actionSizer->Add(m_testBtn, 0);
		modelCardSizer->Add(actionSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

		m_modelCard->SetSizer(modelCardSizer);
		m_mainSizer->Add(m_modelCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20_dip);

		// Group 2: OCR 模型 (OCR Model Settings Card)
		m_ocrCard = new wxPanel(m_contentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
		m_ocrCard->SetBackgroundColour(palette.cardBg);

		wxBoxSizer* ocrCardSizer = new wxBoxSizer(wxVERTICAL);

		// 卡片标题 + 状态指示
		wxBoxSizer* ocrTitleSizer = new wxBoxSizer(wxHORIZONTAL);

		wxBitmapBundle ocrTitleBundle = IconManager::GetIconBundle(SVG::OCR, dip(18, 18), palette.textPrimary);
		wxStaticBitmap* ocrTitleIcon = new wxStaticBitmap(m_ocrCard, wxID_ANY, ocrTitleBundle);

		m_ocrTitleText = new wxStaticText(m_ocrCard, wxID_ANY, L"OCR 模型");
		m_ocrTitleText->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
		m_ocrTitleText->SetForegroundColour(palette.textPrimary);

		m_ocrStatusBadge = new StatusBadge(m_ocrCard);

		ocrTitleSizer->Add(ocrTitleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
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

		ocrCardSizer->Add(ocrMmprojRow, 0, wxEXPAND | wxBOTTOM, 16_dip);

		// OCR 操作按钮 (`保存配置` & `测试模型`)
		wxBoxSizer* ocrActionSizer = new wxBoxSizer(wxHORIZONTAL);
		m_ocrSaveBtn = new CustomButton(m_ocrCard, wxID_ANY, L"保存配置", ButtonStyle::Primary,wxDefaultPosition, dip(145, 40));
		m_ocrSaveBtn->SetIcon(SVG::COPY, dip(16, 16), *wxWHITE);

		m_ocrTestBtn = new CustomButton(m_ocrCard, wxID_ANY, L"测试模型", ButtonStyle::Secondary,wxDefaultPosition, dip(130, 40));
		m_ocrTestBtn->SetIcon(SVG::TRANSLATE, dip(16, 16));

		ocrActionSizer->Add(m_ocrSaveBtn, 0, wxRIGHT, 12_dip);
		ocrActionSizer->Add(m_ocrTestBtn, 0);
		ocrCardSizer->Add(ocrActionSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

		// 底部说明
		m_ocrFooterPanel = new wxPanel(m_ocrCard, wxID_ANY, wxDefaultPosition,wxDefaultSize, wxBORDER_NONE);
		m_ocrFooterPanel->SetBackgroundColour(palette.cardBg);
		wxBoxSizer* ocrFooterSizer = new wxBoxSizer(wxHORIZONTAL);

		wxBitmapBundle infoBundle = IconManager::GetIconBundle(SVG::INFO, dip(16, 16), palette.accentPrimary);
		wxStaticBitmap* infoIcon =new wxStaticBitmap(m_ocrFooterPanel, wxID_ANY, infoBundle);

		m_ocrFooterText = new wxStaticText(m_ocrFooterPanel, wxID_ANY,L"OCR 模型需为 GGUF 格式，并配合对应的 mmproj 视觉投影器文件使用。");
		m_ocrFooterText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,wxFONTWEIGHT_NORMAL, false,"Microsoft YaHei"));
		m_ocrFooterText->SetForegroundColour(palette.textSecondary);

		ocrFooterSizer->Add(infoIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6_dip);
		ocrFooterSizer->Add(m_ocrFooterText, 0, wxALIGN_CENTER_VERTICAL);
		m_ocrFooterPanel->SetSizer(ocrFooterSizer);

		ocrCardSizer->Add(m_ocrFooterPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,16_dip);

		m_ocrCard->SetSizer(ocrCardSizer);
		m_mainSizer->Add(m_ocrCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20_dip);

		// Group 3: 划词翻译设置卡片
		m_selectionCard = new wxPanel(m_contentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
		m_selectionCard->SetBackgroundColour(palette.cardBg);
		wxBoxSizer* selSizer = new wxBoxSizer(wxVERTICAL);

		wxBoxSizer* selTitleSizer = new wxBoxSizer(wxHORIZONTAL);
		wxBitmapBundle selBundle = IconManager::GetIconBundle(SVG::TRANSLATE, dip(18, 18), palette.accentPrimary);
		wxStaticBitmap* selIcon = new wxStaticBitmap(m_selectionCard, wxID_ANY, selBundle);

		m_selectionTitleText = new wxStaticText(m_selectionCard, wxID_ANY, L"全局划词翻译设置");
		m_selectionTitleText->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
		m_selectionTitleText->SetForegroundColour(palette.textPrimary);

		selTitleSizer->Add(selIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
		selTitleSizer->Add(m_selectionTitleText, 0, wxALIGN_CENTER_VERTICAL);
		selSizer->Add(selTitleSizer, 0, wxALL, 16_dip);

		// 1. 启用开关
		m_selectionEnableCheck = new wxCheckBox(m_selectionCard, wxID_ANY, L"启用全局划词翻译（选中文本后在光标旁显示悬浮按钮）");
		m_selectionEnableCheck->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
		m_selectionEnableCheck->SetForegroundColour(palette.textPrimary);
		selSizer->Add(m_selectionEnableCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

		// 2. 触发模式选择
		wxArrayString modes;
		modes.Add(L"① 鼠标直接划词（拖拽选中文本后自动检测）");
		modes.Add(L"② 划词 + 辅助按键（按住辅助按键划词触发）");
		modes.Add(L"③ 双击划词 / 三击划段（双击选词或三击选段触发）");
		m_selectionModeRadio = new wxRadioBox(m_selectionCard, wxID_ANY, L"划词触发模式", wxDefaultPosition, wxDefaultSize, modes, 1, wxRA_SPECIFY_COLS);
		m_selectionModeRadio->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
		m_selectionModeRadio->SetForegroundColour(palette.textPrimary);
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
		m_preserveClipCheck->SetForegroundColour(palette.textPrimary);
		selSizer->Add(m_preserveClipCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

		// 5. 保存按钮与状态
		wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
		m_selectionSaveBtn = new CustomButton(m_selectionCard, wxID_ANY, L"保存划词设置", ButtonStyle::Primary);
		m_selectionStatusText = new wxStaticText(m_selectionCard, wxID_ANY, "");
		m_selectionStatusText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
		m_selectionStatusText->SetForegroundColour(palette.accentGreen);

		btnSizer->Add(m_selectionSaveBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12_dip);
		btnSizer->Add(m_selectionStatusText, 0, wxALIGN_CENTER_VERTICAL);
		selSizer->Add(btnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

		m_selectionCard->SetSizer(selSizer);
		m_mainSizer->Add(m_selectionCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20_dip);

		// Group 4: StarDict 词典设置卡片
		m_dictCard = new wxPanel(m_contentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
		m_dictCard->SetBackgroundColour(palette.cardBg);
		wxBoxSizer* dictSizer = new wxBoxSizer(wxVERTICAL);

		wxBoxSizer* dictTitleSizer = new wxBoxSizer(wxHORIZONTAL);
		wxBitmapBundle dictBundle = IconManager::GetIconBundle(SVG::DICTIONARY, dip(18, 18), palette.accentPrimary);
		wxStaticBitmap* dictIcon = new wxStaticBitmap(m_dictCard, wxID_ANY, dictBundle);

		m_dictTitleText = new wxStaticText(m_dictCard, wxID_ANY, L"本地 StarDict 词典设置");
		m_dictTitleText->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
		m_dictTitleText->SetForegroundColour(palette.textPrimary);

		m_dictStatusBadge = new StatusBadge(m_dictCard);

		dictTitleSizer->Add(dictIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
		dictTitleSizer->Add(m_dictTitleText, 0, wxALIGN_CENTER_VERTICAL);
		dictTitleSizer->AddStretchSpacer(1);
		dictTitleSizer->Add(m_dictStatusBadge, 0, wxALIGN_CENTER_VERTICAL);
		dictSizer->Add(dictTitleSizer, 0, wxEXPAND | wxALL, 16_dip);

		// 目录选择行
		wxBoxSizer* dictDirRow = new wxBoxSizer(wxHORIZONTAL);
		m_dictDirLabel = new wxStaticText(m_dictCard, wxID_ANY, L"词典目录", wxDefaultPosition, wxSize(70_dip, -1));
		m_dictDirLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
		m_dictDirLabel->SetForegroundColour(palette.textPrimary);

		m_dictDirPathCtrl = new CustomInputBox(m_dictCard, wxID_ANY, L"",
			L"指定包含 StarDict 词典 (.ifo / .idx / .dict) 的文件夹路径", wxDefaultPosition, wxSize(-1, 38_dip));
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
		m_dictSaveBtn = new CustomButton(m_dictCard, wxID_ANY, L"保存词典设置", ButtonStyle::Primary, wxDefaultPosition, dip(145, 40));
		m_dictSaveBtn->SetIcon(SVG::COPY, dip(16, 16), *wxWHITE);

		m_dictReloadBtn = new CustomButton(m_dictCard, wxID_ANY, L"重新扫描词典", ButtonStyle::Secondary, wxDefaultPosition, dip(145, 40));
		m_dictReloadBtn->SetIcon(SVG::REPLACE, dip(16, 16));

		m_dictStatusText = new wxStaticText(m_dictCard, wxID_ANY, "");
		m_dictStatusText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
		m_dictStatusText->SetForegroundColour(palette.accentGreen);

		dictActionRow->Add(m_dictSaveBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);
		dictActionRow->Add(m_dictReloadBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12_dip);
		dictActionRow->Add(m_dictStatusText, 0, wxALIGN_CENTER_VERTICAL);
		dictSizer->Add(dictActionRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

		// 已识别词典摘要展示框
		m_dictListTitleText = new wxStaticText(m_dictCard, wxID_ANY, L"当前已识别加载的词典：");
		m_dictListTitleText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
		m_dictListTitleText->SetForegroundColour(palette.textSecondary);
		dictSizer->Add(m_dictListTitleText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6_dip);

		m_dictListInfoCtrl = new TextCtrl(m_dictCard, wxID_ANY, L"",
			wxDefaultPosition, wxSize(-1, 120_dip), wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
		m_dictListInfoCtrl->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Consolas, Microsoft YaHei"));
		m_dictListInfoCtrl->SetBackgroundColour(palette.windowBg);
		m_dictListInfoCtrl->SetForegroundColour(palette.textPrimary);
		dictSizer->Add(m_dictListInfoCtrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

		m_dictCard->SetSizer(dictSizer);
		m_mainSizer->Add(m_dictCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20_dip);

		// Group 5: 日志与诊断设置卡片
		m_logCard = new wxPanel(m_contentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
		m_logCard->SetBackgroundColour(palette.cardBg);
		wxBoxSizer* logSizer = new wxBoxSizer(wxVERTICAL);

		wxBoxSizer* logTitleSizer = new wxBoxSizer(wxHORIZONTAL);
		wxBitmapBundle logBundle = IconManager::GetIconBundle(SVG::LOG, dip(18, 18), palette.accentPrimary);
		wxStaticBitmap* logIcon = new wxStaticBitmap(m_logCard, wxID_ANY, logBundle);

		m_logTitleText = new wxStaticText(m_logCard, wxID_ANY, L"运行日志与诊断设置");
		m_logTitleText->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
		m_logTitleText->SetForegroundColour(palette.textPrimary);

		logTitleSizer->Add(logIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
		logTitleSizer->Add(m_logTitleText, 0, wxALIGN_CENTER_VERTICAL);
		logSizer->Add(logTitleSizer, 0, wxALL, 16_dip);

		// 1. 保存日志到文件开关
		m_saveLogToFileCheck = new wxCheckBox(m_logCard, wxID_ANY, L"保存运行日志到本地文件（便于问题排查与诊断）");
		m_saveLogToFileCheck->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
		m_saveLogToFileCheck->SetForegroundColour(palette.textPrimary);
		logSizer->Add(m_saveLogToFileCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12_dip);

		// 2. 日志文件保存路径说明
		wxString logPathStr = wxString::FromUTF8(ConfigManager::GetDefaultLogFilePath());
		m_logPathInfoText = new wxStaticText(m_logCard, wxID_ANY, L"日志文件保存位置：" + logPathStr);
		m_logPathInfoText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
		m_logPathInfoText->SetForegroundColour(palette.textSecondary);
		logSizer->Add(m_logPathInfoText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

		// 3. 按钮行
		wxBoxSizer* logBtnSizer = new wxBoxSizer(wxHORIZONTAL);
		m_logSaveBtn = new CustomButton(m_logCard, wxID_ANY, L"保存日志设置", ButtonStyle::Primary);
		m_openLogDirBtn = new CustomButton(m_logCard, wxID_ANY, L"打开日志目录", ButtonStyle::Secondary);
		m_logStatusText = new wxStaticText(m_logCard, wxID_ANY, "");
		m_logStatusText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
		m_logStatusText->SetForegroundColour(palette.accentGreen);

		logBtnSizer->Add(m_logSaveBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
		logBtnSizer->Add(m_openLogDirBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12_dip);
		logBtnSizer->Add(m_logStatusText, 0, wxALIGN_CENTER_VERTICAL);
		logSizer->Add(logBtnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);

		m_logCard->SetSizer(logSizer);
		m_mainSizer->Add(m_logCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20_dip);

		// Group 6: 偏好设置卡片
		m_prefCard = new wxPanel(m_contentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
		m_prefCard->SetBackgroundColour(palette.cardBg);
		wxBoxSizer* prefSizer = new wxBoxSizer(wxVERTICAL);

		wxBoxSizer* prefTitleSizer = new wxBoxSizer(wxHORIZONTAL);
		wxBitmapBundle moonBundle = IconManager::GetIconBundle(SVG::MOON, dip(18, 18), palette.textPrimary);
		wxStaticBitmap* moonIcon = new wxStaticBitmap(m_prefCard, wxID_ANY, moonBundle);

		m_prefTitle = new wxStaticText(m_prefCard, wxID_ANY, L"深色主题与偏好");
		m_prefTitle->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
		m_prefTitle->SetForegroundColour(palette.textPrimary);

		prefTitleSizer->Add(moonIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
		prefTitleSizer->Add(m_prefTitle, 0, wxALIGN_CENTER_VERTICAL);
		prefSizer->Add(prefTitleSizer, 0, wxALL, 16_dip);

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

		// 初始化已保存的配置数据
		if (m_configManager) {
			auto cfg = m_configManager->GetConfig();
			SetModelPath(wxString::FromUTF8(cfg.modelPath));
			SetOcrModelPath(wxString::FromUTF8(cfg.ocrModelPath), wxString::FromUTF8(cfg.ocrMmprojPath));
			SetSelectionConfig(cfg);
			SetDictConfig(cfg);
			SetLogConfig(cfg);
		}

		// 事件绑定 - 翻译模型 Group
		m_browseBtn->Bind(wxEVT_BUTTON, &SettingsView::OnBrowseModel, this);
		m_openDirBtn->Bind(wxEVT_BUTTON, &SettingsView::OnOpenModelDir, this);
		m_saveBtn->Bind(wxEVT_BUTTON, &SettingsView::OnSaveConfig, this);
		m_testBtn->Bind(wxEVT_BUTTON, &SettingsView::OnTestModel, this);

		// 事件绑定 - OCR 模型 Group
		m_ocrBrowseBtn->Bind(wxEVT_BUTTON, &SettingsView::OnBrowseOcrModel, this);
		m_ocrMmprojBrowseBtn->Bind(wxEVT_BUTTON, &SettingsView::OnBrowseOcrMmproj, this);
		m_ocrSaveBtn->Bind(wxEVT_BUTTON, &SettingsView::OnSaveOcrConfig, this);
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
	}

	void SettingsView::SetModelPath(const wxString& path) {
		m_configuredPath = path;
		if (m_modelPathCtrl) {
			m_modelPathCtrl->SetValue(path);
		}

		bool loaded = false;
		if (m_modelManager) {
			auto info = m_modelManager->GetHealthStatus(TargetModelType::Translation);
			loaded = (info.state == ServerHealthState::Ready);
		}

		if (loaded || (!path.IsEmpty() && wxFileExists(path))) {
			m_statusBadge->SetStatus(ServerHealthState::Ready, L"● 已配置: " + wxFileName(path).GetFullName());
		}
		else {
			m_statusBadge->SetStatus(ServerHealthState::Unconfigured, L"● 未配置模型");
		}
	}

	void SettingsView::SetOcrModelPath(const wxString& mainPath,
		const wxString& mmprojPath) {
		if (m_ocrModelPathCtrl)
			m_ocrModelPathCtrl->SetValue(mainPath);
		if (m_ocrMmprojPathCtrl)
			m_ocrMmprojPathCtrl->SetValue(mmprojPath);

		UpdateOcrStatus();
	}

	void SettingsView::UpdateOcrStatus() {
		if (!m_ocrStatusBadge)
			return;

		wxString mainPath = m_ocrModelPathCtrl ? m_ocrModelPathCtrl->GetValue() : L"";
		wxString mmprojPath = m_ocrMmprojPathCtrl ? m_ocrMmprojPathCtrl->GetValue() : L"";

		bool loaded = false;
		if (m_modelManager) {
			auto info = m_modelManager->GetHealthStatus(TargetModelType::Ocr);
			loaded = (info.state == ServerHealthState::Ready);
		}
		if (!loaded && !mainPath.IsEmpty() && !mmprojPath.IsEmpty()) {
			loaded = wxFileExists(mainPath) && wxFileExists(mmprojPath);
		}

		if (loaded) {
			m_ocrStatusBadge->SetStatus(ServerHealthState::Ready, L"● 已配置: " + wxFileName(mainPath).GetFullName());
		}
		else {
			m_ocrStatusBadge->SetStatus(ServerHealthState::Unconfigured, L"● 未配置");
		}
	}

	void SettingsView::OnBrowseModel(wxCommandEvent& WXUNUSED(event)) {
		wxFileDialog openFileDialog(
			this, L"选择 GGUF 翻译大模型文件", "", "",
			"GGUF Model Files (*.gguf)|*.gguf|All Files (*.*)|*.*",
			wxFD_OPEN | wxFD_FILE_MUST_EXIST);

		if (openFileDialog.ShowModal() == wxID_OK) {
			wxString path = openFileDialog.GetPath();
			SetModelPath(path);
		}
	}

	void SettingsView::OnBrowseOcrModel(wxCommandEvent& WXUNUSED(event)) {
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

	void SettingsView::OnBrowseOcrMmproj(wxCommandEvent& WXUNUSED(event)) {
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

	void SettingsView::OnOpenModelDir(wxCommandEvent& WXUNUSED(event)) {
		wxString modelDir = wxString::FromUTF8(Downloader::GetDefaultModelDir());
		wxLaunchDefaultBrowser(modelDir);
	}

	void SettingsView::OnSaveConfig(wxCommandEvent& WXUNUSED(event)) {
		wxString path = m_modelPathCtrl->GetValue();
		SetModelPath(path);

		if (m_configManager) {
			m_configManager->SaveModelPath(path.ToUTF8().data());
		}

		if (!path.IsEmpty() && wxFileExists(path)) {
			wxMessageBox(L"翻译模型配置已成功保存至 config.ini 配置文件！", L"系统设置",
				wxOK | wxICON_INFORMATION, this);
		}
		else {
			wxMessageBox(L"请输入或选择合法的 GGUF 翻译模型路径！", L"系统设置",
				wxOK | wxICON_WARNING, this);
		}
	}

	void SettingsView::OnSaveOcrConfig(wxCommandEvent& WXUNUSED(event)) {
		wxString ocrPath = m_ocrModelPathCtrl ? m_ocrModelPathCtrl->GetValue() : L"";
		wxString mmprojPath = m_ocrMmprojPathCtrl ? m_ocrMmprojPathCtrl->GetValue() : L"";

		if (m_configManager) {
			m_configManager->SaveOcrConfig(ocrPath.ToUTF8().data(), mmprojPath.ToUTF8().data());
		}

		SetOcrModelPath(ocrPath, mmprojPath);

		if (wxFileExists(ocrPath) && wxFileExists(mmprojPath)) {
			wxMessageBox(L"OCR 主模型与 mmproj 视觉投影器配置已成功保存！",
				L"系统设置", wxOK | wxICON_INFORMATION, this);
		}
		else {
			wxMessageBox(L"请输入或选择合法的 OCR 主模型与 mmproj 视觉投影器文件路径！",
				L"系统设置", wxOK | wxICON_WARNING, this);
		}
	}

	void SettingsView::OnTestModel(wxCommandEvent& WXUNUSED(event)) {
		wxString path = m_modelPathCtrl->GetValue();
		if (path.IsEmpty() || !wxFileExists(path)) {
			wxMessageBox(L"请先配置合法的 .gguf 翻译模型文件路径！", L"测试模型失败",
				wxOK | wxICON_WARNING, this);
		}
		else {
			SetModelPath(path);
			wxMessageBox(L"正在测试推理，Hy-MT2 1.8B 翻译模型回应正常！",
				L"测试模型成功", wxOK | wxICON_INFORMATION, this);
		}
	}

	void SettingsView::OnTestOcrModel(wxCommandEvent& WXUNUSED(event)) {
		wxString mainPath = m_ocrModelPathCtrl ? m_ocrModelPathCtrl->GetValue() : L"";
		wxString mmprojPath = m_ocrMmprojPathCtrl ? m_ocrMmprojPathCtrl->GetValue() : L"";

		SetOcrModelPath(mainPath, mmprojPath);

		bool mainOk = !mainPath.IsEmpty() && wxFileExists(mainPath);
		bool mmprojOk = !mmprojPath.IsEmpty() && wxFileExists(mmprojPath);

		if (mainOk && mmprojOk) {
			wxMessageBox(L"OCR 模型路径与 mmproj 视觉投影器配置有效！",
				L"测试模型成功", wxOK | wxICON_INFORMATION, this);
		} else {
			wxString msg = L"OCR 模型配置校验失败:\n";
			if (!mainOk)
				msg += L" - 主模型路径无效或文件缺失\n";
			if (!mmprojOk)
				msg += L" - mmproj 视觉投影器路径无效或文件缺失\n";
			wxMessageBox(msg, L"测试 OCR 模型提示", wxOK | wxICON_WARNING, this);
		}
	}

	void SettingsView::UpdateTheme() {
		auto palette = ThemeColors::GetCurrentPalette();
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

		if (m_selectionCard)
			m_selectionCard->SetBackgroundColour(palette.cardBg);
		if (m_selectionTitleText)
			m_selectionTitleText->SetForegroundColour(palette.textPrimary);
		if (m_selectionEnableCheck)
			m_selectionEnableCheck->SetForegroundColour(palette.textPrimary);
		if (m_selectionModeRadio)
			m_selectionModeRadio->SetForegroundColour(palette.textPrimary);
		if (m_modifierKeyLabel)
			m_modifierKeyLabel->SetForegroundColour(palette.textPrimary);
		if (m_preserveClipCheck)
			m_preserveClipCheck->SetForegroundColour(palette.textPrimary);
		if (m_selectionSaveBtn)
			m_selectionSaveBtn->Refresh();

		if (m_dictCard)
			m_dictCard->SetBackgroundColour(palette.cardBg);
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

		if (m_dictBrowseBtn)
			m_dictBrowseBtn->Refresh();
		if (m_dictOpenDirBtn)
			m_dictOpenDirBtn->Refresh();
		if (m_dictSaveBtn)
			m_dictSaveBtn->Refresh();
		if (m_dictReloadBtn)
			m_dictReloadBtn->Refresh();

		if (m_logCard)
			m_logCard->SetBackgroundColour(palette.cardBg);
		if (m_logTitleText)
			m_logTitleText->SetForegroundColour(palette.textPrimary);
		if (m_saveLogToFileCheck)
			m_saveLogToFileCheck->SetForegroundColour(palette.textPrimary);
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
		if (m_browseBtn)
			m_browseBtn->Refresh();
		if (m_openDirBtn)
			m_openDirBtn->Refresh();
		if (m_saveBtn)
			m_saveBtn->Refresh();
		if (m_testBtn)
			m_testBtn->Refresh();

		if (m_ocrModelPathCtrl)
			m_ocrModelPathCtrl->UpdateTheme();
		if (m_ocrMmprojPathCtrl)
			m_ocrMmprojPathCtrl->UpdateTheme();

		if (m_dictDirPathCtrl)
			m_dictDirPathCtrl->UpdateTheme();
		if (m_dictListInfoCtrl) {
			m_dictListInfoCtrl->SetBackgroundColour(palette.windowBg);
			m_dictListInfoCtrl->SetForegroundColour(palette.textPrimary);
			m_dictListInfoCtrl->Refresh();
		}

		if (m_ocrBrowseBtn)
			m_ocrBrowseBtn->Refresh();
		if (m_ocrMmprojBrowseBtn)
			m_ocrMmprojBrowseBtn->Refresh();
		if (m_ocrSaveBtn)
			m_ocrSaveBtn->Refresh();
		if (m_ocrTestBtn)
			m_ocrTestBtn->Refresh();

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
		if (rotation == 0) return;
		int delta = (rotation > 0 ? -50_dip : 50_dip);
		ScrollTo(m_scrollOffsetY + delta);
		if (m_scrollBar) {
			m_scrollBar->NotifyActivity();
		}
	}

	void SettingsView::BindMouseWheelRecursively(wxWindow* win) {
		if (!win) return;
		if (dynamic_cast<TextCtrl*>(win)) {
			// TextCtrl 拥有独立的内部文本滚动与专属 ScrollBar，不应被父级设置页面滚轮拦截
			return;
		}
		win->Bind(wxEVT_MOUSEWHEEL, &SettingsView::OnMouseWheel, this);
		for (wxWindowList::compatibility_iterator node = win->GetChildren().GetFirst();
			 node; node = node->GetNext()) {
			BindMouseWheelRecursively(node->GetData());
		}
	}

	void SettingsView::ScrollTo(int targetY) {
		if (!m_viewport || !m_contentPanel) return;

		int vpHeight = m_viewport->GetClientSize().y;
		int contentHeight = m_contentPanel->GetSize().y;
		int maxScroll = std::max(0, contentHeight - vpHeight);

		m_scrollOffsetY = std::clamp(targetY, 0, maxScroll);
		m_contentPanel->Move(0, -m_scrollOffsetY);

		if (m_scrollBar) {
			m_scrollBar->SetScrollParams(m_scrollOffsetY, vpHeight, contentHeight);
		}
	}

	void SettingsView::UpdateLayoutAndScroll() {
		if (!m_viewport || !m_contentPanel) return;

		int vpWidth = m_viewport->GetClientSize().x;
		int vpHeight = m_viewport->GetClientSize().y;
		if (vpWidth <= 0 || vpHeight <= 0) return;

		m_contentPanel->SetSize(vpWidth, -1);
		m_contentPanel->Layout();

		int contentHeight = m_mainSizer ? m_mainSizer->GetMinSize().y : m_contentPanel->GetMinSize().y;
		contentHeight = std::max(contentHeight, vpHeight);

		int maxScroll = std::max(0, contentHeight - vpHeight);
		m_scrollOffsetY = std::clamp(m_scrollOffsetY, 0, maxScroll);

		m_contentPanel->SetSize(0, -m_scrollOffsetY, vpWidth, contentHeight);

		if (m_scrollBar) {
			m_scrollBar->SetScrollParams(m_scrollOffsetY, vpHeight, contentHeight);
		}
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
		if (!m_configManager) return;

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
		if (!m_configManager) return;

		wxString dirPath = m_dictDirPathCtrl ? m_dictDirPathCtrl->GetValue() : "";
		m_configManager->SaveDictDir(dirPath.ToUTF8().data());

		if (m_modelManager && m_modelManager->GetDictEngine()) {
			m_modelManager->GetDictEngine()->LoadDictionaries(dirPath.ToUTF8().data());
		}

		UpdateDictListSummary();

		if (m_dictStatusText) {
			m_dictStatusText->SetLabel(L"词典目录设置已保存并重新扫描！");
		}
	}

	void SettingsView::OnReloadDicts(wxCommandEvent& WXUNUSED(event)) {
		wxString dirPath = m_dictDirPathCtrl ? m_dictDirPathCtrl->GetValue() : "";
		if (m_modelManager && m_modelManager->GetDictEngine()) {
			size_t count = m_modelManager->GetDictEngine()->LoadDictionaries(dirPath.ToUTF8().data());
			UpdateDictListSummary();
			if (m_dictStatusText) {
				m_dictStatusText->SetLabel(wxString::Format(L"扫描完成，已加载 %zu 本词典！", count));
			}
		}
	}

	void SettingsView::UpdateDictListSummary() {
		if (!m_modelManager || !m_modelManager->GetDictEngine()) return;

		auto dictEngine = m_modelManager->GetDictEngine();
		auto dicts = dictEngine->GetLoadedDictionaries();
		size_t totalWords = dictEngine->GetTotalWordCount();

		if (m_dictStatusBadge) {
			if (dicts.empty()) {
				m_dictStatusBadge->SetStatus(ServerHealthState::Unconfigured, L"● 未加载词典");
			}
			else {
				m_dictStatusBadge->SetStatus(ServerHealthState::Ready,
					wxString::Format(L"● 已就绪: %zu 本词典 (%zu 词)", dicts.size(), totalWords));
			}
		}

		if (m_dictListInfoCtrl) {
			if (dicts.empty()) {
				m_dictListInfoCtrl->SetValue(L"（暂无已加载词典，请将 StarDict 格式的 .ifo / .idx / .dict 文件放入词典目录中）");
			}
			else {
				wxString summary;
				for (size_t i = 0; i < dicts.size(); ++i) {
					const auto& d = dicts[i];
					summary += wxString::Format(L"【%zu】 %s\n    ├─ 词条总量: %u 词\n    ├─ 词典版本: %s\n    ├─ 存储格式: %s\n    └─ 描述文件: %s\n\n",
						i + 1,
						wxString::FromUTF8(d.bookName),
						d.wordCount,
						wxString::FromUTF8(d.version.empty() ? "N/A" : d.version),
						wxString::FromUTF8(d.isDz ? "DictZip 压缩 (.dict.dz)" : "纯文本 (.dict)"),
						wxString::FromUTF8(d.ifoPath)
					);
				}
				m_dictListInfoCtrl->SetValue(summary.Trim());
			}
		}
	}

	void SettingsView::SetLogConfig(const AppConfig& cfg) {
		if (m_saveLogToFileCheck) {
			m_saveLogToFileCheck->SetValue(cfg.saveLogToFile);
		}
	}

	void SettingsView::OnSaveLogConfig(wxCommandEvent& WXUNUSED(event)) {
		if (!m_configManager) return;

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

} // namespace LinguaAlpaca::UI
