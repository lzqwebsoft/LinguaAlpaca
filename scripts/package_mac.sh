#!/usr/bin/env bash
# ==============================================================================
# LinguaAlpaca macOS Standalone Application Bundle & DMG Packaging Tool
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${1:-${PROJECT_ROOT}/build}"
DIST_DIR="${BUILD_DIR}/dist"
SRC_APP="${BUILD_DIR}/bin/LinguaAlpaca.app"
LLAMA_SERVER="${BUILD_DIR}/bin/llama-server"
DEST_APP="${DIST_DIR}/LinguaAlpaca.app"
APP_NAME="LinguaAlpaca"
VERSION="1.0.2"
DMG_NAME="${APP_NAME}-${VERSION}-macOS.dmg"
DMG_PATH="${DIST_DIR}/${DMG_NAME}"

echo "================================================================================"
echo " [LinguaAlpaca] Starting macOS Standalone App Packaging"
echo "================================================================================"
echo " Project Root:  ${PROJECT_ROOT}"
echo " Build Dir:     ${BUILD_DIR}"
echo " Target Bundle: ${DEST_APP}"
echo " Output DMG:    ${DMG_PATH}"
echo "================================================================================"

# 1. 验证编译产物存在
if [ ! -d "${SRC_APP}" ]; then
    echo " Error: Application bundle not found at: ${SRC_APP}"
    echo " Please run: cmake --build \"${BUILD_DIR}\" --target LinguaAlpaca"
    exit 1
fi

if [ ! -f "${LLAMA_SERVER}" ]; then
    echo " Error: llama-server executable not found at: ${LLAMA_SERVER}"
    echo " Please run: cmake --build \"${BUILD_DIR}\" --target llama-server"
    exit 1
fi

# 2. 准备纯净输出目录
mkdir -p "${DIST_DIR}"
rm -rf "${DEST_APP}"
echo "[1/6] Copying base application bundle to dist..."
cp -R "${SRC_APP}" "${DEST_APP}"

# 3. 替换 llama-server 为实体二进制文件（消除软链接）
echo "[2/6] Embedding native llama-server binary..."
rm -f "${DEST_APP}/Contents/MacOS/llama-server"
cp -f "${LLAMA_SERVER}" "${DEST_APP}/Contents/MacOS/llama-server"
chmod +x "${DEST_APP}/Contents/MacOS/llama-server"

# 4. 创建 Frameworks 目录，执行依赖递归收集与 @rpath 重定位
echo "[3/6] Resolving and bundling dynamic libraries (dylibs)..."
mkdir -p "${DEST_APP}/Contents/Frameworks"

/usr/bin/python3 - <<PY_SCRIPT
import subprocess
import re
import os
import shutil

build_dir = "${BUILD_DIR}"
dest_app = "${DEST_APP}"
frameworks_dir = os.path.join(dest_app, "Contents/Frameworks")
macos_dir = os.path.join(dest_app, "Contents/MacOS")

search_paths = [
    os.path.join(build_dir, "bin"),
    os.path.join(build_dir, "third_party/wxWidgets/lib"),
    "/opt/homebrew/opt/openssl@3/lib",
    "/usr/local/opt/openssl@3/lib"
]

def get_dependencies(file_path):
    try:
        out = subprocess.check_output(["otool", "-L", file_path]).decode("utf-8")
    except Exception as e:
        print(f"  Warning: failed to read otool -L for {file_path}: {e}")
        return []
    deps = []
    for line in out.splitlines()[1:]:
        m = re.search(r'^\s+(\S+)', line)
        if m:
            dep = m.group(1)
            if not dep.startswith("/System/") and not dep.startswith("/usr/lib/"):
                deps.append(dep)
    return deps

def resolve_dep(dep):
    if dep.startswith("@rpath/"):
        name = dep[len("@rpath/"):]
        for sp in search_paths:
            candidate = os.path.join(sp, name)
            if os.path.exists(candidate):
                return candidate
    elif os.path.exists(dep):
        return dep
    else:
        # Check by basename in search paths
        bname = os.path.basename(dep)
        for sp in search_paths:
            candidate = os.path.join(sp, bname)
            if os.path.exists(candidate):
                return candidate
    return None

root_binaries = [
    os.path.join(macos_dir, "LinguaAlpaca"),
    os.path.join(macos_dir, "llama-server")
]

visited_reals = set()
queue = list(root_binaries)
dep_mapping = {}  # dep_raw_string -> resolved_file_path

