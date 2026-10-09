#include <catch2/catch.hpp>
#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>
#include "core/pdf/PdfHelper.hpp"
#include "engine/DocLayoutEngine.hpp"
#include "core/document/DocumentPipeline.hpp"
#include "core/Config.hpp"

using namespace LinguaAlpaca;

TEST_CASE("PdfHelper - Extension and file detection", "[pdf]") {
    REQUIRE(PdfHelper::IsPdfFile("document.pdf") == true);
    REQUIRE(PdfHelper::IsPdfFile("DOCUMENT.PDF") == true);
    REQUIRE(PdfHelper::IsPdfFile("path/to/my_file.pdf") == true);
    REQUIRE(PdfHelper::IsPdfFile("image.png") == false);
    REQUIRE(PdfHelper::IsPdfFile("scan.jpg") == false);
    REQUIRE(PdfHelper::IsPdfFile("") == false);
}

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
TEST_CASE("PdfHelper - Real PDF rendering on macOS", "[pdf]") {
    wxString tempPdfPath = wxFileName::CreateTempFileName("test_pdf_") + ".pdf";
    CFStringRef pathStr = CFStringCreateWithCString(kCFAllocatorDefault, tempPdfPath.ToUTF8().data(), kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, pathStr, kCFURLPOSIXPathStyle, false);
    CFRelease(pathStr);
    CGRect mediaBox = CGRectMake(0, 0, 200, 200);
    CGContextRef pdfCtx = CGPDFContextCreateWithURL(url, &mediaBox, nullptr);
    CFRelease(url);
    REQUIRE(pdfCtx != nullptr);
    CGPDFContextBeginPage(pdfCtx, nullptr);
    CGContextSetRGBFillColor(pdfCtx, 1, 0, 0, 1);
    CGContextFillRect(pdfCtx, CGRectMake(20, 20, 160, 160));
    CGPDFContextEndPage(pdfCtx);
    CGPDFContextClose(pdfCtx);
    CGContextRelease(pdfCtx);

    REQUIRE(PdfHelper::GetPageCount(tempPdfPath.ToUTF8().data()) == 1);

    wxImage img;
    REQUIRE(PdfHelper::RenderPage(tempPdfPath.ToUTF8().data(), 0, img, 400) == true);
    REQUIRE(img.IsOk());
    REQUIRE(img.GetWidth() == 400);
    REQUIRE(img.GetHeight() == 400);

    std::string tempPng = PdfHelper::RenderPageToTempFile(tempPdfPath.ToUTF8().data(), 0, wxStandardPaths::Get().GetTempDir().ToUTF8().data(), 400);
    REQUIRE(!tempPng.empty());
    REQUIRE(wxFileExists(wxString::FromUTF8(tempPng)));

    PdfHelper::ClearCache();

    wxRemoveFile(tempPdfPath);
    wxRemoveFile(wxString::FromUTF8(tempPng));
}
#endif

TEST_CASE("DocLayoutEngine - Heuristic layout fallback", "[layout]") {
    DocLayoutEngine engine;
    REQUIRE_FALSE(engine.IsLoaded());

    DocumentLayoutResult result;
    // Analyzing non-existent image safely fails with error
    bool okNonExistent = engine.AnalyzeLayout("non_existent_file.png", result);
    REQUIRE(okNonExistent == false);

    // Analyzing real image without ONNX model triggers heuristic fallback
    wxImage dummyImg(200, 300);
    wxString tempPath = wxFileName::CreateTempFileName("test_layout_") + ".png";
    dummyImg.SaveFile(tempPath, wxBITMAP_TYPE_PNG);

    bool ok = engine.AnalyzeLayout(tempPath.ToUTF8().data(), result);
    REQUIRE(ok == true);
    REQUIRE(result.imageWidth == 200);
    REQUIRE(result.imageHeight == 300);
    REQUIRE(result.elements.size() == 1);
    REQUIRE(result.elements[0].type == LayoutElementType::Text);
    REQUIRE(result.elements[0].x2 == 200);
    REQUIRE(result.elements[0].y2 == 300);

    wxRemoveFile(tempPath);
}

