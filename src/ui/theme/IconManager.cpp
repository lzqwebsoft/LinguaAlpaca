#include "IconManager.hpp"
#include <unordered_map>
#include <wx/mstream.h>
#include <wx/image.h>
#include <wx/iconbndl.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>

namespace LinguaAlpaca::UI {

struct IconCacheKey {
    const void* svgPtr;
    int w;
    int h;
    uint32_t colorRgb;

    bool operator==(const IconCacheKey& other) const noexcept {
        return svgPtr == other.svgPtr && w == other.w && h == other.h && colorRgb == other.colorRgb;
    }
};

struct IconCacheKeyHash {
    size_t operator()(const IconCacheKey& k) const noexcept {
        size_t h1 = std::hash<const void*>{}(k.svgPtr);
        size_t h2 = std::hash<int>{}(k.w ^ (k.h << 16));
        size_t h3 = std::hash<uint32_t>{}(k.colorRgb);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

static std::unordered_map<IconCacheKey, wxBitmapBundle, IconCacheKeyHash> s_bundleCache;

static wxString ResolveResourcePath(const wxString& relativePath) {
    // 1. 尝试可执行文件所在目录
    wxFileName exeDir(wxStandardPaths::Get().GetExecutablePath());
    wxString dir = exeDir.GetPath();

    wxString candidate1 = dir + wxFileName::GetPathSeparator() + relativePath;
    if (wxFileExists(candidate1)) return candidate1;

    // 2. 尝试上级目录 (例如 build/bin/Debug/ 对应根目录)
    wxString candidate2 = dir + wxFileName::GetPathSeparator() + ".." + wxFileName::GetPathSeparator() + relativePath;
    if (wxFileExists(candidate2)) return candidate2;

    wxString candidate3 = dir + wxFileName::GetPathSeparator() + ".." + wxFileName::GetPathSeparator() + ".." + wxFileName::GetPathSeparator() + relativePath;
    if (wxFileExists(candidate3)) return candidate3;

    wxString candidate4 = dir + wxFileName::GetPathSeparator() + ".." + wxFileName::GetPathSeparator() + ".." + wxFileName::GetPathSeparator() + ".." + wxFileName::GetPathSeparator() + relativePath;
    if (wxFileExists(candidate4)) return candidate4;

    // 3. 尝试当前工作目录
    if (wxFileExists(relativePath)) return relativePath;

    return wxEmptyString;
}

wxBitmapBundle IconManager::GetIconBundle(
    const char* svgContent,
    const wxSize& size,
    const wxColour& tintColor) {

    if (!svgContent) {
        return wxBitmapBundle();
    }

    uint32_t colorKey = tintColor.IsOk() ? (tintColor.GetRGB() | 0xFF000000) : 0;
    IconCacheKey key{ static_cast<const void*>(svgContent), size.x, size.y, colorKey };

    auto it = s_bundleCache.find(key);
    if (it != s_bundleCache.end()) {
        return it->second;
    }

    std::string svgStr(svgContent);

    if (tintColor.IsOk()) {
        char hexBuf[16];
        snprintf(hexBuf, sizeof(hexBuf), "#%02X%02X%02X", tintColor.Red(), tintColor.Green(), tintColor.Blue());
        
        size_t pos = 0;
        while ((pos = svgStr.find("currentColor", pos)) != std::string::npos) {
            svgStr.replace(pos, 12, hexBuf);
            pos += 7;
        }
    }

    wxBitmapBundle bundle = wxBitmapBundle::FromSVG(svgStr.c_str(), size);
    s_bundleCache.emplace(key, bundle);
    return bundle;
}

#ifdef _WIN32
#include <windows.h>
#endif

wxImage IconManager::GetAppLogoImage() {
#ifdef _WIN32
    // 优先从可执行文件内静态链接的 Windows RCDATA 资源直接读取 logo.png (用于标题栏 Logo)
    HRSRC hRes = ::FindResourceW(NULL, L"APP_LOGO_PNG", RT_RCDATA);
    if (hRes) {
        HGLOBAL hGlobal = ::LoadResource(NULL, hRes);
        if (hGlobal) {
            void* pData = ::LockResource(hGlobal);
            DWORD dwSize = ::SizeofResource(NULL, hRes);
            if (pData && dwSize > 0) {
                wxMemoryInputStream stream(pData, dwSize);
                wxImage img;
                if (img.LoadFile(stream, wxBITMAP_TYPE_PNG)) {
                    return img;
                }
            }
        }
    }
#endif

    // 回退机制：从文件路径解析加载 logo.png
    wxString logoPath = ResolveResourcePath("resources/logo.png");
    if (!logoPath.IsEmpty() && wxFileExists(logoPath)) {
        wxImage img;
        if (img.LoadFile(logoPath, wxBITMAP_TYPE_PNG)) {
            return img;
        }
    }
    return wxNullImage;
}

wxImage IconManager::GetAppWindowIconImage() {
#ifdef _WIN32
    // 优先从可执行文件内静态链接的 Windows RCDATA 资源直接读取 app_icon.png (用于窗体/任务栏图标)
    HRSRC hRes = ::FindResourceW(NULL, L"APP_WINDOW_ICON_PNG", RT_RCDATA);
    if (hRes) {
        HGLOBAL hGlobal = ::LoadResource(NULL, hRes);
        if (hGlobal) {
            void* pData = ::LockResource(hGlobal);
            DWORD dwSize = ::SizeofResource(NULL, hRes);
            if (pData && dwSize > 0) {
                wxMemoryInputStream stream(pData, dwSize);
                wxImage img;
                if (img.LoadFile(stream, wxBITMAP_TYPE_PNG)) {
                    return img;
                }
            }
        }
    }
#endif

    // 回退机制：从文件路径解析加载 app_icon.png
    wxString logoPath = ResolveResourcePath("resources/app_icon.png");
    if (!logoPath.IsEmpty() && wxFileExists(logoPath)) {
        wxImage img;
        if (img.LoadFile(logoPath, wxBITMAP_TYPE_PNG)) {
            return img;
        }
    }
    return wxNullImage;
}

wxBitmapBundle IconManager::GetAppLogoBundle(const wxSize& targetSize) {
    wxImage img = GetAppLogoImage();
    if (!img.IsOk() || targetSize.x <= 0 || targetSize.y <= 0) {
        return wxBitmapBundle();
    }
    wxBitmap bmp(img.Scale(targetSize.x, targetSize.y, wxIMAGE_QUALITY_HIGH));
    wxBitmap bmp1_25x(img.Scale(static_cast<int>(std::round(targetSize.x * 1.25)), static_cast<int>(std::round(targetSize.y * 1.25)), wxIMAGE_QUALITY_HIGH));
    wxBitmap bmp1_5x(img.Scale(static_cast<int>(std::round(targetSize.x * 1.5)), static_cast<int>(std::round(targetSize.y * 1.5)), wxIMAGE_QUALITY_HIGH));
    wxBitmap bmp1_75x(img.Scale(static_cast<int>(std::round(targetSize.x * 1.75)), static_cast<int>(std::round(targetSize.y * 1.75)), wxIMAGE_QUALITY_HIGH));
    wxBitmap bmp2x(img.Scale(targetSize.x * 2, targetSize.y * 2, wxIMAGE_QUALITY_HIGH));
    wxVector<wxBitmap> bitmaps;
    bitmaps.push_back(bmp);
    bitmaps.push_back(bmp1_25x);
    bitmaps.push_back(bmp1_5x);
    bitmaps.push_back(bmp1_75x);
    bitmaps.push_back(bmp2x);
    return wxBitmapBundle::FromBitmaps(bitmaps);
}

wxIcon IconManager::GetAppIcon(const wxSize& targetSize) {
#ifdef _WIN32
    HICON hIcon = (HICON)::LoadImageW(
        ::GetModuleHandleW(NULL),
        MAKEINTRESOURCEW(1),
        IMAGE_ICON,
        targetSize.x,
        targetSize.y,
        LR_DEFAULTCOLOR
    );
    if (hIcon) {
        wxIcon icon;
        if (icon.CreateFromHICON(hIcon)) {
            return icon;
        }
        ::DestroyIcon(hIcon);
    }
#endif

    wxImage img = GetAppWindowIconImage();
    if (!img.IsOk()) {
        img = GetAppLogoImage();
    }
    if (!img.IsOk()) {
        return wxNullIcon;
    }
    wxBitmap bmp(img.Scale(targetSize.x, targetSize.y, wxIMAGE_QUALITY_HIGH));
    wxIcon icon;
    icon.CopyFromBitmap(bmp);
    return icon;
}

wxIconBundle IconManager::GetAppIconBundle() {
    wxIconBundle bundle;
#ifdef _WIN32
    const int sizes[] = { 16, 24, 32, 48, 64, 128, 256 };
    for (int sz : sizes) {
        HICON hIcon = (HICON)::LoadImageW(
            ::GetModuleHandleW(NULL),
            MAKEINTRESOURCEW(1),
            IMAGE_ICON,
            sz,
            sz,
            LR_DEFAULTCOLOR
        );
        if (hIcon) {
            wxIcon icon;
            if (icon.CreateFromHICON(hIcon)) {
                bundle.AddIcon(icon);
            } else {
                ::DestroyIcon(hIcon);
            }
        }
    }
    if (bundle.IsOk() && bundle.GetIcon(wxSize(32, 32)).IsOk()) {
        return bundle;
    }
#endif

    wxImage img = GetAppWindowIconImage();
    if (!img.IsOk()) {
        img = GetAppLogoImage();
    }
    if (img.IsOk()) {
        const int sizes[] = { 16, 24, 32, 48, 64, 128, 256 };
        for (int sz : sizes) {
            wxBitmap bmp(img.Scale(sz, sz, wxIMAGE_QUALITY_HIGH));
            wxIcon icon;
            icon.CopyFromBitmap(bmp);
            bundle.AddIcon(icon);
        }
    }
    return bundle;
}

} // namespace LinguaAlpaca::UI
