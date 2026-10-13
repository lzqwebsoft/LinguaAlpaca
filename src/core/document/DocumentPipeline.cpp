#pragma execution_character_set("utf-8")
#include "DocumentPipeline.hpp"
#include "core/pdf/PdfHelper.hpp"
#include "core/Logger.hpp"

#include <nlohmann/json.hpp>
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/stdpaths.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <base64.hpp>

#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <algorithm>
#include <regex>
#include <string_view>
#include <chrono>

using json = nlohmann::json;

namespace LinguaAlpaca {

// ============================================================================
// 1. 内部高内聚数据模型与零拷贝辅助工具集 (Anonymous Namespace)
// ============================================================================
namespace {

inline bool StartsWith(std::string_view sv, std::string_view prefix) {
    return sv.size() >= prefix.size() && sv.substr(0, prefix.size()) == prefix;
}

inline bool EndsWith(std::string_view sv, std::string_view suffix) {
    return sv.size() >= suffix.size() && sv.substr(sv.size() - suffix.size()) == suffix;
}

std::string TrimString(std::string_view str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return std::string(str.substr(first, (last - first + 1)));
}

std::filesystem::path ToFsPath(const wxString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.ToStdWstring());
#else
    return std::filesystem::path(path.ToUTF8().data());
#endif
}

bool IsPredominantlyEnglish(const std::string& text) {
    if (text.empty())
        return false;
    int latinCount = 0;
    int cjkCount = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            latinCount++;
        } else if (c >= 0xE4 && c <= 0xE9 && i + 2 < text.size()) {
            cjkCount++;
            i += 2;
        }
    }
    return (latinCount > 15 && latinCount > cjkCount * 2);
}

// 严格判断两路径是否指向同一物理文件 (处理 Windows 大小写、正反斜杠及符号链接)
bool IsSameFilePath(const std::string& path1, const std::string& path2) {
    if (path1.empty() || path2.empty())
        return false;
    if (path1 == path2)
        return true;
    try {
        std::filesystem::path p1 = std::filesystem::u8path(path1);
        std::filesystem::path p2 = std::filesystem::u8path(path2);
        if (std::filesystem::exists(p1) && std::filesystem::exists(p2)) {
            return std::filesystem::equivalent(p1, p2);
        }
    } catch (...) {}
    wxFileName fn1(wxString::FromUTF8(path1));
    wxFileName fn2(wxString::FromUTF8(path2));
    fn1.Normalize(wxPATH_NORM_ALL);
    fn2.Normalize(wxPATH_NORM_ALL);
    return fn1.GetFullPath().CmpNoCase(fn2.GetFullPath()) == 0;
}

// 安全回收临时文件：绝对不删除用户原始输入文件，且仅允许清理临时缓存目录内的文件
void SafeRemoveTempFile(const std::string& pathToRemove, const std::string& originalInputFile, const wxString& tempDir, const std::string& normalizedTempPrefix = "") {
    if (pathToRemove.empty() || StartsWith(pathToRemove, "data:image/"))
        return;

    // 1. 绝对防御：若目标路径与用户原始输入文档/图片相同，绝不删除
    if (IsSameFilePath(pathToRemove, originalInputFile)) {
        LOG_DEBUG("DocumentPipeline", "Skip deleting original input file: " + pathToRemove);
        return;
    }

    wxString wxPath = wxString::FromUTF8(pathToRemove);
    if (!wxFileExists(wxPath)) {
        return;
    }

    // 2. 深度防御：确保仅删除位于 tempDir 临时工作缓存目录内的临时文件
    wxFileName targetFn(wxPath);
    targetFn.Normalize(wxPATH_NORM_ALL);
    std::string targetNorm = targetFn.GetFullPath().Lower().ToStdString();

    std::string tempNormPrefix = normalizedTempPrefix;
    if (tempNormPrefix.empty()) {
        wxFileName tempDirFn(tempDir);
        tempDirFn.Normalize(wxPATH_NORM_ALL);
        wxString tempNorm = tempDirFn.GetFullPath();
        if (!tempNorm.EndsWith("/") && !tempNorm.EndsWith("\\")) {
            tempNorm += wxFileName::GetPathSeparator();
        }
        tempNormPrefix = tempNorm.Lower().ToStdString();
    }

    if (StartsWith(targetNorm, tempNormPrefix)) {
        wxRemoveFile(wxPath);
    } else {
        LOG_WARN("DocumentPipeline", "Prevented deletion of file outside temp cache directory: " + pathToRemove);
    }
}

// 剥离大模型返回中可能包裹的 Markdown 代码块围栏 (```markdown ... ``` 或 ``` ... ```)
std::string StripMarkdownFences(const std::string& text) {
    std::string_view sv = text;
    size_t first = sv.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return "";
    size_t last = sv.find_last_not_of(" \t\r\n");
    sv = sv.substr(first, last - first + 1);

    if (StartsWith(sv, "```markdown")) {
        sv.remove_prefix(11);
    } else if (StartsWith(sv, "```")) {
        sv.remove_prefix(3);
    }
    if (EndsWith(sv, "```")) {
        sv.remove_suffix(3);
    }
    first = sv.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return "";
    last = sv.find_last_not_of(" \t\r\n");
    return std::string(sv.substr(first, last - first + 1));
}

// 高效 O(1) 剥离公式外层包裹的 $$、\[\]、\(\)、$ 并格式化为标准独立公式块
std::string CleanAndFormatFormula(const std::string& rawFormula) {
    std::string_view sv = rawFormula;
    size_t first = sv.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return "$$\n\n$$";
    }
    size_t last = sv.find_last_not_of(" \t\r\n");
    sv = sv.substr(first, last - first + 1);

    bool stripped = true;
    while (stripped && sv.size() >= 2) {
        stripped = false;
        if (sv.size() >= 4 && ((StartsWith(sv, "$$") && EndsWith(sv, "$$")) ||
                               (StartsWith(sv, "\\[") && EndsWith(sv, "\\]")) ||
                               (StartsWith(sv, "\\(") && EndsWith(sv, "\\)")))) {
            sv = sv.substr(2, sv.size() - 4);
            stripped = true;
        } else if (sv.size() >= 2 && sv.front() == '$' && sv.back() == '$' && sv[1] != '$') {
            sv = sv.substr(1, sv.size() - 2);
            stripped = true;
        }
        if (stripped) {
            first = sv.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) {
                sv = {};
                break;
            }
            last = sv.find_last_not_of(" \t\r\n");
            sv = sv.substr(first, last - first + 1);
        }
    }
    return "$$\n" + std::string(sv) + "\n$$";
}

// 规范追加段落内容至 Markdown 字符串，自动保证统一空行分隔
void AppendToMarkdown(std::string& md, const std::string& content) {
    if (content.empty()) return;
    if (!md.empty()) {
        while (!md.empty() && (md.back() == '\n' || md.back() == '\r')) {
            md.pop_back();
        }
        md += "\n\n";
    }
    md += content;
}

// 单例共享的页码正则表达式
const std::regex& GetPageNumPattern() {
    static const std::regex s_pattern(
        R"(^[\s\-\·\~\—\#\[\(第·\.]*(?:page|p\.|no\.)?[\s·\.]*(\d+|[IVXLCDMivxlcdm]+)[\s\-\·\~\—\#\]\)页\.]*(?:page|p\.)?[\s\.]*$)",
        std::regex::icase
    );
    return s_pattern;
}

// 预处理裁剪元素信息结构
struct PreprocessedElement {
    LayoutElement elem;
    int x1{0};
    int y1{0};
    int x2{0};
    int y2{0};
    int cropW{0};
    int cropH{0};
    bool isImage{false};
    std::string ocrInputSource; // 若为非 Image 则为内存 base64 URI (data:image/jpeg;base64,...)
    std::string figRelPath;     // 若为 Image 则为 imgs/img_p1_box_...jpg
    std::string imgTag;         // 若为 Image 则为标准居中 HTML 标签
    std::string taskType{"ocr"};
};

// 安全序列化 JSON，避免由于异常导致中断或数据丢失
std::string SafeDumpJson(const json& j, int indent = 2, const std::string& fallback = "{}") {
    try {
        return j.dump(indent, ' ', false, json::error_handler_t::replace);
    } catch (const std::exception& e) {
        LOG_WARN("DocumentPipeline", std::string("Failed to dump json: ") + e.what());
        return fallback;
    } catch (...) {
        LOG_WARN("DocumentPipeline", "Unknown exception dumping json");
        return fallback;
    }
}

// 裁剪并持久化单个版面元素切片
bool CropAndSaveElement(
    const wxImage& pageImg,
    const LayoutElement& elem,
    size_t elIdx,
    int p,
    int x1, int y1, int x2, int y2,
    int cropW, int cropH,
    int pImgW,
    const wxString& figuresDir,
    const wxString& tempCacheDir,
    DocumentPipeline::PreprocessedPage& pageData
);

} // namespace

