#pragma execution_character_set("utf-8")
#include "PdfHelper.hpp"
#include "core/Logger.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <fpdfview.h>
#include <fpdf_doc.h>
#include <fpdf_text.h>

#include <wx/filename.h>
#include <wx/filefn.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <regex>
#include <sstream>
#include <string_view>

namespace {

// 全局 PDFium 运行环境生命周期守护
struct PdfiumScope {
    PdfiumScope() {
        FPDF_LIBRARY_CONFIG config;
        config.version = 2;
        config.m_pUserFontPaths = nullptr;
        config.m_pIsolate = nullptr;
        config.m_v8EmbedderSlot = 0;
        FPDF_InitLibraryWithConfig(&config);
        LOG_INFO("PdfHelper", "Initialized Google PDFium engine");
    }
    ~PdfiumScope() { FPDF_DestroyLibrary(); }
};

void EnsurePdfiumInitialized() {
    static PdfiumScope s_scope;
}

// 全局 PDF 文档互斥锁与单例缓存（杜绝频繁翻页或批量渲染时重复从磁盘解析大体量 PDF 结构）
std::mutex s_pdfDocMutex;
std::string s_cachedPdfPath;
FPDF_DOCUMENT s_cachedPdfDoc = nullptr;

void ClearCacheLocked() {
    if (s_cachedPdfDoc) {
        FPDF_CloseDocument(s_cachedPdfDoc);
        s_cachedPdfDoc = nullptr;
    }
    s_cachedPdfPath.clear();
}

FPDF_DOCUMENT GetOrLoadPdfDocLocked(const std::string& filePath) {
    if (s_cachedPdfDoc && s_cachedPdfPath == filePath) {
        return s_cachedPdfDoc;
    }
    ClearCacheLocked();

    FPDF_DOCUMENT doc = FPDF_LoadDocument(filePath.c_str(), nullptr);
    if (doc) {
        s_cachedPdfDoc = doc;
        s_cachedPdfPath = filePath;
    } else {
        LOG_WARN("PdfHelper", "Failed to load PDF document: " + filePath);
    }
    return doc;
}

} // namespace

