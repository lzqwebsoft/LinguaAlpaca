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
 * 2. 阶段 1：DocLayoutEngine (PP-DocLayoutV2 ONNX Runtime) 进行目标定位与阅读顺序拓扑排序；
 * 3. 阶段 2：裁剪子图并通过 PaddleOCR-VL (llama-server VLM) 针对性推理表格、公式、标题及正文；
 * 4. 阶段 3：restructure_pages 页面重构，支持 Markdown/JSON 结构化自动生成与落盘；
 * 5. 英文文档支持自动触发 Hy-MT2 模型流式翻译为中文。
 */
class DocumentPipeline : public std::enable_shared_from_this<DocumentPipeline> {
public:
    DocumentPipeline(
        std::shared_ptr<ModelManager> modelManager,
        std::shared_ptr<DocLayoutEngine> layoutEngine
    );
    ~DocumentPipeline();

    // 启动解析任务 (在后台异步线程运行)
    void StartParseAsync(
        const std::string& inputFilePath,
        const std::string& outputDir,
        bool translateEnglishToChinese,
        DocProgressCallback onProgress,
        DocCompleteCallback onComplete
    );

    void Cancel();
    bool IsRunning() const { return m_isRunning.load(); }

    // 结构化落盘辅助方法
    static bool SaveToMarkdown(const std::string& saveDir, const std::string& baseName, const std::string& markdownContent);
    static bool SaveToJson(const std::string& saveDir, const std::string& baseName, const std::string& jsonContent);

    // PaddleOCR OTSL 表格转标准 HTML 表格辅助方法
    static std::string ConvertOtslToHtml(const std::string& otslStr);

private:
    std::shared_ptr<ModelManager> m_modelManager;
    std::shared_ptr<DocLayoutEngine> m_layoutEngine;

    std::shared_ptr<std::atomic<bool>> m_aliveToken;
    std::atomic<bool> m_shouldStop{false};
    std::atomic<bool> m_isRunning{false};

    mutable std::mutex m_mutex;
};

} // namespace LinguaAlpaca
