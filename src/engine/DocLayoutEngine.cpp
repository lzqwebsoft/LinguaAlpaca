#pragma execution_character_set("utf-8")
#include "DocLayoutEngine.hpp"
#include "core/Logger.hpp"

#include <onnxruntime_cxx_api.h>

#include <wx/filefn.h>
#include <wx/image.h>
#include <wx/log.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace LinguaAlpaca {

struct DocLayoutEngine::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "DocLayoutEngine"};
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
    std::mutex sessionMutex;
};

DocLayoutEngine::DocLayoutEngine()
    : m_impl(std::make_unique<Impl>()) {
}

DocLayoutEngine::~DocLayoutEngine() {
    Unload();
}

std::string DocLayoutEngine::ResolveLayoutModelPath(const std::string& path) {
    if (path.empty()) return "";
    try {
        std::filesystem::path p(path);
        if (std::filesystem::is_regular_file(p)) {
            return p.string();
        }
        if (std::filesystem::is_directory(p)) {
            const std::vector<std::string> preferred = {
                "PP-DocLayoutV3.onnx",
                "PP-DocLayoutV2.onnx",
                "model.onnx"
            };
            for (const auto& name : preferred) {
                auto sub = p / name;
                if (std::filesystem::is_regular_file(sub)) {
                    return sub.string();
                }
            }
            for (const auto& entry : std::filesystem::directory_iterator(p)) {
                if (entry.is_regular_file() && entry.path().extension() == ".onnx") {
                    return entry.path().string();
                }
            }
        }
    } catch (...) {}
    if (wxFileExists(wxString::FromUTF8(path))) {
        return path;
    }
    return "";
}

bool DocLayoutEngine::Initialize(const std::string& modelPath, int executionProvider, int threads) {
    std::lock_guard<std::mutex> lock(m_impl->sessionMutex);
    m_lastError.clear();
    m_isLoaded = false;
    m_impl->session.reset();
    m_impl->inputNames.clear();
    m_impl->outputNames.clear();

    std::string actualPath = ResolveLayoutModelPath(modelPath);
    if (actualPath.empty()) {
        m_lastError = "模型文件不存在: " + modelPath;
        LOG_WARN("DocLayoutEngine", "Model file does not exist: " + modelPath);
        return false;
    }

    // 检查文件有效性与 Git LFS 占位指针防护
    try {
        std::error_code ec;
        auto fsize = std::filesystem::file_size(actualPath, ec);
        if (!ec) {
            if (fsize < 1024) {
                std::ifstream checkFile(actualPath);
                std::string firstLine;
                if (std::getline(checkFile, firstLine) && firstLine.find("version https://git-lfs") != std::string::npos) {
                    m_lastError = "模型文件为 Git LFS 占位指针 (134字节)，未包含真实权重。请在模型目录运行 git lfs pull 或重新下载完整 .onnx 文件: " + actualPath;
                    LOG_ERROR("DocLayoutEngine", m_lastError);
                    return false;
                }
                m_lastError = "模型文件过小 (" + std::to_string(fsize) + " 字节)，并非有效的 ONNX 模型: " + actualPath;
                LOG_ERROR("DocLayoutEngine", m_lastError);
                return false;
            }
        }
    } catch (...) {}

    try {
        Ort::SessionOptions sessionOptions;
        if (threads > 0) {
            sessionOptions.SetIntraOpNumThreads(threads);
        } else {
            sessionOptions.SetIntraOpNumThreads(4);
        }
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // 如果用户选择了硬件加速模式 (DirectML on Windows, CoreML on macOS)
        if (executionProvider == 1) {
#if defined(_WIN32)
            LOG_INFO("DocLayoutEngine", "Hardware acceleration selected (DirectML mode)");
#elif defined(__APPLE__)
            LOG_INFO("DocLayoutEngine", "Hardware acceleration selected (CoreML mode)");
#endif
        }

#ifdef _WIN32
        std::wstring wModelPath = wxString::FromUTF8(actualPath).ToStdWstring();
        m_impl->session = std::make_unique<Ort::Session>(m_impl->env, wModelPath.c_str(), sessionOptions);
#else
        m_impl->session = std::make_unique<Ort::Session>(m_impl->env, actualPath.c_str(), sessionOptions);
#endif

        Ort::AllocatorWithDefaultOptions allocator;
        size_t numInputs = m_impl->session->GetInputCount();
        for (size_t i = 0; i < numInputs; ++i) {
            auto inputName = m_impl->session->GetInputNameAllocated(i, allocator);
            m_impl->inputNames.push_back(inputName.get());
        }

        size_t numOutputs = m_impl->session->GetOutputCount();
        for (size_t i = 0; i < numOutputs; ++i) {
            auto outputName = m_impl->session->GetOutputNameAllocated(i, allocator);
            m_impl->outputNames.push_back(outputName.get());
        }

        m_modelPath = actualPath;
        m_executionProvider = executionProvider;
        m_threads = threads;
        m_isLoaded = true;

        LOG_INFO("DocLayoutEngine", "Loaded ONNX model successfully: " + actualPath);
        return true;
    } catch (const Ort::Exception& e) {
        std::string whatStr = e.what();
        if (whatStr.find("Protobuf parsing failed") != std::string::npos || whatStr.find("INVALID_PROTOBUF") != std::string::npos) {
            m_lastError = "ONNX 模型解析失败 (Protobuf parsing failed): 权重文件损坏或不完整。若通过 Git 下载，请确认已安装 git-lfs 并拉取了真实权重: " + actualPath;
        } else {
            m_lastError = std::string("ONNX Runtime 异常: ") + whatStr;
        }
        LOG_ERROR("DocLayoutEngine", "Failed to load model: " + m_lastError);
        return false;
    } catch (const std::exception& e) {
        m_lastError = std::string("加载失败: ") + e.what();
        LOG_ERROR("DocLayoutEngine", "Failed to load model: " + m_lastError);
        return false;
    }
}

