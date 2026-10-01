#include "MarkdownView.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/uri.h>

#include <nlohmann/json.hpp>

#include "core/Logger.hpp"
#include "../theme/Theme.hpp"

namespace {

#ifdef _WIN32
void RemoveNativeWindowBorders(wxWindow* win) {
    if (!win) return;
    HWND hwnd = (HWND)win->GetHWND();
    if (!hwnd) return;

    auto strip = [](HWND h) {
        LONG_PTR style = ::GetWindowLongPtr(h, GWL_STYLE);
        if (style & (WS_BORDER | WS_THICKFRAME)) {
            ::SetWindowLongPtr(h, GWL_STYLE, style & ~(WS_BORDER | WS_THICKFRAME));
        }
        LONG_PTR exStyle = ::GetWindowLongPtr(h, GWL_EXSTYLE);
        if (exStyle & (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE)) {
            ::SetWindowLongPtr(h, GWL_EXSTYLE, exStyle & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE));
        }
        ::SetWindowPos(h, NULL, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    };

    strip(hwnd);
    ::EnumChildWindows(hwnd, [](HWND child, LPARAM) -> BOOL {
        LONG_PTR style = ::GetWindowLongPtr(child, GWL_STYLE);
        if (style & (WS_BORDER | WS_THICKFRAME)) {
            ::SetWindowLongPtr(child, GWL_STYLE, style & ~(WS_BORDER | WS_THICKFRAME));
        }
        LONG_PTR exStyle = ::GetWindowLongPtr(child, GWL_EXSTYLE);
        if (exStyle & (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE)) {
            ::SetWindowLongPtr(child, GWL_EXSTYLE, exStyle & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE));
        }
        ::SetWindowPos(child, NULL, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        return TRUE;
    }, 0);
}
#endif

wxString SafeJsStringLiteral(const wxString& ws) {
    if (ws.IsEmpty()) {
        return "\"\"";
    }

    wxString out = "\"";
    out.Alloc(ws.length() + 32);
    for (wxUniChar ch : ws) {
        switch (ch.GetValue()) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case 0x2028: out += "\\u2028"; break;
            case 0x2029: out += "\\u2029"; break;
            default:
                if (ch.GetValue() < 32) {
                    out += wxString::Format("\\u%04x", static_cast<unsigned int>(ch.GetValue()));
                } else {
                    out += ch;
                }
                break;
        }
    }
    out += "\"";
    return out;
}

} // namespace

namespace LinguaAlpaca::UI {

MarkdownView::MarkdownView(wxWindow* parent, wxWindowID id,
                           const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, id, pos, size, wxBORDER_NONE) {
    InitUI();
}

void MarkdownView::InitUI() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.cardBg);

    m_sizer = new wxBoxSizer(wxVERTICAL);

    // 1. 初始化源码模式下的 TextCtrl
    m_textCtrl = new TextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
    m_textCtrl->SetHint(L"此处呈现 Markdown 源码与结构化文本...");
    m_textCtrl->GetInnerCtrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent& e) {
        if (m_currentMode == MarkdownViewMode::Source) {
            m_rawMarkdown = m_textCtrl->GetValue().ToUTF8().data();
            if (m_onContentChangedCallback) {
                m_onContentChangedCallback(m_textCtrl->GetValue());
            }
        }
        e.Skip();
    });

    // 2. 初始化嵌入式 Web 引擎 (wxWebView)
    InitWebView();

    if (m_webView) {
        m_sizer->Add(m_webView, 1, wxEXPAND);
        m_sizer->Add(m_textCtrl, 1, wxEXPAND);
        m_textCtrl->Hide(); // 默认首选 Rendered 排版模式
    } else {
        // 若环境不支持 WebView，平滑退化为纯文本模式
        m_sizer->Add(m_textCtrl, 1, wxEXPAND);
        m_currentMode = MarkdownViewMode::Source;
    }

    SetSizer(m_sizer);
    Layout();
}