// ============================================================================
// 2. 内部会话与派发数据结构实现
// ============================================================================

struct DocumentPipeline::ParseSession {
    std::string inputFilePath;
    std::string baseDocName;
    std::string actualOutputDir;
    wxString figuresDir;
    wxString tempCacheDir;
    std::string normalizedTempCacheDir;
    int totalPages{0};
    int actualStartPage{0};
    json fullDocJson;
    std::string fullMarkdown;
    bool translateEnglish{false};
};

struct DocumentPipeline::OcrDispatchResult {
    std::vector<std::string> recognizedResults;
    std::vector<bool> elementSuccess;
    bool pageInterrupted{false};
    bool isServerDown{false};
    std::string failureReason;
};

struct DocumentPipeline::AssembledPage {
    std::string pageMarkdown;
    json pageJson = json::object();
};

struct DocumentPipeline::PreprocessedPage {
    int pageIndex{0}; // 0-indexed
    int pageWidth{0};
    int pageHeight{0};
    std::string pageImgPath;
    DocumentLayoutResult layoutResult;
    std::vector<PreprocessedElement> elements;
    std::vector<std::string> tempCropFiles;
    std::vector<std::string> figureFilesCreated;
    bool success{false};
    std::string error;

    void CleanRemainingTempFiles(const std::string& originalInputFile, const wxString& tempDir, bool cleanFigures = false) {
        for (const auto& f : tempCropFiles) {
            SafeRemoveTempFile(f, originalInputFile, tempDir);
        }
        tempCropFiles.clear();
        if (!pageImgPath.empty()) {
            SafeRemoveTempFile(pageImgPath, originalInputFile, tempDir);
            pageImgPath.clear();
        }
        if (cleanFigures) {
            for (const auto& fig : figureFilesCreated) {
                wxString wFig = wxString::FromUTF8(fig);
                if (wxFileExists(wFig)) {
                    wxRemoveFile(wFig);
                }
            }
            figureFilesCreated.clear();
        }
    }
};

namespace {

bool CropAndSaveElement(
    const wxImage& pageImg,
    const LayoutElement& elem,
    size_t elIdx,
    int p,
    int x1, int y1, int x2, int y2,
    int cropW, int cropH,
    int pImgW,
    const wxString& figuresDir,
    const wxString& tempCacheDir,
    DocumentPipeline::PreprocessedPage& pageData
) {
    try {
        wxImage crop = pageImg.GetSubImage(wxRect(x1, y1, cropW, cropH));
        if (!crop.IsOk()) {
            LOG_WARN("DocumentPipeline", "Failed to crop valid sub-image at [" + std::to_string(x1) + ", " + std::to_string(y1) + "]");
            return false;
        }

        PreprocessedElement pElem;
        pElem.elem = elem;
        pElem.x1 = x1;
        pElem.y1 = y1;
        pElem.x2 = x2;
        pElem.y2 = y2;
        pElem.cropW = cropW;
        pElem.cropH = cropH;

        if (elem.type == LayoutElementType::Image) {
            pElem.isImage = true;
            wxString figFileName = wxString::Format("img_p%d_box_%d_%d_%d_%d.jpg", p + 1, x1, y1, x2, y2);
            wxString absFigPath = figuresDir + "/" + figFileName;
            crop.SetOption(wxIMAGE_OPTION_QUALITY, 92);
            if (!crop.SaveFile(absFigPath, wxBITMAP_TYPE_JPEG)) {
                LOG_WARN("DocumentPipeline", "Failed to save figure image to: " + absFigPath.ToStdString());
                return false;
            }
            pageData.figureFilesCreated.push_back(absFigPath.ToUTF8().data());

            std::string relPath = "imgs/" + std::string(figFileName.ToUTF8().data());
            int widthPct = static_cast<int>(std::round(static_cast<double>(cropW) * 100.0 / pImgW));
            if (widthPct >= 80) {
                widthPct = 100;
            } else if (widthPct < 25) {
                widthPct = 25;
            }

            pElem.figRelPath = relPath;
            pElem.imgTag = "<div style=\"text-align: center;\"><img src=\"" + relPath + "\" alt=\"Image\" width=\"" + std::to_string(widthPct) +
                           "%\" style=\"max-width: 100%; max-height: 600px; object-fit: contain;\" /></div>";
        } else {
            pElem.isImage = false;
            // ★★★ 核心性能优化：内存直通编码为 JPEG 92 质量 Base64 URI，彻底消除磁盘临时文件与 I/O 开销
            wxMemoryOutputStream memStream;
            crop.SetOption(wxIMAGE_OPTION_QUALITY, 92);
            bool encodedOk = false;
            if (crop.SaveFile(memStream, wxBITMAP_TYPE_JPEG)) {
                wxStreamBuffer* buf = memStream.GetOutputStreamBuffer();
                std::string encoded = base64::encode(static_cast<const char*>(buf->GetBufferStart()), buf->GetBufferSize());
                pElem.ocrInputSource = "data:image/jpeg;base64," + encoded;
                encodedOk = true;
            } else {
                wxMemoryOutputStream pngStream;
                if (crop.SaveFile(pngStream, wxBITMAP_TYPE_PNG)) {
                    wxStreamBuffer* buf = pngStream.GetOutputStreamBuffer();
                    std::string encoded = base64::encode(static_cast<const char*>(buf->GetBufferStart()), buf->GetBufferSize());
                    pElem.ocrInputSource = "data:image/png;base64," + encoded;
                    encodedOk = true;
                }
            }

            if (!encodedOk) {
                LOG_WARN("DocumentPipeline", "Failed to encode crop sub-image to memory stream at [" + std::to_string(x1) + ", " + std::to_string(y1) + "]");
                return false;
            }

            std::string taskType = "ocr";
            if (elem.type == LayoutElementType::Table) {
                taskType = "table";
            } else if (elem.type == LayoutElementType::Formula) {
                taskType = "formula";
            } else if (elem.type == LayoutElementType::Chart) {
                taskType = "chart";
            }
            pElem.taskType = taskType;
        }

        pageData.elements.push_back(std::move(pElem));
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("DocumentPipeline", std::string("Exception processing element ") + std::to_string(elIdx) + ": " + e.what());
        return false;
    } catch (...) {
        LOG_WARN("DocumentPipeline", "Unknown exception processing element " + std::to_string(elIdx));
        return false;
    }
}

void DrainFutureAndClean(std::future<std::shared_ptr<DocumentPipeline::PreprocessedPage>>& fut, const std::string& inputFilePath, const wxString& tempCacheDir) {
    if (!fut.valid()) return;
    try {
        auto nextData = fut.get();
        if (nextData) {
            nextData->CleanRemainingTempFiles(inputFilePath, tempCacheDir, true);
        }
    } catch (const std::exception& e) {
        LOG_WARN("DocumentPipeline", std::string("DrainFutureAndClean exception: ") + e.what());
    } catch (...) {
        LOG_WARN("DocumentPipeline", "DrainFutureAndClean unknown exception");
    }
}

std::shared_ptr<DocumentPipeline::PreprocessedPage> PreprocessStageOne(
    int p, int totalPages,
    const std::string& inputFilePath,
    const wxString& tempCacheDir,
    const wxString& figuresDir,
    const std::shared_ptr<DocLayoutEngine>& layoutEngine,
    const std::shared_ptr<std::atomic<bool>>& aliveToken,
    const std::atomic<bool>& shouldStop,
    DocProgressCallback onProgress = nullptr,
    const std::string& currentFullMarkdown = ""
) {
    if (shouldStop.load() || !aliveToken->load()) {
        return nullptr;
    }

    auto pageData = std::make_shared<DocumentPipeline::PreprocessedPage>();
    pageData->pageIndex = p;

    if (onProgress && aliveToken->load() && !shouldStop.load()) {
        std::string status = "第 " + std::to_string(p + 1) + " / " + std::to_string(totalPages) + " 页: 正在光栅化...";
        onProgress(p, totalPages, status, currentFullMarkdown);
    }

    wxImage pageImg;
    if (!PdfHelper::RenderPage(inputFilePath, p, pageImg, 1600) || !pageImg.IsOk()) {
        LOG_ERROR("DocumentPipeline", "Failed to rasterize page: " + std::to_string(p + 1));
        pageData->success = false;
        pageData->error = "光栅化失败";
        return pageData;
    }

    if (shouldStop.load() || !aliveToken->load()) {
        pageImg.Destroy();
        pageData->CleanRemainingTempFiles(inputFilePath, tempCacheDir, true);
        return nullptr;
    }

    int pImgW = pageImg.GetWidth();
    int pImgH = pageImg.GetHeight();
    pageData->pageWidth = pImgW;
    pageData->pageHeight = pImgH;

    if (DocLayoutEngine::IsImageContentEmpty(pageImg)) {
        LOG_INFO("DocumentPipeline", "第 " + std::to_string(p + 1) + " 页判定为空白页，跳过版面分析与 OCR 视觉推理");
        pageData->elements.clear();
        pageData->success = true;
        pageImg.Destroy();
        return pageData;
    }

    if (onProgress && aliveToken->load() && !shouldStop.load()) {
        std::string status = "第 " + std::to_string(p + 1) + " / " + std::to_string(totalPages) + " 页: 版面分析中 (检测标题/表格/公式/分栏)...";
        onProgress(p, totalPages, status, currentFullMarkdown);
    }

    pageData->layoutResult.pageIndex = p + 1;
    if (layoutEngine) {
        layoutEngine->AnalyzeLayout(pageImg, pageData->layoutResult);
    }

    if (shouldStop.load() || !aliveToken->load()) {
        pageImg.Destroy();
        pageData->CleanRemainingTempFiles(inputFilePath, tempCacheDir, true);
        return nullptr;
    }

    for (size_t elIdx = 0; elIdx < pageData->layoutResult.elements.size(); ++elIdx) {
        if (shouldStop.load() || !aliveToken->load()) {
            break;
        }

        const auto& elem = pageData->layoutResult.elements[elIdx];
        int left = (std::min)(elem.x1, elem.x2);
        int right = (std::max)(elem.x1, elem.x2);
        int top = (std::min)(elem.y1, elem.y2);
        int bottom = (std::max)(elem.y1, elem.y2);

        int x1 = (std::clamp)(left, 0, pImgW);
        int y1 = (std::clamp)(top, 0, pImgH);
        int x2 = (std::clamp)(right, 0, pImgW);
        int y2 = (std::clamp)(bottom, 0, pImgH);
        int cropW = x2 - x1;
        int cropH = y2 - y1;
        if (cropW < 6 || cropH < 6)
            continue;
        if (x1 + cropW > pImgW)
            cropW = pImgW - x1;
        if (y1 + cropH > pImgH)
            cropH = pImgH - y1;
        if (cropW < 6 || cropH < 6)
            continue;

        if (elem.labelName == "number" || elem.labelName == "formula_number") {
            if (DocumentPipeline::IsHeaderOrFooterPageNumber("", elem.labelName, x1, y1, x2, y2, pImgW, pImgH)) {
                LOG_DEBUG("DocumentPipeline", "Preprocess: skipped page number element '" + elem.labelName +
                          "' at box [" + std::to_string(x1) + ", " + std::to_string(y1) + ", " +
                          std::to_string(x2) + ", " + std::to_string(y2) + "]");
                continue;
            }
        }

        CropAndSaveElement(pageImg, elem, elIdx, p, x1, y1, x2, y2, cropW, cropH, pImgW, figuresDir, tempCacheDir, *pageData);
    }

    pageImg.Destroy();

    if (shouldStop.load() || !aliveToken->load()) {
        pageData->CleanRemainingTempFiles(inputFilePath, tempCacheDir, true);
        return nullptr;
    }

    pageData->success = true;
    return pageData;
}

std::future<std::shared_ptr<DocumentPipeline::PreprocessedPage>> LaunchPreprocessStageOneAsync(
    int p, int totalPages,
    const std::string& inputFilePath,
    const wxString& tempCacheDir,
    const wxString& figuresDir,
    const std::shared_ptr<DocLayoutEngine>& layoutEngine,
    const std::shared_ptr<std::atomic<bool>>& aliveToken,
    const std::atomic<bool>& shouldStop,
    DocProgressCallback onProgress = nullptr,
    const std::string& currentFullMarkdown = ""
) {
    return std::async(std::launch::async, [=, &shouldStop]() {
        try {
            return PreprocessStageOne(p, totalPages, inputFilePath, tempCacheDir, figuresDir, layoutEngine, aliveToken, shouldStop, onProgress, currentFullMarkdown);
        } catch (const std::exception& e) {
            LOG_ERROR("DocumentPipeline", std::string("PreprocessStageOne exception: ") + e.what());
            return std::shared_ptr<DocumentPipeline::PreprocessedPage>(nullptr);
        } catch (...) {
            LOG_ERROR("DocumentPipeline", "PreprocessStageOne unknown exception");
            return std::shared_ptr<DocumentPipeline::PreprocessedPage>(nullptr);
        }
    });
}

} // namespace

