#include "LanguageSelectorBar.hpp"
#include "../theme/ThemeColors.hpp"

namespace LinguaAlpaca::Presentation::Components {

wxDEFINE_EVENT(EVT_LANGUAGE_CHANGED, wxCommandEvent);

LanguageSelectorBar::LanguageSelectorBar(wxWindow* parent, wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE) {
    InitUI();
}

void LanguageSelectorBar::InitUI() {
    wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);
    auto palette = Theme::ThemeColors::GetCurrentPalette();

    SetBackgroundColour(palette.cardBg);

    // 源语言标签与下拉框
    m_srcLabel = new wxStaticText(this, wxID_ANY, L"➔ 源语言");
    m_srcLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_srcLabel->SetForegroundColour(palette.textSecondary);

    m_sourceChoice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(200, 36));
    
    // 目标语言标签与下拉框
    m_targetLabel = new wxStaticText(this, wxID_ANY, L"⬅ 目标语言");
    m_targetLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_targetLabel->SetForegroundColour(palette.textSecondary);

    m_targetChoice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(200, 36));

    // 填充语言列表
    const auto& languages = Domain::Model::LanguageHelper::GetSupportedLanguages();
    for (const auto& lang : languages) {
        wxString name = wxString::FromUTF8(lang.displayName);
        m_sourceChoice->Append(name);
        if (lang.code != Domain::Model::LanguageCode::AutoDetect) {
            m_targetChoice->Append(name);
        }
    }

    m_sourceChoice->SetStringSelection(L"英语");
    m_targetChoice->SetStringSelection(L"中文");

    m_sourceChoice->SetBackgroundColour(palette.cardBg);
    m_sourceChoice->SetForegroundColour(palette.textPrimary);
    m_targetChoice->SetBackgroundColour(palette.cardBg);
    m_targetChoice->SetForegroundColour(palette.textPrimary);

    // 交换按钮 (⇄)
    m_swapBtn = new wxButton(this, wxID_ANY, L"⇄", wxDefaultPosition, wxSize(42, 36), wxBORDER_NONE);
    m_swapBtn->SetFont(wxFont(14, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI Emoji"));
    m_swapBtn->SetBackgroundColour(palette.windowBg);
    m_swapBtn->SetForegroundColour(palette.textPrimary);

    mainSizer->Add(m_srcLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    mainSizer->Add(m_sourceChoice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);
    mainSizer->Add(m_swapBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);
    mainSizer->Add(m_targetLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    mainSizer->Add(m_targetChoice, 1, wxALIGN_CENTER_VERTICAL);

    SetSizer(mainSizer);

    m_swapBtn->Bind(wxEVT_BUTTON, &LanguageSelectorBar::OnSwapClicked, this);
    m_sourceChoice->Bind(wxEVT_CHOICE, &LanguageSelectorBar::OnChoiceSelected, this);
    m_targetChoice->Bind(wxEVT_CHOICE, &LanguageSelectorBar::OnChoiceSelected, this);
}

void LanguageSelectorBar::UpdateTheme() {
    auto palette = Theme::ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.cardBg);

    if (m_srcLabel) m_srcLabel->SetForegroundColour(palette.textSecondary);
    if (m_targetLabel) m_targetLabel->SetForegroundColour(palette.textSecondary);

    if (m_sourceChoice) {
        m_sourceChoice->SetBackgroundColour(palette.cardBg);
        m_sourceChoice->SetForegroundColour(palette.textPrimary);
        m_sourceChoice->Refresh();
    }

    if (m_targetChoice) {
        m_targetChoice->SetBackgroundColour(palette.cardBg);
        m_targetChoice->SetForegroundColour(palette.textPrimary);
        m_targetChoice->Refresh();
    }

    if (m_swapBtn) {
        m_swapBtn->SetBackgroundColour(palette.windowBg);
        m_swapBtn->SetForegroundColour(palette.textPrimary);
        m_swapBtn->Refresh();
    }

    Refresh();
}

Domain::Model::LanguageCode LanguageSelectorBar::GetSourceLanguage() const {
    return Domain::Model::LanguageHelper::FromDisplayName(m_sourceChoice->GetStringSelection().ToUTF8().data());
}

Domain::Model::LanguageCode LanguageSelectorBar::GetTargetLanguage() const {
    return Domain::Model::LanguageHelper::FromDisplayName(m_targetChoice->GetStringSelection().ToUTF8().data());
}

void LanguageSelectorBar::SetSourceLanguage(Domain::Model::LanguageCode code) {
    m_sourceChoice->SetStringSelection(wxString::FromUTF8(Domain::Model::LanguageHelper::GetDisplayName(code)));
}

void LanguageSelectorBar::SetTargetLanguage(Domain::Model::LanguageCode code) {
    m_targetChoice->SetStringSelection(wxString::FromUTF8(Domain::Model::LanguageHelper::GetDisplayName(code)));
}

void LanguageSelectorBar::SwapLanguages() {
    auto srcCode = GetSourceLanguage();
    auto targetCode = GetTargetLanguage();

    if (srcCode != Domain::Model::LanguageCode::AutoDetect) {
        SetSourceLanguage(targetCode);
        SetTargetLanguage(srcCode);

        wxCommandEvent evt(EVT_LANGUAGE_CHANGED, GetId());
        evt.SetEventObject(this);
        ProcessWindowEvent(evt);
    }
}

void LanguageSelectorBar::OnSwapClicked(wxCommandEvent& WXUNUSED(event)) {
    SwapLanguages();
}

void LanguageSelectorBar::OnChoiceSelected(wxCommandEvent& WXUNUSED(event)) {
    wxCommandEvent evt(EVT_LANGUAGE_CHANGED, GetId());
    evt.SetEventObject(this);
    ProcessWindowEvent(evt);
}

} // namespace LinguaAlpaca::Presentation::Components
