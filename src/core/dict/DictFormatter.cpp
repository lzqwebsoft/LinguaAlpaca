#pragma execution_character_set("utf-8")
#include "DictFormatter.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

namespace LinguaAlpaca {

namespace {

inline std::string ToLowerAscii(const std::string& str) {
    std::string res;
    res.reserve(str.size());
    for (char c : str) {
        res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return res;
}

inline uint32_t ReadUint32BigEndian(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           (static_cast<uint32_t>(p[3]));
}

} // namespace

std::string DictFormatter::Utf32ToUtf8(uint32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        if (cp >= 0xD800 && cp <= 0xDFFF) return "";
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::string DictFormatter::DecodeHtmlEntities(const std::string& text) {
    if (text.find('&') == std::string::npos) return text;

    static const std::unordered_map<std::string, std::string> kNamedEntities = {
        {"nbsp", " "},     {"amp", "&"},       {"lt", "<"},         {"gt", ">"},
        {"quot", "\""},    {"apos", "'"},      {"copy", "©"},       {"reg", "®"},
        {"trade", "™"},    {"deg", "°"},       {"plusmn", "±"},     {"times", "×"},
        {"divide", "÷"},   {"middot", "·"},    {"bull", "•"},       {"hellip", "…"},
        {"mdash", "—"},    {"ndash", "–"},     {"lsquo", "‘"},      {"rsquo", "’"},
        {"ldquo", "“"},    {"rdquo", "”"},     {"laquo", "«"},      {"raquo", "»"},
        {"lsaquo", "‹"},   {"rsaquo", "›"},    {"spades", "♠"},     {"clubs", "♣"},
        {"hearts", "♥"},   {"diams", "♦"},     {"ensp", " "},       {"emsp", "  "},
        {"thinsp", " "},   {"cent", "¢"},      {"pound", "£"},      {"yen", "¥"},
        {"euro", "€"},     {"sect", "§"},      {"para", "¶"},       {"iquest", "¿"},
        {"iexcl", "¡"},    {"micro", "µ"},     {"permil", "‰"},     {"prime", "′"},
        {"Prime", "″"},    {"ne", "≠"},        {"le", "≤"},         {"ge", "≥"},
        {"infin", "∞"},    {"radic", "√"},     {"sim", "∼"},        {"asymp", "≈"}
    };

    std::string out;
    out.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '&') {
            size_t semi = text.find(';', i + 1);
            if (semi != std::string::npos && (semi - i) <= 12) {
                std::string ent = text.substr(i + 1, semi - (i + 1));
                if (!ent.empty()) {
                    if (ent[0] == '#') {
                        // 数字实体
                        uint32_t codepoint = 0;
                        bool valid = false;
                        if ((ent.size() >= 2) && (ent[1] == 'x' || ent[1] == 'X')) {
                            // 十六进制
                            try {
                                codepoint = static_cast<uint32_t>(std::stoul(ent.substr(2), nullptr, 16));
                                valid = true;
                            } catch (...) {}
                        } else if (ent.size() >= 2) {
                            // 十进制
                            try {
                                codepoint = static_cast<uint32_t>(std::stoul(ent.substr(1), nullptr, 10));
                                valid = true;
                            } catch (...) {}
                        }

                        if (valid && codepoint > 0) {
                            if (codepoint == 160) {
                                out.push_back(' ');
                            } else {
                                out += Utf32ToUtf8(codepoint);
                            }
                            i = semi;
                            continue;
                        }
                    } else {
                        // 命名实体
                        auto it = kNamedEntities.find(ent);
                        if (it != kNamedEntities.end()) {
                            out += it->second;
                            i = semi;
                            continue;
                        }
                    }
                }
            }
        }
        out.push_back(text[i]);
    }

    return out;
}

std::string DictFormatter::NormalizeNewlinesAndTrim(const std::string& text) {
    if (text.empty()) return "";

    std::string clean;
    clean.reserve(text.size());
    int consecutiveNewlines = 0;

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                continue; // 处理 \r\n
            }
            c = '\n';
        }

        if (c == '\n') {
            consecutiveNewlines++;
            if (consecutiveNewlines <= 2) {
                clean.push_back('\n');
            }
        } else {
            consecutiveNewlines = 0;
            clean.push_back(c);
        }
    }

    // 移除首尾空白行与空格
    size_t start = clean.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = clean.find_last_not_of(" \t\n\r");
    return clean.substr(start, end - start + 1);
}