// ============================================================================
// 3. DocumentPipeline 公共辅助与算法实现
// ============================================================================

bool DocumentPipeline::IsImageContentEmpty(const wxImage& img) {
    return DocLayoutEngine::IsImageContentEmpty(img);
}

std::string DocumentPipeline::ConvertOtslToHtml(const std::string& otslStr) {
    if (otslStr.empty() || otslStr.find("<fcel>") == std::string::npos) {
        return otslStr;
    }

    struct TableCellData {
        std::string type;
        std::string text;
        int rowspan{1};
        int colspan{1};
    };

    // 1. 按行分割 (<nl>)
    std::vector<std::string> rowTokens;
    size_t pos = 0;
    while (pos < otslStr.size()) {
        size_t nlPos = otslStr.find("<nl>", pos);
        if (nlPos == std::string::npos) {
            std::string row = otslStr.substr(pos);
            if (!row.empty())
                rowTokens.push_back(row);
            break;
        }
        rowTokens.push_back(otslStr.substr(pos, nlPos - pos));
        pos = nlPos + 4;
    }

    std::vector<std::vector<TableCellData>> grid;
    for (const auto& rowStr : rowTokens) {
        std::vector<TableCellData> rowCells;
        size_t i = 0;
        while (i < rowStr.size()) {
            size_t tagStart = rowStr.find('<', i);
            if (tagStart == std::string::npos)
                break;
            size_t tagEnd = rowStr.find('>', tagStart);
            if (tagEnd == std::string::npos)
                break;

            std::string tag = rowStr.substr(tagStart + 1, tagEnd - tagStart - 1);
            std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });

            if (tag == "fcel" || tag == "ecel" || tag == "lcel" || tag == "ucel" || tag == "xcel") {
                size_t nextTag = rowStr.find('<', tagEnd + 1);
                std::string cellText;
                if (nextTag == std::string::npos) {
                    cellText = rowStr.substr(tagEnd + 1);
                    i = rowStr.size();
                } else {
                    cellText = rowStr.substr(tagEnd + 1, nextTag - (tagEnd + 1));
                    i = nextTag;
                }
                cellText = TrimString(cellText);
                rowCells.push_back({tag, cellText, 1, 1});
            } else {
                i = tagEnd + 1;
            }
        }
        if (!rowCells.empty()) {
            grid.push_back(std::move(rowCells));
        }
    }

    if (grid.empty())
        return otslStr;

    int maxCols = 0;
    for (const auto& row : grid) {
        if (static_cast<int>(row.size()) > maxCols) {
            maxCols = static_cast<int>(row.size());
        }
    }
    for (auto& row : grid) {
        while (static_cast<int>(row.size()) < maxCols) {
            row.push_back({"ecel", "", 1, 1});
        }
    }

    int numRows = static_cast<int>(grid.size());
    int numCols = maxCols;

    for (int r = 0; r < numRows; ++r) {
        for (int c = 0; c < numCols; ++c) {
            auto& cell = grid[r][c];
            if (cell.type == "fcel" || cell.type == "ecel") {
                int cIter = c + 1;
                while (cIter < numCols && (grid[r][cIter].type == "lcel" || grid[r][cIter].type == "xcel")) {
                    cell.colspan++;
                    cIter++;
                }

                int rIter = r + 1;
                while (rIter < numRows && (grid[rIter][c].type == "ucel" || grid[rIter][c].type == "xcel")) {
                    cell.rowspan++;
                    rIter++;
                }
            }
        }
    }

    int headerRows = 1;
    for (int c = 0; c < numCols; ++c) {
        const auto& cell = grid[0][c];
        if ((cell.type == "fcel" || cell.type == "ecel") && cell.rowspan > headerRows) {
            headerRows = cell.rowspan;
        }
    }

    auto formatCellContent = [](const std::string& rawText) -> std::string {
        std::string text = TrimString(rawText);
        if (text.empty()) return "";

        // 1. 剥离末尾游离或未闭合的 \(
        if (text.size() >= 2 && text.compare(text.size() - 2, 2, "\\(") == 0) {
            text = TrimString(text.substr(0, text.size() - 2));
        }
        // 剥离开头游离未闭合的 \)
        if (text.size() >= 2 && text.compare(0, 2, "\\)") == 0) {
            text = TrimString(text.substr(2));
        }

        // 2. 基础 HTML 实体转义
        std::string escaped;
        if (text.find_first_of("&<>\"'") == std::string::npos) {
            escaped = text;
        } else {
            escaped.reserve(text.size() + 16);
            for (char ch : text) {
                if (ch == '&') escaped += "&amp;";
                else if (ch == '<') escaped += "&lt;";
                else if (ch == '>') escaped += "&gt;";
                else if (ch == '"') escaped += "&quot;";
                else if (ch == '\'') escaped += "&#039;";
                else escaped += ch;
            }
        }

        // 3. 将常见注解/上标模式转换为语义化 <sup> 标签 (如 7.5^{①}, 7.5\(^{①}\), 7.5^①, \(^{[1]}\) 等)
        static const std::regex s_supRegex(R"((?:\\\(|\$)?\^\{+([^{}]+?)\}+(?:\\\)|\$)?|(?:\\\(|\$)?\^([0-9①-⑩\*]+)(?:\\\)|\$)?)");
        return std::regex_replace(escaped, s_supRegex, "<sup>$1$2</sup>");
    };

    std::string theadHtml;
    std::string tbodyHtml;
    theadHtml.reserve(otslStr.size());
    tbodyHtml.reserve(otslStr.size());

    for (int r = 0; r < numRows; ++r) {
        bool isHeaderRow = (r < headerRows);
        std::string tag = isHeaderRow ? "th" : "td";
        std::string rowHtml = "  <tr>\n";

        for (int c = 0; c < numCols; ++c) {
            const auto& cell = grid[r][c];
            if (cell.type == "fcel" || cell.type == "ecel") {
                std::string attrs;
                if (cell.rowspan > 1) {
                    attrs += " rowspan=\"" + std::to_string(cell.rowspan) + "\"";
                }
                if (cell.colspan > 1) {
                    attrs += " colspan=\"" + std::to_string(cell.colspan) + "\"";
                }
                rowHtml += "    <" + tag + attrs + ">" + formatCellContent(cell.text) + "</" + tag + ">\n";
            }
        }
        rowHtml += "  </tr>\n";

        if (isHeaderRow) {
            theadHtml += rowHtml;
        } else {
            tbodyHtml += rowHtml;
        }
    }

    std::string tableHtml = "<table>\n";
    tableHtml.reserve(theadHtml.size() + tbodyHtml.size() + 32);
    if (!theadHtml.empty()) {
        tableHtml += "<thead>\n" + theadHtml + "</thead>\n";
    }
    if (!tbodyHtml.empty()) {
        tableHtml += "<tbody>\n" + tbodyHtml + "</tbody>\n";
    }
    tableHtml += "</table>";

    return tableHtml;
}