TEST_CASE("DocLayoutEngine - Test with User Newspaper Image", "[layout]") {
    DocLayoutEngine engine;

    // 动态从配置、环境变量或项目默认模型路径读取，绝不硬编码机器绝对路径
    std::string modelPath;
    try {
        ConfigManager configManager;
        modelPath = configManager.GetConfig().layoutModelPath;
    } catch (...) {}

    std::string resolvedPath = DocLayoutEngine::ResolveLayoutModelPath(modelPath);

    if (resolvedPath.empty()) {
        wxString userDir = wxStandardPaths::Get().GetUserConfigDir() + wxFileName::GetPathSeparator() + "LinguaAlpaca";
        wxString iniPath = userDir + wxFileName::GetPathSeparator() + "config.ini";
        if (wxFileExists(iniPath)) {
            wxFileConfig fileConfig("LinguaAlpaca", "", iniPath);
            std::string pathInFile = fileConfig.Read("/LayoutModel/Path", "").ToUTF8().data();
            resolvedPath = DocLayoutEngine::ResolveLayoutModelPath(pathInFile);
        }
    }

    if (resolvedPath.empty()) {
        wxString envPath;
        if (wxGetEnv("LINGUA_LAYOUT_MODEL_PATH", &envPath) && !envPath.IsEmpty()) {
            resolvedPath = DocLayoutEngine::ResolveLayoutModelPath(envPath.ToUTF8().data());
        }
    }

    if (resolvedPath.empty()) {
        const std::vector<std::string> defaultCandidates = {
            "models/PP-DocLayoutV3.onnx",
            "../models/PP-DocLayoutV3.onnx",
            "../../models/PP-DocLayoutV3.onnx",
            "models/PP-DocLayoutV2.onnx",
            "../models/PP-DocLayoutV2.onnx",
            "../../models/PP-DocLayoutV2.onnx"
        };
        for (const auto& cand : defaultCandidates) {
            std::string res = DocLayoutEngine::ResolveLayoutModelPath(cand);
            if (!res.empty()) {
                resolvedPath = res;
                break;
            }
        }
    }

    if (!resolvedPath.empty()) {
        bool initOk = engine.Initialize(resolvedPath, 0, 4);
        REQUIRE(initOk == true);
        REQUIRE(engine.IsLoaded() == true);
    }

    DocumentLayoutResult result;

    std::string imgPath = "tests/test_newspaper.jpg";
    if (!wxFileExists(imgPath)) {
        imgPath = "../tests/test_newspaper.jpg";
    }
    if (!wxFileExists(imgPath)) {
        imgPath = "../../tests/test_newspaper.jpg";
    }

    if (wxFileExists(imgPath)) {
        bool ok = engine.AnalyzeLayout(imgPath, result);
        REQUIRE(ok == true);
        REQUIRE(result.imageWidth > 0);
        REQUIRE(result.imageHeight > 0);
        REQUIRE(result.elements.size() >= 1);
        std::cout << "\n>>> Detected " << result.elements.size() << " layout elements in test_newspaper.jpg:\n";
        for (const auto& elem : result.elements) {
            std::cout << "  [" << elem.readingOrder << "] " << elem.labelName 
                 << " (score=" << elem.score << ") bbox=[" 
                 << elem.x1 << ", " << elem.y1 << ", " << elem.x2 << ", " << elem.y2 << "]\n";
        }
        // 验证用户真实图书第 5 页 (篇章标题 "总  序") 与 第 8 页 (目录 "目录 Contents") 必须被正确保留
        // 图片固定存放在 tests/ 目录中，脱离对外部网络共享 PDF 路径的依赖
        std::string page5Path = "tests/test_book_page5.png";
        std::string page8Path = "tests/test_book_page8.png";
        if (!wxFileExists(page5Path)) { page5Path = "../tests/test_book_page5.png"; }
        if (!wxFileExists(page5Path)) { page5Path = "../../tests/test_book_page5.png"; }
        if (!wxFileExists(page8Path)) { page8Path = "../tests/test_book_page8.png"; }
        if (!wxFileExists(page8Path)) { page8Path = "../../tests/test_book_page8.png"; }

        // 若本地图片尚未生成且外部 PDF 存在，自动提取渲染并固化至 tests 目录
        std::string pdfUserPath = "/Volumes/SHARE FILES/马戛尔尼使团使华观感 ([英] 乔治•马戛尔尼  [英] 约翰•巴罗 著 何高济  何毓宁 译) (Z-Library).pdf";
        if ((!wxFileExists(page5Path) || !wxFileExists(page8Path)) && wxFileExists(pdfUserPath)) {
            std::string targetDir = "tests";
            if (!wxDirExists(targetDir)) { targetDir = "../tests"; }
            if (!wxDirExists(targetDir)) { targetDir = "../../tests"; }

            wxImage img5, img8;
            if (!wxFileExists(page5Path) && PdfHelper::RenderPage(pdfUserPath, 4, img5, 1600)) {
                img5.SaveFile(targetDir + "/test_book_page5.png", wxBITMAP_TYPE_PNG);
                page5Path = targetDir + "/test_book_page5.png";
            }
            if (!wxFileExists(page8Path) && PdfHelper::RenderPage(pdfUserPath, 7, img8, 1600)) {
                img8.SaveFile(targetDir + "/test_book_page8.png", wxBITMAP_TYPE_PNG);
                page8Path = targetDir + "/test_book_page8.png";
            }
        }

        // 验证第 5 页 (总序)
        REQUIRE(wxFileExists(page5Path));
        {
            DocumentLayoutResult res5;
            bool ok5 = engine.AnalyzeLayout(page5Path, res5);
            REQUIRE(ok5 == true);
            CHECK(res5.elements.size() >= 3);
            std::cout << "\n--- Page 5 (W=" << res5.imageWidth << ", H=" << res5.imageHeight << ") detected " << res5.elements.size() << " elements:\n";
            bool foundTitle = false;
            for (const auto& elem : res5.elements) {
                std::cout << "  [" << elem.readingOrder << "] " << elem.labelName 
                     << " (score=" << elem.score << ") bbox=[" 
                     << elem.x1 << ", " << elem.y1 << ", " << elem.x2 << ", " << elem.y2 << "]\n";
                if (elem.y1 >= 280 && elem.y2 <= 420 && elem.x1 >= 700 && elem.x2 <= 1000) {
                    foundTitle = true;
                }
            }
            CHECK(foundTitle == true);
        }

        // 验证第 8 页 (目录 Contents)
        REQUIRE(wxFileExists(page8Path));
        {
            DocumentLayoutResult res8;
            bool ok8 = engine.AnalyzeLayout(page8Path, res8);
            REQUIRE(ok8 == true);
            CHECK(res8.elements.size() >= 14);
            std::cout << "\n--- Page 8 (W=" << res8.imageWidth << ", H=" << res8.imageHeight << ") detected " << res8.elements.size() << " elements:\n";
            bool foundContents = false;
            for (const auto& elem : res8.elements) {
                std::cout << "  [" << elem.readingOrder << "] " << elem.labelName 
                     << " (score=" << elem.score << ") bbox=[" 
                     << elem.x1 << ", " << elem.y1 << ", " << elem.x2 << ", " << elem.y2 << "]\n";
                if (elem.y1 >= 300 && elem.y2 <= 420 && elem.x1 >= 200 && elem.x2 <= 550) {
                    foundContents = true;
                }
            }
            CHECK(foundContents == true);
        }

        // Verify bounding boxes stay within image bounds and no duplicate containment
        for (size_t i = 0; i < result.elements.size(); ++i) {
            const auto& a = result.elements[i];
            REQUIRE(a.x1 >= 0);
            REQUIRE(a.y1 >= 0);
            REQUIRE(a.x2 <= result.imageWidth);
            REQUIRE(a.y2 <= result.imageHeight);

            int areaA = (a.x2 - a.x1) * (a.y2 - a.y1);
            for (size_t j = i + 1; j < result.elements.size(); ++j) {
                const auto& b = result.elements[j];
                int areaB = (b.x2 - b.x1) * (b.y2 - b.y1);
                int ix1 = (std::max)(a.x1, b.x1);
                int iy1 = (std::max)(a.y1, b.y1);
                int ix2 = (std::min)(a.x2, b.x2);
                int iy2 = (std::min)(a.y2, b.y2);
                int iw = (std::max)(0, ix2 - ix1);
                int ih = (std::max)(0, iy2 - iy1);
                int interArea = iw * ih;
                if (interArea > 0 && areaA > 0 && areaB > 0) {
                    float ios = static_cast<float>(interArea) / (std::min)(areaA, areaB);
                    if (a.labelName == b.labelName) {
                        // 相同类别绝不能存在高度包含的重复嵌套框
                        CHECK(ios < 0.70f);
                    }
                }
            }
        }
    }
}