std::string DictFormatter::UnescapePlaintext(const std::string& text) {
    if (text.empty()) return "";

    std::string unescaped;
    unescaped.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            char next = text[i + 1];
            if (next == 'n') {
                unescaped.push_back('\n');
                i++;
                continue;
            } else if (next == 'r') {
                i++;
                continue;
            } else if (next == 't') {
                unescaped.push_back('\t');
                i++;
                continue;
            } else if (next == '\\') {
                unescaped.push_back('\\');
                i++;
                continue;
            } else if (next == '0') {
                unescaped.push_back('\n');
                i++;
                continue;
            }
        }
        unescaped.push_back(text[i]);
    }

    return NormalizeNewlinesAndTrim(unescaped);
}

std::string DictFormatter::FormatPhonetic(const std::string& text) {
    std::string clean = DecodeHtmlEntities(text);
    // 过滤非打印不可见控制字符（保留 UTF-8 字符）
    std::string out;
    out.reserve(clean.size());
    for (size_t i = 0; i < clean.size(); ++i) {
        unsigned char uc = static_cast<unsigned char>(clean[i]);
        if (uc >= 32 || uc > 127) {
            out.push_back(clean[i]);
        }
    }

    // 修剪首尾空白
    size_t start = out.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = out.find_last_not_of(" \t\n\r");
    std::string trimmed = out.substr(start, end - start + 1);

    // 若未包含定界符，保持原样，展示层自适应处理
    return trimmed;
}

std::string DictFormatter::FormatHtml(const std::string& html) {
    if (html.empty()) return "";

    // 1. 转义字符预解包
    std::string unescaped;
    unescaped.reserve(html.size());
    for (size_t i = 0; i < html.size(); ++i) {
        if (html[i] == '\\' && i + 1 < html.size()) {
            char next = html[i + 1];
            if (next == 'n') {
                unescaped.push_back('\n');
                i++;
                continue;
            } else if (next == 'r') {
                i++;
                continue;
            } else if (next == 't') {
                unescaped.push_back('\t');
                i++;
                continue;
            } else if (next == '\\') {
                unescaped.push_back('\\');
                i++;
                continue;
            }
        }
        unescaped.push_back(html[i]);
    }

    std::string out;
    out.reserve(unescaped.size());

    bool inTag = false;
    std::string currentTag;
    bool skipContent = false;
    std::string skipUntilTag;

    int listDepth = 0;
    int listIndex = 0;
    bool inOrderedList = false;

    for (size_t i = 0; i < unescaped.size(); ++i) {
        // 过滤 HTML 注释 <!-- ... -->
        if (unescaped.compare(i, 4, "<!--") == 0) {
            size_t commentEnd = unescaped.find("-->", i + 4);
            if (commentEnd != std::string::npos) {
                i = commentEnd + 2;
                continue;
            }
        }

        char c = unescaped[i];

        if (c == '<') {
            inTag = true;
            currentTag.clear();
            continue;
        }

        if (c == '>') {
            inTag = false;
            std::string lowerTag = ToLowerAscii(currentTag);

            // 提取纯标签名
            bool isClose = false;
            size_t nameStart = 0;
            while (nameStart < lowerTag.size() && (lowerTag[nameStart] == ' ' || lowerTag[nameStart] == '/')) {
                if (lowerTag[nameStart] == '/') isClose = true;
                nameStart++;
            }
            size_t sp = lowerTag.find_first_of(" \t\n\r/>", nameStart);
            std::string tagName = (sp != std::string::npos) ? lowerTag.substr(nameStart, sp - nameStart) : lowerTag.substr(nameStart);

            // 跳过 style 与 script
            if (tagName == "script" || tagName == "style") {
                if (!isClose) {
                    skipContent = true;
                    skipUntilTag = tagName;
                } else if (skipContent && tagName == skipUntilTag) {
                    skipContent = false;
                    skipUntilTag.clear();
                }
                continue;
            }

            if (skipContent) continue;

            // 块级标签排版映射
            if (tagName == "br") {
                out.push_back('\n');
            } else if (tagName == "p" || tagName == "div") {
                out.push_back('\n');
            } else if (tagName == "hr") {
                out.append("\n────────────────\n");
            } else if (tagName == "h1" || tagName == "h2" || tagName == "h3" ||
                       tagName == "h4" || tagName == "h5" || tagName == "h6") {
                if (!isClose) {
                    out.append("\n\n【");
                } else {
                    out.append("】\n");
                }
            } else if (tagName == "ul") {
                if (!isClose) {
                    listDepth++;
                    inOrderedList = false;
                } else if (listDepth > 0) {
                    listDepth--;
                }
                out.push_back('\n');
            } else if (tagName == "ol") {
                if (!isClose) {
                    listDepth++;
                    inOrderedList = true;
                    listIndex = 1;
                } else if (listDepth > 0) {
                    listDepth--;
                }
                out.push_back('\n');
            } else if (tagName == "li") {
                if (!isClose) {
                    out.push_back('\n');
                    for (int d = 1; d < listDepth; ++d) out.append("  ");
                    if (inOrderedList) {
                        out.append(std::to_string(listIndex++) + ". ");
                    } else {
                        out.append(" • ");
                    }
                }
            } else if (tagName == "dt") {
                if (!isClose) out.append("\n▸ ");
            } else if (tagName == "dd") {
                if (!isClose) out.append("\n   ");
            } else if (tagName == "blockquote") {
                if (!isClose) out.append("\n  │ ");
            } else if (tagName == "tr") {
                out.push_back('\n');
            } else if (tagName == "td" || tagName == "th") {
                if (!isClose) {
                    out.append(" | ");
                }
            }
            continue;
        }

        if (inTag) {
            currentTag.push_back(c);
        } else if (!skipContent) {
            out.push_back(c);
        }
    }

    std::string decoded = DecodeHtmlEntities(out);
    return NormalizeNewlinesAndTrim(decoded);
}