namespace LinguaAlpaca {

void PdfHelper::ClearCache() {
    std::lock_guard<std::mutex> lock(s_pdfDocMutex);
    ClearCacheLocked();
}

bool PdfHelper::IsPdfFile(const std::string& filePath) {
    if (filePath.empty())
        return false;
    wxFileName fn(wxString::FromUTF8(filePath));
    wxString ext = fn.GetExt().Lower();
    return ext == "pdf";
}

int PdfHelper::GetPageCount(const std::string& filePath) {
    if (!wxFileExists(wxString::FromUTF8(filePath))) {
        return 0;
    }
    if (!IsPdfFile(filePath)) {
        return 1; // 普通图像视为单页
    }

    try {
        EnsurePdfiumInitialized();
        std::lock_guard<std::mutex> lock(s_pdfDocMutex);
        FPDF_DOCUMENT doc = GetOrLoadPdfDocLocked(filePath);
        if (!doc)
            return 0;
        return FPDF_GetPageCount(doc);
    } catch (const std::exception& ex) {
        LOG_WARN("PdfHelper", "Failed to query PDF page count: " + std::string(ex.what()));
        return 0;
    } catch (...) {
        LOG_WARN("PdfHelper", "Unknown exception while querying PDF page count");
        return 0;
    }
}

bool PdfHelper::RenderPage(const std::string& filePath, int pageIndex, wxImage& outImage, int targetWidth) {
    if (!wxFileExists(wxString::FromUTF8(filePath))) {
        return false;
    }
    if (!IsPdfFile(filePath)) {
        return outImage.LoadFile(wxString::FromUTF8(filePath));
    }

    try {
        EnsurePdfiumInitialized();
        FPDF_PAGE page = nullptr;
        double origW = 0.0;
        double origH = 0.0;

        {
            std::lock_guard<std::mutex> lock(s_pdfDocMutex);
            FPDF_DOCUMENT doc = GetOrLoadPdfDocLocked(filePath);
            if (!doc)
                return false;

            int totalPages = FPDF_GetPageCount(doc);
            if (pageIndex < 0 || pageIndex >= totalPages) {
                LOG_WARN("PdfHelper", "Page index out of range: " + std::to_string(pageIndex) + " / " + std::to_string(totalPages));
                return false;
            }

            page = FPDF_LoadPage(doc, pageIndex);
            if (!page) {
                LOG_ERROR("PdfHelper", "Failed to load page " + std::to_string(pageIndex + 1));
                return false;
            }

            origW = FPDF_GetPageWidthF(page);
            origH = FPDF_GetPageHeightF(page);
        }

        if (origW <= 0.0 || origH <= 0.0) {
            FPDF_ClosePage(page);
            return false;
        }

        double scale = 1.0;
        if (targetWidth > 0 && origW > 0.0) {
            scale = static_cast<double>(targetWidth) / origW;
        }

        int outW = static_cast<int>(std::round(origW * scale));
        int outH = static_cast<int>(std::round(origH * scale));
        if (outW <= 0 || outH <= 0) {
            FPDF_ClosePage(page);
            return false;
        }

        // 创建 PDFium 渲染位图 (0 表示白底背景，不带 alpha 通道)
        FPDF_BITMAP bitmap = FPDFBitmap_Create(outW, outH, 0);
        if (!bitmap) {
            FPDF_ClosePage(page);
            return false;
        }

        // 默认填充纯白背景 (防止透明背景 PDF 渲染为黑底)
        FPDFBitmap_FillRect(bitmap, 0, 0, outW, outH, 0xFFFFFFFF);

        // 使用 Google PDFium 渲染页面：
        // FPDF_ANNOT: 渲染全部可见文本、图形、掩模与标注
        // FPDF_REVERSE_BYTE_ORDER: 直接生成 RGBA 顺序字节，便于高效转换为 wxImage RGB 格式
        FPDF_RenderPageBitmap(bitmap, page, 0, 0, outW, outH, 0, FPDF_ANNOT | FPDF_REVERSE_BYTE_ORDER);

        const uint8_t* buffer = static_cast<const uint8_t*>(FPDFBitmap_GetBuffer(bitmap));
        int stride = FPDFBitmap_GetStride(bitmap);

        // 转换位图数据至 wxImage RGB 格式
        unsigned char* rgbData = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(outW) * outH * 3));
        if (!rgbData) {
            FPDFBitmap_Destroy(bitmap);
            FPDF_ClosePage(page);
            return false;
        }

        for (int y = 0; y < outH; ++y) {
            const uint8_t* srcRow = buffer + y * stride;
            unsigned char* dstRow = rgbData + static_cast<size_t>(y) * outW * 3;
            for (int x = 0; x < outW; ++x) {
                dstRow[x * 3] = srcRow[x * 4];
                dstRow[x * 3 + 1] = srcRow[x * 4 + 1];
                dstRow[x * 3 + 2] = srcRow[x * 4 + 2];
            }
        }

        FPDFBitmap_Destroy(bitmap);
        FPDF_ClosePage(page);

        // wxImage 接管已分配的 rgbData 内存
        outImage.Create(outW, outH, rgbData, false);
        return outImage.IsOk();
    } catch (const std::exception& ex) {
        LOG_ERROR("PdfHelper", "RenderPage exception: " + std::string(ex.what()));
        return false;
    } catch (...) {
        LOG_ERROR("PdfHelper", "RenderPage unknown exception");
        return false;
    }
}

