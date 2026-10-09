#pragma once
#pragma execution_character_set("utf-8")

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>

#include "core/Types.hpp"
#include "core/ModelManager.hpp"
#include "engine/DocLayoutEngine.hpp"

class wxImage;

namespace LinguaAlpaca {

struct PageElementResult {
    LayoutElement element;
    std::string recognizedText;
    std::string translatedText;
    std::string figurePath; // 若为图像元素，保存的本地相对路径
};

struct PageStructResult {
    int pageIndex{0};
    std::vector<PageElementResult> elements;
    std::string pageMarkdown;
};

using DocProgressCallback = std::function<void(
    int currentPage,
    int totalPages,
    const std::string& stageDescription,
    const std::string& currentFullMarkdown
)>;

using DocCompleteCallback = std::function<void(
    bool success,
    const std::string& fullMarkdown,
    const std::string& jsonStructured,
    const std::string& error
)>;

/**
 * @brief PaddleOCR-VL 两阶段文档解析流水线服务
 * 
 * 核心特性：
 * 1. 严格防爆显存/内存：PDF 按页单向流式光栅化与处理，单页处理完即刻销毁位图与临时文件，内存恒定；
 * 2. 核心阶段流水线重叠提速 (Staged Pipeline Overlap)：当前页执行阶段 2 (VLM 视觉推理) 时，后台异步线程并发执行下一页阶段 1 (PDF 光栅化与版面分析与元素切片)，零等待衔接，完全消除页面切换开销；
 * 3. 严格断点可恢复性 (Resumption & Recoverability)：每页完成/中断时原子级实时落盘，异常或用户取消时自动清理预取临时数据，支持从任意中断页无损继续解析；
 * 4. 阶段 1：DocLayoutEngine (PP-DocLayoutV2 ONNX Runtime) 进行目标定位与阅读顺序拓扑排序；
 * 5. 阶段 2：裁剪子图并通过 PaddleOCR-VL (llama-server VLM) 针对性推理表格、公式、标题及正文；
 * 6. 阶段 3：restructure_pages 页面重构，支持 Markdown/JSON 结构化自动生成与落盘；
 * 7. 英文文档支持自动触发 Hy-MT2 模型流式翻译为中文。
 */
class DocumentPipeline : public std::enable_shared_from_this<DocumentPipeline> {
public:
    DocumentPipeline(
        std::shared_ptr<ModelManager> modelManager,
        std::shared_ptr<DocLayoutEngine> layoutEngine
    );
    ~DocumentPipeline();

    struct ResumeInfo {
        bool hasResumeData{false};
        int totalPages{0};
        int lastProcessedPage{0};   // 1-indexed: 上次处理到的最后一页 (即旧一页)
        int completedPages{0};       // 已经完全确认完成的页数 (lastProcessedPage - 1)
        bool isCompleted{false};
        std::string markdown;
        std::string jsonStructured;
    };

    // 检查历史解析落盘状态与断点数据
    static ResumeInfo CheckResumeInfo(const std::string& inputFilePath, const std::string& outputDir = "");

    // 提取指定页数前（包含第 pageCount 页）的 Markdown 内容
    static std::string ExtractMarkdownUpToPage(const std::string& fullMd, int pageCount);

    // 启动或断点恢复解析任务 (在后台异步线程运行)
    void StartParseAsync(
        const std::string& inputFilePath,
        const std::string& outputDir,
        bool translateEnglishToChinese,
        DocProgressCallback onProgress,
        DocCompleteCallback onComplete,
        int startFromPage = 0,
        const std::string& initialMarkdown = "",
        const std::string& initialJson = ""
    );

    void Cancel();
    bool IsRunning() const { return m_isRunning.load(); }

    // 结构化落盘辅助方法
    static bool SaveToMarkdown(const std::string& saveDir, const std::string& baseName, const std::string& markdownContent);
    static bool SaveToJson(const std::string& saveDir, const std::string& baseName, const std::string& jsonContent);

    // PaddleOCR OTSL 表格转标准 HTML 表格辅助方法
    static std::string ConvertOtslToHtml(const std::string& otslStr);

    // 判定位图是否为空白内容（无有效笔墨或文字符号）
    static bool IsImageContentEmpty(const wxImage& img);

    // 判定是否属于页眉/页脚区域的独立页码 (用于 Markdown 与 JSON 组装时的二次过滤防护)
    static bool IsHeaderOrFooterPageNumber(
        const std::string& text,
        const std::string& labelName,
        int x1, int y1, int x2, int y2,
        int pageW, int pageH
    );

    // 判定文本中是否存在注解数字标记 (如 LaTeX \(^{[1]}\), [1], ①, 脚注星号等)
    static bool HasAnnotationMarkers(const std::string& text);

    // 判定候选元素是否为底部注解/脚注 (结合模型标签、正文注解数字标记及文本形态综合研判)
    static bool IsFootnoteOrAnnotation(
        const std::string& text,
        const std::string& labelName,
        int y1, int y2,
        int pageH,
        bool bodyHasMarkers = false
    );

private:
    std::shared_ptr<ModelManager> m_modelManager;
    std::shared_ptr<DocLayoutEngine> m_layoutEngine;

    std::shared_ptr<std::atomic<bool>> m_aliveToken;
    std::atomic<bool> m_shouldStop{false};
    std::atomic<bool> m_isRunning{false};

    mutable std::mutex m_mutex;
};

} // namespace LinguaAlpaca