bool DocumentPipeline::IsHeaderOrFooterPageNumber(
    const std::string& text,
    const std::string& labelName,
    int x1, int y1, int x2, int y2,
    int pageW, int pageH
) {
    if (labelName == "footnote" || labelName == "vision_footnote" ||
        labelName == "doc_title" || labelName == "paragraph_title") {
        return false;
    }

    if (pageW <= 0 || pageH <= 0) {
        if (labelName == "number" || labelName == "formula_number") {
            return (y2 - y1 <= 120);
        }
        return false;
    }

    // 1. 垂直高度区域判定：必须位于顶部 22% 或底部 22% 敏感区内
    const float topRatio = static_cast<float>(y1) / static_cast<float>(pageH);
    const float bottomRatio = static_cast<float>(y2) / static_cast<float>(pageH);
    const bool inHeaderZone = (bottomRatio <= 0.22f || topRatio <= 0.20f);
    const bool inFooterZone = (topRatio >= 0.78f || bottomRatio >= 0.80f);

    if (!inHeaderZone && !inFooterZone) {
        return false;
    }

    if (labelName == "number" || labelName == "formula_number") {
        return true;
    }

    std::string trimmed = TrimString(text);
    if (trimmed.empty()) {
        return false;
    }

    // 保护章节大标题与目录内容绝不误判为页码
    if (trimmed.find("目录") != std::string::npos ||
        trimmed.find("Contents") != std::string::npos ||
        trimmed.find("contents") != std::string::npos ||
        trimmed.find("序") != std::string::npos ||
        trimmed.find("导言") != std::string::npos ||
        trimmed.find("前言") != std::string::npos) {
        return false;
    }

    if (IsFootnoteOrAnnotation(trimmed, labelName, y1, y2, pageH, false)) {
        return false;
    }

    // 2. 水平位置与几何尺寸判定
    const int boxW = (std::max)(1, x2 - x1);
    const int boxH = (std::max)(1, y2 - y1);
    const float leftRatio = static_cast<float>(x1) / static_cast<float>(pageW);
    const float rightRatio = static_cast<float>(x2) / static_cast<float>(pageW);
    const float widthRatio = static_cast<float>(boxW) / static_cast<float>(pageW);
    const float heightRatio = static_cast<float>(boxH) / static_cast<float>(pageH);

    const bool isCornerOrCenter = (rightRatio <= 0.35f || leftRatio >= 0.65f ||
                                  (leftRatio >= 0.30f && rightRatio <= 0.70f && widthRatio <= 0.18f));

    if (!isCornerOrCenter || heightRatio > 0.08f) {
        return false;
    }

    // 3. 极速纯数字前置匹配
    if (trimmed.size() <= 4) {
        bool allDigits = true;
        for (char ch : trimmed) {
            if (ch < '0' || ch > '9') { allDigits = false; break; }
        }
        if (allDigits) return true;
    }

    // 4. 正则模式匹配：调用单例模式
    try {
        if (std::regex_match(trimmed, GetPageNumPattern())) {
            return true;
        }
    } catch (...) {}

    return false;
}

bool DocumentPipeline::HasAnnotationMarkers(const std::string& text) {
    if (text.empty()) return false;

    // 1. 高速 UTF-8 字节扫描
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char b1 = static_cast<unsigned char>(text[i]);
        if (i + 1 < text.size()) {
            unsigned char b2 = static_cast<unsigned char>(text[i + 1]);
            if (b1 == 0xC2 && (b2 == 0xB9 || b2 == 0xB2 || b2 == 0xB3)) {
                return true; // ¹, ², ³
            }
        }
        if (i + 2 < text.size()) {
            unsigned char b2 = static_cast<unsigned char>(text[i + 1]);
            unsigned char b3 = static_cast<unsigned char>(text[i + 2]);
            if (b1 == 0xE2) {
                if (b2 == 0x91 && (b3 >= 0xA0 && b3 <= 0xB3)) return true; // ① - ⑳
                if (b2 == 0x92 && (b3 >= 0x84 && b3 <= 0x97)) return true; // ⑴ - ⒇
                if (b2 == 0x81 && (b3 >= 0xB0 && b3 <= 0xB9)) return true; // ⁰, ⁴ - ⁹
            }
        }
    }

    // Fast-path: 若文本中不包含候选触发符号，跳过全部正则
    if (text.find_first_of("\\^[<*") == std::string::npos && text.find("【") == std::string::npos) {
        return false;
    }

    try {
        static const std::regex kMarkerRegexes[] = {
            std::regex(R"(\\\(\^\{?[\*\d]+(?:\])?\}?\\\)|\\\(\^\[?\d+\]?\\\))"), // \(^{[1]}\), \(^1\)
            std::regex(R"(\^\{?\[?\d+\]?\})"),                                    // ^{[1]}, ^1
            std::regex(R"(\[\s*\d{1,3}\s*\])"),                                  // [1], [2]
            std::regex(R"(\[(?:注\s*\d*|\d+)\]|【(?:注\s*\d*|\d+)】)"),         // [注1], 【注】
            std::regex(R"(\[\^[a-zA-Z0-9_\-]+\])"),                              // [^1]
            std::regex(R"(<sup>.*?</sup>)", std::regex::icase)                    // <sup>...</sup>
        };

        for (const auto& re : kMarkerRegexes) {
            if (std::regex_search(text, re)) {
                return true;
            }
        }
    } catch (...) {}

    return false;
}

