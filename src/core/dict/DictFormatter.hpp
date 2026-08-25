#pragma once
#pragma execution_character_set("utf-8")

#include <string>
#include <vector>
#include <cstdint>

#include "DictEngine.hpp"

namespace LinguaAlpaca {

/**
 * @brief 结构化富文本片段样式类型
 */
enum class DictTextStyle {
    Default,       // 普通释义正文
    DictHeader,    // 词典名称标头 (如 📖 朗道英汉字典5.0)
    Phonetic,      // 音标
    PartOfSpeech,  // 词性前缀 (如 [n.], v., adj.)
    Tag,           // 版块标签 (如 【例】, 【短语】, 【同义词】)
    Example,       // 例句或注释文本 (如 e.g. / 【例】后的内容)
    NumberedItem,  // 序号标号 (如 1., ①, (1))
    Divider        // 词典间分割线
};

/**
 * @brief 单个富文本样式片段
 */
struct DictTextSegment {
    DictTextStyle style{DictTextStyle::Default};
    std::string text;
};

/**
 * @brief StarDict 释义转义与排版格式化工具集
 * 
 * 针对 StarDict 规范中定义的多种数据类型（'m', 't', 'y', 'h', 'x', 'g', 'k', 'w' 等）
 * 提供结构化解析、标签转义与排版整理，使其适合在 wxTextCtrl (wxTE_RICH2) 中展示。
 */
class DictFormatter {
public:
    /**
     * @brief 纯文本转义与结构化排版 ('m')
     * 统一处理转义字符、字源《》、词性<<>>、义项编号、例句分隔符 (* / ~)、短语动词、习语及用法说明
     */
    static std::string FormatPlaintext(const std::string& text);

    // 为向上兼容及特定测试保留快捷调用
    static std::string UnescapePlaintext(const std::string& text) { return FormatPlaintext(text); }
    static std::string FormatOxfordPlaintext(const std::string& text) { return FormatPlaintext(text); }
    static std::string Format21Century(const std::string& text) { return FormatPlaintext(text); }

    /**
     * @brief 国际音标与注音格式化 ('t' 英文音标, 'y' 汉语拼音/注音)
     * 清理控制字符，保证音标括号结构完整
     */
    static std::string FormatPhonetic(const std::string& text);

    /**
     * @brief HTML 网页排版格式转义 ('h' / 'H')
     * 转换段落、列表、表格、标题为排版文本，剥离样式标签，全面解码 HTML 实体
     */
    static std::string FormatHtml(const std::string& html);

    /**
     * @brief XDXF 词典 XML 格式转义 ('x' / 'X')
     * 解析 <dtrn>, <pos>, <tr>, <ex>, <k>, <co> 等 XDXF 专用标签并排版
     */
    static std::string FormatXdxf(const std::string& xdxf);

    /**
     * @brief Pango 文本标记转义 ('g')
     * 剥离 <span foreground=...>, <b>, <i> 等 Pango 样式标签并解码实体
     */
    static std::string FormatPango(const std::string& pango);

    /**
     * @brief 金山词霸 XML 格式转义 ('k')
     * 解析 <pos>, <pron>, <def>, <sent>, <orig>, <trans> 等双语词典节点
     */
    static std::string FormatKingsoft(const std::string& xml);

    /**
     * @brief MediaWiki 维基语法转义 ('w')
     * 转换加粗、斜体、章节标题、列表与内链语法
     */
    static std::string FormatMediaWiki(const std::string& wiki);

    /**
     * @brief 通用 HTML/XML 命名实体及 Unicode (&#...; / &#x...;) 字符解码
     */
    static std::string DecodeHtmlEntities(const std::string& text);

    /**
     * @brief Unicode 码点转 UTF-8 字符串
     */
    static std::string Utf32ToUtf8(uint32_t codepoint);

    /**
     * @brief StarDict 统一格式化总调度入口
     * 
     * @param rawData 词典原始二进制/文本数据
     * @param sameTypeSequence 词典 .ifo 中声明的 sametypesequence（为空则按动态类型解析）
     * @param outPhonetic 输出提取到的音标（如 [fə'netɪk]）
     * @param outDefinition 输出排版完毕的释义正文
     */
    static void Format(const std::string& rawData,
                       const std::string& sameTypeSequence,
                       std::string& outPhonetic,
                       std::string& outDefinition);

    /**
     * @brief 将多个词典检索结果结构化分解为富文本片段序列，供 UI 视图渲染
     */
    static std::vector<DictTextSegment> BuildRichTextSegments(const std::vector<DictSearchResult>& results);

    /**
     * @brief 清洗并规范化多余空行与首尾空白
     */
    static std::string NormalizeNewlinesAndTrim(const std::string& text);
};

} // namespace LinguaAlpaca
