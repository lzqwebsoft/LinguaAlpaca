/**
 * LinguaAlpaca Markdown & Formula Unified Rendering Engine
 * 供内嵌 WebView (index.html) 与独立自包含 HTML 导出模板 (export.html) 共享复用
 */

(function(global) {
    'use strict';

    // 1. 初始化 marked 编译引擎配置 (GFM、自动断行、保留 HTML)
    if (typeof marked !== 'undefined') {
        marked.use({
            gfm: true,
            breaks: true
        });
    }

    // 2. 基础 HTML 实体转义
    function escapeHtmlText(str) {
        if (!str) return "";
        return str
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;");
    }

    // 3. 保护数学公式内部换行，将表格单元格公式外的 \n 统一转为 <br> 并规范化单引号
    function formatCellContent(content) {
        if (!content) return "";
        content = content.replace(/&#0*39;|&apos;|&#x0*27;/gi, "'");
        return content.replace(/(\\\[[\s\S]*?\\\]|\\\([\s\S]*?\\\)|\$\$[\s\S]*?\$\$|\$[^\$]+?\$)|(\\r\\n|\\n|\r\n|[\r\n])/g, function(m, math, nl) {
            return math ? math : "<br>";
        });
    }

    // 3. LaTeX 嵌套花括号匹配提取
    function extractBalancedBraces(str, startIdx) {
        if (startIdx >= str.length || str[startIdx] !== '{') return null;
        var depth = 0;
        for (var i = startIdx; i < str.length; i++) {
            if (str[i] === '\\') {
                i++;
                continue;
            }
            if (str[i] === '{') {
                depth++;
            } else if (str[i] === '}') {
                depth--;
                if (depth === 0) {
                    return {
                        content: str.substring(startIdx + 1, i),
                        endIdx: i
                    };
                }
            }
        }
        return null;
    }

    // 4. 剥离 LaTeX \text{} 嵌套修饰
    function unwrapLatexText(content) {
        if (!content) return "";
        var res = content.trim();
        var changed = true;
        while (changed) {
            changed = false;
            var textMatch = res.match(/^\\(?:text|textrm|mathrm)\s*\{/);
            if (textMatch) {
                var extracted = extractBalancedBraces(res, textMatch[0].length - 1);
                if (extracted && extracted.endIdx === res.length - 1) {
                    res = extracted.content.trim();
                    changed = true;
                    continue;
                }
            }
        }
        res = preprocessLatexDecorations(res);
        res = res.replace(/\\textbf\{([^{}]+)\}/g, "<strong>$1</strong>");
        res = res.replace(/\\(?:textit|emph)\{([^{}]+)\}/g, "<em>$1</em>");
        return res;
    }

    var decorationMap = {
        "underline": "single-underline",
        "uwave": "wavy-underline",
        "uline": "single-underline",
        "uuline": "double-underline",
        "dashuline": "dashed-underline",
        "dotuline": "dotted-underline",
        "sout": "strikethrough-line",
        "xout": "xout-line"
    };

    // 5. 预处理 LaTeX 专名与排版修饰 (普通下划线 \underline / \uline, 波浪下划线 \uwave 等)
    function preprocessLatexDecorations(raw) {
        if (!raw) return "";

        var cmdRegex = /\\(underline|uwave|uline|uuline|dashuline|dotuline|sout|xout)\b/g;
        var result = "";
        var lastIdx = 0;
        var match;

        while ((match = cmdRegex.exec(raw)) !== null) {
            var cmd = match[1];
            var cssClass = decorationMap[cmd];
            var cmdStart = match.index;
            var afterCmd = cmdStart + match[0].length;

            // 检查前面是否有 \( 或 $ (排除 $$)
            var hasParen = false;
            var hasDollar = false;
            var replaceStart = cmdStart;

            var prefixIdx = cmdStart - 1;
            while (prefixIdx >= 0 && (raw[prefixIdx] === ' ' || raw[prefixIdx] === '\t')) {
                prefixIdx--;
            }
            if (prefixIdx >= 1 && raw[prefixIdx - 1] === '\\' && raw[prefixIdx] === '(') {
                hasParen = true;
                replaceStart = prefixIdx - 1;
            } else if (prefixIdx >= 0 && raw[prefixIdx] === '$' && (prefixIdx === 0 || raw[prefixIdx - 1] !== '$')) {
                hasDollar = true;
                replaceStart = prefixIdx;
            }

            // 跳过命令名后的空白
            var braceIdx = afterCmd;
            while (braceIdx < raw.length && (raw[braceIdx] === ' ' || raw[braceIdx] === '\t')) {
                braceIdx++;
            }

            var innerText = "";
            var replaceEnd = afterCmd;

            if (braceIdx < raw.length && raw[braceIdx] === '{') {
                var extracted = extractBalancedBraces(raw, braceIdx);
                if (extracted) {
                    innerText = unwrapLatexText(extracted.content);
                    replaceEnd = extracted.endIdx + 1;
                } else {
                    // 容错：处理嵌套 \text{...} 但外层丢失闭合花括号的情况 (如 \underline{\text{URL} 或 \underline{\text{URL}\))
                    var innerTextMatch = raw.substring(braceIdx + 1).match(/^\\(?:text|textrm|mathrm)\s*\{/);
                    if (innerTextMatch) {
                        var innerBraceStart = braceIdx + 1 + innerTextMatch[0].length - 1;
                        var innerExtracted = extractBalancedBraces(raw, innerBraceStart);
                        if (innerExtracted) {
                            innerText = innerExtracted.content.trim();
                            replaceEnd = innerExtracted.endIdx + 1;
                            if (replaceEnd < raw.length && raw[replaceEnd] === '}') {
                                replaceEnd++;
                            }
                        }
                    } else if (hasParen) {
                        var endParenIdx = raw.indexOf("\\)", braceIdx);
                        if (endParenIdx !== -1) {
                            var innerRaw = raw.substring(braceIdx + 1, endParenIdx).trim();
                            if (innerRaw.endsWith("}")) innerRaw = innerRaw.slice(0, -1).trim();
                            innerText = unwrapLatexText(innerRaw);
                            replaceEnd = endParenIdx + 2;
                            hasParen = false;
                        }
                    } else {
                        var nextTerminator = raw.substring(braceIdx).search(/[\s,，。；！？\)\$]/);
                        var endIdx = nextTerminator !== -1 ? braceIdx + nextTerminator : raw.length;
                        var innerRaw = raw.substring(braceIdx + 1, endIdx).trim();
                        if (innerRaw.endsWith("}")) innerRaw = innerRaw.slice(0, -1).trim();
                        innerText = unwrapLatexText(innerRaw);
                        replaceEnd = endIdx;
                    }
                }
            } else {
                // 无大括号情况
                var nextWordMatch = raw.substring(braceIdx).match(/^([^\s,，。；！？\)\$]+)/);
                if (nextWordMatch) {
                    innerText = nextWordMatch[1];
                    replaceEnd = braceIdx + innerText.length;
                }
            }

            if (innerText) {
                if (hasParen) {
                    var checkParen = replaceEnd;
                    while (checkParen < raw.length && (raw[checkParen] === ' ' || raw[checkParen] === '\t')) {
                        checkParen++;
                    }
                    if (checkParen + 1 < raw.length && raw.substring(checkParen, checkParen + 2) === "\\)") {
                        replaceEnd = checkParen + 2;
                    }
                } else if (hasDollar) {
                    var checkDollar = replaceEnd;
                    while (checkDollar < raw.length && (raw[checkDollar] === ' ' || raw[checkDollar] === '\t')) {
                        checkDollar++;
                    }
                    if (checkDollar < raw.length && raw[checkDollar] === '$' && (checkDollar + 1 >= raw.length || raw[checkDollar + 1] !== '$')) {
                        replaceEnd = checkDollar + 1;
                    }
                }

                result += raw.substring(lastIdx, replaceStart);
                result += "<span class='" + cssClass + "'>" + innerText + "</span>";
                lastIdx = replaceEnd;
                cmdRegex.lastIndex = replaceEnd;
            }
        }

        result += raw.substring(lastIdx);
        return result;
    }

    // 6. PaddleOCR OTSL 结构化表格 (<fcel>, <lcel>, <ucel>, <nl>) 转换为标准 HTML 表格
    function convertOtslToHtml(otslStr) {
        if (!otslStr || !otslStr.includes("<fcel>")) {
            return otslStr;
        }

        // 1. 清理外层可能包含的多余标签 (如 <html><body><table>...</table></body></html>)
        var clean = otslStr.replace(/<\/?(?:html|body|table|tbody|thead|tr|td|th)\b[^>]*>/gi, "").trim();

        // 2. 按行分割 (<nl>)
        var rowTokens = clean.split(/<nl>/i);
        var grid = [];

        var cellRegex = /<(fcel|ecel|lcel|ucel|xcel)>([\s\S]*?)(?=(?:<(?:fcel|ecel|lcel|ucel|xcel)>|$))/gi;

        for (var i = 0; i < rowTokens.length; i++) {
            var rowStr = rowTokens[i].trim();
            if (!rowStr) continue;
            var rowCells = [];
            var match;
            cellRegex.lastIndex = 0;
            while ((match = cellRegex.exec(rowStr)) !== null) {
                rowCells.push({
                    type: match[1].toLowerCase(),
                    text: match[2].trim(),
                    rowspan: 1,
                    colspan: 1
                });
            }
            if (rowCells.length > 0) {
                grid.push(rowCells);
            }
        }

        if (grid.length === 0) return otslStr;

        // 3. 补齐网格列数 (确保每行具有相同数量的列)
        var maxCols = 0;
        for (var r = 0; r < grid.length; r++) {
            if (grid[r].length > maxCols) maxCols = grid[r].length;
        }
        for (var r = 0; r < grid.length; r++) {
            while (grid[r].length < maxCols) {
                grid[r].push({ type: 'ecel', text: '', rowspan: 1, colspan: 1 });
            }
        }

        var numRows = grid.length;
        var numCols = maxCols;

        // 4. 计算 colspan 与 rowspan (水平向右与垂直向下跨列跨行)
        for (var r = 0; r < numRows; r++) {
            for (var c = 0; c < numCols; c++) {
                var cell = grid[r][c];
                if (cell.type === 'fcel' || cell.type === 'ecel') {
                    // 向右探测横向跨列 (lcel 或 xcel)
                    var cIter = c + 1;
                    while (cIter < numCols && (grid[r][cIter].type === 'lcel' || grid[r][cIter].type === 'xcel')) {
                        cell.colspan++;
                        cIter++;
                    }

                    // 向下探测纵向跨行 (ucel 或 xcel)
                    var rIter = r + 1;
                    while (rIter < numRows && (grid[rIter][c].type === 'ucel' || grid[rIter][c].type === 'xcel')) {
                        cell.rowspan++;
                        rIter++;
                    }
                }
            }
        }

        // 5. 智能计算表头行数 (若第0行有跨行则合并行皆视为表头)
        var headerRows = 1;
        for (var c = 0; c < numCols; c++) {
            var cell = grid[0][c];
            if ((cell.type === 'fcel' || cell.type === 'ecel') && cell.rowspan > headerRows) {
                headerRows = cell.rowspan;
            }
        }

        // 6. 构造标准 HTML 表格代码
        var theadHtml = "";
        var tbodyHtml = "";

        for (var r = 0; r < numRows; r++) {
            var isHeaderRow = (r < headerRows);
            var tag = isHeaderRow ? "th" : "td";
            var rowHtml = "  <tr>\n";

            for (var c = 0; c < numCols; c++) {
                var cell = grid[r][c];
                // 仅对起始单元格 (fcel, ecel) 输出 HTML 标签，被合并的单元格跳过
                if (cell.type === 'fcel' || cell.type === 'ecel') {
                    var attrs = "";
                    if (cell.rowspan > 1) attrs += ' rowspan="' + cell.rowspan + '"';
                    if (cell.colspan > 1) attrs += ' colspan="' + cell.colspan + '"';
                    var rawCell = (cell.text || "").replace(/\\\(\s*$/, '').replace(/^\s*\\\)/, '');
                    var content = escapeHtmlText(rawCell)
                        .replace(/(?:\\\(|\$)?\^\{+([^{}]+?)\}+(?:\\\)|\$)?|(?:\\\(|\$)?\^([0-9①-⑩\*]+)(?:\\\)|\$)?/g, "<sup>$1$2</sup>");
                    var formatted = formatCellContent(content);
                    rowHtml += "    <" + tag + attrs + ">" + formatted + "</" + tag + ">\n";
                }
            }
            rowHtml += "  </tr>\n";

            if (isHeaderRow) {
                theadHtml += rowHtml;
            } else {
                tbodyHtml += rowHtml;
            }
        }

        var tableHtml = "<table>\n";
        if (theadHtml) {
            tableHtml += "<thead>\n" + theadHtml + "</thead>\n";
        }
        if (tbodyHtml) {
            tableHtml += "<tbody>\n" + tbodyHtml + "</tbody>\n";
        }
        tableHtml += "</table>";

        return tableHtml;
    }

    function preprocessOtslTables(raw) {
        if (!raw || !raw.includes("<fcel>")) {
            return raw;
        }

        var tableRegex = /(?:<html>\s*<body>\s*<table>\s*)?<(?:fcel|ecel)>[\s\S]*?(?:<nl>|$)(?:[\s\r\n]*<(?:fcel|ecel|lcel|ucel|xcel)>[\s\S]*?(?:<nl>|$))*(?:\s*<\/table>\s*<\/body>\s*<\/html>)?/gi;

        return raw.replace(tableRegex, function(match) {
            if (match.includes("<fcel>")) {
                return "\n\n" + convertOtslToHtml(match) + "\n\n";
            }
            return match;
        });
    }

    // 7. 预处理与转义伪 HTML 标签与 NLP 专用标记 (如 <s>, </s>, <\s>, <unk>, <pad>, <eos> 等)
    function sanitizeNlpTokensAndPseudoTags(str) {
        if (!str) return "";

        // A. 专门处理 <s>, </s>, <\s>, 以及常见 NLP/LLM 特殊标记
        var nlpTokenRegex = /<\/?(?:s|unk|pad|sos|eos|bos|bot|eot|cls|sep|mask|start|end|num|url|id|blank)\b[^>]*>|<\\[sS]>/gi;
        var sanitized = str.replace(nlpTokenRegex, function(match) {
            return match.replace(/</g, "&lt;").replace(/>/g, "&gt;");
        });

        // B. 对非合法 HTML 标签的任意未知伪标签进行实体转义，防止在 HTML 中被静默吞噬或破坏排版
        var validHtmlTags = /^(?:\/?(?:a|b|i|u|em|strong|small|sub|sup|span|br|hr|p|div|table|thead|tbody|tr|th|td|ul|ol|li|dl|dt|dd|blockquote|pre|code|kbd|del|details|summary|img|h[1-6]))$/i;

        sanitized = sanitized.replace(/<\/?([a-zA-Z][a-zA-Z0-9_-]*)\b([^>]*)>/g, function(fullMatch, tagName) {
            if (!validHtmlTags.test(tagName.toLowerCase())) {
                return fullMatch.replace(/</g, "&lt;").replace(/>/g, "&gt;");
            }
            return fullMatch;
        });

        return sanitized;
    }

    // 8. 清理和规范化 LaTeX 数学公式中的多余/嵌套定界符、单引号实体与不平衡花括号
    function balanceBracesSinglePass(s) {
        if (!s) return "";
        var out = "";
        var depth = 0;
        for (var i = 0; i < s.length; i++) {
            var ch = s[i];
            if (ch === '\\' && i + 1 < s.length) {
                out += ch + s[i + 1];
                i++;
                continue;
            }
            if (ch === '{') {
                depth++;
                out += '{';
            } else if (ch === '}') {
                if (depth > 0) {
                    depth--;
                    out += '}';
                }
                // depth === 0 处的游离 '}' 直接丢弃，防止 KaTeX 报 Expected 'EOF', got '}'
            } else {
                out += ch;
            }
        }
        while (depth > 0) {
            out += '}';
            depth--;
        }
        return out;
    }

    function cleanMathFormula(f) {
        if (!f) return "";
        var s = f.trim();

        // 还原 HTML 实体单引号与 Unicode prime 为 LaTeX 认可的标准 prime 符号 '
        s = s.replace(/&#0*39;|&apos;|&#x0*27;/gi, "'");
        s = s.replace(/[’′]/g, "'");

        var changed = true;
        while (changed) {
            changed = false;
            // 剥离成对外层冗余定界符
            if (s.startsWith("$$") && s.endsWith("$$") && s.length >= 4) {
                s = s.substring(2, s.length - 2).trim();
                changed = true;
            } else if (s.startsWith("\\[") && s.endsWith("\\]") && s.length >= 4) {
                s = s.substring(2, s.length - 2).trim();
                changed = true;
            } else if (s.startsWith("\\(") && s.endsWith("\\)") && s.length >= 4) {
                s = s.substring(2, s.length - 2).trim();
                changed = true;
            } else if (s.startsWith("$") && s.endsWith("$") && s.length >= 2 && !s.startsWith("$$")) {
                s = s.substring(1, s.length - 1).trim();
                changed = true;
            } else if (s.startsWith("\\begin{equation}") && s.endsWith("\\end{equation}") && s.length >= 30) {
                s = s.substring(16, s.length - 14).trim();
                changed = true;
            } else if (s.startsWith("\\begin{equation*}") && s.endsWith("\\end{equation*}") && s.length >= 32) {
                s = s.substring(17, s.length - 15).trim();
                changed = true;
            }
            // 剥离单侧残留定界符
            else if (s.startsWith("$$") || s.startsWith("\\[") || s.startsWith("\\(")) {
                s = s.substring(2).trim();
                changed = true;
            } else if (s.startsWith("$") && !s.startsWith("$$")) {
                s = s.substring(1).trim();
                changed = true;
            } else if (s.endsWith("$$") || s.endsWith("\\]") || s.endsWith("\\)")) {
                s = s.substring(0, s.length - 2).trim();
                changed = true;
            } else if (s.endsWith("$") && !s.endsWith("$$")) {
                s = s.substring(0, s.length - 1).trim();
                changed = true;
            }
        }

        // 保留末尾 \end{...} 环境结尾，对公式主体执行单趟花括号平衡
        var endEnvMatch = s.match(/\\end\{[a-zA-Z]+\*?\}\s*$/);
        if (endEnvMatch) {
            var body = s.substring(0, endEnvMatch.index);
            var suffix = s.substring(endEnvMatch.index);
            return balanceBracesSinglePass(body) + suffix;
        }
        return balanceBracesSinglePass(s);
    }

    // 9. 隔离由于空行未断开而导致 marked.js 自动将数字累加合并的相邻有序列表
    function separateAdjacentLists(text) {
        if (!text || typeof text !== 'string') return text;

        // 规范化各种形态的 HTML 注释 (包含中文输入法或 macOS 智能标点自动转换的 em-dash/en-dash 如 <!— —>)
        text = text.replace(/<![\-—–\s]+>/g, "<!-- -->");

        var lines = text.split('\n');
        var result = [];
        var inList = false;
        var lastListNum = -1;
        var blankCount = 0;

        for (var i = 0; i < lines.length; i++) {
            var line = lines[i];
            var trimmed = line.trim();

            if (trimmed === '') {
                blankCount++;
                continue;
            }

            if (trimmed === '<!-- -->' || trimmed === '<!-- separator -->') {
                // 确保 <!-- --> 前方有独立空行（至少一行空行），绝不能紧挨上一行被 marked.js 误当做 inline HTML 吞噬进 <li>
                if (result.length > 0 && result[result.length - 1] !== '') {
                    result.push('');
                }
                result.push('<!-- -->');
                result.push('');
                inList = false;
                lastListNum = -1;
                blankCount = 0;
                continue;
            }

            var match = line.match(/^(\s*)(\d+)[\.\)]\s+/);
            if (match) {
                var currentNum = parseInt(match[2], 10);
                // 当在列表中，遇到空行分隔，且新列表项编号 <= 上一项编号（例如 8->1 或 1->1）
                if (inList && blankCount > 0 && currentNum <= lastListNum) {
                    if (result.length > 0 && result[result.length - 1] !== '') {
                        result.push('');
                    }
                    result.push('<!-- -->');
                    result.push('');
                } else if (blankCount > 0) {
                    for (var b = 0; b < blankCount; b++) {
                        result.push('');
                    }
                }
                lastListNum = currentNum;
                inList = true;
            } else {
                if (blankCount > 0) {
                    for (var b = 0; b < blankCount; b++) {
                        result.push('');
                    }
                    if (!line.match(/^\s{2,}/)) {
                        inList = false;
                        lastListNum = -1;
                    }
                }
            }

            blankCount = 0;
            result.push(line);
        }

        while (blankCount > 0) {
            result.push('');
            blankCount--;
        }

        return result.join('\n');
    }

    // 轻量级 FNV-1a 64位风格双 32 位哈希指纹，避免在 JS 堆中长期滞留数 MB 原始 Markdown 副本
    function computeFastFingerprint(str) {
        if (!str) return "0:0:0";
        var h1 = 0x811c9dc5;
        var h2 = 0x9e3779b9;
        for (var i = 0; i < str.length; i++) {
            var c = str.charCodeAt(i);
            h1 ^= c;
            h1 = Math.imul(h1, 0x01000193);
            h2 ^= (c + i);
            h2 = Math.imul(h2, 0x85ebca6b);
        }
        return str.length + ":" + (h1 >>> 0).toString(36) + ":" + (h2 >>> 0).toString(36);
    }

    // 提取并渲染未包裹定界符但含 \mathrm{...} / \frac{...} 等数学宏的裸表达式
    function extractNakedMathExpressions(text, mathBlocks) {
        if (!text || !/\\(?:mathrm|mathit|mathbf|mathcal|mathbb|frac|sqrt)\s*\{/.test(text)) {
            return text;
        }
        var nakedExprRegex = /[A-Za-z0-9_\\\{\}\(\)\[\].,;:\+\-\*\/=\s]*\\(?:mathrm|mathit|mathbf|mathcal|mathbb|frac|sqrt)\s*\{[^\n<>]*?(?=\s*$|\s*[，。；！？]|<\/|<br\b)/g;
        var lines = text.split('\n');
        for (var i = 0; i < lines.length; i++) {
            var line = lines[i];
            if (!line || line.indexOf('%%CODE') !== -1 || line.indexOf('\\') === -1) {
                continue;
            }
            if (/\\(?:mathrm|mathit|mathbf|mathcal|mathbb|frac|sqrt)\s*\{/.test(line)) {
                lines[i] = line.replace(nakedExprRegex, function(m) {
                    if (m.indexOf('%%') !== -1) return m;
                    var trimmed = m.trim();
                    if (!trimmed) return m;
                    var leadLen = m.length - m.trimStart().length;
                    var trailLen = m.length - m.trimEnd().length;
                    var lead = leadLen > 0 ? m.substring(0, leadLen) : "";
                    var trail = trailLen > 0 ? m.substring(m.length - trailLen) : "";
                    var key = "%%MATHINLINE_" + mathBlocks.length + "%%";
                    mathBlocks.push({ isBlock: false, formula: trimmed });
                    return lead + key + trail;
                });
            }
        }
        return lines.join('\n');
    }

    // 渲染单个 Markdown 分块为完整 HTML 字符串（含 OTSL 表格、KaTeX 公式、图片懒加载属性注入）
    function renderChunkHtml(chunkMarkdown) {
        if (!chunkMarkdown || chunkMarkdown.trim() === '') {
            return '';
        }

        var processedMarkdown = preprocessOtslTables(chunkMarkdown);
        processedMarkdown = preprocessLatexDecorations(processedMarkdown);

        // 处理已转为 HTML 的表格单元格：统一规范化单引号与公式外换行
        processedMarkdown = processedMarkdown.replace(/(<t[dh]\b[^>]*>)([\s\S]*?)(<\/t[dh]>)/gi, function(match, openTag, cellContent, closeTag) {
            return openTag + formatCellContent(cellContent) + closeTag;
        });

        // 保护代码块免受公式标记与实体转义影响
        var codeBlocks = [];
        var text = processedMarkdown.replace(/(```[\s\S]*?```|~~~[\s\S]*?~~~)/g, function(match) {
            var key = "%%CODEBLOCK_" + codeBlocks.length + "%%";
            codeBlocks.push(match);
            return key;
        });
        text = text.replace(/(`[^`\n]+?`)/g, function(match) {
            var key = "%%CODEINLINE_" + codeBlocks.length + "%%";
            codeBlocks.push(match);
            return key;
        });

        // 保护数学公式免受 marked 转义
        var mathBlocks = [];

        // 块级公式: $$ ... $$ 或 \[ ... \] 或 \begin{...} ... \end{...}
        text = text.replace(/\$\$([\s\S]*?)\$\$/g, function(match, formula) {
            var key = "%%MATHBLOCK_" + mathBlocks.length + "%%";
            mathBlocks.push({ isBlock: true, formula: formula });
            return key;
        });
        text = text.replace(/\\\[([\s\S]*?)\\\]/g, function(match, formula) {
            var key = "%%MATHBLOCK_" + mathBlocks.length + "%%";
            mathBlocks.push({ isBlock: true, formula: formula });
            return key;
        });
        text = text.replace(/\\begin\{([a-zA-Z]+\*?)\}([\s\S]*?)\\end\{\1\}/g, function(match) {
            var key = "%%MATHBLOCK_" + mathBlocks.length + "%%";
            mathBlocks.push({ isBlock: true, formula: match });
            return key;
        });

        // 行内公式: $ ... $ 或 \( ... \)
        text = text.replace(/(^|[^\$])\$([^\$\n]+?)\$(?!\$)/g, function(match, prefix, formula) {
            var key = "%%MATHINLINE_" + mathBlocks.length + "%%";
            mathBlocks.push({ isBlock: false, formula: formula });
            return prefix + key;
        });
        text = text.replace(/\\\(([\s\S]*?)\\\)/g, function(match, formula) {
            // 保护：行内公式严禁跨越段落空行(\n\n)或 HTML 表格/块级标签，防止游离未闭合定界符贪婪吞噬后续整个文档与表格
            if (formula.indexOf('\n\n') !== -1 || /<\/?(?:table|tbody|thead|tr|td|th|div|p)\b/i.test(formula)) {
                return match;
            }
            var key = "%%MATHINLINE_" + mathBlocks.length + "%%";
            mathBlocks.push({ isBlock: false, formula: formula });
            return key;
        });

        // 兜底提取裸露但含 \mathrm{...} 等数学宏的表达式
        text = extractNakedMathExpressions(text, mathBlocks);

        // 实体转义未知伪标签与 NLP 特殊标记
        text = sanitizeNlpTokensAndPseudoTags(text);

        // 隔离由于空行未断开而导致 marked.js 自动将数字累加合并的相邻有序列表
        text = separateAdjacentLists(text);

        // 还原受保护的代码块供 marked 解析
        for (var k = 0; k < codeBlocks.length; k++) {
            var cKey = (codeBlocks[k].startsWith("`") && !codeBlocks[k].startsWith("```"))
                ? "%%CODEINLINE_" + k + "%%"
                : "%%CODEBLOCK_" + k + "%%";
            text = text.split(cKey).join(codeBlocks[k]);
        }

        // 调用 marked 进行基础 Markdown 与 HTML 解析
        var html = '';
        try {
            if (typeof marked !== 'undefined') {
                html = marked.parse(text);
            } else {
                html = '<pre>' + escapeHtmlText(text) + '</pre>';
            }
        } catch (err) {
            html = '<pre>' + escapeHtmlText(text) + '</pre>';
        }

        // 还原数学公式并调用 KaTeX 矢量渲染 (仅输出 html 以减半 DOM 节点并控制缓存上限)
        if (!global.__katexCache) {
            global.__katexCache = {};
            global.__katexCacheCount = 0;
        } else if (global.__katexCacheCount > 2500) {
            global.__katexCache = {};
            global.__katexCacheCount = 0;
        }

        for (var i = 0; i < mathBlocks.length; i++) {
            var item = mathBlocks[i];
            var placeholder = item.isBlock ? "%%MATHBLOCK_" + i + "%%" : "%%MATHINLINE_" + i + "%%";
            var renderedMath = "";
            var formulaStr = cleanMathFormula(item.formula);
            var cacheKey = (item.isBlock ? "B:" : "I:") + formulaStr;

            if (global.__katexCache[cacheKey]) {
                renderedMath = global.__katexCache[cacheKey];
            } else {
                try {
                    if (typeof katex !== 'undefined') {
                        renderedMath = katex.renderToString(formulaStr, {
                            displayMode: item.isBlock,
                            throwOnError: false,
                            trust: true,
                            output: "html",
                            macros: {
                                "\\underline": "\\htmlClass{single-underline}{#1}",
                                "\\uwave": "\\htmlClass{wavy-underline}{#1}",
                                "\\uline": "\\htmlClass{single-underline}{#1}",
                                "\\uuline": "\\htmlClass{double-underline}{#1}",
                                "\\dashuline": "\\htmlClass{dashed-underline}{#1}",
                                "\\dotuline": "\\htmlClass{dotted-underline}{#1}",
                                "\\sout": "\\htmlClass{strikethrough-line}{#1}",
                                "\\xout": "\\htmlClass{xout-line}{#1}"
                            }
                        });
                        global.__katexCache[cacheKey] = renderedMath;
                        global.__katexCacheCount++;
                    } else {
                        renderedMath = item.isBlock ? "$$" + formulaStr + "$$" : "$" + formulaStr + "$";
                    }
                } catch (e) {
                    renderedMath = '<span class="katex-error">' + escapeHtmlText(formulaStr) + '</span>';
                }
            }
            if (item.isBlock) {
                html = html.replace(new RegExp('<p>\\s*' + placeholder + '\\s*<\\/p>', 'g'), renderedMath);
            }
            html = html.split(placeholder).join(renderedMath);
        }

        // 将 table (含 GFM 表格及带属性的 HTML 表格) 包装进横向滑动容器 .table-wrapper
        html = html.replace(/(<div class=['"]table-wrapper['"]>)?(<table\b[^>]*>[\s\S]*?<\/table>)(<\/div>)?/gi, function(match, openDiv, tableHtml, closeDiv) {
            if (openDiv && closeDiv) {
                return match;
            }
            return "<div class='table-wrapper'>" + tableHtml + "</div>";
        });

        // 为图片注入原生懒加载与异步解码属性，避免超长文档在打开时将视口外所有图片解码进内存
        html = html.replace(/<img\b([^>]*)>/gi, function(match, attrs) {
            if (/loading\s*=/i.test(attrs)) {
                return match;
            }
            var trimmedAttrs = attrs.replace(/\/\s*$/, '');
            var selfClosing = /\/\s*$/.test(attrs) ? ' /' : '';
            return '<img loading="lazy" decoding="async"' + trimmedAttrs + selfClosing + '>';
        });

        return html;
    }

    // 将长文档安全拆分为多个可独立虚拟化与增量更新的分块（尊重代码块与数学公式边界）
    function splitMarkdownIntoChunks(rawMarkdown) {
        if (!rawMarkdown) return [];

        // 按页面分割线 "---" 进行分块（保留后续块的 "---" 以便 <hr> 渲染一致）
        var chunks = [];
        var lines = rawMarkdown.split(/\r?\n/);
        var currentLines = [];
        var inCodeFence = false;
        var inMathBlock = false;
        var currentLen = 0;

        for (var i = 0; i < lines.length; i++) {
            var line = lines[i];
            var trimmed = line.trim();

            if (trimmed.startsWith('```') || trimmed.startsWith('~~~')) {
                inCodeFence = !inCodeFence;
            } else if (!inCodeFence && trimmed === '$$') {
                inMathBlock = !inMathBlock;
            }

            var isPageDivider = (!inCodeFence && !inMathBlock &&
                                 trimmed === '---' &&
                                 i > 0 && lines[i - 1].trim() === '' &&
                                 i + 1 < lines.length && lines[i + 1].trim() === '');

            // 若文档没有显式分页符但单块长度超过 12000 字符，在空行处自动切分块
            var isSizeSplit = (!inCodeFence && !inMathBlock &&
                               trimmed === '' && currentLen >= 12000 &&
                               i + 1 < lines.length && lines[i + 1].trim() !== '---');

            if (isPageDivider && currentLines.length > 0) {
                chunks.push(currentLines.join('\n'));
                currentLines = [line];
                currentLen = line.length;
                continue;
            }

            if (isSizeSplit) {
                chunks.push(currentLines.join('\n'));
                currentLines = [];
                currentLen = 0;
                continue;
            }

            currentLines.push(line);
            currentLen += line.length + 1;
        }

        if (currentLines.length > 0) {
            chunks.push(currentLines.join('\n'));
        }

        return chunks;
    }

    // 确保容器绑定唯一图片点击事件委托（避免超长文档绑定数千个闭包监听）
    function ensureContainerImgDelegation(container) {
        if (!container || container._hasImgClickListener) return;
        container._hasImgClickListener = true;
        container.addEventListener('click', function(e) {
            var img = e.target && e.target.closest ? e.target.closest('img') : null;
            if (!img) return;
            e.stopPropagation();
            var originalSrc = img.getAttribute('data-original-src');
            var targetSrc = originalSrc || img.getAttribute('src') || img.src;
            if (typeof notifyImageClick === 'function') {
                notifyImageClick(targetSrc);
            } else if (targetSrc && !targetSrc.startsWith('data:')) {
                window.open(targetSrc, '_blank');
            }
        });
    }

    // 同步冲刷后台分片队列，确保 GetRenderedHtml() / TOC 等需要完整 DOM 时无遗漏
    function flushPendingRender() {
        if (global.__renderChunkTimer) {
            clearTimeout(global.__renderChunkTimer);
            global.__renderChunkTimer = null;
        }
        if (typeof global.__flushRemainingChunks === 'function') {
            var fn = global.__flushRemainingChunks;
            global.__flushRemainingChunks = null;
            fn();
        }
    }

    // 10. 核心 Markdown、OTSL 表格与数学公式渲染接口（支持按页增量复用、时间切片防卡顿、CSS 视口虚拟化）
    function renderMarkdown(rawMarkdown, baseHref, preserveScroll) {
        var getScrollY = function() {
            if (typeof window.scrollY !== 'undefined' && window.scrollY > 0) return window.scrollY;
            if (typeof window.pageYOffset !== 'undefined' && window.pageYOffset > 0) return window.pageYOffset;
            if (document.documentElement && document.documentElement.scrollTop > 0) return document.documentElement.scrollTop;
            if (document.body && document.body.scrollTop > 0) return document.body.scrollTop;
            return 0;
        };

        var restoreScroll = function(y) {
            if (y <= 0) return;
            window.scrollTo(0, y);
            if (document.documentElement) document.documentElement.scrollTop = y;
            if (document.body) document.body.scrollTop = y;
        };

        var savedScrollY = preserveScroll ? getScrollY() : 0;

        if (baseHref) {
            var baseTag = document.getElementById('baseTag');
            if (baseTag) {
                var normalizedBase = baseHref.replace(/\\/g, '/');
                if (!normalizedBase.endsWith('/')) {
                    normalizedBase += '/';
                }
                if (!normalizedBase.startsWith('file:///') && !normalizedBase.startsWith('http')) {
                    normalizedBase = 'file:///' + normalizedBase;
                }
                baseTag.href = normalizedBase;
            }
        }

        var container = document.getElementById('content');
        if (!container) return;

        ensureContainerImgDelegation(container);

        // 指纹比对：内容与路径完全一致时短路退出，杜绝无意义的 DOM 重建与大字符串内存滞留
        var rawFingerprint = computeFastFingerprint(rawMarkdown || '');
        if (container._lastRawFingerprint === rawFingerprint && container._lastBaseDir === baseHref) {
            if (preserveScroll && savedScrollY > 0) {
                restoreScroll(savedScrollY);
            }
            return;
        }

        // 中止上一次未完成的后台切片任务
        if (global.__renderChunkTimer) {
            clearTimeout(global.__renderChunkTimer);
            global.__renderChunkTimer = null;
        }
        global.__flushRemainingChunks = null;

        if (!rawMarkdown || rawMarkdown.trim() === '') {
            container._lastRawFingerprint = rawFingerprint;
            container._lastBaseDir = baseHref;
            container._isChunked = false;
            container._renderedChunkHashes = [];
            container.innerHTML = '';
            return;
        }

        var chunks = splitMarkdownIntoChunks(rawMarkdown);
        container._lastRawFingerprint = rawFingerprint;

        // 1. 短文档 / 单块快速路径：直接渲染进 #content，保持零包裹嵌套
        if (chunks.length <= 1) {
            container._isChunked = false;
            container._renderedChunkHashes = [];
            container._lastBaseDir = baseHref;
            container.innerHTML = renderChunkHtml(chunks[0] || rawMarkdown);
        } else {
            // 2. 长文档 / 多页文档路径：按页分块 + 增量前缀复用 + 时间切片分批挂载
            var chunkHashes = new Array(chunks.length);
            for (var c = 0; c < chunks.length; c++) {
                chunkHashes[c] = computeFastFingerprint(chunks[c]);
            }

            var oldHashes = (container._isChunked && container._lastBaseDir === baseHref && Array.isArray(container._renderedChunkHashes))
                ? container._renderedChunkHashes
                : [];

            // 计算前后相同的前缀页数量（末尾最后一页保留重渲染，防止在同页末尾追加内容被跳过）
            var commonPrefix = 0;
            if (oldHashes.length > 0 && container.children.length >= oldHashes.length) {
                var maxCheck = Math.min(oldHashes.length, chunkHashes.length - 1);
                while (commonPrefix < maxCheck && oldHashes[commonPrefix] === chunkHashes[commonPrefix]) {
                    commonPrefix++;
                }
            }

            if (commonPrefix === 0) {
                container.innerHTML = '';
            } else {
                while (container.children.length > commonPrefix) {
                    container.removeChild(container.lastChild);
                }
            }

            container._isChunked = true;
            container._lastBaseDir = baseHref;
            container._renderedChunkHashes = chunkHashes;

            var createChunkNode = function(mdText) {
                var section = document.createElement('section');
                section.className = 'doc-page-chunk';
                section.innerHTML = renderChunkHtml(mdText);
                return section;
            };

            // 首屏或增量批次立即同步渲染（每次最多 3 页，确保主线程阻塞 < 20ms）
            var syncEnd = Math.min(chunks.length, commonPrefix + 3);
            var syncFrag = document.createDocumentFragment();
            for (var s = commonPrefix; s < syncEnd; s++) {
                syncFrag.appendChild(createChunkNode(chunks[s]));
            }
            container.appendChild(syncFrag);

            // 若仍有剩余页，通过时间切片在后台增量挂载，彻底避免 UI 卡死
            if (syncEnd < chunks.length) {
                var nextIdx = syncEnd;
                var renderBatch = function(batchLimit) {
                    var frag = document.createDocumentFragment();
                    var count = 0;
                    while (nextIdx < chunks.length && count < batchLimit) {
                        frag.appendChild(createChunkNode(chunks[nextIdx]));
                        nextIdx++;
                        count++;
                    }
                    container.appendChild(frag);
                };

                global.__flushRemainingChunks = function() {
                    if (nextIdx < chunks.length) {
                        renderBatch(chunks.length - nextIdx);
                    }
                };

                var scheduleNextSlice = function() {
                    if (nextIdx >= chunks.length) {
                        global.__renderChunkTimer = null;
                        global.__flushRemainingChunks = null;
                        return;
                    }
                    global.__renderChunkTimer = setTimeout(function() {
                        renderBatch(4);
                        scheduleNextSlice();
                    }, 8);
                };
                scheduleNextSlice();
            }
        }

        // 恢复视口滚动高度，杜绝跳动
        if (preserveScroll && savedScrollY > 0) {
            restoreScroll(savedScrollY);
            if (typeof requestAnimationFrame === 'function') {
                requestAnimationFrame(function() {
                    restoreScroll(savedScrollY);
                    setTimeout(function() {
                        restoreScroll(savedScrollY);
                    }, 40);
                });
            } else {
                setTimeout(function() {
                    restoreScroll(savedScrollY);
                }, 40);
            }
        } else if (!preserveScroll) {
            window.scrollTo(0, 0);
            if (document.documentElement) document.documentElement.scrollTop = 0;
            if (document.body) document.body.scrollTop = 0;
        }
    }

    // 注册到全局对象供 index.html 与 export.html 直接调用
    global.escapeHtmlText = escapeHtmlText;
    global.extractBalancedBraces = extractBalancedBraces;
    global.unwrapLatexText = unwrapLatexText;
    global.preprocessLatexDecorations = preprocessLatexDecorations;
    global.convertOtslToHtml = convertOtslToHtml;
    global.preprocessOtslTables = preprocessOtslTables;
    global.sanitizeNlpTokensAndPseudoTags = sanitizeNlpTokensAndPseudoTags;
    global.cleanMathFormula = cleanMathFormula;
    global.flushPendingRender = flushPendingRender;
    global.renderMarkdown = renderMarkdown;
    global.renderDocument = renderMarkdown;

})(typeof window !== 'undefined' ? window : this);