void MarkdownView::InitWebView() {
    wxString templatePath = FindHtmlTemplatePath();
    if (templatePath.IsEmpty() || !wxFileExists(templatePath)) {
        m_webView = nullptr;
        return;
    }

    wxString fileUrl = "file:///" + templatePath;
    fileUrl.Replace("\\", "/");

    try {
        m_webView = wxWebView::New(this, wxID_ANY, fileUrl, wxDefaultPosition, wxDefaultSize,
                                   wxWebViewBackendDefault, wxBORDER_NONE);
    } catch (...) {
        m_webView = nullptr;
        return;
    }

    if (!m_webView)
        return;

    // 禁用 WebView 弹出菜单与快捷键，防止触发后焦点陷入底层浏览器无法回到主界面
    m_webView->EnableContextMenu(false);
    m_webView->EnableBrowserAcceleratorKeys(false);
    m_webView->EnableAccessToDevTools(false);

    m_webView->Bind(wxEVT_CONTEXT_MENU, [](wxContextMenuEvent&) {
        // 显式拦截并阻止 wxWidgets 层的右键菜单事件
    });

    wxString antiFocusTrapScript =
        "window.addEventListener('contextmenu', function(e) { e.preventDefault(); e.stopPropagation(); return false; }, true);\n"
        "window.addEventListener('keydown', function(e) {\n"
        "    if ((e.ctrlKey || e.metaKey) && !e.altKey && !e.shiftKey) {\n"
        "        if (e.key === 'c' || e.key === 'C' || e.key === 'a' || e.key === 'A') return;\n"
        "    }\n"
        "    if (e.key && e.key.startsWith('F') && e.key.length > 1) { e.preventDefault(); e.stopPropagation(); return false; }\n"
        "    if (e.ctrlKey || e.metaKey || e.altKey) { e.preventDefault(); e.stopPropagation(); return false; }\n"
        "}, true);";
    m_webView->AddUserScript(antiFocusTrapScript);

#ifdef _WIN32
    RemoveNativeWindowBorders(m_webView);
#endif

    // 页面完全载入后触发主题同步与待渲染任务
    m_webView->Bind(wxEVT_WEBVIEW_LOADED, [this](wxWebViewEvent& event) {
        m_isWebViewReady = true;
#ifdef _WIN32
        RemoveNativeWindowBorders(m_webView);
#endif
        ApplyThemeToWebView();

        if (m_hasPendingRender) {
            DoRenderMarkdown(m_pendingPreserveScroll);
            m_hasPendingRender = false;
        }
    });

    // 拦截网页内链接与图片点击协议，禁止 WebView 内部跳转外部网页，统统调用系统默认浏览器打开
    m_webView->Bind(wxEVT_WEBVIEW_NAVIGATING, [this](wxWebViewEvent& event) {
        wxString url = event.GetURL();

        // 1. 拦截图片点击预览协议: lingua-img-preview://<encoded_src>
        if (url.StartsWith("lingua-img-preview://")) {
            event.Veto();
            wxString encoded = url.Mid(21);
            wxString decoded = wxURI::Unescape(encoded);
            if (m_onImageClickCallback) {
                wxTheApp->CallAfter([this, decoded]() {
                    if (m_onImageClickCallback) {
                        m_onImageClickCallback(decoded);
                    }
                });
            }
            return;
        }

        // 2. 拦截自定义外部链接协议: lingua-external-link://<encoded_url>
        if (url.StartsWith("lingua-external-link://")) {
            event.Veto();
            wxString encoded = url.Mid(23);
            wxString decoded = wxURI::Unescape(encoded);
            wxTheApp->CallAfter([decoded]() {
                wxLaunchDefaultBrowser(decoded);
            });
            return;
        }

        // 3. 兜底拦截直接通过原生机制触发的外网链接 (http/https/mailto/ftp)
        if (url.StartsWith("http://") || url.StartsWith("https://") ||
            url.StartsWith("mailto:") || url.StartsWith("ftp://")) {
            event.Veto();
            wxTheApp->CallAfter([url]() {
                wxLaunchDefaultBrowser(url);
            });
            return;
        }

        // 4. 允许初始模版页面 file:// 载入，严格拦截其它外部 file:// 页面跳转
        if (url.StartsWith("file://")) {
            wxString templatePath = FindHtmlTemplatePath();
            wxString normTemplate = "file:///" + templatePath;
            normTemplate.Replace("\\", "/");
            if (url != normTemplate && !url.StartsWith(normTemplate + "?") && !url.StartsWith(normTemplate + "#")) {
                event.Veto();
                wxTheApp->CallAfter([url]() {
                    wxLaunchDefaultBrowser(url);
                });
                return;
            }
        }
    });

    // 拦截并阻止任何新建窗口事件 (如 target="_blank" 或 window.open)，强制由系统默认浏览器打开
    m_webView->Bind(wxEVT_WEBVIEW_NEWWINDOW, [](wxWebViewEvent& event) {
        event.Veto();
        wxString url = event.GetURL();
        if (!url.IsEmpty() && !url.StartsWith("about:") && !url.StartsWith("javascript:")) {
            wxTheApp->CallAfter([url]() {
                wxLaunchDefaultBrowser(url);
            });
        }
    });
}

