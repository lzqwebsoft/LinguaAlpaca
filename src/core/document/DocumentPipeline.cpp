#pragma execution_character_set("utf-8")
#include "DocumentPipeline.hpp"
#include "core/pdf/PdfHelper.hpp"
#include "core/Logger.hpp"

#include <nlohmann/json.hpp>
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/stdpaths.h>
#include <wx/image.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <algorithm>
#include <regex>

using json = nlohmann::json;

namespace LinguaAlpaca {

namespace {

std::string TrimString(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
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
    } catch (...) {
    }
    wxFileName fn1(wxString::FromUTF8(path1));
    wxFileName fn2(wxString::FromUTF8(path2));
    fn1.Normalize();
    fn2.Normalize();
    return fn1.GetFullPath().CmpNoCase(fn2.GetFullPath()) == 0;
}

// 安全回收临时文件：绝对不删除用户原始输入文件，且仅允许清理临时缓存目录内的文件
void SafeRemoveTempFile(const std::string& pathToRemove, const std::string& originalInputFile, const wxString& tempDir) {
    if (pathToRemove.empty())
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
    wxFileName tempDirFn(tempDir);
    targetFn.Normalize();
    tempDirFn.Normalize();
    wxString targetNorm = targetFn.GetFullPath();
    wxString tempNorm = tempDirFn.GetFullPath();
    if (!tempNorm.EndsWith("/") && !tempNorm.EndsWith("\\")) {
        tempNorm += wxFileName::GetPathSeparator();
    }

    if (targetNorm.Lower().StartsWith(tempNorm.Lower())) {
        wxRemoveFile(wxPath);
    } else {
        LOG_WARN("DocumentPipeline", "Prevented deletion of file outside temp cache directory: " + pathToRemove);
    }
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
    std::string tempCropFile;
    std::string figRelPath;
    std::string imgTag;
    std::string taskType{"ocr"};
};

// 单页预处理（阶段 1：光栅化 + PP-DocLayout 版面分析 + 元素裁剪与大图内存回收）数据包
struct PreprocessedPage {
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

