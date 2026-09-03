#include "CustomTableView.hpp"
#include "core/ClipboardHelper.hpp"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/filedlg.h>
#include <wx/wfstream.h>
#include <numeric>

namespace LinguaAlpaca::UI {

CustomTableView::CustomTableView(wxWindow* parent, wxWindowID id,
                                 const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, id, pos, size, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE) {
    InitUI();
}

void CustomTableView::InitUI() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.cardBg);

    m_headerFont = wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei");
    m_rowFont = wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei");
    m_hintFont = wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei");

    m_canvas = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE);
    m_canvas->SetBackgroundStyle(wxBG_STYLE_PAINT);

    m_scrollBar = new ScrollBar(this, [this](int row) {
        ScrollToRow(row);
    });

    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_canvas, 1, wxEXPAND);
    sizer->Add(m_scrollBar, 0, wxEXPAND | wxRIGHT, 2_dip);
    SetSizer(sizer);

    m_canvas->Bind(wxEVT_PAINT, &CustomTableView::OnPaint, this);
    m_canvas->Bind(wxEVT_SIZE, &CustomTableView::OnSize, this);
    m_canvas->Bind(wxEVT_MOTION, &CustomTableView::OnMouseMove, this);
    m_canvas->Bind(wxEVT_LEAVE_WINDOW, &CustomTableView::OnMouseLeave, this);
    m_canvas->Bind(wxEVT_LEFT_DOWN, &CustomTableView::OnLeftDown, this);
    m_canvas->Bind(wxEVT_RIGHT_DOWN, &CustomTableView::OnRightDown, this);
    m_canvas->Bind(wxEVT_MOUSEWHEEL, &CustomTableView::OnMouseWheel, this);
    Bind(wxEVT_MOUSEWHEEL, &CustomTableView::OnMouseWheel, this);
}

void CustomTableView::SetTableData(const TableData& data) {
    m_data = data;
    m_firstVisibleRow = 0;
    m_hoveredRow = -1;
    m_selectedRow = -1;
    m_selectedCol = -1;
    m_needMeasureColWidths = true;
    RecalculateLayout();
    if (m_canvas) m_canvas->Refresh();
}

void CustomTableView::Clear() {
    m_data.Clear();
    m_baseColWidths.clear();
    m_colWidths.clear();
    m_firstVisibleRow = 0;
    m_hoveredRow = -1;
    m_selectedRow = -1;
    m_selectedCol = -1;
    m_needMeasureColWidths = true;
    UpdateScrollParams();
    if (m_canvas) m_canvas->Refresh();
}

void CustomTableView::UpdateTheme() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.cardBg);
    if (m_canvas) {
        m_canvas->SetBackgroundColour(palette.cardBg);
        m_canvas->Refresh();
    }
    if (m_scrollBar) {
        m_scrollBar->SetBackgroundColour(palette.cardBg);
        m_scrollBar->Refresh();
    }
    Refresh();
}

void CustomTableView::ScrollToRow(int row) {
    int totalRows = static_cast<int>(m_data.rows.size());
    if (totalRows == 0 || !m_canvas) return;

    int canvasH = m_canvas->GetClientSize().y;
    int visibleRows = std::max(1, (canvasH - m_headerHeight - 8_dip) / m_rowHeight);
    int maxFirst = std::max(0, totalRows - visibleRows);

    int target = std::clamp(row, 0, maxFirst);
    if (m_firstVisibleRow != target) {
        m_firstVisibleRow = target;
        m_canvas->Refresh();
    }
    UpdateScrollParams();
}

void CustomTableView::UpdateScrollParams() {
    if (!m_scrollBar || !m_canvas) return;
    int totalRows = static_cast<int>(m_data.rows.size());
    int canvasH = m_canvas->GetClientSize().y;
    int visibleRows = std::max(1, (canvasH - m_headerHeight - 8_dip) / m_rowHeight);
    m_scrollBar->SetScrollParams(m_firstVisibleRow, visibleRows, totalRows);
}

