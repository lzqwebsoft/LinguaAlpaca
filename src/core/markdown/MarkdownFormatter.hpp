#pragma once
#pragma execution_character_set("utf-8")

#include <string>
#include <vector>
#include <string_view>

namespace LinguaAlpaca {

/**
 * @brief Markdown 样式片段类型
 */
enum class MarkdownStyle {
    Default,        // 普通正文
    Heading1,       // 一级标题 (# )
    Heading2,       // 二级标题 (## )
    Heading3,       // 三级标题 (### )
    Heading4,       // 四级标题 (#### )
    Heading5,       // 五级标题 (##### )
    Heading6,       // 六级标题 (###### )
    Bold,           // 加粗 (**text** / __text__)
    Italic,         // 斜体 (*text* / _text_)
    BoldItalic,     // 粗斜体 (***text*** / ___text___)
    InlineCode,     // 行内代码 (`code`)
    CodeBlock,      // 多行代码块 (```...```)
    Blockquote,     // 引用内容 (> quote)
    BlockquoteBar,  // 引用左侧装饰条 (▍ )
    ListBullet,     // 无序列表圆点 (• )
    ListNumber,     // 有序列表序号 (1. 2. 等)
    Divider,        // 水平分割线 (--- / ***)
    LinkText,       // 链接文本 ([text](url))
    Strikethrough   // 删除线 (~~text~~)
};

/**
 * @brief 单个 Markdown 样式片段
 */
struct MarkdownSegment {
    MarkdownStyle style{MarkdownStyle::Default};
    std::string text;
};

/**
 * @brief Markdown 文本解析与排版工具类
 */
class MarkdownFormatter {
public:
    /**
     * @brief 将 Markdown 原始文本解析为结构化的富文本片段列表
     * @param markdown 原始 Markdown 字符串 (UTF-8)
     * @return 样式片段列表
     */
    static std::vector<MarkdownSegment> Parse(const std::string& markdown);

    /**
     * @brief 剥离 Markdown 语法标记，获取纯文本内容（用于剪贴板复制或朗读）
     * @param markdown 原始 Markdown 字符串 (UTF-8)
     * @return 去除 Markdown 格式标记后的纯文本
     */
    static std::string StripMarkdown(const std::string& markdown);

    /**
     * @brief 解析单行中的行内 Markdown 语法（加粗、斜体、代码、链接、删除线等）
     */
    static void ParseInlineElements(std::string_view line,
                                   std::vector<MarkdownSegment>& outSegments,
                                   MarkdownStyle baseStyle = MarkdownStyle::Default);

private:
    static std::string_view Trim(std::string_view s);
    static std::string_view TrimLeft(std::string_view s);
    static std::string_view TrimRight(std::string_view s);
    static bool IsHorizontalRule(std::string_view line);
    static bool IsHeading(std::string_view line, int& outLevel, std::string_view& outContent);
    static bool IsOrderedList(std::string_view line, std::string_view& outNum, std::string_view& outContent, size_t& outIndent);
    static bool IsUnorderedList(std::string_view line, std::string_view& outContent, size_t& outIndent);
};

} // namespace LinguaAlpaca