bool DocLayoutEngine::IsLoaded() const {
    return m_isLoaded && m_impl && m_impl->session;
}

void DocLayoutEngine::Unload() {
    std::lock_guard<std::mutex> lock(m_impl->sessionMutex);
    m_impl->session.reset();
    m_impl->inputNames.clear();
    m_impl->outputNames.clear();
    m_isLoaded = false;
}

namespace {

// PP-DocLayoutV3 官方 25 类标签完整定义 (对齐 PP-DocLayoutV3 config.json 规范)
const std::vector<std::string> kDocLayoutLabels = {
    "abstract",          // 0
    "algorithm",         // 1
    "aside_text",        // 2
    "chart",             // 3
    "content",           // 4
    "display_formula",   // 5
    "doc_title",         // 6
    "figure_title",      // 7
    "footer",            // 8
    "footer_image",      // 9
    "footnote",          // 10
    "formula_number",    // 11
    "header",            // 12
    "header_image",      // 13
    "image",             // 14
    "inline_formula",    // 15
    "number",            // 16
    "paragraph_title",   // 17
    "reference",         // 18
    "reference_content", // 19
    "seal",              // 20
    "table",             // 21
    "text",              // 22
    "vertical_text",     // 23
    "vision_footnote"    // 24
};

inline std::pair<LayoutElementType, std::string> MapDocLayoutClass(int classId) {
    std::string labelName = "text";
    if (classId >= 0 && classId < static_cast<int>(kDocLayoutLabels.size())) {
        labelName = kDocLayoutLabels[classId];
    }

    LayoutElementType type = LayoutElementType::Text;
    if (labelName == "doc_title" || labelName == "paragraph_title") {
        type = LayoutElementType::Title;
    } else if (labelName == "image" || labelName == "seal") {
        type = LayoutElementType::Image;
    } else if (labelName == "table") {
        type = LayoutElementType::Table;
    } else if (labelName == "display_formula" || labelName == "inline_formula") {
        type = LayoutElementType::Formula;
    } else if (labelName == "chart") {
        type = LayoutElementType::Chart;
    } else if (labelName == "header" || labelName == "header_image") {
        type = LayoutElementType::Header;
    } else if (labelName == "footer" || labelName == "footer_image") {
        type = LayoutElementType::Footer;
    } else {
        type = LayoutElementType::Text;
    }

    return {type, labelName};
}

/**
 * @brief 空间分栏拓扑排序算法 (对齐 PaddleX / XY-Cut++ 规范，作为模型缺少 read_order 时的兜底)
 * 
 * 核心原理：
 * 1. 识别通栏跨列元素 (Span Elements，如主标题、全宽大图、通栏大表格)，将其作为水平断层将页面垂直分段 (Bands)；
 * 2. 在每个分段 (Band) 内部，检测元素是否呈现左右分栏分布；
 * 3. 若存在分栏，优先按“先完整读完左栏自上而下所有段落，再读右栏自上而下所有段落”的逻辑拼接；
 * 4. 若为单栏，自上而下自然流式阅读。
 */
void SortLayoutReadingOrderRobust(std::vector<LayoutElement>& elements, int origW, int origH) {
    if (elements.size() <= 1) return;

    const float spanThreshold = origW * 0.60f;
    auto isSpanElement = [spanThreshold](const LayoutElement& elem) {
        if ((elem.x2 - elem.x1) >= spanThreshold) return true;
        if (elem.type == LayoutElementType::Title && elem.labelName == "doc_title") return true;
        return false;
    };

    std::vector<LayoutElement> spanElements;
    std::vector<LayoutElement> localElements;
    for (const auto& elem : elements) {
        if (isSpanElement(elem)) {
            spanElements.push_back(elem);
        } else {
            localElements.push_back(elem);
        }
    }

    // 通栏元素按 Y 坐标自上而下排序
    std::sort(spanElements.begin(), spanElements.end(), [](const LayoutElement& a, const LayoutElement& b) {
        if (a.y1 != b.y1) return a.y1 < b.y1;
        return a.x1 < b.x1;
    });

    // 构建水平垂直分段 (Bands)
    struct Band {
        int yTop{0};
        int yBottom{0};
        LayoutElement* spanAnchor{nullptr};
        std::vector<LayoutElement> columnElements;
    };

    std::vector<Band> bands;
    int currentY = 0;

    for (auto& span : spanElements) {
        if (span.y1 > currentY) {
            Band colBand;
            colBand.yTop = currentY;
            colBand.yBottom = span.y1;
            colBand.spanAnchor = nullptr;
            bands.push_back(colBand);
        }

        Band spanBand;
        spanBand.yTop = span.y1;
        spanBand.yBottom = span.y2;
        spanBand.spanAnchor = &span;
        bands.push_back(spanBand);

        currentY = (std::max)(currentY, span.y2);
    }

    if (currentY < origH) {
        Band lastBand;
        lastBand.yTop = currentY;
        lastBand.yBottom = origH;
        lastBand.spanAnchor = nullptr;
        bands.push_back(lastBand);
    }

    // 分配局部元素到各个 Band
    for (const auto& elem : localElements) {
        int elemMidY = (elem.y1 + elem.y2) / 2;
        bool assigned = false;
        for (auto& band : bands) {
            if (band.spanAnchor == nullptr) {
                if (elemMidY >= band.yTop && elemMidY <= band.yBottom) {
                    band.columnElements.push_back(elem);
                    assigned = true;
                    break;
                }
            }
        }
        if (!assigned) {
            int minDist = 1e9;
            Band* bestBand = nullptr;
            for (auto& band : bands) {
                if (band.spanAnchor == nullptr) {
                    int d = std::abs(elemMidY - (band.yTop + band.yBottom) / 2);
                    if (d < minDist) {
                        minDist = d;
                        bestBand = &band;
                    }
                }
            }
            if (bestBand) {
                bestBand->columnElements.push_back(elem);
            } else if (!bands.empty()) {
                bands.back().columnElements.push_back(elem);
            }
        }
    }

    // 对每个 Band 内部进行排序拼接
    std::vector<LayoutElement> sortedElements;
    sortedElements.reserve(elements.size());

    const float midX = origW * 0.5f;

    for (auto& band : bands) {
        if (band.spanAnchor != nullptr) {
            sortedElements.push_back(*band.spanAnchor);
            continue;
        }

        if (band.columnElements.empty()) continue;

        int leftCount = 0;
        int rightCount = 0;
        for (const auto& elem : band.columnElements) {
            float elemCenterX = (elem.x1 + elem.x2) * 0.5f;
            if (elemCenterX < midX) {
                leftCount++;
            } else {
                rightCount++;
            }
        }

        // 判定是否存在分栏
        bool isMultiColumn = (leftCount > 0 && rightCount > 0);

        if (isMultiColumn) {
            std::vector<LayoutElement> leftCol;
            std::vector<LayoutElement> rightCol;
            for (const auto& elem : band.columnElements) {
                float elemCenterX = (elem.x1 + elem.x2) * 0.5f;
                if (elemCenterX < midX) {
                    leftCol.push_back(elem);
                } else {
                    rightCol.push_back(elem);
                }
            }

            // 左栏自上而下排序
            std::sort(leftCol.begin(), leftCol.end(), [](const LayoutElement& a, const LayoutElement& b) {
                if (std::abs(a.y1 - b.y1) > 8) return a.y1 < b.y1;
                return a.x1 < b.x1;
            });

            // 右栏自上而下排序
            std::sort(rightCol.begin(), rightCol.end(), [](const LayoutElement& a, const LayoutElement& b) {
                if (std::abs(a.y1 - b.y1) > 8) return a.y1 < b.y1;
                return a.x1 < b.x1;
            });

            // 左栏读完再读右栏
            for (auto& e : leftCol) sortedElements.push_back(e);
            for (auto& e : rightCol) sortedElements.push_back(e);
        } else {
            // 单栏：常规自然从上到下排序
            std::sort(band.columnElements.begin(), band.columnElements.end(), [](const LayoutElement& a, const LayoutElement& b) {
                int aMidY = (a.y1 + a.y2) / 2;
                int bMidY = (b.y1 + b.y2) / 2;
                if (std::abs(aMidY - bMidY) < 15) {
                    return a.x1 < b.x1;
                }
                return a.y1 < b.y1;
            });
            for (auto& e : band.columnElements) sortedElements.push_back(e);
        }
    }

    elements = std::move(sortedElements);
}

} // anonymous namespace

