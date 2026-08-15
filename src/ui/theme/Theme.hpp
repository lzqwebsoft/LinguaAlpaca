#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include <functional>
#include <vector>

namespace LinguaAlpaca::UI {

enum class ThemeMode {
    Light,
    Dark
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
    
    void SetTheme(ThemeMode theme) {
        if (m_currentTheme != theme) {
            m_currentTheme = theme;
            NotifyCallbacks();
        }
    }

    void ToggleTheme() {
        SetTheme(m_currentTheme == ThemeMode::Light ? ThemeMode::Dark : ThemeMode::Light);
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
    std::vector<ThemeChangedCallback> m_callbacks;
};

using ThemeColors = ThemeManager;

} // namespace LinguaAlpaca::UI
