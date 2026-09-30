#pragma once
#pragma execution_character_set("utf-8")

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "core/Types.hpp"

namespace LinguaAlpaca {

/**
 * @brief 文档版面分析推理引擎 (基于 ONNX Runtime C++ API 与 PP-DocLayoutV2 权重)
 * 
 * 采用 PIMPL 设计模式隔离 ONNX Runtime 底层头文件，避免符号与宏泄漏。
 * 负责解析整页 PDF 光栅化图像中的文本、表格、公式、图像等区域并建立拓扑阅读顺序。
 */
class DocLayoutEngine {
public:
    DocLayoutEngine();
    ~DocLayoutEngine();

    DocLayoutEngine(const DocLayoutEngine&) = delete;
    DocLayoutEngine& operator=(const DocLayoutEngine&) = delete;

    // 校验并装载 ONNX 目标检测模型权重
    bool Initialize(const std::string& modelPath, int executionProvider = 0, int threads = 0);
    bool IsLoaded() const;
    void Unload();

    // 路径解析工具：支持直接传入 .onnx 文件或包含版面模型的目录
    static std::string ResolveLayoutModelPath(const std::string& path);

    const std::string& GetModelPath() const { return m_modelPath; }
    const std::string& GetLastError() const { return m_lastError; }
    int GetExecutionProvider() const { return m_executionProvider; }

    // 版面过滤与阅读流配置管理 (对齐 PaddleX 规范)
    void SetFilterConfig(const DocLayoutFilterConfig& config) { m_filterConfig = config; }
    const DocLayoutFilterConfig& GetFilterConfig() const { return m_filterConfig; }

    // 对整页文档图像进行版面分析与元素定位 (支持自定义过滤参数，默认使用引擎内嵌配置)
    bool AnalyzeLayout(const std::string& imagePath, DocumentLayoutResult& outResult, const DocLayoutFilterConfig& filterConfig);
    bool AnalyzeLayout(const std::string& imagePath, DocumentLayoutResult& outResult) {
        return AnalyzeLayout(imagePath, outResult, m_filterConfig);
    }

private:
    std::string m_modelPath;
    std::string m_lastError;
    int m_executionProvider{0};
    int m_threads{0};
    bool m_isLoaded{false};
    DocLayoutFilterConfig m_filterConfig;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LinguaAlpaca