/**
 * @brief 判断当前候选版面元素是否属于应该被过滤的页眉/页脚/页码 (双重过滤策略)
 */
bool DocLayoutEngine::ShouldFilterElement(const LayoutElement& elem,
                                          int origW,
                                          int origH,
                                          const DocLayoutFilterConfig& cfg) {
    if (origH <= 0 || origW <= 0) return false;

    const float boxH = static_cast<float>(elem.y2 - elem.y1);
    const float boxW = static_cast<float>(elem.x2 - elem.x1);
    const float boxHeightRatio = boxH / static_cast<float>(origH);
    const float boxWidthRatio = boxW / static_cast<float>(origW);
    const float topRatio = static_cast<float>(elem.y1) / static_cast<float>(origH);
    const float bottomRatio = static_cast<float>(elem.y2) / static_cast<float>(origH);
    const float leftRatio = static_cast<float>(elem.x1) / static_cast<float>(origW);
    const float rightRatio = static_cast<float>(elem.x2) / static_cast<float>(origW);

    const float effectiveHeaderMargin = (std::max)(cfg.headerMarginRatio, 0.18f);
    const float effectiveFooterMargin = (std::max)(cfg.footerMarginRatio, 0.18f);

    // ------------------------------------------------------------------------
    // 第 0 重保护：标题保护与注解/脚注保护 (绝对不作为页眉/页脚/页码过滤)
    // ------------------------------------------------------------------------
    if (elem.type == LayoutElementType::Title || 
        elem.labelName == "doc_title" || 
        elem.labelName == "paragraph_title") {
        return false;
    }

    if (cfg.preserveFootnotes && (elem.labelName == "footnote" || elem.labelName == "vision_footnote")) {
        return false;
    }

    // ------------------------------------------------------------------------
    // 第 1 重过滤：模型显式语义标签判定 (结合上下半区软边界防护)
    // ------------------------------------------------------------------------
    if (cfg.filterHeader && (elem.type == LayoutElementType::Header || 
                             elem.labelName == "header" || 
                             elem.labelName == "header_image")) {
        // 防御性校验：页眉必须位于页面上半部分 (y1 在前 30% 范围内)，防止模型偶发中心区域分类漂移
        if (topRatio <= 0.30f) {
            LOG_DEBUG("DocLayoutEngine", 
                wxString::Format("Filter: dropped model-classified header '%s' at x=[%d, %d], y=[%d, %d]", 
                                 elem.labelName.c_str(), elem.x1, elem.x2, elem.y1, elem.y2).ToStdString());
            return true;
        }
    }

    if (cfg.filterFooter && (elem.type == LayoutElementType::Footer || 
                             elem.labelName == "footer" || 
                             elem.labelName == "footer_image")) {
        // 防御性校验：页脚必须位于页面下半部分 (y2 在后 30% 范围内)
        if (bottomRatio >= 0.70f) {
            LOG_DEBUG("DocLayoutEngine", 
                wxString::Format("Filter: dropped model-classified footer '%s' at y=[%d, %d]", 
                                 elem.labelName.c_str(), elem.y1, elem.y2).ToStdString());
            return true;
        }
    }

    // 独立页码标签 (number)
    // 过滤页眉区 (<= effectiveHeaderMargin 或 <= 20%) 或页脚区 (>= 1.0 - effectiveFooterMargin 或 >= 80%) 的独立页码
    if (cfg.filterPageNumber && elem.labelName == "number") {
        const float headerThresh = (std::max)(effectiveHeaderMargin, 0.20f);
        const float footerThresh = (std::max)(effectiveFooterMargin, 0.20f);
        if (topRatio <= headerThresh || bottomRatio >= (1.0f - footerThresh)) {
            LOG_DEBUG("DocLayoutEngine", 
                wxString::Format("Filter: dropped page number '%s' at x=[%d, %d], y=[%d, %d]", 
                                 elem.labelName.c_str(), elem.x1, elem.x2, elem.y1, elem.y2).ToStdString());
            return true;
        }
    }

    // ------------------------------------------------------------------------
    // 第 2 重过滤：几何空间坐标兜底 (针对被模型误判为 Text 或 Title 的漏检页眉页脚及边角页码)
    // ------------------------------------------------------------------------
    if (cfg.enableGeometricFallback) {
        // 仅对普通文本类元素做几何兜底 (标题、表格、公式、图表等复杂结构不在此激进剔除)
        bool isTextLike = (elem.type == LayoutElementType::Text || 
                           elem.labelName == "aside_text" || 
                           elem.labelName == "content");

        if (isTextLike) {
            // A. 页眉/页脚边角与居中独立页码兜底过滤 (Header / Footer Corner & Center Page Numbers)
            // 典型特征：真实页码框非常狭小 (通常单数字或罗马数字，宽度不超过页面 8%，高度不超过 4%)
            // - 边角独立页码：位于最边缘极角区 (靠左 x2 <= 20%W 或靠右 x1 >= 80%W)
            // - 底部居中独立页码：居中 (x1 >= 30%W 且 x2 <= 70%W 且 boxWidthRatio <= 8%W)。
            // 注意：顶部区域 (尤其是 8%~18% 区域) 以及文本列起始处 (如 x1 >= 10%W) 通常为篇章标题 (如"总 序"、"目 录 Contents") 或章节条目，绝不能作为页码过滤！
            bool isCornerPageNum = (rightRatio <= 0.20f || leftRatio >= 0.80f);
            bool isBottomCenterPageNum = (topRatio >= (1.0f - effectiveFooterMargin) &&
                                          leftRatio >= 0.30f && rightRatio <= 0.70f && boxWidthRatio <= 0.08f);
            bool isSmallBox = (boxHeightRatio <= 0.04f && boxWidthRatio <= 0.08f);

            if (cfg.filterPageNumber && isSmallBox && (isCornerPageNum || isBottomCenterPageNum)) {
                if (bottomRatio <= 0.14f || topRatio >= (1.0f - effectiveFooterMargin)) {
                    LOG_DEBUG("DocLayoutEngine", 
                        wxString::Format("Geometric fallback: dropped corner/center page number at x=[%d, %d], y=[%d, %d]", 
                                         elem.x1, elem.x2, elem.y1, elem.y2).ToStdString());
                    return true;
                }
            }
        }

        // B. 顶部页眉兜底：针对极靠近页面顶缘的单行短文本或误判标题 (bottomRatio <= 0.065f)
        // 典型特征：高度极低 (boxHeightRatio <= 0.04f)，位于极顶部敏感区内
        // 注意：若模型将其误判为 text、content、aside_text 甚至 paragraph_title (常见于以书名/篇名作为页眉的图书，如《数学之美》)，在此一律作为页眉过滤。
        // 避开真正的主文档大标题 doc_title (如报纸顶栏报头，通常宽度 > 50% 页面宽度)。
        // 避开位于 8%~20% 范围的章节目录大标题 (如"总 序"、"目录 Contents"，高度与底距均大于此门限)。
        bool isHeaderCandidate = (isTextLike || elem.labelName == "paragraph_title");
        if (cfg.filterHeader && 
            isHeaderCandidate &&
            elem.labelName != "doc_title" && 
            bottomRatio <= 0.065f && 
            boxHeightRatio <= 0.04f && 
            boxWidthRatio <= 0.60f) {
            LOG_DEBUG("DocLayoutEngine", 
                wxString::Format("Geometric fallback: dropped misclassified header '%s' at x=[%d, %d], y=[%d, %d]", 
                                 elem.labelName.c_str(), elem.x1, elem.x2, elem.y1, elem.y2).ToStdString());
            return true;
        }

        if (isTextLike) {
            // C. 底部页脚兜底：仅针对极靠近页面底缘的漏检单行页脚 (topRatio >= 0.92f)，避开正文末尾与目录条目
            // 注意：80%~92% 范围通常是正文最后几行、目录末尾小项 (如"水利 ...... 071") 或底部注解，绝不能粗暴误删！
            const float strictFooterThreshold = (std::max)(1.0f - effectiveFooterMargin, 0.92f);
            if (cfg.filterFooter && elem.labelName != "footnote" && elem.labelName != "vision_footnote" &&
                topRatio >= strictFooterThreshold) {
                bool shouldDrop = false;
                if (cfg.preserveFootnotes) {
                    // 仅当宽度很小 (<= 20% 页面宽度) 时才作为无用页脚过滤；宽文本框放行给 OCR 提取注解
                    shouldDrop = (boxHeightRatio <= cfg.maxFooterHeightRatio && boxWidthRatio <= 0.20f);
                } else {
                    shouldDrop = (boxHeightRatio <= cfg.maxFooterHeightRatio);
                }
                if (shouldDrop) {
                    LOG_DEBUG("DocLayoutEngine", 
                        wxString::Format("Geometric fallback: dropped misclassified footer at y=[%d, %d]", 
                                         elem.y1, elem.y2).ToStdString());
                    return true;
                }
            }
        }
    }

    return false;
}

