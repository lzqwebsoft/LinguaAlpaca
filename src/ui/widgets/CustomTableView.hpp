#pragma once
#include <wx/wx.h>
#include <vector>
#include <string>
#include "core/table/TableParser.hpp"
#include "../theme/Theme.hpp"
#include "ScrollBar.hpp"

namespace LinguaAlpaca::UI {

/**
 * @brief 现代化自适应高颜值表格渲染控件 (集成自定义细条滑动条与高性能布局缓存)
 */
class CustomTableView : public wxPanel {
public:
    CustomTableView(wxWindow* parent,
                    wxWindowID id = wxID_ANY,
                    const wxPoint& pos = wxDefaultPosition,
                    const wxSize& size = wxDefaultSize);

    void SetTableData(const TableData& data);
    const TableData& GetTableData() const { return m_data; }

    void Clear();
    void UpdateTheme();

    void CopyAsExcel();
    void CopyAsMarkdown();
    void CopyAsCsv();
    void ExportCsvDialog();

    void ScrollToRow(int row);

private:
    void InitUI();
    void RecalculateLayout();
    void UpdateScrollParams();

    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnRightDown(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void ShowContextMenu(const wxPoint& pos);

    TableData m_data;
    std::vector<int> m_baseColWidths; // 缓存的基础测量列宽，避免窗口缩放时重复 O(R*C) DC 文本测量
    std::vector<int> m_colWidths;     // 动态拉伸适配客户区后的显示列宽
    bool m_needMeasureColWidths{true};

    int m_totalTableWidth{0};
    int m_firstVisibleRow{0};

    int m_hoveredRow{-1};
    int m_selectedRow{-1};
    int m_selectedCol{-1};

    int m_headerHeight{34};
    int m_rowHeight{32};

    wxFont m_headerFont;
    wxFont m_rowFont;
    wxFont m_hintFont;

    wxPanel* m_canvas{nullptr};
    ScrollBar* m_scrollBar{nullptr};
};

} // namespace LinguaAlpaca::UI
