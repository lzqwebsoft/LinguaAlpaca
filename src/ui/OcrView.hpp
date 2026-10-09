#pragma once
#include <wx/wx.h>
#include <wx/dnd.h>
#include <wx/timer.h>
#include <unordered_map>
#include <list>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <atomic>
#include "AsyncTrackable.hpp"
#include "core/ModelManager.hpp"
#include "widgets/CardPanel.hpp"
#include "widgets/CustomButton.hpp"
#include "widgets/ImagePreviewDialog.hpp"
#include "widgets/TextCtrl.hpp"
#include "widgets/StatusBadge.hpp"
#include "widgets/OcrProgressPanel.hpp"

namespace LinguaAlpaca::UI {

enum class OcrTaskState {
    Idle,
    Recognizing,
    Translating
};

enum class DropzoneHoverAction {
    None,
    Preview,
    Replace,
    PdfSlider
};

class OcrView : public wxPanel, public AsyncTrackable {
public:
    OcrView(wxWindow* parent,
            std::shared_ptr<ModelManager> modelManager,
            wxWindowID id = wxID_ANY);

    ~OcrView() override;

    void UpdateTheme();
    void UpdateStatusBadge();
    void OnImageFileDropped(const wxString& filePath);
    void SetTranslateCallback(std::function<void(const wxString&)> callback) {
        m_onTranslateCallback = std::move(callback);
    }
    bool Show(bool show = true) override;

private:
    void InitUI();
    void OpenImageDialog();
    void OpenImagePreview();
    void OnSelectImageClicked(wxMouseEvent& event);
    void OnDropzoneMouseEnter(wxMouseEvent& event);
    void OnDropzoneMouseLeave(wxMouseEvent& event);
    void OnDropzoneMouseMove(wxMouseEvent& event);
    void OnDropzoneLeftDown(wxMouseEvent& event);
    void OnDropzoneLeftUp(wxMouseEvent& event);
    void OnDropzoneMouseWheel(wxMouseEvent& event);
    void OnRecognizeClicked(wxCommandEvent& event);
    void OnStopClicked(wxCommandEvent& event);
    void OnTranslateClicked(wxCommandEvent& event);
    void UpdateTranslateButtonVisibility();

    void SetPdfPage(int page);
    void RequestPdfPageAsync(int page);
    void OnPdfDebounceTimer(wxTimerEvent& event);
    void PutPageCache(int page, const wxImage& img);
    bool TryGetPageCache(int page, wxImage& outImg);
    void ClearPageCache();
    void StartRenderWorker();
    void StopRenderWorker();

    void DoExecuteDocumentPipeline(const std::string& docPath);
    void OpenOutputDir();

    void SetState(OcrTaskState state);
    void LoadImageFile(const wxString& filePath);
    bool CheckAndPromptResume(const wxString& filePath);
    bool PasteImageFromClipboard();
    void ShowDropzoneContextMenu(const wxPoint& pos);
    void UpdateDropzoneUI();

    std::shared_ptr<ModelManager> m_modelManager;
    wxTimer m_healthTimer;
    wxTimer m_pdfDebounceTimer;
    std::atomic<uint64_t> m_pdfRenderRequestId{0};

    // 专用单任务后台渲染工作线程 (单槽位新任务覆盖旧任务，彻底杜绝高频拖拽/翻页时线程爆炸与 COM 阻塞)
    std::thread m_renderWorker;
    std::mutex m_workerMutex;
    std::condition_variable m_workerCv;
    bool m_workerStop{false};

    struct WorkerTask {
        std::string filePath;
        int pageIndex{0};
        uint64_t reqId{0};
        int targetWidth{1000};
        bool isPrefetch{false};
    };
    std::optional<WorkerTask> m_pendingTask;

    // PDF 页面 LRU 缓存与快速预览 (最大缓存 20 页，保证滑动和翻页 0ms 瞬间响应)
    std::mutex m_pageCacheMutex;
    std::unordered_map<int, wxImage> m_pdfPageCache;
    std::list<int> m_pdfPageCacheOrder;
    static constexpr size_t MAX_PDF_PAGE_CACHE = 20;

    // 缩放绘制位图缓存 (消除鼠标悬停/拖拽时每帧高开销 wxIMAGE_QUALITY_HIGH 重采样)
    wxBitmap m_cachedDisplayBmp;
    int m_cachedDrawW{0};
    int m_cachedDrawH{0};
    int m_cachedDisplayPage{-1};
    wxString m_cachedImagePath;

    OcrTaskState m_currentState{OcrTaskState::Idle};

    wxString m_loadedImagePath;
    wxString m_imageFileName;
    wxImage m_loadedImage;

    bool m_isPdfDoc{false};
    int m_pdfTotalPages{0};
    int m_pdfCurrentPage{0};
    int m_pdfScrollY{0};
    wxString m_lastMarkdownResult;
    wxString m_lastJsonResult;
    wxString m_lastOutputDir;

    // 断点恢复与进度追踪
    int m_resumeFromPage{0};
    std::string m_resumeInitialMarkdown;
    std::string m_resumeInitialJson;

    // 渲染平滑调度与节流控制（彻底避免百页文档频繁重排卡死 UI）
    uint64_t m_lastRenderTimestamp{0};
    int m_lastRenderedPage{-1};

    // Dropzone hover & action states
    bool m_isDropzoneHovered{false};
    DropzoneHoverAction m_hoveredAction{DropzoneHoverAction::None};
    wxRect m_previewBtnRect;
    wxRect m_centerBtnRect;

    // PDF Vertical Slider states
    wxRect m_pdfSliderTrackRect;
    wxRect m_pdfSliderThumbRect;
    bool m_isHoveringPdfSlider{false};
    bool m_isDraggingPdfSlider{false};
    int m_sliderDragStartMouseY{0};
    int m_sliderDragStartPage{0};
    int m_sliderHoverPage{0};
    int m_lastMouseY{0};

    // Header Controls
    wxStaticText* m_titleText{nullptr};
    StatusBadge* m_statusBadge{nullptr};

    // Left Column Controls: Only m_dropzonePanel

    wxPanel* m_dropzonePanel{nullptr};
    wxStaticBitmap* m_uploadIconBmp{nullptr};
    wxStaticText* m_dropTextPrimary{nullptr};
    wxStaticText* m_dropTextSecondary{nullptr};

    // Right Column Controls
    CardPanel* m_resultCard{nullptr};

    // Dedicated Progress & Status Bar (独立的进度与状态展示组件)
    OcrProgressPanel* m_progressPanel{nullptr};

    // Bottom Action Bar Buttons
    CustomButton* m_recognizeBtn{nullptr};
    CustomButton* m_stopBtn{nullptr};
    CustomButton* m_translateBtn{nullptr};
    CustomButton* m_openFolderBtn{nullptr};

    std::function<void(const wxString&)> m_onTranslateCallback;
};

class OcrFileDropTarget : public wxFileDropTarget {
public:
    OcrFileDropTarget(OcrView* view) : m_view(view) {}
    bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& filenames) override {
        if (!filenames.IsEmpty() && m_view) {
            m_view->OnImageFileDropped(filenames[0]);
            return true;
        }
        return false;
    }
private:
    OcrView* m_view;
};

} // namespace LinguaAlpaca::UI
