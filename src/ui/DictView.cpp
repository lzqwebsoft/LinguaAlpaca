#pragma execution_character_set("utf-8")
#include "DictView.hpp"
#include "MainFrame.hpp"
#include "theme/IconManager.hpp"
#include "theme/Theme.hpp"
#include "core/WinTtsHelper.hpp"
#include "core/ClipboardHelper.hpp"

#include <wx/clipbrd.h>

namespace LinguaAlpaca::UI {

DictView::DictView(wxWindow* parent,
                   std::shared_ptr<ModelManager> modelManager,
                   wxWindowID id)
    : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
      m_modelManager(std::move(modelManager)) {
    
    if (m_modelManager) {
        m_dictEngine = m_modelManager->GetDictEngine();
    }
    
    m_suggestTimer.SetOwner(this);
    Bind(wxEVT_TIMER, &DictView::OnSuggestTimer, this, m_suggestTimer.GetId());

    InitUI();
    RefreshDictList();

    ThemeManager::GetInstance().RegisterCallback([this](ThemeMode) {
        UpdateTheme();
    });
}

void DictView::InitUI() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    m_mainSizer = new wxBoxSizer(wxVERTICAL);

    // ==========================================
    // 1. 顶部 Header 与 检索控制栏
    // ==========================================
    m_headerPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_headerPanel->SetBackgroundColour(palette.windowBg);

    wxBoxSizer* headerTopSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBitmapBundle titleBundle = IconManager::GetIconBundle(
        SVG::DICTIONARY, dip(24, 24), palette.accentPrimary);
    wxStaticBitmap* titleIcon = new wxStaticBitmap(m_headerPanel, wxID_ANY, titleBundle);