TEST_CASE("DocLayoutEngine - Suppress Contained and Duplicate Footnote Boxes", "[layout]") {
    // 模拟官方 PaddleOCR 示例中 test_newspaper 检测到的 3 个图注候选框
    // 框 1: 单行切片框 (score=0.4316)
    // 框 2: 多行完整块 (score=0.6346)
    // 框 3: 右下落款切片框 (score=0.3509)
    std::vector<LayoutElement> elements = {
        LayoutElement{1, LayoutElementType::Text, "vision_footnote", 0.4316f, 810, 702, 1452, 724, 0},
        LayoutElement{2, LayoutElementType::Text, "vision_footnote", 0.6346f, 809, 702, 1486, 750, 0},
        LayoutElement{3, LayoutElementType::Text, "vision_footnote", 0.3509f, 1246, 729, 1487, 750, 0}
    };

    DocLayoutFilterConfig cfg;
    cfg.mergeLayoutBlocks = true;
    cfg.containmentIosThreshold = 0.70f;

    DocLayoutEngine::SuppressContainedOrDuplicateBoxes(elements, cfg);

    // 核心断言：两个单行局部碎片框必须被抑制，仅保留唯一的多行完整图注大框！
    REQUIRE(elements.size() == 1);
    CHECK(elements[0].labelName == "vision_footnote");
    CHECK(elements[0].score >= 0.6346f);
    CHECK(elements[0].x1 <= 809);
    CHECK(elements[0].y1 <= 702);
    CHECK(elements[0].x2 >= 1486);
    CHECK(elements[0].y2 >= 750);
}

