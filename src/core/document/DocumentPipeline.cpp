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

using json = nlohmann::json;

namespace LinguaAlpaca {

namespace {

std::string TrimString(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

bool IsPredominantlyEnglish(const std::string& text) {
    if (text.empty()) return false;
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
    if (path1.empty() || path2.empty()) return false;
    if (path1 == path2) return true;
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
    if (pathToRemove.empty()) return;

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

} // namespace

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
            if (!row.empty()) rowTokens.push_back(row);
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
            if (tagStart == std::string::npos) break;
            size_t tagEnd = rowStr.find('>', tagStart);
            if (tagEnd == std::string::npos) break;

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

    if (grid.empty()) return otslStr;

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
            if (ch == '&') res += "&amp;";
            else if (ch == '<') res += "&lt;";
            else if (ch == '>') res += "&gt;";
            else if (ch == '"') res += "&quot;";
            else if (ch == '\'') res += "&#039;";
            else res += ch;
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

DocumentPipeline::DocumentPipeline(
    std::shared_ptr<ModelManager> modelManager,
    std::shared_ptr<DocLayoutEngine> layoutEngine)
    : m_modelManager(std::move(modelManager))
    , m_layoutEngine(std::move(layoutEngine))
    , m_aliveToken(std::make_shared<std::atomic<bool>>(true)) {
}

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
    wxString dir = wxString::FromUTF8(saveDir);
    if (!wxDirExists(dir)) {
        wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    }
    wxString filePath = dir + "/" + wxString::FromUTF8(baseName) + ".md";
    std::ofstream out(filePath.ToStdWstring(), std::ios::binary);
    if (!out) return false;
    out.write(markdownContent.data(), markdownContent.size());
    out.close();
    return true;
}

bool DocumentPipeline::SaveToJson(const std::string& saveDir, const std::string& baseName, const std::string& jsonContent) {
    wxString dir = wxString::FromUTF8(saveDir);
    if (!wxDirExists(dir)) {
        wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    }
    wxString filePath = dir + "/" + wxString::FromUTF8(baseName) + ".json";
    std::ofstream out(filePath.ToStdWstring(), std::ios::binary);
    if (!out) return false;
    out.write(jsonContent.data(), jsonContent.size());
    out.close();
    return true;
}

void DocumentPipeline::StartParseAsync(
    const std::string& inputFilePath,
    const std::string& outputDir,
    bool translateEnglishToChinese,
    DocProgressCallback onProgress,
    DocCompleteCallback onComplete) {

    Cancel();
    m_shouldStop.store(false);
    m_isRunning.store(true);

    auto aliveToken = m_aliveToken;

    std::thread([this, aliveToken, inputFilePath, outputDir, translateEnglishToChinese, onProgress, onComplete]() {
        if (!aliveToken->load()) return;

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

        LOG_INFO("DocumentPipeline", "Start parsing document: " + inputFilePath + " (Total pages: " + std::to_string(totalPages) + ")");

        // 建立临时工作缓存目录
        wxString tempCacheDir = wxStandardPaths::Get().GetTempDir() + "/LinguaAlpaca_doccache";
        if (!wxDirExists(tempCacheDir)) {
            wxFileName::Mkdir(tempCacheDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        }

        std::string fullMarkdown;
        json fullDocJson = json::object();
        fullDocJson["file_name"] = baseDocName;
        fullDocJson["total_pages"] = totalPages;
        fullDocJson["pages"] = json::array();

        bool hasError = false;
        std::string errorMessage;

        for (int p = 0; p < totalPages; ++p) {
            if (m_shouldStop.load() || !aliveToken->load()) {
                hasError = true;
                errorMessage = "用户已取消解析";
                break;
            }

            std::string pagePromptStatus = "第 " + std::to_string(p + 1) + " / " + std::to_string(totalPages) + " 页: 正在光栅化...";
            if (aliveToken->load() && onProgress) {
                onProgress(p, totalPages, pagePromptStatus, fullMarkdown);
            }

            // 1. 光栅化当前单页 (防止内存溢出：一次只保留一页高分辨率图)
            std::string pageImgPath = PdfHelper::RenderPageToTempFile(inputFilePath, p, tempCacheDir.ToUTF8().data(), 1600);
            if (pageImgPath.empty() || !wxFileExists(wxString::FromUTF8(pageImgPath))) {
                LOG_ERROR("DocumentPipeline", "Failed to rasterize page: " + std::to_string(p + 1));
                continue;
            }

            // 2. 阶段 1：DocLayoutEngine 执行版面目标检测 (PP-DocLayout ONNX Runtime)
            if (aliveToken->load() && onProgress) {
                onProgress(p, totalPages, "第 " + std::to_string(p + 1) + " / " + std::to_string(totalPages) + " 页: 版面分析中 (检测标题/表格/公式/分栏)...", fullMarkdown);
            }

            DocumentLayoutResult layoutResult;
            layoutResult.pageIndex = p + 1;
            if (m_layoutEngine) {
                m_layoutEngine->AnalyzeLayout(pageImgPath, layoutResult);
            }

            // 载入当前单页位图用于裁剪子图
            wxImage pageImg;
            if (!pageImg.LoadFile(wxString::FromUTF8(pageImgPath))) {
                SafeRemoveTempFile(pageImgPath, inputFilePath, tempCacheDir);
                continue;
            }

            int pImgW = pageImg.GetWidth();
            int pImgH = pageImg.GetHeight();

            std::string pageMarkdown;
            json pageJson = json::object();
            pageJson["page_index"] = p + 1;
            pageJson["elements"] = json::array();

            // 3. 阶段 2：裁剪元素子图并进行 VLM 独立识别
            for (size_t elIdx = 0; elIdx < layoutResult.elements.size(); ++elIdx) {
                if (m_shouldStop.load() || !aliveToken->load()) {
                    break;
                }

                const auto& elem = layoutResult.elements[elIdx];
                int x1 = (std::clamp)(elem.x1, 0, pImgW);
                int y1 = (std::clamp)(elem.y1, 0, pImgH);
                int x2 = (std::clamp)(elem.x2, 0, pImgW);
                int y2 = (std::clamp)(elem.y2, 0, pImgH);
                int cropW = (std::max)(1, x2 - x1);
                int cropH = (std::max)(1, y2 - y1);

                if (cropW < 6 || cropH < 6) continue;

                // 裁剪当前元素子图
                wxImage crop = pageImg.GetSubImage(wxRect(x1, y1, cropW, cropH));

                // 图像 / 图表元素处理：直接保存到 output_dir/imgs/ 并生成 PaddleOCR 标准 HTML/Markdown 引用
                if (elem.type == LayoutElementType::Image) {
                    wxString figFileName = wxString::Format("img_p%d_box_%d_%d_%d_%d.jpg", p + 1, x1, y1, x2, y2);
                    wxString absFigPath = figuresDir + "/" + figFileName;
                    crop.SaveFile(absFigPath, wxBITMAP_TYPE_JPEG);

                    std::string relPath = "imgs/" + std::string(figFileName.ToUTF8().data());
                    int widthPct = static_cast<int>(std::round(static_cast<double>(cropW) * 100.0 / pImgW));
                    if (widthPct >= 80) {
                        widthPct = 100;
                    } else if (widthPct < 25) {
                        widthPct = 25;
                    }

                    std::string imgTag = "<div style=\"text-align: center;\"><img src=\"" + relPath + "\" alt=\"Image\" width=\"" + std::to_string(widthPct) + "%\" style=\"max-width: 100%; max-height: 600px; object-fit: contain;\" /></div>";
                    if (!pageMarkdown.empty()) {
                        while (!pageMarkdown.empty() && (pageMarkdown.back() == '\n' || pageMarkdown.back() == '\r')) {
                            pageMarkdown.pop_back();
                        }
                        pageMarkdown += "\n\n";
                    }
                    pageMarkdown += imgTag;

                    json elJson = {
                        {"id", elem.id},
                        {"type", elem.labelName},
                        {"reading_order", elem.readingOrder},
                        {"box", {x1, y1, x2, y2}},
                        {"figure_path", relPath}
                    };
                    pageJson["elements"].push_back(elJson);
                    continue;
                }

                // 文本、标题、表格、公式：调用 PaddleOCR-VL VLM 进行独立子图识别
                wxString tempCropFile = tempCacheDir + wxString::Format("/crop_p%d_e%d.png", p + 1, elem.id);
                crop.SaveFile(tempCropFile, wxBITMAP_TYPE_PNG);

                std::string taskType = "ocr";
                if (elem.type == LayoutElementType::Table) {
                    taskType = "table";
                } else if (elem.type == LayoutElementType::Formula) {
                    taskType = "formula";
                } else if (elem.type == LayoutElementType::Chart) {
                    taskType = "chart";
                }

                std::string stageMsg = "第 " + std::to_string(p + 1) + " / " + std::to_string(totalPages) + 
                    " 页: 识别 " + elem.labelName + " (" + std::to_string(elIdx + 1) + "/" + 
                    std::to_string(layoutResult.elements.size()) + ")...";

                if (aliveToken->load() && onProgress) {
                    onProgress(p, totalPages, stageMsg, fullMarkdown + (!pageMarkdown.empty() ? "\n\n" + pageMarkdown : ""));
                }

                // 同步等待当前子图推理完成
                std::promise<std::pair<bool, std::string>> ocrPromise;
                auto ocrFuture = ocrPromise.get_future();

                if (m_modelManager) {
                    m_modelManager->ExecuteOcrStream(
                        tempCropFile.ToUTF8().data(),
                        taskType,
                        nullptr, // 批量流中只需完整结果
                        [&ocrPromise](const std::string& fullText, bool success, const std::string& err) {
                            ocrPromise.set_value({success, fullText});
                        }
                    );
                } else {
                    ocrPromise.set_value({false, ""});
                }

                auto [ocrSuccess, recognizedText] = ocrFuture.get();
                SafeRemoveTempFile(tempCropFile.ToUTF8().data(), inputFilePath, tempCacheDir);

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

                // 结构化格式化输出
                std::string formattedContent;
                if (elem.type == LayoutElementType::Formula) {
                    formattedContent = "$$\n" + recognizedText + "\n$$";
                } else if (elem.type == LayoutElementType::Title) {
                    if (elem.labelName == "doc_title") {
                        formattedContent = "# " + recognizedText;
                    } else {
                        formattedContent = "## " + recognizedText;
                    }
                } else if (elem.type == LayoutElementType::Table) {
                    formattedContent = ConvertOtslToHtml(recognizedText);
                } else {
                    // 若启用英文文档自动翻译，且段落为纯英文，调用 Hy-MT2 翻译引擎
                    if (translateEnglishToChinese && IsPredominantlyEnglish(recognizedText) && m_modelManager) {
                        TranslationTask transTask(recognizedText, LanguageCode::English, LanguageCode::Chinese);
                        std::promise<std::string> transPromise;
                        auto transFuture = transPromise.get_future();

                        m_modelManager->ExecuteTranslationStream(
                            transTask,
                            nullptr,
                            [&transPromise](bool success, const std::string& fullText, const std::string&) {
                                transPromise.set_value(success ? fullText : "");
                            }
                        );

                        std::string transResult = transFuture.get();
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

                json elJson = {
                    {"id", elem.id},
                    {"type", elem.labelName},
                    {"reading_order", elem.readingOrder},
                    {"box", {x1, y1, x2, y2}},
                    {"content", recognizedText}
                };
                pageJson["elements"].push_back(elJson);
            }

            // 4. 单页内存与临时文件即刻回收 (O(1) 恒定内存，绝对不删除用户原始输入文档/图片)
            pageImg.Destroy();
            SafeRemoveTempFile(pageImgPath, inputFilePath, tempCacheDir);

            fullDocJson["pages"].push_back(pageJson);

            if (!fullMarkdown.empty()) {
                fullMarkdown += "\n\n---\n\n";
            }
            fullMarkdown += pageMarkdown;

            if (aliveToken->load() && onProgress) {
                onProgress(p + 1, totalPages, "第 " + std::to_string(p + 1) + " 页解析完成", fullMarkdown);
            }
        }

        // 5. 阶段 3：restructure_pages 页面重构与自动落盘
        std::string jsonDumpStr;
        try {
            jsonDumpStr = fullDocJson.dump(2, ' ', false, json::error_handler_t::replace);
        } catch (...) {
            jsonDumpStr = "{}";
        }

        if (!hasError && !m_shouldStop.load()) {
            SaveToMarkdown(actualOutputDir, baseDocName, fullMarkdown);
            SaveToJson(actualOutputDir, baseDocName, jsonDumpStr);
            LOG_INFO("DocumentPipeline", "Document parse completed, results saved to: " + actualOutputDir);
        }

        m_isRunning.store(false);

        if (!aliveToken->load()) return;

        if (onComplete) {
            if (hasError) {
                onComplete(false, fullMarkdown, jsonDumpStr, errorMessage);
            } else {
                onComplete(true, fullMarkdown, jsonDumpStr, "");
            }
        }
    }).detach();
}

} // namespace LinguaAlpaca
