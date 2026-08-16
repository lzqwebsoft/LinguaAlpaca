#pragma execution_character_set("utf-8")
#include "WinUIAutomationHelper.hpp"
#include "Logger.hpp"

#ifdef _WIN32
#include <windows.h>
#include <ole2.h>
#include <unknwn.h>
#include <UIAutomationClient.h>
#include <comdef.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace LinguaAlpaca {

namespace {

std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0], size, nullptr, nullptr);
    return str;
}

std::string Trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

} // namespace

bool WinUIAutomationHelper::TryExtract(int x, int y, std::string& outText, int& outAnchorX, int& outAnchorY) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool shouldUninit = SUCCEEDED(hr);

    IUIAutomation* pAutomation = nullptr;
    hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation, (void**)&pAutomation);
    if (FAILED(hr) || !pAutomation) {
        if (shouldUninit) CoUninitialize();
        return false;
    }

    bool success = false;
    IUIAutomationElement* pElement = nullptr;
    POINT pt = { x, y };

    if (SUCCEEDED(pAutomation->ElementFromPoint(pt, &pElement)) && pElement) {
        IUIAutomationTextPattern* pTextPattern = nullptr;
        if (SUCCEEDED(pElement->GetCurrentPatternAs(UIA_TextPatternId, IID_IUIAutomationTextPattern, (void**)&pTextPattern)) && pTextPattern) {
            IUIAutomationTextRangeArray* pSelectionArray = nullptr;
            if (SUCCEEDED(pTextPattern->GetSelection(&pSelectionArray)) && pSelectionArray) {
                int length = 0;
                pSelectionArray->get_Length(&length);
                if (length > 0) {
                    IUIAutomationTextRange* pRange = nullptr;
                    if (SUCCEEDED(pSelectionArray->GetElement(0, &pRange)) && pRange) {
                        BSTR bstrText = nullptr;
                        if (SUCCEEDED(pRange->GetText(-1, &bstrText)) && bstrText) {
                            std::string text = WideToUtf8(bstrText);
                            text = Trim(text);
                            SysFreeString(bstrText);

                            if (!text.empty()) {
                                outText = text;
                                success = true;

                                // 尝试获取选中文本的外接包围矩形
                                SAFEARRAY* pRects = nullptr;
                                if (SUCCEEDED(pRange->GetBoundingRectangles(&pRects)) && pRects) {
                                    double* pData = nullptr;
                                    if (SUCCEEDED(SafeArrayAccessData(pRects, (void**)&pData))) {
                                        long uBound = 0;
                                        SafeArrayGetUBound(pRects, 1, &uBound);
                                        if (uBound >= 3) {
                                            // 每个矩形有 4 个 double: left, top, width, height
                                            double left = pData[0];
                                            double top = pData[1];
                                            double width = pData[2];
                                            double height = pData[3];
                                            outAnchorX = static_cast<int>(left + width);
                                            outAnchorY = static_cast<int>(top + height + 6);
                                        }
                                        SafeArrayUnaccessData(pRects);
                                    }
                                    SafeArrayDestroy(pRects);
                                }
                            }
                        }
                        pRange->Release();
                    }
                }
                pSelectionArray->Release();
            }
            pTextPattern->Release();
        }
        pElement->Release();
    }

    pAutomation->Release();
    if (shouldUninit) CoUninitialize();
    return success;
}

} // namespace LinguaAlpaca

#else

namespace LinguaAlpaca {
bool WinUIAutomationHelper::TryExtract(int, int, std::string&, int&, int&) {
    return false;
}
} // namespace LinguaAlpaca

#endif