TEST_CASE("DocumentPipeline - Never delete original input image or document file", "[pipeline]") {
    // 模拟用户原始输入图片文件
    wxString tempOriginal = wxFileName::CreateTempFileName("user_orig_doc_") + ".png";
    {
        wxImage dummyImg(300, 200);
        dummyImg.Clear(255);
        REQUIRE(dummyImg.SaveFile(tempOriginal, wxBITMAP_TYPE_PNG));
    }
    REQUIRE(wxFileExists(tempOriginal));

    auto pipeline = std::make_shared<DocumentPipeline>(nullptr, nullptr);

    std::promise<bool> finishPromise;
    auto finishFuture = finishPromise.get_future();

    wxString outDir = wxFileName::CreateTempFileName("doc_out_test");
    wxRemoveFile(outDir);

    pipeline->StartParseAsync(
        tempOriginal.ToUTF8().data(),
        outDir.ToUTF8().data(),
        false,
        nullptr,
        [&finishPromise](bool success, const std::string& md, const std::string& json, const std::string& err) {
            finishPromise.set_value(true);
        }
    );

    finishFuture.wait_for(std::chrono::seconds(5));

    // 核心断言：流水线处理完成后，用户原始文件绝对不能被删除！
    REQUIRE(wxFileExists(tempOriginal) == true);

    // 仅在测试结束时清理测试用例创建的文件
    wxRemoveFile(tempOriginal);
    if (wxDirExists(outDir)) {
        wxFileName::Rmdir(outDir, wxPATH_RMDIR_RECURSIVE);
    }
}