std::string DictFormatter::FormatXdxf(const std::string& xdxf) {
    if (xdxf.empty()) return "";

    std::string out;
    out.reserve(xdxf.size());

    bool inTag = false;
    std::string currentTag;

    for (size_t i = 0; i < xdxf.size(); ++i) {
        char c = xdxf[i];

        if (c == '<') {
            inTag = true;
            currentTag.clear();
            continue;
        }

        if (c == '>') {
            inTag = false;
            std::string lowerTag = ToLowerAscii(currentTag);

            bool isClose = false;
            size_t nameStart = 0;
            while (nameStart < lowerTag.size() && (lowerTag[nameStart] == ' ' || lowerTag[nameStart] == '/')) {
                if (lowerTag[nameStart] == '/') isClose = true;
                nameStart++;
            }
            size_t sp = lowerTag.find_first_of(" \t\n\r/>", nameStart);
            std::string tagName = (sp != std::string::npos) ? lowerTag.substr(nameStart, sp - nameStart) : lowerTag.substr(nameStart);

            if (tagName == "tr") {
                // 音标标签
                if (!isClose) out.append(" [");
                else out.append("] ");
            } else if (tagName == "pos") {
                // 词性标签
                if (!isClose) out.append("\n[");
                else out.append("] ");
            } else if (tagName == "dtrn" || tagName == "def") {
                // 释义标签
                if (!isClose) out.append("\n • ");
            } else if (tagName == "ex") {
                // 例句标签
                if (!isClose) out.append("\n  【例】 ");
            } else if (tagName == "co") {
                // 注释
                if (!isClose) out.append(" (");
                else out.append(") ");
            } else if (tagName == "abbr") {
                if (!isClose) out.append(" [");
                else out.append("] ");
            } else if (tagName == "rref") {
                if (!isClose) out.append(" -> ");
            } else if (tagName == "blockquote") {
                if (!isClose) out.append("\n  │ ");
            } else if (tagName == "br" || tagName == "p") {
                out.push_back('\n');
            }
            continue;
        }

        if (inTag) {
            currentTag.push_back(c);
        } else {
            out.push_back(c);
        }
    }

    std::string decoded = DecodeHtmlEntities(out);
    return NormalizeNewlinesAndTrim(decoded);
}

