#pragma once
#pragma execution_character_set("utf-8")

#include <string>
#include <vector>
#include <wx/image.h>

namespace LinguaAlpaca {

/**
 * @brief PDF 与文档光栅化辅助类
 * 
 * 核心设计目标：
 * 1. 严格防止内存溢出 (OOM)：支持分批/按页光栅化，绝不将整个长篇 PDF 一次性读入显存/内存；
 * 2. Windows 平台基于系统原生 WinRT Windows.Data.Pdf API (零外部依赖，硬件加速渲染)；
 * 3. macOS 平台基于系统原生 PDFKit / CoreGraphics；
 * 4. 自动管理单页光栅化位图与临时文件生命周期。
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

    // 清理已缓存的 PDF 文档对象（释放文件句柄与内存）
    static void ClearCache();
};

} // namespace LinguaAlpaca