TEST_CASE("DocumentPipeline - Convert PaddleOCR OTSL table to HTML table", "[table]") {
    std::string otsl =
        "<fcel>Strategy<fcel>Discrepancy<fcel>w/ sensitive keyword<lcel><lcel><fcel>w/o sensitive keyword<lcel><nl>"
        "<ucel><ucel><fcel>Success<fcel>Failure 1<fcel>Failure 2<fcel>Success<fcel>Failure 1<nl>"
        "<fcel>No Strategy<fcel>N/A<fcel>2.8%<fcel>0.4%<fcel>96.8%<fcel>98.9%<fcel>1.1%<nl>";

    std::string html = DocumentPipeline::ConvertOtslToHtml(otsl);

    REQUIRE_FALSE(html.empty());
    REQUIRE(html.find("<table>") != std::string::npos);
    REQUIRE(html.find("<thead>") != std::string::npos);
    REQUIRE(html.find("<tbody>") != std::string::npos);
    // 验证合并单元格生成正确
    REQUIRE(html.find("rowspan=\"2\">Strategy</th>") != std::string::npos);
    REQUIRE(html.find("rowspan=\"2\">Discrepancy</th>") != std::string::npos);
    REQUIRE(html.find("colspan=\"3\">w/ sensitive keyword</th>") != std::string::npos);
    REQUIRE(html.find("colspan=\"2\">w/o sensitive keyword</th>") != std::string::npos);
    REQUIRE(html.find("<td>No Strategy</td>") != std::string::npos);
    REQUIRE(html.find("<td>96.8%</td>") != std::string::npos);
    REQUIRE(html.find("</table>") != std::string::npos);
}

TEST_CASE("DocLayoutEngine - Filter Header and Footer Page Numbers", "[layout][filter]") {
    DocLayoutFilterConfig cfg;
    const int origW = 2800;
    const int origH = 3900;

    // 1. 用户真实文档：左侧页眉独立页码 (如第 2 页: [446, 445, 509, 512], type="number")
    LayoutElement leftHeaderNum{1, LayoutElementType::Text, "number", 0.95f, 446, 445, 509, 512, 1};
    CHECK(DocLayoutEngine::ShouldFilterElement(leftHeaderNum, origW, origH, cfg) == true);

    // 2. 用户真实文档：右侧页眉独立页码 (如第 5 页: [2411, 456, 2466, 518], type="number")
    LayoutElement rightHeaderNum{1, LayoutElementType::Text, "number", 0.96f, 2411, 456, 2466, 518, 1};
    CHECK(DocLayoutEngine::ShouldFilterElement(rightHeaderNum, origW, origH, cfg) == true);

    // 3. 底部页脚独立页码 (如居中: [1380, 3680, 1440, 3740], y ≈ 94%H)
    LayoutElement footerNum{10, LayoutElementType::Text, "number", 0.92f, 1380, 3680, 1440, 3740, 10};
    CHECK(DocLayoutEngine::ShouldFilterElement(footerNum, origW, origH, cfg) == true);

    // 4. 几何兜底：被模型误判为 "text" 的左上角或右上角单行页码
    LayoutElement misclassifiedCorner{1, LayoutElementType::Text, "text", 0.80f, 446, 445, 509, 512, 1};
    CHECK(DocLayoutEngine::ShouldFilterElement(misclassifiedCorner, origW, origH, cfg) == true);

    // 5. 显式模型页眉与页脚标签
    LayoutElement modelHeader{1, LayoutElementType::Header, "header", 0.88f, 1000, 420, 1800, 480, 1};
    CHECK(DocLayoutEngine::ShouldFilterElement(modelHeader, origW, origH, cfg) == true);

    LayoutElement modelFooter{20, LayoutElementType::Footer, "footer", 0.85f, 800, 3650, 2000, 3720, 20};
    CHECK(DocLayoutEngine::ShouldFilterElement(modelFooter, origW, origH, cfg) == true);

    // 6. 反向校验：页面正文段落 (绝不能被误删)
    LayoutElement bodyParagraph{2, LayoutElementType::Text, "text", 0.99f, 350, 800, 2450, 1800, 2};
    CHECK(DocLayoutEngine::ShouldFilterElement(bodyParagraph, origW, origH, cfg) == false);

    // 7. 反向校验：顶部文档主标题 (doc_title 绝不能被几何兜底误删)
    LayoutElement docTitle{1, LayoutElementType::Title, "doc_title", 0.98f, 600, 450, 2200, 580, 1};
    CHECK(DocLayoutEngine::ShouldFilterElement(docTitle, origW, origH, cfg) == false);

    // 8. 绝对保护：显式 footnote 与 vision_footnote 标签 (绝不能作为页眉/页脚被过滤)
    LayoutElement modelFootnote{10, LayoutElementType::Text, "footnote", 0.90f, 400, 3650, 2400, 3720, 10};
    CHECK(DocLayoutEngine::ShouldFilterElement(modelFootnote, origW, origH, cfg) == false);

    LayoutElement visionFootnote{11, LayoutElementType::Text, "vision_footnote", 0.88f, 500, 3660, 2200, 3730, 11};
    CHECK(DocLayoutEngine::ShouldFilterElement(visionFootnote, origW, origH, cfg) == false);

    // 9. 绝对保护：底部宽文本块 (可能为未被模型识别为 footnote 的普通注解文本，放行给 OCR 与正文标记研判)
    LayoutElement wideBottomAnnotation{12, LayoutElementType::Text, "text", 0.85f, 400, 3650, 2400, 3720, 12};
    CHECK(DocLayoutEngine::ShouldFilterElement(wideBottomAnnotation, origW, origH, cfg) == false);
}

