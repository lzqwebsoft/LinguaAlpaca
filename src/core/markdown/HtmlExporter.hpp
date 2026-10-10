#pragma once
#pragma execution_character_set("utf-8")

#include <string>
#include <wx/string.h>

namespace LinguaAlpaca {

/**
 * @brief 自包含单文件 HTML 导出器
 *
 * 特性：
 * - 将 Markdown 及本地截取的图片 (相对路径) 转换为 100% 独立自包含的单文件 HTML
 * - 内联嵌入 Base64 Data URL 图片，彻底摆脱 imgs/ 图片子文件夹依赖
 * - 离线内联嵌入 marked.js 解析引擎、KaTeX 矢量公式渲染器与精美 GitHub/Notion 风格 CSS
 * - 任何操作系统 (Windows/macOS/Linux/iOS/Android) 上的浏览器双击即开，零格式失真
 * - 原生支持 @media print，可从浏览器直接无损打印或另存为 PDF
 */
class HtmlExporter {
public:
    /**
     * @brief 导出 Markdown 及图片为独立单文件 HTML 网页并保存到磁盘
     * @param markdown 原始或经过版面管线处理的 Markdown 文本
     * @param baseDir 图片等相对资源的基准路径（如 outputDir，可为空）
     * @param outputPath 输出的目标 .html 文件绝对路径
     * @param title 文档标题（默认为 "OCR 解析文档"）
     * @return 导出成功返回 true，否则返回 false
     */
    static bool ExportToStandaloneHtml(
        const std::string& markdown,
        const std::string& baseDir,
        const std::string& outputPath,
        const std::string& title = "OCR 解析文档"
    );

    /**
     * @brief 生成自包含单文件 HTML 网页字符串
     * @param markdown 原始 Markdown 文本
     * @param baseDir 相对资源基准路径
     * @param title 文档标题
     * @return 完整 HTML 网页内容
     */
    static std::string GenerateStandaloneHtml(
        const std::string& markdown,
        const std::string& baseDir,
        const std::string& title = "OCR 解析文档"
    );

    /**
     * @brief 解析 Markdown 中的本地相对图片并替换为内联 Base64 Data URL
     * @param md 包含 <img src="..."> 或 ![alt](path) 的 Markdown 文本
     * @param baseDir 相对路径基准目录
     * @return 替换为 Base64 图片后的 Markdown 文本
     */
    static std::string EmbedLocalImagesAsBase64(
        const std::string& md,
        const std::string& baseDir
    );

    /**
     * @brief 加载本地图片文件并编码为 Base64 Data URL (例如 data:image/jpeg;base64,...)
     * @param fullPath 本地图片文件绝对路径
     * @return Data URL 字符串，若加载失败则返回空字符串
     */
    static std::string LoadImageFileAsDataUrl(const std::string& fullPath);

    /**
     * @brief 查找运行环境下的 resources/web 目录绝对路径
     */
    static wxString GetWebResourcesDir();
};

} // namespace LinguaAlpaca