    m_titleText = new wxStaticText(m_headerPanel, wxID_ANY, L"词典查询");
    m_titleText->SetFont(wxFont(18, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL,
                                wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_titleText->SetForegroundColour(palette.textPrimary);

    m_dictCountBadge = new StatusBadge(m_headerPanel);

    headerTopSizer->Add(titleIcon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);
    headerTopSizer->Add(m_titleText, 0, wxALIGN_CENTER_VERTICAL);
    headerTopSizer->AddStretchSpacer(1);
    headerTopSizer->Add(m_dictCountBadge, 0, wxALIGN_CENTER_VERTICAL);

    // 搜索输入行
    wxBoxSizer* searchRowSizer = new wxBoxSizer(wxHORIZONTAL);

    // 词典切换下拉框
    m_dictChoice = new wxChoice(m_headerPanel, wxID_ANY, wxDefaultPosition, dip(180, -1));
    m_dictChoice->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_dictChoice->Append(L"全部已加载词典");
    m_dictChoice->SetSelection(0);
    m_dictChoice->Bind(wxEVT_CHOICE, &DictView::OnDictChoiceSelected, this);

    // 现代圆角搜索输入框（内嵌搜索图标与实时清除 x 按钮）
    m_searchBox = new SearchInputBox(m_headerPanel, wxID_ANY, L"",
                                     L"输入要查询的单词或短语，按回车检索...",
                                     wxDefaultPosition, wxSize(-1, 38_dip));
    m_searchBox->Bind(wxEVT_TEXT, &DictView::OnSearchTextChanged, this);
    m_searchBox->Bind(wxEVT_TEXT_ENTER, &DictView::OnSearchTextEnter, this);
    m_searchBox->SetOnClearCallback([this]() {
        wxCommandEvent dummy;
        OnClearClicked(dummy);
    });

    // 检索按钮
    m_searchBtn = new CustomButton(m_headerPanel, wxID_ANY, L"查询", ButtonStyle::Primary,
                                   wxDefaultPosition, dip(90, 38));
    m_searchBtn->SetIcon(SVG::BROWSE, dip(16, 16), *wxWHITE);
    m_searchBtn->Bind(wxEVT_BUTTON, &DictView::OnSearchClicked, this);

    searchRowSizer->Add(m_dictChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);
    searchRowSizer->Add(m_searchBox, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10_dip);
    searchRowSizer->Add(m_searchBtn, 0, wxALIGN_CENTER_VERTICAL);

    wxBoxSizer* headerPanelSizer = new wxBoxSizer(wxVERTICAL);
    headerPanelSizer->Add(headerTopSizer, 0, wxEXPAND | wxBOTTOM, 14_dip);
    headerPanelSizer->Add(searchRowSizer, 0, wxEXPAND);
    m_headerPanel->SetSizer(headerPanelSizer);

    m_mainSizer->Add(m_headerPanel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 20_dip);

    // ==========================================
    // 2. 主内容区 (分栏容器)
    // ==========================================
    m_mainContentPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_mainContentPanel->SetBackgroundColour(palette.windowBg);

    wxBoxSizer* contentRowSizer = new wxBoxSizer(wxHORIZONTAL);

    // 2.1 左侧候选联想词栏 (30% 弹性权重) - 圆角边框与定制滑动条
    m_leftSuggestCard = new wxPanel(m_mainContentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_leftSuggestCard->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_leftSuggestCard->Bind(wxEVT_PAINT, [this](wxPaintEvent& WXUNUSED(event)) {
        wxAutoBufferedPaintDC dc(m_leftSuggestCard);
        wxSize size = m_leftSuggestCard->GetClientSize();
        if (size.x <= 0 || size.y <= 0) return;
        auto palette = ThemeColors::GetCurrentPalette();
        dc.SetBackground(wxBrush(palette.windowBg));
        dc.Clear();
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (gc) {
            gc->SetBrush(gc->CreateBrush(wxBrush(palette.cardBg)));
            gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 1.0)));
            gc->DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, 12.0_dip);
        }
    });

    wxBoxSizer* leftCardSizer = new wxBoxSizer(wxVERTICAL);

    // 建议栏顶部铺满标题栏 (左右对称铺满)
    wxPanel* suggestHeaderBar = new wxPanel(m_leftSuggestCard, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    suggestHeaderBar->SetBackgroundColour(palette.cardBg);

    wxBoxSizer* suggestHeaderSizer = new wxBoxSizer(wxHORIZONTAL);
    m_suggestTitle = new wxStaticText(suggestHeaderBar, wxID_ANY, L"联想词列表");
    m_suggestTitle->SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_suggestTitle->SetForegroundColour(palette.textPrimary);
    suggestHeaderSizer->Add(m_suggestTitle, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16_dip);
    suggestHeaderBar->SetSizer(suggestHeaderSizer);

    m_suggestListBox = new SuggestListBox(m_leftSuggestCard, wxID_ANY);
    m_suggestListBox->Bind(wxEVT_LISTBOX, &DictView::OnSuggestSelected, this);

    leftCardSizer->Add(suggestHeaderBar, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 14_dip);
    leftCardSizer->Add(m_suggestListBox, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6_dip);
    m_leftSuggestCard->SetSizer(leftCardSizer);

    // 2.2 右侧释义卡片 (70% 弹性权重) - 圆角边框卡片
    m_rightResultCard = new wxPanel(m_mainContentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_rightResultCard->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_rightResultCard->Bind(wxEVT_PAINT, [this](wxPaintEvent& WXUNUSED(event)) {
        wxAutoBufferedPaintDC dc(m_rightResultCard);
        wxSize size = m_rightResultCard->GetClientSize();
        if (size.x <= 0 || size.y <= 0) return;
        auto palette = ThemeColors::GetCurrentPalette();
        dc.SetBackground(wxBrush(palette.windowBg));
        dc.Clear();
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (gc) {
            gc->SetBrush(gc->CreateBrush(wxBrush(palette.cardBg)));
            gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 1.0)));
            gc->DrawRoundedRectangle(1, 1, size.x - 2, size.y - 2, 12.0_dip);
        }
    });

    wxBoxSizer* rightCardSizer = new wxBoxSizer(wxVERTICAL);

    // 单词标题与操作栏
    m_wordHeaderBar = new wxPanel(m_rightResultCard, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_wordHeaderBar->SetBackgroundColour(palette.cardBg);

    wxBoxSizer* wordHeaderSizer = new wxBoxSizer(wxHORIZONTAL);
    m_headwordText = new wxStaticText(m_wordHeaderBar, wxID_ANY, L"");
    m_headwordText->SetFont(wxFont(16, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_headwordText->SetForegroundColour(palette.textPrimary);

    m_phoneticText = new wxStaticText(m_wordHeaderBar, wxID_ANY, L"");
    m_phoneticText->SetFont(wxFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL, false, "Lucida Sans Unicode"));
    m_phoneticText->SetForegroundColour(palette.accentPrimary);

    m_speakBtn = new CustomButton(m_wordHeaderBar, wxID_ANY, L"发音", ButtonStyle::Secondary,
                                  wxDefaultPosition, dip(74, 30));
    m_speakBtn->SetIcon(SVG::SPEAKER, dip(14, 14));
    m_speakBtn->Bind(wxEVT_BUTTON, &DictView::OnSpeakClicked, this);
    m_speakBtn->Hide();

    m_copyBtn = new CustomButton(m_wordHeaderBar, wxID_ANY, L"复制释义", ButtonStyle::Secondary,
                                 wxDefaultPosition, dip(96, 30));
    m_copyBtn->SetIcon(SVG::COPY, dip(14, 14));
    m_copyBtn->Bind(wxEVT_BUTTON, &DictView::OnCopyClicked, this);
    m_copyBtn->Hide();

    wordHeaderSizer->Add(m_headwordText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12_dip);
    wordHeaderSizer->Add(m_phoneticText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12_dip);
    wordHeaderSizer->Add(m_speakBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8_dip);
    wordHeaderSizer->AddStretchSpacer(1);
    wordHeaderSizer->Add(m_copyBtn, 0, wxALIGN_CENTER_VERTICAL);
    m_wordHeaderBar->SetSizer(wordHeaderSizer);

    // 释义内容文本展示框
    m_definitionCtrl = new TextCtrl(m_rightResultCard, wxID_ANY, L"", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
    m_definitionCtrl->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_definitionCtrl->SetBackgroundColour(palette.cardBg);
    m_definitionCtrl->SetForegroundColour(palette.textPrimary);

    rightCardSizer->Add(m_wordHeaderBar, 0, wxEXPAND | wxALL, 16_dip);
    rightCardSizer->Add(m_definitionCtrl, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16_dip);
    m_rightResultCard->SetSizer(rightCardSizer);

    // 2.3 空状态/引导面板
    m_emptyStateCard = new wxPanel(m_mainContentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_emptyStateCard->SetBackgroundColour(palette.cardBg);

    wxBoxSizer* emptySizer = new wxBoxSizer(wxVERTICAL);
    wxBitmapBundle emptyBundle = IconManager::GetIconBundle(
        SVG::DICTIONARY, dip(48, 48), palette.textSecondary);
    m_emptyIcon = new wxStaticBitmap(m_emptyStateCard, wxID_ANY, emptyBundle);

    m_emptyTitle = new wxStaticText(m_emptyStateCard, wxID_ANY, L"欢迎使用 StarDict 本地词典");
    m_emptyTitle->SetFont(wxFont(14, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    m_emptyTitle->SetForegroundColour(palette.textPrimary);

    m_emptyDesc = new wxStaticText(m_emptyStateCard, wxID_ANY,
        L"在上方输入框中输入英文或中文单词，即可快速查询本地 StarDict 词典的详细释义与音标。\n"
        L"如未加载词典，请前往【设置 -> 词典设置】指定 StarDict 词典目录。");
    m_emptyDesc->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_emptyDesc->SetForegroundColour(palette.textSecondary);

    m_goToSettingsBtn = new CustomButton(m_emptyStateCard, wxID_ANY, L"前往词典设置", ButtonStyle::Primary,
                                         wxDefaultPosition, dip(160, 38));
    m_goToSettingsBtn->SetIcon(SVG::SETTINGS, dip(16, 16), *wxWHITE);
    m_goToSettingsBtn->Bind(wxEVT_BUTTON, &DictView::OnGoToSettingsClicked, this);

    emptySizer->AddStretchSpacer(1);
    emptySizer->Add(m_emptyIcon, 0, wxALIGN_CENTER | wxBOTTOM, 16_dip);
    emptySizer->Add(m_emptyTitle, 0, wxALIGN_CENTER | wxBOTTOM, 10_dip);
    emptySizer->Add(m_emptyDesc, 0, wxALIGN_CENTER | wxBOTTOM, 20_dip);
    emptySizer->Add(m_goToSettingsBtn, 0, wxALIGN_CENTER);
    emptySizer->AddStretchSpacer(1);
    m_emptyStateCard->SetSizer(emptySizer);

    contentRowSizer->Add(m_leftSuggestCard, 30, wxEXPAND | wxRIGHT, 12_dip);
    contentRowSizer->Add(m_rightResultCard, 70, wxEXPAND);
    contentRowSizer->Add(m_emptyStateCard, 100, wxEXPAND);
    m_emptyStateCard->Hide();

    m_mainContentPanel->SetSizer(contentRowSizer);
    m_mainSizer->Add(m_mainContentPanel, 1, wxEXPAND | wxALL, 20_dip);

    SetSizer(m_mainSizer);
    Layout();
}

void DictView::RefreshDictList() {
    if (!m_dictEngine) return;

    m_cachedDictInfos = m_dictEngine->GetLoadedDictionaries();
    size_t count = m_cachedDictInfos.size();
    size_t totalWords = m_dictEngine->GetTotalWordCount();

    if (count == 0) {
        m_dictCountBadge->SetStatus(ServerHealthState::Unconfigured, "未加载词典");
    } else {
        std::string badgeText = std::to_string(count) + " 本词典 (" + std::to_string(totalWords) + " 词)";
        m_dictCountBadge->SetStatus(ServerHealthState::Ready, badgeText);
    }

    // 刷新下拉框选项
    m_dictChoice->Clear();
    m_dictChoice->Append(L"全部已加载词典");
    for (const auto& dict : m_cachedDictInfos) {
        wxString name = wxString::FromUTF8(dict.bookName);
        if (dict.wordCount > 0) {
            name += wxString::Format(L" (%u 词)", dict.wordCount);
        }
        m_dictChoice->Append(name);
    }
    m_dictChoice->SetSelection(0);
    m_currentSelectedDictId.clear();

    UpdateEmptyStateView(count > 0, !m_currentWord.empty(), m_definitionCtrl && !m_definitionCtrl->GetValue().empty());
}

void DictView::SearchWord(const wxString& word) {
    if (m_searchBox) {
        m_searchBox->SetValue(word);
    }
    DoSearch(word.ToUTF8().data());
}

void DictView::OnSearchClicked(wxCommandEvent& WXUNUSED(event)) {
    if (!m_searchBox) return;
    wxString word = m_searchBox->GetValue().Trim(true).Trim(false);
    if (!word.IsEmpty()) {
        DoSearch(word.ToUTF8().data());
    }
}

void DictView::OnSearchTextEnter(wxCommandEvent& WXUNUSED(event)) {
    if (!m_searchBox) return;
    wxString word = m_searchBox->GetValue().Trim(true).Trim(false);
    if (!word.IsEmpty()) {
        DoSearch(word.ToUTF8().data());
    }
}

void DictView::OnSearchTextChanged(wxCommandEvent& WXUNUSED(event)) {
    if (!m_searchBox) return;
    // 延迟 200ms 触发联想词检索，避免按键频繁二分
    m_suggestTimer.StartOnce(200);
}

void DictView::OnSuggestTimer(wxTimerEvent& WXUNUSED(event)) {
    if (!m_searchBox || !m_dictEngine || !m_suggestListBox) return;

    std::string prefix = m_searchBox->GetValue().Trim(true).Trim(false).ToUTF8().data();
    if (prefix.empty()) {
        m_suggestListBox->Clear();
        return;
    }

    auto suggestions = m_dictEngine->GetSuggestions(prefix, 40, m_currentSelectedDictId);
    m_suggestListBox->SetItems(suggestions);
}

void DictView::OnSuggestSelected(wxCommandEvent& event) {
    int sel = event.GetSelection();
    if (sel != wxNOT_FOUND && m_suggestListBox) {
        wxString word = m_suggestListBox->GetString(sel);
        if (m_searchBox) {
            m_searchBox->ChangeValue(word);
        }
        DoSearch(word.ToUTF8().data());
    }
}

void DictView::OnDictChoiceSelected(wxCommandEvent& event) {
    int sel = event.GetSelection();
    if (sel <= 0) {
        m_currentSelectedDictId.clear();
    } else {
        size_t idx = static_cast<size_t>(sel - 1);
        if (idx < m_cachedDictInfos.size()) {
            m_currentSelectedDictId = m_cachedDictInfos[idx].id;
        }
    }

    if (!m_currentWord.empty()) {
        DoSearch(m_currentWord);
    }
}

void DictView::OnClearClicked(wxCommandEvent& WXUNUSED(event)) {
    if (m_searchBox) m_searchBox->Clear();
    if (m_suggestListBox) m_suggestListBox->Clear();
    if (m_definitionCtrl) m_definitionCtrl->Clear();
    if (m_headwordText) m_headwordText->SetLabel(L"");
    if (m_phoneticText) m_phoneticText->SetLabel(L"");
    if (m_speakBtn) m_speakBtn->Hide();
    if (m_copyBtn) m_copyBtn->Hide();
    m_currentWord.clear();

    UpdateEmptyStateView(m_dictEngine && m_dictEngine->HasDictionaries(), false, false);
}

void DictView::OnSpeakClicked(wxCommandEvent& WXUNUSED(event)) {
    if (!m_currentWord.empty()) {
        WinTtsHelper::GetInstance().Speak(m_currentWord);
    }
}

void DictView::OnCopyClicked(wxCommandEvent& WXUNUSED(event)) {
    if (m_definitionCtrl) {
        wxString content = m_headwordText->GetLabel();
        if (!m_phoneticText->GetLabel().IsEmpty()) {
            content += " " + m_phoneticText->GetLabel();
        }
        content += "\n\n" + m_definitionCtrl->GetValue();
        ClipboardHelper::SetClipboardText(content.ToUTF8().data());
    }
}

void DictView::OnGoToSettingsClicked(wxCommandEvent& WXUNUSED(event)) {
    wxWindow* p = GetParent();
    while (p) {
        MainFrame* frame = dynamic_cast<MainFrame*>(p);
        if (frame) {
            frame->NavigateToSettings();
            break;
        }
        p = p->GetParent();
    }
}

void DictView::DoSearch(const std::string& word) {
    if (!m_dictEngine || word.empty()) return;
    m_currentWord = word;

    auto results = m_dictEngine->Lookup(word, m_currentSelectedDictId);

    if (results.empty()) {
        m_headwordText->SetLabel(wxString::FromUTF8(word));
        m_phoneticText->SetLabel(L"");
        m_speakBtn->Hide();
        m_copyBtn->Hide();
        m_definitionCtrl->SetValue(L"未在已加载词典中检索到该单词的释义。\n\n提示：\n1. 请检查拼写是否正确\n2. 可尝试在【设置 -> 词典设置】中添加更多 StarDict 词典文件。");
        UpdateEmptyStateView(m_dictEngine->HasDictionaries(), true, false);
        return;
    }

    // 提取第一个有效的音标
    std::string phonetic;
    for (const auto& r : results) {
        if (!r.phonetic.empty()) {
            phonetic = r.phonetic;
            break;
        }
    }

    m_headwordText->SetLabel(wxString::FromUTF8(results.front().word));
    if (!phonetic.empty()) {
        m_phoneticText->SetLabel(wxString::FromUTF8(phonetic));
    } else {
        m_phoneticText->SetLabel(L"");
    }

    m_speakBtn->Show();
    m_copyBtn->Show();

    // 聚合多词典释义文本
    wxString combinedDef;
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        if (i > 0) {
            combinedDef += L"\n\n═══════════════════════════════════════════\n\n";
        }
        combinedDef += wxString::Format(L"【 %s 】\n", wxString::FromUTF8(r.dictName));
        combinedDef += wxString::FromUTF8(r.definition);
    }

    m_definitionCtrl->SetValue(combinedDef);
    m_wordHeaderBar->Layout();
    m_rightResultCard->Layout();

    UpdateEmptyStateView(true, true, true);
}

void DictView::UpdateEmptyStateView(bool hasDictionaries, bool hasSearched, bool hasResults) {
    if (!hasDictionaries) {
        m_leftSuggestCard->Hide();
        m_rightResultCard->Hide();
        m_emptyStateCard->Show();
        m_emptyTitle->SetLabel(L"未检测到本地 StarDict 词典");
        m_emptyDesc->SetLabel(
            L"系统尚未在词典目录中检测到任何 StarDict 格式词典（.ifo / .idx / .dict 或 .dict.dz）。\n"
            L"请点击下方按钮前往【设置 -> 词典设置】指定包含词典的目录。");
        m_goToSettingsBtn->Show();
    } else if (!hasSearched) {
        m_leftSuggestCard->Show();
        m_rightResultCard->Show();
        m_emptyStateCard->Hide();
        m_headwordText->SetLabel(L"输入单词开始查询");
        m_phoneticText->SetLabel(L"");
        m_speakBtn->Hide();
        m_copyBtn->Hide();
        m_definitionCtrl->SetValue(L"在上方搜索栏输入英文或中文，支持即时联想候选词。\n点击左侧候选词列表可直接查看详细释义。");
    } else {
        m_leftSuggestCard->Show();
        m_rightResultCard->Show();
        m_emptyStateCard->Hide();
    }
    m_mainContentPanel->Layout();
}

void DictView::UpdateTheme() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.windowBg);

    if (m_headerPanel) m_headerPanel->SetBackgroundColour(palette.windowBg);
    if (m_titleText) m_titleText->SetForegroundColour(palette.textPrimary);
    if (m_searchBox) {
        m_searchBox->UpdateTheme();
    }
    if (m_mainContentPanel) m_mainContentPanel->SetBackgroundColour(palette.windowBg);

    if (m_leftSuggestCard) m_leftSuggestCard->SetBackgroundColour(palette.cardBg);
    if (m_suggestTitle) m_suggestTitle->SetForegroundColour(palette.textSecondary);
    if (m_suggestListBox) {
        m_suggestListBox->UpdateTheme();
    }

    if (m_rightResultCard) m_rightResultCard->SetBackgroundColour(palette.cardBg);
    if (m_wordHeaderBar) m_wordHeaderBar->SetBackgroundColour(palette.cardBg);
    if (m_headwordText) m_headwordText->SetForegroundColour(palette.textPrimary);
    if (m_phoneticText) m_phoneticText->SetForegroundColour(palette.accentPrimary);
    if (m_definitionCtrl) {
        m_definitionCtrl->SetBackgroundColour(palette.cardBg);
        m_definitionCtrl->SetForegroundColour(palette.textPrimary);
    }

    if (m_emptyStateCard) m_emptyStateCard->SetBackgroundColour(palette.cardBg);
    if (m_emptyTitle) m_emptyTitle->SetForegroundColour(palette.textPrimary);
    if (m_emptyDesc) m_emptyDesc->SetForegroundColour(palette.textSecondary);

    Refresh();
}

} // namespace LinguaAlpaca::UI