void CustomTableView::RecalculateLayout() {
    m_headerHeight = 34_dip;
    m_rowHeight = 32_dip;

    size_t colCount = m_data.ColCount();
    if (colCount == 0 || !m_canvas) {
        m_baseColWidths.clear();
        m_colWidths.clear();
        m_totalTableWidth = 0;
        UpdateScrollParams();
        return;
    }

    // 仅在数据变更时执行一次 O(R*C) 的 DC 文本测量，窗口拉伸缩放时直接复用缓存并按比例计算 (O(C))
    if (m_needMeasureColWidths) {
        wxClientDC dc(m_canvas);
        m_baseColWidths.assign(colCount, 80_dip);

        // 测量表头宽度
        dc.SetFont(m_headerFont);
        for (size_t c = 0; c < colCount; ++c) {
            if (c < m_data.headers.size()) {
                wxSize sz = dc.GetTextExtent(wxString::FromUTF8(m_data.headers[c]));
                m_baseColWidths[c] = std::max(m_baseColWidths[c], sz.x + 28_dip);
            }
        }

        // 测量行内容宽度 (若行数较多，采样检测前 150 行以确保极速响应)
        dc.SetFont(m_rowFont);
        size_t sampleRows = std::min(m_data.rows.size(), size_t(150));
        for (size_t r = 0; r < sampleRows; ++r) {
            const auto& row = m_data.rows[r];
            for (size_t c = 0; c < colCount; ++c) {
                if (c < row.size()) {
                    wxSize sz = dc.GetTextExtent(wxString::FromUTF8(row[c]));
                    m_baseColWidths[c] = std::max(m_baseColWidths[c], sz.x + 28_dip);
                }
            }
        }
        m_needMeasureColWidths = false;
    }

    m_colWidths = m_baseColWidths;
    int measuredWidth = std::accumulate(m_colWidths.begin(), m_colWidths.end(), 0);
    int clientWidth = m_canvas->GetClientSize().x;

    // 若表格总宽小于画布客户区宽度，则按比例撑满客户区 (100% 宽度响应)
    if (clientWidth > 0 && measuredWidth < clientWidth - 8_dip) {
        double ratio = static_cast<double>(clientWidth - 8_dip) / measuredWidth;
        for (auto& w : m_colWidths) {
            w = static_cast<int>(w * ratio);
        }
        measuredWidth = std::accumulate(m_colWidths.begin(), m_colWidths.end(), 0);
    }

    m_totalTableWidth = measuredWidth;
    UpdateScrollParams();
}

void CustomTableView::OnSize(wxSizeEvent& event) {
    RecalculateLayout();
    if (m_canvas) m_canvas->Refresh();
    event.Skip();
}

