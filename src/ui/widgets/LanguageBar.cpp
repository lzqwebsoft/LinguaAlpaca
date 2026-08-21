#include "LanguageBar.hpp"
#include "../theme/Theme.hpp"
#include "../theme/IconManager.hpp"

namespace LinguaAlpaca::UI {

wxDEFINE_EVENT(EVT_LANGUAGE_CHANGED, wxCommandEvent);

LanguageBar::LanguageBar(wxWindow* parent, wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE) {
    InitUI();
}

void LanguageBar::InitUI() {
    wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);
    auto palette = ThemeColors::GetCurrentPalette();

    SetBackgroundColour(palette.cardBg);

    // 源语言标签与下拉框
    m_srcLabel = new wxStaticText(this, wxID_ANY, L"源语言");
    m_srcLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_srcLabel->SetForegroundColour(palette.textSecondary);

    m_sourceChoice = new CustomChoice(this, wxID_ANY, wxDefaultPosition, dip(120, 32));
    
    // 目标语言标签与下拉框
    m_targetLabel = new wxStaticText(this, wxID_ANY, L"目标语言");
    m_targetLabel->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_targetLabel->SetForegroundColour(palette.textSecondary);

    m_targetChoice = new CustomChoice(this, wxID_ANY, wxDefaultPosition, dip(120, 32));

    // 填充语言列表
    const auto& languages = LanguageHelper::GetSupportedLanguages();
    for (const auto& lang : languages) {
        wxString name = wxString::FromUTF8(lang.displayName);
        m_sourceChoice->Append(name);
        if (lang.code != LanguageCode::AutoDetect) {
            m_targetChoice->Append(name);
        }
    }

    m_sourceChoice->SetStringSelection(L"英语");
    m_targetChoice->SetStringSelection(L"中文");

    // 交换按钮 (SVG Swap)
    wxBitmapBundle swapBundle = IconManager::GetIconBundle(SVG::SWAP, dip(16, 16), palette.textPrimary);
    m_swapBtn = new wxBitmapButton(this, wxID_ANY, swapBundle, wxDefaultPosition, dip(36, 30), wxBORDER_NONE);
    m_swapBtn->SetBackgroundColour(palette.windowBg);
    m_swapBtn->SetToolTip(L"互换源语言与目标语言");

    mainSizer->Add(m_srcLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 10_dip);
    mainSizer->Add(m_sourceChoice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16_dip);
    mainSizer->Add(m_swapBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16_dip);
    mainSizer->Add(m_targetLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);
    mainSizer->Add(m_targetChoice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);

    SetSizer(mainSizer);

    m_swapBtn->Bind(wxEVT_BUTTON, &LanguageBar::OnSwapClicked, this);

    m_sourceChoice->Bind(wxEVT_CHOICE, &LanguageBar::OnChoiceSelected, this);
    m_targetChoice->Bind(wxEVT_CHOICE, &LanguageBar::OnChoiceSelected, this);
}

void LanguageBar::UpdateTheme() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.cardBg);

    if (m_srcLabel) m_srcLabel->SetForegroundColour(palette.textSecondary);
    if (m_targetLabel) m_targetLabel->SetForegroundColour(palette.textSecondary);

    if (m_sourceChoice) {
        m_sourceChoice->UpdateTheme();
    }

    if (m_targetChoice) {
        m_targetChoice->UpdateTheme();
    }

    if (m_swapBtn) {
        wxBitmapBundle swapBundle = IconManager::GetIconBundle(SVG::SWAP, dip(16, 16), palette.textPrimary);
        m_swapBtn->SetBitmap(swapBundle);
        m_swapBtn->SetBackgroundColour(palette.windowBg);
        m_swapBtn->Refresh();
    }

    Refresh();
}

LanguageCode LanguageBar::GetSourceLanguage() const {
    return LanguageHelper::FromDisplayName(m_sourceChoice->GetStringSelection().ToUTF8().data());
}

LanguageCode LanguageBar::GetTargetLanguage() const {
    return LanguageHelper::FromDisplayName(m_targetChoice->GetStringSelection().ToUTF8().data());
}

void LanguageBar::SetSourceLanguage(LanguageCode code) {
    m_sourceChoice->SetStringSelection(wxString::FromUTF8(LanguageHelper::GetDisplayName(code)));
}

void LanguageBar::SetTargetLanguage(LanguageCode code) {
    m_targetChoice->SetStringSelection(wxString::FromUTF8(LanguageHelper::GetDisplayName(code)));
}

void LanguageBar::SwapLanguages() {
    auto srcCode = GetSourceLanguage();
    auto targetCode = GetTargetLanguage();

    if (srcCode != LanguageCode::AutoDetect) {
        SetSourceLanguage(targetCode);
        SetTargetLanguage(srcCode);

        wxCommandEvent evt(EVT_LANGUAGE_CHANGED, GetId());
        evt.SetEventObject(this);
        ProcessWindowEvent(evt);
    }
}

void LanguageBar::OnSwapClicked(wxCommandEvent& WXUNUSED(event)) {
    SwapLanguages();
}

void LanguageBar::OnChoiceSelected(wxCommandEvent& WXUNUSED(event)) {
    wxCommandEvent evt(EVT_LANGUAGE_CHANGED, GetId());
    evt.SetEventObject(this);
    ProcessWindowEvent(evt);
}

} // namespace LinguaAlpaca::UI
