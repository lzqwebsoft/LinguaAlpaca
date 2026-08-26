#pragma execution_character_set("utf-8")
#include "SplashScreen.hpp"
#include "../theme/IconManager.hpp"
#include "../theme/AppIcons.hpp"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/filename.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace LinguaAlpaca::UI {

	SplashScreen::SplashScreen(wxWindow* parent)
		: wxFrame(parent, wxID_ANY, "", wxDefaultPosition, dip(460, 196),
			wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxBORDER_NONE),
		m_animTimer(this),
		m_finishTimer(this),
		m_statusText(L"正在启动 LinguaAlpaca...") {

		SetClientSize(dip(460, 196));
		SetMinClientSize(dip(460, 196));
		SetMaxClientSize(dip(460, 196));

		InitUI();
	}

	SplashScreen::~SplashScreen() {
		if (m_animTimer.IsRunning()) {
			m_animTimer.Stop();
		}
		if (m_finishTimer.IsRunning()) {
			m_finishTimer.Stop();
		}
	}

	void SplashScreen::InitUI() {
		SetBackgroundStyle(wxBG_STYLE_PAINT);

		Bind(wxEVT_PAINT, &SplashScreen::OnPaint, this);

		// 绑定平滑插值动画定时器 (约 60fps, 16ms 间隔)
		Bind(wxEVT_TIMER, &SplashScreen::OnAnimTimer, this, m_animTimer.GetId());
		// 绑定完成后的延时切换定时器
		Bind(wxEVT_TIMER, &SplashScreen::OnFinishTimer, this, m_finishTimer.GetId());

		m_animTimer.Start(16);
	}

	void SplashScreen::SetProgress(float targetProgress, const wxString& statusText) {
		m_targetProgress = std::clamp(targetProgress, 0.0f, 100.0f);
		if (!statusText.IsEmpty()) {
			m_statusText = statusText;
		}
		Refresh();
	}

	void SplashScreen::StartInitialization(
		std::shared_ptr<ModelManager> modelManager,
		std::function<void()> onComplete) {

		m_modelManager = std::move(modelManager);
		m_onComplete = std::move(onComplete);
		m_targetProgress = 10.0f;
		m_currentProgress = 0.0f;
		m_statusText = L"正在初始化运行环境...";

		RunInitPipeline();
	}

	void SplashScreen::RunInitPipeline() {
		// 采用独立后台线程按序执行初始化流水线
		std::thread([this]() {
			// 阶段 1: 核心配置与基础运行环境加载 (10% -> 25%)
			auto updateProgress = BindUi([this](float progress, wxString status) {
				SetProgress(progress, status);
			});

			updateProgress(15.0f, wxString(L"正在解析核心配置与基础运行环境..."));
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

			updateProgress(25.0f, wxString(L"核心配置与日志系统加载完毕"));
			std::this_thread::sleep_for(std::chrono::milliseconds(80));

			// 阶段 2: 词典引擎与本地词典库解析 (25% -> 55%)
			updateProgress(35.0f, wxString(L"正在检索并扫描本地 StarDict 词典库..."));
			if (m_modelManager) {
				auto dictEngine = m_modelManager->GetDictEngine();
				auto configMgr = m_modelManager->GetConfigManager();
				if (dictEngine && configMgr) {
					std::string dictDir = configMgr->GetConfig().dictDirPath;
					if (!dictDir.empty()) {
						updateProgress(42.0f, wxString(L"正在解析离线词典索引与词条缓存..."));
						dictEngine->LoadDictionaries(dictDir);

						auto dicts = dictEngine->GetLoadedDictionaries();
						size_t wordCount = dictEngine->GetTotalWordCount();
						if (!dicts.empty() && wordCount > 0) {
							wxString dictName = wxString::FromUTF8(dicts[0].bookName);
							if (dicts.size() > 1) {
								dictName += wxString::Format(L" 等 %zu 部", dicts.size());
							}
							wxString msg = wxString::Format(L"已载入离线词典《%s》(共 %zu 词条)", dictName, wordCount);
							updateProgress(55.0f, msg);
						}
						else {
							updateProgress(55.0f, wxString(L"词典模块就绪 (可稍后在设置中导入词典)"));
						}
					}
					else {
						updateProgress(55.0f, wxString(L"词典模块就绪 (未配置词典目录)"));
					}
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(120));

			// 阶段 3: 本地翻译大模型预热与健康状态校验 (55% -> 90%)
			updateProgress(60.0f, wxString(L"正在检查本地翻译模型引擎配置..."));

			if (m_modelManager) {
				auto configMgr = m_modelManager->GetConfigManager();
				std::string modelPath = configMgr ? configMgr->GetConfig().modelPath : "";

				if (!modelPath.empty() && wxFileExists(wxString::FromUTF8(modelPath))) {
					wxFileName fn(wxString::FromUTF8(modelPath));
					wxString modelName = fn.GetFullName();
					updateProgress(68.0f, wxString::Format(L"正在拉起翻译模型: %s", modelName));

					std::mutex cvMtx;
					std::condition_variable cv;
					bool done = false;
					bool loadSuccess = false;
					std::string resultMsg;

					// 异步拉起翻译模型并等待直到确认就绪
					m_modelManager->EnsureModelAsync(
						TargetModelType::Translation,
						[updateProgress](const std::string& statusMsg) {
							updateProgress(80.0f, wxString::Format(L"翻译引擎: %s", wxString::FromUTF8(statusMsg)));
						},
						[&cvMtx, &cv, &done, &loadSuccess, &resultMsg](bool success, const ServerStatusInfo& info) {
							std::lock_guard<std::mutex> lock(cvMtx);
							done = true;
							loadSuccess = success;
							resultMsg = info.message;
							cv.notify_one();
						}
					);

					// 严格等待模型加载完毕并完全确认正常运行
					std::unique_lock<std::mutex> lock(cvMtx);
					cv.wait(lock, [&done]() { return done; });

					if (loadSuccess) {
						updateProgress(90.0f, wxString(L"翻译大模型服务已正常运行并就绪"));
					}
					else {
						updateProgress(90.0f, wxString::Format(L"翻译模型加载异常: %s", wxString::FromUTF8(resultMsg)));
					}
				}
				else {
					updateProgress(90.0f, wxString(L"翻译模型未配置 (可在主界面设置中选择)"));
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(120));

			// 阶段 4: 初始化全局划词服务与悬浮组件 (90% -> 100%)
			updateProgress(95.0f, wxString(L"正在初始化全局划词监听与悬浮气泡..."));
			std::this_thread::sleep_for(std::chrono::milliseconds(80));

			auto finishProgress = BindUi([this]() {
				SetProgress(100.0f, wxString(L"全部模块初始化就绪，正在开启应用..."));
				m_pipelineFinished = true;
				});

			finishProgress();
			}).detach();
	}

	void SplashScreen::OnAnimTimer(wxTimerEvent& WXUNUSED(event)) {
		float diff = m_targetProgress - m_currentProgress;

		if (std::abs(diff) > 0.08f) {
			// 缓动平滑插值 Lerp
			m_currentProgress += diff * 0.16f;
			Refresh();
		}
		else {
			m_currentProgress = m_targetProgress;
			Refresh();
		}

		// 检查是否所有流水线完毕且进度已达到 100%
		if (m_pipelineFinished && m_currentProgress >= 99.5f && !m_callbackTriggered) {
			m_callbackTriggered = true;
			m_currentProgress = 100.0f;
			m_statusText = L"初始化完成，正在进入应用...";
			Refresh();
			m_animTimer.Stop();

			// 停留 260ms 呈现完整的 100% 状态，然后平滑过渡
			m_finishTimer.StartOnce(260);
		}
	}

	void SplashScreen::OnFinishTimer(wxTimerEvent& WXUNUSED(event)) {
		if (m_onComplete) {
			auto cb = std::move(m_onComplete);
			m_onComplete = nullptr;
			cb();
		}
	}

	void SplashScreen::OnPaint(wxPaintEvent& WXUNUSED(event)) {
		wxAutoBufferedPaintDC dc(this);
		wxSize size = GetClientSize();
		if (size.x <= 0 || size.y <= 0) return;

		auto palette = ThemeColors::GetCurrentPalette();

		dc.SetBackground(wxBrush(palette.cardBg));
		dc.Clear();

		std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
		if (!gc) return;

		// 1. 绘制纯色背景与精细直角矩形边框
		gc->SetBrush(gc->CreateBrush(wxBrush(palette.cardBg)));
		gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 1.0)));
		gc->DrawRectangle(0.5, 0.5, size.x - 1.0, size.y - 1.0);

		// 2. 第一行：精致 Logo (42x42) + 主标题与英文副标组合
		wxSize logoSize = dip(42, 42);
		wxBitmapBundle logoBundle = IconManager::GetAppLogoBundle(wxSize(42, 42));
		wxBitmap logoBmp = logoBundle.GetBitmap(logoSize);

		wxFont titleFont(15, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Microsoft YaHei");
		wxFont subTitleFont(11, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI");

		wxString titleCn = L"译灵驼";
		wxString titleEn = L"LinguaAlpaca";

		gc->SetFont(titleFont, palette.textPrimary);
		double twCn = 0, thCn = 0;
		gc->GetTextExtent(titleCn, &twCn, &thCn);

		gc->SetFont(subTitleFont, palette.textSecondary);
		double twEn = 0, thEn = 0;
		gc->GetTextExtent(titleEn, &twEn, &thEn);

		double dotW = 0, dotH = 0;
		wxString dotStr = L"·";
		gc->GetTextExtent(dotStr, &dotW, &dotH);

		double headerGap = 12_dip;
		double itemGap = 6_dip;
		double textTotalW = twCn + itemGap + dotW + itemGap + twEn;
		double totalHeaderW = logoSize.x + headerGap + textTotalW;
		double headerStartX = (size.x - totalHeaderW) / 2.0;
		double headerY = 18_dip;

		// 绘制 Logo
		if (logoBmp.IsOk()) {
			gc->DrawBitmap(logoBmp, headerStartX, headerY, logoSize.x, logoSize.y);
		}

		// 绘制文本 (中文加粗主标题 + 点分隔符 + 英文优雅副标)
		double textBaseX = headerStartX + logoSize.x + headerGap;
		double textCenterY = headerY + (logoSize.y / 2.0);

		gc->SetFont(titleFont, palette.textPrimary);
		gc->DrawText(titleCn, textBaseX, textCenterY - thCn / 2.0);

		gc->SetFont(subTitleFont, palette.textSecondary);
		gc->DrawText(dotStr, textBaseX + twCn + itemGap, textCenterY - dotH / 2.0);
		gc->DrawText(titleEn, textBaseX + twCn + itemGap + dotW + itemGap, textCenterY - thEn / 2.0);

		// 3. 第二行：左侧加载阶段状态描述，右侧百分比数字
		double marginX = 28_dip;
		double statusY = headerY + logoSize.y + 15_dip;

		// 百分比文字 (加粗高亮主色，靠右固定排版)
		wxFont percentFont(9.5, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, "Segoe UI");
		gc->SetFont(percentFont, palette.accentPrimary);

		int progressPercent = (int)std::round(m_currentProgress);
		wxString percentStr = wxString::Format(L"%d%%", progressPercent);
		double pw = 0, ph = 0;
		gc->GetTextExtent(percentStr, &pw, &ph);
		gc->DrawText(percentStr, size.x - marginX - pw, statusY);

		// 状态文字 (自适应剩余可用宽度，超长时智能截断省略，绝对不与百分比重叠或溢出)
		wxFont statusFont(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei");
		gc->SetFont(statusFont, palette.textSecondary);

		double maxStatusW = (size.x - marginX - pw - 12_dip) - marginX;
		wxString displayStatus = m_statusText;
		double stw = 0, sth = 0;
		gc->GetTextExtent(displayStatus, &stw, &sth);

		if (stw > maxStatusW && maxStatusW > 30_dip) {
			while (!displayStatus.IsEmpty() && stw > maxStatusW) {
				displayStatus.RemoveLast();
				gc->GetTextExtent(displayStatus + L"...", &stw, &sth);
			}
			displayStatus += L"...";
		}
		gc->DrawText(displayStatus, marginX, statusY + (ph - sth) / 2.0);

		// 4. 第三行：现代胶囊发光渐变进度条
		double barY = statusY + sth + 7_dip;
		double barH = 5_dip;
		double barW = size.x - marginX * 2;

		// 轨道底槽
		gc->SetBrush(gc->CreateBrush(wxBrush(palette.windowBg)));
		gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 0.8)));
		gc->DrawRoundedRectangle(marginX, barY, barW, barH, 2.5_dip);

		// 进度条发光渐变填充
		double fillW = barW * (m_currentProgress / 100.0);
		if (fillW > 2.0) {
			wxGraphicsGradientStops fillGrad;
			fillGrad.Add(palette.accentPrimary, 0.0f);
			fillGrad.Add(palette.cardBorderActive, 1.0f);
			wxGraphicsBrush fillBrush = gc->CreateLinearGradientBrush(marginX, barY, marginX + fillW, barY, fillGrad);

			gc->SetBrush(fillBrush);
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->DrawRoundedRectangle(marginX, barY, fillW, barH, 2.5_dip);
		}

		// 5. 第四行：提炼合一的 Slogan 口号（居中排版）
		wxFont sloganFont(8.5, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei");
		gc->SetFont(sloganFont, palette.textSecondary);

		wxString sloganText = L"凭本地之智，见世界之全，守私密之心";
		double sw = 0, sh = 0;
		gc->GetTextExtent(sloganText, &sw, &sh);
		double sloganX = (size.x - sw) / 2.0;
		double sloganY = barY + barH + 13_dip;
		gc->DrawText(sloganText, sloganX, sloganY);

		// 6. 第五行：三个功能微徽章 (视觉对比度更舒适，圆角更圆润)
		const std::vector<wxString> badges = {
			L"llama.cpp 引擎",
			L"StarDict 词典",
			L"多模态 OCR"
		};

		wxFont badgeFont(8, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei");
		gc->SetFont(badgeFont, palette.accentPrimary);

		double badgeH = 18_dip;
		double badgePadH = 8_dip;
		double badgeGap = 8_dip;

		double totalBadgesW = 0;
		std::vector<double> badgeWidths;
		for (const auto& bText : badges) {
			double bw = 0, bh = 0;
			gc->GetTextExtent(bText, &bw, &bh);
			double itemW = bw + badgePadH * 2;
			badgeWidths.push_back(itemW);
			totalBadgesW += itemW;
		}
		totalBadgesW += (badges.size() - 1) * badgeGap;

		double curBadgeX = (size.x - totalBadgesW) / 2.0;
		double curBadgeY = sloganY + sh + 9_dip;

		for (size_t i = 0; i < badges.size(); ++i) {
			double bw = badgeWidths[i];

			// 胶囊背景与精细边框
			gc->SetBrush(gc->CreateBrush(wxBrush(palette.windowBg)));
			gc->SetPen(gc->CreatePen(wxPen(palette.cardBorder, 0.8)));
			gc->DrawRoundedRectangle(curBadgeX, curBadgeY, bw, badgeH, 4.0_dip);

			// 文字居中绘制
			double textW = 0, textH = 0;
			gc->GetTextExtent(badges[i], &textW, &textH);
			gc->DrawText(badges[i], curBadgeX + (bw - textW) / 2.0, curBadgeY + (badgeH - textH) / 2.0);

			curBadgeX += bw + badgeGap;
		}
	}

} // namespace LinguaAlpaca::UI
