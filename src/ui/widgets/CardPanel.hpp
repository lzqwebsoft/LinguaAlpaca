#pragma once
#include <wx/wx.h>
#include <vector>
#include <functional>
#include "../theme/Theme.hpp"
#include "TextCtrl.hpp"
#include "CustomTableView.hpp"
#include "core/table/TableParser.hpp"

namespace LinguaAlpaca::UI {

enum class CardViewMode {
    Text,
    Table
};

struct CardToolIcon {
    int id;
    const char* svgContent;
    wxString tooltip;
    std::function<void()> onClick;
};

class CardPanel : public wxPanel {
public:
    CardPanel(wxWindow* parent, const wxString& title, bool isActiveBorder = false, wxWindowID id = wxID_ANY);

    void AddToolIcon(int id, const char* svgContent, const wxString& tooltip, std::function<void()> onClick);
    void SetCharacterCount(size_t count);
    void UpdateTheme();

    TextCtrl* GetTextCtrl() const { return m_textCtrl; }
    CustomTableView* GetTableView() const { return m_tableView; }

    void SetContent(const std::string& text);
    void SetTableData(const TableData& table);
    void SetViewMode(CardViewMode mode);
    CardViewMode GetViewMode() const { return m_currentMode; }
    bool HasTableData() const { return m_hasTableData; }
    const TableData& GetTableData() const { return m_cachedTableData; }
    void Clear();

private:
    void InitUI();
    void OnPaint(wxPaintEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);

    wxString m_title;
    bool m_isActiveBorder;
    size_t m_charCount{0};

    TextCtrl* m_textCtrl{nullptr};
    CustomTableView* m_tableView{nullptr};
    wxBoxSizer* m_contentContainerSizer{nullptr};

    CardViewMode m_currentMode{CardViewMode::Text};
    bool m_hasTableData{false};
    TableData m_cachedTableData;

    std::vector<CardToolIcon> m_tools;
    int m_hoverToolIndex{-1};

    // 字体缓存
    wxFont m_titleFont;
    wxFont m_tabFont;
    wxFont m_countFont;

    // 顶部视图切换 Tab 区域
    wxRect m_tableTabRect;
    wxRect m_textTabRect;
    int m_hoverTab{-1}; // 0: Table, 1: Text, -1: None
};

} // namespace LinguaAlpaca::UI