TEST_CASE("DocumentPipeline - Secondary Page Number Filtering", "[pipeline][filter]") {
    const int pageW = 2800;
    const int pageH = 3900;

    // 1. 显式 number 标签在顶部/底部
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("2", "number", 446, 445, 509, 512, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("5", "number", 2411, 456, 2466, 518, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("86", "number", 1380, 3680, 1440, 3740, pageW, pageH) == true);

    // 2. 文本标签但在边角且文本为纯数字/装饰页码
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("2", "text", 446, 445, 509, 512, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("5", "text", 2411, 456, 2466, 518, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("- 2 -", "text", 446, 445, 550, 512, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("· 5 ·", "text", 2350, 456, 2466, 518, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("第 5 页", "text", 1300, 3680, 1500, 3740, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("Page 12", "text", 2300, 456, 2466, 518, pageW, pageH) == true);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("IV", "text", 446, 445, 509, 512, pageW, pageH) == true);

    // 3. 反向校验：正文中包含数字的普通句子
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("In 1984, George Orwell wrote 1984", "text", 350, 800, 2450, 1800, pageW, pageH) == false);

    // 4. 反向校验：标题
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("第一章 绪论", "paragraph_title", 500, 450, 2000, 550, pageW, pageH) == false);

    // 5. 反向校验：注解文本绝不被误判为页码
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("[1] 参考文献详情说明", "text", 400, 3650, 2400, 3720, pageW, pageH) == false);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("① 底部注解详情内容", "text", 400, 3650, 2400, 3720, pageW, pageH) == false);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("注：底部特别说明", "text", 400, 3650, 2400, 3720, pageW, pageH) == false);

    // 6. 反向校验：目录标题与篇章标题即便带有短小英文或位于顶部角落，绝不能被误判为页码
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("目录 Contents", "text", 235, 323, 500, 401, 1600, 2318) == false);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("目录", "text", 235, 323, 400, 401, 1600, 2318) == false);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("总  序", "text", 235, 432, 500, 498, 1600, 2318) == false);
    CHECK(DocumentPipeline::IsHeaderOrFooterPageNumber("", "text", 235, 323, 500, 401, 1600, 2318) == false);
}