std::string PdfHelper::RenderPageToTempFile(const std::string& filePath, int pageIndex, const std::string& tempDir, int targetWidth) {
    if (!wxFileExists(wxString::FromUTF8(filePath))) {
        return "";
    }
    if (!IsPdfFile(filePath)) {
        // 普通图像已是栅格图像，无需再次光栅化，直接返回原图路径（调用方严禁作为临时文件删除）
        return filePath;
    }

    try {
        wxImage img;
        if (!RenderPage(filePath, pageIndex, img, targetWidth)) {
            return "";
        }

        wxString dir = wxString::FromUTF8(tempDir);
        if (!wxDirExists(dir)) {
            wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        }

        wxFileName sourceFn(wxString::FromUTF8(filePath));
        wxString targetPath = wxString::Format("%s/%s_p%04d.png", dir, sourceFn.GetName(), pageIndex + 1);

        if (!img.SaveFile(targetPath, wxBITMAP_TYPE_PNG)) {
            LOG_ERROR("PdfHelper", "RenderPageToTempFile failed to save image to: " + std::string(targetPath.ToUTF8().data()));
            return "";
        }

        return targetPath.ToUTF8().data();
    } catch (const std::exception& ex) {
        LOG_ERROR("PdfHelper", "RenderPageToTempFile exception: " + std::string(ex.what()));
        return "";
    } catch (...) {
        LOG_ERROR("PdfHelper", "RenderPageToTempFile unknown exception");
        return "";
    }
}

