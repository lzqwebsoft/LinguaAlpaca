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

bool TryExtractFromElement(IUIAutomationElement* pElement, std::string& outText, int& outAnchorX, int& outAnchorY) {
    if (!pElement) return false;

    auto extractFromPattern = [&](IUIAutomationTextPattern* pTextPattern) -> bool {
        if (!pTextPattern) return false;
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

                            // 尝试获取选中文本的外接包围矩形
                            SAFEARRAY* pRects = nullptr;
                            if (SUCCEEDED(pRange->GetBoundingRectangles(&pRects)) && pRects) {
                                double* pData = nullptr;
                                if (SUCCEEDED(SafeArrayAccessData(pRects, (void**)&pData))) {
                                    long uBound = 0;
                                    SafeArrayGetUBound(pRects, 1, &uBound);
                                    if (uBound >= 3) {
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
                            pRange->Release();
                            pSelectionArray->Release();
                            return true;
                        }
                    }
                    pRange->Release();
                }
            }
            pSelectionArray->Release();
        }
        return false;
    };

    // 1. 尝试标准 UIA_TextPatternId
    IUIAutomationTextPattern* pTextPattern = nullptr;
    if (SUCCEEDED(pElement->GetCurrentPatternAs(UIA_TextPatternId, IID_IUIAutomationTextPattern, (void**)&pTextPattern)) && pTextPattern) {
        bool res = extractFromPattern(pTextPattern);
        pTextPattern->Release();
        if (res) return true;
    }

    // 2. 尝试 UIA_TextPattern2Id (Windows Terminal / 现代 Windows 10/11 核心控件)
    IUIAutomationTextPattern2* pTextPattern2 = nullptr;
    if (SUCCEEDED(pElement->GetCurrentPatternAs(UIA_TextPattern2Id, IID_IUIAutomationTextPattern2, (void**)&pTextPattern2)) && pTextPattern2) {
        bool res = extractFromPattern((IUIAutomationTextPattern*)pTextPattern2);
        pTextPattern2->Release();
        if (res) return true;
    }

    return false;
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

    POINT pt = { x, y };

    // 唤醒目标窗口（特别是 Chromium / Electron / VS Code）内部的无障碍渲染引擎
    HWND hwndUnderMouse = WindowFromPoint(pt);
    if (hwndUnderMouse) {
        IAccessible* pAcc = nullptr;
        if (SUCCEEDED(AccessibleObjectFromWindow(hwndUnderMouse, OBJID_CLIENT, IID_IAccessible, (void**)&pAcc)) && pAcc) {
            pAcc->Release();
        }
    }

    IUIAutomationTreeWalker* pWalker = nullptr;
    pAutomation->get_ControlViewWalker(&pWalker);

    // 极简高效祖先回溯：最大深度 10 层，首个有效节点即 O(1) 短路退出
    auto tryWithAncestors = [&](IUIAutomationElement* pStart) -> bool {
        if (!pStart) return false;
        IUIAutomationElement* pCurr = pStart;
        pCurr->AddRef();

        for (int depth = 0; depth < 10 && pCurr; ++depth) {
            if (TryExtractFromElement(pCurr, outText, outAnchorX, outAnchorY)) {
                pCurr->Release();
                return true;
            }

            IUIAutomationElement* pParent = nullptr;
            if (pWalker && SUCCEEDED(pWalker->GetParentElement(pCurr, &pParent)) && pParent) {
                pCurr->Release();
                pCurr = pParent;
            } else {
                pCurr->Release();
                pCurr = nullptr;
                break;
            }
        }
        if (pCurr) pCurr->Release();
        return false;
    };

    bool success = false;

    // 优先 1：从鼠标释放坐标处的元素及祖先节点提取 (精确覆盖 Windows Terminal 的 TermControl 及 Web/VS Code 容器)
    IUIAutomationElement* pElement = nullptr;
    if (SUCCEEDED(pAutomation->ElementFromPoint(pt, &pElement)) && pElement) {
        if (tryWithAncestors(pElement)) {
            success = true;
        }
        pElement->Release();
    }

    // 优先 2：未命中时从当前拥有焦点的元素及祖先节点提取
    if (!success) {
        IUIAutomationElement* pFocused = nullptr;
        if (SUCCEEDED(pAutomation->GetFocusedElement(&pFocused)) && pFocused) {
            if (tryWithAncestors(pFocused)) {
                success = true;
            }
            pFocused->Release();
        }
    }

    if (pWalker) pWalker->Release();
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
