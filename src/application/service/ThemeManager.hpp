#pragma once
#include <functional>
#include <vector>
#include "../../domain/model/AppTheme.hpp"

namespace LinguaAlpaca::Application::Service {

using ThemeChangedCallback = std::function<void(Domain::Model::AppThemeMode)>;

class ThemeManager {
public:
    static ThemeManager& GetInstance() {
        static ThemeManager instance;
        return instance;
    }

    Domain::Model::AppThemeMode GetCurrentTheme() const { return m_currentTheme; }
    void SetTheme(Domain::Model::AppThemeMode theme) {
        if (m_currentTheme != theme) {
            m_currentTheme = theme;
            NotifyCallbacks();
        }
    }

    void ToggleTheme() {
        SetTheme(m_currentTheme == Domain::Model::AppThemeMode::Light ? 
            Domain::Model::AppThemeMode::Dark : Domain::Model::AppThemeMode::Light);
    }

    void RegisterCallback(ThemeChangedCallback cb) {
        m_callbacks.push_back(cb);
    }

private:
    ThemeManager() : m_currentTheme(Domain::Model::AppThemeMode::Light) {}
    
    void NotifyCallbacks() {
        for (const auto& cb : m_callbacks) {
            if (cb) cb(m_currentTheme);
        }
    }

    Domain::Model::AppThemeMode m_currentTheme;
    std::vector<ThemeChangedCallback> m_callbacks;
};

} // namespace LinguaAlpaca::Application::Service
