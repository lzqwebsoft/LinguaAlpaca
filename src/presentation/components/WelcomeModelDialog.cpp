#include "WelcomeModelDialog.hpp"
#include "CustomButton.hpp"
#include <wx/graphics.h>

namespace LinguaAlpaca::Presentation::Components {

WelcomeModelDialog::WelcomeModelDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, L"欢迎使用 LinguaAlpaca", wxDefaultPosition, wxSize(560, 480), wxDEFAULT_DIALOG_STYLE) {
    InitUI();
    CentreOnParent();
}

void WelcomeModelDialog::InitUI() {
    auto palette = Theme::ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    mainSizer->AddSpacer(20);

    // 1. Alpaca 吉祥物图标 🦙
    wxStaticText* mascotText = new wxStaticText(this, wxID_ANY, L"🦙");
    mascotText->SetFont(wxFont(42, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI Emoji"));
    mainSizer->Add(mascotText, 0, wxALIGN_CENTER);

    mainSizer->AddSpacer(10);

    // 2. 主标题 `欢迎使用 LinguaAlpaca`
    wxStaticText* titleText = new wxStaticText(this, wxID_ANY, L"欢迎使用 LinguaAlpaca");
    titleText->SetFont(wxFont(20, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    titleText->SetForegroundColour(palette.textPrimary);
    mainSizer->Add(titleText, 0, wxALIGN_CENTER);

    mainSizer->AddSpacer(10);

    // 3. 副标题说明
    wxStaticText* subtitleText = new wxStaticText(this, wxID_ANY, L"灵驼译 是一款基于 llama.cpp 的离线翻译工具，翻译质量高、保护隐私。");
    subtitleText->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    subtitleText->SetForegroundColour(palette.textSecondary);
    mainSizer->Add(subtitleText, 0, wxALIGN_CENTER);

    mainSizer->AddSpacer(20);

    // 4. 蓝框提示 Banner
    wxPanel* bannerPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    bannerPanel->SetBackgroundColour(palette.bannerBg);
    
    wxBoxSizer* bannerSizer = new wxBoxSizer(wxVERTICAL);
    
    wxStaticText* info1 = new wxStaticText(bannerPanel, wxID_ANY, L"ℹ  初次使用需要先下载并配置一个兼容的 GGUF 格式 翻译模型。");
    info1->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    info1->SetForegroundColour(palette.bannerText);

    wxStaticText* info2 = new wxStaticText(bannerPanel, wxID_ANY, L"推荐模型: 「Hy-MT2-1.8B-Q4_K_M.gguf」(腾讯混元中英日韩离线翻译大模型)");
    info2->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    info2->SetForegroundColour(palette.bannerText);

    bannerSizer->Add(info1, 0, wxALL, 12);
    bannerSizer->Add(info2, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
    bannerPanel->SetSizer(bannerSizer);

    mainSizer->Add(bannerPanel, 0, wxEXPAND | wxLEFT | wxRIGHT, 24);

    mainSizer->AddSpacer(24);

    // 5. 按钮操作栏 (`⚙ 前往设置配置模型` & `稍后再说`)
    wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    CustomButton* goToSettingsBtn = new CustomButton(this, wxID_ANY, L"⚙  前往设置配置模型", ButtonStyle::Primary, wxDefaultPosition, wxSize(190, 42));
    CustomButton* laterBtn = new CustomButton(this, wxID_ANY, L"稍后再说", ButtonStyle::Secondary, wxDefaultPosition, wxSize(120, 42));

    btnSizer->Add(goToSettingsBtn, 0, wxRIGHT, 12);
    btnSizer->Add(laterBtn, 0);
    mainSizer->Add(btnSizer, 0, wxALIGN_CENTER);

    mainSizer->AddSpacer(18);

    // 6. 底部 Footer 说明
    wxStaticText* footerText = new wxStaticText(this, wxID_ANY, L"配置后即可开始翻译，支持中、英、日、韩等多语言");
    footerText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    footerText->SetForegroundColour(palette.textSecondary);
    mainSizer->Add(footerText, 0, wxALIGN_CENTER);

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

} // namespace LinguaAlpaca::Presentation::Components
