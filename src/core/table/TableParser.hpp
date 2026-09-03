#pragma once
#pragma execution_character_set("utf-8")

#include <string>
#include <vector>
#include <string_view>
#include <algorithm>

namespace LinguaAlpaca {

/**
 * @brief 结构化表格数据对象
 */
struct TableData {
    std::vector<std::string> headers;                // 表头列名称列表
    std::vector<std::vector<std::string>> rows;       // 数据行列表（每行为各列文本）
    bool hasHeader{true};                            // 是否包含表头

    bool IsEmpty() const {
        return headers.empty() && rows.empty();
    }

    size_t RowCount() const {
        return rows.size();
    }

    size_t ColCount() const {
        size_t maxCols = headers.size();
        for (const auto& row : rows) {
            maxCols = std::max(maxCols, row.size());
        }
        return maxCols;
    }

    void Clear() {
        headers.clear();
        rows.clear();
        hasHeader = true;
    }
};

/**
 * @brief 表格解析与格式化工具类 (支持 OTSL、Markdown、TSV/CSV)
 */
class TableParser {
public:
    /**
     * @brief 判断文本是否包含表格结构（如 OTSL <fcel>/<nl>、Markdown 表格等）
     */
    static bool IsTableFormat(std::string_view text);

    /**
     * @brief 解析原始文本为结构化 TableData
     * @param text 包含 OTSL、Markdown 或 TSV 的原始文本
     * @return 解析后的 TableData 结构
     */
    static TableData Parse(std::string_view text);

    /**
     * @brief 专门解析 OTSL 格式表格 (<fcel>...<fcel>...<nl>)
     */
    static TableData ParseOtsl(std::string_view text);

    /**
     * @brief 专门解析 Markdown 格式表格 (| col1 | col2 |\n|---|---|\n| a | b |)
     */
    static TableData ParseMarkdownTable(std::string_view text);

    /**
     * @brief 专门解析 TSV (Tab 分隔) 格式表格
     */
    static TableData ParseTsv(std::string_view text);

    /**
     * @brief 将 TableData 转换为标准 Markdown 表格字符串
     */
    static std::string ToMarkdown(const TableData& table);

    /**
     * @brief 将 TableData 转换为制表符分隔 TSV 字符串 (完美适配 Excel/WPS/飞书/Notion 直接粘贴)
     */
    static std::string ToTsv(const TableData& table);

    /**
     * @brief 将 TableData 转换为符合 RFC 4180 的 CSV 字符串
     */
    static std::string ToCsv(const TableData& table);

    /**
     * @brief 将 TableData 转换为对齐的纯文本网格
     */
    static std::string ToPlainGrid(const TableData& table);

    /**
     * @brief 将 TableData 转换为用于 TTS 语音合成朗读的自然流畅文本
     */
    static std::string ToSpeechText(const TableData& table);

private:
    static std::string_view Trim(std::string_view s);
    static std::string CleanCellContent(std::string_view s);
    static std::string EscapeCsvCell(const std::string& cell);
    static void NormalizeColumns(TableData& table);
};

} // namespace LinguaAlpaca
