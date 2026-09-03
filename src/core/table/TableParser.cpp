#include "TableParser.hpp"
#include <iomanip>
#include <cctype>

namespace LinguaAlpaca {

std::string_view TableParser::Trim(std::string_view s) {
    while (!s.empty() && (static_cast<unsigned char>(s.front()) <= ' ' || s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (static_cast<unsigned char>(s.back()) <= ' ' || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

std::string TableParser::CleanCellContent(std::string_view s) {
    s = Trim(s);
    if (s.empty()) return "";

    // 快速路径：若不包含标签与换行，直接返回截取结果，避免逐字符拷贝与额外分配
    bool hasSpecialChar = false;
    for (char c : s) {
        if (c == '<' || c == '>' || c == '\r' || c == '\n') {
            hasSpecialChar = true;
            break;
        }
    }
    if (!hasSpecialChar) {
        return std::string(s);
    }

    std::string result;
    result.reserve(s.size());

    // 过滤掉残留的内联标签，并将换行替换为空格
    bool inTag = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '<') {
            inTag = true;
        } else if (s[i] == '>') {
            inTag = false;
        } else if (!inTag) {
            if (s[i] == '\r' || s[i] == '\n') {
                result.push_back(' ');
            } else {
                result.push_back(s[i]);
            }
        }
    }
    return std::string(Trim(result));
}

bool TableParser::IsTableFormat(std::string_view text) {
    if (text.empty()) return false;

    // 1. 快速检查是否包含 OTSL / HTML 标签
    if (text.find('<') != std::string_view::npos) {
        if (text.find("<fcel>") != std::string_view::npos ||
            text.find("<nl>") != std::string_view::npos ||
            text.find("<ecel>") != std::string_view::npos ||
            text.find("<ched>") != std::string_view::npos ||
            text.find("<lcel>") != std::string_view::npos ||
            text.find("<ucel>") != std::string_view::npos) {
            return true;
        }
        if (text.find("<table") != std::string_view::npos &&
            text.find("<tr>") != std::string_view::npos) {
            return true;
        }
    }

    // 2. 检查 Markdown 表格标记 (| col1 | col2 |\n|---|---|)
    size_t pipePos = text.find('|');
    if (pipePos != std::string_view::npos) {
        if (text.find("|---", pipePos) != std::string_view::npos ||
            text.find("| ---", pipePos) != std::string_view::npos ||
            text.find("|:-", pipePos) != std::string_view::npos) {
            return true;
        }
    }

    return false;
}

TableData TableParser::Parse(std::string_view text) {
    if (text.empty()) {
        return TableData{};
    }

    if (text.find('<') != std::string_view::npos) {
        if (text.find("<fcel>") != std::string_view::npos ||
            text.find("<nl>") != std::string_view::npos ||
            text.find("<ecel>") != std::string_view::npos ||
            text.find("<ched>") != std::string_view::npos) {
            return ParseOtsl(text);
        }
    }

    if (text.find('|') != std::string_view::npos) {
        return ParseMarkdownTable(text);
    }

    if (text.find('\t') != std::string_view::npos) {
        return ParseTsv(text);
    }

    return TableData{};
}

TableData TableParser::ParseOtsl(std::string_view text) {
    TableData table;
    std::vector<std::vector<std::string>> allRows;
    allRows.reserve(32);

    std::vector<std::string> currentRow;
    currentRow.reserve(8);

    size_t pos = 0;
    const size_t len = text.size();

    size_t currentCellContentStart = std::string_view::npos;

    auto finishCell = [&](size_t endPos) {
        if (currentCellContentStart != std::string_view::npos && endPos >= currentCellContentStart) {
            std::string_view rawContent = text.substr(currentCellContentStart, endPos - currentCellContentStart);
            currentRow.push_back(CleanCellContent(rawContent));
            currentCellContentStart = std::string_view::npos;
        }
    };

    while (pos < len) {
        size_t tagOpen = text.find('<', pos);
        if (tagOpen == std::string_view::npos) {
            finishCell(len);
            break;
        }

        size_t tagClose = text.find('>', tagOpen);
        if (tagClose == std::string_view::npos) {
            finishCell(len);
            break;
        }

        std::string_view tag = text.substr(tagOpen, tagClose - tagOpen + 1);

        if (tag == "<fcel>" || tag == "<ched>" || tag == "<rhed>" || tag == "<td>" || tag == "<th>") {
            finishCell(tagOpen);
            currentCellContentStart = tagClose + 1;
        }
        else if (tag == "<ecel>") {
            finishCell(tagOpen);
            currentRow.push_back("");
        }
        else if (tag == "<lcel>" || tag == "<ucel>" || tag == "<xcel>") {
            finishCell(tagOpen);
            currentRow.push_back("");
        }
        else if (tag == "<nl>" || tag == "</nl>" || tag == "</tr>") {
            finishCell(tagOpen);
            if (!currentRow.empty()) {
                allRows.push_back(std::move(currentRow));
                currentRow.clear();
                currentRow.reserve(8);
            }
        }
        else if (tag == "</td>" || tag == "</th>") {
            finishCell(tagOpen);
        }

        pos = tagClose + 1;
    }

    if (!currentRow.empty()) {
        allRows.push_back(std::move(currentRow));
    }

    if (allRows.empty()) {
        return table;
    }

    table.headers = std::move(allRows[0]);
    table.hasHeader = true;

    table.rows.reserve(allRows.size() - 1);
    for (size_t r = 1; r < allRows.size(); ++r) {
        table.rows.push_back(std::move(allRows[r]));
    }

    NormalizeColumns(table);
    return table;
}

TableData TableParser::ParseMarkdownTable(std::string_view text) {
    TableData table;
    std::vector<std::vector<std::string>> parsedRows;
    parsedRows.reserve(32);

    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();

        std::string_view line = Trim(text.substr(start, end - start));
        start = end + 1;

        if (line.empty() || line.front() != '|') {
            continue;
        }

        // 判断是否是分隔行 (如 |---|---| 或 | :--- | :---: |)
        bool isDivider = true;
        size_t dashCount = 0;
        for (char c : line) {
            if (c == '-') dashCount++;
            else if (c != '|' && c != ':' && c != ' ' && c != '\t') {
                isDivider = false;
                break;
            }
        }
        if (isDivider && dashCount >= 2) {
            continue;
        }

        std::vector<std::string> rowCells;
        rowCells.reserve(8);
        size_t cellStart = 1;
        while (cellStart < line.size()) {
            size_t cellEnd = line.find('|', cellStart);
            if (cellEnd == std::string_view::npos) {
                cellEnd = line.size();
            }
            rowCells.push_back(CleanCellContent(line.substr(cellStart, cellEnd - cellStart)));
            cellStart = cellEnd + 1;
        }

        if (!rowCells.empty()) {
            parsedRows.push_back(std::move(rowCells));
        }
    }

    if (parsedRows.empty()) {
        return table;
    }

    table.headers = std::move(parsedRows[0]);
    table.hasHeader = true;

    table.rows.reserve(parsedRows.size() - 1);
    for (size_t r = 1; r < parsedRows.size(); ++r) {
        table.rows.push_back(std::move(parsedRows[r]));
    }

    NormalizeColumns(table);
    return table;
}

TableData TableParser::ParseTsv(std::string_view text) {
    TableData table;
    std::vector<std::vector<std::string>> parsedRows;
    parsedRows.reserve(32);

    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();

        std::string_view line = Trim(text.substr(start, end - start));
        start = end + 1;

        if (line.empty()) continue;

        std::vector<std::string> rowCells;
        rowCells.reserve(8);
        size_t cellStart = 0;
        while (cellStart < line.size()) {
            size_t cellEnd = line.find('\t', cellStart);
            if (cellEnd == std::string_view::npos) {
                cellEnd = line.size();
            }
            rowCells.push_back(CleanCellContent(line.substr(cellStart, cellEnd - cellStart)));
            cellStart = cellEnd + 1;
        }

        if (!rowCells.empty()) {
            parsedRows.push_back(std::move(rowCells));
        }
    }

    if (parsedRows.empty()) return table;

    table.headers = std::move(parsedRows[0]);
    table.hasHeader = true;
    table.rows.reserve(parsedRows.size() - 1);
    for (size_t r = 1; r < parsedRows.size(); ++r) {
        table.rows.push_back(std::move(parsedRows[r]));
    }

    NormalizeColumns(table);
    return table;
}

void TableParser::NormalizeColumns(TableData& table) {
    // 移除末尾全为空白/空值的无效行 (例如模型结尾产生的 <ecel><ecel><nl>)
    while (!table.rows.empty()) {
        bool allEmpty = true;
        for (const auto& cell : table.rows.back()) {
            if (!Trim(cell).empty()) {
                allEmpty = false;
                break;
            }
        }
        if (allEmpty) {
            table.rows.pop_back();
        } else {
            break;
        }
    }

    size_t maxCols = table.ColCount();
    if (maxCols == 0) return;

    while (table.headers.size() < maxCols) {
        table.headers.push_back("列 " + std::to_string(table.headers.size() + 1));
    }

    for (auto& row : table.rows) {
        while (row.size() < maxCols) {
            row.push_back("");
        }
    }
}

std::string TableParser::ToMarkdown(const TableData& table) {
    if (table.IsEmpty()) return "";

    size_t cols = table.ColCount();
    size_t rows = table.rows.size();

    std::string result;
    result.reserve(cols * (rows + 2) * 20);

    // 1. 表头
    result.append("| ");
    for (size_t c = 0; c < cols; ++c) {
        if (c < table.headers.size()) {
            result.append(table.headers[c]);
        }
        result.append(" | ");
    }
    result.append("\n| ");

    // 2. 对齐分割线
    for (size_t c = 0; c < cols; ++c) {
        result.append(":--- | ");
    }
    result.append("\n");

    // 3. 数据行
    for (const auto& row : table.rows) {
        result.append("| ");
        for (size_t c = 0; c < cols; ++c) {
            if (c < row.size()) {
                result.append(row[c]);
            }
            result.append(" | ");
        }
        result.append("\n");
    }

    return result;
}

std::string TableParser::ToTsv(const TableData& table) {
    if (table.IsEmpty()) return "";

    size_t cols = table.ColCount();
    size_t rows = table.rows.size();

    std::string result;
    result.reserve(cols * (rows + 1) * 16);

    // 表头
    for (size_t c = 0; c < cols; ++c) {
        if (c < table.headers.size()) {
            result.append(table.headers[c]);
        }
        if (c + 1 < cols) result.push_back('\t');
    }
    result.push_back('\n');

    // 数据行
    for (const auto& row : table.rows) {
        for (size_t c = 0; c < cols; ++c) {
            if (c < row.size()) {
                result.append(row[c]);
            }
            if (c + 1 < cols) result.push_back('\t');
        }
        result.push_back('\n');
    }

    return result;
}

std::string TableParser::EscapeCsvCell(const std::string& cell) {
    bool needQuotes = false;
    for (char c : cell) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needQuotes = true;
            break;
        }
    }

    if (!needQuotes) return cell;

    std::string escaped;
    escaped.reserve(cell.size() + 4);
    escaped.push_back('"');
    for (char c : cell) {
        if (c == '"') {
            escaped.append("\"\"");
        } else {
            escaped.push_back(c);
        }
    }
    escaped.push_back('"');
    return escaped;
}

