#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include <wx/settings.h>
#include "Dpi.hpp"
#include <functional>
#include <vector>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace LinguaAlpaca::UI {

enum class ThemeMode {
    Light,
    Dark
};

enum class ThemePreference {
    Light,
    Dark,
    System
};

struct ThemePalette {
    wxColour windowBg;
    wxColour sidebarBg;
    wxColour cardBg;
    wxColour cardBorder;
    wxColour cardBorderActive;
    wxColour textPrimary;
    wxColour textSecondary;
    wxColour accentPrimary;
    wxColour accentHover;
    wxColour accentGreen;
    wxColour bannerBg;
    wxColour bannerBorder;
    wxColour bannerText;
    wxColour badgeBg;
    wxColour badgeText;
};

using ThemeChangedCallback = std::function<void(ThemeMode)>;

class ThemeManager {
public:
    static ThemeManager& GetInstance() {
        static ThemeManager instance;
        return instance;
    }

    ThemeMode GetCurrentTheme() const { return m_currentTheme; }
    ThemePreference GetPreference() const { return m_preference; }

    static ThemeMode DetectSystemTheme() {
#ifdef _WIN32
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD appsUseLightTheme = 1;
            DWORD size = sizeof(appsUseLightTheme);
            DWORD type = REG_DWORD;
            if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, &type, (LPBYTE)&appsUseLightTheme, &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                return (appsUseLightTheme == 0) ? ThemeMode::Dark : ThemeMode::Light;
            }
            RegCloseKey(hKey);
        }
#endif
        return wxSystemSettings::GetAppearance().IsDark() ? ThemeMode::Dark : ThemeMode::Light;
    }

    void SetTheme(ThemeMode theme) {
        if (m_currentTheme != theme) {
            m_currentTheme = theme;
            NotifyCallbacks();
        }
    }

    void SetPreference(ThemePreference pref) {
        m_preference = pref;
        ThemeMode targetMode = ThemeMode::Light;
        if (pref == ThemePreference::Light) {
            targetMode = ThemeMode::Light;
        } else if (pref == ThemePreference::Dark) {
            targetMode = ThemeMode::Dark;
        } else {
            targetMode = DetectSystemTheme();
        }
        SetTheme(targetMode);
    }

    void SetPreferenceByString(const std::string& prefStr) {
        if (prefStr == "Dark") {
            SetPreference(ThemePreference::Dark);
        } else if (prefStr == "System") {
            SetPreference(ThemePreference::System);
        } else {
            SetPreference(ThemePreference::Light);
        }
    }

    std::string GetPreferenceString() const {
        switch (m_preference) {
        case ThemePreference::Dark: return "Dark";
        case ThemePreference::System: return "System";
        case ThemePreference::Light:
        default: return "Light";
        }
    }

    void ToggleTheme() {
        if (m_preference == ThemePreference::Light) {
            SetPreference(ThemePreference::Dark);
        } else if (m_preference == ThemePreference::Dark) {
            SetPreference(ThemePreference::Light);
        } else {
            SetPreference(m_currentTheme == ThemeMode::Light ? ThemePreference::Dark : ThemePreference::Light);
        }
    }

    void RegisterCallback(ThemeChangedCallback cb) {
        m_callbacks.push_back(cb);
    }

    static ThemePalette GetPalette(ThemeMode mode) {
        ThemePalette p;
        if (mode == ThemeMode::Light) {
            p.windowBg         = wxColour(248, 250, 252); // #F8FAFC
            p.sidebarBg        = wxColour(255, 255, 255); // #FFFFFF
            p.cardBg           = wxColour(255, 255, 255); // #FFFFFF
            p.cardBorder       = wxColour(226, 232, 240); // #E2E8F0
            p.cardBorderActive = wxColour(96, 165, 250);  // #60A5FA
            p.textPrimary      = wxColour(30, 41, 59);    // #1E293B
            p.textSecondary    = wxColour(100, 116, 139); // #64748B
            p.accentPrimary    = wxColour(59, 130, 246);  // #3B82F6
            p.accentHover      = wxColour(37, 99, 235);   // #2563EB
            p.accentGreen      = wxColour(34, 197, 94);   // #22C55E
            p.bannerBg         = wxColour(239, 246, 255); // #EFF6FF
            p.bannerBorder     = wxColour(191, 219, 254); // #BFDBFE
            p.bannerText       = wxColour(30, 64, 175);   // #1E40AF
            p.badgeBg          = wxColour(240, 253, 244); // #F0FDF4
            p.badgeText        = wxColour(22, 101, 52);   // #166534
        } else {
            p.windowBg         = wxColour(15, 23, 42);    // #0F172A
            p.sidebarBg        = wxColour(30, 41, 59);    // #1E293B
            p.cardBg           = wxColour(30, 41, 59);    // #1E293B
            p.cardBorder       = wxColour(51, 65, 85);    // #334155
            p.cardBorderActive = wxColour(96, 165, 250);  // #60A5FA
            p.textPrimary      = wxColour(248, 250, 252); // #F8FAFC
            p.textSecondary    = wxColour(148, 163, 184); // #94A3B8
            p.accentPrimary    = wxColour(59, 130, 246);  // #3B82F6
            p.accentHover      = wxColour(96, 165, 250);  // #60A5FA
            p.accentGreen      = wxColour(34, 197, 94);   // #22C55E
            p.bannerBg         = wxColour(30, 58, 138);   // #1E3A8A
            p.bannerBorder     = wxColour(59, 130, 246);  // #3B82F6
            p.bannerText       = wxColour(219, 234, 254); // #DBEAFE
            p.badgeBg          = wxColour(20, 83, 45);    // #14532D
            p.badgeText        = wxColour(187, 247, 208); // #BBF7D0
        }
        return p;
    }

    static ThemePalette GetCurrentPalette() {
        return GetPalette(GetInstance().GetCurrentTheme());
    }

private:
    ThemeManager() : m_currentTheme(ThemeMode::Light) {}
    
    void NotifyCallbacks() {
        for (const auto& cb : m_callbacks) {
            if (cb) cb(m_currentTheme);
        }
    }

    ThemeMode m_currentTheme{ThemeMode::Light};
    ThemePreference m_preference{ThemePreference::Light};
    std::vector<ThemeChangedCallback> m_callbacks;
};

using ThemeColors = ThemeManager;

} // namespace LinguaAlpaca::UI