void CustomTableView::OnPaint(wxPaintEvent& WXUNUSED(event)) {
    if (!m_canvas) return;
    wxAutoBufferedPaintDC dc(m_canvas);
    wxSize clientSize = m_canvas->GetClientSize();
    if (clientSize.x <= 0 || clientSize.y <= 0) return;

    auto palette = ThemeColors::GetCurrentPalette();
    dc.SetBackground(wxBrush(palette.cardBg));
    dc.Clear();

    if (m_data.IsEmpty()) {
        dc.SetFont(m_hintFont);
        dc.SetTextForeground(palette.textSecondary);
        wxString hint = L"暂无表格数据";
        wxSize sz = dc.GetTextExtent(hint);
        dc.DrawText(hint, (clientSize.x - sz.x) / 2, (clientSize.y - sz.y) / 2);
        return;
    }

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;

    int startDrawX = 4_dip;
    int startDrawY = 4_dip;
    int tableWidth = std::max(m_totalTableWidth, clientSize.x - 8_dip);
    int colCount = static_cast<int>(m_colWidths.size());

    int totalRows = static_cast<int>(m_data.rows.size());
    int visibleRows = std::max(1, (clientSize.y - m_headerHeight - 8_dip) / m_rowHeight + 1);
    int endRow = std::min(m_firstVisibleRow + visibleRows, totalRows);
    int renderedRowCount = endRow - m_firstVisibleRow;

    // 1. 绘制表格外边框与圆角背景
    wxGraphicsPath cardPath = gc->CreatePath();
    double radius = 6.0_dip;
    int totalDrawH = m_headerHeight + renderedRowCount * m_rowHeight;
    cardPath.AddRoundedRectangle(startDrawX, startDrawY, tableWidth, totalDrawH, radius);
    gc->SetBrush(gc->CreateBrush(wxBrush(palette.cardBg)));
    gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 1)));
    gc->DrawPath(cardPath);

    // 2. 绘制表头 (Header) 背景
    wxGraphicsPath headerPath = gc->CreatePath();
    headerPath.AddRoundedRectangle(startDrawX, startDrawY, tableWidth, m_headerHeight, radius);
    gc->SetBrush(gc->CreateBrush(wxBrush(palette.bannerBg)));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->FillPath(headerPath);

    // 绘制表头底部分割线
    gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 1.5)));
    gc->StrokeLine(startDrawX, startDrawY + m_headerHeight, startDrawX + tableWidth, startDrawY + m_headerHeight);

    // 3. 绘制表头文字与列分割线
    gc->SetFont(m_headerFont, palette.textPrimary);
    int curColX = startDrawX;
    for (int c = 0; c < colCount; ++c) {
        int colW = m_colWidths[c];
        if (c < static_cast<int>(m_data.headers.size())) {
            wxString hText = wxString::FromUTF8(m_data.headers[c]);
            double tw = 0, th = 0;
            gc->GetTextExtent(hText, &tw, &th);

            gc->PushState();
            gc->Clip(curColX + 4_dip, startDrawY, colW - 8_dip, m_headerHeight);
            gc->DrawText(hText, curColX + 12_dip, startDrawY + (m_headerHeight - th) / 2.0);
            gc->PopState();
        }

        // 垂直列分割线
        if (c + 1 < colCount) {
            gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 0.8)));
            gc->StrokeLine(curColX + colW, startDrawY + 4_dip, curColX + colW, startDrawY + m_headerHeight - 4_dip);
        }
        curColX += colW;
    }

    // 4. 绘制可见数据行 (Data Rows)
    int curRowY = startDrawY + m_headerHeight;
    for (int r = m_firstVisibleRow; r < endRow; ++r) {
        int rowH = m_rowHeight;
        if (curRowY + rowH > clientSize.y) break;

        bool isHovered = (r == m_hoveredRow);
        bool isSelected = (r == m_selectedRow);

        // 行背景色 (斑马纹 / Hover / Selected)
        wxColour rowBg;
        if (isSelected) {
            rowBg = palette.bannerBg;
        } else if (isHovered) {
            rowBg = (ThemeColors::GetInstance().GetCurrentTheme() == ThemeMode::Dark)
                        ? wxColour(45, 60, 85)
                        : wxColour(241, 245, 249);
        } else if (r % 2 == 1) {
            rowBg = (ThemeColors::GetInstance().GetCurrentTheme() == ThemeMode::Dark)
                        ? wxColour(35, 48, 68)
                        : wxColour(248, 250, 252);
        } else {
            rowBg = palette.cardBg;
        }

        gc->SetBrush(gc->CreateBrush(wxBrush(rowBg)));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->DrawRectangle(startDrawX + 1, curRowY, tableWidth - 2, rowH);

        // 绘制单元格文本
        gc->SetFont(m_rowFont, palette.textPrimary);
        curColX = startDrawX;
        const auto& rowData = m_data.rows[r];

        for (int c = 0; c < colCount; ++c) {
            int colW = m_colWidths[c];
            if (c < static_cast<int>(rowData.size())) {
                wxString cellText = wxString::FromUTF8(rowData[c]);
                double tw = 0, th = 0;
                gc->GetTextExtent(cellText, &tw, &th);

                gc->PushState();
                gc->Clip(curColX + 4_dip, curRowY, colW - 8_dip, rowH);
                gc->DrawText(cellText, curColX + 12_dip, curRowY + (rowH - th) / 2.0);
                gc->PopState();
            }

            // 列分隔线
            if (c + 1 < colCount) {
                gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 0.5)));
                gc->StrokeLine(curColX + colW, curRowY, curColX + colW, curRowY + rowH);
            }
            curColX += colW;
        }

        // 行底部分割线
        gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 0.8)));
        gc->StrokeLine(startDrawX, curRowY + rowH, startDrawX + tableWidth, curRowY + rowH);

        curRowY += rowH;
    }
}

void CustomTableView::OnMouseMove(wxMouseEvent& event) {
    if (m_scrollBar) m_scrollBar->NotifyActivity();

    int mouseY = event.GetY();
    int relY = mouseY - (4_dip + m_headerHeight);

    int oldHover = m_hoveredRow;
    if (relY >= 0 && !m_data.rows.empty()) {
        int r = m_firstVisibleRow + (relY / m_rowHeight);
        if (r >= 0 && r < static_cast<int>(m_data.rows.size())) {
            m_hoveredRow = r;
        } else {
            m_hoveredRow = -1;
        }
    } else {
        m_hoveredRow = -1;
    }

    if (oldHover != m_hoveredRow && m_canvas) {
        m_canvas->Refresh();
    }
    event.Skip();
}

void CustomTableView::OnMouseLeave(wxMouseEvent& event) {
    if (m_hoveredRow != -1) {
        m_hoveredRow = -1;
        if (m_canvas) m_canvas->Refresh();
    }
    event.Skip();
}