void DocLayoutEngine::SuppressContainedOrDuplicateBoxes(std::vector<LayoutElement>& elements, const DocLayoutFilterConfig& cfg) {
    if (elements.size() <= 1) return;

    std::vector<bool> suppressed(elements.size(), false);

    for (size_t i = 0; i < elements.size(); ++i) {
        if (suppressed[i]) continue;
        auto& boxA = elements[i];
        int areaA = (boxA.x2 - boxA.x1) * (boxA.y2 - boxA.y1);
        if (areaA <= 0) { suppressed[i] = true; continue; }

        for (size_t j = i + 1; j < elements.size(); ++j) {
            if (suppressed[j]) continue;
            auto& boxB = elements[j];
            int areaB = (boxB.x2 - boxB.x1) * (boxB.y2 - boxB.y1);
            if (areaB <= 0) { suppressed[j] = true; continue; }

            int interX1 = (std::max)(boxA.x1, boxB.x1);
            int interY1 = (std::max)(boxA.y1, boxB.y1);
            int interX2 = (std::min)(boxA.x2, boxB.x2);
            int interY2 = (std::min)(boxA.y2, boxB.y2);

            int interW = (std::max)(0, interX2 - interX1);
            int interH = (std::max)(0, interY2 - interY1);
            int interArea = interW * interH;
            if (interArea <= 0) continue;

            float minArea = static_cast<float>((std::min)(areaA, areaB));
            float unionArea = static_cast<float>(areaA + areaB - interArea);
            float ios = static_cast<float>(interArea) / minArea;
            float iou = (unionArea > 0) ? (static_cast<float>(interArea) / unionArea) : 0.0f;

            // A. 处理落在 Image / Chart 区域内部的文字、公式或切片碎片 (IoS >= 0.80)，非独立图注/脚注
            bool isGraphicA = (boxA.type == LayoutElementType::Image || boxA.type == LayoutElementType::Chart || boxA.labelName == "image" || boxA.labelName == "chart");
            bool isGraphicB = (boxB.type == LayoutElementType::Image || boxB.type == LayoutElementType::Chart || boxB.labelName == "image" || boxB.labelName == "chart");
            if (isGraphicA && !isGraphicB && areaA >= areaB &&
                boxB.labelName != "vision_footnote" && boxB.labelName != "footnote" && boxB.labelName != "figure_title") {
                if (ios >= 0.80f) {
                    suppressed[j] = true;
                    continue;
                }
            } else if (isGraphicB && !isGraphicA && areaB >= areaA &&
                boxA.labelName != "vision_footnote" && boxA.labelName != "footnote" && boxA.labelName != "figure_title") {
                if (ios >= 0.80f) {
                    suppressed[i] = true;
                    break;
                }
            }

            bool sameLabel = (boxA.labelName == boxB.labelName);
            bool bothTextLike = (boxA.type == LayoutElementType::Text || boxA.type == LayoutElementType::Title) &&
                                (boxB.type == LayoutElementType::Text || boxB.type == LayoutElementType::Title);

            bool isFormulaA = (boxA.type == LayoutElementType::Formula || boxA.labelName == "inline_formula" || boxA.labelName == "display_formula" || boxA.labelName == "formula");
            bool isFormulaB = (boxB.type == LayoutElementType::Formula || boxB.labelName == "inline_formula" || boxB.labelName == "display_formula" || boxB.labelName == "formula");
            bool textContainsFormula = 
                (areaA >= areaB && (boxA.type == LayoutElementType::Text || boxA.type == LayoutElementType::Title) && isFormulaB) ||
                (areaB >= areaA && (boxB.type == LayoutElementType::Text || boxB.type == LayoutElementType::Title) && isFormulaA);

            bool isTableA = (boxA.type == LayoutElementType::Table || boxA.labelName == "table");
            bool isTableB = (boxB.type == LayoutElementType::Table || boxB.labelName == "table");
            bool tableContainsSubBox = 
                (areaA >= areaB && isTableA && !isTableB &&
                 boxB.labelName != "vision_footnote" && boxB.labelName != "footnote" && boxB.labelName != "figure_title") ||
                (areaB >= areaA && isTableB && !isTableA &&
                 boxA.labelName != "vision_footnote" && boxA.labelName != "footnote" && boxA.labelName != "figure_title");

            // B. 包含性抑制 (IoS >= cfg.containmentIosThreshold)
            // 支持同类合并、文本类元素包含合并、段落文本包含行内/块级公式、以及表格包含内部公式/文本/数字子块的抑制
            if (ios >= cfg.containmentIosThreshold && (sameLabel || bothTextLike || textContainsFormula || tableContainsSubBox)) {
                // 如果两框面积高度接近 (IoU >= 0.70)，按结构特异性或置信度择优
                if (iou >= 0.70f) {
                    if (isTableA && !isTableB) {
                        suppressed[j] = true;
                    } else if (isTableB && !isTableA) {
                        suppressed[i] = true;
                        break;
                    } else if (boxA.score >= boxB.score) {
                        suppressed[j] = true;
                    } else {
                        suppressed[i] = true;
                        break;
                    }
                    continue;
                }

                size_t largeIdx = (areaA >= areaB) ? i : j;
                size_t smallIdx = (areaA >= areaB) ? j : i;
                auto& largeBox = elements[largeIdx];
                auto& smallBox = elements[smallIdx];

                // 吸收合并外包络，保留大块，抑制小切片
                largeBox.x1 = (std::min)(largeBox.x1, smallBox.x1);
                largeBox.y1 = (std::min)(largeBox.y1, smallBox.y1);
                largeBox.x2 = (std::max)(largeBox.x2, smallBox.x2);
                largeBox.y2 = (std::max)(largeBox.y2, smallBox.y2);
                largeBox.score = (std::max)(largeBox.score, smallBox.score);
                if (largeBox.readingOrder < 0 && smallBox.readingOrder >= 0) {
                    largeBox.readingOrder = smallBox.readingOrder;
                }

                LOG_DEBUG("DocLayoutEngine", 
                    wxString::Format("Suppressed nested sub-box [%s score=%.2f] into enclosing block [%s score=%.2f], IoS=%.2f",
                        smallBox.labelName.c_str(), smallBox.score,
                        largeBox.labelName.c_str(), largeBox.score, ios).ToStdString());

                suppressed[smallIdx] = true;
                if (smallIdx == i) {
                    break; // boxA 已被抑制，退出内循环
                }
                continue;
            }

            // C. 同类别的重影检测框抑制 (IoU >= cfg.overlapIouThreshold)
            if (iou >= cfg.overlapIouThreshold && sameLabel) {
                if (boxA.score >= boxB.score) {
                    suppressed[j] = true;
                } else {
                    suppressed[i] = true;
                    break;
                }
            }
        }
    }

    std::vector<LayoutElement> filtered;
    filtered.reserve(elements.size());
    for (size_t i = 0; i < elements.size(); ++i) {
        if (!suppressed[i]) {
            filtered.push_back(elements[i]);
        }
    }
    elements = std::move(filtered);
}

