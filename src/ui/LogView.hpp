#pragma once
#include <memory>
#include <vector>
#include "AsyncTrackable.hpp"
#include "core/Logger.hpp"
#include "core/Config.hpp"
#include "widgets/CustomButton.hpp"
#include "widgets/TextCtrl.hpp"

namespace LinguaAlpaca::UI {

class LogView : public wxPanel, public AsyncTrackable {
public:
    LogView(wxWindow* parent,
            std::shared_ptr<ConfigManager> configManager,
            wxWindowID id = wxID_ANY);
    ~LogView() override;

    void UpdateTheme();
    void AppendLogMessage(const LogMessage& msg);
    void ReloadLogs();

private:
    void InitUI();
    void OnClear(wxCommandEvent& event);
    void OnCopyAll(wxCommandEvent& event);
    void OnOpenLogDir(wxCommandEvent& event);
    void OnFilterChanged(wxCommandEvent& event);
    void OnAutoScrollToggled(wxCommandEvent& event);

    std::shared_ptr<ConfigManager> m_configManager;
    size_t m_listenerId{0};
    int m_filterLevel{-1}; // -1: 全部, 0: Debug, 1: Info, 2: Warning, 3: Error
    bool m_autoScroll{true};

    // UI Elements
    wxPanel* m_headerPanel{nullptr};
    wxStaticBitmap* m_titleIcon{nullptr};
    wxStaticText* m_titleText{nullptr};
    wxChoice* m_filterChoice{nullptr};
    wxCheckBox* m_autoScrollCheck{nullptr};
    CustomButton* m_clearBtn{nullptr};
    CustomButton* m_copyBtn{nullptr};
    CustomButton* m_openDirBtn{nullptr};

    wxPanel* m_cardContainer{nullptr};
    TextCtrl* m_logTextCtrl{nullptr};
};

} // namespace LinguaAlpaca::UI