    // 清理该预处理页尚未被消费的临时文件 (避免取消或异常时残留孤儿文件)
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

// 裁剪并持久化单个版面元素切片（单元素原子事务处理：统一异常守护与明确状态码返回）
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
    PreprocessedPage& pageData
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
            wxString tempCropFile = tempCacheDir + wxString::Format("/crop_p%d_e%d.png", p + 1, elem.id);
            if (!crop.SaveFile(tempCropFile, wxBITMAP_TYPE_PNG)) {
                LOG_WARN("DocumentPipeline", "Failed to save temp crop file: " + tempCropFile.ToStdString());
                return false;
            }
            pElem.tempCropFile = tempCropFile.ToUTF8().data();
            pageData.tempCropFiles.push_back(pElem.tempCropFile);

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

// 安全回收预取任务中的临时资源
void DrainFutureAndClean(std::future<std::shared_ptr<PreprocessedPage>>& fut, const std::string& inputFilePath, const wxString& tempCacheDir) {
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

// 阶段 1：页面预处理 (支持后台异步线程并发执行，零等待衔接)
std::shared_ptr<PreprocessedPage> PreprocessStageOne(int p, int totalPages, const std::string& inputFilePath, const wxString& tempCacheDir, const wxString& figuresDir,
                                                     const std::shared_ptr<DocLayoutEngine>& layoutEngine, const std::shared_ptr<std::atomic<bool>>& aliveToken, const std::atomic<bool>& shouldStop,
                                                     DocProgressCallback onProgress = nullptr, const std::string& currentFullMarkdown = "") {
    if (shouldStop.load() || !aliveToken->load()) {
        return nullptr;
    }

    auto pageData = std::make_shared<PreprocessedPage>();
    pageData->pageIndex = p;

    // 1. 光栅化当前单页
    if (onProgress && aliveToken->load() && !shouldStop.load()) {
        std::string status = "第 " + std::to_string(p + 1) + " / " + std::to_string(totalPages) + " 页: 正在光栅化...";
        onProgress(p, totalPages, status, currentFullMarkdown);
    }

    std::string pageImgPath = PdfHelper::RenderPageToTempFile(inputFilePath, p, tempCacheDir.ToUTF8().data(), 1600);
    if (pageImgPath.empty() || !wxFileExists(wxString::FromUTF8(pageImgPath))) {
        LOG_ERROR("DocumentPipeline", "Failed to rasterize page: " + std::to_string(p + 1));
        pageData->success = false;
        pageData->error = "光栅化失败";
        return pageData;
    }
    pageData->pageImgPath = pageImgPath;

    if (shouldStop.load() || !aliveToken->load()) {
        pageData->CleanRemainingTempFiles(inputFilePath, tempCacheDir, true);
        return nullptr;
    }

    // 2. 载入当前单页位图用于空白页快速判定与子图裁剪
    wxImage pageImg;
    if (!pageImg.LoadFile(wxString::FromUTF8(pageImgPath))) {
        pageData->CleanRemainingTempFiles(inputFilePath, tempCacheDir, true);
        pageData->success = false;
        pageData->error = "加载光栅化图像失败";
        return pageData;
    }

    int pImgW = pageImg.GetWidth();
    int pImgH = pageImg.GetHeight();
    pageData->pageWidth = pImgW;
    pageData->pageHeight = pImgH;

    // 空页快速判定：若整页位图为纯空白（或仅有微弱噪点无有效笔墨内容），直接标记为空页完成预处理，0ms 跳过版面检测与 OCR
    if (DocLayoutEngine::IsImageContentEmpty(pageImg)) {
        LOG_INFO("DocumentPipeline", "第 " + std::to_string(p + 1) + " 页判定为空白页，跳过版面分析与 OCR 视觉推理");
        pageData->elements.clear();
        pageData->success = true;
        pageImg.Destroy();
        SafeRemoveTempFile(pageImgPath, inputFilePath, tempCacheDir);
        pageData->pageImgPath.clear();
        return pageData;
    }

    // 3. DocLayoutEngine 执行版面目标检测
    if (onProgress && aliveToken->load() && !shouldStop.load()) {
        std::string status = "第 " + std::to_string(p + 1) + " / " + std::to_string(totalPages) + " 页: 版面分析中 (检测标题/表格/公式/分栏)...";
        onProgress(p, totalPages, status, currentFullMarkdown);
    }

    pageData->layoutResult.pageIndex = p + 1;
    if (layoutEngine) {
        layoutEngine->AnalyzeLayout(pageImgPath, pageData->layoutResult);
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

        // 阶段 1 前置过滤保护：仅当元素被模型明确标记为独立页码 (number / formula_number) 时才在 OCR 前跳过
        // 普通文本类元素 (Text / content / aside_text) 必须送交 OCR 视觉推理，以真实文本语义精准判别
        if (elem.labelName == "number" || elem.labelName == "formula_number") {
            if (DocumentPipeline::IsHeaderOrFooterPageNumber("", elem.labelName, x1, y1, x2, y2, pImgW, pImgH)) {
                LOG_DEBUG("DocumentPipeline", "Preprocess: skipped page number element '" + elem.labelName +
                          "' at box [" + std::to_string(x1) + ", " + std::to_string(y1) + ", " +
                          std::to_string(x2) + ", " + std::to_string(y2) + "]");
                continue;
            }
        }

        // 委托原子事务函数完成切片提取与持久化，保持高内聚与线性控制流
        CropAndSaveElement(pageImg, elem, elIdx, p, x1, y1, x2, y2, cropW, cropH, pImgW, figuresDir, tempCacheDir, *pageData);
    }

    // 内存立即释放：整页高分辨率位图与临时光栅化文件在此处即刻回收 (O(1) 恒定内存)
    pageImg.Destroy();
    SafeRemoveTempFile(pageImgPath, inputFilePath, tempCacheDir);
    pageData->pageImgPath.clear();

    if (shouldStop.load() || !aliveToken->load()) {
        pageData->CleanRemainingTempFiles(inputFilePath, tempCacheDir, true);
        return nullptr;
    }

    pageData->success = true;
    return pageData;
}

// 异步执行单页预处理任务（跨页流水线预取，单一统一异常网守护）
std::future<std::shared_ptr<PreprocessedPage>> LaunchPreprocessStageOneAsync(
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
            return std::shared_ptr<PreprocessedPage>(nullptr);
        } catch (...) {
            LOG_ERROR("DocumentPipeline", "PreprocessStageOne unknown exception");
            return std::shared_ptr<PreprocessedPage>(nullptr);
        }
    });
}

} // namespace

bool DocumentPipeline::IsImageContentEmpty(const wxImage& img) {
    return DocLayoutEngine::IsImageContentEmpty(img);
}

// 将 PaddleOCR OTSL 结构化表格表示 (<fcel>, <lcel>, <ucel>, <nl>) 转换为标准 HTML 表格
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
                size_t first = cellText.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) {
                    cellText = "";
                } else {
                    size_t last = cellText.find_last_not_of(" \t\r\n");
                    cellText = cellText.substr(first, last - first + 1);
                }
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

    auto escapeHtml = [](const std::string& str) -> std::string {
        std::string res;
        for (char ch : str) {
            if (ch == '&')
                res += "&amp;";
            else if (ch == '<')
                res += "&lt;";
            else if (ch == '>')
                res += "&gt;";
            else if (ch == '"')
                res += "&quot;";
            else if (ch == '\'')
                res += "&#039;";
            else
                res += ch;
        }
        return res;
    };

    std::string theadHtml;
    std::string tbodyHtml;

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
                rowHtml += "    <" + tag + attrs + ">" + escapeHtml(cell.text) + "</" + tag + ">\n";
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
    if (!theadHtml.empty()) {
        tableHtml += "<thead>\n" + theadHtml + "</thead>\n";
    }
    if (!tbodyHtml.empty()) {
        tableHtml += "<tbody>\n" + tbodyHtml + "</tbody>\n";
    }
    tableHtml += "</table>";

    return tableHtml;
}

