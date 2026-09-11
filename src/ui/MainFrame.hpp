#pragma once
#include <wx/wx.h>
#include <memory>
#include <optional>
#include <functional>
#include "core/ModelManager.hpp"
#include "theme/Theme.hpp"
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
    ~MainFrame() override;

    void NavigateToSettings();
    void CheckAndShowWelcomeDialog();
    void RestoreAndFocus();
    void QuitApplication();

private:
    void InitUI();
    void ApplyTheme();
    void UpdateActiveViewTheme();
    void EnsureViewTheme(wxWindow* view, std::optional<ThemeMode>& appliedTheme, ThemeMode currentTheme, const std::function<void()>& updateFn);
    void OnThemeToggle(wxCommandEvent& event);
    void OnNavChanged(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);

    // 拖动与窗口控制
    void OnHeaderLeftDown(wxMouseEvent& event);
    void OnHeaderLeftUp(wxMouseEvent& event);
    void OnHeaderMouseMove(wxMouseEvent& event);
    void OnHeaderDoubleClick(wxMouseEvent& event);
    void UpdateMaxButtonState();
    bool IsCustomMaximized() const;
    void ToggleMaximize();

#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;
#elif defined(__APPLE__)
    void SetupPlatformWindowMac();
    void ActivateAndBringToFrontMac();
#endif

    std::shared_ptr<ModelManager> m_modelManager;
    std::unique_ptr<class AppTaskBarIcon> m_taskBarIcon;

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

    std::optional<ThemeMode> m_textViewTheme;
    std::optional<ThemeMode> m_ocrViewTheme;
    std::optional<ThemeMode> m_dictViewTheme;
    std::optional<ThemeMode> m_logViewTheme;
    std::optional<ThemeMode> m_settingsViewTheme;

    wxStaticBitmap* m_logoIcon{nullptr};
    wxStaticText* m_appNameText{nullptr};
    wxButton* m_themeBtn{nullptr};
    wxButton* m_minBtn{nullptr};
    wxButton* m_maxBtn{nullptr};
    wxButton* m_closeBtn{nullptr};

    bool m_isDragging{false};
    wxPoint m_dragStartPos;
    bool m_isMaximizedMac{false};
    wxRect m_savedRestoreRect;
};

} // namespace LinguaAlpaca::UI
