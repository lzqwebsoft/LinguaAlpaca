#pragma execution_character_set("utf-8")
#include "ModelManager.hpp"

#include <iostream>
#include <filesystem>
#include <wx/filefn.h>

namespace LinguaAlpaca {

	static bool FileExists(const std::string& path) {
		if (path.empty()) return false;
		try {
			return std::filesystem::exists(path);
		}
		catch (...) {
			return wxFileExists(wxString::FromUTF8(path));
		}
	}

	ModelManager::ModelManager(std::shared_ptr<ConfigManager> configManager)
		: m_configManager(std::move(configManager)) {
		m_server = std::make_shared<LlamaServer>();
		m_client = std::make_shared<LlamaClient>(m_server);
		m_dictEngine = std::make_shared<DictEngine>();

		if (m_configManager) {
			std::string dictDir = m_configManager->GetConfig().dictDirPath;
			if (!dictDir.empty()) {
				m_dictEngine->LoadDictionaries(dictDir);
			}
		}
	}

	ModelManager::~ModelManager() {
		++m_currentSessionId;
		if (m_server) {
			m_server->Stop();
		}
	}

	void ModelManager::EnsureModelAsync(
		TargetModelType type,
		std::function<void(const std::string& statusMsg)> onProgress,
		std::function<void(bool success, const ServerStatusInfo& info)> onComplete) {

		if (type == TargetModelType::None) {
			if (onComplete) {
				ServerStatusInfo info;
				info.state = ServerHealthState::Offline;
				info.message = "未指定模型类型";
				onComplete(false, info);
			}
			return;
		}

		auto appConfig = m_configManager ? m_configManager->GetConfig() : AppConfig{};
		std::string targetModelPath;
		std::string targetMmprojPath;

		if (type == TargetModelType::Translation) {
			targetModelPath = appConfig.modelPath;
			if (targetModelPath.empty() || !FileExists(targetModelPath)) {
				ServerStatusInfo info;
				info.state = ServerHealthState::Unconfigured;
				info.message = "翻译模型未配置或文件不存在";
				info.activeType = type;
				if (onComplete) onComplete(false, info);
				return;
			}
		}
		else if (type == TargetModelType::Ocr) {
			targetModelPath = appConfig.ocrModelPath;
			targetMmprojPath = appConfig.ocrMmprojPath;
			if (targetModelPath.empty() || !FileExists(targetModelPath) ||
				targetMmprojPath.empty() || !FileExists(targetMmprojPath)) {
				ServerStatusInfo info;
				info.state = ServerHealthState::Unconfigured;
				info.message = "OCR 主模型或 mmproj 视觉投影器未配置或文件不存在";
				info.activeType = type;
				if (onComplete) onComplete(false, info);
				return;
			}
		}

		// 若已经运行相同模型且服务健康，立即返回
		if (m_activeModelType.load() == type && !m_isSwitching.load()) {
			ServerStatusInfo probeInfo;
			if (m_server->QueryHealth(probeInfo) && probeInfo.state == ServerHealthState::Ready) {
				probeInfo.activeType = type;
				probeInfo.currentModel = targetModelPath;
				if (onComplete) onComplete(true, probeInfo);
				return;
			}
		}

		uint64_t sessionId = ++m_currentSessionId;
		int targetNgl = (type == TargetModelType::Ocr) ? appConfig.ocrGpuLayers : appConfig.gpuLayers;

		m_isSwitching.store(true, std::memory_order_release);

		std::thread([this, sessionId, type, targetModelPath, targetMmprojPath, targetNgl, onProgress, onComplete]() {
			std::lock_guard<std::mutex> lock(m_switchMutex);

			if (sessionId != m_currentSessionId.load(std::memory_order_acquire)) {
				return;
			}

			if (onProgress) {
				onProgress("正在切换并装载模型...");
			}

			ServerConfig serverConfig;
			serverConfig.modelPath = targetModelPath;
			serverConfig.mmprojPath = targetMmprojPath;
			serverConfig.ngl = targetNgl;

			auto shouldAbort = [this, sessionId]() {
				return sessionId != m_currentSessionId.load(std::memory_order_acquire);
			};

			bool ok = m_server->EnsureModelRunning(serverConfig, onProgress, shouldAbort);

			if (sessionId != m_currentSessionId.load(std::memory_order_acquire)) {
				return;
			}

			ServerStatusInfo finalInfo;
			if (ok) {
				m_activeModelType.store(type, std::memory_order_release);
				m_client->SetServer(m_server);

				finalInfo.state = ServerHealthState::Ready;
				finalInfo.message = "模型已成功就绪";
				finalInfo.currentModel = targetModelPath;
				finalInfo.activeType = type;
			}
			else {
				finalInfo.state = ServerHealthState::Error;
				finalInfo.message = "模型加载超时或启动失败";
				finalInfo.activeType = type;
			}

			m_isSwitching.store(false, std::memory_order_release);

			if (onComplete) {
				onComplete(ok, finalInfo);
			}
		}).detach();
	}

