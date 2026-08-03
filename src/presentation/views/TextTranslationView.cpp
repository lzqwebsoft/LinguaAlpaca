#include "TextTranslationView.hpp"
#include "../theme/ThemeColors.hpp"
#include "../theme/IconManager.hpp"
#include <wx/clipbrd.h>

namespace LinguaAlpaca::Presentation::Views {

TextTranslationView::TextTranslationView(
    wxWindow* parent,
    std::shared_ptr<Application::Service::TranslationService> translationService,
    wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_translationService(std::move(translationService)) {
    InitUI();
}

void TextTranslationView::InitUI() {
    auto palette = Theme::ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // 1. 顶栏：SVG 图标 + 标题 (文本翻译) + 状态标签 (● 监听中)
    wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBitmapBundle titleBundle = Theme::IconManager::GetIconBundle(Theme::SVG::TEXT, wxSize(22, 22), palette.accentPrimary);
    wxStaticBitmap* titleIcon = new wxStaticBitmap(this, wxID_ANY, titleBundle);

    m_titleText = new wxStaticText(this, wxID_ANY, L"文本翻译");
    m_titleText->SetFont(wxFont(18, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_titleText->SetForegroundColour(palette.textPrimary);

    m_statusBadge = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(78, 28), wxBORDER_NONE);
    m_statusBadge->SetBackgroundColour(palette.badgeBg);
    wxBoxSizer* badgeSizer = new wxBoxSizer(wxHORIZONTAL);
    m_badgeText = new wxStaticText(m_statusBadge, wxID_ANY, L"●  监听中");
    m_badgeText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_badgeText->SetForegroundColour(palette.badgeText);
    badgeSizer->Add(m_badgeText, 0, wxALIGN_CENTER);
    m_statusBadge->SetSizer(badgeSizer);

    headerSizer->Add(titleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    headerSizer->Add(m_titleText, 0, wxALIGN_CENTER_VERTICAL);
    headerSizer->AddStretchSpacer(1);
    headerSizer->Add(m_statusBadge, 0, wxALIGN_CENTER_VERTICAL);

    mainSizer->Add(headerSizer, 0, wxEXPAND | wxALL, 20);

    // 2. 划词状态提示 Banner (SVG Info Icon)
    m_bannerPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 44), wxBORDER_NONE);
    m_bannerPanel->SetBackgroundColour(palette.bannerBg);

    wxBoxSizer* bannerSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBitmapBundle infoBundle = Theme::IconManager::GetIconBundle(Theme::SVG::INFO, wxSize(16, 16), palette.textSecondary);
    wxStaticBitmap* infoIcon = new wxStaticBitmap(m_bannerPanel, wxID_ANY, infoBundle);

    m_bannerText = new wxStaticText(m_bannerPanel, wxID_ANY, L"选中任意文本 · 自动翻译 · 当前选中：");
    m_bannerText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_bannerText->SetForegroundColour(palette.bannerText);

    m_selectedTagPanel = new wxPanel(m_bannerPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 24), wxBORDER_NONE);
    m_selectedTagPanel->SetBackgroundColour(palette.bannerBg);
    wxBoxSizer* tagSizer = new wxBoxSizer(wxHORIZONTAL);
    m_tagText = new wxStaticText(m_selectedTagPanel, wxID_ANY, L"「Hello World」");
    m_tagText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_tagText->SetForegroundColour(palette.bannerText);
    tagSizer->Add(m_tagText, 0, wxALIGN_CENTER);
    m_selectedTagPanel->SetSizer(tagSizer);

    m_instantTransBtn = new Components::CustomButton(m_bannerPanel, wxID_ANY, L"立即翻译", Components::ButtonStyle::Primary, wxDefaultPosition, wxSize(96, 32));

    bannerSizer->Add(infoIcon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16);
    bannerSizer->Add(m_bannerText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
    bannerSizer->Add(m_selectedTagPanel, 0, wxALIGN_CENTER_VERTICAL);
    bannerSizer->AddStretchSpacer(1);
    bannerSizer->Add(m_instantTransBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    m_bannerPanel->SetSizer(bannerSizer);
    mainSizer->Add(m_bannerPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20);

    // 3. 语言选择条
    m_langPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 52), wxBORDER_NONE);
    m_langPanel->SetBackgroundColour(palette.cardBg);

    wxBoxSizer* langSizer = new wxBoxSizer(wxHORIZONTAL);
    m_langSelector = new Components::LanguageSelectorBar(m_langPanel);
    langSizer->Add(m_langSelector, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 12);
    m_langPanel->SetSizer(langSizer);

    mainSizer->Add(m_langPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20);

    // 4. 原文与译文卡片区
    wxBoxSizer* cardsSizer = new wxBoxSizer(wxHORIZONTAL);
    m_sourceCard = new Components::CardPanel(this, L"原文", false);
    m_sourceCard->AddToolIcon(1, Theme::SVG::PASTE, L"粘贴文本", [this]() {
        if (wxTheClipboard->Open()) {
            if (wxTheClipboard->IsSupported(wxDF_TEXT)) {
                wxTextDataObject data;
                wxTheClipboard->GetData(data);
                m_sourceCard->GetTextCtrl()->SetValue(data.GetText());
            }
            wxTheClipboard->Close();
        }
    });
    m_sourceCard->AddToolIcon(2, Theme::SVG::CLEAR, L"清空原文", [this]() {
        m_sourceCard->GetTextCtrl()->Clear();
        m_sourceCard->SetCharacterCount(0);
    });

    m_sourceCard->GetTextCtrl()->SetValue(L"Hello, welcome to Lingo Translator!");
    m_sourceCard->SetCharacterCount(35);

    m_targetCard = new Components::CardPanel(this, L"译文", true);
    m_targetCard->AddToolIcon(1, Theme::SVG::COPY, L"复制译文", [this]() {
        wxString text = m_targetCard->GetTextCtrl()->GetValue();
        if (!text.IsEmpty() && wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(text));
            wxTheClipboard->Close();
        }
    });
    m_targetCard->AddToolIcon(2, Theme::SVG::SPEAKER, L"朗读", []() {});

