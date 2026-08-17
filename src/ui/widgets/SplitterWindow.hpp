#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include <wx/splitter.h>

namespace LinguaAlpaca::UI {

/**
 * @brief 自定义主题化 SplitterWindow，支持精美手柄绘制、悬停高亮与平滑实时拖拽
 */
class SplitterWindow : public wxSplitterWindow {
public:
    SplitterWindow(wxWindow* parent, wxWindowID id = wxID_ANY,
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxDefaultSize,
                   long style = wxSP_LIVE_UPDATE | wxSP_NOBORDER);
    ~SplitterWindow() override = default;

    void DrawSash(wxDC& dc) override;

protected:
    void OnEnterSash() override;
    void OnLeaveSash() override;
};

} // namespace LinguaAlpaca::UI
