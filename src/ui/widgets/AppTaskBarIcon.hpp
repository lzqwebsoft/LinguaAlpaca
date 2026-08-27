#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include <wx/taskbar.h>

namespace LinguaAlpaca::UI {

class MainFrame;

class AppTaskBarIcon : public wxTaskBarIcon {
public:
    explicit AppTaskBarIcon(MainFrame* mainFrame);
    ~AppTaskBarIcon() override = default;

    wxMenu* CreatePopupMenu() override;

private:
    void OnLeftClick(wxTaskBarIconEvent& event);
    void OnShowMain(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);

    MainFrame* m_mainFrame{nullptr};

    enum {
        ID_TRAY_SHOW = wxID_HIGHEST + 1001,
        ID_TRAY_EXIT
    };
};

} // namespace LinguaAlpaca::UI
