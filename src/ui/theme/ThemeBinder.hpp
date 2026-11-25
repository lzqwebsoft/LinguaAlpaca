#pragma once
#pragma execution_character_set("utf-8")

#include "IThemeable.hpp"
#include "IconManager.hpp"
#include "PlatformThemeHelper.hpp"
#include "Theme.hpp"
#include <wx/hyperlink.h>
#include <wx/weakref.h>
#include <wx/wx.h>
#include <functional>
#include <vector>

namespace LinguaAlpaca::UI {

enum class TextRole {
    Primary,
    Secondary,
    Accent
};

enum class BgRole {
    Window,
    Card
};

/**
 * @brief 声明式主题绑定器
 *
 * 在 InitUI() 创建原生控件旁声明其语义角色，主题切换时通过 Apply()
 * 统一完成判空、着色、图标重绘以及子 IThemeable 控件树的递归更新。
 */
class ThemeBinder {
public:
    template <typename... Ts>
    void BindText(TextRole role, Ts*... ctrls) {
        (BindTextSingle(role, ctrls), ...);
    }

    template <typename... Ts>
    void BindBg(BgRole role, bool refresh, Ts*... ctrls) {
        (BindBgSingle(role, refresh, ctrls), ...);
    }

    template <typename... Ts>
    void BindTextBg(TextRole fgRole, BgRole bgRole, bool refresh, Ts*... ctrls) {
        (BindTextBgSingle(fgRole, bgRole, refresh, ctrls), ...);
    }

    void BindIcon(wxStaticBitmap* bmp, const char* svgContent, const wxSize& sizeDip, TextRole role = TextRole::Accent) {
        if (!bmp || !svgContent)
            return;
        wxWeakRef<wxStaticBitmap> ref(bmp);
        m_bindings.push_back([ref, svgContent, sizeDip, role](const ThemePalette& p) {
            if (wxStaticBitmap* target = ref.get()) {
                target->SetBitmap(IconManager::GetIconBundle(svgContent, sizeDip, ResolveTextColour(p, role)));
            }
        });
    }

    template <typename... Ts>
    void BindLink(Ts*... links) {
        (BindLinkSingle(links), ...);
    }

    template <typename... Ts>
    void BindNative(Ts*... ctrls) {
        (BindNativeSingle(ctrls), ...);
    }

    void Apply(wxWindow* root, const ThemePalette& palette) const {
        for (const auto& fn : m_bindings) {
            if (fn) {
                fn(palette);
            }
        }
        if (root) {
            UpdateChildrenRecursive(root);
        }
    }

    static void UpdateChildrenRecursive(wxWindow* parent) {
        if (!parent)
            return;
        for (wxWindowList::compatibility_iterator node = parent->GetChildren().GetFirst(); node; node = node->GetNext()) {
            wxWindow* child = node->GetData();
            if (!child)
                continue;
            if (auto* themeable = dynamic_cast<IThemeable*>(child)) {
                themeable->UpdateTheme();
            } else if (!child->GetChildren().IsEmpty()) {
                UpdateChildrenRecursive(child);
            }
        }
    }

private:
    static wxColour ResolveTextColour(const ThemePalette& p, TextRole role) {
        switch (role) {
        case TextRole::Secondary: return p.textSecondary;
        case TextRole::Accent:    return p.accentPrimary;
        case TextRole::Primary:
        default:                  return p.textPrimary;
        }
    }

    static wxColour ResolveBgColour(const ThemePalette& p, BgRole role) {
        switch (role) {
        case BgRole::Card:   return p.cardBg;
        case BgRole::Window:
        default:             return p.windowBg;
        }
    }

    void BindTextSingle(TextRole role, wxWindow* ctrl) {
        if (!ctrl)
            return;
        wxWeakRef<wxWindow> ref(ctrl);
        m_bindings.push_back([ref, role](const ThemePalette& p) {
            if (wxWindow* target = ref.get()) {
                target->SetForegroundColour(ResolveTextColour(p, role));
            }
        });
    }

    void BindBgSingle(BgRole role, bool refresh, wxWindow* ctrl) {
        if (!ctrl)
            return;
        wxWeakRef<wxWindow> ref(ctrl);
        m_bindings.push_back([ref, role, refresh](const ThemePalette& p) {
            if (wxWindow* target = ref.get()) {
                target->SetBackgroundColour(ResolveBgColour(p, role));
                if (refresh) {
                    target->Refresh();
                }
            }
        });
    }

    void BindTextBgSingle(TextRole fgRole, BgRole bgRole, bool refresh, wxWindow* ctrl) {
        if (!ctrl)
            return;
        wxWeakRef<wxWindow> ref(ctrl);
        m_bindings.push_back([ref, fgRole, bgRole, refresh](const ThemePalette& p) {
            if (wxWindow* target = ref.get()) {
                target->SetForegroundColour(ResolveTextColour(p, fgRole));
                target->SetBackgroundColour(ResolveBgColour(p, bgRole));
                if (refresh) {
                    target->Refresh();
                }
            }
        });
    }

    void BindLinkSingle(wxHyperlinkCtrl* link) {
        if (!link)
            return;
        wxWeakRef<wxHyperlinkCtrl> ref(link);
        m_bindings.push_back([ref](const ThemePalette& p) {
            if (wxHyperlinkCtrl* target = ref.get()) {
                target->SetNormalColour(p.accentPrimary);
                target->SetHoverColour(p.accentHover);
            }
        });
    }

    void BindNativeSingle(wxWindow* ctrl) {
        if (!ctrl)
            return;
        wxWeakRef<wxWindow> ref(ctrl);
        m_bindings.push_back([ref](const ThemePalette& p) {
            if (wxWindow* target = ref.get()) {
                PlatformThemeHelper::ApplyControlTheme(target, p);
            }
        });
    }

    std::vector<std::function<void(const ThemePalette&)>> m_bindings;
};

} // namespace LinguaAlpaca::UI
