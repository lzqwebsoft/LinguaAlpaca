#include "OcrService.hpp"

namespace LinguaAlpaca::Application::Service {

OcrService::OcrService(std::shared_ptr<Domain::Repository::IOcrEngine> ocrEngine)
    : m_ocrEngine(std::move(ocrEngine)) {}

bool OcrService::IsModelLoaded() const {
    return m_ocrEngine ? m_ocrEngine->IsModelLoaded() : false;
}

std::string OcrService::GetModelPath() const {
    return m_ocrEngine ? m_ocrEngine->GetModelPath() : "";
}

std::string OcrService::GetMmprojPath() const {
    return m_ocrEngine ? m_ocrEngine->GetMmprojPath() : "";
}

void OcrService::RecognizeImageStream(
    const std::string& imagePath,
    const std::string& taskType,
    const std::string& modelPath,
    const std::string& mmprojPath,
    Domain::Repository::OcrTokenCallback onToken,
    Domain::Repository::OcrCompleteCallback onComplete) {

    if (m_ocrEngine) {
        m_ocrEngine->RecognizeStream(imagePath, taskType, modelPath, mmprojPath, onToken, onComplete);
    }
}

void OcrService::CancelOcr() {
    if (m_ocrEngine) {
        m_ocrEngine->Cancel();
    }
}

bool OcrService::IsRunning() const {
    return m_ocrEngine ? m_ocrEngine->IsRunning() : false;
}

} // namespace LinguaAlpaca::Application::Service
