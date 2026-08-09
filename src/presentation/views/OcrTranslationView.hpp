#pragma once
#include <wx/wx.h>
#include <wx/dnd.h>
#include <memory>
#include <thread>
#include <atomic>
#include "../../application/service/TranslationService.hpp"
#include "../../application/service/OcrService.hpp"
#include "../components/LanguageSelectorBar.hpp"
#include "../components/CustomButton.hpp"
#include "../components/CardPanel.hpp"
#include "../components/ImagePreviewDialog.hpp"

namespace LinguaAlpaca::Presentation::Views {

enum class OcrTaskState {
    Idle,
    Recognizing,
    Translating
};

class OcrTranslationView : public wxPanel {
public:
    OcrTranslationView(wxWindow* parent,
                       std::shared_ptr<Application::Service::TranslationService> translationService,
                       std::shared_ptr<Application::Service::OcrService> ocrService,
                       wxWindowID id = wxID_ANY);

    ~OcrTranslationView() override;

    void UpdateTheme();
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
    void UpdateStatusBadge();
    void UpdateDropzoneUI();
    std::string GetSelectedTaskType() const;

    std::shared_ptr<Application::Service::TranslationService> m_translationService;
    std::shared_ptr<Application::Service::OcrService> m_ocrService;

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
    Components::CustomButton* m_previewBtn{nullptr};
    Components::CustomButton* m_replaceBtn{nullptr};
    Components::CustomButton* m_centerUploadBtn{nullptr};
    wxStaticBitmap* m_uploadIconBmp{nullptr};
    wxStaticText* m_dropTextPrimary{nullptr};
    wxStaticText* m_dropTextSecondary{nullptr};

    // Right Column Controls (Recognized Text Card)
    wxPanel* m_resultCard{nullptr};
    wxStaticText* m_resultTitle{nullptr};
    wxTextCtrl* m_resultTextCtrl{nullptr};
    wxStaticText* m_charCountText{nullptr};
    Components::CustomButton* m_copyBtn{nullptr};
    Components::CustomButton* m_clearBtn{nullptr};

    // Bottom Action Bar Buttons
    Components::CustomButton* m_recognizeBtn{nullptr};
    Components::CustomButton* m_stopBtn{nullptr};
};

// Custom Drop Target for image files
class OcrFileDropTarget : public wxFileDropTarget {
public:
    OcrFileDropTarget(OcrTranslationView* view) : m_view(view) {}
    bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames) override {
        if (!filenames.IsEmpty() && m_view) {
            m_view->OnImageFileDropped(filenames[0]);
            return true;
        }
        return false;
    }
private:
    OcrTranslationView* m_view;
};

} // namespace LinguaAlpaca::Presentation::Views
