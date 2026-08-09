#include "ImagePreviewDialog.hpp"

namespace LinguaAlpaca::Presentation::Components {

ImagePreviewDialog::ImagePreviewDialog(wxWindow* parent, const wxImage& image, const wxString& titleName)
    : wxDialog(parent, wxID_ANY, titleName, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
      m_originalImage(image) {

    wxSize parentSize = parent ? parent->GetSize() : wxSize(1024, 720);
    wxSize dialogSize(std::min(1280, std::max(700, static_cast<int>(parentSize.x * 0.85))),
                      std::min(900, std::max(500, static_cast<int>(parentSize.y * 0.85))));
    SetSize(dialogSize);
    CentreOnParent();

    InitUI();
}

void ImagePreviewDialog::InitUI() {
    SetBackgroundColour(wxColour(20, 22, 28));

    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    auto palette = Theme::ThemeColors::GetCurrentPalette();

    // Custom Top Bar (Supports Window Dragging & Maximize/Minimize)
    m_topBar = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 44));
    m_topBar->SetBackgroundColour(wxColour(15, 17, 21));

    wxBoxSizer* topSizer = new wxBoxSizer(wxHORIZONTAL);

    wxStaticText* titleText = new wxStaticText(m_topBar, wxID_ANY, L"🔍 图片预览 (支持滚轮缩放 / 拖拽平移)");
    titleText->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei"));
    titleText->SetForegroundColour(wxColour(220, 225, 235));

    m_infoText = new wxStaticText(m_topBar, wxID_ANY, L"100%");
    m_infoText->SetFont(wxFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
    m_infoText->SetForegroundColour(wxColour(160, 170, 185));

    m_resetBtn = new CustomButton(m_topBar, wxID_ANY, L"1:1 重置", ButtonStyle::Secondary, wxDefaultPosition, wxSize(76, 28));

    // Window control buttons: Minimize, Maximize/Restore, Close
    m_minBtn = new CustomButton(m_topBar, wxID_ANY, L"", ButtonStyle::Secondary, wxDefaultPosition, wxSize(32, 28));
    m_minBtn->SetIcon(Theme::SVG::MINIMIZE, wxSize(14, 14), palette.textPrimary);
    m_minBtn->SetToolTip(L"最小化");

    m_maxBtn = new CustomButton(m_topBar, wxID_ANY, L"", ButtonStyle::Secondary, wxDefaultPosition, wxSize(32, 28));
    m_maxBtn->SetIcon(Theme::SVG::MAXIMIZE, wxSize(14, 14), palette.textPrimary);
    m_maxBtn->SetToolTip(L"最大化 / 还原");

    m_closeBtn = new CustomButton(m_topBar, wxID_ANY, L"", ButtonStyle::Danger, wxDefaultPosition, wxSize(34, 28));
    m_closeBtn->SetIcon(Theme::SVG::CLOSE, wxSize(14, 14), *wxWHITE);
    m_closeBtn->SetToolTip(L"关闭窗口");

    topSizer->Add(titleText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16);
    topSizer->Add(m_infoText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    topSizer->AddStretchSpacer(1);
    topSizer->Add(m_resetBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    topSizer->Add(m_minBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    topSizer->Add(m_maxBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    topSizer->Add(m_closeBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

    m_topBar->SetSizer(topSizer);
    mainSizer->Add(m_topBar, 0, wxEXPAND);

    // Canvas Panel
    m_canvasPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_canvasPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
    mainSizer->Add(m_canvasPanel, 1, wxEXPAND);

    SetSizer(mainSizer);

    // TopBar Window Dragging Events
    m_topBar->Bind(wxEVT_LEFT_DOWN, &ImagePreviewDialog::OnTopBarMouseDown, this);
    m_topBar->Bind(wxEVT_MOTION, &ImagePreviewDialog::OnTopBarMouseMotion, this);
    m_topBar->Bind(wxEVT_LEFT_UP, &ImagePreviewDialog::OnTopBarMouseUp, this);
    titleText->Bind(wxEVT_LEFT_DOWN, &ImagePreviewDialog::OnTopBarMouseDown, this);
    titleText->Bind(wxEVT_MOTION, &ImagePreviewDialog::OnTopBarMouseMotion, this);
    titleText->Bind(wxEVT_LEFT_UP, &ImagePreviewDialog::OnTopBarMouseUp, this);
    m_infoText->Bind(wxEVT_LEFT_DOWN, &ImagePreviewDialog::OnTopBarMouseDown, this);
    m_infoText->Bind(wxEVT_MOTION, &ImagePreviewDialog::OnTopBarMouseMotion, this);
    m_infoText->Bind(wxEVT_LEFT_UP, &ImagePreviewDialog::OnTopBarMouseUp, this);

    // Canvas Events
    m_canvasPanel->Bind(wxEVT_PAINT, &ImagePreviewDialog::OnPaint, this);
    m_canvasPanel->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        ResetZoom();
        event.Skip();
    });
    m_canvasPanel->Bind(wxEVT_MOUSEWHEEL, &ImagePreviewDialog::OnMouseWheel, this);
    m_canvasPanel->Bind(wxEVT_LEFT_DOWN, &ImagePreviewDialog::OnMouseDown, this);
    m_canvasPanel->Bind(wxEVT_MOTION, &ImagePreviewDialog::OnMouseMotion, this);
    m_canvasPanel->Bind(wxEVT_LEFT_UP, &ImagePreviewDialog::OnMouseUp, this);
    m_canvasPanel->Bind(wxEVT_CHAR_HOOK, &ImagePreviewDialog::OnKeyDown, this);
    Bind(wxEVT_CHAR_HOOK, &ImagePreviewDialog::OnKeyDown, this);

    // Button Events
    m_resetBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ResetZoom(); });
    m_minBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Iconize(true); });
    m_maxBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        Maximize(!IsMaximized());
        ResetZoom();
    });
    m_closeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });

    wxTheApp->CallAfter([this]() { ResetZoom(); });
}