bool DocumentPipeline::IsFootnoteOrAnnotation(
    const std::string& text,
    const std::string& labelName,
    int y1, int y2,
    int pageH,
    bool bodyHasMarkers
) {
    if (labelName == "footnote" || labelName == "vision_footnote") {
        return true;
    }

    std::string trimmed = TrimString(text);
    if (trimmed.empty()) {
        return false;
    }

    if (trimmed.size() >= 3) {
        unsigned char b1 = static_cast<unsigned char>(trimmed[0]);
        unsigned char b2 = static_cast<unsigned char>(trimmed[1]);
        unsigned char b3 = static_cast<unsigned char>(trimmed[2]);
        if (b1 == 0xE2 && ((b2 == 0x91 && b3 >= 0xA0 && b3 <= 0xB3) ||
                           (b2 == 0x92 && b3 >= 0x84 && b3 <= 0x97))) {
            return true;
        }
    }

    try {
        static const std::regex kFootnotePrefixRegex(
            R"(^(?:\[\s*\d+\s*\]|\[\s*注\s*\d*\s*\]|【\s*注\s*\d*\s*】|\[\^[a-zA-Z0-9_\-]+\](?:\:)?|\\\(\^\{?\[?\d+\]?\}?\\\)|\(?\^\{?\[?\d+\]?\}?\)?|\*{1,3}|注[\s：:]|附注[\s：:]|note[\s：:]|footnote[\s：:]).*$)",
            std::regex::icase
        );
        if (std::regex_match(trimmed, kFootnotePrefixRegex)) {
            return true;
        }

        if (pageH > 0) {
            float topRatio = static_cast<float>(y1) / static_cast<float>(pageH);
            float bottomRatio = static_cast<float>(y2) / static_cast<float>(pageH);
            if (topRatio >= 0.72f || bottomRatio >= 0.78f) {
                static const std::regex kNumberedPrefix(R"(^(?:\d+[\.\、\)])\s*(\S))");
                std::smatch m;
                if (std::regex_search(trimmed, m, kNumberedPrefix)) {
                    unsigned char firstByte = static_cast<unsigned char>(m[1].str()[0]);
                    if (firstByte >= 0x80 || (firstByte >= 'A' && firstByte <= 'Z') || (firstByte >= 'a' && firstByte <= 'z')) {
                        return true;
                    }
                }

                if (bodyHasMarkers && trimmed.size() >= 4) {
                    if (!std::regex_match(trimmed, GetPageNumPattern())) {
                        return true;
                    }
                }
            }
        }
    } catch (...) {}

    return false;
}

DocumentPipeline::DocumentPipeline(std::shared_ptr<ModelManager> modelManager, std::shared_ptr<DocLayoutEngine> layoutEngine)
    : m_modelManager(std::move(modelManager))
    , m_layoutEngine(std::move(layoutEngine))
    , m_aliveToken(std::make_shared<std::atomic<bool>>(true)) {}

DocumentPipeline::~DocumentPipeline() {
    Cancel();
    if (m_aliveToken) {
        m_aliveToken->store(false);
    }
}

void DocumentPipeline::Cancel() {
    m_shouldStop.store(true);
    if (m_modelManager) {
        m_modelManager->CancelInference(TargetModelType::Ocr);
        m_modelManager->CancelInference(TargetModelType::Translation);
    }
}

bool DocumentPipeline::SaveToMarkdown(const std::string& saveDir, const std::string& baseName, const std::string& markdownContent) {
    try {
        wxString dir = wxString::FromUTF8(saveDir);
        if (!wxDirExists(dir)) {
            wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        }
        wxString filePath = dir + "/" + wxString::FromUTF8(baseName) + ".md";
        std::ofstream out(ToFsPath(filePath), std::ios::binary);
        if (!out)
            return false;
        out.write(markdownContent.data(), markdownContent.size());
        out.close();
        return true;
    } catch (...) {
        return false;
    }
}

bool DocumentPipeline::SaveToJson(const std::string& saveDir, const std::string& baseName, const std::string& jsonContent) {
    try {
        wxString dir = wxString::FromUTF8(saveDir);
        if (!wxDirExists(dir)) {
            wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        }
        wxString filePath = dir + "/" + wxString::FromUTF8(baseName) + ".json";
        std::ofstream out(ToFsPath(filePath), std::ios::binary);
        if (!out)
            return false;
        out.write(jsonContent.data(), jsonContent.size());
        out.close();
        return true;
    } catch (...) {
        return false;
    }
}