bool DocLayoutEngine::IsImageContentEmpty(const wxImage& img) {
    if (!img.IsOk()) return true;
    int w = img.GetWidth();
    int h = img.GetHeight();
    if (w <= 0 || h <= 0) return true;
    const unsigned char* data = img.GetData();
    if (!data) return true;

    // 1. 采样四角及边缘中点估算背景基准色
    std::vector<std::pair<int, int>> samplePoints = {
        {(std::min)(5, w - 1), (std::min)(5, h - 1)},
        {(std::max)(0, w - 6), (std::min)(5, h - 1)},
        {(std::min)(5, w - 1), (std::max)(0, h - 6)},
        {(std::max)(0, w - 6), (std::max)(0, h - 6)},
        {w / 2, (std::min)(5, h - 1)},
        {w / 2, (std::max)(0, h - 6)},
        {(std::min)(5, w - 1), h / 2},
        {(std::max)(0, w - 6), h / 2}
    };

    int sumR = 0, sumG = 0, sumB = 0;
    for (const auto& pt : samplePoints) {
        size_t idx = (static_cast<size_t>(pt.second) * w + pt.first) * 3;
        sumR += data[idx];
        sumG += data[idx + 1];
        sumB += data[idx + 2];
    }
    int bgR = sumR / static_cast<int>(samplePoints.size());
    int bgG = sumG / static_cast<int>(samplePoints.size());
    int bgB = sumB / static_cast<int>(samplePoints.size());
    int bgGray = (bgR * 299 + bgG * 587 + bgB * 114) / 1000;

    // 2. 步长采样统计前景有效笔墨像素
    const int step = 2;
    int fgCount = 0;
    int totalSampled = 0;
    for (int y = 0; y < h; y += step) {
        size_t rowOffset = static_cast<size_t>(y) * w * 3;
        for (int x = 0; x < w; x += step) {
            totalSampled++;
            size_t idx = rowOffset + static_cast<size_t>(x) * 3;
            int r = data[idx];
            int g = data[idx + 1];
            int b = data[idx + 2];

            int diff = std::abs(r - bgR) + std::abs(g - bgG) + std::abs(b - bgB);
            int gray = (r * 299 + g * 587 + b * 114) / 1000;

            bool isForeground = false;
            if (bgGray >= 128) {
                if (diff > 40 && gray < 225) {
                    isForeground = true;
                }
            } else {
                if (diff > 40 && gray > 50) {
                    isForeground = true;
                }
            }

            if (isForeground) {
                fgCount++;
            }
        }
    }

    if (totalSampled == 0) return true;
    double fgRatio = static_cast<double>(fgCount) / static_cast<double>(totalSampled);

    // 对于极小尺寸子图 (<500 个采样点)，要求至多 2 个笔墨点或占比低于 1%
    if (totalSampled < 500) {
        return (fgCount <= 2 || fgRatio < 0.01);
    }
    // 对于整页大图，要求笔墨像素少于 60 个或占比低于 0.02% (排除偶发细微噪点或扫描仪边框污渍)
    return (fgCount < 60 || fgRatio < 0.0002);
}

