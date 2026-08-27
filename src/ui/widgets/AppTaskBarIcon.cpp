#pragma execution_character_set("utf-8")
#include "AppTaskBarIcon.hpp"
#include "../MainFrame.hpp"
#include "../theme/IconManager.hpp"

#ifdef __WXMSW__
#include <windows.h>
#endif

namespace LinguaAlpaca::UI {

AppTaskBarIcon::AppTaskBarIcon(MainFrame* mainFrame)
    : m_mainFrame(mainFrame) {
    wxIcon icon = IconManager::GetAppIcon(wxSize(16, 16));
    if (icon.IsOk()) {
        SetIcon(icon, L"译灵驼 · LinguaAlpaca");
    }

    Bind(wxEVT_TASKBAR_LEFT_UP, &AppTaskBarIcon::OnLeftClick, this);
    Bind(wxEVT_TASKBAR_LEFT_DCLICK, &AppTaskBarIcon::OnLeftClick, this);
    Bind(wxEVT_MENU, &AppTaskBarIcon::OnShowMain, this, ID_TRAY_SHOW);
    Bind(wxEVT_MENU, &AppTaskBarIcon::OnExit, this, ID_TRAY_EXIT);
}

wxMenu* AppTaskBarIcon::CreatePopupMenu() {
    wxMenu* menu = new wxMenu();
    menu->Append(ID_TRAY_SHOW, L"显示主界面");
    menu->AppendSeparator();
    menu->Append(ID_TRAY_EXIT, L"退出");
    return menu;
}

void AppTaskBarIcon::OnLeftClick(wxTaskBarIconEvent& WXUNUSED(event)) {
    if (m_mainFrame) {
        m_mainFrame->RestoreAndFocus();
    }
}

void AppTaskBarIcon::OnShowMain(wxCommandEvent& WXUNUSED(event)) {
    if (m_mainFrame) {
        m_mainFrame->RestoreAndFocus();
    }
}

void AppTaskBarIcon::OnExit(wxCommandEvent& WXUNUSED(event)) {
    if (m_mainFrame) {
        m_mainFrame->QuitApplication();
    }
}

} // namespace LinguaAlpaca::UI