void ImagePreviewDialog::OnTopBarMouseDown(wxMouseEvent& event) {
    if (event.LeftDClick()) {
        Maximize(!IsMaximized());
        ResetZoom();
        return;
    }
    m_isDraggingWindow = true;
    m_windowDragStartPos = event.GetPosition();
    if (!m_topBar->HasCapture()) {
        m_topBar->CaptureMouse();
    }
}

void ImagePreviewDialog::OnTopBarMouseMotion(wxMouseEvent& event) {
    if (m_isDraggingWindow && event.Dragging() && event.LeftIsDown()) {
        wxPoint currentScreenPos = wxGetMousePosition();
        SetPosition(currentScreenPos - m_windowDragStartPos);
    }
}

void ImagePreviewDialog::OnTopBarMouseUp(wxMouseEvent& WXUNUSED(event)) {
    if (m_isDraggingWindow) {
        m_isDraggingWindow = false;
        if (m_topBar->HasCapture()) {
            m_topBar->ReleaseMouse();
        }
    }
}

void ImagePreviewDialog::ResetZoom() {
    if (!m_originalImage.IsOk()) return;

    wxSize clientSize = m_canvasPanel->GetClientSize();
    if (clientSize.x <= 0 || clientSize.y <= 0) return;

    int imgW = m_originalImage.GetWidth();
    int imgH = m_originalImage.GetHeight();
    if (imgW <= 0 || imgH <= 0) return;

    double fitW = static_cast<double>(clientSize.x - 40) / imgW;
    double fitH = static_cast<double>(clientSize.y - 40) / imgH;
    m_fitScale = std::min(fitW, fitH);
    if (m_fitScale <= 0) m_fitScale = 1.0;

    m_scale = m_fitScale;
    m_panOffset = wxPoint2DDouble(0.0, 0.0);

    if (m_infoText) {
        int percent = static_cast<int>(std::round((m_scale / m_fitScale) * 100.0));
        m_infoText->SetLabel(wxString::Format(L"%d%%", percent));
    }

    m_canvasPanel->Refresh();
}

void ImagePreviewDialog::ZoomByFactor(double factor) {
    if (!m_originalImage.IsOk()) return;

    double newScale = std::min(m_fitScale * 10.0, std::max(m_fitScale * 0.1, m_scale * factor));
    if (std::abs(newScale - m_scale) > 1e-4) {
        m_scale = newScale;
        if (m_infoText) {
            int percent = static_cast<int>(std::round((m_scale / m_fitScale) * 100.0));
            m_infoText->SetLabel(wxString::Format(L"%d%%", percent));
        }
        m_canvasPanel->Refresh();
    }
}

