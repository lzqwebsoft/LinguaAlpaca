#include "OcrProgressPanel.hpp"
#include <algorithm>

namespace LinguaAlpaca::UI {

OcrProgressPanel::OcrProgressPanel(wxWindow* parent, wxWindowID id,
                                   const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, id, pos, size, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.cardBg);
    SetMinSize(dip(-1, 36));

    Bind(wxEVT_PAINT, &OcrProgressPanel::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        Refresh();
        event.Skip();
    });
}

wxSize OcrProgressPanel::DoGetBestSize() const {
    return dip(-1, 36);
}

void OcrProgressPanel::SetIdle(const wxString& hint) {
    m_status = OcrProgressStatus::Idle;
    m_percent = 0;
    m_curPage = 0;
    m_totalPages = 0;
    m_title = L"两阶段文档解析流水线就绪";
    if (!hint.IsEmpty()) {
        m_stageDesc = hint;
    } else {
        m_stageDesc = L"PP-DocLayout 版面分析 (多栏排版/表格/公式拓扑重构) ➔ PaddleOCR-VL 视觉定向识别";
    }
    Refresh();
}

void OcrProgressPanel::SetStarting(const wxString& message) {
    m_status = OcrProgressStatus::Starting;
    m_percent = 2;
    m_title = L"准备解析流水线";
    m_stageDesc = message;
    Refresh();
}

void OcrProgressPanel::SetProgress(int curPage, int totalPages, int percent, const wxString& stageDesc) {
    m_status = OcrProgressStatus::Running;
    m_curPage = curPage;
    m_totalPages = totalPages;
    m_percent = std::clamp(percent, 0, 100);

    if (totalPages > 1) {
        m_title = wxString::Format(L"正在解析文档 (第 %d / %d 页)", curPage, totalPages);
    } else {
        m_title = L"正在解析图像文档";
    }
    m_stageDesc = stageDesc;
    Refresh();
}

void OcrProgressPanel::SetCompleted(int totalPages, const wxString& outputDir) {
    m_status = OcrProgressStatus::Completed;
    m_percent = 100;
    m_totalPages = totalPages;
    if (totalPages > 1) {
        m_title = wxString::Format(L"✓ 文档解析完成 (共 %d 页已重构为 Markdown)", totalPages);
    } else {
        m_title = L"✓ 文档解析完成 (已结构化生成 Markdown)";
    }
    if (!outputDir.IsEmpty()) {
        m_stageDesc = L"结果已自动保存至: " + outputDir;
    } else {
        m_stageDesc = L"表格、公式、插图与正文均已按阅读顺序完成结构化重构";
    }
    Refresh();
}

void OcrProgressPanel::SetCancelled(int completedPages, int totalPages, const wxString& message) {
    m_status = OcrProgressStatus::Cancelled;
    if (totalPages > 1 && completedPages > 0) {
        m_title = wxString::Format(L"⏹ 解析已停止 (已成功保留前 %d 页识别结果)", completedPages);
    } else {
        m_title = L"⏹ 解析已停止 (已保留当前解析结果)";
    }
    if (!message.IsEmpty()) {
        m_stageDesc = message;
    } else {
        m_stageDesc = L"用户已中断解析，右侧文本框已保留当前所有已生成的 Markdown 结果";
    }
    Refresh();
}

void OcrProgressPanel::SetError(const wxString& errorMessage) {
    m_status = OcrProgressStatus::Error;
    m_title = L"✕ 解析提示 / 异常";
    m_stageDesc = errorMessage;
    Refresh();
}

void OcrProgressPanel::Reset() {
    SetIdle();
}

void OcrProgressPanel::UpdateTheme() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.cardBg);
    Refresh();
}