while queue:
    curr = queue.pop(0)
    real_curr = os.path.realpath(curr)
    if real_curr in visited_reals:
        continue
    visited_reals.add(real_curr)
    for dep in get_dependencies(real_curr):
        resolved = resolve_dep(dep)
        if resolved:
            dep_mapping[dep] = resolved
            real_resolved = os.path.realpath(resolved)
            if real_resolved not in visited_reals:
                queue.append(real_resolved)

print(f"  Found {len(dep_mapping)} non-system library dependencies.")

# 复制动态库至 Contents/Frameworks/
copied_dylibs = set()
for dep_str, src_path in dep_mapping.items():
    real_src = os.path.realpath(src_path)
    bname = os.path.basename(src_path)
    real_bname = os.path.basename(real_src)
    dest_real = os.path.join(frameworks_dir, real_bname)

    if not os.path.exists(dest_real):
        shutil.copy2(real_src, dest_real)
        os.chmod(dest_real, 0o755)
        copied_dylibs.add(dest_real)

    # 若引用名称与实际文件名不同（如 libllama.0.dylib -> libllama.0.0.10206.dylib），建立对应符号链接
    if bname != real_bname:
        alias_link = os.path.join(frameworks_dir, bname)
        if os.path.lexists(alias_link):
            os.remove(alias_link)
        os.symlink(real_bname, alias_link)

print(f"  Copied {len(copied_dylibs)} unique dylib binaries to Frameworks.")

# 获取 Frameworks 下所有文件
all_framework_files = [os.path.join(frameworks_dir, f) for f in os.listdir(frameworks_dir)]
all_binaries = root_binaries + [f for f in all_framework_files if not os.path.islink(f)]

# 建立库重定位映射规则
# 只要某个依赖出现在 dep_mapping 中，就统一将其映射为 @rpath/<referenced_basename>
relocation_map = {}
for dep_str, src_path in dep_mapping.items():
    bname = os.path.basename(src_path)
    relocation_map[dep_str] = f"@rpath/{bname}"

# 特殊处理：Homebrew Cellar 具体版本全路径也一并加入重定位表
for f in os.listdir(frameworks_dir):
    relocation_map[f] = f"@rpath/{f}"

# 修复每个二进制文件 / 动态库的 ID 与引用路径
for bin_path in all_binaries:
    is_dylib = bin_path.endswith(".dylib") or ".dylib." in bin_path

    # 1. 如果是动态库，重设自身 ID 为 @rpath/<basename>
    if is_dylib:
        bname = os.path.basename(bin_path)
        subprocess.run(["install_name_tool", "-id", f"@rpath/{bname}", bin_path], check=True)

    # 2. 修改它所引用的外部依赖路径为 @rpath
    for old_dep in get_dependencies(bin_path):
        if old_dep in relocation_map:
            new_dep = relocation_map[old_dep]
            if old_dep != new_dep:
                subprocess.run(["install_name_tool", "-change", old_dep, new_dep, bin_path], check=False)

    # 3. 清除旧的 LC_RPATH，统一加入 @executable_path / @loader_path
    try:
        otool_l = subprocess.check_output(["otool", "-l", bin_path]).decode("utf-8")
        current_rpaths = re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (\S+)', otool_l)
    except Exception:
        current_rpaths = []

    for rpath in current_rpaths:
        subprocess.run(["install_name_tool", "-delete_rpath", rpath, bin_path], check=False)

    if is_dylib:
        subprocess.run(["install_name_tool", "-add_rpath", "@loader_path", bin_path], check=False)
        subprocess.run(["install_name_tool", "-add_rpath", "@loader_path/../Frameworks", bin_path], check=False)
    else:
        subprocess.run(["install_name_tool", "-add_rpath", "@executable_path/../Frameworks", bin_path], check=False)

print("  All install names and LC_RPATHs relocated successfully.")
PY_SCRIPT

# 5. 确保 Resources 资源目录及图标完整
echo "[4/6] Verifying resources and Info.plist..."
mkdir -p "${DEST_APP}/Contents/Resources"
if [ -d "${PROJECT_ROOT}/resources" ]; then
    cp -R "${PROJECT_ROOT}/resources" "${DEST_APP}/Contents/Resources/resources"
fi
if [ -f "${PROJECT_ROOT}/resources/app_icon.icns" ]; then
    cp -f "${PROJECT_ROOT}/resources/app_icon.icns" "${DEST_APP}/Contents/Resources/app_icon.icns"
fi

