#include "IconManager.hpp"
#include <unordered_map>
#include <wx/mstream.h>
#include <wx/image.h>
#include <wx/iconbndl.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#endif

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
    // 0. 尝试 wxStandardPaths Resources 目录 (适用于 macOS App Bundle: Contents/Resources)
    wxString resDir = wxStandardPaths::Get().GetResourcesDir();
    if (!resDir.IsEmpty()) {
        wxString candidateRes1 = resDir + wxFileName::GetPathSeparator() + relativePath;
        if (wxFileExists(candidateRes1)) return candidateRes1;

        if (relativePath.StartsWith("resources/") || relativePath.StartsWith("resources\\")) {
            wxString stripped = relativePath.Mid(10);
            wxString candidateRes2 = resDir + wxFileName::GetPathSeparator() + stripped;
            if (wxFileExists(candidateRes2)) return candidateRes2;
        }
    }

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

    // 从文件路径解析加载 logo.png
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

    // 从文件路径解析加载 app_icon.png
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

wxBitmapBundle IconManager::GetAppStatusBarBundle() {
    // 优先使用清晰度与对比度更高的应用专用图标 app_icon.png
    wxImage srcImg = GetAppWindowIconImage();
    if (!srcImg.IsOk()) {
        srcImg = GetAppLogoImage();
    }
    if (!srcImg.IsOk()) {
        return wxBitmapBundle();
    }

    // 针对 macOS 状态栏 (22~24pt) 与各平台系统托盘，构建带安全内边距的高清多尺寸集合
    // 逻辑基准尺寸：22x22 点 (上下各 2pt 呼吸安全留白，核心图标 18x18 点)
    struct SizeSpec {
        int canvasSize;
        int iconSize;
    };
    const SizeSpec specs[] = {
        { 22, 18 },  // 1.0x 标准屏基准 (22pt 状态栏，18pt 居中图标)
        { 24, 20 },  // 1.0x Notch 屏基准 (24pt 状态栏，20pt 居中图标)
        { 28, 22 },  // 1.25x 缩放
        { 33, 27 },  // 1.5x 缩放
        { 44, 36 },  // 2.0x Retina 屏 (44px 物理像素，36px 居中图标)
        { 48, 40 },  // 2.0x Retina Notch 屏 (48px 物理像素，40px 居中图标)
        { 66, 54 },  // 3.0x 超视网膜屏 (66px 物理像素，54px 居中图标)
        { 88, 72 },  // 4.0x 超高分屏 (88px 物理像素，72px 居中图标)
    };

    wxVector<wxBitmap> bitmaps;
    for (const auto& sp : specs) {
        wxImage scaled = srcImg.Scale(sp.iconSize, sp.iconSize, wxIMAGE_QUALITY_HIGH);
        wxImage canvas(sp.canvasSize, sp.canvasSize);
        canvas.InitAlpha();
        memset(canvas.GetAlpha(), 0, sp.canvasSize * sp.canvasSize);
        memset(canvas.GetData(), 0, sp.canvasSize * sp.canvasSize * 3);
        int offset = (sp.canvasSize - sp.iconSize) / 2;
        canvas.Paste(scaled, offset, offset, wxIMAGE_ALPHA_BLEND_OVER);
        bitmaps.push_back(wxBitmap(canvas));
    }

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

    // 尝试从 resources/app_icon.ico 解析多分辨率图标
    wxString icoPath = ResolveResourcePath("resources/app_icon.ico");
    if (icoPath.IsEmpty()) icoPath = ResolveResourcePath("app_icon.ico");
    if (!icoPath.IsEmpty() && wxFileExists(icoPath)) {
        wxIconBundle bundle(icoPath, wxBITMAP_TYPE_ICO);
        if (bundle.IsOk()) {
            wxIcon icon = bundle.GetIcon(targetSize);
            if (icon.IsOk()) {
                return icon;
            }
        }
    }

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

    // 尝试从 resources/app_icon.ico 加载全部规格图标
    wxString icoPath = ResolveResourcePath("resources/app_icon.ico");
    if (icoPath.IsEmpty()) icoPath = ResolveResourcePath("app_icon.ico");
    if (!icoPath.IsEmpty() && wxFileExists(icoPath)) {
        bundle.AddIcon(icoPath, wxBITMAP_TYPE_ICO);
        if (bundle.IsOk() && bundle.GetIcon(wxSize(32, 32)).IsOk()) {
            return bundle;
        }
    }

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

#ifdef __APPLE__
static void SetupMacDockIcon() {
    @autoreleasepool {
        NSImage* appIconImage = nil;

        // 1. 优先从 resources/app_icon.icns 加载原生多分辨率矢量与位图图标
        wxString icnsPath = ResolveResourcePath("resources/app_icon.icns");
        if (icnsPath.IsEmpty()) {
            icnsPath = ResolveResourcePath("app_icon.icns");
        }
        if (!icnsPath.IsEmpty() && wxFileExists(icnsPath)) {
            NSString* nsPath = [NSString stringWithUTF8String:icnsPath.ToUTF8().data()];
            appIconImage = [[NSImage alloc] initWithContentsOfFile:nsPath];
        }

        // 2. 次选从 resources/app_icon_mac.png 或 resources/app_icon.png 加载
        if (!appIconImage) {
            wxString pngPath = ResolveResourcePath("resources/app_icon_mac.png");
            if (pngPath.IsEmpty()) {
                pngPath = ResolveResourcePath("app_icon_mac.png");
            }
            if (pngPath.IsEmpty() || !wxFileExists(pngPath)) {
                pngPath = ResolveResourcePath("resources/app_icon.png");
                if (pngPath.IsEmpty()) {
                    pngPath = ResolveResourcePath("app_icon.png");
                }
            }
            if (!pngPath.IsEmpty() && wxFileExists(pngPath)) {
                NSString* nsPath = [NSString stringWithUTF8String:pngPath.ToUTF8().data()];
                appIconImage = [[NSImage alloc] initWithContentsOfFile:nsPath];
            }
        }

        // 3. 回退机制：从 wxImage / wxBitmap 导出 NSImage
        if (!appIconImage) {
            wxImage img = IconManager::GetAppWindowIconImage();
            if (!img.IsOk()) {
                img = IconManager::GetAppLogoImage();
            }
            if (img.IsOk()) {
                wxBitmap bmp(img);
                if (bmp.IsOk()) {
                    appIconImage = (NSImage*)bmp.GetNSImage();
                    if (appIconImage) {
#if !__has_feature(objc_arc)
                        [appIconImage retain];
#endif
                    }
                }
            }
        }

        // 4. 将图标注入 macOS Cocoa 运行态中枢并立即刷新 Dock 坞标识
        if (appIconImage) {
            NSApplication* app = [NSApplication sharedApplication];
            if ([app activationPolicy] != NSApplicationActivationPolicyRegular) {
                [app setActivationPolicy:NSApplicationActivationPolicyRegular];
            }
            [app setApplicationIconImage:appIconImage];
            [[app dockTile] display];

            // 5. 将自定义图标固化到可执行文件与 Bundle 磁盘本体 (通过 NSWorkspace 文件元数据扩展属性)，
            // 确保用户在 macOS 程序坞中勾选「在程序坞中保留」并退出程序后，程序坞依然常驻显示精美图标
            wxString exePath = wxStandardPaths::Get().GetExecutablePath();
            if (!exePath.IsEmpty() && wxFileExists(exePath)) {
                NSString* nsExePath = [NSString stringWithUTF8String:exePath.ToUTF8().data()];
                [[NSWorkspace sharedWorkspace] setIcon:appIconImage forFile:nsExePath options:0];

                if ([nsExePath containsString:@".app/Contents/MacOS"]) {
                    NSString* appBundlePath = [nsExePath componentsSeparatedByString:@"/Contents/MacOS"][0];
                    [[NSWorkspace sharedWorkspace] setIcon:appIconImage forFile:appBundlePath options:0];
                }
            }

#if !__has_feature(objc_arc)
            [appIconImage release];
#endif
        }
    }
}
#endif

void IconManager::SetupApplicationIcon() {
#ifdef __APPLE__
    SetupMacDockIcon();
#endif
}

} // namespace LinguaAlpaca::UI
