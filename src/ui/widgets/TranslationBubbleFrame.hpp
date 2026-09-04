#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include <memory>
#include <string>
#include "../../core/ModelManager.hpp"
#include "../../core/Config.hpp"
#include "CustomButton.hpp"
#include "StatusBadge.hpp"
#include "SplitterWindow.hpp"
#include "TextCtrl.hpp"
#include "../AsyncTrackable.hpp"

namespace LinguaAlpaca::UI {

class TranslationBubbleFrame : public wxFrame, public AsyncTrackable {
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
    ~TranslationBubbleFrame() override;

    // 显示并启动划词翻译
    void ShowAndTranslate(const wxPoint& spawnPos, const std::string& sourceText);

    // 隐藏窗口并取消未完成的推理
    void Dismiss();

    // 更新深浅色主题
    void UpdateTheme();

    // 同步刷新语言胶囊标签
    void UpdateLanguageBadge();

private:
    void InitUI();

    // 边缘缩放与拖拽处理 (统一消除冗余的各窗口重复事件定义)
    void HandleEdgeMouseMove(wxMouseEvent& event, wxWindow* sourceWin, bool isHeader = false);
    void HandleEdgeLeftDown(wxMouseEvent& event, wxWindow* sourceWin, bool isHeader = false);
    void HandleEdgeLeftUp(wxMouseEvent& event);
    void HandleEdgeMouseLeave(wxMouseEvent& event, wxWindow* sourceWin);
    void OnGripPaint(wxPaintEvent& event);

    // 调整大小核心辅助函数
    ResizeDirection HitTest(const wxPoint& ptInFrame, const wxSize& frameSize) const;
    void UpdateCursorForDir(ResizeDirection dir, wxWindow* targetWin);
    void StartResize(ResizeDirection dir, const wxPoint& screenPos, wxWindow* captureWin);
    void ProcessResizeDrag(const wxPoint& screenPos);
    void EndResize();

    // 按钮操作
    void OnIncreaseFontSize(wxCommandEvent& event);
    void OnDecreaseFontSize(wxCommandEvent& event);
    void ApplyFontSize(int fontSize, bool saveToConfig = true);
    void OnCopyResult(wxCommandEvent& event);
    void OnTogglePin(wxCommandEvent& event);
    void OnRetry(wxCommandEvent& event);
    void OnCloseBtn(wxCommandEvent& event);

    void DoExecuteTranslation(const std::string& sourceText);
    void SetSourcePanelExpanded(bool expanded);
    void UpdateSourcePreview();

    std::shared_ptr<ModelManager> m_modelManager;

    // UI 组件
    wxPanel* m_mainPanel{nullptr};
    wxPanel* m_headerPanel{nullptr};
    wxStaticText* m_titleText{nullptr};
    StatusBadge* m_langBadge{nullptr};
    wxBitmapButton* m_fontDecreaseBtn{nullptr};
    wxBitmapButton* m_fontIncreaseBtn{nullptr};
    wxBitmapButton* m_pinBtn{nullptr};
    wxBitmapButton* m_retryBtn{nullptr};
    wxBitmapButton* m_copyBtn{nullptr};
    wxBitmapButton* m_closeBtn{nullptr};

    // 原文折叠/展开相关控件
    wxPanel* m_sourceToggleBar{nullptr};
    wxStaticBitmap* m_sourceToggleIcon{nullptr};
    wxStaticText* m_sourceToggleLabel{nullptr};
    wxStaticText* m_sourcePreviewText{nullptr};
    bool m_isSourceExpanded{false};
    int m_savedSashPos{0};

    SplitterWindow* m_splitter{nullptr};
    wxPanel* m_sourcePanel{nullptr};
    wxPanel* m_targetPanel{nullptr};
    TextCtrl* m_sourceCtrl{nullptr};
    TextCtrl* m_targetCtrl{nullptr};
    wxBitmapButton* m_sourceSpeakBtn{nullptr};
    wxBitmapButton* m_targetSpeakBtn{nullptr};

    wxPanel* m_footerPanel{nullptr};
    wxStaticText* m_statusText{nullptr};
    wxPanel* m_resizeGrip{nullptr};

    bool m_isPinned{false};
    bool m_hasPinnedPos{false};
    wxPoint m_pinnedPos;
    bool m_isDragging{false};
    wxPoint m_dragOffset;
    std::string m_lastSourceText;
    std::string m_currentFullText;

    // 缩放状态与尺寸记忆
    bool m_isResizing{false};
    ResizeDirection m_resizeDir{ResizeDirection::None};
    wxPoint m_resizeStartMousePos;
    wxPoint m_resizeStartFramePos;
    wxSize m_resizeStartFrameSize;
    wxSize m_bubbleSize;
    wxWindow* m_resizeCaptureWin{nullptr};

    // 字体大小
    int m_currentFontSize{10};
};

} // namespace LinguaAlpaca::UI