bool DocLayoutEngine::AnalyzeLayout(const std::string& imagePath, DocumentLayoutResult& outResult, const DocLayoutFilterConfig& filterConfig) {
    if (!wxFileExists(wxString::FromUTF8(imagePath))) {
        m_lastError = "目标图像文件不存在: " + imagePath;
        return false;
    }

    wxLogNull noLog;
    wxImage img;
    if (!img.LoadFile(wxString::FromUTF8(imagePath))) {
        m_lastError = "无法读取目标图像: " + imagePath;
        return false;
    }

    return AnalyzeLayout(img, outResult, filterConfig);
}

bool DocLayoutEngine::AnalyzeLayout(const wxImage& img, DocumentLayoutResult& outResult, const DocLayoutFilterConfig& filterConfig) {
    if (!img.IsOk()) {
        m_lastError = "目标图像数据无效";
        return false;
    }

    int origW = img.GetWidth();
    int origH = img.GetHeight();
    outResult.imageWidth = origW;
    outResult.imageHeight = origH;
    outResult.elements.clear();

    if (!IsLoaded()) {
        // 启发式兜底模式：若用户尚未配置或加载 ONNX 版面模型，将整页作为完整文本块进行单阶段/全页 OCR
        LayoutElement elem;
        elem.id = 1;
        elem.type = LayoutElementType::Text;
        elem.labelName = "text";
        elem.score = 1.0f;
        elem.x1 = 0;
        elem.y1 = 0;
        elem.x2 = origW;
        elem.y2 = origH;
        elem.readingOrder = 1;
        outResult.elements.push_back(elem);
        return true;
    }

    // ONNX Runtime 推理阶段
    std::lock_guard<std::mutex> lock(m_impl->sessionMutex);
    try {
        const int targetSize = 800;
        wxImage scaledImg = img.Scale(targetSize, targetSize, wxIMAGE_QUALITY_HIGH);

        // Preprocess: NCHW ImageNet 归一化
        std::vector<float> inputValues(1 * 3 * targetSize * targetSize);
        const unsigned char* rgbData = scaledImg.GetData();
        if (!rgbData) {
            m_lastError = "图像像素数据为空";
            return false;
        }

        const float mean[3] = {0.485f, 0.456f, 0.406f};
        const float std[3]  = {0.229f, 0.224f, 0.225f};

        int channelStride = targetSize * targetSize;
        for (int y = 0; y < targetSize; ++y) {
            for (int x = 0; x < targetSize; ++x) {
                int pixelIdx = (y * targetSize + x) * 3;
                int spatialIdx = y * targetSize + x;
                for (int c = 0; c < 3; ++c) {
                    float normVal = (static_cast<float>(rgbData[pixelIdx + c]) / 255.0f - mean[c]) / std[c];
                    inputValues[c * channelStride + spatialIdx] = normVal;
                }
            }
        }

        Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<int64_t> imgShape = {1, 3, targetSize, targetSize};
        Ort::Value imgTensor = Ort::Value::CreateTensor<float>(
            memInfo, inputValues.data(), inputValues.size(), imgShape.data(), imgShape.size()
        );

        std::vector<const char*> inputNamesC;
        std::vector<Ort::Value> inputTensors;

        // 根据 ONNX 模型导出的输入签名匹配输入张量
        std::vector<float> scaleValues = { static_cast<float>(targetSize) / origH, static_cast<float>(targetSize) / origW };
        std::vector<int64_t> scaleShape = {1, 2};
        Ort::Value scaleTensor = Ort::Value::CreateTensor<float>(
            memInfo, scaleValues.data(), scaleValues.size(), scaleShape.data(), scaleShape.size()
        );

        std::vector<float> imShapeValues = { static_cast<float>(targetSize), static_cast<float>(targetSize) };
        std::vector<int64_t> imShapeDims = {1, 2};
        Ort::Value imShapeTensor = Ort::Value::CreateTensor<float>(
            memInfo, imShapeValues.data(), imShapeValues.size(), imShapeDims.data(), imShapeDims.size()
        );

        for (const auto& name : m_impl->inputNames) {
            inputNamesC.push_back(name.c_str());
            if (name == "image" || name == "pixel_values" || name == "x") {
                inputTensors.push_back(std::move(imgTensor));
            } else if (name == "scale_factor" || name == "scale_factors") {
                inputTensors.push_back(std::move(scaleTensor));
            } else if (name == "im_shape" || name == "preprocess_shape") {
                inputTensors.push_back(std::move(imShapeTensor));
            } else {
                inputTensors.push_back(Ort::Value::CreateTensor<float>(
                    memInfo, scaleValues.data(), scaleValues.size(), scaleShape.data(), scaleShape.size()
                ));
            }
        }

        // 输出张量签名
        std::vector<const char*> outputNamesC;
        for (const auto& name : m_impl->outputNames) {
            outputNamesC.push_back(name.c_str());
        }

        auto outputTensors = m_impl->session->Run(
            Ort::RunOptions{nullptr},
            inputNamesC.data(),
            inputTensors.data(),
            inputTensors.size(),
            outputNamesC.data(),
            outputNamesC.size()
        );

        // 解析检测框
        if (!outputTensors.empty()) {
            auto& out = outputTensors[0];
            auto typeInfo = out.GetTensorTypeAndShapeInfo();
            auto shape = typeInfo.GetShape();
            const float* outData = out.GetTensorData<float>();

            if (shape.size() >= 2 && shape.back() >= 6) {
                int64_t numBoxes = shape[0];
                if (shape.size() == 3) numBoxes = shape[1];
                int64_t featDim = shape.back();

                int elemId = 1;
                std::vector<LayoutElement> rawCandidates;
                std::vector<LayoutElement> filteredHeaderFooterBoxes;
                for (int64_t i = 0; i < numBoxes; ++i) {
                    const float* box = outData + i * featDim;
                    
                    int classId = 0;
                    float score = 0.0f;
                    float xmin = 0.0f, ymin = 0.0f, xmax = 0.0f, ymax = 0.0f;
                    int readOrder = -1;

                    if (featDim >= 8) {
                        // Batched 格式: [img_idx, class_id, score, x0, y0, x1, y1, read_order]
                        classId = static_cast<int>(box[1]);
                        score = box[2];
                        xmin = box[3]; ymin = box[4]; xmax = box[5]; ymax = box[6];
                        readOrder = static_cast<int>(box[7]);
                    } else if (featDim == 7) {
                        // 单图格式: [class_id, score, x0, y0, x1, y1, read_order]
                        classId = static_cast<int>(box[0]);
                        score = box[1];
                        xmin = box[2]; ymin = box[3]; xmax = box[4]; ymax = box[5];
                        readOrder = static_cast<int>(box[6]);
                    } else {
                        // 经典 6 列格式: [class_id, score, x0, y0, x1, y1]
                        classId = static_cast<int>(box[0]);
                        score = box[1];
                        xmin = box[2]; ymin = box[3]; xmax = box[4]; ymax = box[5];
                    }

                    if (score < filterConfig.scoreThreshold) continue; // 置信度阈值过滤

                    // 坐标换算：PP-DocLayout 输入已绑定 scale_factor，因此输出坐标已经是原图真实像素坐标
                    int x1 = (xmin <= 1.0f && xmax <= 1.0f) ? static_cast<int>(xmin * origW) : static_cast<int>(xmin);
                    int y1 = (ymin <= 1.0f && ymax <= 1.0f) ? static_cast<int>(ymin * origH) : static_cast<int>(ymin);
                    int x2 = (xmin <= 1.0f && xmax <= 1.0f) ? static_cast<int>(xmax * origW) : static_cast<int>(xmax);
                    int y2 = (ymin <= 1.0f && ymax <= 1.0f) ? static_cast<int>(ymax * origH) : static_cast<int>(ymax);

                    x1 = (std::clamp)(x1, 0, origW);
                    y1 = (std::clamp)(y1, 0, origH);
                    x2 = (std::clamp)(x2, 0, origW);
                    y2 = (std::clamp)(y2, 0, origH);

                    if (x2 - x1 < 8 || y2 - y1 < 8) continue; // 过滤无意义小杂框

                    LayoutElement elem;
                    elem.id = elemId++;
                    elem.score = score;
                    elem.x1 = x1;
                    elem.y1 = y1;
                    elem.x2 = x2;
                    elem.y2 = y2;
                    elem.readingOrder = readOrder;

                    // PP-DocLayoutV3 官方 25 类别精确映射
                    auto [mappedType, mappedLabel] = MapDocLayoutClass(classId);
                    elem.type = mappedType;
                    elem.labelName = mappedLabel;

                    LOG_DEBUG("DocLayoutEngine", 
                        wxString::Format("Candidate box %d: %s (score=%.4f) bbox=[%d, %d, %d, %d]", 
                                         elem.id, elem.labelName.c_str(), elem.score, elem.x1, elem.y1, elem.x2, elem.y2).ToStdString());

                    rawCandidates.push_back(elem);
                    if (ShouldFilterElement(elem, origW, origH, filterConfig)) {
                        filteredHeaderFooterBoxes.push_back(elem);
                    }
                }

                for (const auto& elem : rawCandidates) {
                    if (ShouldFilterElement(elem, origW, origH, filterConfig)) {
                        continue;
                    }

                    // 重影抑制：若当前候选框与已被模型标签或几何过滤的页眉/页脚/页码候选框高度重叠 (IoU >= 0.50 或 IoS >= 0.70)，
                    // 说明是模型多类别输出导致的同区域重影副标签（例如同一页眉同时输出了 header 与 paragraph_title），予以同步过滤
                    bool isDuplicateOfFiltered = false;
                    int areaA = (elem.x2 - elem.x1) * (elem.y2 - elem.y1);
                    for (const auto& hfBox : filteredHeaderFooterBoxes) {
                        int areaB = (hfBox.x2 - hfBox.x1) * (hfBox.y2 - hfBox.y1);
                        int ix1 = (std::max)(elem.x1, hfBox.x1);
                        int iy1 = (std::max)(elem.y1, hfBox.y1);
                        int ix2 = (std::min)(elem.x2, hfBox.x2);
                        int iy2 = (std::min)(elem.y2, hfBox.y2);
                        int iw = (std::max)(0, ix2 - ix1);
                        int ih = (std::max)(0, iy2 - iy1);
                        int interArea = iw * ih;
                        if (interArea > 0 && areaA > 0 && areaB > 0) {
                            float ios = static_cast<float>(interArea) / (std::min)(areaA, areaB);
                            float iou = static_cast<float>(interArea) / static_cast<float>(areaA + areaB - interArea);
                            if (iou >= 0.50f || ios >= 0.70f) {
                                isDuplicateOfFiltered = true;
                                LOG_DEBUG("DocLayoutEngine", 
                                    wxString::Format("Filter: dropped duplicate of filtered header/footer [%s score=%.2f] matching [%s score=%.2f], IoU=%.2f, IoS=%.2f",
                                                     elem.labelName.c_str(), elem.score, hfBox.labelName.c_str(), hfBox.score, iou, ios).ToStdString());
                                break;
                            }
                        }
                    }

                    if (isDuplicateOfFiltered) {
                        continue;
                    }

                    outResult.elements.push_back(elem);
                }
            }
        }

        // 1. 包含性重叠与子框去重抑制 (对齐 PaddleX merge_layout_blocks 与 IoS 规范)
        if (filterConfig.mergeLayoutBlocks) {
            SuppressContainedOrDuplicateBoxes(outResult.elements, filterConfig);
        }

        // 2. 优先读取模型原生预测的 read_order (端到端注意力/Pointer Network 拓扑流)
        bool hasModelReadOrder = false;
        int firstValidOrder = -1;
        for (const auto& elem : outResult.elements) {
            if (elem.readingOrder >= 0) {
                if (firstValidOrder == -1) {
                    firstValidOrder = elem.readingOrder;
                } else if (elem.readingOrder != firstValidOrder) {
                    hasModelReadOrder = true;
                    break;
                }
            }
        }

        if (hasModelReadOrder) {
            LOG_DEBUG("DocLayoutEngine", "Sorting layout elements using model-predicted reading order");
            std::sort(outResult.elements.begin(), outResult.elements.end(), 
                [](const LayoutElement& a, const LayoutElement& b) {
                    if (a.readingOrder != b.readingOrder) {
                        return a.readingOrder < b.readingOrder;
                    }
                    if (std::abs(a.y1 - b.y1) > 8) return a.y1 < b.y1;
                    return a.x1 < b.x1;
                });
        } else {
            // 2. 空间分栏拓扑排序兜底 (对齐 PaddleX / XY-Cut++ 规范，自适应双栏与通栏)
            LOG_DEBUG("DocLayoutEngine", "Sorting layout elements using spatial column-aware topological sort fallback");
            SortLayoutReadingOrderRobust(outResult.elements, origW, origH);
        }

        // 3. 重新编排紧凑连续的阅读序号与 ID (1, 2, 3...)
        for (size_t i = 0; i < outResult.elements.size(); ++i) {
            outResult.elements[i].id = static_cast<int>(i + 1);
            outResult.elements[i].readingOrder = static_cast<int>(i + 1);
        }

        if (outResult.elements.empty() && !IsImageContentEmpty(img)) {
            LayoutElement elem;
            elem.id = 1;
            elem.type = LayoutElementType::Text;
            elem.labelName = "text";
            elem.score = 1.0f;
            elem.x1 = 0; elem.y1 = 0; elem.x2 = origW; elem.y2 = origH;
            elem.readingOrder = 1;
            outResult.elements.push_back(elem);
        }

        return true;
    } catch (const Ort::Exception& e) {
        m_lastError = std::string("ONNX 推理异常: ") + e.what();
        LOG_ERROR("DocLayoutEngine", m_lastError);
        return false;
    } catch (const std::exception& e) {
        m_lastError = std::string("推理异常: ") + e.what();
        LOG_ERROR("DocLayoutEngine", m_lastError);
        return false;
    }
}

} // namespace LinguaAlpaca