// 判定是否属于页眉/页脚区域的独立页码 (用于 Markdown 与 JSON 组装时的二次过滤防护)
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

    // 2. 若模型原生显式标签即为 "number" 或 "formula_number"，且位于页眉页脚区，直接判定为页码
    if (labelName == "number" || labelName == "formula_number") {
        return true;
    }

    std::string trimmed = TrimString(text);
    if (trimmed.empty()) {
        return false; // 未执行 OCR 或纯空切片，若非明确 number 标签，绝不能盲目判定为页码
    }

    // 保护章节大标题与目录内容 (如 "目录", "Contents", "目录 Contents", "总序", "前言") 绝不误判为页码
    if (trimmed.find("目录") != std::string::npos ||
        trimmed.find("Contents") != std::string::npos ||
        trimmed.find("contents") != std::string::npos ||
        trimmed.find("序") != std::string::npos ||
        trimmed.find("导言") != std::string::npos ||
        trimmed.find("前言") != std::string::npos) {
        return false;
    }

    // 若当前文本本身满足注解/脚注特征，绝不误判为页码
    if (IsFootnoteOrAnnotation(trimmed, labelName, y1, y2, pageH, false)) {
        return false;
    }

    // 3. 水平位置与几何尺寸判定：
    // - 左侧页码 (x2 <= 35%W)
    // - 右侧页码 (x1 >= 65%W)
    // - 居中狭窄单行页码 (x1 >= 30%W 且 x2 <= 70%W 且单框宽度 <= 18%W)
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

    // 4. 文本模式匹配：纯数字、罗马数字、带装饰符或页码词缀的短文本 (例如: "2", "5", "- 2 -", "· 5 ·", "第 2 页", "Page 5", "IV")
    try {
        static const std::regex kPageNumPattern(
            R"(^[\s\-\·\~\—\#\[\(第·\.]*(?:page|p\.|no\.)?[\s·\.]*(\d+|[IVXLCDMivxlcdm]+)[\s\-\·\~\—\#\]\)页\.]*(?:page|p\.)?[\s\.]*$)",
            std::regex::icase
        );
        if (std::regex_match(trimmed, kPageNumPattern)) {
            return true;
        }
    } catch (...) {
        // 正则解析异常防御兜底
    }

    return false;
}

// 判定文本中是否存在注解数字标记 (如 LaTeX \(^{[1]}\), [1], ①, 脚注星号等)
bool DocumentPipeline::HasAnnotationMarkers(const std::string& text) {
    if (text.empty()) return false;

    // 1. 高速 UTF-8 字节扫描：带圈数字 (①-⑳, ⑴-⒇) 与 Unicode 上标字符 (¹²³⁴-⁹)
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

    // 2. 正则匹配：LaTeX 上标 \(^{[1]}\), [1], [注1], 【注1】, [^1], <sup>
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
    } catch (...) {
        // 正则匹配异常防护
    }

    return false;
}

