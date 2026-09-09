#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif
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

std::wstring CleanUiaString(const wchar_t* wstr, size_t len) {
    if (!wstr || len == 0) return L"";
    std::wstring result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        wchar_t ch = wstr[i];
        // U+FFFC 是 UIA 在跨段落、跨子元素或块级容器时插入的 Object Replacement Character
        if (ch == 0xFFFC) {
            if (result.empty() || result.back() != L'\n') {
                result.push_back(L'\n');
            }
        } else if (ch == 0xFEFF || ch == 0x200B || ch == 0x200C || ch == 0x200D) {
            // 忽略零宽字符
            continue;
        } else {
            result.push_back(ch);
        }
    }
    return result;
}

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
                        std::wstring cleanWstr = CleanUiaString(bstrText, SysStringLen(bstrText));
                        std::string text = WideToUtf8(cleanWstr);
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

#elif defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>

namespace LinguaAlpaca {

namespace {

std::string CleanAXString(NSString* nsStr) {
    if (!nsStr) return "";
    NSString* trimmed = [nsStr stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    return [trimmed UTF8String] ? [trimmed UTF8String] : "";
}

bool ExtractFromAXElement(AXUIElementRef element, std::string& outText, int& outAnchorX, int& outAnchorY) {
    if (!element) return false;

    CFTypeRef selectedTextVal = NULL;
    AXError err = AXUIElementCopyAttributeValue(element, kAXSelectedTextAttribute, &selectedTextVal);
    if (err == kAXErrorSuccess && selectedTextVal) {
        if (CFGetTypeID(selectedTextVal) == CFStringGetTypeID()) {
            NSString* str = (__bridge NSString*)selectedTextVal;
            std::string text = CleanAXString(str);
            if (!text.empty()) {
                outText = text;

                // 尝试获取选中文本的外接包围矩形以提供精准悬浮锚点
                CFTypeRef selectedRangeVal = NULL;
                if (AXUIElementCopyAttributeValue(element, kAXSelectedTextRangeAttribute, &selectedRangeVal) == kAXErrorSuccess && selectedRangeVal) {
                    CFTypeRef boundsVal = NULL;
                    if (AXUIElementCopyParameterizedAttributeValue(element, kAXBoundsForRangeParameterizedAttribute, selectedRangeVal, &boundsVal) == kAXErrorSuccess && boundsVal) {
                        CGRect rect = CGRectZero;
                        if (AXValueGetValue((AXValueRef)boundsVal, kAXValueTypeCGRect, &rect)) {
                            outAnchorX = static_cast<int>(rect.origin.x + rect.size.width);
                            outAnchorY = static_cast<int>(rect.origin.y + rect.size.height + 6);
                        }
                        CFRelease(boundsVal);
                    }
                    CFRelease(selectedRangeVal);
                }

                CFRelease(selectedTextVal);
                return true;
            }
        }
        CFRelease(selectedTextVal);
    }
    return false;
}

} // namespace

bool WinUIAutomationHelper::TryExtract(int x, int y, std::string& outText, int& outAnchorX, int& outAnchorY) {
    @autoreleasepool {
        AXUIElementRef systemWide = AXUIElementCreateSystemWide();
        if (!systemWide) return false;

        // 1. 优先从系统级当前聚焦元素及其父级祖先中提取
        AXUIElementRef focused = NULL;
        if (AXUIElementCopyAttributeValue(systemWide, kAXFocusedUIElementAttribute, (CFTypeRef*)&focused) == kAXErrorSuccess && focused) {
            AXUIElementRef curr = focused;
            for (int depth = 0; depth < 8 && curr; ++depth) {
                if (ExtractFromAXElement(curr, outText, outAnchorX, outAnchorY)) {
                    if (curr != focused) CFRelease(curr);
                    CFRelease(focused);
                    CFRelease(systemWide);
                    return true;
                }
                AXUIElementRef parent = NULL;
                if (AXUIElementCopyAttributeValue(curr, kAXParentAttribute, (CFTypeRef*)&parent) == kAXErrorSuccess && parent) {
                    if (curr != focused) CFRelease(curr);
                    curr = parent;
                } else {
                    if (curr != focused) CFRelease(curr);
                    break;
                }
            }
            CFRelease(focused);
        }

        // 2. 若聚焦元素未命中，从鼠标释放点坐标处的元素及其祖先提取
        CGPoint pt = CGPointMake(static_cast<CGFloat>(x), static_cast<CGFloat>(y));
        AXUIElementRef elementAtPoint = NULL;
        if (AXUIElementCopyElementAtPosition(systemWide, (float)pt.x, (float)pt.y, &elementAtPoint) == kAXErrorSuccess && elementAtPoint) {
            AXUIElementRef curr = elementAtPoint;
            for (int depth = 0; depth < 8 && curr; ++depth) {
                if (ExtractFromAXElement(curr, outText, outAnchorX, outAnchorY)) {
                    if (curr != elementAtPoint) CFRelease(curr);
                    CFRelease(elementAtPoint);
                    CFRelease(systemWide);
                    return true;
                }
                AXUIElementRef parent = NULL;
                if (AXUIElementCopyAttributeValue(curr, kAXParentAttribute, (CFTypeRef*)&parent) == kAXErrorSuccess && parent) {
                    if (curr != elementAtPoint) CFRelease(curr);
                    curr = parent;
                } else {
                    if (curr != elementAtPoint) CFRelease(curr);
                    break;
                }
            }
            CFRelease(elementAtPoint);
        }

        CFRelease(systemWide);
        return false;
    }
}

} // namespace LinguaAlpaca

#else

namespace LinguaAlpaca {
bool WinUIAutomationHelper::TryExtract(int, int, std::string&, int&, int&) {
    return false;
}
} // namespace LinguaAlpaca

#endif
