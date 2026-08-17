#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include <memory>
#include <string>
#include "ScrollBar.hpp"

namespace LinguaAlpaca::UI {

/**
 * @brief 自定义现代化文本框组件 (TextCtrl)
 * 
 * 特性：
 * - 嵌入定制的极简圆角高亮滑动条 (ScrollBar)，完全替代系统陈旧原生滚动条
 * - 支持可编辑模式 (Editable) 与只读模式 (Read-Only)
 * - 深度集成 ThemePalette 主题色，支持背景色、前景色、字体自定义与实时主题刷新
 * - 提供完整的文本处理接口 (SetValue, GetValue, AppendText, Clear, WriteText 等)
 */
class TextCtrl : public wxPanel {
public:
    TextCtrl(wxWindow* parent, wxWindowID id = wxID_ANY,
             const wxString& value = wxEmptyString,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             long style = 0);
    ~TextCtrl() override = default;

    // 核心文本内容接口
    void SetValue(const wxString& value);
    wxString GetValue() const;
    void AppendText(const wxString& text);
    void Clear();
    void WriteText(const wxString& text);
    void SetHint(const wxString& hint);
    bool SetDefaultStyle(const wxTextAttr& style);
    void ShowPosition(long pos);
    long GetLastPosition() const;

    // 编辑状态控制 (可编辑与不可编辑)
    void SetEditable(bool editable);
    bool IsEditable() const;
    void SetReadOnly(bool readOnly) { SetEditable(!readOnly); }

    // 光标与选区
    void SetInsertionPoint(long pos);
    void SetInsertionPointEnd();
    long GetInsertionPoint() const;
    void SelectAll();

    // 样式与主题
    bool SetFont(const wxFont& font) override;
    bool SetBackgroundColour(const wxColour& colour) override;
    bool SetForegroundColour(const wxColour& colour) override;

    // 获取内部原生 wxTextCtrl 实例
    wxTextCtrl* GetInnerCtrl() const { return m_textCtrl; }

    // 滚动同步
    void ScrollToLine(int targetLine);
    void UpdateScrollInfo();

private:
    void InitUI(const wxString& value, long style);
    void OnMouseWheel(wxMouseEvent& event);
    void OnMiddleDown(wxMouseEvent& event);
    void OnMiddleUp(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);

    wxTextCtrl* m_textCtrl{nullptr};
    ScrollBar* m_scrollBar{nullptr};
    bool m_isEditable{true};
    bool m_isMiddleDragging{false};
    int m_middleDragStartY{0};
    int m_middleDragStartFirstLine{0};
};

} // namespace LinguaAlpaca::UI
