#pragma execution_character_set("utf-8")
#include "MarkdownFormatter.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace LinguaAlpaca {

std::string_view MarkdownFormatter::Trim(std::string_view s) {
    return TrimRight(TrimLeft(s));
}

std::string_view MarkdownFormatter::TrimLeft(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
        ++start;
    }
    return s.substr(start);
}

std::string_view MarkdownFormatter::TrimRight(std::string_view s) {
    if (s.empty()) return s;
    size_t end = s.size();
    while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        --end;
    }
    return s.substr(0, end);
}

bool MarkdownFormatter::IsHorizontalRule(std::string_view line) {
    std::string_view trimmed = Trim(line);
    if (trimmed.size() < 3) return false;

    char firstChar = trimmed[0];
    if (firstChar != '-' && firstChar != '*' && firstChar != '_') return false;

    size_t count = 0;
    for (char c : trimmed) {
        if (c == firstChar) {
            ++count;
        } else if (c != ' ' && c != '\t') {
            return false;
        }
    }
    return count >= 3;
}

bool MarkdownFormatter::IsHeading(std::string_view line, int& outLevel, std::string_view& outContent) {
    std::string_view trimmed = TrimLeft(line);
    if (trimmed.empty() || trimmed[0] != '#') return false;

    size_t i = 0;
    while (i < trimmed.size() && trimmed[i] == '#' && i < 6) {
        ++i;
    }

    if (i > 0 && i < trimmed.size() && (trimmed[i] == ' ' || trimmed[i] == '\t')) {
        outLevel = static_cast<int>(i);
        outContent = Trim(trimmed.substr(i));
        return true;
    }
    return false;
}

bool MarkdownFormatter::IsOrderedList(std::string_view line, std::string_view& outNum, std::string_view& outContent, size_t& outIndent) {
    size_t indent = 0;
    while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
        ++indent;
    }

    size_t i = indent;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
        ++i;
    }

    if (i > indent && i < line.size() && (line[i] == '.' || line[i] == ')') && (i + 1 < line.size()) && (line[i + 1] == ' ' || line[i + 1] == '\t')) {
        outIndent = indent;
        outNum = line.substr(indent, (i - indent + 1));
        outContent = TrimLeft(line.substr(i + 2));
        return true;
    }
    return false;
}

bool MarkdownFormatter::IsUnorderedList(std::string_view line, std::string_view& outContent, size_t& outIndent) {
    size_t indent = 0;
    while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
        ++indent;
    }

    if (indent + 1 < line.size()) {
        char c = line[indent];
        char next = line[indent + 1];
        if ((c == '-' || c == '*' || c == '+') && (next == ' ' || next == '\t')) {
            if (!IsHorizontalRule(line)) {
                outIndent = indent;
                outContent = TrimLeft(line.substr(indent + 2));
                return true;
            }
        }
    }
    return false;
}