std::string TableParser::ToCsv(const TableData& table) {
    if (table.IsEmpty()) return "";

    size_t cols = table.ColCount();
    size_t rows = table.rows.size();

    std::string result;
    result.reserve(cols * (rows + 1) * 16);

    // 表头
    for (size_t c = 0; c < cols; ++c) {
        if (c < table.headers.size()) {
            result.append(EscapeCsvCell(table.headers[c]));
        }
        if (c + 1 < cols) result.push_back(',');
    }
    result.push_back('\n');

    // 数据行
    for (const auto& row : table.rows) {
        for (size_t c = 0; c < cols; ++c) {
            if (c < row.size()) {
                result.append(EscapeCsvCell(row[c]));
            }
            if (c + 1 < cols) result.push_back(',');
        }
        result.push_back('\n');
    }

    return result;
}

std::string TableParser::ToPlainGrid(const TableData& table) {
    if (table.IsEmpty()) return "";

    size_t cols = table.ColCount();
    std::vector<size_t> colWidths(cols, 4);

    for (size_t c = 0; c < cols; ++c) {
        if (c < table.headers.size()) {
            colWidths[c] = std::max(colWidths[c], table.headers[c].size());
        }
    }

    for (const auto& row : table.rows) {
        for (size_t c = 0; c < cols; ++c) {
            if (c < row.size()) {
                colWidths[c] = std::max(colWidths[c], row[c].size());
            }
        }
    }

    std::string result;
    auto appendDivider = [&]() {
        result.push_back('+');
        for (size_t c = 0; c < cols; ++c) {
            result.append(colWidths[c] + 2, '-');
            result.push_back('+');
        }
        result.push_back('\n');
    };

    appendDivider();

    // 表头
    result.push_back('|');
    for (size_t c = 0; c < cols; ++c) {
        std::string val = (c < table.headers.size()) ? table.headers[c] : "";
        result.push_back(' ');
        result.append(val);
        if (val.size() < colWidths[c]) {
            result.append(colWidths[c] - val.size(), ' ');
        }
        result.append(" |");
    }
    result.push_back('\n');

    appendDivider();

    // 数据行
    for (const auto& row : table.rows) {
        result.push_back('|');
        for (size_t c = 0; c < cols; ++c) {
            std::string val = (c < row.size()) ? row[c] : "";
            result.push_back(' ');
            result.append(val);
            if (val.size() < colWidths[c]) {
                result.append(colWidths[c] - val.size(), ' ');
            }
            result.append(" |");
        }
        result.push_back('\n');
    }

    appendDivider();
    return result;
}

std::string TableParser::ToSpeechText(const TableData& table) {
    if (table.IsEmpty()) return "";

    std::string result;
    size_t totalRows = table.RowCount();
    result.append("识别到表格，共 ");
    result.append(std::to_string(totalRows));
    result.append(" 行数据。");

    for (size_t r = 0; r < table.rows.size(); ++r) {
        result.append("第 ");
        result.append(std::to_string(r + 1));
        result.append(" 行：");
        const auto& row = table.rows[r];
        for (size_t c = 0; c < row.size(); ++c) {
            std::string colName = (c < table.headers.size()) ? table.headers[c] : "";
            if (!colName.empty()) {
                result.append(colName);
                result.push_back(' ');
                result.append(row[c]);
            } else {
                result.append(row[c]);
            }
            if (c + 1 < row.size()) {
                result.append("，");
            }
        }
        result.append("。");
    }

    return result;
}

} // namespace LinguaAlpaca