void OcrProgressPanel::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    wxSize size = GetClientSize();
    if (size.x <= 0 || size.y <= 0) return;

    auto p = ThemeColors::GetCurrentPalette();
    dc.SetBackground(wxBrush(p.windowBg));
    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;

    // 1. 卡片基础几何与圆角路径
    double cardX = 2_dip;
    double cardY = 2_dip;
    double cardW = size.x - 4_dip;
    double cardH = size.y - 4_dip;
    double radius = 6.0_dip;

    if (cardW <= 0 || cardH <= 0) return;

    wxGraphicsPath cardPath = gc->CreatePath();
    cardPath.AddRoundedRectangle(cardX, cardY, cardW, cardH, radius);

    // 基础卡片底色填充
    gc->SetBrush(gc->CreateBrush(wxBrush(p.cardBg)));
    gc->FillPath(cardPath);

    // 2. 整体背景色块进度映射 (从左侧平滑延伸至对应百分比宽度，100% 铺满整个组件)
    if (m_percent > 0) {
        double fillRatio = std::clamp(static_cast<double>(m_percent) / 100.0, 0.0, 1.0);
        double fillW = cardW * fillRatio;
        if (fillW > 0) {
            wxColour fillCol;
            wxColour edgeCol;
            switch (m_status) {
            case OcrProgressStatus::Completed:
                fillCol = wxColour(34, 197, 94, 45);          // 翠绿色半透明色块
                edgeCol = wxColour(34, 197, 94, 160);
                break;
            case OcrProgressStatus::Cancelled:
                fillCol = wxColour(245, 158, 11, 40);         // 琥珀黄半透明色块
                edgeCol = wxColour(245, 158, 11, 150);
                break;
            case OcrProgressStatus::Error:
                fillCol = wxColour(239, 68, 68, 40);          // 珊瑚红半透明色块
                edgeCol = wxColour(239, 68, 68, 150);
                break;
            case OcrProgressStatus::Starting:
            case OcrProgressStatus::Running:
            default:
                fillCol = wxColour(p.accentPrimary.Red(), p.accentPrimary.Green(), p.accentPrimary.Blue(), 42); // 科技蓝半透明色块
                edgeCol = wxColour(p.accentPrimary.Red(), p.accentPrimary.Green(), p.accentPrimary.Blue(), 160);
                break;
            }

            gc->PushState();
            // 通过矩形裁剪卡片路径，既平滑保留左端圆角，又在进度百分比处利落截断
            gc->Clip(cardX, cardY, fillW, cardH);
            gc->SetBrush(gc->CreateBrush(wxBrush(fillCol)));
            gc->FillPath(cardPath);

            // 未达到 100% 时，在进度色块右端绘制一条精致的高光分界线增加科技动感
            if (fillRatio < 1.0 && fillW >= 2_dip) {
                gc->SetPen(gc->CreatePen(wxPen(edgeCol, 1.5)));
                gc->StrokeLine(cardX + fillW, cardY, cardX + fillW, cardY + cardH);
            }
            gc->PopState();
        }
    }

    // 3. 卡片边框绘制
    wxColour borderCol = p.cardBorder;
    switch (m_status) {
    case OcrProgressStatus::Starting:
    case OcrProgressStatus::Running:
        borderCol = p.accentPrimary;
        break;
    case OcrProgressStatus::Completed:
        borderCol = wxColour(34, 197, 94);
        break;
    case OcrProgressStatus::Cancelled:
        borderCol = wxColour(245, 158, 11);
        break;
    case OcrProgressStatus::Error:
        borderCol = wxColour(239, 68, 68);
        break;
    case OcrProgressStatus::Idle:
    default:
        borderCol = p.cardBorder;
        break;
    }
    gc->SetPen(gc->CreatePen(wxPen(borderCol, 1)));
    gc->StrokePath(cardPath);

    // 4. 状态指示圆点 (单行垂直居中)
    wxColour dotCol = p.textSecondary;
    switch (m_status) {
    case OcrProgressStatus::Starting:
    case OcrProgressStatus::Running:
        dotCol = p.accentPrimary;
        break;
    case OcrProgressStatus::Completed:
        dotCol = wxColour(34, 197, 94);
        break;
    case OcrProgressStatus::Cancelled:
        dotCol = wxColour(245, 158, 11);
        break;
    case OcrProgressStatus::Error:
        dotCol = wxColour(239, 68, 68);
        break;
    default:
        dotCol = p.textSecondary;
        break;
    }

    double dotX = 16_dip;
    double dotY = size.y / 2.0;
    double dotR = 3.5_dip;
    gc->SetBrush(gc->CreateBrush(wxBrush(dotCol)));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawEllipse(dotX - dotR, dotY - dotR, dotR * 2, dotR * 2);

    // 5. 右侧状态/百分比药丸 (Pill Badge, 垂直居中)
    int pillW = 58_dip;
    int pillH = 22_dip;
    double pillX = size.x - 12_dip - pillW;
    double pillY = (size.y - pillH) / 2.0;

    wxColour pillBg;
    wxColour pillFg;
    wxString pillText;

    switch (m_status) {
    case OcrProgressStatus::Starting:
    case OcrProgressStatus::Running:
        pillBg = wxColour(p.accentPrimary.Red(), p.accentPrimary.Green(), p.accentPrimary.Blue(), 45);
        pillFg = p.accentPrimary;
        pillText = wxString::Format(L"%d%%", m_percent);
        break;
    case OcrProgressStatus::Completed:
        pillBg = wxColour(34, 197, 94, 45);
        pillFg = wxColour(34, 197, 94);
        pillText = L"100%";
        break;
    case OcrProgressStatus::Cancelled:
        pillBg = wxColour(245, 158, 11, 45);
        pillFg = wxColour(245, 158, 11);
        pillText = wxString::Format(L"%d%%", m_percent);
        break;
    case OcrProgressStatus::Error:
        pillBg = wxColour(239, 68, 68, 45);
        pillFg = wxColour(239, 68, 68);
        pillText = L"异常";
        break;
    case OcrProgressStatus::Idle:
    default:
        pillBg = wxColour(120, 120, 120, 30);
        pillFg = p.textSecondary;
        pillText = L"就绪";
        break;
    }

    gc->SetBrush(gc->CreateBrush(wxBrush(pillBg)));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRoundedRectangle(pillX, pillY, pillW, pillH, 11.0_dip);

    wxFont pillFont = ThemeFont::GetFont(FontRole::Control, true);
    gc->SetFont(pillFont, pillFg);
    double pTw = 0, pTh = 0;
    gc->GetTextExtent(pillText, &pTw, &pTh);
    gc->DrawText(pillText, pillX + (pillW - pTw) / 2.0, pillY + (pillH - pTh) / 2.0);

    // 6. 左侧标题与阶段描述文字 (单行垂直居中排版)
    double textStartX = dotX + dotR + 8_dip;

    wxColour titleCol = (m_status == OcrProgressStatus::Running || m_status == OcrProgressStatus::Starting)
                            ? p.accentPrimary
                            : (m_status == OcrProgressStatus::Error ? wxColour(239, 68, 68) : p.textPrimary);
    wxFont titleFont = ThemeFont::GetFont(FontRole::Control, true);
    gc->SetFont(titleFont, titleCol);

    double titleW = 0, titleH = 0;
    gc->GetTextExtent(m_title, &titleW, &titleH);
    double titleY = (size.y - titleH) / 2.0;
    gc->DrawText(m_title, textStartX, titleY);

    // 阶段详情描述文字 (带防溢出裁剪，垂直居中)
    double descStartX = textStartX + titleW + 10_dip;
    double maxDescW = pillX - descStartX - 10_dip;

    if (maxDescW > 30_dip && !m_stageDesc.IsEmpty()) {
        wxFont descFont = ThemeFont::GetFont(FontRole::Control);
        gc->SetFont(descFont, p.textSecondary);
        double descW = 0, descH = 0;
        gc->GetTextExtent(m_stageDesc, &descW, &descH);
        double descY = (size.y - descH) / 2.0;

        gc->PushState();
        gc->Clip(descStartX, 0, maxDescW, size.y);
        gc->DrawText(m_stageDesc, descStartX, descY);
        gc->PopState();
    }
}

} // namespace LinguaAlpaca::UI