	void ModelManager::StopModelAsync(std::function<void()> onComplete) {
		++m_currentSessionId;
		std::thread([this, onComplete]() {
			std::lock_guard<std::mutex> lock(m_switchMutex);
			if (m_server && m_server->IsAlive()) {
				m_server->Stop();
			}
			m_activeModelType.store(TargetModelType::None, std::memory_order_release);
			m_isSwitching.store(false, std::memory_order_release);
			if (onComplete) {
				onComplete();
			}
		}).detach();
	}

	ServerStatusInfo ModelManager::GetHealthStatus(TargetModelType targetType) const {
		ServerStatusInfo info;
		info.activeType = targetType;

		auto appConfig = m_configManager ? m_configManager->GetConfig() : AppConfig{};

		if (targetType == TargetModelType::Translation) {
			if (appConfig.modelPath.empty() || !FileExists(appConfig.modelPath)) {
				info.state = ServerHealthState::Unconfigured;
				info.message = "翻译模型未配置";
				return info;
			}
		}
		else if (targetType == TargetModelType::Ocr) {
			if (appConfig.ocrModelPath.empty() || !FileExists(appConfig.ocrModelPath) ||
				appConfig.ocrMmprojPath.empty() || !FileExists(appConfig.ocrMmprojPath)) {
				info.state = ServerHealthState::Unconfigured;
				info.message = "OCR 模型未配置";
				return info;
			}
		}

		if (m_isSwitching.load(std::memory_order_acquire)) {
			info.state = ServerHealthState::Loading;
			info.message = "正在加载模型中...";
			return info;
		}

		if (m_activeModelType.load(std::memory_order_acquire) != targetType) {
			info.state = ServerHealthState::Offline;
			info.message = "待按需切换";
			return info;
		}

		m_server->QueryHealth(info);
		info.activeType = targetType;
		return info;
	}

	void ModelManager::ExecuteTranslationStream(
		const TranslationTask& task,
		StreamTokenCallback onToken,
		StreamCompleteCallback onComplete) {

		if (!m_client) {
			if (onComplete) onComplete(false, "", "推理客户端未就绪");
			return;
		}

		if (task.GetSourceText().empty()) {
			if (onComplete) onComplete(true, "", "");
			return;
		}

		m_client->TranslateStreamAsync(task, onToken, [this, task, onComplete](bool success, const std::string& fullText, const std::string& error) {
			if (success && !fullText.empty()) {
				HistoryRecord record;
				record.sourceText = task.GetSourceText();
				record.translatedText = fullText;
				record.sourceLang = task.GetSourceLanguage();
				record.targetLang = task.GetTargetLanguage();
				record.timestamp = std::chrono::system_clock::now();
				AddHistory(record);
			}

			if (onComplete) {
				onComplete(success, fullText, error);
			}
			});
	}

	void ModelManager::ExecuteOcrStream(
		const std::string& imagePath,
		const std::string& taskType,
		OcrTokenCallback onToken,
		OcrCompleteCallback onComplete) {

		if (m_client) {
			m_client->RecognizeStream(imagePath, taskType, "", "", onToken, onComplete);
		}
		else if (onComplete) {
			onComplete("", false, "OCR 客户端未就绪");
		}
	}

	void ModelManager::CancelInference() {
		if (m_client) {
			m_client->CancelCurrentTask();
		}
	}

	void ModelManager::AddHistory(const HistoryRecord& record) {
		std::lock_guard<std::mutex> lock(m_historyMutex);
		m_history.push_back(record);
	}

	std::vector<HistoryRecord> ModelManager::GetHistory() const {
		std::lock_guard<std::mutex> lock(m_historyMutex);
		return m_history;
	}

	void ModelManager::ClearHistory() {
		std::lock_guard<std::mutex> lock(m_historyMutex);
		m_history.clear();
	}

} // namespace LinguaAlpaca
