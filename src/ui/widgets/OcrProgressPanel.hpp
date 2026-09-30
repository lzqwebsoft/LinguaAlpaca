#pragma once
#include <wx/wx.h>
#include <wx/graphics.h>
#include <wx/dcbuffer.h>
#include "../theme/Theme.hpp"

namespace LinguaAlpaca::UI {

enum class OcrProgressStatus {
    Idle,
    Starting,
    Running,
    Completed,
    Cancelled,
    Error
};

/**
 * @brief 现代化两阶段文档 OCR 解析状态与进度展示面板
 *
 * 特性：
 * - 纯 GraphicsContext 抗锯齿高质感渲染，无缝适配深色/浅色主题与高 DPI 缩放
 * - 支持整体组件背景色块平滑进度条映射与百分比状态药丸 (Pill Badge)，极致压缩高度提升版面空间
 * - 明确区分多页 PDF (如 "第 5 / 50 页 (10%)") 与单张图片的两阶段解析阶段
 * - 独立承载模型准备、阶段提示、用户取消与异常信息，彻底避免冲掉右侧已解析结果
 */
class OcrProgressPanel : public wxPanel {
public:
    OcrProgressPanel(wxWindow* parent, wxWindowID id = wxID_ANY,
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxDefaultSize);
    ~OcrProgressPanel() override = default;

    void SetIdle(const wxString& hint = wxEmptyString);
    void SetStarting(const wxString& message);
    void SetProgress(int curPage, int totalPages, int percent, const wxString& stageDesc);
    void SetCompleted(int totalPages, const wxString& outputDir);
    void SetCancelled(int completedPages, int totalPages, const wxString& message = wxEmptyString);
    void SetError(const wxString& errorMessage);
    void Reset();

    void UpdateTheme();

protected:
    wxSize DoGetBestSize() const override;

private:
    void OnPaint(wxPaintEvent& event);

    OcrProgressStatus m_status{OcrProgressStatus::Idle};
    int m_percent{0};
    int m_curPage{0};
    int m_totalPages{0};

    wxString m_title{L"两阶段文档解析流水线就绪"};
    wxString m_stageDesc{L"PP-DocLayout 版面分析 (多栏排版/表格/公式拓扑重构) ➔ PaddleOCR-VL 视觉定向识别"};
};

} // namespace LinguaAlpaca::UI