# 6. 递归执行 Ad-hoc 代码签名 (Apple Silicon 兼容性保障)
echo "[5/6] Performing recursive Ad-hoc code signing..."
# 先对 Frameworks 目录所有动态库单独签名
find "${DEST_APP}/Contents/Frameworks" -type f -name "*.dylib" -o -name "*.dylib.*" | while read -r dylib; do
    codesign --force --sign - --timestamp=none "${dylib}"
done

# 对 MacOS 二进制签名
codesign --force --sign - --timestamp=none "${DEST_APP}/Contents/MacOS/llama-server"
codesign --force --sign - --timestamp=none "${DEST_APP}/Contents/MacOS/LinguaAlpaca"

# 对整个 App Bundle 签名
codesign --force --deep --sign - --timestamp=none "${DEST_APP}"

# 验证签名有效性
echo "  Verifying code signature integrity..."
codesign --verify --deep --strict --verbose=2 "${DEST_APP}" 2>&1 | sed 's/^/  [codesign] /'

# 7. 制作 DMG 安装镜像与自定义图标 (同时支持 DMG 文件图标与挂载卷宗图标)
echo "[6/6] Generating macOS DMG installer with custom volume & file icons..."
DMG_STAGING="${DIST_DIR}/dmg_staging"
TMP_RW_DMG="${DIST_DIR}/temp_rw.dmg"
TMP_MOUNT="${DIST_DIR}/temp_mount"

rm -rf "${DMG_STAGING}" "${TMP_RW_DMG}" "${TMP_MOUNT}" "${DMG_PATH}"
mkdir -p "${DMG_STAGING}" "${TMP_MOUNT}"

cp -R "${DEST_APP}" "${DMG_STAGING}/"
ln -s /Applications "${DMG_STAGING}/Applications"

ICON_FILE="${PROJECT_ROOT}/resources/app_icon.icns"

if [ -f "${ICON_FILE}" ]; then
    cp -f "${ICON_FILE}" "${DMG_STAGING}/.VolumeIcon.icns"
    
    # 先创建可读写 (UDRW) 临时镜像以持久化卷宗图标属性
    hdiutil create \
        -srcfolder "${DMG_STAGING}" \
        -fs HFS+ \
        -format UDRW \
        -volname "${APP_NAME}" \
        -ov \
        "${TMP_RW_DMG}" > /dev/null

    # 挂载临时镜像，标记 Finder 卷宗自定义图标属性 (SetFile -a C)
    hdiutil attach "${TMP_RW_DMG}" -mountpoint "${TMP_MOUNT}" -nobrowse -quiet
    SetFile -c icnC "${TMP_MOUNT}/.VolumeIcon.icns" 2>/dev/null || true
    SetFile -a C "${TMP_MOUNT}" 2>/dev/null || true
    SetFile -a V "${TMP_MOUNT}/.VolumeIcon.icns" 2>/dev/null || true
    hdiutil detach "${TMP_MOUNT}" -quiet

    # 转换为高度压缩的只读 UDZO 格式最终发布镜像
    hdiutil convert "${TMP_RW_DMG}" -format UDZO -o "${DMG_PATH}" -ov > /dev/null
    rm -f "${TMP_RW_DMG}"
else
    hdiutil create \
        -volname "${APP_NAME}" \
        -srcfolder "${DMG_STAGING}" \
        -ov \
        -format UDZO \
        "${DMG_PATH}" > /dev/null
fi

rm -rf "${DMG_STAGING}" "${TMP_MOUNT}"

# 为 .dmg 镜像文件本身注入自定义文件图标 (在访达中直接查看 dmg 时的图标)
if [ -f "${ICON_FILE}" ] && command -v swift >/dev/null 2>&1; then
    swift -e '
    import Cocoa
    if CommandLine.arguments.count >= 3,
       let image = NSImage(contentsOfFile: CommandLine.arguments[1]) {
        _ = NSWorkspace.shared.setIcon(image, forFile: CommandLine.arguments[2], options: [])
    }
    ' "${ICON_FILE}" "${DMG_PATH}" 2>/dev/null || true
fi

echo "================================================================================"
echo " [LinguaAlpaca] macOS Packaging Completed Successfully!"
echo "================================================================================"
echo " Standalone App: ${DEST_APP}"
echo " App Size:       $(du -sh "${DEST_APP}" | awk '{print $1}')"
echo " DMG Installer:  ${DMG_PATH}"
echo " DMG Size:       $(du -sh "${DMG_PATH}" | awk '{print $1}')"
echo "================================================================================"
