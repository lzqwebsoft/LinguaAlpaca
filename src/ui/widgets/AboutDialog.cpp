#include "AboutDialog.hpp"
#include "CustomButton.hpp"
#include "core/AppVersion.hpp"
#include "../theme/IconManager.hpp"
#include "../theme/AppIcons.hpp"

namespace LinguaAlpaca::UI {

    AboutDialog::AboutDialog(wxWindow* parent, const wxString& version)
        : wxDialog(parent, wxID_ANY, L"关于 LinguaAlpaca", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE),
          m_version(version.IsEmpty() ? wxString::FromUTF8(GetAppVersion()) : version) {
        InitUI();
        Fit();
        CentreOnParent();
        StartVersionCheck();
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

        // 第一行：应用名称与版本徽标
        wxBoxSizer* nameRow = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* nameText = new wxStaticText(this, wxID_ANY, L"LinguaAlpaca 灵驼译");
        nameText->SetFont(ThemeFont::GetFont(FontRole::SectionTitle));
        nameText->SetForegroundColour(palette.textPrimary);

        wxStaticText* versionBadge = new wxStaticText(this, wxID_ANY, L"v" + m_version);
        versionBadge->SetFont(ThemeFont::GetFont(FontRole::Badge));
        versionBadge->SetForegroundColour(palette.accentPrimary);

        nameRow->Add(nameText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
        nameRow->Add(versionBadge, 0, wxALIGN_CENTER_VERTICAL);

        // 第二行：口号
        wxStaticText* sloganText = new wxStaticText(this, wxID_ANY, L"端侧多模态全能离线翻译助手");
        sloganText->SetFont(ThemeFont::GetFont(FontRole::Control));
        sloganText->SetForegroundColour(palette.accentPrimary);

        titleCol->Add(nameRow, 0, wxBOTTOM, 3_dip);
        titleCol->Add(sloganText, 0);

        // 右侧：版本更新状态与手动检查按钮 (与左侧标题口号平齐，节省垂直高度)
        wxBoxSizer* updateRow = new wxBoxSizer(wxHORIZONTAL);
        m_updateStatusText = new wxStaticText(this, wxID_ANY, L"正在检查新版本...");
        m_updateStatusText->SetFont(ThemeFont::GetFont(FontRole::Caption));
        m_updateStatusText->SetForegroundColour(palette.textSecondary);

        m_checkBtn = new CustomButton(this, wxID_ANY, L"检查更新", ButtonStyle::Secondary, wxDefaultPosition, dip(74, 24));
        m_checkBtn->SetFont(ThemeFont::GetFont(FontRole::Caption));

        updateRow->Add(m_updateStatusText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
        updateRow->Add(m_checkBtn, 0, wxALIGN_CENTER_VERTICAL);

        headerSizer->Add(logoBitmap, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14_dip);
        headerSizer->Add(titleCol, 0, wxALIGN_CENTER_VERTICAL);
        headerSizer->AddStretchSpacer(1);
        headerSizer->Add(updateRow, 0, wxALIGN_CENTER_VERTICAL);

        mainSizer->Add(headerSizer, 0, wxEXPAND | wxLEFT | wxRIGHT, 20_dip);
        mainSizer->AddSpacer(12_dip);

        // 1.5 新版本提示 Card (发现新版本时展示)
        m_updateCard = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        m_updateCard->SetBackgroundColour(palette.bannerBg);

        wxBoxSizer* updateCardSizer = new wxBoxSizer(wxVERTICAL);

        wxBoxSizer* updateCardTopRow = new wxBoxSizer(wxHORIZONTAL);
        m_updateCardTitle = new wxStaticText(m_updateCard, wxID_ANY, L"发现新版本！");
        m_updateCardTitle->SetFont(ThemeFont::GetFont(FontRole::Control, true));
        m_updateCardTitle->SetForegroundColour(palette.bannerText);

        m_updateActionBtn = new CustomButton(m_updateCard, wxID_ANY, L"前往下载更新", ButtonStyle::Green, wxDefaultPosition, dip(120, 26));
        m_updateActionBtn->SetFont(ThemeFont::GetFont(FontRole::Caption));
        m_updateActionBtn->SetIcon(SVG::DOWNLOAD, dip(14, 14), *wxWHITE);

        updateCardTopRow->Add(m_updateCardTitle, 1, wxALIGN_CENTER_VERTICAL);
        updateCardTopRow->Add(m_updateActionBtn, 0, wxALIGN_CENTER_VERTICAL);

        updateCardSizer->Add(updateCardTopRow, 0, wxEXPAND | wxALL, 10_dip);

        m_updateCardNotes = new wxStaticText(m_updateCard, wxID_ANY, wxEmptyString);
        m_updateCardNotes->SetFont(ThemeFont::GetFont(FontRole::Caption));
        m_updateCardNotes->SetForegroundColour(palette.bannerText);
        updateCardSizer->Add(m_updateCardNotes, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10_dip);

        m_updateCard->SetSizer(updateCardSizer);

        wxBoxSizer* updateWrapper = new wxBoxSizer(wxVERTICAL);
        updateWrapper->Add(m_updateCard, 0, wxEXPAND | wxBOTTOM, 12_dip);
        mainSizer->Add(updateWrapper, 0, wxEXPAND | wxLEFT | wxRIGHT, 20_dip);
        m_updateCard->Hide();

        // 2. 核心特性与目标初衷 Card
        wxPanel* infoCard = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        infoCard->SetBackgroundColour(palette.cardBg);

        wxBoxSizer* infoCardSizer = new wxBoxSizer(wxVERTICAL);

        wxStaticText* introTitle = new wxStaticText(infoCard, wxID_ANY, L"核心特性");
        introTitle->SetFont(ThemeFont::GetFont(FontRole::Control, true));
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
            featText->SetFont(ThemeFont::GetFont(FontRole::Control));
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
        copyrightText->SetFont(ThemeFont::GetFont(FontRole::Caption));
        copyrightText->SetForegroundColour(palette.textSecondary);
        mainSizer->Add(copyrightText, 0, wxALIGN_CENTER | wxBOTTOM, 14_dip);

        SetSizer(mainSizer);

        m_checkBtn->Bind(wxEVT_BUTTON, &AboutDialog::OnCheckUpdate, this);
        m_updateActionBtn->Bind(wxEVT_BUTTON, &AboutDialog::OnOpenReleases, this);
        githubBtn->Bind(wxEVT_BUTTON, &AboutDialog::OnVisitGithub, this);
    }

    void AboutDialog::StartVersionCheck() {
        if (!m_checkBtn || !m_updateStatusText) return;

        auto palette = ThemeColors::GetCurrentPalette();
        m_updateStatusText->SetLabel(L"正在检查新版本...");
        m_updateStatusText->SetForegroundColour(palette.textSecondary);
        m_checkBtn->SetLabel(L"检查中...");
        m_checkBtn->Enable(false);
        Layout();

        VersionChecker::CheckLatestVersionAsync(
            m_version.ToStdString(),
            BindUi([this](const VersionCheckResult& result) {
                auto pal = ThemeColors::GetCurrentPalette();
                m_checkBtn->Enable(true);

                if (!result.success) {
                    m_updateStatusText->SetLabel(L"检查更新失败");
                    m_updateStatusText->SetForegroundColour(pal.textSecondary);
                    m_checkBtn->SetLabel(L"重试");
                    m_updateCard->Hide();
                    Fit();
                    Layout();
                    return;
                }

                if (!result.hasUpdate) {
                    m_updateStatusText->SetLabel(L"✓ 当前已是最新版本");
                    m_updateStatusText->SetForegroundColour(pal.accentGreen);
                    m_checkBtn->SetLabel(L"重新检查");
                    m_updateCard->Hide();
                    Fit();
                    Layout();
                    return;
                }

                // 发现新版本
                m_latestVersion = wxString::FromUTF8(result.latestVersion);
                if (!result.releaseUrl.empty()) {
                    m_releaseUrl = wxString::FromUTF8(result.releaseUrl);
                } else {
                    m_releaseUrl = "https://github.com/lzqwebsoft/LinguaAlpaca/releases";
                }

                m_updateStatusText->SetLabel(L"✨ 发现新版本: v" + m_latestVersion);
                m_updateStatusText->SetForegroundColour(pal.accentPrimary);
                m_checkBtn->SetLabel(L"重新检查");

                // 更新 Banner 卡片内容
                m_updateCardTitle->SetLabel(L"🎉 发现新版本 v" + m_latestVersion + L" 可供下载！");

                wxString notes = wxString::FromUTF8(result.releaseNotes);
                notes.Trim(true).Trim(false);
                if (notes.IsEmpty()) {
                    notes = L"点击右侧按钮前往 GitHub Releases 页面获取最新安装包。";
                } else if (notes.Length() > 240) {
                    notes = notes.Left(237) + L"...";
                }
                m_updateCardNotes->SetLabel(notes);

                m_updateCard->Show();
                Fit();
                Layout();
            })
        );
    }

    void AboutDialog::OnCheckUpdate(wxCommandEvent& WXUNUSED(event)) {
        StartVersionCheck();
    }

    void AboutDialog::OnVisitGithub(wxCommandEvent& WXUNUSED(event)) {
        wxLaunchDefaultBrowser("https://github.com/lzqwebsoft/LinguaAlpaca");
    }

    void AboutDialog::OnOpenReleases(wxCommandEvent& WXUNUSED(event)) {
        wxString url = m_releaseUrl.IsEmpty() ? wxString("https://github.com/lzqwebsoft/LinguaAlpaca/releases") : m_releaseUrl;
        wxLaunchDefaultBrowser(url);
    }

} // namespace LinguaAlpaca::UI
