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
#include "widgets/CustomChoice.hpp"
#include "widgets/ImagePreviewDialog.hpp"
#include "widgets/TextCtrl.hpp"
#include "widgets/StatusBadge.hpp"

namespace LinguaAlpaca::UI {

enum class OcrTaskState {
    Idle,
    Recognizing,
    Translating
};

enum class DropzoneHoverAction {
    None,
    Preview,
    Replace
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

private:
    void InitUI();
    void OpenImageDialog();
    void OpenImagePreview();
    void OnSelectImageClicked(wxMouseEvent& event);
    void OnPreviewClicked(wxCommandEvent& event);
    void OnReplaceClicked(wxCommandEvent& event);
    void OnDropzoneMouseEnter(wxMouseEvent& event);
    void OnDropzoneMouseLeave(wxMouseEvent& event);
    void OnDropzoneMouseMove(wxMouseEvent& event);
    void OnDropzoneLeftDown(wxMouseEvent& event);
    void OnRecognizeClicked(wxCommandEvent& event);
    void OnStopClicked(wxCommandEvent& event);

    void SetState(OcrTaskState state);
    void LoadImageFile(const wxString& filePath);
    void UpdateDropzoneUI();
    std::string GetSelectedTaskType() const;
    void DoExecuteOcr(const std::string& imgPath, const std::string& taskType);

    std::shared_ptr<ModelManager> m_modelManager;
    wxTimer m_healthTimer;

    OcrTaskState m_currentState{OcrTaskState::Idle};

    wxString m_loadedImagePath;
    wxString m_imageFileName;
    wxImage m_loadedImage;

    // Dropzone hover & action states
    bool m_isDropzoneHovered{false};
    DropzoneHoverAction m_hoveredAction{DropzoneHoverAction::None};
    wxRect m_previewBtnRect;
    wxRect m_centerBtnRect;

    // Header Controls
    wxStaticText* m_titleText{nullptr};
    StatusBadge* m_statusBadge{nullptr};

    // Left Column Controls
    wxPanel* m_leftControlPanel{nullptr};
    wxStaticText* m_typeLabel{nullptr};
    CustomChoice* m_typeChoice{nullptr};

    wxPanel* m_dropzonePanel{nullptr};
    wxStaticBitmap* m_uploadIconBmp{nullptr};
    wxStaticText* m_dropTextPrimary{nullptr};
    wxStaticText* m_dropTextSecondary{nullptr};

    // Right Column Controls
    CardPanel* m_resultCard{nullptr};

    // Bottom Action Bar Buttons
    CustomButton* m_recognizeBtn{nullptr};
    CustomButton* m_stopBtn{nullptr};
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
