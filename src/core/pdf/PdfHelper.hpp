#pragma once
#pragma execution_character_set("utf-8")

#include <string>
#include <vector>
#include <wx/image.h>

namespace LinguaAlpaca {

/**
 * @brief PDF 页面矩形区域定义 (对应光栅化图像像素坐标系，原点在左上角)
 */
struct PdfBoxRegion {
    int x1{0};
    int y1{0};
    int x2{0};
    int y2{0};
};

/**
 * @brief PDF 局部矩形区域原生文本提取结果
 */
struct PdfBoxTextResult {
    bool success{false};          // 是否成功提取到有效原生文本（非空、无编码损坏、无未转换数学专用字体）
    bool hasFormulaFont{false};   // 框内是否检测到数学公式/符号专用字体或数学运算符（需交由 OCR 还原 LaTeX）
    bool hasGarbledChar{false};   // 框内是否存在 ToUnicode 映射错误或私有区乱码字符（需交由 OCR 识别）
    std::string text;             // 经过自然段折行合并与连字符修复后的 UTF-8 文本
};

/**
 * @brief PDF 与文档光栅化及原生文本提取辅助类
 * 
 * 核心设计目标：
 * 1. 严格防止内存溢出 (OOM)：支持分批/按页光栅化，绝不将整个长篇 PDF 一次性读入显存/内存；
 * 2. 基于工业级 Google PDFium 跨平台高性能渲染与文本引擎；
 * 3. 100% 容错兼容各类历史古籍扫描件、JBIG2/JPEG2000 复合掩模及复杂矢量排版；
 * 4. 支持按版面元素包围盒毫秒级提取内嵌文本，并内置乱码与数学字体自动识别门禁；
 * 5. 自动管理单页光栅化位图与临时文件生命周期。
 */
class PdfHelper {
public:
    // 检查文件是否为 PDF 文档
    static bool IsPdfFile(const std::string& filePath);

    // 获取 PDF 总页数 (若是普通图片则返回 1，若非法文件则返回 0)
    static int GetPageCount(const std::string& filePath);

    // 按页光栅化单页 PDF 为 wxImage (只在内存保留当前页，目标宽度如 1600px 保证 OCR 精度)
    static bool RenderPage(const std::string& filePath, int pageIndex, wxImage& outImage, int targetWidth = 1600);

    // 将单页直接光栅化保存为临时 PNG 图像文件并返回临时路径（若输入已是普通图片，则直接返回原文件路径，调用方绝不可将其作为临时文件删除）
    static std::string RenderPageToTempFile(const std::string& filePath, int pageIndex, const std::string& tempDir, int targetWidth = 1600);

    // 批量按版面元素像素包围盒提取单页 PDF 原生内嵌文本（内置字体编码与数学符号门禁校验）
    static bool ExtractPageBoxesText(
        const std::string& filePath,
        int pageIndex,
        int renderedWidth,
        int renderedHeight,
        const std::vector<PdfBoxRegion>& boxes,
        std::vector<PdfBoxTextResult>& outResults
    );

    // 判定 PDF 字体名称是否属于数学公式或特殊符号字体 (如 CMMI10, CMSY10, CMEX10, CambriaMath, Symbol 等)
    static bool IsMathOrSymbolFontName(std::string_view fontName);

    // 对单个版面框内提取出的 PDF 多行原生文本执行自然段折行合并（处理英文断词连字符、CJK 无缝拼接、保留列表项换行）
    static std::string UnwrapPdfParagraphLines(std::string_view rawText);

    // 清理已缓存的 PDF 文档对象（释放文件句柄与内存）
    static void ClearCache();
};

} // namespace LinguaAlpaca
