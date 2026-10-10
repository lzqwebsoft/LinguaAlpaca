#include "HtmlExporter.hpp"

#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/uri.h>
#include <wx/file.h>
#include <wx/base64.h>
#include <wx/datetime.h>

#include <nlohmann/json.hpp>
#include <unordered_map>
#include <cctype>
#include <vector>
#include <mutex>

#include "core/Logger.hpp"

namespace {

std::string GetMimeTypeForExtension(const wxString& ext) {
    wxString lower = ext.Lower();
    if (lower == "png") return "image/png";
    if (lower == "jpg" || lower == "jpeg") return "image/jpeg";
    if (lower == "webp") return "image/webp";
    if (lower == "gif") return "image/gif";
    if (lower == "svg") return "image/svg+xml";
    if (lower == "bmp") return "image/bmp";
    return "image/jpeg";
}

std::string ResolveLocalImagePath(const std::string& rawPath, const std::string& baseDir) {
    if (rawPath.empty()) return "";
    if (rawPath.rfind("data:", 0) == 0 ||
        rawPath.rfind("http://", 0) == 0 ||
        rawPath.rfind("https://", 0) == 0) {
        return ""; // 非本地文件
    }

    auto tryCandidate = [](const wxString& pathCandidate) -> std::string {
        wxFileName fn(pathCandidate);
        fn.Normalize(wxPATH_NORM_ALL);
        if (wxFileExists(fn.GetFullPath())) {
            return std::string(fn.GetFullPath().ToUTF8().data());
        }
        return "";
    };

    std::string path = rawPath;
    if (path.rfind("file:///", 0) == 0) {
        path = path.substr(8);
#ifdef _WIN32
        if (path.size() >= 3 && path[0] == '/' && path[2] == ':') {
            path = path.substr(1);
        }
#endif
    } else if (path.rfind("file://", 0) == 0) {
        path = path.substr(7);
    }

    // 尝试解码 URL 编码 (如 %20 -> 空格)
    wxString unescapedPath = wxURI::Unescape(wxString::FromUTF8(path));

    // 1. 直接绝对路径检测
    std::string found = tryCandidate(wxString::FromUTF8(path));
    if (!found.empty()) return found;
    found = tryCandidate(unescapedPath);
    if (!found.empty()) return found;

    // 2. 结合 baseDir 解析相对路径
    if (!baseDir.empty()) {
        wxString wxBase = wxString::FromUTF8(baseDir);
        found = tryCandidate(wxBase + "/" + wxString::FromUTF8(path));
        if (!found.empty()) return found;
        found = tryCandidate(wxBase + "/" + unescapedPath);
        if (!found.empty()) return found;

        // 若 baseDir 是文件路径，尝试其所属同名子目录
        if (wxFileExists(wxBase)) {
            wxFileName docFn(wxBase);
            wxString cand = docFn.GetPath() + "/" + docFn.GetName() + "/" + wxString::FromUTF8(path);
            found = tryCandidate(cand);
            if (!found.empty()) return found;
            cand = docFn.GetPath() + "/" + docFn.GetName() + "/" + unescapedPath;
            found = tryCandidate(cand);
            if (!found.empty()) return found;
        }
    }

    return "";
}

struct CachedImageDataUrl {
    wxLongLong mtimeTicks{0};
    wxULongLong size{0};
    std::string dataUrl;
};

static std::mutex s_imageDataUrlMutex;
static std::unordered_map<std::string, CachedImageDataUrl> s_imageDataUrlCache;
constexpr size_t MAX_IMAGE_DATA_URL_CACHE_SIZE = 2000;

std::string ReadTextFile(const wxString& filePath) {
    if (!wxFileExists(filePath)) return "";
    wxFile file(filePath, wxFile::read);
    if (!file.IsOpened()) return "";
    wxFileOffset length = file.Length();
    if (length <= 0 || length > 30 * 1024 * 1024) return "";
    std::string content;
    content.resize(static_cast<size_t>(length));
    if (file.Read(&content[0], length) != length) return "";
    return content;
}

std::string EscapeHtmlText(const std::string& str) {
    std::string out;
    out.reserve(str.size() + 16);
    for (char c : str) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#039;"; break;
            default: out += c; break;
        }
    }
    return out;
}

