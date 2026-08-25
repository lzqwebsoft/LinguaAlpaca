#pragma once
#include <wx/wx.h>
#include <memory>
#include "core/ModelManager.hpp"
#include "widgets/SidebarNav.hpp"
#include "TextView.hpp"
#include "OcrView.hpp"
#include "DictView.hpp"
#include "LogView.hpp"
#include "SettingsView.hpp"

namespace LinguaAlpaca::UI {

class MainFrame : public wxFrame {
public:
    explicit MainFrame(std::shared_ptr<ModelManager> modelManager);
    ~MainFrame() override = default;

    void NavigateToSettings();

private:
    void InitUI();
    void OnThemeToggle(wxCommandEvent& event);
    void OnNavChanged(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);

    // 拖动与窗口控制
    void OnHeaderLeftDown(wxMouseEvent& event);
    void OnHeaderLeftUp(wxMouseEvent& event);
    void OnHeaderMouseMove(wxMouseEvent& event);
    void OnHeaderDoubleClick(wxMouseEvent& event);
    void UpdateMaxButtonState();

#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

    std::shared_ptr<ModelManager> m_modelManager;

    // UI Elements
    wxPanel* m_topHeaderPanel{nullptr};
    SidebarNav* m_sidebar{nullptr};
    wxPanel* m_contentContainer{nullptr};
    wxBoxSizer* m_contentSizer{nullptr};

    TextView* m_textView{nullptr};
    OcrView* m_ocrView{nullptr};
    DictView* m_dictView{nullptr};
    LogView* m_logView{nullptr};
    SettingsView* m_settingsView{nullptr};

    wxStaticBitmap* m_logoIcon{nullptr};
    wxStaticText* m_appNameText{nullptr};
    wxButton* m_themeBtn{nullptr};
    wxButton* m_minBtn{nullptr};
    wxButton* m_maxBtn{nullptr};
    wxButton* m_closeBtn{nullptr};

    bool m_isDragging{false};
    wxPoint m_dragStartPos;
};

} // namespace LinguaAlpaca::UI