// 判定候选元素是否为底部注解/脚注 (结合模型标签、正文注解数字标记及文本形态综合研判)
bool DocumentPipeline::IsFootnoteOrAnnotation(
    const std::string& text,
    const std::string& labelName,
    int y1, int y2,
    int pageH,
    bool bodyHasMarkers
) {
    // 1. 模型原生显式语义标签判定
    if (labelName == "footnote" || labelName == "vision_footnote") {
        return true;
    }

    std::string trimmed = TrimString(text);
    if (trimmed.empty()) {
        return false;
    }

    // 2. 检查文本开头是否为典型注解/脚注前缀标识
    // (1) 带圈数字开头：①、②...
    if (trimmed.size() >= 3) {
        unsigned char b1 = static_cast<unsigned char>(trimmed[0]);
        unsigned char b2 = static_cast<unsigned char>(trimmed[1]);
        unsigned char b3 = static_cast<unsigned char>(trimmed[2]);
        if (b1 == 0xE2 && ((b2 == 0x91 && b3 >= 0xA0 && b3 <= 0xB3) ||
                           (b2 == 0x92 && b3 >= 0x84 && b3 <= 0x97))) {
            return true;
        }
    }

    // (2) 常见注解前缀匹配与底部区域结构研判（统一单层异常防护）
    try {
        static const std::regex kFootnotePrefixRegex(
            R"(^(?:\[\s*\d+\s*\]|\[\s*注\s*\d*\s*\]|【\s*注\s*\d*\s*】|\[\^[a-zA-Z0-9_\-]+\](?:\:)?|\\\(\^\{?\[?\d+\]?\}?\\\)|\(?\^\{?\[?\d+\]?\}?\)?|\*{1,3}|注[\s：:]|附注[\s：:]|note[\s：:]|footnote[\s：:]).*$)",
            std::regex::icase
        );
        if (std::regex_match(trimmed, kFootnotePrefixRegex)) {
            return true;
        }

        // (3) 位于页面底部敏感区 (topRatio >= 0.72f 或 bottomRatio >= 0.78f) 时的结构判定
        if (pageH > 0) {
            float topRatio = static_cast<float>(y1) / static_cast<float>(pageH);
            float bottomRatio = static_cast<float>(y2) / static_cast<float>(pageH);
            if (topRatio >= 0.72f || bottomRatio >= 0.78f) {
                // 安全匹配编号开头注释 (如 "1. xxxxx"、"1、xxxxx"、"1) xxxxx")
                static const std::regex kNumberedPrefix(R"(^(?:\d+[\.\、\)])\s*(\S))");
                std::smatch m;
                if (std::regex_search(trimmed, m, kNumberedPrefix)) {
                    unsigned char firstByte = static_cast<unsigned char>(m[1].str()[0]);
                    // 满足非纯数字（多字节 UTF-8 中文字符或英文字母）
                    if (firstByte >= 0x80 || (firstByte >= 'A' && firstByte <= 'Z') || (firstByte >= 'a' && firstByte <= 'z')) {
                        return true;
                    }
                }

                // (4) 若当前页正文中已检测出注解标记 (bodyHasMarkers == true)，
                // 且底部文本具有一定长度 (非纯页码)，则判定为该标记对应的底部注解
                if (bodyHasMarkers && trimmed.size() >= 4) {
                    static const std::regex kPageNumPattern(
                        R"(^[\s\-\·\~\—\#\[\(第·\.]*(?:page|p\.|no\.)?[\s·\.]*(\d+|[IVXLCDMivxlcdm]+)[\s\-\·\~\—\#\]\)页\.]*(?:page|p\.)?[\s\.]*$)",
                        std::regex::icase
                    );
                    if (!std::regex_match(trimmed, kPageNumPattern)) {
                        return true;
                    }
                }
            }
        }
    } catch (...) {
        // 正则防护
    }

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
            // 未完成时：以记录中最后一页作为旧一页 (从该页开始重新完整解析，防止上次中断导致旧一页未完全解析而丢失数据)
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

    // 智能识别分隔符模式 (\r\n\r\n---\r\n\r\n 或 \n\n---\n\n)
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

void DocumentPipeline::StartParseAsync(const std::string& inputFilePath, const std::string& outputDir, bool translateEnglishToChinese, DocProgressCallback onProgress, DocCompleteCallback onComplete,
                                       int startFromPage, const std::string& initialMarkdown, const std::string& initialJson) {

    Cancel();
    m_shouldStop.store(false);
    m_isRunning.store(true);

    auto aliveToken = m_aliveToken;

    std::thread([this, aliveToken, inputFilePath, outputDir, translateEnglishToChinese, onProgress, onComplete, startFromPage, initialMarkdown, initialJson]() {
        if (!aliveToken->load())
            return;

        std::string fullMarkdown;
        try {
            if (!wxFileExists(wxString::FromUTF8(inputFilePath))) {
                m_isRunning.store(false);
                if (aliveToken->load() && onComplete) {
                    onComplete(false, "", "", "输入文件不存在: " + inputFilePath);
                }
                return;
            }

        // 确定输出目录 (默认在当前目录或文档目录下建立专属文件夹)
        wxFileName inFn(wxString::FromUTF8(inputFilePath));
        std::string baseDocName = inFn.GetName().ToUTF8().data();
        std::string actualOutputDir = outputDir;
        if (actualOutputDir.empty()) {
            actualOutputDir = std::string(inFn.GetPath().ToUTF8().data()) + "/" + baseDocName;
        }

        // 创建主输出目录及 imgs 子目录 (PaddleOCR 标准图像输出目录)
        wxString figuresDir = wxString::FromUTF8(actualOutputDir + "/imgs");
        if (!wxDirExists(figuresDir)) {
            wxFileName::Mkdir(figuresDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        }

        // 获取文档总页数 (PDF 分页检测，普通图片则为 1 页)
        int totalPages = PdfHelper::GetPageCount(inputFilePath);
        if (totalPages <= 0) {
            m_isRunning.store(false);
            if (aliveToken->load() && onComplete) {
                onComplete(false, "", "", "无法解析文档格式或 PDF 页数为空");
            }
            return;
        }

        LOG_INFO("DocumentPipeline", "Start parsing document: " + inputFilePath + " (Total pages: " + std::to_string(totalPages) + ", Start from page: " + std::to_string(startFromPage) + ")");

        if (m_modelManager) {
            m_modelManager->ResetCancelState(TargetModelType::Ocr);
            m_modelManager->ResetCancelState(TargetModelType::Translation);
        }

        // 建立临时工作缓存目录
        wxString tempCacheDir = wxStandardPaths::Get().GetTempDir() + "/LinguaAlpaca_doccache";
        if (!wxDirExists(tempCacheDir)) {
            wxFileName::Mkdir(tempCacheDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        }

        json fullDocJson;
        if (!initialJson.empty()) {
            try {
                fullDocJson = json::parse(initialJson);
            } catch (...) {
                fullDocJson = json::object();
            }
        }
        if (!fullDocJson.is_object()) {
            fullDocJson = json::object();
        }
        fullDocJson["file_name"] = baseDocName;
        fullDocJson["total_pages"] = totalPages;
        if (!fullDocJson.contains("pages") || !fullDocJson["pages"].is_array()) {
            fullDocJson["pages"] = json::array();
        }

        int actualStartPage = (std::clamp)(startFromPage, 0, totalPages);
        if (actualStartPage >= totalPages) {
            actualStartPage = 0;
            fullDocJson["pages"] = json::array();
        }

        // 核心要求：从旧一页 (actualStartPage) 重新开始解析，必须彻底清理旧一页的残留数据！
        // 1. JSON：仅保留旧一页之前的完整页面 (page_index < actualStartPage + 1)
        if (fullDocJson.contains("pages") && fullDocJson["pages"].is_array()) {
            auto& pagesArr = fullDocJson["pages"];
            for (auto it = pagesArr.begin(); it != pagesArr.end();) {
                int pIdx = it->value("page_index", 0);
                if (pIdx >= actualStartPage + 1) {
                    it = pagesArr.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // 2. Markdown：截取旧一页之前的确认内容，丢弃旧一页未完成的残片，实现安全合并
        std::string cleanMd;
        bool allHaveMd = false;
        if (fullDocJson.contains("pages") && fullDocJson["pages"].is_array() && !fullDocJson["pages"].empty()) {
            allHaveMd = true;
            for (const auto& pObj : fullDocJson["pages"]) {
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
            fullMarkdown = cleanMd;
        } else {
            fullMarkdown = ExtractMarkdownUpToPage(initialMarkdown, actualStartPage);
        }

        bool hasError = false;
        std::string errorMessage;

        // ★★★ 核心阶段流水线 (Staged Pipeline Overlapping)：
        // 跨页预取异步 Future：当当前页 p 处于阶段 2 (耗时 VLM 识别) 时，后台异步线程并发执行下一页 p+1 的阶段 1 (光栅化+版面分析+切片)
        std::future<std::shared_ptr<PreprocessedPage>> nextPageFuture;

        // 为首个待解析页启动阶段 1 预处理 (显式值捕获，杜绝并发引用竞争)
        nextPageFuture = LaunchPreprocessStageOneAsync(actualStartPage, totalPages, inputFilePath, tempCacheDir, figuresDir, m_layoutEngine, aliveToken, m_shouldStop, onProgress, fullMarkdown);

        for (int p = actualStartPage; p < totalPages; ++p) {
            if (m_shouldStop.load() || !aliveToken->load()) {
                hasError = true;
                errorMessage = "用户已取消解析";
                break;
            }

            // 获取当前页阶段 1 预处理结果 (若上一页识别时间大于版面分析时间，此处早已准备就绪，0 毫秒等待)
            std::shared_ptr<PreprocessedPage> curPageData = nullptr;
            if (nextPageFuture.valid()) {
                try {
                    curPageData = nextPageFuture.get();
                } catch (const std::exception& e) {
                    LOG_ERROR("DocumentPipeline", std::string("nextPageFuture.get() exception: ") + e.what());
                    curPageData = nullptr;
                } catch (...) {
                    LOG_ERROR("DocumentPipeline", "nextPageFuture.get() unknown exception");
                    curPageData = nullptr;
                }
            }

            if (!curPageData || !curPageData->success) {
                if (m_shouldStop.load() || !aliveToken->load()) {
                    hasError = true;
                    errorMessage = "用户已取消解析";
                    break;
                }
                LOG_ERROR("DocumentPipeline", "Failed to preprocess stage 1 for page: " + std::to_string(p + 1));
                // 若预处理失败但未取消，为下一页启动预处理并跳过异常页
                if (p + 1 < totalPages && !m_shouldStop.load() && aliveToken->load()) {
                    nextPageFuture = LaunchPreprocessStageOneAsync(p + 1, totalPages, inputFilePath, tempCacheDir, figuresDir, m_layoutEngine, aliveToken, m_shouldStop);
                }
                continue;
            }

            // ★★★ 核心提速重叠：在当前页 p 进入耗时的阶段 2 (VLM OCR) 之前，立刻在后台启动下一页 p+1 的阶段 1 预处理！
            if (p + 1 < totalPages && !m_shouldStop.load() && aliveToken->load()) {
                nextPageFuture = LaunchPreprocessStageOneAsync(p + 1, totalPages, inputFilePath, tempCacheDir, figuresDir, m_layoutEngine, aliveToken, m_shouldStop);
            }

            // 阶段 2：裁剪元素子图并进行 VLM 独立识别
            std::string pageMarkdown;
            json pageJson = json::object();
            pageJson["page_index"] = p + 1;
            pageJson["elements"] = json::array();

            bool pageInterrupted = false;

            // 待识别文本/表格/公式/图表切片索引列表 (排除已在预处理阶段处理完毕的 Image 元素)
            std::vector<size_t> ocrIndices;
            for (size_t elIdx = 0; elIdx < curPageData->elements.size(); ++elIdx) {
                if (!curPageData->elements[elIdx].isImage) {
                    ocrIndices.push_back(elIdx);
                }
            }

            // 记录各元素识别结果与成功状态 (按原始 elIdx 对齐，严格保证拓扑阅读顺序)
            std::vector<std::string> recognizedResults(curPageData->elements.size());
            std::vector<bool> elementSuccess(curPageData->elements.size(), false);

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

            int numWorkers = (std::min)(static_cast<int>(ocrIndices.size()), maxParallel);
            if (numWorkers > 0) {
                std::vector<std::thread> workers;
                workers.reserve(numWorkers);

                struct WorkersJoinGuard {
                    std::vector<std::thread>& w;
                    ~WorkersJoinGuard() {
                        for (auto& t : w) {
                            if (t.joinable()) {
                                try {
                                    t.join();
                                } catch (...) {}
                            }
                        }
                    }
                } workersGuard{workers};

                for (int w = 0; w < numWorkers; ++w) {
                    workers.emplace_back([&]() {
                        try {
                            while (true) {
                                if (m_shouldStop.load() || !aliveToken->load() || pageCancelled.load() || pageCancelToken->load()) {
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
                                        elem.tempCropFile, elem.taskType, nullptr,
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
                                    auto res = ocrFuture.get();
                                    ocrSuccess = res.first;
                                    recognizedText = res.second;
                                } catch (const std::exception& ex) {
                                    LOG_ERROR("DocumentPipeline", std::string("ocrFuture.get() exception: ") + ex.what());
                                    ocrSuccess = false;
                                } catch (...) {
                                    LOG_ERROR("DocumentPipeline", "ocrFuture.get() unknown exception");
                                    ocrSuccess = false;
                                }

                                // 识别完成后立即安全删除当前切片临时文件
                                SafeRemoveTempFile(elem.tempCropFile, inputFilePath, tempCacheDir);

                                if (m_shouldStop.load() || !aliveToken->load() || pageCancelToken->load()) {
                                    pageCancelled.store(true);
                                    break;
                                }

                                recognizedText = TrimString(recognizedText);
                                if (recognizedText.rfind("```markdown", 0) == 0) {
                                    recognizedText = recognizedText.substr(11);
                                } else if (recognizedText.rfind("```", 0) == 0) {
                                    recognizedText = recognizedText.substr(3);
                                }
                                if (recognizedText.size() >= 3 && recognizedText.substr(recognizedText.size() - 3) == "```") {
                                    recognizedText = recognizedText.substr(0, recognizedText.size() - 3);
                                }
                                recognizedText = TrimString(recognizedText);

                                recognizedResults[elIdx] = recognizedText;
                                elementSuccess[elIdx] = ocrSuccess;

                                int doneCount = ++completedOcrCount;
                                {
                                    std::lock_guard<std::mutex> pLock(progressMutex);
                                    if (aliveToken->load() && onProgress) {
                                        std::string stageMsg = "第 " + std::to_string(p + 1) + " / " + std::to_string(totalPages) + " 页: 并发识别元素 (" + std::to_string(doneCount) + "/" +
                                                               std::to_string(ocrIndices.size()) + ")...";

                                        // 实时收集当前页已完成识别切片的临时预览，立即流式推送到前端，杜绝前端界面白屏停滞
                                        std::string currentTempPageMd;
                                        for (size_t idx = 0; idx < curPageData->elements.size(); ++idx) {
                                            if (elementSuccess[idx] && !recognizedResults[idx].empty()) {
                                                if (!currentTempPageMd.empty()) {
                                                    currentTempPageMd += "\n\n";
                                                }
                                                const auto& elemRef = curPageData->elements[idx];
                                                if (elemRef.elem.type == LayoutElementType::Title) {
                                                    currentTempPageMd += (elemRef.elem.labelName == "doc_title" ? "# " : "## ") + recognizedResults[idx];
                                                } else {
                                                    currentTempPageMd += recognizedResults[idx];
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

                for (auto& t : workers) {
                    if (t.joinable()) {
                        try {
                            t.join();
                        } catch (...) {}
                    }
                }
            }

            if (m_shouldStop.load() || !aliveToken->load() || pageCancelled.load() || pageCancelToken->load()) {
                pageInterrupted = true;
            }

            // 健壮性与异常区分检查：区分真实推理服务异常 vs 切片无文字内容 (空内容/空白切片)
            int ocrValidTextCount = 0;
            for (size_t elIdx : ocrIndices) {
                if (!recognizedResults[elIdx].empty()) {
                    ocrValidTextCount++;
                }
            }

            std::string pageFailureReason;
            if (!ocrIndices.empty() && !pageInterrupted) {
                // 仅当所有切片均遭遇网络/HTTP服务错误 (httpFailCount > 0 且 httpSuccessCount == 0)
                // 或者嵌入推理服务处于离线/非就绪状态时，判定为真实服务异常中断
                bool isServerDown = false;
                if (m_modelManager) {
                    auto health = m_modelManager->GetHealthStatus(TargetModelType::Ocr);
                    if (health.state != ServerHealthState::Ready && health.state != ServerHealthState::Loading) {
                        isServerDown = true;
                    }
                }

                if ((httpFailCount.load() > 0 && httpSuccessCount.load() == 0) || isServerDown) {
                    std::string detailErr = firstOcrError.empty() ? (isServerDown ? "OCR 服务未处于就绪状态" : "嵌入推理服务异常或未响应") : firstOcrError;
                    LOG_ERROR("DocumentPipeline", "第 " + std::to_string(p + 1) + " 页所有切片识别均因服务异常失败: " + detailErr);
                    pageInterrupted = true;
                    pageFailureReason = "第 " + std::to_string(p + 1) + " 页 OCR 识别失败 (" + detailErr + ")";
                } else if (ocrValidTextCount == 0) {
                    LOG_INFO("DocumentPipeline", "第 " + std::to_string(p + 1) + " 页所有切片推理成功完成，但未检测到文本内容 (空白页或非文字图形)，正常记录为空页");
                }
            }

            // 收集当前页正文主体内容 (非底部敏感区域的文本) 用于检测正文中是否存在注解数字标记
            std::string pageBodyText;
            if (curPageData->pageHeight > 0) {
                for (size_t elIdx = 0; elIdx < curPageData->elements.size(); ++elIdx) {
                    const auto& elem = curPageData->elements[elIdx];
                    if (!elem.isImage && elem.elem.type != LayoutElementType::Table) {
                        float bottomRatio = static_cast<float>(elem.y2) / static_cast<float>(curPageData->pageHeight);
                        if (bottomRatio < 0.80f) {
                            pageBodyText += " " + recognizedResults[elIdx];
                        }
                    }
                }
            }
            const bool pageHasAnnotationMarkers = HasAnnotationMarkers(pageBodyText);

            // 按严格拓扑阅读顺序组装 Markdown 与 JSON
            for (size_t elIdx = 0; elIdx < curPageData->elements.size(); ++elIdx) {
                const auto& elem = curPageData->elements[elIdx];

                // 图像 / 图表元素处理：直接生成 PaddleOCR 标准 HTML/Markdown 引用并记入 JSON
                if (elem.isImage) {
                    if (!pageMarkdown.empty()) {
                        while (!pageMarkdown.empty() && (pageMarkdown.back() == '\n' || pageMarkdown.back() == '\r')) {
                            pageMarkdown.pop_back();
                        }
                        pageMarkdown += "\n\n";
                    }
                    pageMarkdown += elem.imgTag;

                    json elJson = {{"id", elem.elem.id},
                                   {"type", elem.elem.labelName},
                                   {"reading_order", elem.elem.readingOrder},
                                   {"box", {elem.x1, elem.y1, elem.x2, elem.y2}},
                                   {"figure_path", elem.figRelPath}};
                    pageJson["elements"].push_back(elJson);
                    continue;
                }

                // 若被中断且该元素尚未完成识别，则跳过
                if (pageInterrupted && !elementSuccess[elIdx] && recognizedResults[elIdx].empty()) {
                    continue;
                }

                std::string recognizedText = recognizedResults[elIdx];

                // 综合研判是否属于底部注解/脚注 (结合模型标签、正文注解数字标记及文本形态)
                bool isFootnote = IsFootnoteOrAnnotation(
                    recognizedText, elem.elem.labelName,
                    elem.y1, elem.y2, curPageData->pageHeight,
                    pageHasAnnotationMarkers
                );

                // 二次防护：过滤页眉左右两侧及页脚中的独立页码，防止污染正文 Markdown 与 JSON 结果
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
                    std::string cleanFormula = recognizedText;
                    auto trimStr = [](std::string& s) {
                        while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
                            s.erase(s.begin());
                        }
                        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
                            s.pop_back();
                        }
                    };
                    trimStr(cleanFormula);
                    bool stripped = true;
                    while (stripped && cleanFormula.size() >= 2) {
                        stripped = false;
                        if (cleanFormula.size() >= 4 && cleanFormula.rfind("$$", 0) == 0 && cleanFormula.compare(cleanFormula.size() - 2, 2, "$$") == 0) {
                            cleanFormula = cleanFormula.substr(2, cleanFormula.size() - 4);
                            trimStr(cleanFormula);
                            stripped = true;
                        } else if (cleanFormula.size() >= 4 && cleanFormula.rfind("\\[", 0) == 0 && cleanFormula.compare(cleanFormula.size() - 2, 2, "\\]") == 0) {
                            cleanFormula = cleanFormula.substr(2, cleanFormula.size() - 4);
                            trimStr(cleanFormula);
                            stripped = true;
                        } else if (cleanFormula.size() >= 4 && cleanFormula.rfind("\\(", 0) == 0 && cleanFormula.compare(cleanFormula.size() - 2, 2, "\\)") == 0) {
                            cleanFormula = cleanFormula.substr(2, cleanFormula.size() - 4);
                            trimStr(cleanFormula);
                            stripped = true;
                        } else if (cleanFormula.size() >= 2 && cleanFormula.front() == '$' && cleanFormula.back() == '$' && cleanFormula.size() > 2 && cleanFormula[1] != '$') {
                            cleanFormula = cleanFormula.substr(1, cleanFormula.size() - 2);
                            trimStr(cleanFormula);
                            stripped = true;
                        }
                    }
                    formattedContent = "$$\n" + cleanFormula + "\n$$";
                    recognizedText = cleanFormula;
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
                    // 若启用英文文档自动翻译，且段落为纯英文，调用 Hy-MT2 翻译引擎
                    if (translateEnglishToChinese && IsPredominantlyEnglish(recognizedText) && m_modelManager) {
                        TranslationTask transTask(recognizedText, LanguageCode::English, LanguageCode::Chinese);
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
                        formattedContent = transResult.empty() ? recognizedText : transResult;
                    } else {
                        formattedContent = recognizedText;
                    }
                }

                if (!formattedContent.empty()) {
                    if (!pageMarkdown.empty()) {
                        while (!pageMarkdown.empty() && (pageMarkdown.back() == '\n' || pageMarkdown.back() == '\r')) {
                            pageMarkdown.pop_back();
                        }
                        pageMarkdown += "\n\n";
                    }
                    pageMarkdown += formattedContent;
                }

                std::string finalType = isFootnote ? "footnote" : elem.elem.labelName;
                json elJson = {
                    {"id", elem.elem.id}, {"type", finalType}, {"reading_order", elem.elem.readingOrder}, {"box", {elem.x1, elem.y1, elem.x2, elem.y2}}, {"content", recognizedText}};
                pageJson["elements"].push_back(elJson);
            }

            // 确保当前页所有临时文件均已清理完毕
            curPageData->CleanRemainingTempFiles(inputFilePath, tempCacheDir, false);

            if (pageInterrupted) {
                // 中断处理：
                // 1. 实时保存当前中断页已经识别的部分
                pageJson["markdown"] = pageMarkdown;
                pageJson["is_page_completed"] = false;
                if (!pageMarkdown.empty()) {
                    if (!fullMarkdown.empty()) {
                        fullMarkdown += "\n\n---\n\n";
                    }
                    fullMarkdown += pageMarkdown;
                    fullDocJson["pages"].push_back(pageJson);
                }
                fullDocJson["completed_pages"] = p;
                fullDocJson["last_interrupted_page"] = p + 1;
                fullDocJson["is_completed"] = false;

                // 实时落盘保存被中断时的进度与数据，保证随时可断点恢复
                std::string interruptedJsonStr = SafeDumpJson(fullDocJson);
                SaveToMarkdown(actualOutputDir, baseDocName, fullMarkdown);
                SaveToJson(actualOutputDir, baseDocName, interruptedJsonStr);

                // 2. 核心清理：立即安全回收后台预取的下一页临时数据（防止污染磁盘或残留未消费图片）
                DrainFutureAndClean(nextPageFuture, inputFilePath, tempCacheDir);

                hasError = true;
                errorMessage = pageFailureReason.empty() ? "用户已取消解析" : pageFailureReason;
                break;
            }

            pageJson["markdown"] = pageMarkdown;
            pageJson["is_page_completed"] = true;
            fullDocJson["pages"].push_back(pageJson);

            if (!pageMarkdown.empty()) {
                if (!fullMarkdown.empty()) {
                    fullMarkdown += "\n\n---\n\n";
                }
                fullMarkdown += pageMarkdown;
            }

            fullDocJson["completed_pages"] = p + 1;
            fullDocJson["is_completed"] = (p + 1 == totalPages);

            // ★★★ 核心要求 1：每页完成识别后实时落盘保存 Markdown 与 JSON，杜绝中断时数据丢失
            std::string currentJsonDump = SafeDumpJson(fullDocJson);
            SaveToMarkdown(actualOutputDir, baseDocName, fullMarkdown);
            SaveToJson(actualOutputDir, baseDocName, currentJsonDump);

            if (aliveToken->load() && onProgress) {
                onProgress(p + 1, totalPages, "第 " + std::to_string(p + 1) + " 页解析完成", fullMarkdown);
            }
        }

        // 循环退出后，若仍有未消费的 nextPageFuture，安全回收其临时文件
        DrainFutureAndClean(nextPageFuture, inputFilePath, tempCacheDir);

        // 5. 阶段 3：restructure_pages 页面重构与自动落盘
        std::string jsonDumpStr = SafeDumpJson(fullDocJson);

        // 确保最新解析内容安全落盘
        SaveToMarkdown(actualOutputDir, baseDocName, fullMarkdown);
        SaveToJson(actualOutputDir, baseDocName, jsonDumpStr);
        if (!hasError && !m_shouldStop.load()) {
            LOG_INFO("DocumentPipeline", "Document parse completed, results saved to: " + actualOutputDir);
        }

        m_isRunning.store(false);

        if (!aliveToken->load())
            return;

        if (onComplete) {
            if (hasError) {
                onComplete(false, fullMarkdown, jsonDumpStr, errorMessage);
            } else {
                onComplete(true, fullMarkdown, jsonDumpStr, "");
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("DocumentPipeline", std::string("Document parsing fatal exception: ") + e.what());
        m_isRunning.store(false);
        if (aliveToken->load() && onComplete) {
            onComplete(false, fullMarkdown, "{}", std::string("解析异常: ") + e.what());
        }
    } catch (...) {
        LOG_ERROR("DocumentPipeline", "Document parsing fatal unknown exception");
        m_isRunning.store(false);
        if (aliveToken->load() && onComplete) {
            onComplete(false, fullMarkdown, "{}", "解析发生未知严重异常");
        }
    }
    }).detach();
}

} // namespace LinguaAlpaca
