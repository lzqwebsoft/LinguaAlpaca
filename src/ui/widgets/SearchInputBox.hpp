#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include "../theme/Theme.hpp"
#include <functional>

namespace LinguaAlpaca::UI {

class SearchInputBox : public wxPanel {
public:
    SearchInputBox(wxWindow* parent,
                   wxWindowID id = wxID_ANY,
                   const wxString& value = "",
                   const wxString& hint = L"输入要查询的单词或短语，按回车检索...",
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxDefaultSize);
    ~SearchInputBox() override = default;

    wxString GetValue() const;
    void SetValue(const wxString& value);
    void ChangeValue(const wxString& value);
    void Clear();
    void SetHint(const wxString& hint);
    void SetFocus() override;

    wxTextCtrl* GetTextCtrl() const { return m_textCtrl; }
    void UpdateTheme();

    void SetOnClearCallback(std::function<void()> callback) {
        m_onClearCallback = std::move(callback);
    }

private:
    void InitUI(const wxString& value, const wxString& hint);
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnTextChanged(wxCommandEvent& event);
    void OnTextEnter(wxCommandEvent& event);

    wxRect GetClearBtnRect() const;

    wxTextCtrl* m_textCtrl{nullptr};
    bool m_isFocused{false};
    bool m_isHovered{false};
    bool m_isClearHovered{false};
    std::function<void()> m_onClearCallback;
};

} // namespace LinguaAlpaca::UI
