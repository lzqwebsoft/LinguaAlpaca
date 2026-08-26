#include "AboutDialog.hpp"
#include "CustomButton.hpp"
#include "../theme/IconManager.hpp"

namespace LinguaAlpaca::UI {

    AboutDialog::AboutDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, L"关于 LinguaAlpaca", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE) {
        InitUI();
        Fit();
        CentreOnParent();
    }

    void AboutDialog::InitUI() {
        auto palette = ThemeColors::GetCurrentPalette();
        SetBackgroundColour(palette.windowBg);

        wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

        mainSizer->AddSpacer(16_dip);

        // 1. App Logo 与主标题 Header
        wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

        wxBitmapBundle logoBundle = IconManager::GetAppLogoBundle(wxSize(44, 44));
        wxStaticBitmap* logoBitmap = new wxStaticBitmap(this, wxID_ANY, logoBundle);

        wxBoxSizer* titleCol = new wxBoxSizer(wxVERTICAL);

        wxBoxSizer* nameRow = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* nameText = new wxStaticText(this, wxID_ANY, L"LinguaAlpaca 灵驼译");
        nameText->SetFont(wxFont(15, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
        nameText->SetForegroundColour(palette.textPrimary);

        wxStaticText* versionBadge = new wxStaticText(this, wxID_ANY, L" v1.0.0 ");
        versionBadge->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
        versionBadge->SetForegroundColour(palette.badgeText);
        versionBadge->SetBackgroundColour(palette.badgeBg);

        nameRow->Add(nameText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
        nameRow->Add(versionBadge, 0, wxALIGN_CENTER_VERTICAL);

        wxStaticText* sloganText = new wxStaticText(this, wxID_ANY, L"凭本地之智，见世界之全 —— 端侧多模态全能离线翻译助手");
        sloganText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
        sloganText->SetForegroundColour(palette.accentPrimary);

        titleCol->Add(nameRow, 0, wxBOTTOM, 3_dip);
        titleCol->Add(sloganText, 0);

        headerSizer->Add(logoBitmap, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14_dip);
        headerSizer->Add(titleCol, 1, wxALIGN_CENTER_VERTICAL);

        mainSizer->Add(headerSizer, 0, wxEXPAND | wxLEFT | wxRIGHT, 20_dip);
        mainSizer->AddSpacer(12_dip);

        // 2. 核心特性与目标初衷 Card
        wxPanel* infoCard = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        infoCard->SetBackgroundColour(palette.cardBg);

        wxBoxSizer* infoCardSizer = new wxBoxSizer(wxVERTICAL);

        wxStaticText* introTitle = new wxStaticText(infoCard, wxID_ANY, L"核心特性");
        introTitle->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
        introTitle->SetForegroundColour(palette.textPrimary);
        infoCardSizer->Add(introTitle, 0, wxALL, 12_dip);

        const wchar_t* features[] = {
            L"• 100% 纯本地端侧离线计算：零数据上传云端，彻底杜绝数据与隐私泄露隐患。",
            L"• 原生双 llama-server 并发：支持大模型机器翻译与 OCR 视觉多模态独立并行调度。",
            L"• 前沿大模型深度适配：Tencent Hy-MT2 1.8B 多语言翻译与 PaddleOCR-VL 视觉图文提取。",
            L"• StarDict 本地词典秒查：内置海量本地多词典高速索引，全局鼠标划词悬浮窗即划即译。",
            L"• 现代高分屏弹性流式 UI，支持深色/浅色/跟随系统主题。"
        };

        for (const auto& feat : features) {
            wxStaticText* featText = new wxStaticText(infoCard, wxID_ANY, feat);
            featText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
            featText->SetForegroundColour(palette.textSecondary);
            infoCardSizer->Add(featText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6_dip);
        }

        infoCard->SetSizer(infoCardSizer);
        mainSizer->Add(infoCard, 0, wxEXPAND | wxLEFT | wxRIGHT, 20_dip);

        mainSizer->AddSpacer(14_dip);

        // 3. 访问 GitHub 项目主页按钮 (居中)
        CustomButton* githubBtn = new CustomButton(this, wxID_ANY, L"访问 GitHub 项目主页", ButtonStyle::Primary, wxDefaultPosition, dip(180, 36));
        githubBtn->SetIcon(SVG::BROWSE, dip(14, 14), *wxWHITE);

        mainSizer->Add(githubBtn, 0, wxALIGN_CENTER);

        mainSizer->AddSpacer(10_dip);

        // 4. 底部版权信息
        wxStaticText* copyrightText = new wxStaticText(this, wxID_ANY, L"开源项目");
        copyrightText->SetFont(wxFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
        copyrightText->SetForegroundColour(palette.textSecondary);
        mainSizer->Add(copyrightText, 0, wxALIGN_CENTER | wxBOTTOM, 14_dip);

        SetSizer(mainSizer);

        githubBtn->Bind(wxEVT_BUTTON, &AboutDialog::OnVisitGithub, this);
    }

    void AboutDialog::OnVisitGithub(wxCommandEvent& WXUNUSED(event)) {
        wxLaunchDefaultBrowser("https://github.com/lzqwebsoft/LinguaAlpaca");
    }

} // namespace LinguaAlpaca::UI