void ReplaceAll(std::string& target, const std::string& search, const std::string& replacement) {
    if (search.empty()) return;
    size_t pos = 0;
    while ((pos = target.find(search, pos)) != std::string::npos) {
        target.replace(pos, search.length(), replacement);
        pos += replacement.length();
    }
}

struct WebAssets {
    std::string katexCss;
    std::string markedJs;
    std::string katexJs;
    std::string renderJs;
    std::string exportHtml;
    bool loaded{false};
};

static std::mutex s_assetsMutex;
static WebAssets s_cachedAssets;

} // namespace

namespace LinguaAlpaca {

std::string HtmlExporter::LoadImageFileAsDataUrl(const std::string& fullPath) {
    wxFile file(wxString::FromUTF8(fullPath), wxFile::read);
    if (!file.IsOpened()) {
        return "";
    }
    wxFileOffset length = file.Length();
    if (length <= 0 || length > 30 * 1024 * 1024) { // 30MB 安全上限
        return "";
    }

    std::vector<unsigned char> buffer(static_cast<size_t>(length));
    if (file.Read(buffer.data(), length) != length) {
        return "";
    }

    wxString b64 = wxBase64Encode(buffer.data(), buffer.size());
    wxFileName fn(wxString::FromUTF8(fullPath));
    std::string mime = GetMimeTypeForExtension(fn.GetExt());

    return "data:" + mime + ";base64," + std::string(b64.ToUTF8().data());
}

std::string HtmlExporter::EmbedLocalImagesAsBase64(const std::string& md, const std::string& baseDir) {
    if (md.empty()) return md;

    std::unordered_map<std::string, std::pair<std::string, std::string>> localPathCache;
    auto getOrLoadDataUrl = [&](const std::string& rawSrc) -> std::pair<std::string, std::string> {
        auto itLocal = localPathCache.find(rawSrc);
        if (itLocal != localPathCache.end()) {
            return itLocal->second;
        }

        std::string fullPath = ResolveLocalImagePath(rawSrc, baseDir);
        if (fullPath.empty()) {
            localPathCache[rawSrc] = {"", ""};
            return {"", ""};
        }

        wxFileName fn(wxString::FromUTF8(fullPath));
        wxDateTime mtime = fn.GetModificationTime();
        wxULongLong sz = fn.GetSize();
        wxLongLong ticks = mtime.IsValid() ? mtime.GetValue() : wxLongLong(0);

        {
            std::lock_guard<std::mutex> lock(s_imageDataUrlMutex);
            auto it = s_imageDataUrlCache.find(fullPath);
            if (it != s_imageDataUrlCache.end()) {
                if (it->second.mtimeTicks == ticks && it->second.size == sz) {
                    localPathCache[rawSrc] = {fullPath, it->second.dataUrl};
                    return {fullPath, it->second.dataUrl};
                }
            }
        }

        std::string dataUrl = LoadImageFileAsDataUrl(fullPath);
        if (!dataUrl.empty()) {
            std::lock_guard<std::mutex> lock(s_imageDataUrlMutex);
            if (s_imageDataUrlCache.size() >= MAX_IMAGE_DATA_URL_CACHE_SIZE) {
                s_imageDataUrlCache.clear();
            }
            s_imageDataUrlCache[fullPath] = CachedImageDataUrl{ticks, sz, dataUrl};
        }
        localPathCache[rawSrc] = {fullPath, dataUrl};
        return {fullPath, dataUrl};
    };

    std::string result;
    result.reserve(md.size() + 2048);

    size_t i = 0;
    const size_t n = md.size();
    bool inCodeFence = false;

    while (i < n) {
        // 判断代码块状态切换 (行首 ``` 或 ~~~)
        if (i == 0 || md[i - 1] == '\n') {
            if (i + 2 < n && ((md[i] == '`' && md[i+1] == '`' && md[i+2] == '`') ||
                             (md[i] == '~' && md[i+1] == '~' && md[i+2] == '~'))) {
                inCodeFence = !inCodeFence;
                result += md[i];
                i++;
                continue;
            }
        }

        if (inCodeFence) {
            result += md[i];
            i++;
            continue;
        }

        // 检测 HTML <img ...>
        if (md[i] == '<' && i + 4 < n &&
            (md[i+1] == 'i' || md[i+1] == 'I') &&
            (md[i+2] == 'm' || md[i+2] == 'M') &&
            (md[i+3] == 'g' || md[i+3] == 'G') &&
            (md[i+4] == ' ' || md[i+4] == '\t' || md[i+4] == '\n' || md[i+4] == '\r' || md[i+4] == '/')) {

            size_t tagEnd = md.find('>', i + 4);
            if (tagEnd != std::string::npos) {
                std::string tagContent = md.substr(i, tagEnd - i + 1);

                size_t srcPos = std::string::npos;
                for (size_t p = 0; p + 4 < tagContent.size(); ++p) {
                    if ((tagContent[p] == 's' || tagContent[p] == 'S') &&
                        (tagContent[p+1] == 'r' || tagContent[p+1] == 'R') &&
                        (tagContent[p+2] == 'c' || tagContent[p+2] == 'C') &&
                        tagContent[p+3] == '=') {
                        if (p == 0 || isspace(static_cast<unsigned char>(tagContent[p-1]))) {
                            srcPos = p;
                            break;
                        }
                    }
                }

                if (srcPos != std::string::npos) {
                    size_t quoteStart = srcPos + 4;
                    while (quoteStart < tagContent.size() && isspace(static_cast<unsigned char>(tagContent[quoteStart]))) {
                        quoteStart++;
                    }
                    if (quoteStart < tagContent.size() && (tagContent[quoteStart] == '"' || tagContent[quoteStart] == '\'')) {
                        char quote = tagContent[quoteStart];
                        size_t quoteEnd = tagContent.find(quote, quoteStart + 1);
                        if (quoteEnd != std::string::npos) {
                            std::string rawSrc = tagContent.substr(quoteStart + 1, quoteEnd - (quoteStart + 1));
                            auto [fullPath, dataUrl] = getOrLoadDataUrl(rawSrc);
                            if (!dataUrl.empty()) {
                                std::string newTag = tagContent.substr(0, quoteStart + 1);
                                newTag += dataUrl;
                                newTag += quote;

                                if (tagContent.find("data-original-src=") == std::string::npos) {
                                    newTag += " data-original-src=\"" + rawSrc + "\"";
                                }
                                newTag += tagContent.substr(quoteEnd + 1);

                                result += newTag;
                                i = tagEnd + 1;
                                continue;
                            }
                        }
                    }
                }

                result += tagContent;
                i = tagEnd + 1;
                continue;
            }
        }

        // 检测 Markdown ![alt](url)
        if (md[i] == '!' && i + 1 < n && md[i+1] == '[') {
            size_t closeBracket = md.find(']', i + 2);
            if (closeBracket != std::string::npos && closeBracket + 1 < n && md[closeBracket + 1] == '(') {
                size_t closeParen = md.find(')', closeBracket + 2);
                if (closeParen != std::string::npos) {
                    std::string altText = md.substr(i + 2, closeBracket - (i + 2));
                    std::string insideParen = md.substr(closeBracket + 2, closeParen - (closeBracket + 2));

                    std::string rawUrl = insideParen;
                    std::string title;
                    size_t spacePos = insideParen.find(' ');
                    if (spacePos != std::string::npos) {
                        rawUrl = insideParen.substr(0, spacePos);
                        title = insideParen.substr(spacePos + 1);
                        while (!title.empty() && isspace(static_cast<unsigned char>(title.front()))) title.erase(title.begin());
                        while (!title.empty() && isspace(static_cast<unsigned char>(title.back()))) title.pop_back();
                        if (title.size() >= 2 && ((title.front() == '"' && title.back() == '"') || (title.front() == '\'' && title.back() == '\''))) {
                            title = title.substr(1, title.size() - 2);
                        }
                    }

                    while (!rawUrl.empty() && isspace(static_cast<unsigned char>(rawUrl.front()))) rawUrl.erase(rawUrl.begin());
                    while (!rawUrl.empty() && isspace(static_cast<unsigned char>(rawUrl.back()))) rawUrl.pop_back();

                    auto [fullPath, dataUrl] = getOrLoadDataUrl(rawUrl);
                    if (!dataUrl.empty()) {
                        std::string htmlImg = "<img src=\"" + dataUrl + "\" alt=\"" + altText + "\"";
                        if (!title.empty()) {
                            htmlImg += " title=\"" + title + "\"";
                        }
                        htmlImg += " data-original-src=\"" + rawUrl + "\" style=\"max-width: 100%; object-fit: contain;\" />";
                        result += htmlImg;
                        i = closeParen + 1;
                        continue;
                    }
                }
            }
        }

        result += md[i];
        i++;
    }

    return result;
}

wxString HtmlExporter::GetWebResourcesDir() {
    std::vector<wxString> candidates;

    // 1. 可执行程序同级与各级上级路径
    wxFileName execFn(wxStandardPaths::Get().GetExecutablePath());
    wxString dir = execFn.GetPath();
    candidates.push_back(dir + "/resources/web");
    candidates.push_back(dir + "/../resources/web");
    candidates.push_back(dir + "/../../resources/web");
    candidates.push_back(dir + "/../../../resources/web");

#ifdef __APPLE__
    // 2. macOS App Bundle 资源目录查找
    candidates.push_back(wxStandardPaths::Get().GetResourcesDir() + "/resources/web");
    candidates.push_back(wxStandardPaths::Get().GetResourcesDir() + "/resources");
#endif

    // 3. 开发环境源码树工作目录相对路径查找
    candidates.push_back("resources/web");
    candidates.push_back("../resources/web");
    candidates.push_back("../../resources/web");

    // 第一轮：严格寻找包含 export.html 的目录
    for (const auto& c : candidates) {
        if (wxFileExists(c + "/export.html") || wxFileExists(c + "/export_template.html")) {
            return c;
        }
    }

    // 第二轮降级：寻找包含 index.html 的目录
    for (const auto& c : candidates) {
        if (wxFileExists(c + "/index.html")) {
            return c;
        }
    }

    return wxEmptyString;
}

std::string HtmlExporter::GenerateStandaloneHtml(
    const std::string& markdown,
    const std::string& baseDir,
    const std::string& title
) {
    // 1. 本地图片内联嵌入 Base64
    std::string processedMd = EmbedLocalImagesAsBase64(markdown, baseDir);

    // 2. 将 Markdown 转为安全 JSON 字符串字面量，防止 </script> 语法断裂
    std::string jsonMd = nlohmann::json(processedMd).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    ReplaceAll(jsonMd, "</", "<\\/");

    // 3. 读取本地静态模板 export.html 与离线脚本/样式
    {
        std::lock_guard<std::mutex> lock(s_assetsMutex);
        if (!s_cachedAssets.loaded) {
            wxString resDir = GetWebResourcesDir();
            if (!resDir.IsEmpty()) {
                s_cachedAssets.katexCss = ReadTextFile(resDir + "/katex/katex.min.css");
                s_cachedAssets.markedJs = ReadTextFile(resDir + "/marked.min.js");
                s_cachedAssets.katexJs = ReadTextFile(resDir + "/katex/katex.min.js");
                s_cachedAssets.renderJs = ReadTextFile(resDir + "/render.js");
                s_cachedAssets.exportHtml = ReadTextFile(resDir + "/export.html");
                if (s_cachedAssets.exportHtml.empty()) {
                    s_cachedAssets.exportHtml = ReadTextFile(resDir + "/export_template.html");
                }

                // 将 KaTeX 内部的本地字体相对路径重定向为官方 CDN 地址，同时保留 MathML 与通用衬线体离线兜底
                if (!s_cachedAssets.katexCss.empty()) {
                    ReplaceAll(s_cachedAssets.katexCss, "url(fonts/", "url(https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/fonts/");
                }

                if (!s_cachedAssets.exportHtml.empty()) {
                    s_cachedAssets.loaded = true;
                }
            }
        }
    }

    std::string safeTitle = title.empty() ? "OCR 解析文档" : EscapeHtmlText(title);
    wxString nowTimeStr = wxDateTime::Now().Format("%Y-%m-%d %H:%M:%S");
    std::string timeStr = std::string(nowTimeStr.ToUTF8().data());
    std::string charCountStr = std::to_string(markdown.size());

    // 4. 使用外部文件模板 export.html 进行占位符动态填充 (完全零 C++ 硬编码)
    if (s_cachedAssets.loaded && !s_cachedAssets.exportHtml.empty()) {
        std::string html = s_cachedAssets.exportHtml;

        ReplaceAll(html, "{{TITLE}}", safeTitle);
        ReplaceAll(html, "{{TIME}}", timeStr);
        ReplaceAll(html, "{{CHAR_COUNT}}", charCountStr);
        ReplaceAll(html, "{{KATEX_CSS}}", s_cachedAssets.katexCss);
        ReplaceAll(html, "{{MARKED_JS}}", s_cachedAssets.markedJs);
        ReplaceAll(html, "{{KATEX_JS}}", s_cachedAssets.katexJs);
        ReplaceAll(html, "{{RENDER_JS}}", s_cachedAssets.renderJs);
        ReplaceAll(html, "{{RAW_MARKDOWN_JSON}}", jsonMd);

        return html;
    }

    // 5. 极端环境降级逻辑 (若 resources/web/export.html 丢失)
    std::string fallbackHtml =
        "<!DOCTYPE html>\n"
        "<html lang=\"zh-CN\">\n"
        "<head>\n"
        "    <meta charset=\"UTF-8\">\n"
        "    <title>" + safeTitle + "</title>\n"
        "    <link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.css\">\n"
        "    <style>\n"
        "        :root { --bg: #f5f1e8; --card: #ffffff; --ink: #2a2520; --line: #e0d8c8; --accent: #8b4513; }\n"
        "        body { font-family: 'Songti SC', 'SimSun', serif; background: var(--bg); color: var(--ink); padding: 32px 20px; max-width: 1180px; margin: 0 auto; }\n"
        "        .content-panel { background: var(--card); border: 1px solid var(--line); border-radius: 10px; padding: 32px; }\n"
        "    </style>\n"
        "    <script src=\"https://cdn.jsdelivr.net/npm/marked/marked.min.js\"></script>\n"
        "    <script src=\"https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.js\"></script>\n"
        "</head>\n"
        "<body>\n"
        "    <header style='text-align:center;margin-bottom:24px;border-bottom:2px solid var(--line);padding-bottom:16px;'>\n"
        "        <h1 style='color:#3a2a1a;font-size:26px;'>" + safeTitle + "</h1>\n"
        "        <div style='color:#6b6355;font-size:13px;'>LinguaAlpaca 文档解析结果</div>\n"
        "    </header>\n"
        "    <main class='content-panel'><div id='content'></div></main>\n"
        "    <script>\n"
        "        var rawMarkdownData = " + jsonMd + ";\n"
        "        function renderMarkdown(md) {\n"
        "            if (typeof marked !== 'undefined') document.getElementById('content').innerHTML = marked.parse(md);\n"
        "        }\n"
        "        renderMarkdown(rawMarkdownData);\n"
        "    </script>\n"
        "</body>\n"
        "</html>\n";

    return fallbackHtml;
}

bool HtmlExporter::ExportToStandaloneHtml(
    const std::string& markdown,
    const std::string& baseDir,
    const std::string& outputPath,
    const std::string& title
) {
    if (outputPath.empty()) {
        LOG_WARN("HtmlExporter", "Export failed: output path is empty");
        return false;
    }

    try {
        wxFileName outFn(wxString::FromUTF8(outputPath));
        wxString dirPath = outFn.GetPath();
        if (!dirPath.IsEmpty() && !wxDirExists(dirPath)) {
            wxFileName::Mkdir(dirPath, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        }

        wxFile file(wxString::FromUTF8(outputPath), wxFile::write);
        if (!file.IsOpened()) {
            LOG_ERROR("HtmlExporter", "Failed to open output file for writing: " + outputPath);
            return false;
        }

        std::string html = GenerateStandaloneHtml(markdown, baseDir, title);
        if (file.Write(html.data(), html.size()) != html.size()) {
            LOG_ERROR("HtmlExporter", "Failed to write complete HTML content: " + outputPath);
            return false;
        }
        file.Close();

        LOG_INFO("HtmlExporter", "Successfully exported standalone HTML: " + outputPath + " (" + std::to_string(html.size()) + " bytes)");
        return true;
    } catch (const std::exception& ex) {
        LOG_ERROR("HtmlExporter", std::string("Exception exporting HTML: ") + ex.what());
        return false;
    } catch (...) {
        LOG_ERROR("HtmlExporter", "Unknown exception exporting HTML");
        return false;
    }
}

} // namespace LinguaAlpaca
