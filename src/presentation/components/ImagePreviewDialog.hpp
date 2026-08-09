#pragma once
#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include "../theme/ThemeColors.hpp"
#include "../theme/IconManager.hpp"
#include "../components/CustomButton.hpp"

namespace LinguaAlpaca::Presentation::Components {

class ImagePreviewDialog : public wxDialog {
public:
    ImagePreviewDialog(wxWindow* parent, const wxImage& image, const wxString& titleName = L"图片预览");

private:
    void InitUI();
    void OnPaint(wxPaintEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnMouseDown(wxMouseEvent& event);
    void OnMouseMotion(wxMouseEvent& event);
    void OnMouseUp(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void ResetZoom();
    void ZoomByFactor(double factor);
    wxRect GetRenderedImageRect() const;

    void OnTopBarMouseDown(wxMouseEvent& event);
    void OnTopBarMouseMotion(wxMouseEvent& event);
    void OnTopBarMouseUp(wxMouseEvent& event);

    wxImage m_originalImage;
    wxBitmap m_cachedBitmap;

    double m_scale{1.0};
    double m_fitScale{1.0};
    wxPoint2DDouble m_panOffset{0.0, 0.0};

    bool m_isDragging{false};
    wxPoint m_lastMousePos;

    bool m_isDraggingWindow{false};
    wxPoint m_windowDragStartPos;

    wxPanel* m_topBar{nullptr};
    wxPanel* m_canvasPanel{nullptr};
    CustomButton* m_resetBtn{nullptr};
    CustomButton* m_minBtn{nullptr};
    CustomButton* m_maxBtn{nullptr};
    CustomButton* m_closeBtn{nullptr};
    wxStaticText* m_infoText{nullptr};
};

} // namespace LinguaAlpaca::Presentation::Components
