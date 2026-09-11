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
    wxBitmapBundle bundle = IconManager::GetAppStatusBarBundle();
    if (bundle.IsOk()) {
        SetIcon(bundle, L"译灵驼 · LinguaAlpaca");
    } else {
        wxIcon icon = IconManager::GetAppIcon(wxSize(32, 32));
        if (icon.IsOk()) {
            SetIcon(icon, L"译灵驼 · LinguaAlpaca");
        }
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
    if (m_mainFrame && wxTheApp) {
        wxTheApp->CallAfter([this]() {
            if (m_mainFrame) {
                m_mainFrame->RestoreAndFocus();
            }
        });
    }
}

void AppTaskBarIcon::OnShowMain(wxCommandEvent& WXUNUSED(event)) {
    if (m_mainFrame && wxTheApp) {
        // 使用 CallAfter 确保在状态栏菜单完全关闭退出跟踪之后再执行置顶激活，
        // 彻底消除 macOS 菜单关闭与主窗口前台抢占的时序冲突
        wxTheApp->CallAfter([this]() {
            if (m_mainFrame) {
                m_mainFrame->RestoreAndFocus();
            }
        });
    }
}

void AppTaskBarIcon::OnExit(wxCommandEvent& WXUNUSED(event)) {
    if (m_mainFrame) {
        m_mainFrame->QuitApplication();
    }
}

} // namespace LinguaAlpaca::UI
