#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include <wx/font.h>
#include <wx/settings.h>

namespace LinguaAlpaca::UI {

enum class FontRole {
    DisplayTitle, // 界面主标题 (Win 18pt Bold / Mac 21pt Bold)
    SectionTitle, // 分组/对话框主标题 (Win 14pt Bold / Mac 17pt Bold)
    CardTitle,    // 卡片小标题 (Win 11pt Bold / Mac 15pt Bold)
    WindowTitle,  // 悬浮窗/窗口工具栏标题 (Win 10pt Bold / Mac 14pt Bold)
    Body,         // 默认正文/编辑区域 (Win 10pt Normal / Mac 14pt Normal)
    Control,      // 标准控件/按钮/下拉选项/表格 (Win 9pt / Mac 13pt)
    Badge,        // 状态药丸/胶囊标签 (Win 9pt Bold / Mac 13pt Bold)
    Caption,      // 辅助提示/折叠预览/底部状态 (Win 8pt / Mac 11pt)
    Code,         // 等宽代码/控制台日志 (Win 9pt / Mac 12pt Mono)
};

class ThemeFont {
public:
    // 获取当前平台首选中文字体名称
    static wxString GetDefaultFamily() {
#if defined(__APPLE__)
        return "PingFang SC";
#elif defined(_WIN32)
        return "Microsoft YaHei";
#else
        return "Sans";
#endif
    }

    // 获取当前平台首选等宽字体名称
    static wxString GetDefaultMonoFamily() {
#if defined(__APPLE__)
        return "Menlo";
#elif defined(_WIN32)
        return "Consolas";
#else
        return "monospace";
#endif
    }

    // 将 Windows 基准字号映射校准为适合当前平台的字号
    static int CalibratePtSize(int winPtSize) {
#if defined(__APPLE__)
        // macOS HIG 排版规范与 Retina 屏幕全局字号校准：
        // Win 8pt (Caption 辅助提示) -> 11pt (+3pt, 对齐 [NSFont smallSystemFontSize] 11pt)
        // Win 9pt (Control 按钮/选项/表格) -> 13pt (+4pt, 对齐 [NSFont systemFontSize] 13pt 原生标准)
        // Win 10pt (Body 正文/卡片内容) -> 14pt (+4pt, 舒展清晰的阅读正文)
        // Win 11pt (CardTitle 卡片标题) -> 15pt (+4pt)
        // Win 12~14pt (SectionTitle 对话框/卡片标题) -> 16~17pt (+3pt)
        // Win 18pt+ (DisplayTitle 页面主标题) -> 21pt+ (+3pt)
        if (winPtSize <= 8) {
            return winPtSize + 3;
        } else if (winPtSize <= 11) {
            return winPtSize + 4;
        } else if (winPtSize <= 14) {
            return winPtSize + 3;
        } else {
            return winPtSize + 3;
        }
#else
        return winPtSize;
#endif
    }

    // 创建指定点数（未校准）的跨平台字体
    static wxFont CreateFont(int ptSize, wxFontWeight weight = wxFONTWEIGHT_NORMAL, bool italic = false, const wxString& preferredFamily = wxEmptyString) {
        wxString faceName = preferredFamily.IsEmpty() ? GetDefaultFamily() : preferredFamily;
        wxFontStyle style = italic ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL;
        wxFont font(ptSize, wxFONTFAMILY_SWISS, style, weight, false, faceName);
        if (!font.IsOk()) {
            font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
            font.SetPointSize(ptSize);
            font.SetWeight(weight);
            font.SetStyle(style);
        }
        return font;
    }

    // 根据 Windows 传统设计基准字号创建自适应校准字体
    static wxFont MakeFont(int winBaselinePt, wxFontWeight weight = wxFONTWEIGHT_NORMAL, bool italic = false, const wxString& preferredFamily = wxEmptyString) {
        return CreateFont(CalibratePtSize(winBaselinePt), weight, italic, preferredFamily);
    }

    // 根据语义角色获取推荐字体
    static wxFont GetFont(FontRole role, bool bold = false) {
        switch (role) {
        case FontRole::DisplayTitle:
            return MakeFont(18, wxFONTWEIGHT_BOLD);
        case FontRole::SectionTitle:
            return MakeFont(14, wxFONTWEIGHT_BOLD);
        case FontRole::CardTitle:
            return MakeFont(11, wxFONTWEIGHT_BOLD);
        case FontRole::WindowTitle:
            return MakeFont(10, wxFONTWEIGHT_BOLD);
        case FontRole::Body:
            return MakeFont(10, bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
        case FontRole::Control:
            return MakeFont(9, bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
        case FontRole::Badge:
            return MakeFont(9, wxFONTWEIGHT_BOLD);
        case FontRole::Caption:
            return MakeFont(8, bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
        case FontRole::Code:
            return MakeFont(9, bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL, false, GetDefaultMonoFamily());
        default:
            return MakeFont(10, bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
        }
    }

    // 获取等宽字体（代码/终端/日志等，支持根据角色微调尺寸）
    static wxFont GetMonoFont(FontRole role = FontRole::Code, bool bold = false) {
        int winBaseline = (role == FontRole::Caption) ? 8 : ((role == FontRole::Control) ? 9 : 10);
        return MakeFont(winBaseline, bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL, false, GetDefaultMonoFamily());
    }
};

} // namespace LinguaAlpaca::UI
