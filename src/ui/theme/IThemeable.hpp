#pragma once
#pragma execution_character_set("utf-8")

namespace LinguaAlpaca::UI {

/// 主题响应接口：自定义复合控件与视图继承此接口即可参与主题树的自动递归更新
class IThemeable {
public:
    virtual ~IThemeable() = default;
    virtual void UpdateTheme() = 0;
};

} // namespace LinguaAlpaca::UI