wxString MarkdownView::FindHtmlTemplatePath() const {
    // 1. 优先从运行可执行程序同级的 resources/web/ 查找
    wxFileName execFn(wxStandardPaths::Get().GetExecutablePath());
    wxString dir = execFn.GetPath();

    wxString candidate = dir + "/resources/web/index.html";
    if (wxFileExists(candidate))
        return candidate;

#ifdef __APPLE__
    // 2. macOS App Bundle 资源目录查找
    candidate = wxStandardPaths::Get().GetResourcesDir() + "/resources/web/index.html";
    if (wxFileExists(candidate))
        return candidate;
#endif

    // 3. 开发环境源码树相对路径查找
    candidate = "resources/web/index.html";
    if (wxFileExists(candidate))
        return candidate;

    candidate = "../resources/web/index.html";
    if (wxFileExists(candidate))
        return candidate;

    return wxEmptyString;
}

void MarkdownView::SetMarkdown(const std::string& markdown, const std::string& baseDir, bool preserveScroll) {
    m_rawMarkdown = markdown;
    m_baseDir = baseDir;

    // 仅在源码编辑模式下才同步给 TextCtrl，在排版模式下标记脏位延迟同步，彻底避免大量文本卡死 Win32 RichEdit
    if (m_currentMode == MarkdownViewMode::Source) {
        if (m_textCtrl) {
            wxString ws = wxString::FromUTF8(m_rawMarkdown);
            if (ws.IsEmpty() && !m_rawMarkdown.empty()) {
                ws = wxString(m_rawMarkdown.c_str(), wxConvLocal);
            }
            m_textCtrl->SetValue(ws, preserveScroll);
        }
        m_isTextCtrlDirty = false;
    } else {
        m_isTextCtrlDirty = true;
    }

    // 若处于排版模式，派发给 WebView 异步渲染
    if (m_webView) {
        if (m_isWebViewReady) {
            DoRenderMarkdown(preserveScroll);
        } else {
            m_hasPendingRender = true;
            m_pendingPreserveScroll = preserveScroll;
        }
    }
}

void MarkdownView::SetMarkdown(const wxString& markdown, const wxString& baseDir, bool preserveScroll) {
    SetMarkdown(std::string(markdown.ToUTF8().data()),
                std::string(baseDir.ToUTF8().data()),
                preserveScroll);
}

void MarkdownView::DoRenderMarkdown(bool preserveScroll) {
    if (!m_webView || !m_isWebViewReady)
        return;

    try {
        std::string jsonMd = nlohmann::json(m_rawMarkdown).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        std::string jsonBase = nlohmann::json(m_baseDir).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

        wxString jsCall = "renderMarkdown(" + wxString::FromUTF8(jsonMd) + ", "
                        + wxString::FromUTF8(jsonBase) + ", "
                        + (preserveScroll ? "true" : "false") + ");";

        m_webView->RunScriptAsync(jsCall);
    } catch (const std::exception& ex) {
        LOG_WARN("MarkdownView", std::string("DoRenderMarkdown exception: ") + ex.what());
    } catch (...) {
        LOG_WARN("MarkdownView", "Unknown exception in DoRenderMarkdown");
    }
}