    m_targetCard->GetTextCtrl()->SetValue(L"你好，欢迎使用轻译翻译器！");
    m_targetCard->SetCharacterCount(13);

    cardsSizer->Add(m_sourceCard, 1, wxEXPAND | wxRIGHT, 12);
    cardsSizer->Add(m_targetCard, 1, wxEXPAND | wxLEFT, 12);

    mainSizer->Add(cardsSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20);

    // 5. 底部操作按钮栏
    wxBoxSizer* bottomSizer = new wxBoxSizer(wxHORIZONTAL);

    m_translateBtn = new Components::CustomButton(this, wxID_ANY, L"翻译", Components::ButtonStyle::Primary, wxDefaultPosition, wxSize(110, 42));
    m_translateBtn->SetIcon(Theme::SVG::TRANSLATE, wxSize(16, 16), *wxWHITE);

    m_stopBtn      = new Components::CustomButton(this, wxID_ANY, L"中断翻译", Components::ButtonStyle::Danger, wxDefaultPosition, wxSize(130, 42));
    m_stopBtn->SetIcon(Theme::SVG::STOP, wxSize(16, 16), *wxWHITE);

    m_clearBtn     = new Components::CustomButton(this, wxID_ANY, L"清空", Components::ButtonStyle::Secondary, wxDefaultPosition, wxSize(100, 42));
    m_clearBtn->SetIcon(Theme::SVG::CLEAR, wxSize(16, 16));

    m_swapBtn      = new Components::CustomButton(this, wxID_ANY, L"交换", Components::ButtonStyle::Secondary, wxDefaultPosition, wxSize(100, 42));
    m_swapBtn->SetIcon(Theme::SVG::SWAP, wxSize(16, 16));

    m_copyBtn      = new Components::CustomButton(this, wxID_ANY, L"复制译文", Components::ButtonStyle::Green, wxDefaultPosition, wxSize(120, 42));
    m_copyBtn->SetIcon(Theme::SVG::COPY, wxSize(16, 16), *wxWHITE);

    m_stopBtn->Hide();

    bottomSizer->Add(m_translateBtn, 0, wxRIGHT, 12);
    bottomSizer->Add(m_stopBtn, 0, wxRIGHT, 12);
    bottomSizer->Add(m_clearBtn, 0);
    bottomSizer->AddStretchSpacer(1);
    bottomSizer->Add(m_swapBtn, 0, wxRIGHT, 12);
    bottomSizer->Add(m_copyBtn, 0);

    mainSizer->Add(bottomSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 20);

    SetSizer(mainSizer);

    // 事件绑定
    m_translateBtn->Bind(wxEVT_BUTTON, &TextTranslationView::OnTranslateClicked, this);
    m_stopBtn->Bind(wxEVT_BUTTON, &TextTranslationView::OnStopClicked, this);
    m_instantTransBtn->Bind(wxEVT_BUTTON, &TextTranslationView::OnTranslateClicked, this);
    m_clearBtn->Bind(wxEVT_BUTTON, &TextTranslationView::OnClearClicked, this);
    m_swapBtn->Bind(wxEVT_BUTTON, &TextTranslationView::OnSwapClicked, this);
    m_copyBtn->Bind(wxEVT_BUTTON, &TextTranslationView::OnCopyTargetClicked, this);
    m_sourceCard->GetTextCtrl()->Bind(wxEVT_TEXT, &TextTranslationView::OnSourceTextChanged, this);
}