std::string DictFormatter::FormatPango(const std::string& pango) {
    if (pango.empty()) return "";

    std::string out;
    out.reserve(pango.size());

    bool inTag = false;
    for (char c : pango) {
        if (c == '<') {
            inTag = true;
            continue;
        }
        if (c == '>') {
            inTag = false;
            continue;
        }
        if (!inTag) {
            out.push_back(c);
        }
    }

    std::string decoded = DecodeHtmlEntities(out);
    return NormalizeNewlinesAndTrim(decoded);
}

std::string DictFormatter::FormatKingsoft(const std::string& xml) {
    if (xml.empty()) return "";

    std::string out;
    out.reserve(xml.size());

    bool inTag = false;
    std::string currentTag;

    for (size_t i = 0; i < xml.size(); ++i) {
        char c = xml[i];

        if (c == '<') {
            inTag = true;
            currentTag.clear();
            continue;
        }

        if (c == '>') {
            inTag = false;
            std::string lowerTag = ToLowerAscii(currentTag);

            bool isClose = false;
            size_t nameStart = 0;
            while (nameStart < lowerTag.size() && (lowerTag[nameStart] == ' ' || lowerTag[nameStart] == '/')) {
                if (lowerTag[nameStart] == '/') isClose = true;
                nameStart++;
            }
            size_t sp = lowerTag.find_first_of(" \t\n\r/>", nameStart);
            std::string tagName = (sp != std::string::npos) ? lowerTag.substr(nameStart, sp - nameStart) : lowerTag.substr(nameStart);

            if (tagName == "pos") {
                if (!isClose) out.append("\n[");
                else out.append("] ");
            } else if (tagName == "pron" || tagName == "phonetic") {
                if (!isClose) out.append(" [");
                else out.append("] ");
            } else if (tagName == "def") {
                if (!isClose) out.append("\n  ");
            } else if (tagName == "sent") {
                if (!isClose) out.append("\n");
            } else if (tagName == "orig") {
                if (!isClose) out.append("  【例】 ");
            } else if (tagName == "trans") {
                if (!isClose) out.append("\n       ");
            } else if (tagName == "phrase") {
                if (!isClose) out.append("\n【短语】 ");
            }
            continue;
        }

        if (inTag) {
            currentTag.push_back(c);
        } else {
            out.push_back(c);
        }
    }

    std::string decoded = DecodeHtmlEntities(out);
    return NormalizeNewlinesAndTrim(decoded);
}

std::string DictFormatter::FormatMediaWiki(const std::string& wiki) {
    if (wiki.empty()) return "";

    std::string out;
    out.reserve(wiki.size());

    // 逐行解析 MediaWiki 语法
    std::istringstream iss(wiki);
    std::string line;

    while (std::getline(iss, line)) {
        // 去除 \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // 1. 标题语法 == Header == 或 === Sub ===
        if (line.size() >= 4 && line.front() == '=' && line.back() == '=') {
            size_t start = line.find_first_not_of('=');
            size_t end = line.find_last_not_of('=');
            if (start != std::string::npos && end != std::string::npos && end >= start) {
                std::string header = line.substr(start, end - start + 1);
                // trim
                size_t hs = header.find_first_not_of(" \t");
                size_t he = header.find_last_not_of(" \t");
                if (hs != std::string::npos) header = header.substr(hs, he - hs + 1);

                if (start >= 3) {
                    out.append("\n▪ " + header + "\n");
                } else {
                    out.append("\n【" + header + "】\n");
                }
                continue;
            }
        }

        // 2. 列表项 * 或 #
        if (!line.empty() && (line[0] == '*' || line[0] == '#')) {
            size_t itemStart = line.find_first_not_of(" \t", 1);
            std::string itemText = (itemStart != std::string::npos) ? line.substr(itemStart) : "";
            if (line[0] == '*') {
                out.append(" • " + itemText + "\n");
            } else {
                out.append(" 1. " + itemText + "\n");
            }
            continue;
        }

        // 3. 内部加粗 ''' 与斜体 '' 剥离
        std::string processed;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line.compare(i, 3, "'''") == 0) {
                i += 2;
                continue;
            }
            if (line.compare(i, 2, "''") == 0) {
                i += 1;
                continue;
            }
            // 4. 内链 [[Target|Text]] 或 [[Target]]
            if (line.compare(i, 2, "[[") == 0) {
                size_t linkEnd = line.find("]]", i + 2);
                if (linkEnd != std::string::npos) {
                    std::string linkContent = line.substr(i + 2, linkEnd - (i + 2));
                    size_t pipe = linkContent.find('|');
                    if (pipe != std::string::npos) {
                        processed += linkContent.substr(pipe + 1);
                    } else {
                        processed += linkContent;
                    }
                    i = linkEnd + 1;
                    continue;
                }
            }
            // 5. 模板 {{...}}
            if (line.compare(i, 2, "{{") == 0) {
                size_t tmplEnd = line.find("}}", i + 2);
                if (tmplEnd != std::string::npos) {
                    i = tmplEnd + 1;
                    continue;
                }
            }

            processed.push_back(line[i]);
        }

        out.append(processed + "\n");
    }

    std::string decoded = DecodeHtmlEntities(out);
    return NormalizeNewlinesAndTrim(decoded);
}

