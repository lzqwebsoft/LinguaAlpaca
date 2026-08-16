#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include <memory>
#include <string>
#include "../../core/ModelManager.hpp"
#include "../../core/Config.hpp"
#include "CustomButton.hpp"

namespace LinguaAlpaca::UI {

class TranslationBubbleFrame : public wxFrame {
public:
    enum class ResizeDirection {
        None,
        Left,
        Right,
        Top,
        Bottom,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    explicit TranslationBubbleFrame(std::shared_ptr<ModelManager> modelManager, wxWindow* parent = nullptr);
    ~TranslationBubbleFrame() override = default;

    // 显示并启动划词翻译
    void ShowAndTranslate(const wxPoint& spawnPos, const std::string& sourceText);

    // 隐藏窗口并取消未完成的推理
    void Dismiss();

    // 更新深浅色主题
    void UpdateTheme();

private:
    void InitUI();

    // 标题栏拖拽与边缘检测
    void OnHeaderLeftDown(wxMouseEvent& event);
    void OnHeaderLeftUp(wxMouseEvent& event);
    void OnHeaderMouseMove(wxMouseEvent& event);
    void OnHeaderMouseLeave(wxMouseEvent& event);

    // 主面板与底栏边缘检测
    void OnMainPanelMouseMove(wxMouseEvent& event);
    void OnMainPanelLeftDown(wxMouseEvent& event);
    void OnMainPanelLeftUp(wxMouseEvent& event);
    void OnMainPanelMouseLeave(wxMouseEvent& event);

    void OnFooterMouseMove(wxMouseEvent& event);
    void OnFooterLeftDown(wxMouseEvent& event);
    void OnFooterLeftUp(wxMouseEvent& event);
    void OnFooterMouseLeave(wxMouseEvent& event);

    // 右下角 Grip 手柄事件
    void OnGripPaint(wxPaintEvent& event);
    void OnGripLeftDown(wxMouseEvent& event);
    void OnGripMouseMove(wxMouseEvent& event);
    void OnGripLeftUp(wxMouseEvent& event);

    // 调整大小核心辅助函数
    ResizeDirection HitTest(const wxPoint& ptInFrame, const wxSize& frameSize) const;
    void UpdateCursorForDir(ResizeDirection dir, wxWindow* targetWin);
    void StartResize(ResizeDirection dir, const wxPoint& screenPos, wxWindow* captureWin);
    void ProcessResizeDrag(const wxPoint& screenPos);
    void EndResize();

    // 按钮操作
    void OnCopyResult(wxCommandEvent& event);
    void OnTogglePin(wxCommandEvent& event);
    void OnCloseBtn(wxCommandEvent& event);

    std::shared_ptr<ModelManager> m_modelManager;

    // UI 组件
    wxPanel* m_mainPanel{nullptr};
    wxPanel* m_headerPanel{nullptr};
    wxStaticText* m_titleText{nullptr};
    wxStaticText* m_langBadge{nullptr};
    wxButton* m_pinBtn{nullptr};
    wxButton* m_copyBtn{nullptr};
    wxButton* m_closeBtn{nullptr};

    wxTextCtrl* m_sourceCtrl{nullptr};
    wxTextCtrl* m_targetCtrl{nullptr};

    wxPanel* m_footerPanel{nullptr};
    wxStaticText* m_statusText{nullptr};
    wxPanel* m_resizeGrip{nullptr};

    bool m_isPinned{false};
    bool m_hasPinnedPos{false};
    wxPoint m_pinnedPos;
    bool m_isDragging{false};
    wxPoint m_dragStartPos;
    std::string m_currentFullText;

    // 缩放状态与尺寸记忆
    bool m_isResizing{false};
    ResizeDirection m_resizeDir{ResizeDirection::None};
    wxPoint m_resizeStartMousePos;
    wxPoint m_resizeStartFramePos;
    wxSize m_resizeStartFrameSize;
    wxSize m_bubbleSize;
    wxWindow* m_resizeCaptureWin{nullptr};
};

} // namespace LinguaAlpaca::UI
