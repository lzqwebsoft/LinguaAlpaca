#pragma once
#pragma execution_character_set("utf-8")

#include "../theme/Theme.hpp"
#include <functional>
#include <memory>
#include <string>
#include <wx/wx.h>

namespace LinguaAlpaca::UI {

/**
 * @brief 现代化自定义文本输入框组件 (CustomInputBox)
 *
 * 统一承载全局单行搜索、路径配置、多行详情等各类输入展示场景。
 *
 * 特性：
 * - 优雅的白色圆角卡片底色 (8_dip) 与悬停/焦点状态微动效 (Accent Border)
 * - 支持可选的前缀矢量图标 (Prefix SVG Icon)
 * - 支持可交互的快速清除按钮 ('x' Clear Button) 与自定义清除回调
 * - 支持单行输入 (Single-line) 与多行展示 (Multiline) 模式
 * - 完备的 wxTextCtrl 委托接口 (GetValue, SetValue, ChangeValue, SetHint,
 * Clear, SetEditable 等)
 * - Per-Monitor High-DPI 缩放与深浅主题自适应
 */
class CustomInputBox : public wxPanel {
public:
  CustomInputBox(wxWindow *parent, wxWindowID id = wxID_ANY,
                 const wxString &value = wxEmptyString,
                 const wxString &hint = wxEmptyString,
                 const wxPoint &pos = wxDefaultPosition,
                 const wxSize &size = wxDefaultSize, long style = 0);
  ~CustomInputBox() override = default;

  // 核心文本内容接口
  wxString GetValue() const;
  void SetValue(const wxString &value);
  void ChangeValue(const wxString &value);
  void Clear();
  void SetHint(const wxString &hint);
  wxString GetHint() const { return m_hint; }
  void AppendText(const wxString &text);

  // 编辑状态控制
  void SetEditable(bool editable);
  bool IsEditable() const;
  void SetReadOnly(bool readOnly) { SetEditable(!readOnly); }

  // 焦点与选区
  void SetFocus() override;
  void SetInsertionPoint(long pos);
  void SetInsertionPointEnd();
  long GetInsertionPoint() const;
  void SelectAll();

  // 前缀图标、清除按钮与外观配置
  void SetPrefixIcon(const char *svgContent, const wxSize &size = dip(16, 16));
  void SetShowClearButton(bool show);
  void SetOnClearCallback(std::function<void()> callback) {
    m_onClearCallback = std::move(callback);
  }
  void SetCornerRadius(double radius) {
    m_cornerRadius = radius;
    Refresh();
  }

  // 样式与主题刷新
  bool SetFont(const wxFont &font) override;
  bool SetBackgroundColour(const wxColour &colour) override;
  bool SetForegroundColour(const wxColour &colour) override;
  void UpdateTheme();

  // 访问内部原生 wxTextCtrl 实例
  wxTextCtrl *GetInnerCtrl() const { return m_textCtrl; }
  wxTextCtrl *GetTextCtrl() const { return m_textCtrl; }

private:
  void InitUI(const wxString &value, const wxString &hint, long style);
  void OnPaint(wxPaintEvent &event);
  void OnSize(wxSizeEvent &event);
  void OnMouseMove(wxMouseEvent &event);
  void OnMouseLeave(wxMouseEvent &event);
  void OnLeftDown(wxMouseEvent &event);
  void OnTextChanged(wxCommandEvent &event);
  void OnTextEnter(wxCommandEvent &event);

  wxRect GetClearBtnRect() const;
  void RebuildLayout();

  wxTextCtrl *m_textCtrl{nullptr};
  wxBoxSizer *m_mainSizer{nullptr};
  wxBoxSizer *m_inputSizer{nullptr};

  const char *m_prefixSvg{nullptr};
  wxSize m_prefixIconSize{dip(16, 16)};
  bool m_showClearButton{true};
  double m_cornerRadius{8.0};
  wxString m_hint;

  bool m_isFocused{false};
  bool m_isHovered{false};
  bool m_isClearHovered{false};
  bool m_isMultiline{false};

  std::function<void()> m_onClearCallback;
};

} // namespace LinguaAlpaca::UI