wxRect ImagePreviewDialog::GetRenderedImageRect() const {
    if (!m_originalImage.IsOk() || !m_canvasPanel) return wxRect(0, 0, 0, 0);

    wxSize clientSize = m_canvasPanel->GetClientSize();
    int imgW = static_cast<int>(m_originalImage.GetWidth() * m_scale);
    int imgH = static_cast<int>(m_originalImage.GetHeight() * m_scale);

    int drawX = static_cast<int>((clientSize.x - imgW) / 2.0 + m_panOffset.m_x);
    int drawY = static_cast<int>((clientSize.y - imgH) / 2.0 + m_panOffset.m_y);

    return wxRect(drawX, drawY, imgW, imgH);
}

void ImagePreviewDialog::OnPaint(wxPaintEvent& WXUNUSED(event)) {
    wxAutoBufferedPaintDC dc(m_canvasPanel);
    wxSize clientSize = m_canvasPanel->GetClientSize();
    if (clientSize.x <= 0 || clientSize.y <= 0) return;

    dc.SetBackground(wxBrush(wxColour(24, 26, 32)));
    dc.Clear();

    if (!m_originalImage.IsOk()) return;

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;

    wxRect imgRect = GetRenderedImageRect();

    // Draw shadow behind image
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(wxColour(0, 0, 0, 100)));
    gc->DrawRectangle(imgRect.x + 4, imgRect.y + 4, imgRect.width, imgRect.height);

    // Render scaled bitmap
    wxImage scaledImg = m_originalImage;
    if (imgRect.width > 0 && imgRect.height > 0) {
        scaledImg.Rescale(imgRect.width, imgRect.height, wxIMAGE_QUALITY_HIGH);
        wxBitmap bmp(scaledImg);
        gc->DrawBitmap(bmp, imgRect.x, imgRect.y, imgRect.width, imgRect.height);
    }
}

void ImagePreviewDialog::OnMouseWheel(wxMouseEvent& event) {
    if (!m_originalImage.IsOk()) return;

    int rotation = event.GetWheelRotation();
    if (rotation == 0) return;

    double factor = (rotation > 0) ? 1.15 : (1.0 / 1.15);
    double newScale = std::min(m_fitScale * 10.0, std::max(m_fitScale * 0.1, m_scale * factor));

    if (std::abs(newScale - m_scale) > 1e-4) {
        wxPoint mousePos = event.GetPosition();
        wxSize clientSize = m_canvasPanel->GetClientSize();
        wxPoint2DDouble center(clientSize.x / 2.0, clientSize.y / 2.0);

        double scaleRatio = newScale / m_scale;
        m_panOffset.m_x = (m_panOffset.m_x + center.m_x - mousePos.x) * scaleRatio + mousePos.x - center.m_x;
        m_panOffset.m_y = (m_panOffset.m_y + center.m_y - mousePos.y) * scaleRatio + mousePos.y - center.m_y;

        m_scale = newScale;

        if (m_infoText) {
            int percent = static_cast<int>(std::round((m_scale / m_fitScale) * 100.0));
            m_infoText->SetLabel(wxString::Format(L"%d%%", percent));
        }

        m_canvasPanel->Refresh();
    }
}

void ImagePreviewDialog::OnMouseDown(wxMouseEvent& event) {
    m_isDragging = true;
    m_lastMousePos = event.GetPosition();
    m_canvasPanel->CaptureMouse();
}

void ImagePreviewDialog::OnMouseMotion(wxMouseEvent& event) {
    if (m_isDragging && event.Dragging() && event.LeftIsDown()) {
        wxPoint currentPos = event.GetPosition();
        wxPoint delta = currentPos - m_lastMousePos;
        m_panOffset.m_x += delta.x;
        m_panOffset.m_y += delta.y;
        m_lastMousePos = currentPos;
        m_canvasPanel->Refresh();
    }
}

void ImagePreviewDialog::OnMouseUp(wxMouseEvent& WXUNUSED(event)) {
    if (m_isDragging) {
        m_isDragging = false;
        if (m_canvasPanel->HasCapture()) {
            m_canvasPanel->ReleaseMouse();
        }
    }
}

void ImagePreviewDialog::OnKeyDown(wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_ESCAPE) {
        EndModal(wxID_CANCEL);
    } else {
        event.Skip();
    }
}

} // namespace LinguaAlpaca::Presentation::Components