void DictFormatter::Format(const std::string& rawData,
                           const std::string& sameTypeSequence,
                           std::string& outPhonetic,
                           std::string& outDefinition) {
    outPhonetic.clear();
    outDefinition.clear();

    if (rawData.empty()) return;

    if (!sameTypeSequence.empty()) {
        if (sameTypeSequence == "m") {
            outDefinition = UnescapePlaintext(rawData);
        } else if (sameTypeSequence == "g") {
            outDefinition = FormatPango(rawData);
        } else if (sameTypeSequence == "h" || sameTypeSequence == "H") {
            outDefinition = FormatHtml(rawData);
        } else if (sameTypeSequence == "x" || sameTypeSequence == "X") {
            outDefinition = FormatXdxf(rawData);
        } else if (sameTypeSequence == "k") {
            outDefinition = FormatKingsoft(rawData);
        } else if (sameTypeSequence == "w") {
            outDefinition = FormatMediaWiki(rawData);
        } else if (sameTypeSequence == "t" || sameTypeSequence == "y") {
            outPhonetic = FormatPhonetic(rawData);
        } else if (sameTypeSequence == "tm" || sameTypeSequence == "ym") {
            // 前半段音标，后半段释义
            size_t nullPos = rawData.find('\0');
            if (nullPos != std::string::npos) {
                outPhonetic = FormatPhonetic(rawData.substr(0, nullPos));
                outDefinition = UnescapePlaintext(rawData.substr(nullPos + 1));
            } else {
                outDefinition = UnescapePlaintext(rawData);
            }
        } else if (sameTypeSequence == "th" || sameTypeSequence == "yh") {
            size_t nullPos = rawData.find('\0');
            if (nullPos != std::string::npos) {
                outPhonetic = FormatPhonetic(rawData.substr(0, nullPos));
                outDefinition = FormatHtml(rawData.substr(nullPos + 1));
            } else {
                outDefinition = FormatHtml(rawData);
            }
        } else if (sameTypeSequence == "tx" || sameTypeSequence == "yx") {
            size_t nullPos = rawData.find('\0');
            if (nullPos != std::string::npos) {
                outPhonetic = FormatPhonetic(rawData.substr(0, nullPos));
                outDefinition = FormatXdxf(rawData.substr(nullPos + 1));
            } else {
                outDefinition = FormatXdxf(rawData);
            }
        } else {
            // 通用复合序列逐段解析
            size_t pos = 0;
            std::string defAcc;
            for (char type : sameTypeSequence) {
                if (pos >= rawData.size()) break;
                if (std::islower(static_cast<unsigned char>(type))) {
                    // null-terminated string
                    size_t strEnd = rawData.find('\0', pos);
                    std::string seg;
                    if (strEnd != std::string::npos) {
                        seg = rawData.substr(pos, strEnd - pos);
                        pos = strEnd + 1;
                    } else {
                        seg = rawData.substr(pos);
                        pos = rawData.size();
                    }

                    if (type == 't' || type == 'y') {
                        if (outPhonetic.empty()) outPhonetic = FormatPhonetic(seg);
                    } else if (type == 'm') {
                        if (!defAcc.empty()) defAcc.push_back('\n');
                        defAcc += UnescapePlaintext(seg);
                    } else if (type == 'h') {
                        if (!defAcc.empty()) defAcc.push_back('\n');
                        defAcc += FormatHtml(seg);
                    } else if (type == 'x') {
                        if (!defAcc.empty()) defAcc.push_back('\n');
                        defAcc += FormatXdxf(seg);
                    } else if (type == 'g') {
                        if (!defAcc.empty()) defAcc.push_back('\n');
                        defAcc += FormatPango(seg);
                    } else if (type == 'k') {
                        if (!defAcc.empty()) defAcc.push_back('\n');
                        defAcc += FormatKingsoft(seg);
                    } else if (type == 'w') {
                        if (!defAcc.empty()) defAcc.push_back('\n');
                        defAcc += FormatMediaWiki(seg);
                    }
                } else {
                    // 大写类型：4字节大端长度 + 数据
                    if (pos + 4 <= rawData.size()) {
                        uint32_t len = ReadUint32BigEndian(reinterpret_cast<const uint8_t*>(&rawData[pos]));
                        pos += 4;
                        if (pos + len <= rawData.size()) {
                            std::string seg = rawData.substr(pos, len);
                            pos += len;
                            if (type == 'H') {
                                if (!defAcc.empty()) defAcc.push_back('\n');
                                defAcc += FormatHtml(seg);
                            } else if (type == 'X') {
                                if (!defAcc.empty()) defAcc.push_back('\n');
                                defAcc += FormatXdxf(seg);
                            }
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                }
            }
            outDefinition = defAcc.empty() ? UnescapePlaintext(rawData) : defAcc;
        }
    } else {
        // 动态单条目多类型混排解析
        size_t pos = 0;
        std::string defAcc;
        while (pos < rawData.size()) {
            char type = rawData[pos++];
            if (std::islower(static_cast<unsigned char>(type))) {
                size_t strEnd = rawData.find('\0', pos);
                std::string seg;
                if (strEnd != std::string::npos) {
                    seg = rawData.substr(pos, strEnd - pos);
                    pos = strEnd + 1;
                } else {
                    seg = rawData.substr(pos);
                    pos = rawData.size();
                }

                if (type == 't' || type == 'y') {
                    if (outPhonetic.empty()) outPhonetic = FormatPhonetic(seg);
                } else if (type == 'm') {
                    if (!defAcc.empty()) defAcc.push_back('\n');
                    defAcc += UnescapePlaintext(seg);
                } else if (type == 'h') {
                    if (!defAcc.empty()) defAcc.push_back('\n');
                    defAcc += FormatHtml(seg);
                } else if (type == 'x') {
                    if (!defAcc.empty()) defAcc.push_back('\n');
                    defAcc += FormatXdxf(seg);
                } else if (type == 'g') {
                    if (!defAcc.empty()) defAcc.push_back('\n');
                    defAcc += FormatPango(seg);
                } else if (type == 'k') {
                    if (!defAcc.empty()) defAcc.push_back('\n');
                    defAcc += FormatKingsoft(seg);
                } else if (type == 'w') {
                    if (!defAcc.empty()) defAcc.push_back('\n');
                    defAcc += FormatMediaWiki(seg);
                }
            } else {
                // 大写类型：4字节大端长度 + 数据
                if (pos + 4 <= rawData.size()) {
                    uint32_t len = ReadUint32BigEndian(reinterpret_cast<const uint8_t*>(&rawData[pos]));
                    pos += 4;
                    if (pos + len <= rawData.size()) {
                        std::string seg = rawData.substr(pos, len);
                        pos += len;
                        if (type == 'H') {
                            if (!defAcc.empty()) defAcc.push_back('\n');
                            defAcc += FormatHtml(seg);
                        } else if (type == 'X') {
                            if (!defAcc.empty()) defAcc.push_back('\n');
                            defAcc += FormatXdxf(seg);
                        }
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }
        }
        outDefinition = defAcc.empty() ? UnescapePlaintext(rawData) : defAcc;
    }

    // 智能兜底音标与星号核心词提取：若未通过独立数据段提取到音标，尝试在释义开头探测 [*][音标] 或 /*/音标/
    if (outPhonetic.empty() && !outDefinition.empty()) {
        size_t startOffset = 0;
        // 跳过开头的空白
        while (startOffset < outDefinition.size() && 
               (outDefinition[startOffset] == ' ' || outDefinition[startOffset] == '\t' || outDefinition[startOffset] == '\r' || outDefinition[startOffset] == '\n')) {
            startOffset++;
        }

        // 探测星级/重点词标记 (如 *, **, ***, ★, ▲, #)
        bool hasStar = false;
        while (startOffset < outDefinition.size()) {
            char ch = outDefinition[startOffset];
            if (ch == '*' || ch == '#' || ch == '^') {
                hasStar = true;
                startOffset++;
            } else if (startOffset + 3 <= outDefinition.size() &&
                       (outDefinition.compare(startOffset, 3, "★") == 0 ||
                        outDefinition.compare(startOffset, 3, "▲") == 0 ||
                        outDefinition.compare(startOffset, 3, "☆") == 0)) {
                hasStar = true;
                startOffset += 3;
            } else if (ch == ' ' || ch == '\t') {
                startOffset++;
            } else {
                break;
            }
        }

        if (startOffset < outDefinition.size()) {
            char openChar = outDefinition[startOffset];
            if (openChar == '[' || openChar == '/') {
                char closeChar = (openChar == '[') ? ']' : '/';
                size_t endPos = outDefinition.find(closeChar, startOffset + 1);
                if (endPos != std::string::npos && endPos - startOffset <= 50) {
                    std::string rawPhonetic = outDefinition.substr(startOffset, endPos - startOffset + 1);
                    if (hasStar) {
                        outPhonetic = "★ " + rawPhonetic;
                    } else {
                        outPhonetic = rawPhonetic;
                    }

                    // 剔除释义开头的音标及星号部分以避免在正文中重复显示
                    size_t nextPos = outDefinition.find_first_not_of(" \t\n\r", endPos + 1);
                    if (nextPos != std::string::npos) {
                        outDefinition = outDefinition.substr(nextPos);
                    } else {
                        outDefinition.clear();
                    }
                }
            }
        }
    }
}

std::vector<DictTextSegment> DictFormatter::BuildRichTextSegments(const std::vector<DictSearchResult>& results) {
    std::vector<DictTextSegment> segments;

    static const std::vector<std::string> posPrefixes = {
        // 括号包裹形式
        "[adj.]", "[adv.]", "[prep.]", "[conj.]", "[pron.]", "[num.]", "[art.]", "[int.]", "[abbr.]", "[aux.]",
        "[interj.]", "[sing.]", "[vt.]", "[vi.]", "[pl.]", "[pp.]", "[pt.]", "[v.]", "[n.]", "[a.]", "[ad.]",
        "[adj]", "[adv]", "[prep]", "[conj]", "[pron]", "[num]", "[art]", "[int]", "[abbr]", "[aux]",
        "[vt]", "[vi]", "[pl]", "[v]", "[n]", "[a]", "[ad]",
        // 中文词性
        "【及物】", "【不及物】", "【名】", "【动】", "【形】", "【副】", "【介】", "【连】", "【代】", "【数】", "【量】",
        "【冠】", "【感】", "【助】", "【缩】", "【口】", "【俗】", "【叹】",
        // 点号+空格形式 (优先长词缀)
        "interj. ", "sing. ", "prep. ", "conj. ", "pron. ", "abbr. ", "aux. ", "art. ", "num. ",
        "adj. ", "adv. ", "vt. ", "vi. ", "pl. ", "pp. ", "pt. ", "int. ",
        "a. ", "ad. ", "n. ", "v. ",
        // 紧凑点号形式
        "interj.", "sing.", "prep.", "conj.", "pron.", "abbr.", "aux.", "art.", "num.",
        "adj.", "adv.", "vt.", "vi.", "pl.", "pp.", "pt.", "int.",
        "a.", "ad.", "n.", "v."
    };

    for (size_t d = 0; d < results.size(); ++d) {
        const auto& r = results[d];

        // 词典间高雅分割线与段落间距
        if (d > 0) {
            segments.push_back({ DictTextStyle::Divider, "\n\n────────────────────────────────────────────────\n\n" });
        }

        // 1. 词典名称标头 (如: 📖 朗道英汉字典5.0)
        segments.push_back({ DictTextStyle::DictHeader, " 📖 " + r.dictName + " " });
        segments.push_back({ DictTextStyle::Default, "\n" });

        // 2. 本词典附带的音标 (如果有)
        if (!r.phonetic.empty()) {
            segments.push_back({ DictTextStyle::Phonetic, "   🗣 " + r.phonetic + "\n" });
        }

        // 3. 逐行排版
        std::istringstream iss(r.definition);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();

            // 去除行首尾空白
            size_t end = line.find_last_not_of(" \t\r\n");
            if (end != std::string::npos) {
                line = line.substr(0, end + 1);
            } else {
                line.clear();
            }

            if (line.empty()) {
                segments.push_back({ DictTextStyle::Default, "\n" });
                continue;
            }

            // A. 检查词性前缀 (如: n. / [n.] / a. / adj. / v. / adv. 等)
            bool matchedPos = false;
            for (const auto& posTag : posPrefixes) {
                if (line.compare(0, posTag.size(), posTag) == 0) {
                    segments.push_back({ DictTextStyle::PartOfSpeech, " " + posTag + " " });
                    std::string rest = line.substr(posTag.length());
                    size_t start = rest.find_first_not_of(" \t");
                    if (start != std::string::npos) rest = rest.substr(start);
                    segments.push_back({ DictTextStyle::Default, rest + "\n" });
                    matchedPos = true;
                    break;
                }
            }
            if (matchedPos) continue;

            // B. 检查通用分类/专业领域/条目标签 (如: 【化】 / 【医】 / 【经】 / 【例】 / 【短语】 / 【用法】 / 【同义词】)
            if (line.compare(0, 3, "【") == 0) {
                size_t closePos = line.find("】", 3);
                if (closePos != std::string::npos && closePos <= 30) {
                    std::string tag = line.substr(0, closePos + 3);
                    std::string rest = line.substr(closePos + 3);
                    size_t start = rest.find_first_not_of(" \t");
                    if (start != std::string::npos) {
                        rest = rest.substr(start);
                    } else {
                        rest.clear();
                    }

                    segments.push_back({ DictTextStyle::Tag, " " + tag + " " });

                    // 例句与短语采用次级弱化样式；专业学科（化/医/经/计等）及用法释义采用正文样式
                    if (tag.find("例") != std::string::npos || tag.find("短语") != std::string::npos ||
                        tag.find("句") != std::string::npos) {
                        segments.push_back({ DictTextStyle::Example, rest + "\n" });
                    } else {
                        segments.push_back({ DictTextStyle::Default, rest + "\n" });
                    }
                    continue;
                }
            }

            // C. 检查例句或参考 (如: e.g. / Ex. / eg.)
            if (line.compare(0, 4, "e.g.") == 0 || line.compare(0, 3, "eg.") == 0 ||
                line.compare(0, 3, "Ex.") == 0 || line.compare(0, 3, "ex.") == 0) {
                segments.push_back({ DictTextStyle::Example, "   " + line + "\n" });
                continue;
            }

            // D. 检查序号条目 (如: 1. / 2. / (1) / (2) / ① / ② / [1] / [2])
            bool matchedNum = false;
            if (line.size() >= 2 && (std::isdigit(static_cast<unsigned char>(line[0])) ||
                line.front() == '(' || line.front() == '[' ||
                (static_cast<unsigned char>(line[0]) == 0xE2 && static_cast<unsigned char>(line[1]) == 0x91))) {
                
                size_t splitIdx = std::string::npos;
                size_t dotPos = line.find('.');
                size_t parenPos = line.find(')');
                size_t bracketPos = line.find(']');

                if (dotPos != std::string::npos && dotPos <= 3) {
                    splitIdx = dotPos + 1;
                } else if (parenPos != std::string::npos && parenPos <= 4) {
                    splitIdx = parenPos + 1;
                } else if (bracketPos != std::string::npos && bracketPos <= 4) {
                    splitIdx = bracketPos + 1;
                } else if (static_cast<unsigned char>(line[0]) == 0xE2 && static_cast<unsigned char>(line[1]) == 0x91) {
                    splitIdx = 3;
                }

                if (splitIdx != std::string::npos && splitIdx < line.size()) {
                    std::string numPart = line.substr(0, splitIdx);
                    std::string textPart = line.substr(splitIdx);
                    segments.push_back({ DictTextStyle::NumberedItem, " " + numPart });
                    segments.push_back({ DictTextStyle::Default, textPart + "\n" });
                    matchedNum = true;
                }
            }
            if (matchedNum) continue;

            // E. 普通释义文本
            segments.push_back({ DictTextStyle::Default, line + "\n" });
        }
    }

    return segments;
}

} // namespace LinguaAlpaca
