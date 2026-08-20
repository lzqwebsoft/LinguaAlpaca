#pragma once
#include <wx/wx.h>
#include <wx/timer.h>
#include <memory>
#include <vector>
#include <string>

#include "core/ModelManager.hpp"
#include "core/DictEngine.hpp"
#include "AsyncTrackable.hpp"
#include "widgets/CustomButton.hpp"
#include "widgets/TextCtrl.hpp"
#include "widgets/StatusBadge.hpp"
#include "widgets/SearchInputBox.hpp"
#include "widgets/SuggestListBox.hpp"

namespace LinguaAlpaca::UI {

class DictView : public wxPanel, public AsyncTrackable {
public:
    DictView(wxWindow* parent,
             std::shared_ptr<ModelManager> modelManager,
             wxWindowID id = wxID_ANY);
    ~DictView() override = default;

    void UpdateTheme();
    void RefreshDictList();
    void SearchWord(const wxString& word);

private:
    void InitUI();
    void OnSearchClicked(wxCommandEvent& event);
    void OnSearchTextEnter(wxCommandEvent& event);
    void OnSearchTextChanged(wxCommandEvent& event);
    void OnSuggestTimer(wxTimerEvent& event);
    void OnSuggestSelected(wxCommandEvent& event);
    void OnDictChoiceSelected(wxCommandEvent& event);
    void OnClearClicked(wxCommandEvent& event);
    void OnSpeakClicked(wxCommandEvent& event);
    void OnCopyClicked(wxCommandEvent& event);
    void OnGoToSettingsClicked(wxCommandEvent& event);

    void DoSearch(const std::string& word);
    void RenderRichDictionaryResults(const std::vector<DictSearchResult>& results);
    void UpdateEmptyStateView(bool hasDictionaries, bool hasSearched, bool hasResults);

    std::shared_ptr<ModelManager> m_modelManager;
    std::shared_ptr<DictEngine> m_dictEngine;
    wxTimer m_suggestTimer;

    // UI - 顶部检索工具栏
    wxPanel* m_headerPanel{nullptr};
    wxStaticText* m_titleText{nullptr};
    StatusBadge* m_dictCountBadge{nullptr};
    wxChoice* m_dictChoice{nullptr};

    SearchInputBox* m_searchBox{nullptr};
    CustomButton* m_searchBtn{nullptr};

    // UI - 主分栏容器
    wxPanel* m_mainContentPanel{nullptr};
    wxBoxSizer* m_mainSizer{nullptr};

    // 左侧候选联想词栏
    wxPanel* m_leftSuggestCard{nullptr};
    wxStaticText* m_suggestTitle{nullptr};
    SuggestListBox* m_suggestListBox{nullptr};

    // 右侧释义卡片
    wxPanel* m_rightResultCard{nullptr};
    wxPanel* m_wordHeaderBar{nullptr};
    wxStaticText* m_headwordText{nullptr};
    wxStaticText* m_phoneticText{nullptr};
    CustomButton* m_speakBtn{nullptr};
    CustomButton* m_copyBtn{nullptr};
    TextCtrl* m_definitionCtrl{nullptr};

    // 空状态/引导面板
    wxPanel* m_emptyStateCard{nullptr};
    wxStaticBitmap* m_emptyIcon{nullptr};
    wxStaticText* m_emptyTitle{nullptr};
    wxStaticText* m_emptyDesc{nullptr};
    CustomButton* m_goToSettingsBtn{nullptr};

    std::string m_currentWord;
    std::string m_currentSelectedDictId;
    std::vector<DictInfo> m_cachedDictInfos;
    std::vector<DictSearchResult> m_lastSearchResults;
};

} // namespace LinguaAlpaca::UI