void MarkdownView::SetViewMode(MarkdownViewMode mode) {
    m_currentMode = mode;

    if (!m_webView) {
        m_currentMode = MarkdownViewMode::Source;
        if (m_textCtrl) m_textCtrl->Show();
        Layout();
        return;
    }

    if (mode == MarkdownViewMode::Rendered) {
        // 从源码切回渲染时，将最新的编辑文本更新至渲染视图
        if (m_textCtrl) {
            std::string updated = m_textCtrl->GetValue().ToUTF8().data();
            if (updated != m_rawMarkdown) {
                m_rawMarkdown = updated;
                DoRenderMarkdown(true);
            }
        }
        m_webView->Show();
        m_textCtrl->Hide();
    } else {
        // 切换为源码模式，按需把最新 markdown 文本同步给 TextCtrl
        if (m_textCtrl) {
            if (m_isTextCtrlDirty) {
                wxString ws = wxString::FromUTF8(m_rawMarkdown);
                if (ws.IsEmpty() && !m_rawMarkdown.empty()) {
                    ws = wxString(m_rawMarkdown.c_str(), wxConvLocal);
                }
                m_textCtrl->SetValue(ws, true);
                m_isTextCtrlDirty = false;
            }
            m_textCtrl->Show();
        }
        m_webView->Hide();
    }

    Layout();
}

void MarkdownView::Clear() {
    m_rawMarkdown.clear();
    m_baseDir.clear();
    m_isTextCtrlDirty = false;

    if (m_textCtrl) {
        m_textCtrl->Clear();
    }
    if (m_webView && m_isWebViewReady) {
        m_webView->RunScriptAsync("renderMarkdown('', '', false);");
    }
}

wxString MarkdownView::GetPlainText() const {
    if (m_textCtrl) {
        return m_textCtrl->GetValue();
    }
    return wxString::FromUTF8(m_rawMarkdown);
}

void MarkdownView::UpdateTheme() {
    auto palette = ThemeColors::GetCurrentPalette();
    SetBackgroundColour(palette.cardBg);

    if (m_textCtrl) {
        m_textCtrl->SetBackgroundColour(palette.cardBg);
        m_textCtrl->SetForegroundColour(palette.textPrimary);
        m_textCtrl->Refresh();
    }

    ApplyThemeToWebView();
    Refresh();
}

void MarkdownView::ApplyThemeToWebView() {
    if (!m_webView || !m_isWebViewReady)
        return;

    try {
        bool isDark = (ThemeManager::GetInstance().GetCurrentTheme() == ThemeMode::Dark);
        auto palette = ThemeColors::GetCurrentPalette();

        nlohmann::json palObj;
        palObj["windowBg"] = palette.cardBg.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
        palObj["textPrimary"] = palette.textPrimary.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
        palObj["textSecondary"] = palette.textSecondary.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
        palObj["accentPrimary"] = palette.accentPrimary.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
        palObj["cardBg"] = palette.windowBg.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
        palObj["cardBorder"] = palette.cardBorder.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();

        std::string palStr = palObj.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

        wxString jsCall = "setTheme(" + wxString(isDark ? "true" : "false") + ", "
                        + wxString::FromUTF8(palStr) + ");";

        m_webView->RunScriptAsync(jsCall);
    } catch (const std::exception& ex) {
        LOG_WARN("MarkdownView", std::string("ApplyThemeToWebView exception: ") + ex.what());
    } catch (...) {
        LOG_WARN("MarkdownView", "Unknown exception in ApplyThemeToWebView");
    }
}

} // namespace LinguaAlpaca::UI
