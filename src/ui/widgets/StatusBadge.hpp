#pragma once
#include <wx/wx.h>
#include "../theme/Theme.hpp"
#include "core/Types.hpp"

namespace LinguaAlpaca::UI {

/**
 * @brief 现代化状态胶囊徽章组件 (StatusBadge)
 *
 * 特性：
 * - 纯矢量 GraphicsContext 绘制，支持精美自适应胶囊药丸 (Pill) 圆角背景与高质感边框
 * - 无任何子控件嵌套，彻底杜绝双层背景/内层直角色块与刷新伪影
 * - 根据服务状态 (Ready, Loading, Unconfigured, Offline, Error) 自动应用语义化背景与文本配色
 * - 尺寸随文本动态自适应，并内置安全边界裁剪防溢出
 */
class StatusBadge : public wxControl {
public:
    StatusBadge(wxWindow* parent, wxWindowID id = wxID_ANY,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize);

    void SetStatus(ServerHealthState state, const wxString& customLabel = wxEmptyString);
    void SetStatus(const wxString& label, const wxColour& fg, const wxColour& bg, const wxColour& border = wxNullColour);

    wxString GetStatusLabel() const { return m_label; }

protected:
    wxSize DoGetBestSize() const override;

private:
    void OnPaint(wxPaintEvent& event);

    wxString m_label{L"●  未配置"};
    wxColour m_fgColour{wxColour(220, 38, 38)};
    wxColour m_bgColour{wxColour(254, 242, 242)};
    wxColour m_borderColour{wxColour(254, 202, 202)};
};

} // namespace LinguaAlpaca::UI
