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
		m_transServer = std::make_shared<LlamaServer>();
		m_ocrServer = std::make_shared<LlamaServer>();
		m_transClient = std::make_shared<LlamaClient>(m_transServer);
		m_ocrClient = std::make_shared<LlamaClient>(m_ocrServer);
		m_dictEngine = std::make_shared<DictEngine>();

		if (m_configManager) {
			std::string dictDir = m_configManager->GetConfig().dictDirPath;
			if (!dictDir.empty()) {
				m_dictEngine->SetDictDir(dictDir);
			}
		}
	}

	ModelManager::~ModelManager() {
		++m_transSessionId;
		++m_ocrSessionId;
		if (m_transServer) {
			m_transServer->Stop();
		}
		if (m_ocrServer) {
			m_ocrServer->Stop();
		}
	}

	bool ModelManager::IsSwitching(TargetModelType type) const {
		if (type == TargetModelType::Translation) {
			return m_isTransSwitching.load(std::memory_order_acquire);
		}
		if (type == TargetModelType::Ocr) {
			return m_isOcrSwitching.load(std::memory_order_acquire);
		}
		return m_isTransSwitching.load(std::memory_order_acquire) ||
			   m_isOcrSwitching.load(std::memory_order_acquire);
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

		if (type == TargetModelType::Translation) {
			std::string targetModelPath = appConfig.modelPath;
			if (targetModelPath.empty() || !FileExists(targetModelPath)) {
				ServerStatusInfo info;
				info.state = ServerHealthState::Unconfigured;
				info.message = "翻译模型未配置或文件不存在";
				info.activeType = type;
				if (onComplete) onComplete(false, info);
				return;
			}

			ServerConfig serverConfig;
			serverConfig.modelPath = targetModelPath;
			serverConfig.ngl = appConfig.gpuLayers;
			serverConfig.port = appConfig.translationPort;
			serverConfig.ctxSize = appConfig.translationCtxSize;
			serverConfig.threads = appConfig.translationThreads;

			// 若已在运行且配置一致且健康，直接返回
			if (m_transServer->IsAlive() && !m_isTransSwitching.load()) {
				ServerStatusInfo probeInfo;
				if (m_transServer->QueryHealth(probeInfo) && probeInfo.state == ServerHealthState::Ready) {
					auto curCfg = m_transServer->GetConfig();
					if (curCfg.modelPath == serverConfig.modelPath &&
						curCfg.ngl == serverConfig.ngl &&
						curCfg.port == serverConfig.port &&
						curCfg.ctxSize == serverConfig.ctxSize &&
						curCfg.threads == serverConfig.threads) {
						probeInfo.activeType = type;
						probeInfo.currentModel = targetModelPath;
						probeInfo.port = m_transServer->GetPort();
						probeInfo.baseUrl = m_transServer->GetBaseUrl();
						if (onComplete) onComplete(true, probeInfo);
						return;
					}
				}
			}

			uint64_t sessionId = ++m_transSessionId;
			m_isTransSwitching.store(true, std::memory_order_release);

			std::thread([this, sessionId, type, serverConfig, onProgress, onComplete]() {
				std::lock_guard<std::mutex> lock(m_transSwitchMutex);

				if (sessionId != m_transSessionId.load(std::memory_order_acquire)) {
					return;
				}

				if (onProgress) {
					onProgress("正在启动并装载翻译模型...");
				}

				auto shouldAbort = [this, sessionId]() {
					return sessionId != m_transSessionId.load(std::memory_order_acquire);
				};

				bool ok = m_transServer->EnsureModelRunning(serverConfig, onProgress, shouldAbort);

				if (sessionId != m_transSessionId.load(std::memory_order_acquire)) {
					return;
				}

				ServerStatusInfo finalInfo;
				finalInfo.activeType = type;
				finalInfo.currentModel = serverConfig.modelPath;
				finalInfo.port = m_transServer->GetPort();
				finalInfo.baseUrl = m_transServer->GetBaseUrl();

				if (ok) {
					m_transClient->SetServer(m_transServer);
					finalInfo.state = ServerHealthState::Ready;
					finalInfo.message = "翻译模型已成功就绪";
				}
				else {
					finalInfo.state = ServerHealthState::Error;
					finalInfo.message = "翻译模型加载超时或启动失败";
				}

				m_isTransSwitching.store(false, std::memory_order_release);

				if (onComplete) {
					onComplete(ok, finalInfo);
				}
			}).detach();
		}
		else if (type == TargetModelType::Ocr) {
			std::string targetModelPath = appConfig.ocrModelPath;
			std::string targetMmprojPath = appConfig.ocrMmprojPath;
			if (targetModelPath.empty() || !FileExists(targetModelPath) ||
				targetMmprojPath.empty() || !FileExists(targetMmprojPath)) {
				ServerStatusInfo info;
				info.state = ServerHealthState::Unconfigured;
				info.message = "OCR 主模型或 mmproj 视觉投影器未配置或文件不存在";
				info.activeType = type;
				if (onComplete) onComplete(false, info);
				return;
			}

			ServerConfig serverConfig;
			serverConfig.modelPath = targetModelPath;
			serverConfig.mmprojPath = targetMmprojPath;
			serverConfig.mmprojOffload = appConfig.ocrMmprojOffload;
			serverConfig.ngl = appConfig.ocrGpuLayers;
			serverConfig.port = appConfig.ocrPort;
			serverConfig.ctxSize = appConfig.ocrCtxSize;
			serverConfig.threads = appConfig.ocrThreads;

			// 若已在运行且配置一致且健康，直接返回
			if (m_ocrServer->IsAlive() && !m_isOcrSwitching.load()) {
				ServerStatusInfo probeInfo;
				if (m_ocrServer->QueryHealth(probeInfo) && probeInfo.state == ServerHealthState::Ready) {
					auto curCfg = m_ocrServer->GetConfig();
					if (curCfg.modelPath == serverConfig.modelPath &&
						curCfg.mmprojPath == serverConfig.mmprojPath &&
						curCfg.mmprojOffload == serverConfig.mmprojOffload &&
						curCfg.ngl == serverConfig.ngl &&
						curCfg.port == serverConfig.port &&
						curCfg.ctxSize == serverConfig.ctxSize &&
						curCfg.threads == serverConfig.threads) {
						probeInfo.activeType = type;
						probeInfo.currentModel = targetModelPath;
						probeInfo.port = m_ocrServer->GetPort();
						probeInfo.baseUrl = m_ocrServer->GetBaseUrl();
						if (onComplete) onComplete(true, probeInfo);
						return;
					}
				}
			}

			uint64_t sessionId = ++m_ocrSessionId;
			m_isOcrSwitching.store(true, std::memory_order_release);

			std::thread([this, sessionId, type, serverConfig, onProgress, onComplete]() {
				std::lock_guard<std::mutex> lock(m_ocrSwitchMutex);

				if (sessionId != m_ocrSessionId.load(std::memory_order_acquire)) {
					return;
				}

				if (onProgress) {
					onProgress("正在启动并装载 OCR 视觉识别引擎...");
				}

				auto shouldAbort = [this, sessionId]() {
					return sessionId != m_ocrSessionId.load(std::memory_order_acquire);
				};

				bool ok = m_ocrServer->EnsureModelRunning(serverConfig, onProgress, shouldAbort);

				if (sessionId != m_ocrSessionId.load(std::memory_order_acquire)) {
					return;
				}

				ServerStatusInfo finalInfo;
				finalInfo.activeType = type;
				finalInfo.currentModel = serverConfig.modelPath;
				finalInfo.port = m_ocrServer->GetPort();
				finalInfo.baseUrl = m_ocrServer->GetBaseUrl();

				if (ok) {
					m_ocrClient->SetServer(m_ocrServer);
					finalInfo.state = ServerHealthState::Ready;
					finalInfo.message = "OCR 视觉模型已成功就绪";
				}
				else {
					finalInfo.state = ServerHealthState::Error;
					finalInfo.message = "OCR 视觉模型加载超时或启动失败";
				}

				m_isOcrSwitching.store(false, std::memory_order_release);

				if (onComplete) {
					onComplete(ok, finalInfo);
				}
			}).detach();
		}
	}

	void ModelManager::StopModelAsync(TargetModelType type, std::function<void()> onComplete) {
		if (type == TargetModelType::Translation || type == TargetModelType::None) {
			++m_transSessionId;
		}
		if (type == TargetModelType::Ocr || type == TargetModelType::None) {
			++m_ocrSessionId;
		}

		std::thread([this, type, onComplete]() {
			if (type == TargetModelType::Translation || type == TargetModelType::None) {
				std::lock_guard<std::mutex> lock(m_transSwitchMutex);
				if (m_transServer && m_transServer->IsAlive()) {
					m_transServer->Stop();
				}
				m_isTransSwitching.store(false, std::memory_order_release);
			}

			if (type == TargetModelType::Ocr || type == TargetModelType::None) {
				std::lock_guard<std::mutex> lock(m_ocrSwitchMutex);
				if (m_ocrServer && m_ocrServer->IsAlive()) {
					m_ocrServer->Stop();
				}
				m_isOcrSwitching.store(false, std::memory_order_release);
			}

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
			info.currentModel = appConfig.modelPath;
			if (appConfig.modelPath.empty() || !FileExists(appConfig.modelPath)) {
				info.state = ServerHealthState::Unconfigured;
				info.message = "翻译模型未配置";
				return info;
			}

			if (m_isTransSwitching.load(std::memory_order_acquire)) {
				info.state = ServerHealthState::Loading;
				info.message = "正在启动加载翻译模型中...";
				return info;
			}

			if (m_transServer) {
				m_transServer->QueryHealth(info);
				info.activeType = targetType;
				info.currentModel = appConfig.modelPath;
				info.port = m_transServer->GetPort();
				info.baseUrl = m_transServer->GetBaseUrl();
			}
			return info;
		}
		else if (targetType == TargetModelType::Ocr) {
			info.currentModel = appConfig.ocrModelPath;
			if (appConfig.ocrModelPath.empty() || !FileExists(appConfig.ocrModelPath) ||
				appConfig.ocrMmprojPath.empty() || !FileExists(appConfig.ocrMmprojPath)) {
				info.state = ServerHealthState::Unconfigured;
				info.message = "OCR 视觉模型或 mmproj 未配置";
				return info;
			}

			if (m_isOcrSwitching.load(std::memory_order_acquire)) {
				info.state = ServerHealthState::Loading;
				info.message = "正在启动加载 OCR 视觉模型中...";
				return info;
			}

			if (m_ocrServer) {
				m_ocrServer->QueryHealth(info);
				info.activeType = targetType;
				info.currentModel = appConfig.ocrModelPath;
				info.port = m_ocrServer->GetPort();
				info.baseUrl = m_ocrServer->GetBaseUrl();
			}
			return info;
		}

		info.state = ServerHealthState::Offline;
		info.message = "未指定模型类型";
		return info;
	}

	void ModelManager::ExecuteTranslationStream(
		const TranslationTask& task,
		StreamTokenCallback onToken,
		StreamCompleteCallback onComplete) {

		if (!m_transClient) {
			if (onComplete) onComplete(false, "", "翻译客户端未就绪");
			return;
		}

		if (task.GetSourceText().empty()) {
			if (onComplete) onComplete(true, "", "");
			return;
		}

		m_transClient->TranslateStreamAsync(task, onToken, [this, task, onComplete](bool success, const std::string& fullText, const std::string& error) {
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

		if (m_ocrClient) {
			m_ocrClient->RecognizeStream(imagePath, taskType, "", "", onToken, onComplete);
		}
		else if (onComplete) {
			onComplete("", false, "OCR 客户端未就绪");
		}
	}

	void ModelManager::CancelInference(TargetModelType type) {
		if (type == TargetModelType::Translation || type == TargetModelType::None) {
			if (m_transClient) {
				m_transClient->CancelCurrentTask();
			}
		}
		if (type == TargetModelType::Ocr || type == TargetModelType::None) {
			if (m_ocrClient) {
				m_ocrClient->CancelCurrentTask();
			}
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

