#pragma once
#include <wx/wx.h>
#include <wx/dnd.h>
#include <wx/timer.h>
#include <memory>
#include <thread>
#include <atomic>
#include "core/ModelManager.hpp"
#include "widgets/CustomButton.hpp"
#include "widgets/ImagePreviewDialog.hpp"
#include "widgets/TextCtrl.hpp"

namespace LinguaAlpaca::UI {

enum class OcrTaskState {
    Idle,
    Recognizing,
    Translating
};

class OcrView : public wxPanel {
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
    void OnSelectImageClicked(wxMouseEvent& event);
    void OnPreviewClicked(wxCommandEvent& event);
    void OnReplaceClicked(wxCommandEvent& event);
    void OnDropzoneMouseEnter(wxMouseEvent& event);
    void OnDropzoneMouseLeave(wxMouseEvent& event);
    void OnRecognizeClicked(wxCommandEvent& event);
    void OnStopClicked(wxCommandEvent& event);
    void OnClearClicked(wxCommandEvent& event);
    void OnCopyClicked(wxCommandEvent& event);

    void SetState(OcrTaskState state);
    void LoadImageFile(const wxString& filePath);
    void UpdateDropzoneUI();
    std::string GetSelectedTaskType() const;

    std::shared_ptr<ModelManager> m_modelManager;
    wxTimer m_healthTimer;

    OcrTaskState m_currentState{OcrTaskState::Idle};

    wxString m_loadedImagePath;
    wxString m_imageFileName;
    wxImage m_loadedImage;

    // Header Controls
    wxStaticText* m_titleText{nullptr};
    wxPanel* m_statusBadge{nullptr};
    wxStaticText* m_badgeText{nullptr};

    // Left Column Controls
    wxPanel* m_leftControlPanel{nullptr};
    wxStaticText* m_typeLabel{nullptr};
    wxChoice* m_typeChoice{nullptr};

    wxPanel* m_dropzonePanel{nullptr};
    wxPanel* m_topOverlayBar{nullptr};
    CustomButton* m_previewBtn{nullptr};
    CustomButton* m_replaceBtn{nullptr};
    CustomButton* m_centerUploadBtn{nullptr};
    wxStaticBitmap* m_uploadIconBmp{nullptr};
    wxStaticText* m_dropTextPrimary{nullptr};
    wxStaticText* m_dropTextSecondary{nullptr};

    // Right Column Controls
    wxPanel* m_resultCard{nullptr};
    wxStaticText* m_resultTitle{nullptr};
    TextCtrl* m_resultTextCtrl{nullptr};
    wxStaticText* m_charCountText{nullptr};
    CustomButton* m_copyBtn{nullptr};
    CustomButton* m_clearBtn{nullptr};

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
