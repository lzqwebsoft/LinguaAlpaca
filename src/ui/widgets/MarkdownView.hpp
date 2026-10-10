#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include <wx/webview.h>
#include <string>
#include <functional>
#include "../AsyncTrackable.hpp"
#include "TextCtrl.hpp"
#include "../theme/Theme.hpp"

namespace LinguaAlpaca::UI {

enum class MarkdownViewMode {
    Rendered, // 优雅渲染模式 (wxWebView: 排版/公式/表格/图片)
    Source    // Markdown 源码编辑模式 (TextCtrl: 原始纯文本)
};

/**
 * @brief 现代化双模式 Markdown / LaTeX / 表格 / 图文渲染组件
 *
 * 特性：
 * - 采用嵌入式 Web 引擎 (Windows: Edge WebView2, macOS: Apple WKWebView) 渲染富文本排版
 * - 100% 离线单机运行，零外网依赖
 * - 内置 KaTeX 矢量数学公式排版 ($..$ 与 $$..$$)
 * - 原生支持 HTML 居中图片与相对路径解析 (<img src="imgs/..." width="47%" />)
 * - 响应式 GitHub/Notion 风格表格排版 (斑马纹、圆角边框、横向滚动)
 * - 支持随时无缝切换为 Markdown 源码编辑模式 (TextCtrl)
 * - 深度联动 LinguaAlpaca 主题调色板 (浅色/深色平滑切换无白闪)
 * - 继承 AsyncTrackable，保证后台异步回调与线程安全
 */
class MarkdownView : public wxPanel, public AsyncTrackable {
public:
    MarkdownView(wxWindow* parent, wxWindowID id = wxID_ANY,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize);
    ~MarkdownView() override = default;

    // 内容设置
    void SetMarkdown(const std::string& markdown, const std::string& baseDir = "", bool preserveScroll = false);
    void SetMarkdown(const wxString& markdown, const wxString& baseDir = wxEmptyString, bool preserveScroll = false);
    void Clear();

    // 视图模式 (渲染 vs. 源码)
    void SetViewMode(MarkdownViewMode mode);
    MarkdownViewMode GetViewMode() const { return m_currentMode; }

    // 数据获取
    const std::string& GetRawMarkdown() const { return m_rawMarkdown; }
    wxString GetPlainText() const;
    size_t GetCharacterCount() const { return m_rawMarkdown.size(); }

    /**
     * @brief 获取当前 WebView 内部已渲染完成的完整静态 HTML (即 #content 容器的 innerHTML)
     * @return 包含公式 MathML、OTSL 表格、样式及 Base64 图片的静态 HTML 字符串
     */
    wxString GetRenderedHtml() const;

    // 主题与样式
    void UpdateTheme();

    // 回调
    void SetOnImageClickCallback(std::function<void(const wxString&)> callback) {
        m_onImageClickCallback = std::move(callback);
    }
    void SetOnContentChangedCallback(std::function<void(const wxString&)> callback) {
        m_onContentChangedCallback = std::move(callback);
    }

    // 子组件访问
    TextCtrl* GetTextCtrl() const { return m_textCtrl; }
    wxWebView* GetWebView() const { return m_webView; }

private:
    void InitUI();
    void InitWebView();
    wxString FindHtmlTemplatePath() const;
    void ApplyThemeToWebView();
    void DoRenderMarkdown(bool preserveScroll);

    wxBoxSizer* m_sizer{nullptr};
    wxWebView* m_webView{nullptr};
    TextCtrl* m_textCtrl{nullptr};

    MarkdownViewMode m_currentMode{MarkdownViewMode::Rendered};
    std::string m_rawMarkdown;
    std::string m_baseDir;
    bool m_isWebViewReady{false};
    bool m_hasPendingRender{false};
    bool m_pendingPreserveScroll{false};
    bool m_isTextCtrlDirty{false};

    std::function<void(const wxString&)> m_onImageClickCallback;
    std::function<void(const wxString&)> m_onContentChangedCallback;
};

} // namespace LinguaAlpaca::UI