namespace {

uint32_t DecodeUtf8(std::string_view s, size_t pos, size_t* outLen = nullptr) {
    if (pos >= s.size()) {
        if (outLen) *outLen = 0;
        return 0;
    }
    unsigned char c = static_cast<unsigned char>(s[pos]);
    if (c < 0x80) {
        if (outLen) *outLen = 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && pos + 1 < s.size()) {
        if (outLen) *outLen = 2;
        return ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[pos + 1]) & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && pos + 2 < s.size()) {
        if (outLen) *outLen = 3;
        return ((c & 0x0F) << 12) |
               ((static_cast<unsigned char>(s[pos + 1]) & 0x3F) << 6) |
               (static_cast<unsigned char>(s[pos + 2]) & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && pos + 3 < s.size()) {
        if (outLen) *outLen = 4;
        return ((c & 0x07) << 18) |
               ((static_cast<unsigned char>(s[pos + 1]) & 0x3F) << 12) |
               ((static_cast<unsigned char>(s[pos + 2]) & 0x3F) << 6) |
               (static_cast<unsigned char>(s[pos + 3]) & 0x3F);
    }
    if (outLen) *outLen = 1;
    return c;
}

uint32_t GetCodePointBefore(std::string_view s, size_t pos) {
    if (pos == 0 || pos > s.size()) return 0;
    size_t prev = pos - 1;
    while (prev > 0 && (static_cast<unsigned char>(s[prev]) & 0xC0) == 0x80) {
        --prev;
    }
    return DecodeUtf8(s, prev);
}

uint32_t GetCodePointAt(std::string_view s, size_t pos) {
    if (pos >= s.size()) return 0;
    return DecodeUtf8(s, pos);
}

bool IsUnicodeWhitespace(uint32_t cp) {
    if (cp == 0) return true; // 字符串首尾边界视为空白边界
    if (cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' || cp == '\v' || cp == '\f') return true;
    if (cp == 0x00A0 || cp == 0x1680) return true;
    if (cp >= 0x2000 && cp <= 0x200A) return true;
    if (cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000) return true;
    return false;
}

bool IsUnicodePunctuation(uint32_t cp) {
    // ASCII 标点符号
    if ((cp >= 0x21 && cp <= 0x2F) ||
        (cp >= 0x3A && cp <= 0x40) ||
        (cp >= 0x5B && cp <= 0x60) ||
        (cp >= 0x7B && cp <= 0x7E)) {
        return true;
    }
    // Unicode 通用标点与引号（如 “ ”, ‘ ’, —, … 等）
    if (cp >= 0x2010 && cp <= 0x2027) return true;
    if (cp >= 0x2030 && cp <= 0x205E) return true;
    // CJK 中日韩标点（如 、 。 〈 〉 《 》 「 」 『 』 【 】 等，0x3000 全角空格已归为空白）
    if (cp >= 0x3001 && cp <= 0x303F) return true;
    // CJK 兼容与小符号
    if (cp >= 0xFE10 && cp <= 0xFE1F) return true;
    if (cp >= 0xFE30 && cp <= 0xFE6F) return true;
    // 全角 ASCII 标点变体（如 ！，：；“”‘’（）等）
    if ((cp >= 0xFF01 && cp <= 0xFF0F) ||
        (cp >= 0xFF1A && cp <= 0xFF20) ||
        (cp >= 0xFF3B && cp <= 0xFF40) ||
        (cp >= 0xFF5B && cp <= 0xFF65) ||
        (cp >= 0xFFE0 && cp <= 0xFFEE)) {
        return true;
    }
    return false;
}

void GetFlankingInfo(std::string_view line, size_t delimStart, size_t delimLen,
                     bool& isLeftFlanking, bool& isRightFlanking) {
    uint32_t charBefore = GetCodePointBefore(line, delimStart);
    uint32_t charAfter = GetCodePointAt(line, delimStart + delimLen);

    bool afterIsWhitespace = IsUnicodeWhitespace(charAfter);
    bool afterIsPunct = IsUnicodePunctuation(charAfter);
    bool beforeIsWhitespace = IsUnicodeWhitespace(charBefore);
    bool beforeIsPunct = IsUnicodePunctuation(charBefore);

    // CommonMark 规范 §6.2 侧翼判定
    isLeftFlanking = !afterIsWhitespace && (!afterIsPunct || (beforeIsWhitespace || beforeIsPunct));
    isRightFlanking = !beforeIsWhitespace && (!beforeIsPunct || (afterIsWhitespace || afterIsPunct));
}

bool CanOpenEmphasis(std::string_view line, size_t delimStart, size_t delimLen, char delimChar) {
    bool isLeft = false, isRight = false;
    GetFlankingInfo(line, delimStart, delimLen, isLeft, isRight);
    if (delimChar == '_') {
        // 下划线强调规则：仅在左侧翼且（非右侧翼或前接标点）时可作为开头，严格杜绝变量名内部强调（如 processor_kwargs）
        uint32_t charBefore = GetCodePointBefore(line, delimStart);
        return isLeft && (!isRight || IsUnicodePunctuation(charBefore));
    }
    return isLeft;
}

bool CanCloseEmphasis(std::string_view line, size_t delimStart, size_t delimLen, char delimChar) {
    bool isLeft = false, isRight = false;
    GetFlankingInfo(line, delimStart, delimLen, isLeft, isRight);
    if (delimChar == '_') {
        // 下划线闭合规则：仅在右侧翼且（非左侧翼或后接标点）时可作为闭合，严格杜绝变量名内部闭合（如 text_kwargs）
        uint32_t charAfter = GetCodePointAt(line, delimStart + delimLen);
        return isRight && (!isLeft || IsUnicodePunctuation(charAfter));
    }
    return isRight;
}

} // namespace

void MarkdownFormatter::ParseInlineElements(std::string_view line,
                                           std::vector<MarkdownSegment>& outSegments,
                                           MarkdownStyle baseStyle) {
    size_t i = 0;
    size_t plainStart = 0;

    auto flushPlain = [&](size_t end) {
        if (end > plainStart) {
            outSegments.push_back({ baseStyle, std::string(line.substr(plainStart, end - plainStart)) });
        }
        plainStart = end;
    };

    while (i < line.size()) {
        // 1. 行内代码 `code`
        if (line[i] == '`') {
            size_t closePos = line.find('`', i + 1);
            if (closePos != std::string_view::npos) {
                flushPlain(i);
                std::string codeText = std::string(line.substr(i + 1, closePos - (i + 1)));
                outSegments.push_back({ MarkdownStyle::InlineCode, " " + codeText + " " });
                i = closePos + 1;
                plainStart = i;
                continue;
            }
        }

        // 2. 粗斜体 ***text*** 或 ___text___
        if (i + 2 < line.size() &&
            ((line[i] == '*' && line[i + 1] == '*' && line[i + 2] == '*') ||
             (line[i] == '_' && line[i + 1] == '_' && line[i + 2] == '_'))) {
            char delimChar = line[i];
            if (CanOpenEmphasis(line, i, 3, delimChar)) {
                std::string_view delim = line.substr(i, 3);
                size_t searchPos = i + 3;
                size_t closePos = std::string_view::npos;
                while (searchPos + 2 < line.size()) {
                    size_t found = line.find(delim, searchPos);
                    if (found == std::string_view::npos) break;
                    if (CanCloseEmphasis(line, found, 3, delimChar) && found > i + 3) {
                        closePos = found;
                        break;
                    }
                    searchPos = found + 1;
                }

                if (closePos != std::string_view::npos) {
                    flushPlain(i);
                    std::string content = std::string(line.substr(i + 3, closePos - (i + 3)));
                    outSegments.push_back({ MarkdownStyle::BoldItalic, content });
                    i = closePos + 3;
                    plainStart = i;
                    continue;
                }
            }
        }

        // 3. 加粗 **text** 或 __text__
        if (i + 1 < line.size() &&
            ((line[i] == '*' && line[i + 1] == '*') ||
             (line[i] == '_' && line[i + 1] == '_'))) {
            char delimChar = line[i];
            // 避免将 *** 误当成 ** 处理
            if (i + 2 < line.size() && line[i + 2] == delimChar) {
                ++i;
                continue;
            }

            if (CanOpenEmphasis(line, i, 2, delimChar)) {
                std::string_view delim = line.substr(i, 2);
                size_t searchPos = i + 2;
                size_t closePos = std::string_view::npos;
                while (searchPos + 1 < line.size()) {
                    size_t found = line.find(delim, searchPos);
                    if (found == std::string_view::npos) break;
                    // 避免将 *** 误当作 ** 闭合
                    if (found + 2 < line.size() && line[found + 2] == delimChar) {
                        searchPos = found + 3;
                        continue;
                    }
                    if (CanCloseEmphasis(line, found, 2, delimChar) && found > i + 2) {
                        closePos = found;
                        break;
                    }
                    searchPos = found + 1;
                }

                if (closePos != std::string_view::npos) {
                    flushPlain(i);
                    std::string content = std::string(line.substr(i + 2, closePos - (i + 2)));
                    outSegments.push_back({ MarkdownStyle::Bold, content });
                    i = closePos + 2;
                    plainStart = i;
                    continue;
                }
            }
        }

        // 4. 删除线 ~~text~~
        if (i + 1 < line.size() && line[i] == '~' && line[i + 1] == '~') {
            size_t closePos = line.find("~~", i + 2);
            if (closePos != std::string_view::npos && closePos > i + 2) {
                std::string content = std::string(line.substr(i + 2, closePos - (i + 2)));
                if (!content.empty() && content.front() != ' ' && content.back() != ' ') {
                    flushPlain(i);
                    outSegments.push_back({ MarkdownStyle::Strikethrough, content });
                    i = closePos + 2;
                    plainStart = i;
                    continue;
                }
            }
        }

        // 5. 斜体 *text* 或 _text_
        if ((line[i] == '*' || line[i] == '_')) {
            char delimChar = line[i];
            // 若紧随相同符号，表明这是多字符 delimiter run (如 ** 或 __)，跳过
            if (i + 1 < line.size() && line[i + 1] == delimChar) {
                ++i;
                continue;
            }

            if (CanOpenEmphasis(line, i, 1, delimChar)) {
                size_t searchPos = i + 1;
                size_t closePos = std::string_view::npos;
                while (searchPos < line.size()) {
                    size_t found = line.find(delimChar, searchPos);
                    if (found == std::string_view::npos) break;
                    // 若紧随相同符号，跳过更长 delimiter run (例如 ** 或 ***)
                    if (found + 1 < line.size() && line[found + 1] == delimChar) {
                        size_t runLen = 2;
                        while (found + runLen < line.size() && line[found + runLen] == delimChar) {
                            ++runLen;
                        }
                        searchPos = found + runLen;
                        continue;
                    }
                    if (CanCloseEmphasis(line, found, 1, delimChar) && found > i + 1) {
                        closePos = found;
                        break;
                    }
                    searchPos = found + 1;
                }

                if (closePos != std::string_view::npos) {
                    flushPlain(i);
                    std::string content = std::string(line.substr(i + 1, closePos - (i + 1)));
                    outSegments.push_back({ MarkdownStyle::Italic, content });
                    i = closePos + 1;
                    plainStart = i;
                    continue;
                }
            }
        }

        // 6. 链接 [text](url)
        if (line[i] == '[') {
            size_t closeBracket = line.find(']', i + 1);
            if (closeBracket != std::string_view::npos && closeBracket + 1 < line.size() && line[closeBracket + 1] == '(') {
                size_t closeParen = line.find(')', closeBracket + 2);
                if (closeParen != std::string_view::npos) {
                    flushPlain(i);
                    std::string linkText = std::string(line.substr(i + 1, closeBracket - (i + 1)));
                    outSegments.push_back({ MarkdownStyle::LinkText, linkText });
                    i = closeParen + 1;
                    plainStart = i;
                    continue;
                }
            }
        }

        ++i;
    }

    flushPlain(line.size());
}

std::vector<MarkdownSegment> MarkdownFormatter::Parse(const std::string& markdown) {
    std::vector<MarkdownSegment> segments;
    if (markdown.empty()) return segments;

    std::string_view view(markdown);
    size_t pos = 0;
    bool inCodeBlock = false;
    std::string codeBlockContent;

    while (pos < view.size()) {
        size_t nextNl = view.find('\n', pos);
        std::string_view rawLine = (nextNl != std::string_view::npos) ?
            view.substr(pos, nextNl - pos) : view.substr(pos);
        pos = (nextNl != std::string_view::npos) ? (nextNl + 1) : view.size();

        // 移除行末 \r
        if (!rawLine.empty() && rawLine.back() == '\r') {
            rawLine = rawLine.substr(0, rawLine.size() - 1);
        }

        std::string_view trimmedLeft = TrimLeft(rawLine);

        // 1. 代码块检测 ```
        if (trimmedLeft.size() >= 3 && trimmedLeft.substr(0, 3) == "```") {
            if (inCodeBlock) {
                // 结束代码块
                inCodeBlock = false;
                if (!codeBlockContent.empty() && codeBlockContent.back() == '\n') {
                    codeBlockContent.pop_back();
                }
                segments.push_back({ MarkdownStyle::CodeBlock, codeBlockContent + "\n" });
                codeBlockContent.clear();
            } else {
                // 进入代码块
                inCodeBlock = true;
                codeBlockContent.clear();
            }
            continue;
        }

        if (inCodeBlock) {
            codeBlockContent += std::string(rawLine) + "\n";
            continue;
        }

        // 2. 空行
        if (trimmedLeft.empty()) {
            segments.push_back({ MarkdownStyle::Default, "\n" });
            continue;
        }

        // 3. 水平分割线
        if (IsHorizontalRule(rawLine)) {
            segments.push_back({ MarkdownStyle::Divider, "────────────────────────────────────────\n" });
            continue;
        }

        // 4. 标题 (# ~ ######)
        int headLevel = 0;
        std::string_view headContent;
        if (IsHeading(rawLine, headLevel, headContent)) {
            MarkdownStyle hStyle = MarkdownStyle::Heading1;
            switch (headLevel) {
                case 1: hStyle = MarkdownStyle::Heading1; break;
                case 2: hStyle = MarkdownStyle::Heading2; break;
                case 3: hStyle = MarkdownStyle::Heading3; break;
                case 4: hStyle = MarkdownStyle::Heading4; break;
                case 5: hStyle = MarkdownStyle::Heading5; break;
                case 6: hStyle = MarkdownStyle::Heading6; break;
                default: hStyle = MarkdownStyle::Heading1; break;
            }
            ParseInlineElements(headContent, segments, hStyle);
            segments.push_back({ hStyle, "\n" });
            continue;
        }

        // 5. 引用块 (> )
        if (trimmedLeft.size() >= 1 && trimmedLeft[0] == '>') {
            std::string_view quoteContent = Trim(trimmedLeft.substr(1));
            segments.push_back({ MarkdownStyle::BlockquoteBar, "▍ " });
            ParseInlineElements(quoteContent, segments, MarkdownStyle::Blockquote);
            segments.push_back({ MarkdownStyle::Blockquote, "\n" });
            continue;
        }

        // 6. 无序列表 (- / * / +)
        std::string_view listContent;
        size_t listIndent = 0;
        if (IsUnorderedList(rawLine, listContent, listIndent)) {
            std::string indentStr(listIndent > 0 ? std::string(listIndent, ' ') : "");
            segments.push_back({ MarkdownStyle::Default, indentStr });
            segments.push_back({ MarkdownStyle::ListBullet, "• " });
            ParseInlineElements(listContent, segments, MarkdownStyle::Default);
            segments.push_back({ MarkdownStyle::Default, "\n" });
            continue;
        }

        // 7. 有序列表 (1. 2. 等)
        std::string_view listNum;
        if (IsOrderedList(rawLine, listNum, listContent, listIndent)) {
            std::string indentStr(listIndent > 0 ? std::string(listIndent, ' ') : "");
            segments.push_back({ MarkdownStyle::Default, indentStr });
            segments.push_back({ MarkdownStyle::ListNumber, std::string(listNum) + " " });
            ParseInlineElements(listContent, segments, MarkdownStyle::Default);
            segments.push_back({ MarkdownStyle::Default, "\n" });
            continue;
        }

        // 8. 普通段落与行内 Markdown
        ParseInlineElements(rawLine, segments, MarkdownStyle::Default);
        segments.push_back({ MarkdownStyle::Default, "\n" });
    }

    // 处理未闭合的代码块
    if (inCodeBlock && !codeBlockContent.empty()) {
        if (codeBlockContent.back() == '\n') {
            codeBlockContent.pop_back();
        }
        segments.push_back({ MarkdownStyle::CodeBlock, codeBlockContent + "\n" });
    }

    return segments;
}

std::string MarkdownFormatter::StripMarkdown(const std::string& markdown) {
    auto segments = Parse(markdown);
    std::string result;
    result.reserve(markdown.size());
    for (const auto& seg : segments) {
        if (seg.style == MarkdownStyle::BlockquoteBar) {
            continue;
        }
        result += seg.text;
    }
    return result;
}

} // namespace LinguaAlpaca
