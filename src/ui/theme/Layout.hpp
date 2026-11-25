#pragma once
#pragma execution_character_set("utf-8")

#include "Dpi.hpp"

namespace LinguaAlpaca::UI::Layout {

/// 页面顶栏/底栏外边距 (与 OcrView 对齐)
inline int PagePadding() { return 20_dip; }

/// 主内容区卡片外边距 (与 OcrView 对齐)
inline int ContentPadding() { return 12_dip; }

/// 并列卡片之间的间距
inline int CardGap() { return 12_dip; }

/// 标题图标与标题文本之间的间距
inline int TitleIconGap() { return 10_dip; }

} // namespace LinguaAlpaca::UI::Layout
