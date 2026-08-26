#pragma once
#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include <wx/timer.h>
#include <memory>
#include <functional>

#include "../AsyncTrackable.hpp"
#include "../theme/Theme.hpp"
#include "../../core/ModelManager.hpp"

namespace LinguaAlpaca::UI {

/**
 * @brief 现代无边框应用启动画面 (Splash Screen)
 * 
 * 在应用程序启动时显示，提供应用标识、特性介绍与异步初始化进度展示。
 * 初始化涵盖核心配置加载、StarDict 本地词典库解析与本地大模型预热。
 */
class SplashScreen : public wxFrame, public AsyncTrackable {
public:
    explicit SplashScreen(wxWindow* parent = nullptr);
    virtual ~SplashScreen();

    /**
     * @brief 启动后台异步初始化流水线
     * @param modelManager 模型管理中枢
     * @param onComplete 初始化完成（进度达 100%）后的主线程回调
     */
    void StartInitialization(
        std::shared_ptr<ModelManager> modelManager,
        std::function<void()> onComplete
    );

    /**
     * @brief 设置目标进度与状态描述（支持平滑缓动动画）
     * @param targetProgress 目标进度 (0.0f ~ 100.0f)
     * @param statusText 当前阶段状态文字
     */
    void SetProgress(float targetProgress, const wxString& statusText);

private:
    void InitUI();
    void OnPaint(wxPaintEvent& event);
    void OnAnimTimer(wxTimerEvent& event);
    void OnFinishTimer(wxTimerEvent& event);
    void RunInitPipeline();

    std::shared_ptr<ModelManager> m_modelManager;
    std::function<void()> m_onComplete;

    wxTimer m_animTimer;
    wxTimer m_finishTimer;

    float m_targetProgress{0.0f};
    float m_currentProgress{0.0f};
    wxString m_statusText;
    bool m_pipelineFinished{false};
    bool m_callbackTriggered{false};
};

} // namespace LinguaAlpaca::UI