void CustomTableView::OnLeftDown(wxMouseEvent& event) {
    if (m_scrollBar) m_scrollBar->NotifyActivity();

    int mouseX = event.GetX();
    int mouseY = event.GetY();

    int relY = mouseY - (4_dip + m_headerHeight);
    if (relY >= 0 && !m_data.rows.empty()) {
        int r = m_firstVisibleRow + (relY / m_rowHeight);
        if (r >= 0 && r < static_cast<int>(m_data.rows.size())) {
            m_selectedRow = r;

            // 识别点击的列
            int curColX = 4_dip;
            m_selectedCol = -1;
            for (size_t c = 0; c < m_colWidths.size(); ++c) {
                if (mouseX >= curColX && mouseX < curColX + m_colWidths[c]) {
                    m_selectedCol = static_cast<int>(c);
                    break;
                }
                curColX += m_colWidths[c];
            }

            if (m_canvas) m_canvas->Refresh();
        }
    }
    event.Skip();
}

void CustomTableView::OnMouseWheel(wxMouseEvent& event) {
    if (m_scrollBar) m_scrollBar->NotifyActivity();
    int rotation = event.GetWheelRotation();
    int delta = (rotation > 0) ? -3 : 3;
    ScrollToRow(m_firstVisibleRow + delta);
}

void CustomTableView::OnRightDown(wxMouseEvent& event) {
    ShowContextMenu(event.GetPosition());
}

void CustomTableView::ShowContextMenu(const wxPoint& pos) {
    wxMenu menu;
    menu.Append(101, L"复制表格 (Excel / WPS 格式)\tCtrl+C");
    menu.Append(102, L"复制为 Markdown 表格");
    menu.Append(103, L"复制为 CSV 格式");

    if (m_selectedRow >= 0 && m_selectedRow < static_cast<int>(m_data.rows.size()) &&
        m_selectedCol >= 0 && m_selectedCol < static_cast<int>(m_data.rows[m_selectedRow].size())) {
        menu.AppendSeparator();
        menu.Append(104, L"复制选中单元格内容");
    }

    menu.AppendSeparator();
    menu.Append(105, L"导出为 CSV 文件...");

    Bind(wxEVT_MENU, [this](wxCommandEvent& ev) {
        switch (ev.GetId()) {
        case 101:
            CopyAsExcel();
            break;
        case 102:
            CopyAsMarkdown();
            break;
        case 103:
            CopyAsCsv();
            break;
        case 104:
            if (m_selectedRow >= 0 && m_selectedRow < static_cast<int>(m_data.rows.size()) &&
                m_selectedCol >= 0 && m_selectedCol < static_cast<int>(m_data.rows[m_selectedRow].size())) {
                std::string cellVal = m_data.rows[m_selectedRow][m_selectedCol];
                ClipboardHelper::SetClipboardText(cellVal);
            }
            break;
        case 105:
            ExportCsvDialog();
            break;
        }
    });

    PopupMenu(&menu, pos);
}

void CustomTableView::CopyAsExcel() {
    if (m_data.IsEmpty()) return;
    std::string tsv = TableParser::ToTsv(m_data);
    ClipboardHelper::SetClipboardText(tsv);
    wxMessageBox(L"表格已复制为 Excel/WPS 兼容格式，可直接粘贴到电子表格软件！",
                 L"复制成功", wxOK | wxICON_INFORMATION, this);
}

void CustomTableView::CopyAsMarkdown() {
    if (m_data.IsEmpty()) return;
    std::string md = TableParser::ToMarkdown(m_data);
    ClipboardHelper::SetClipboardText(md);
    wxMessageBox(L"表格已复制为 Markdown 格式！", L"复制成功", wxOK | wxICON_INFORMATION, this);
}

void CustomTableView::CopyAsCsv() {
    if (m_data.IsEmpty()) return;
    std::string csv = TableParser::ToCsv(m_data);
    ClipboardHelper::SetClipboardText(csv);
    wxMessageBox(L"表格已复制为 CSV 格式！", L"复制成功", wxOK | wxICON_INFORMATION, this);
}

void CustomTableView::ExportCsvDialog() {
    if (m_data.IsEmpty()) return;

    wxFileDialog saveFileDialog(this, L"导出表格为 CSV 文件", "", "table_result.csv",
                               "CSV 文件 (*.csv)|*.csv", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (saveFileDialog.ShowModal() == wxID_CANCEL)
        return;

    std::string csv = TableParser::ToCsv(m_data);
    wxFileOutputStream output_stream(saveFileDialog.GetPath());
    if (!output_stream.IsOk()) {
        wxMessageBox(L"无法保存文件到指定路径！", L"错误", wxOK | wxICON_ERROR, this);
        return;
    }

    // 写入 UTF-8 BOM，确保 Excel 打开中文不乱码
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    output_stream.Write(bom, sizeof(bom));
    output_stream.Write(csv.data(), csv.size());

    wxMessageBox(L"表格已成功导出为 CSV 文件！", L"导出成功", wxOK | wxICON_INFORMATION, this);
}

} // namespace LinguaAlpaca::UI