TEST_CASE("DocumentPipeline - Annotation and Footnote Marker Detection", "[pipeline][footnote]") {
    const int pageH = 3900;

    // 1. 正文注解数字标记检测 (HasAnnotationMarkers)
    // LaTeX 上标
    CHECK(DocumentPipeline::HasAnnotationMarkers("在模型注意力机制中\\(^{[1]}\\)，计算复杂度为O(N^2)") == true);
    CHECK(DocumentPipeline::HasAnnotationMarkers("实验结果显示\\(^1\\)显著优于基线模型") == true);
    // 带圈数字
    CHECK(DocumentPipeline::HasAnnotationMarkers("根据最新人口普查数据①，总人口趋于平稳") == true);
    CHECK(DocumentPipeline::HasAnnotationMarkers("第二项研究结论见附录②。") == true);
    // 方括号数字
    CHECK(DocumentPipeline::HasAnnotationMarkers("Transformer架构[1]已被广泛应用于各领域") == true);
    // 中文注记
    CHECK(DocumentPipeline::HasAnnotationMarkers("详细参数配置见【注1】") == true);
    CHECK(DocumentPipeline::HasAnnotationMarkers("测试方案[注2]已通过审核") == true);
    // Markdown 脚注引用
    CHECK(DocumentPipeline::HasAnnotationMarkers("如前文所述[^ref1]，系统保持高可用") == true);
    // 反向校验：普通文本无标记
    CHECK(DocumentPipeline::HasAnnotationMarkers("普通段落文本，没有任何注解或脚注标记。") == false);
    CHECK(DocumentPipeline::HasAnnotationMarkers("2024年10月8日系统运行正常") == false);

    // 2. 底部注解判定 (IsFootnoteOrAnnotation)
    // 显式模型标签
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("普通文本", "footnote", 3500, 3600, pageH, false) == true);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("图表说明", "vision_footnote", 3500, 3600, pageH, false) == true);

    // 文本带有典型注解前缀
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("① 本文所引数据来源于国家统计局2023年统计年鉴。", "text", 3500, 3600, pageH, false) == true);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("[1] Vaswani A, et al. Attention is all you need. NeurIPS, 2017.", "text", 3500, 3600, pageH, false) == true);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("\\(^{[1]}\\) 这里是具体的底部详细说明文字。", "text", 3500, 3600, pageH, false) == true);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("注：表中所有数据均已剔除异常值。", "text", 3500, 3600, pageH, false) == true);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("Note: P-values less than 0.05 are considered statistically significant.", "text", 3500, 3600, pageH, false) == true);

    // 底部编号解释性文本 (topRatio >= 0.72f)
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("1. 本指标不含港澳台地区统计数据。", "text", 3200, 3300, pageH, false) == true);

    // 正文中存在注解标记，且底部存在具有内容的解释文本
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("由国家自然科学基金重点项目资助。", "text", 3500, 3600, pageH, true) == true);

    // 反向校验：纯页码或运行页眉绝不能被误判为注解
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("5", "text", 3500, 3600, pageH, false) == false);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("- 5 -", "text", 3500, 3600, pageH, false) == false);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("第 5 页", "text", 3500, 3600, pageH, false) == false);
    CHECK(DocumentPipeline::IsFootnoteOrAnnotation("5", "number", 3500, 3600, pageH, true) == false);
}

TEST_CASE("DocLayoutEngine & DocumentPipeline - Empty Page and Image Content Detection", "[layout][pipeline][blank_page]") {
    // 1. 纯白空页判定 (255, 255, 255)
    wxImage whiteImg(300, 400);
    whiteImg.SetRGB(wxRect(0, 0, 300, 400), 255, 255, 255);
    CHECK(DocLayoutEngine::IsImageContentEmpty(whiteImg) == true);
    CHECK(DocumentPipeline::IsImageContentEmpty(whiteImg) == true);

    // 2. 纯浅色/米黄色复古纸张空页判定 (245, 240, 235)
    wxImage creamImg(300, 400);
    creamImg.SetRGB(wxRect(0, 0, 300, 400), 245, 240, 235);
    CHECK(DocLayoutEngine::IsImageContentEmpty(creamImg) == true);
    CHECK(DocumentPipeline::IsImageContentEmpty(creamImg) == true);

    // 3. 带有正文笔墨/图表的非空页面
    wxImage textImg(300, 400);
    textImg.SetRGB(wxRect(0, 0, 300, 400), 255, 255, 255);
    // 模拟正文区域黑色字符块 (80x100 黑色区域)
    textImg.SetRGB(wxRect(50, 50, 80, 100), 10, 10, 10);
    CHECK(DocLayoutEngine::IsImageContentEmpty(textImg) == false);
    CHECK(DocumentPipeline::IsImageContentEmpty(textImg) == false);

    // 4. 空/无效位图安全防护
    wxImage invalidImg;
    CHECK(DocLayoutEngine::IsImageContentEmpty(invalidImg) == true);
    CHECK(DocumentPipeline::IsImageContentEmpty(invalidImg) == true);
}


