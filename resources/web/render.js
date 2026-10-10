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
            .replace(/"/g, "&quot;")
            .replace(/'/g, "&#039;");
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
        "uwave": "wavy-underline",
        "uline": "single-underline",
        "uuline": "double-underline",
        "dashuline": "dashed-underline",
        "dotuline": "dotted-underline",
        "sout": "strikethrough-line",
        "xout": "xout-line"
    };

    // 5. 预处理 LaTeX 专名与排版修饰 (波浪下划线 \uwave, 普通下划线 \uline 等)
    function preprocessLatexDecorations(raw) {
        if (!raw) return "";

        var cmdRegex = /\\(uwave|uline|uuline|dashuline|dotuline|sout|xout)\b/g;
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
                    var content = escapeHtmlText(cell.text).replace(/\r?\n/g, "<br>");
                    rowHtml += "    <" + tag + attrs + ">" + content + "</" + tag + ">\n";
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

    // 8. 清理和规范化 LaTeX 数学公式中的多余/嵌套定界符
    function cleanMathFormula(f) {
        if (!f) return "";
        var s = f.trim();
        var changed = true;
        while (changed) {
            changed = false;
            // 剥离外层冗余的 $$ ... $$
            if (s.startsWith("$$") && s.endsWith("$$") && s.length >= 4) {
                s = s.substring(2, s.length - 2).trim();
                changed = true;
            }
            // 剥离外层冗余的 \[ ... \]
            else if (s.startsWith("\\[") && s.endsWith("\\]") && s.length >= 4) {
                s = s.substring(2, s.length - 2).trim();
                changed = true;
            }
            // 剥离外层冗余的 \( ... \)
            else if (s.startsWith("\\(") && s.endsWith("\\)") && s.length >= 4) {
                s = s.substring(2, s.length - 2).trim();
                changed = true;
            }
            // 剥离外层冗余的单美元符号 $ ... $
            else if (s.startsWith("$") && s.endsWith("$") && s.length >= 2 && !s.startsWith("$$")) {
                s = s.substring(1, s.length - 1).trim();
                changed = true;
            }
            // 剥离 \begin{equation} ... \end{equation}
            else if (s.startsWith("\\begin{equation}") && s.endsWith("\\end{equation}") && s.length >= 30) {
                s = s.substring(16, s.length - 14).trim();
                changed = true;
            } else if (s.startsWith("\\begin{equation*}") && s.endsWith("\\end{equation*}") && s.length >= 32) {
                s = s.substring(17, s.length - 15).trim();
                changed = true;
            }
        }
        return s;
    }

    // 9. 核心 Markdown、OTSL 表格与数学公式渲染接口
    function renderMarkdown(rawMarkdown, baseHref, preserveScroll) {
        var savedScrollY = preserveScroll ? window.scrollY : 0;

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

        if (!rawMarkdown || rawMarkdown.trim() === '') {
            container.innerHTML = '';
            return;
        }

        var processedMarkdown = preprocessOtslTables(rawMarkdown);
        processedMarkdown = preprocessLatexDecorations(processedMarkdown);

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
            var key = "%%MATHINLINE_" + mathBlocks.length + "%%";
            mathBlocks.push({ isBlock: false, formula: formula });
            return key;
        });

        // 实体转义未知伪标签与 NLP 特殊标记
        text = sanitizeNlpTokensAndPseudoTags(text);

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

        // 还原数学公式并调用 KaTeX 矢量渲染 (并注入 ulem 宏增强)
        for (var i = 0; i < mathBlocks.length; i++) {
            var item = mathBlocks[i];
            var placeholder = item.isBlock ? "%%MATHBLOCK_" + i + "%%" : "%%MATHINLINE_" + i + "%%";
            var renderedMath = "";
            var formulaStr = cleanMathFormula(item.formula);
            try {
                if (typeof katex !== 'undefined') {
                    renderedMath = katex.renderToString(formulaStr, {
                        displayMode: item.isBlock,
                        throwOnError: false,
                        trust: true,
                        output: "htmlAndMathml",
                        macros: {
                            "\\uwave": "\\htmlClass{wavy-underline}{#1}",
                            "\\uline": "\\htmlClass{single-underline}{#1}",
                            "\\uuline": "\\htmlClass{double-underline}{#1}",
                            "\\dashuline": "\\htmlClass{dashed-underline}{#1}",
                            "\\dotuline": "\\htmlClass{dotted-underline}{#1}",
                            "\\sout": "\\htmlClass{strikethrough-line}{#1}",
                            "\\xout": "\\htmlClass{xout-line}{#1}"
                        }
                    });
                } else {
                    renderedMath = item.isBlock ? "$$" + formulaStr + "$$" : "$" + formulaStr + "$";
                }
            } catch (e) {
                renderedMath = '<span class="katex-error">' + escapeHtmlText(formulaStr) + '</span>';
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

        container.innerHTML = html;

        // 绑定图片点击事件 (支持桌面端协议与浏览器全屏预览)
        var imgs = container.querySelectorAll('img');
        for (var j = 0; j < imgs.length; j++) {
            (function(img) {
                img.addEventListener('click', function(e) {
                    e.stopPropagation();
                    var originalSrc = img.getAttribute('data-original-src');
                    var targetSrc = originalSrc || img.src;
                    if (typeof notifyImageClick === 'function') {
                        notifyImageClick(targetSrc);
                    } else if (targetSrc && !targetSrc.startsWith('data:')) {
                        window.open(targetSrc, '_blank');
                    }
                });
            })(imgs[j]);
        }

        // 恢复视口滚动高度，杜绝跳动
        if (preserveScroll && savedScrollY > 0) {
            window.scrollTo(0, savedScrollY);
        } else if (!preserveScroll) {
            window.scrollTo(0, 0);
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
    global.renderMarkdown = renderMarkdown;
    global.renderDocument = renderMarkdown;

})(typeof window !== 'undefined' ? window : this);
