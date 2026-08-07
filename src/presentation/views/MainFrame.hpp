#pragma once
#include <wx/wx.h>
#include <memory>
#include "../../application/service/TranslationService.hpp"
#include "../../application/service/OcrService.hpp"
#include "../components/SidebarNav.hpp"
#include "TextTranslationView.hpp"
#include "OcrTranslationView.hpp"
#include "SettingsView.hpp"
#include "PlaceholderView.hpp"

namespace LinguaAlpaca::Presentation::Views {

class MainFrame : public wxFrame {
public:
    MainFrame(std::shared_ptr<Application::Service::TranslationService> translationService,
              std::shared_ptr<Application::Service::OcrService> ocrService = nullptr);

    void NavigateToSettings();

private:
    void InitUI();
    void OnThemeToggle(wxCommandEvent& event);
    void OnNavChanged(wxCommandEvent& event);

    // 拖动与窗口控制
    void OnHeaderLeftDown(wxMouseEvent& event);
    void OnHeaderLeftUp(wxMouseEvent& event);
    void OnHeaderMouseMove(wxMouseEvent& event);
    void OnHeaderDoubleClick(wxMouseEvent& event);

    std::shared_ptr<Application::Service::TranslationService> m_translationService;
    std::shared_ptr<Application::Service::OcrService> m_ocrService;

    // UI Elements
    wxPanel* m_topHeaderPanel{nullptr};
    Components::SidebarNav* m_sidebar{nullptr};
    wxPanel* m_contentContainer{nullptr};
    wxBoxSizer* m_contentSizer{nullptr};

    TextTranslationView* m_textView{nullptr};
    OcrTranslationView* m_ocrView{nullptr};
    PlaceholderView* m_historyView{nullptr};
    SettingsView* m_settingsView{nullptr};

    wxStaticText* m_appNameText{nullptr};
    wxButton* m_themeBtn{nullptr};
    wxButton* m_minBtn{nullptr};
    wxButton* m_maxBtn{nullptr};
    wxButton* m_closeBtn{nullptr};

    bool m_isDragging{false};
    wxPoint m_dragStartPos;
};

} // namespace LinguaAlpaca::Presentation::Views
