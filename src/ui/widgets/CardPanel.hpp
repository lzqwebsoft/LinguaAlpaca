#pragma once
#include <wx/wx.h>
#include <vector>
#include <functional>
#include "../theme/Theme.hpp"
#include "TextCtrl.hpp"
#include "MarkdownView.hpp"

namespace LinguaAlpaca::UI {

enum class CardViewMode {
    Rendered,
    Source,
    Text
};

struct CardToolIcon {
    int id;
    const char* svgContent;
    wxString tooltip;
    std::function<void()> onClick;
};

class CardPanel : public wxPanel {
public:
    CardPanel(wxWindow* parent, const wxString& title, bool isActiveBorder = false, bool enableMarkdown = false, wxWindowID id = wxID_ANY);

    void AddToolIcon(int id, const char* svgContent, const wxString& tooltip, std::function<void()> onClick);
    void SetCharacterCount(size_t count);
    void UpdateTheme();

    TextCtrl* GetTextCtrl() const;
    MarkdownView* GetMarkdownView() const { return m_markdownView; }
    wxString GetRenderedHtml() const {
        if (m_markdownView) {
            return m_markdownView->GetRenderedHtml();
        }
        return wxString();
    }

    void SetContent(const std::string& text, bool preserveScroll = false);
    void SetMarkdown(const std::string& markdown, const std::string& baseDir = "", bool preserveScroll = false);
    void SetMarkdown(const wxString& markdown, const wxString& baseDir = wxEmptyString, bool preserveScroll = false);
    void SetViewMode(CardViewMode mode);
    CardViewMode GetViewMode() const { return m_currentMode; }
    void Clear();

private:
    void InitUI();
    void OnPaint(wxPaintEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);

    wxString m_title;
    bool m_isActiveBorder;
    bool m_isMarkdownEnabled{false};
    size_t m_charCount{0};

    TextCtrl* m_textCtrl{nullptr};
    MarkdownView* m_markdownView{nullptr};
    wxBoxSizer* m_contentContainerSizer{nullptr};

    CardViewMode m_currentMode{CardViewMode::Rendered};

    std::vector<CardToolIcon> m_tools;
    int m_hoverToolIndex{-1};

    // 字体缓存
    wxFont m_titleFont;
    wxFont m_tabFont;
    wxFont m_countFont;

    // 顶部 Markdown 视图切换 Tab 区域
    wxRect m_renderedTabRect;
    wxRect m_sourceTabRect;
    int m_hoverTab{-1}; // 0: Rendered, 1: Source
};

} // namespace LinguaAlpaca::UI
