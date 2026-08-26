#include "WelcomeModelDialog.hpp"
#include "CustomButton.hpp"
#include "../theme/IconManager.hpp"
#include "../theme/AppIcons.hpp"

namespace LinguaAlpaca::UI {

    WelcomeModelDialog::WelcomeModelDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, L"欢迎使用 LinguaAlpaca", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE) {
        InitUI();
        Fit();
        CentreOnParent();
    }

    void WelcomeModelDialog::InitUI() {
        auto palette = ThemeColors::GetCurrentPalette();
        SetBackgroundColour(palette.windowBg);

        wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

        mainSizer->AddSpacer(18_dip);

        // 1. App Logo (48x48)
        wxBitmapBundle logoBundle = IconManager::GetAppLogoBundle(wxSize(48, 48));
        wxStaticBitmap* logoBitmap = new wxStaticBitmap(this, wxID_ANY, logoBundle);
        mainSizer->Add(logoBitmap, 0, wxALIGN_CENTER);

        mainSizer->AddSpacer(10_dip);

        // 2. 主标题 `欢迎使用 LinguaAlpaca 灵驼译`
        wxStaticText* titleText = new wxStaticText(this, wxID_ANY, L"欢迎使用 LinguaAlpaca 灵驼译");
        titleText->SetFont(wxFont(16, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
        titleText->SetForegroundColour(palette.textPrimary);
        mainSizer->Add(titleText, 0, wxALIGN_CENTER);

        mainSizer->AddSpacer(6_dip);

        // 3. 副标题口号说明
        wxStaticText* subtitleText = new wxStaticText(this, wxID_ANY, L"凭本地之智，见世界之全 —— 端侧多模态全能离线翻译助手");
        subtitleText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
        subtitleText->SetForegroundColour(palette.accentPrimary);
        mainSizer->Add(subtitleText, 0, wxALIGN_CENTER);

        mainSizer->AddSpacer(16_dip);

        // 4. 提示 Banner 卡片
        wxPanel* bannerPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        bannerPanel->SetBackgroundColour(palette.cardBg);

        wxBoxSizer* bannerSizer = new wxBoxSizer(wxVERTICAL);

        wxStaticText* infoTitle = new wxStaticText(bannerPanel, wxID_ANY, L"初次使用请先配置离线大模型或词典");
        infoTitle->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
        infoTitle->SetForegroundColour(palette.textPrimary);
        bannerSizer->Add(infoTitle, 0, wxALL, 12_dip);

        const wchar_t* guideItems[] = {
            L"• 翻译模型推荐：Tencent Hy-MT2-1.8B-GGUF (中英日韩多语言翻译模型)",
            L"• 视觉识别推荐：PaddleOCR-VL-1.6-GGUF (配合 mmproj 视觉投影器)",
            L"• 本地词典支持：StarDict 免费离线词典库 (.ifo / .idx / .dict 格式秒查)"
        };

        for (const auto& item : guideItems) {
            wxStaticText* itemText = new wxStaticText(bannerPanel, wxID_ANY, item);
            itemText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
            itemText->SetForegroundColour(palette.textSecondary);
            bannerSizer->Add(itemText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6_dip);
        }

        bannerPanel->SetSizer(bannerSizer);
        mainSizer->Add(bannerPanel, 0, wxEXPAND | wxLEFT | wxRIGHT, 20_dip);

        mainSizer->AddSpacer(18_dip);

        // 5. 按钮操作栏 (`前往设置配置模型` & `稍后再说`)
        wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
        CustomButton* goToSettingsBtn = new CustomButton(this, wxID_ANY, L"前往设置配置模型", ButtonStyle::Primary, wxDefaultPosition, dip(170, 38));
        goToSettingsBtn->SetIcon(SVG::SETTINGS, dip(15, 15), *wxWHITE);

        CustomButton* laterBtn = new CustomButton(this, wxID_ANY, L"稍后再说", ButtonStyle::Secondary, wxDefaultPosition, dip(105, 38));

        btnSizer->Add(goToSettingsBtn, 0, wxRIGHT, 10_dip);
        btnSizer->Add(laterBtn, 0);
        mainSizer->Add(btnSizer, 0, wxALIGN_CENTER);

        mainSizer->AddSpacer(12_dip);

        // 6. 底部 Footer 说明
        wxStaticText* footerText = new wxStaticText(this, wxID_ANY, L"全本地端侧离线运算 · 零云端数据交互 · 彻底保护隐私安全");
        footerText->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
        footerText->SetForegroundColour(palette.textSecondary);
        mainSizer->Add(footerText, 0, wxALIGN_CENTER | wxBOTTOM, 14_dip);

        SetSizer(mainSizer);

        goToSettingsBtn->Bind(wxEVT_BUTTON, &WelcomeModelDialog::OnGoToSettings, this);
        laterBtn->Bind(wxEVT_BUTTON, &WelcomeModelDialog::OnLater, this);
    }

    void WelcomeModelDialog::OnGoToSettings(wxCommandEvent& WXUNUSED(event)) {
        m_goToSettings = true;
        EndModal(wxID_OK);
    }

    void WelcomeModelDialog::OnLater(wxCommandEvent& WXUNUSED(event)) {
        m_goToSettings = false;
        EndModal(wxID_CANCEL);
    }

} // namespace LinguaAlpaca::UI