void TextTranslationView::UpdateTheme() {
    auto palette = Theme::ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    if (m_titleText) m_titleText->SetForegroundColour(palette.textPrimary);
    if (m_statusBadge) m_statusBadge->SetBackgroundColour(palette.badgeBg);
    if (m_badgeText) m_badgeText->SetForegroundColour(palette.badgeText);

    if (m_bannerPanel) m_bannerPanel->SetBackgroundColour(palette.bannerBg);
    if (m_bannerText) m_bannerText->SetForegroundColour(palette.bannerText);
    if (m_selectedTagPanel) m_selectedTagPanel->SetBackgroundColour(palette.bannerBg);
    if (m_tagText) m_tagText->SetForegroundColour(palette.bannerText);

    if (m_langPanel) m_langPanel->SetBackgroundColour(palette.cardBg);

    if (m_langSelector) m_langSelector->UpdateTheme();
    if (m_sourceCard) m_sourceCard->UpdateTheme();
    if (m_targetCard) m_targetCard->UpdateTheme();

    if (m_instantTransBtn) m_instantTransBtn->Refresh();
    if (m_translateBtn) m_translateBtn->Refresh();
    if (m_stopBtn) m_stopBtn->Refresh();
    if (m_clearBtn) m_clearBtn->Refresh();
    if (m_swapBtn) m_swapBtn->Refresh();
    if (m_copyBtn) m_copyBtn->Refresh();

    Refresh();
}

void TextTranslationView::OnTranslateClicked(wxCommandEvent& WXUNUSED(event)) {
    Application::DTO::TranslationRequestDto req;
    req.text = m_sourceCard->GetTextCtrl()->GetValue().ToUTF8().data();
    req.sourceLanguage = m_langSelector->GetSourceLanguage();
    req.targetLanguage = m_langSelector->GetTargetLanguage();

    if (req.text.empty()) {
        m_targetCard->GetTextCtrl()->Clear();
        m_targetCard->SetCharacterCount(0);
        return;
    }

    // 1. 清空译文框准备打字机流式追加
    m_targetCard->GetTextCtrl()->Clear();
    m_targetCard->SetCharacterCount(0);

    // 2. 切换 UI 状态：显示红色的 [⏹ 中断翻译] 按钮，禁用 [▶ 翻译] 按钮
    m_stopBtn->Show();
    m_translateBtn->Disable();
    Layout();

    // 3. 发起异步流式翻译
    m_translationService->ExecuteStreamTranslation(
        req,
        // Token 流式接收回调 (打字机效果)
        [this](const std::string& token) {
            wxString wToken = wxString::FromUTF8(token);
            m_targetCard->GetTextCtrl()->AppendText(wToken);

            wxString current = m_targetCard->GetTextCtrl()->GetValue();
            m_targetCard->SetCharacterCount(current.Length());
        },
        // 翻译完成/被中断回调
        [this](bool success, const std::string& fullText, const std::string& error) {
            m_stopBtn->Hide();
            m_translateBtn->Enable();
            Layout();

            if (success) {
                wxString wClean = wxString::FromUTF8(fullText);
                m_targetCard->GetTextCtrl()->SetValue(wClean);
                m_targetCard->SetCharacterCount(wClean.Length());
            } else if (!error.empty()) {
                if (error == "已取消") {
                    m_targetCard->GetTextCtrl()->AppendText(L"\n\n[⏹ 翻译已被用户中断]");
                } else {
                    m_targetCard->GetTextCtrl()->SetValue(wxString::FromUTF8("翻译出错: " + error));
                }
            }
        }
    );
}

void TextTranslationView::OnStopClicked(wxCommandEvent& WXUNUSED(event)) {
    m_translationService->CancelTranslation();
    m_stopBtn->Hide();
    m_translateBtn->Enable();
    Layout();
}

void TextTranslationView::OnClearClicked(wxCommandEvent& WXUNUSED(event)) {
    // 取消当前进行中的翻译
    m_translationService->CancelTranslation();
    m_stopBtn->Hide();
    m_translateBtn->Enable();
    Layout();

    m_sourceCard->GetTextCtrl()->Clear();
    m_targetCard->GetTextCtrl()->Clear();
    m_sourceCard->SetCharacterCount(0);
    m_targetCard->SetCharacterCount(0);
}

void TextTranslationView::OnCopyTargetClicked(wxCommandEvent& WXUNUSED(event)) {
    wxString text = m_targetCard->GetTextCtrl()->GetValue();
    if (text.IsEmpty()) return;

    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(text));
        wxTheClipboard->Close();
        wxMessageBox(L"译文已成功复制到剪贴板！", L"提示", wxOK | wxICON_INFORMATION, this);
    }
}

void TextTranslationView::OnSwapClicked(wxCommandEvent& WXUNUSED(event)) {
    if (m_langSelector) {
        m_langSelector->SwapLanguages();
    }

    wxString srcText = m_sourceCard->GetTextCtrl()->GetValue();
    wxString targetText = m_targetCard->GetTextCtrl()->GetValue();

    m_sourceCard->GetTextCtrl()->SetValue(targetText);
    m_targetCard->GetTextCtrl()->SetValue(srcText);

    m_sourceCard->SetCharacterCount(targetText.Length());
    m_targetCard->SetCharacterCount(srcText.Length());
}

void TextTranslationView::OnSourceTextChanged(wxCommandEvent& WXUNUSED(event)) {
    wxString text = m_sourceCard->GetTextCtrl()->GetValue();
    m_sourceCard->SetCharacterCount(text.Length());
}

} // namespace LinguaAlpaca::Presentation::Views
