#pragma once
#include <wx/wx.h>
#include <wx/dnd.h>
#include <wx/timer.h>
#include <memory>
#include <thread>
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
    void DoExecuteDocumentPipeline(const std::string& docPath);
    void ExportMarkdown();
    void ExportJson();
    void OpenOutputDir();

    void SetState(OcrTaskState state);
    void LoadImageFile(const wxString& filePath);
    bool PasteImageFromClipboard();
    void ShowDropzoneContextMenu(const wxPoint& pos);
    void UpdateDropzoneUI();

    std::shared_ptr<ModelManager> m_modelManager;
    wxTimer m_healthTimer;

    OcrTaskState m_currentState{OcrTaskState::Idle};

    wxString m_loadedImagePath;
    wxString m_imageFileName;
    wxImage m_loadedImage;

    bool m_isPdfDoc{false};
    int m_pdfTotalPages{0};
    int m_pdfCurrentPage{0};
    wxString m_lastMarkdownResult;
    wxString m_lastJsonResult;
    wxString m_lastOutputDir;

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