DocumentPipeline::ResumeInfo DocumentPipeline::CheckResumeInfo(const std::string& inputFilePath, const std::string& outputDir) {
    ResumeInfo info;
    if (inputFilePath.empty())
        return info;

    wxFileName inFn(wxString::FromUTF8(inputFilePath));
    std::string baseDocName = inFn.GetName().ToUTF8().data();
    std::string actualOutputDir = outputDir;
    if (actualOutputDir.empty()) {
        actualOutputDir = std::string(inFn.GetPath().ToUTF8().data()) + "/" + baseDocName;
    }

    wxString mdPath = wxString::FromUTF8(actualOutputDir) + "/" + wxString::FromUTF8(baseDocName) + ".md";
    wxString jsonPath = wxString::FromUTF8(actualOutputDir) + "/" + wxString::FromUTF8(baseDocName) + ".json";

    if (!wxFileExists(mdPath) || !wxFileExists(jsonPath)) {
        return info;
    }

    std::ifstream jsonFile(ToFsPath(jsonPath), std::ios::binary);
    if (!jsonFile)
        return info;
    std::string jsonStr((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());
    jsonFile.close();

    std::ifstream mdFile(ToFsPath(mdPath), std::ios::binary);
    if (!mdFile)
        return info;
    std::string mdStr((std::istreambuf_iterator<char>(mdFile)), std::istreambuf_iterator<char>());
    mdFile.close();

    try {
        json j = json::parse(jsonStr);
        info.totalPages = j.value("total_pages", 0);
        int completedInJson = j.value("completed_pages", 0);

        int pagesCount = 0;
        int lastPageIndex = 0;
        bool lastPageCompleted = true;
        if (j.contains("pages") && j["pages"].is_array()) {
            pagesCount = static_cast<int>(j["pages"].size());
            if (!j["pages"].empty()) {
                const auto& lastP = j["pages"].back();
                lastPageIndex = lastP.value("page_index", pagesCount);
                if (lastP.contains("is_page_completed")) {
                    lastPageCompleted = lastP.value("is_page_completed", true);
                }
            }
        }

        info.isCompleted = j.value("is_completed", false);
        if (!info.isCompleted && info.totalPages > 0 && completedInJson >= info.totalPages && lastPageCompleted) {
            info.isCompleted = true;
        }

        if (info.isCompleted) {
            info.lastProcessedPage = info.totalPages;
            info.completedPages = info.totalPages;
        } else {
            int oldPage = 0;
            if (j.contains("last_interrupted_page")) {
                oldPage = j.value("last_interrupted_page", 0);
            }
            if (oldPage <= 0) {
                oldPage = (std::max)(lastPageIndex, (std::max)(pagesCount, completedInJson));
            }
            info.lastProcessedPage = oldPage;
            info.completedPages = (std::max)(0, oldPage - 1);
        }

        info.markdown = mdStr;
        info.jsonStructured = jsonStr;
        if (info.lastProcessedPage > 0 || !info.markdown.empty()) {
            info.hasResumeData = true;
        }
    } catch (...) {
        return info;
    }

    return info;
}

std::string DocumentPipeline::ExtractMarkdownUpToPage(const std::string& fullMd, int pageCount) {
    if (pageCount <= 0 || fullMd.empty()) {
        return "";
    }

    std::string sep = "\n\n---\n\n";
    if (fullMd.find("\r\n\r\n---\r\n\r\n") != std::string::npos) {
        sep = "\r\n\r\n---\r\n\r\n";
    } else if (fullMd.find("\r\n---\r\n") != std::string::npos && fullMd.find("\n\n---\n\n") == std::string::npos) {
        sep = "\r\n---\r\n";
    } else if (fullMd.find("\n---\n") != std::string::npos && fullMd.find("\n\n---\n\n") == std::string::npos) {
        sep = "\n---\n";
    }

    size_t pos = 0;
    for (int i = 0; i < pageCount; ++i) {
        size_t nextPos = fullMd.find(sep, pos);
        if (nextPos == std::string::npos) {
            return fullMd;
        }
        if (i == pageCount - 1) {
            return fullMd.substr(0, nextPos);
        }
        pos = nextPos + sep.length();
    }
    return fullMd;
}

std::string DocumentPipeline::TranslateEnglishIfNeeded(const std::string& text, bool enableTranslation) {
    if (!enableTranslation || !m_modelManager || !IsPredominantlyEnglish(text)) {
        return text;
    }

    try {
        TranslationTask transTask(text, LanguageCode::English, LanguageCode::Chinese);
        auto transPromise = std::make_shared<std::promise<std::string>>();
        auto transSet = std::make_shared<std::atomic<bool>>(false);
        auto transFuture = transPromise->get_future();

        m_modelManager->ExecuteTranslationStream(
            transTask, nullptr,
            [transPromise, transSet](bool success, const std::string& fullText, const std::string&) {
                if (!transSet->exchange(true)) {
                    try {
                        transPromise->set_value(success ? fullText : "");
                    } catch (...) {}
                }
            });

        std::string transResult;
        try {
            transResult = transFuture.get();
        } catch (...) {}
        return transResult.empty() ? text : transResult;
    } catch (...) {
        return text;
    }
}

// ============================================================================
// 4. 重构核心：高内聚子阶段执行函数群 (SRP 单一职责分解)
// ============================================================================

bool DocumentPipeline::InitParseSession(
    const std::string& inputFilePath,
    const std::string& outputDir,
    bool translateEnglishToChinese,
    int startFromPage,
    const std::string& initialMarkdown,
    const std::string& initialJson,
    ParseSession& session,
    std::string& errorMsg
) {
    if (!wxFileExists(wxString::FromUTF8(inputFilePath))) {
        errorMsg = "输入文件不存在: " + inputFilePath;
        return false;
    }

    session.inputFilePath = inputFilePath;
    wxFileName inFn(wxString::FromUTF8(inputFilePath));
    session.baseDocName = inFn.GetName().ToUTF8().data();
    session.actualOutputDir = outputDir;
    if (session.actualOutputDir.empty()) {
        session.actualOutputDir = std::string(inFn.GetPath().ToUTF8().data()) + "/" + session.baseDocName;
    }

    session.figuresDir = wxString::FromUTF8(session.actualOutputDir + "/imgs");
    if (!wxDirExists(session.figuresDir)) {
        wxFileName::Mkdir(session.figuresDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    }

    session.totalPages = PdfHelper::GetPageCount(inputFilePath);
    if (session.totalPages <= 0) {
        errorMsg = "无法解析文档格式或 PDF 页数为空";
        return false;
    }

    LOG_INFO("DocumentPipeline", "Start parsing document: " + inputFilePath + " (Total pages: " +
             std::to_string(session.totalPages) + ", Start from page: " + std::to_string(startFromPage) + ")");

    if (m_modelManager) {
        m_modelManager->ResetCancelState(TargetModelType::Ocr);
        m_modelManager->ResetCancelState(TargetModelType::Translation);
    }

    session.tempCacheDir = wxStandardPaths::Get().GetTempDir() + "/LinguaAlpaca_doccache";
    if (!wxDirExists(session.tempCacheDir)) {
        wxFileName::Mkdir(session.tempCacheDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    }

    // 预先规范化临时缓存目录，后续删除仅需高效字符串前缀比对
    wxFileName tempDirFn(session.tempCacheDir);
    tempDirFn.Normalize(wxPATH_NORM_ALL);
    wxString normTemp = tempDirFn.GetFullPath();
    if (!normTemp.EndsWith("/") && !normTemp.EndsWith("\\")) {
        normTemp += wxFileName::GetPathSeparator();
    }
    session.normalizedTempCacheDir = normTemp.Lower().ToStdString();

    if (!initialJson.empty()) {
        try {
            session.fullDocJson = json::parse(initialJson);
        } catch (...) {
            session.fullDocJson = json::object();
        }
    }
    if (!session.fullDocJson.is_object()) {
        session.fullDocJson = json::object();
    }
    session.fullDocJson["file_name"] = session.baseDocName;
    session.fullDocJson["total_pages"] = session.totalPages;
    if (!session.fullDocJson.contains("pages") || !session.fullDocJson["pages"].is_array()) {
        session.fullDocJson["pages"] = json::array();
    }

    session.actualStartPage = (std::clamp)(startFromPage, 0, session.totalPages);
    if (session.actualStartPage >= session.totalPages) {
        session.actualStartPage = 0;
        session.fullDocJson["pages"] = json::array();
    }

    // 截断历史旧页残片
    if (session.fullDocJson.contains("pages") && session.fullDocJson["pages"].is_array()) {
        auto& pagesArr = session.fullDocJson["pages"];
        for (auto it = pagesArr.begin(); it != pagesArr.end();) {
            int pIdx = it->value("page_index", 0);
            if (pIdx >= session.actualStartPage + 1) {
                it = pagesArr.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::string cleanMd;
    bool allHaveMd = false;
    if (session.fullDocJson.contains("pages") && session.fullDocJson["pages"].is_array() && !session.fullDocJson["pages"].empty()) {
        allHaveMd = true;
        for (const auto& pObj : session.fullDocJson["pages"]) {
            if (!pObj.contains("markdown") || pObj["markdown"].get<std::string>().empty()) {
                allHaveMd = false;
                break;
            }
            if (!cleanMd.empty()) {
                cleanMd += "\n\n---\n\n";
            }
            cleanMd += pObj["markdown"].get<std::string>();
        }
    }
    if (allHaveMd && !cleanMd.empty()) {
        session.fullMarkdown = cleanMd;
    } else {
        session.fullMarkdown = ExtractMarkdownUpToPage(initialMarkdown, session.actualStartPage);
    }

    session.translateEnglish = translateEnglishToChinese;
    return true;
}

std::shared_ptr<DocumentPipeline::PreprocessedPage> DocumentPipeline::FetchOrWaitStageOne(
    int p,
    int totalPages,
    std::future<std::shared_ptr<PreprocessedPage>>& nextPageFuture,
    DocProgressCallback onProgress,
    const std::string& fullMarkdown
) {
    if (!nextPageFuture.valid()) {
        return nullptr;
    }

    if (nextPageFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        if (m_aliveToken->load() && onProgress) {
            std::string stageMsg = "第 " + std::to_string(p + 1) + " / " + std::to_string(totalPages) + " 页: 正在完成版面分析与切片...";
            onProgress(p, totalPages, stageMsg, fullMarkdown);
        }
    }

    try {
        return nextPageFuture.get();
    } catch (const std::exception& e) {
        LOG_ERROR("DocumentPipeline", std::string("nextPageFuture.get() exception: ") + e.what());
        return nullptr;
    } catch (...) {
        LOG_ERROR("DocumentPipeline", "nextPageFuture.get() unknown exception");
        return nullptr;
    }
}

DocumentPipeline::OcrDispatchResult DocumentPipeline::DispatchParallelOcrWorkers(
    int p,
    int totalPages,
    const std::shared_ptr<PreprocessedPage>& curPageData,
    DocProgressCallback onProgress,
    const std::string& fullMarkdown
) {
    OcrDispatchResult res;
    res.recognizedResults.resize(curPageData->elements.size());
    res.elementSuccess.resize(curPageData->elements.size(), false);

    std::vector<size_t> ocrIndices;
    for (size_t elIdx = 0; elIdx < curPageData->elements.size(); ++elIdx) {
        if (!curPageData->elements[elIdx].isImage) {
            ocrIndices.push_back(elIdx);
        }
    }

    int maxParallel = 2;
    if (m_modelManager && m_modelManager->GetConfigManager()) {
        auto appCfg = m_modelManager->GetConfigManager()->GetConfig();
        maxParallel = (std::clamp)(appCfg.ocrParallel, 1, 4);
    }

    std::atomic<bool> pageCancelled{false};
    auto pageCancelToken = std::make_shared<std::atomic<bool>>(false);
    std::atomic<size_t> nextOcrTaskIdx{0};
    std::atomic<int> completedOcrCount{0};
    std::atomic<int> httpSuccessCount{0};
    std::atomic<int> httpFailCount{0};
    std::string firstOcrError;
    std::mutex progressMutex;
    std::mutex errorMutex;
    auto lastProgressTime = std::chrono::steady_clock::now();

    int numWorkers = (std::min)(static_cast<int>(ocrIndices.size()), maxParallel);
    if (numWorkers > 0) {
        if (m_aliveToken->load() && onProgress) {
            std::string stageMsg = "第 " + std::to_string(p + 1) + " / " + std::to_string(totalPages) + " 页: 开始并发识别 (0/" +
                                   std::to_string(ocrIndices.size()) + ")...";
            onProgress(p, totalPages, stageMsg, fullMarkdown);
        }

        std::vector<std::thread> workers;
        workers.reserve(numWorkers);

        struct WorkersJoinGuard {
            std::vector<std::thread>& w;
            ~WorkersJoinGuard() {
                for (auto& t : w) {
                    if (t.joinable()) {
                        try { t.join(); } catch (...) {}
                    }
                }
            }
        } workersGuard{workers};

        for (int w = 0; w < numWorkers; ++w) {
            workers.emplace_back([&]() {
                try {
                    while (true) {
                        if (m_shouldStop.load() || !m_aliveToken->load() || pageCancelled.load() || pageCancelToken->load()) {
                            break;
                        }

                        size_t taskIdx = nextOcrTaskIdx.fetch_add(1);
                        if (taskIdx >= ocrIndices.size()) {
                            break;
                        }

                        size_t elIdx = ocrIndices[taskIdx];
                        const auto& elem = curPageData->elements[elIdx];

                        auto ocrPromise = std::make_shared<std::promise<std::pair<bool, std::string>>>();
                        auto promiseInvoked = std::make_shared<std::atomic<bool>>(false);
                        auto ocrFuture = ocrPromise->get_future();

                        if (m_modelManager) {
                            m_modelManager->ExecuteOcrStream(
                                elem.ocrInputSource, elem.taskType, nullptr,
                                [ocrPromise, promiseInvoked, &firstOcrError, &errorMutex, &httpSuccessCount, &httpFailCount, elIdx](const std::string& fullText, bool success, const std::string& err) {
                                    if (!success) {
                                        httpFailCount.fetch_add(1);
                                        if (!err.empty()) {
                                            std::lock_guard<std::mutex> lk(errorMutex);
                                            if (firstOcrError.empty()) {
                                                firstOcrError = err;
                                            }
                                            LOG_WARN("DocumentPipeline", "元素 " + std::to_string(elIdx) + " OCR 识别失败: " + err);
                                        }
                                    } else {
                                        httpSuccessCount.fetch_add(1);
                                    }
                                    if (!promiseInvoked->exchange(true)) {
                                        try {
                                            ocrPromise->set_value({success, fullText});
                                        } catch (...) {}
                                    }
                                },
                                pageCancelToken);
                        } else {
                            httpFailCount.fetch_add(1);
                            if (!promiseInvoked->exchange(true)) {
                                try {
                                    ocrPromise->set_value({false, ""});
                                } catch (...) {}
                            }
                        }

                        bool ocrSuccess = false;
                        std::string recognizedText;
                        try {
                            auto ocrRes = ocrFuture.get();
                            ocrSuccess = ocrRes.first;
                            recognizedText = ocrRes.second;
                        } catch (const std::exception& ex) {
                            LOG_ERROR("DocumentPipeline", std::string("ocrFuture.get() exception: ") + ex.what());
                            ocrSuccess = false;
                        } catch (...) {
                            LOG_ERROR("DocumentPipeline", "ocrFuture.get() unknown exception");
                            ocrSuccess = false;
                        }

                        if (m_shouldStop.load() || !m_aliveToken->load() || pageCancelToken->load()) {
                            pageCancelled.store(true);
                            break;
                        }

                        recognizedText = StripMarkdownFences(recognizedText);
                        res.recognizedResults[elIdx] = recognizedText;
                        res.elementSuccess[elIdx] = ocrSuccess;

                        int doneCount = ++completedOcrCount;
                        {
                            std::lock_guard<std::mutex> pLock(progressMutex);
                            if (m_aliveToken->load() && onProgress) {
                                std::string stageMsg = "第 " + std::to_string(p + 1) + " / " + std::to_string(totalPages) + " 页: 并发识别元素 (" +
                                                       std::to_string(doneCount) + "/" + std::to_string(ocrIndices.size()) + ")...";

                                auto now = std::chrono::steady_clock::now();
                                int64_t elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProgressTime).count();
                                bool isLastElement = (doneCount == static_cast<int>(ocrIndices.size()));

                                if (isLastElement || elapsedMs >= 150) {
                                    lastProgressTime = now;
                                    std::string currentTempPageMd;
                                    for (size_t idx = 0; idx < curPageData->elements.size(); ++idx) {
                                        if (res.elementSuccess[idx] && !res.recognizedResults[idx].empty()) {
                                            if (!currentTempPageMd.empty()) {
                                                currentTempPageMd += "\n\n";
                                            }
                                            const auto& elemRef = curPageData->elements[idx];
                                            if (elemRef.elem.type == LayoutElementType::Title) {
                                                currentTempPageMd += (elemRef.elem.labelName == "doc_title" ? "# " : "## ") + res.recognizedResults[idx];
                                            } else {
                                                currentTempPageMd += res.recognizedResults[idx];
                                            }
                                        }
                                    }

                                    std::string streamedMarkdown = fullMarkdown;
                                    if (!currentTempPageMd.empty()) {
                                        if (!streamedMarkdown.empty()) {
                                            streamedMarkdown += "\n\n---\n\n";
                                        }
                                        streamedMarkdown += currentTempPageMd;
                                    }

                                    try {
                                        onProgress(p, totalPages, stageMsg, streamedMarkdown);
                                    } catch (...) {}
                                } else {
                                    try {
                                        onProgress(p, totalPages, stageMsg, fullMarkdown);
                                    } catch (...) {}
                                }
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("DocumentPipeline", std::string("Worker thread exception: ") + e.what());
                } catch (...) {
                    LOG_ERROR("DocumentPipeline", "Worker thread unknown exception");
                }
            });
        }
    }

    if (m_shouldStop.load() || !m_aliveToken->load() || pageCancelled.load() || pageCancelToken->load()) {
        res.pageInterrupted = true;
    }

    int ocrValidTextCount = 0;
    for (size_t elIdx : ocrIndices) {
        if (!res.recognizedResults[elIdx].empty()) {
            ocrValidTextCount++;
        }
    }

    if (!ocrIndices.empty() && !res.pageInterrupted) {
        if (m_modelManager) {
            auto health = m_modelManager->GetHealthStatus(TargetModelType::Ocr);
            if (health.state != ServerHealthState::Ready && health.state != ServerHealthState::Loading) {
                res.isServerDown = true;
            }
        }

        if ((httpFailCount.load() > 0 && httpSuccessCount.load() == 0) || res.isServerDown) {
            std::string detailErr = firstOcrError.empty() ? (res.isServerDown ? "OCR 服务未处于就绪状态" : "嵌入推理服务异常或未响应") : firstOcrError;
            LOG_ERROR("DocumentPipeline", "第 " + std::to_string(p + 1) + " 页所有切片识别均因服务异常失败: " + detailErr);
            res.pageInterrupted = true;
            res.failureReason = "第 " + std::to_string(p + 1) + " 页 OCR 识别失败 (" + detailErr + ")";
        } else if (ocrValidTextCount == 0) {
            LOG_INFO("DocumentPipeline", "第 " + std::to_string(p + 1) + " 页所有切片推理成功完成，但未检测到文本内容 (空白页或非文字图形)，正常记录为空页");
        }
    }

    return res;
}

DocumentPipeline::AssembledPage DocumentPipeline::AssemblePageContent(
    const std::shared_ptr<PreprocessedPage>& curPageData,
    const OcrDispatchResult& ocrRes,
    bool translateEnglishToChinese
) {
    AssembledPage assembled;
    assembled.pageJson["page_index"] = curPageData->pageIndex + 1;
    assembled.pageJson["elements"] = json::array();

    std::string pageBodyText;
    if (curPageData->pageHeight > 0) {
        for (size_t elIdx = 0; elIdx < curPageData->elements.size(); ++elIdx) {
            const auto& elem = curPageData->elements[elIdx];
            if (!elem.isImage && elem.elem.type != LayoutElementType::Table) {
                float bottomRatio = static_cast<float>(elem.y2) / static_cast<float>(curPageData->pageHeight);
                if (bottomRatio < 0.80f) {
                    pageBodyText += " " + ocrRes.recognizedResults[elIdx];
                }
            }
        }
    }
    const bool pageHasAnnotationMarkers = HasAnnotationMarkers(pageBodyText);

    for (size_t elIdx = 0; elIdx < curPageData->elements.size(); ++elIdx) {
        const auto& elem = curPageData->elements[elIdx];

        if (elem.isImage) {
            AppendToMarkdown(assembled.pageMarkdown, elem.imgTag);

            json elJson = {{"id", elem.elem.id},
                           {"type", elem.elem.labelName},
                           {"reading_order", elem.elem.readingOrder},
                           {"box", {elem.x1, elem.y1, elem.x2, elem.y2}},
                           {"figure_path", elem.figRelPath}};
            assembled.pageJson["elements"].push_back(elJson);
            continue;
        }

        if (ocrRes.pageInterrupted && !ocrRes.elementSuccess[elIdx] && ocrRes.recognizedResults[elIdx].empty()) {
            continue;
        }

        std::string recognizedText = ocrRes.recognizedResults[elIdx];

        bool isFootnote = IsFootnoteOrAnnotation(
            recognizedText, elem.elem.labelName,
            elem.y1, elem.y2, curPageData->pageHeight,
            pageHasAnnotationMarkers
        );

        if (elem.elem.type != LayoutElementType::Table && elem.elem.type != LayoutElementType::Formula) {
            if (isFootnote) {
                LOG_DEBUG("DocumentPipeline", "Preserved footnote at box [" +
                          std::to_string(elem.x1) + ", " + std::to_string(elem.y1) + ", " +
                          std::to_string(elem.x2) + ", " + std::to_string(elem.y2) +
                          "]: " + recognizedText);
            } else if (IsHeaderOrFooterPageNumber(recognizedText, elem.elem.labelName, elem.x1, elem.y1, elem.x2, elem.y2, curPageData->pageWidth, curPageData->pageHeight)) {
                LOG_DEBUG("DocumentPipeline", "Secondary filter: skipped page number '" + recognizedText +
                          "' at box [" + std::to_string(elem.x1) + ", " + std::to_string(elem.y1) + ", " +
                          std::to_string(elem.x2) + ", " + std::to_string(elem.y2) + "]");
                continue;
            }
        }

        std::string formattedContent;

        if (elem.elem.type == LayoutElementType::Formula) {
            formattedContent = CleanAndFormatFormula(recognizedText);
            size_t startPos = formattedContent.find('\n');
            size_t endPos = formattedContent.rfind('\n');
            if (startPos != std::string::npos && endPos != std::string::npos && endPos > startPos) {
                recognizedText = formattedContent.substr(startPos + 1, endPos - startPos - 1);
            }
        } else if (elem.elem.type == LayoutElementType::Title ||
                   (elem.elem.type == LayoutElementType::Text && (recognizedText == "目录 Contents" ||
                                                                  recognizedText == "目录" ||
                                                                  recognizedText.rfind("目录 ", 0) == 0))) {
            if (elem.elem.labelName == "doc_title") {
                formattedContent = "# " + recognizedText;
            } else {
                formattedContent = "## " + recognizedText;
            }
        } else if (elem.elem.type == LayoutElementType::Table) {
            formattedContent = ConvertOtslToHtml(recognizedText);
        } else {
            formattedContent = TranslateEnglishIfNeeded(recognizedText, translateEnglishToChinese);
        }

        AppendToMarkdown(assembled.pageMarkdown, formattedContent);

        std::string finalType = isFootnote ? "footnote" : elem.elem.labelName;
        json elJson = {
            {"id", elem.elem.id}, {"type", finalType}, {"reading_order", elem.elem.readingOrder}, {"box", {elem.x1, elem.y1, elem.x2, elem.y2}}, {"content", recognizedText}};
        assembled.pageJson["elements"].push_back(elJson);
    }

    return assembled;
}

void DocumentPipeline::HandlePipelineInterruption(
    ParseSession& session,
    int p,
    const AssembledPage& assembled,
    const std::string& failureReason,
    std::future<std::shared_ptr<PreprocessedPage>>& nextPageFuture
) {
    json pageJsonCopy = assembled.pageJson;
    pageJsonCopy["markdown"] = assembled.pageMarkdown;
    pageJsonCopy["is_page_completed"] = false;

    if (!assembled.pageMarkdown.empty()) {
        AppendToMarkdown(session.fullMarkdown, assembled.pageMarkdown);
        session.fullDocJson["pages"].push_back(pageJsonCopy);
    }

    session.fullDocJson["completed_pages"] = p;
    session.fullDocJson["last_interrupted_page"] = p + 1;
    session.fullDocJson["is_completed"] = false;

    std::string interruptedJsonStr = SafeDumpJson(session.fullDocJson);
    SaveToMarkdown(session.actualOutputDir, session.baseDocName, session.fullMarkdown);
    SaveToJson(session.actualOutputDir, session.baseDocName, interruptedJsonStr);

    DrainFutureAndClean(nextPageFuture, session.inputFilePath, session.tempCacheDir);
}

void DocumentPipeline::PersistPageSnapshot(
    ParseSession& session,
    int p,
    const AssembledPage& assembled,
    bool isCompleted
) {
    json pageJsonCopy = assembled.pageJson;
    pageJsonCopy["markdown"] = assembled.pageMarkdown;
    pageJsonCopy["is_page_completed"] = true;
    session.fullDocJson["pages"].push_back(pageJsonCopy);

    AppendToMarkdown(session.fullMarkdown, assembled.pageMarkdown);

    session.fullDocJson["completed_pages"] = p + 1;
    session.fullDocJson["is_completed"] = isCompleted;

    std::string currentJsonDump = SafeDumpJson(session.fullDocJson, -1);
    SaveToMarkdown(session.actualOutputDir, session.baseDocName, session.fullMarkdown);
    SaveToJson(session.actualOutputDir, session.baseDocName, currentJsonDump);
}

// ============================================================================
// 5. 顶层流水线编排入口 (极简线性控制流，彻底摆脱 God Function)
// ============================================================================

void DocumentPipeline::StartParseAsync(
    const std::string& inputFilePath,
    const std::string& outputDir,
    bool translateEnglishToChinese,
    DocProgressCallback onProgress,
    DocCompleteCallback onComplete,
    int startFromPage,
    const std::string& initialMarkdown,
    const std::string& initialJson
) {
    Cancel();
    m_shouldStop.store(false);
    m_isRunning.store(true);

    auto aliveToken = m_aliveToken;

    std::thread([this, aliveToken, inputFilePath, outputDir, translateEnglishToChinese, onProgress, onComplete, startFromPage, initialMarkdown, initialJson]() {
        ExecutePipeline(inputFilePath, outputDir, translateEnglishToChinese, onProgress, onComplete, startFromPage, initialMarkdown, initialJson);
    }).detach();
}

void DocumentPipeline::ExecutePipeline(
    const std::string& inputFilePath,
    const std::string& outputDir,
    bool translateEnglishToChinese,
    DocProgressCallback onProgress,
    DocCompleteCallback onComplete,
    int startFromPage,
    const std::string& initialMarkdown,
    const std::string& initialJson
) {
    auto aliveToken = m_aliveToken;
    if (!aliveToken || !aliveToken->load())
        return;

    ParseSession session;
    std::string initError;
    if (!InitParseSession(inputFilePath, outputDir, translateEnglishToChinese, startFromPage, initialMarkdown, initialJson, session, initError)) {
        m_isRunning.store(false);
        if (aliveToken->load() && onComplete) {
            onComplete(false, "", "", initError);
        }
        return;
    }

    bool hasError = false;
    std::string errorMessage;

    // ★★★ 核心阶段流水线：跨页预取异步 Future
    std::future<std::shared_ptr<PreprocessedPage>> nextPageFuture;
    nextPageFuture = LaunchPreprocessStageOneAsync(
        session.actualStartPage, session.totalPages,
        session.inputFilePath, session.tempCacheDir, session.figuresDir,
        m_layoutEngine, aliveToken, m_shouldStop, onProgress, session.fullMarkdown
    );

    for (int p = session.actualStartPage; p < session.totalPages; ++p) {
        if (m_shouldStop.load() || !aliveToken->load()) {
            hasError = true;
            errorMessage = "用户已取消解析";
            break;
        }

        auto curPageData = FetchOrWaitStageOne(p, session.totalPages, nextPageFuture, onProgress, session.fullMarkdown);
        if (!curPageData || !curPageData->success) {
            if (m_shouldStop.load() || !aliveToken->load()) {
                hasError = true;
                errorMessage = "用户已取消解析";
                break;
            }
            LOG_ERROR("DocumentPipeline", "Failed to preprocess stage 1 for page: " + std::to_string(p + 1));
            if (p + 1 < session.totalPages && !m_shouldStop.load() && aliveToken->load()) {
                nextPageFuture = LaunchPreprocessStageOneAsync(p + 1, session.totalPages, session.inputFilePath, session.tempCacheDir, session.figuresDir, m_layoutEngine, aliveToken, m_shouldStop);
            }
            continue;
        }

        // 核心提速重叠：在当前页进入耗时的阶段 2 OCR 之前，立刻在后台启动下一页阶段 1 预处理
        if (p + 1 < session.totalPages && !m_shouldStop.load() && aliveToken->load()) {
            nextPageFuture = LaunchPreprocessStageOneAsync(p + 1, session.totalPages, session.inputFilePath, session.tempCacheDir, session.figuresDir, m_layoutEngine, aliveToken, m_shouldStop);
        }

        // 阶段 2：多 Worker 并发切片推理
        auto ocrRes = DispatchParallelOcrWorkers(p, session.totalPages, curPageData, onProgress, session.fullMarkdown);

        // 阶段 3：按拓扑阅读顺序组装 Markdown 与 JSON
        auto assembled = AssemblePageContent(curPageData, ocrRes, session.translateEnglish);

        curPageData->CleanRemainingTempFiles(session.inputFilePath, session.tempCacheDir, false);

        if (ocrRes.pageInterrupted) {
            HandlePipelineInterruption(session, p, assembled, ocrRes.failureReason, nextPageFuture);
            hasError = true;
            errorMessage = ocrRes.failureReason.empty() ? "用户已取消解析" : ocrRes.failureReason;
            break;
        }

        PersistPageSnapshot(session, p, assembled, p + 1 == session.totalPages);

        if (aliveToken->load() && onProgress) {
            onProgress(p + 1, session.totalPages, "第 " + std::to_string(p + 1) + " 页解析完成", session.fullMarkdown);
        }
    }

    DrainFutureAndClean(nextPageFuture, session.inputFilePath, session.tempCacheDir);

    std::string jsonDumpStr = SafeDumpJson(session.fullDocJson);
    SaveToMarkdown(session.actualOutputDir, session.baseDocName, session.fullMarkdown);
    SaveToJson(session.actualOutputDir, session.baseDocName, jsonDumpStr);

    if (!hasError && !m_shouldStop.load()) {
        LOG_INFO("DocumentPipeline", "Document parse completed, results saved to: " + session.actualOutputDir);
    }

    m_isRunning.store(false);

    if (!aliveToken->load())
        return;

    if (onComplete) {
        if (hasError) {
            onComplete(false, session.fullMarkdown, jsonDumpStr, errorMessage);
        } else {
            onComplete(true, session.fullMarkdown, jsonDumpStr, "");
        }
    }
}

} // namespace LinguaAlpaca