namespace {

// RAII 守护：确保 FPDF_PAGE 与 FPDF_TEXTPAGE 在任意退出路径下对称释放
struct ScopedPdfPageText {
    FPDF_PAGE page{nullptr};
    FPDF_TEXTPAGE textPage{nullptr};
    ~ScopedPdfPageText() {
        if (textPage) {
            FPDFText_ClosePage(textPage);
        }
        if (page) {
            FPDF_ClosePage(page);
        }
    }
};

// 将 UTF-16LE 缓冲区安全转换为 UTF-8 字符串
std::string Utf16BufferToUtf8(const unsigned short* buf, int len) {
    if (!buf || len <= 0) return "";
    std::string out;
    out.reserve(static_cast<size_t>(len) * 2);

    int i = 0;
    while (i < len) {
        uint32_t cp = buf[i++];
        if (cp == 0) break;

        // 处理 UTF-16 代理对 (Surrogate Pair)
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (i < len && buf[i] >= 0xDC00 && buf[i] <= 0xDFFF) {
                uint32_t low = buf[i++];
                cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
            } else {
                cp = 0xFFFD;
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }

        // 过滤软连字符 (U+00AD Soft Hyphen)，统一转化为标准连字符或忽略以供后续断词拼接
        if (cp == 0x00AD) {
            cp = '-';
        }

        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// 判定 Unicode 码点是否属于损坏/私有区未映射乱码字符
bool IsGarbledCodepoint(uint32_t cp) {
    if (cp == 0xFFFD || cp == 0xFFFF) return true;
    // BMP Private Use Area (U+E000..U+F8FF) 与 Supplementary PUA
    if ((cp >= 0xE000 && cp <= 0xF8FF) || (cp >= 0xF0000 && cp <= 0x10FFFF)) {
        return true;
    }
    // 非法控制字符 (排除 \t, \n, \r)
    if (cp < 0x20 && cp != '\t' && cp != '\n' && cp != '\r') {
        return true;
    }
    return false;
}

// 判定 Unicode 码点是否属于专业数学公式符号区（例如求和、积分、偏导、数学斜体字母等）
bool IsMathFormulaCodepoint(uint32_t cp) {
    // Mathematical Operators (U+2200..U+22FF: ∀ ∂ ∃ ∅ ∆ ∇ ∈ ∑ √ ∞ ∫ ≈ ≠ ≡ ≤ ≥ 等)
    if (cp >= 0x2200 && cp <= 0x22FF) return true;
    // Miscellaneous Mathematical Symbols-A/B & Supplemental Mathematical Operators
    if ((cp >= 0x27C0 && cp <= 0x27EF) || (cp >= 0x2980 && cp <= 0x2AFF)) return true;
    // Mathematical Alphanumeric Symbols (U+1D400..U+1D7FF)
    if (cp >= 0x1D400 && cp <= 0x1D7FF) return true;
    return false;
}

// 判定行末尾是否为 CJK 多字节字符（用于无空格折行拼接）
bool EndsWithCjkByteSequence(std::string_view line) {
    if (line.size() < 3) return false;
    unsigned char b1 = static_cast<unsigned char>(line[line.size() - 3]);
    // UTF-8 3 字节编码范围 0xE3..0xEF 涵盖中文汉字、日文假名、韩文及全角标点
    return (b1 >= 0xE3 && b1 <= 0xEF);
}

// 判定行开头是否为 CJK 多字节字符
bool StartsWithCjkByteSequence(std::string_view line) {
    if (line.size() < 3) return false;
    unsigned char b1 = static_cast<unsigned char>(line[0]);
    return (b1 >= 0xE3 && b1 <= 0xEF);
}

// 判定新的一行是否为列表项或目录独立条目开头（若是则保留换行，不合并到上一行）
bool StartsWithListItemOrTocPattern(std::string_view line) {
    if (line.empty()) return false;
    static const std::regex s_listLineRegex(
        R"(^(?:(?:\d{1,3}[\.\)\、]|\(\d{1,3}\)|\[\d{1,3}\]|[\-\*\+\•\·])\s+|(?:Chapter|Section|Appendix|第[一二三四五六七八九十百0-9]+[章节篇部分条])\b))"
    );
    std::string s(line);
    return std::regex_search(s, s_listLineRegex);
}

} // namespace

bool PdfHelper::IsMathOrSymbolFontName(std::string_view fontName) {
    if (fontName.empty()) return false;

    // 剥离 PDF 常见的 6 位大写字母子集前缀 (如 "ABCDEF+CMMI10" -> "CMMI10")
    size_t plusPos = fontName.find('+');
    if (plusPos != std::string_view::npos && plusPos + 1 < fontName.size()) {
        fontName.remove_prefix(plusPos + 1);
    }

    std::string lower;
    lower.reserve(fontName.size());
    for (char c : fontName) {
        if (c == ' ' || c == '-' || c == '_') continue;
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    // 1. 包含显式 "math" 标识的数学字体 (如 CambriaMath, LatinModernMath, STIXMath, XITSMath, Asana-Math)
    if (lower.find("math") != std::string::npos) {
        return true;
    }

    // 2. LaTeX / Computer Modern / AMS / TeX 专用数学符号与公式斜体字体族前缀
    static const char* const kMathFontPrefixes[] = {
        "cmmi", "cmsy", "cmex", "msam", "msbm",
        "eufm", "eurm", "eusm", "lasy", "stmary",
        "wasy", "rsfso", "dsrom", "bbold", "mtextra",
        "mathematica", "euclid", "symbol",
        "txsy", "pxsy", "txex", "pxex",
        "txmi", "pxmi", "fourier"
    };

    for (const char* prefix : kMathFontPrefixes) {
        std::string_view p(prefix);
        if (lower.size() >= p.size() && std::string_view(lower).substr(0, p.size()) == p) {
            return true;
        }
    }

    return false;
}

std::string PdfHelper::UnwrapPdfParagraphLines(std::string_view rawText) {
    if (rawText.empty()) return "";

    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < rawText.size()) {
        size_t nextNl = rawText.find('\n', pos);
        std::string_view lineSv = (nextNl == std::string_view::npos)
                                      ? rawText.substr(pos)
                                      : rawText.substr(pos, nextNl - pos);
        // 清理行首尾空白与 \r
        size_t first = lineSv.find_first_not_of(" \t\r");
        if (first != std::string_view::npos) {
            size_t last = lineSv.find_last_not_of(" \t\r");
            std::string cleanLine(lineSv.substr(first, last - first + 1));
            lines.push_back(std::move(cleanLine));
        }
        if (nextNl == std::string_view::npos) break;
        pos = nextNl + 1;
    }

    if (lines.empty()) return "";
    if (lines.size() == 1) return lines[0];

    std::string result = lines[0];
    for (size_t i = 1; i < lines.size(); ++i) {
        const std::string& nextLine = lines[i];
        if (nextLine.empty()) continue;

        // 1. 若下一行显式以列表项编号/项目符号开头，保留换行以维持列表排版
        if (StartsWithListItemOrTocPattern(nextLine)) {
            result += "\n" + nextLine;
            continue;
        }

        // 2. 处理英文跨行断词连字符 (如 "pow-\nerful" -> "powerful")
        if (result.size() >= 2 && result.back() == '-') {
            unsigned char prevCh = static_cast<unsigned char>(result[result.size() - 2]);
            unsigned char nextCh = static_cast<unsigned char>(nextLine.front());
            if (std::isalpha(prevCh) && std::islower(nextCh)) {
                result.pop_back();
                result += nextLine;
                continue;
            }
        }

        // 3. CJK 字符之间直接无缝拼接（不插入多余空格）
        if (EndsWithCjkByteSequence(result) && StartsWithCjkByteSequence(nextLine)) {
            result += nextLine;
            continue;
        }

        // 4. 常规拉丁单词或中英混排折行：以单个空格拼接
        if (result.back() != ' ') {
            result.push_back(' ');
        }
        result += nextLine;
    }

    return result;
}

bool PdfHelper::ExtractPageBoxesText(
    const std::string& filePath,
    int pageIndex,
    int renderedWidth,
    int renderedHeight,
    const std::vector<PdfBoxRegion>& boxes,
    std::vector<PdfBoxTextResult>& outResults
) {
    outResults.assign(boxes.size(), PdfBoxTextResult{});
    if (boxes.empty() || renderedWidth <= 0 || renderedHeight <= 0) {
        return false;
    }
    if (!IsPdfFile(filePath) || !wxFileExists(wxString::FromUTF8(filePath))) {
        return false;
    }

    try {
        EnsurePdfiumInitialized();
        std::lock_guard<std::mutex> lock(s_pdfDocMutex);

        FPDF_DOCUMENT doc = GetOrLoadPdfDocLocked(filePath);
        if (!doc) return false;

        int totalPages = FPDF_GetPageCount(doc);
        if (pageIndex < 0 || pageIndex >= totalPages) {
            return false;
        }

        ScopedPdfPageText guard;
        guard.page = FPDF_LoadPage(doc, pageIndex);
        if (!guard.page) return false;

        double origW = FPDF_GetPageWidthF(guard.page);
        double origH = FPDF_GetPageHeightF(guard.page);
        if (origW <= 0.0 || origH <= 0.0) {
            return false;
        }

        guard.textPage = FPDFText_LoadPage(guard.page);
        if (!guard.textPage) return false;

        int totalChars = FPDFText_CountChars(guard.textPage);
        if (totalChars <= 0) {
            return false; // 扫描版 PDF 或无内嵌文字层页面
        }

        // 预扫描单页全部可见字符的坐标中心、编码有效性及字体属性（单页仅遍历一次）
        struct CharAttr {
            double cx{0.0};
            double cy{0.0};
            bool validBox{false};
            bool isGarbled{false};
            bool isFormula{false};
        };

        std::vector<CharAttr> pageChars;
        pageChars.reserve(static_cast<size_t>(totalChars));

        FPDF_PAGEOBJECT lastTextObj = nullptr;
        bool lastObjIsMathFont = false;

        for (int idx = 0; idx < totalChars; ++idx) {
            if (FPDFText_IsGenerated(guard.textPage, idx) == 1) {
                continue;
            }
            uint32_t unicode = FPDFText_GetUnicode(guard.textPage, idx);
            if (unicode == ' ' || unicode == '\t' || unicode == '\r' || unicode == '\n') {
                continue;
            }

            CharAttr attr;
            FS_RECTF rect{};
            if (FPDFText_GetLooseCharBox(guard.textPage, idx, &rect)) {
                attr.cx = 0.5 * (static_cast<double>(rect.left) + static_cast<double>(rect.right));
                attr.cy = 0.5 * (static_cast<double>(rect.bottom) + static_cast<double>(rect.top));
                attr.validBox = true;
            } else {
                double l = 0, r = 0, b = 0, t = 0;
                if (FPDFText_GetCharBox(guard.textPage, idx, &l, &r, &b, &t)) {
                    attr.cx = 0.5 * (l + r);
                    attr.cy = 0.5 * (b + t);
                    attr.validBox = true;
                }
            }
            if (!attr.validBox) continue;

            if (unicode == 0 || FPDFText_HasUnicodeMapError(guard.textPage, idx) == 1 || IsGarbledCodepoint(unicode)) {
                attr.isGarbled = true;
            }

            FPDF_PAGEOBJECT curObj = FPDFText_GetTextObject(guard.textPage, idx);
            if (curObj != lastTextObj) {
                lastTextObj = curObj;
                lastObjIsMathFont = false;
                char fontBuf[128] = {0};
                int fontFlags = 0;
                unsigned long nameLen = FPDFText_GetFontInfo(guard.textPage, idx, fontBuf, sizeof(fontBuf) - 1, &fontFlags);
                if (nameLen > 0) {
                    lastObjIsMathFont = IsMathOrSymbolFontName(fontBuf);
                }
            }

            if (lastObjIsMathFont || IsMathFormulaCodepoint(unicode)) {
                attr.isFormula = true;
            }

            pageChars.push_back(attr);
        }

        if (pageChars.empty()) {
            return false;
        }

        const double scaleX = static_cast<double>(renderedWidth) / origW;
        const double scaleY = static_cast<double>(renderedHeight) / origH;
        const double kTolerancePt = 1.5;

        bool anyBoxExtracted = false;

        for (size_t bIdx = 0; bIdx < boxes.size(); ++bIdx) {
            const auto& box = boxes[bIdx];
            auto& res = outResults[bIdx];
            if (box.x2 <= box.x1 || box.y2 <= box.y1) {
                continue;
            }

            // 像素坐标系 (左上原点, Y 向下) -> PDF User Space (左下原点, Y 向上)
            double pdfLeft   = (std::max)(0.0, static_cast<double>(box.x1) / scaleX - kTolerancePt);
            double pdfRight  = (std::min)(origW, static_cast<double>(box.x2) / scaleX + kTolerancePt);
            double pdfTop    = (std::min)(origH, origH - static_cast<double>(box.y1) / scaleY + kTolerancePt);
            double pdfBottom = (std::max)(0.0, origH - static_cast<double>(box.y2) / scaleY - kTolerancePt);

            int charsInBox = 0;
            int garbledCount = 0;
            int formulaCount = 0;

            for (const auto& ch : pageChars) {
                if (ch.cx >= pdfLeft && ch.cx <= pdfRight && ch.cy >= pdfBottom && ch.cy <= pdfTop) {
                    charsInBox++;
                    if (ch.isGarbled) garbledCount++;
                    if (ch.isFormula) formulaCount++;
                }
            }

            if (charsInBox == 0) {
                continue;
            }
            if (garbledCount > 0) {
                res.hasGarbledChar = true;
                continue;
            }
            if (formulaCount > 0) {
                res.hasFormulaFont = true;
                continue;
            }

            int u16Count = FPDFText_GetBoundedText(guard.textPage, pdfLeft, pdfTop, pdfRight, pdfBottom, nullptr, 0);
            if (u16Count <= 0) {
                continue;
            }

            std::vector<unsigned short> u16Buf(static_cast<size_t>(u16Count) + 1, 0);
            int extracted = FPDFText_GetBoundedText(
                guard.textPage, pdfLeft, pdfTop, pdfRight, pdfBottom,
                u16Buf.data(), u16Count + 1
            );
            if (extracted <= 0) {
                continue;
            }

            std::string rawUtf8 = Utf16BufferToUtf8(u16Buf.data(), extracted);
            std::string unwrapped = UnwrapPdfParagraphLines(rawUtf8);
            if (!unwrapped.empty()) {
                res.text = std::move(unwrapped);
                res.success = true;
                anyBoxExtracted = true;
            }
        }

        return anyBoxExtracted;
    } catch (const std::exception& ex) {
        LOG_WARN("PdfHelper", "ExtractPageBoxesText exception: " + std::string(ex.what()));
        return false;
    } catch (...) {
        LOG_WARN("PdfHelper", "ExtractPageBoxesText unknown exception");
        return false;
    }
}

} // namespace LinguaAlpaca
