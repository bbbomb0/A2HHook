#!/bin/bash
# ============================================================
# A2HHook - Build Script
# Requirements:
#   - Android NDK (r26+ recommended)
#   - CMake 3.18+
#   - Dobby library (prebuilt or source)
#
# Usage:
#   ./build.sh              # Build with NDK
#   ./build.sh clean        # Clean build artifacts
#   ./build.sh zip          # Build and create KernelSU ZIP
#   ./build.sh ci           # CI mode: clone Dobby, build static, package
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE_DIR="$SCRIPT_DIR"
SRC_DIR="$MODULE_DIR/src"
BUILD_DIR="$MODULE_DIR/build"
OUTPUT_DIR="$MODULE_DIR/zygisk/arm64-v8a"
LIBS_DIR="$MODULE_DIR/libs"

# NDK configuration
ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-$HOME/Android/Sdk/ndk/27.0.12077973}"
API_LEVEL=31  # Android 12+ for Zygisk

# Colors
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# ============================================================
# Check prerequisites
# ============================================================

check_prereqs() {
    info "Checking prerequisites..."

    # Auto-detect NDK
    if [ ! -d "$ANDROID_NDK_HOME" ]; then
        for ndk_path in \
            "$HOME/Android/Sdk/ndk"/* \
            "/usr/local/lib/android/sdk/ndk"/* \
            "$ANDROID_HOME/ndk"/*; do
            [ -d "$ndk_path/toolchains/llvm/prebuilt" ] && { ANDROID_NDK_HOME="$ndk_path"; break; }
        done
    fi

    [ ! -d "$ANDROID_NDK_HOME" ] && error "Android NDK not found. Set ANDROID_NDK_HOME."
    info "NDK: $ANDROID_NDK_HOME"

    # Host tag
    case "$(uname -s)" in
        Linux)  HOST_TAG="linux-x86_64" ;;
        Darwin) HOST_TAG="darwin-x86_64" ;;
        MINGW*|MSYS*|CYGWIN*) HOST_TAG="windows-x86_64" ;;
        *) error "Unsupported OS" ;;
    esac

TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_TAG"
[ ! -d "$TOOLCHAIN" ] && error "Toolchain not found: $TOOLCHAIN"

# CMake
CMAKE="cmake"
command -v cmake &>/dev/null || CMAKE="$ANDROID_NDK_HOME/build/cmake/3.22.1/bin/cmake"
info "CMake: $CMAKE"
if command -v ninja &>/dev/null; then
    CMAKE_GENERATOR="-G Ninja"
    info "Generator: Ninja"
else
    CMAKE_GENERATOR=""
fi
}

# ============================================================
# Prepare Dobby
# ============================================================

prepare_dobby() {
    info "Preparing Dobby..."

    # Static lib (preferred)
    if [ -f "$LIBS_DIR/libdobby.a" ]; then
        info "Using prebuilt libdobby.a (static)"
        return 0
    fi

    # Shared lib
    if [ -f "$LIBS_DIR/libdobby.so" ]; then
        info "Using prebuilt libdobby.so (shared)"
        return 0
    fi

    # Clone and build from source
    DOBBY_SRC=""
    for p in "$MODULE_DIR/dobby" "$MODULE_DIR/../dobby" "$HOME/dobby"; do
        [ -f "$p/include/dobby.h" ] && { DOBBY_SRC="$p"; break; }
    done

    if [ -z "$DOBBY_SRC" ]; then
        info "Cloning Dobby..."
        git clone --depth 1 https://github.com/jmpews/Dobby.git "$MODULE_DIR/dobby"
        DOBBY_SRC="$MODULE_DIR/dobby"
    fi

    info "Building Dobby from source..."
    DOBBY_BUILD="$BUILD_DIR/dobby"
    rm -rf "$DOBBY_BUILD"
    mkdir -p "$DOBBY_BUILD"
    cd "$DOBBY_BUILD"

    "$CMAKE" "$DOBBY_SRC" \
        $CMAKE_GENERATOR \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a \
        -DANDROID_PLATFORM="$API_LEVEL" \
        -DCMAKE_BUILD_TYPE=Release \
        -DDOBBY_DEBUG=OFF \
        -DDOBBY_GENERATE_SHARED=OFF \
        -DDobbyStatic=ON \
        || error "Dobby CMake failed"

    cmake --build . -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) \
        || error "Dobby build failed"

    mkdir -p "$LIBS_DIR" "$LIBS_DIR/include"
    cp libdobby.a "$LIBS_DIR/" 2>/dev/null || true
    cp "$DOBBY_SRC/include/dobby.h" "$LIBS_DIR/include/" 2>/dev/null || true

    cd "$SCRIPT_DIR"
    info "Dobby built: libdobby.a"
}

# ============================================================
# Build the Zygisk module
# ============================================================

build_module() {
    info "Building a2h_hook.so..."

    rm -rf "$BUILD_DIR/arm64-v8a"
    mkdir -p "$BUILD_DIR/arm64-v8a" "$OUTPUT_DIR"
    cd "$BUILD_DIR/arm64-v8a"

    DOBBY_INC="$LIBS_DIR/include"
    [ -d "$DOBBY_INC" ] || DOBBY_INC="$SRC_DIR"  # use bundled dobby.h

    "$CMAKE" "$SRC_DIR" \
        $CMAKE_GENERATOR \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a \
        -DANDROID_PLATFORM="$API_LEVEL" \
        -DANDROID_STL=c++_static \
        -DCMAKE_BUILD_TYPE=Release \
        -DDOBBY_INCLUDE_DIR="$DOBBY_INC" \
        -DDOBBY_LIB_DIR="$LIBS_DIR" \
        || error "CMake configure failed"

    cmake --build . -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) \
        || error "Build failed"

    # Strip
    if [ -f "$OUTPUT_DIR/a2h_hook.so" ]; then
        "$TOOLCHAIN/bin/llvm-strip" --strip-all "$OUTPUT_DIR/a2h_hook.so" 2>/dev/null || true
        info "Output: $OUTPUT_DIR/a2h_hook.so"
        ls -lh "$OUTPUT_DIR/a2h_hook.so"
    else
        error "a2h_hook.so not found in output directory"
    fi

    cd "$SCRIPT_DIR"
}

# ============================================================
# Create KernelSU/Magisk ZIP
# ============================================================

create_zip() {
    [ ! -f "$OUTPUT_DIR/a2h_hook.so" ] && error "a2h_hook.so not found. Build first."
    [ ! -f "$MODULE_DIR/bin/a2h_patch" ] && error "a2h_patch not found. Build first."
    [ ! -f "$MODULE_DIR/bin/a2h_trigger" ] && error "a2h_trigger not found. Build first."

    MODULE_VERSION="$(sed -n 's/^version=//p' "$MODULE_DIR/module.prop" | head -n 1)"
    [ -z "$MODULE_VERSION" ] && error "Cannot read version from module.prop"
    ZIP_NAME="a2h_hook_${MODULE_VERSION}.zip"
    ZIP_PATH="$MODULE_DIR/$ZIP_NAME"
    rm -f "$ZIP_PATH"

    TEMP_ZIP="$BUILD_DIR/zip"
    rm -rf "$TEMP_ZIP"
    mkdir -p "$TEMP_ZIP/bin" "$TEMP_ZIP/zygisk/arm64-v8a"

    cp "$MODULE_DIR/module.prop"  "$TEMP_ZIP/"
    cp "$MODULE_DIR/customize.sh" "$TEMP_ZIP/"
    cp "$MODULE_DIR/service.sh"    "$TEMP_ZIP/"
    cp "$MODULE_DIR/post-fs-data.sh" "$TEMP_ZIP/"
    cp "$MODULE_DIR/wrapper.sh"    "$TEMP_ZIP/"
    cp "$MODULE_DIR/share_logs.sh" "$TEMP_ZIP/"
    cp "$MODULE_DIR/webui.png"     "$TEMP_ZIP/"
    cp -r "$MODULE_DIR/config"    "$TEMP_ZIP/"
    cp -r "$MODULE_DIR/webroot"    "$TEMP_ZIP/"
    cp "$MODULE_DIR/bin/a2h_patch" "$TEMP_ZIP/bin/"
    cp "$MODULE_DIR/bin/a2h_trigger" "$TEMP_ZIP/bin/"
    cp "$MODULE_DIR/bin/a2h_apply" "$TEMP_ZIP/bin/"
    cp "$OUTPUT_DIR/a2h_hook.so" "$TEMP_ZIP/zygisk/arm64-v8a/"

    cd "$TEMP_ZIP"
    chmod 0755 customize.sh service.sh post-fs-data.sh wrapper.sh share_logs.sh bin/a2h_apply bin/a2h_patch bin/a2h_trigger 2>/dev/null || true
    if command -v zip &>/dev/null; then
        zip -r "$ZIP_PATH" . -x "*.git*" -x "*__MACOSX*" -x "*.DS_Store"
        # NTFS/MSYS does not reliably preserve executable bits. Normalize the
        # ZIP metadata so local Windows builds match Linux/CI artifacts.
        PYTHON_BIN=""
        for py in python3 python py; do
            if command -v "$py" >/dev/null 2>&1 && "$py" -c "import sys, zipfile" >/dev/null 2>&1; then
                PYTHON_BIN="$py"
                break
            fi
        done
        if [ -n "$PYTHON_BIN" ]; then
            ZIP_PATH_PY="$ZIP_PATH"
            if command -v cygpath &>/dev/null; then
                ZIP_PATH_PY="$(cygpath -w "$ZIP_PATH" 2>/dev/null || printf '%s' "$ZIP_PATH")"
            fi
            "$PYTHON_BIN" - "$ZIP_PATH_PY" <<'PY'
import os
import sys
import tempfile
import zipfile

source = sys.argv[1]
fd, target = tempfile.mkstemp(prefix="a2h_zip_", suffix=".zip", dir=os.path.dirname(source) or ".")
os.close(fd)
try:
    executable = {
        "customize.sh", "service.sh", "post-fs-data.sh", "wrapper.sh",
        "share_logs.sh", "bin/a2h_apply", "bin/a2h_patch", "bin/a2h_trigger",
    }
    with zipfile.ZipFile(source, "r") as zin, zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as zout:
        for old in zin.infolist():
            info = zipfile.ZipInfo(old.filename, old.date_time)
            info.create_system = 3
            if old.is_dir():
                info.compress_type = zipfile.ZIP_STORED
                info.external_attr = (0o040755 << 16) | 0x10
                data = b""
            else:
                info.compress_type = zipfile.ZIP_DEFLATED
                mode = 0o100755 if old.filename in executable else 0o100644
                info.external_attr = mode << 16
                data = zin.read(old.filename)
            zout.writestr(info, data)
    os.replace(target, source)
finally:
    if os.path.exists(target):
        os.unlink(target)
PY
        else
            case "$(uname -s 2>/dev/null || true)" in
                MINGW*|MSYS*|CYGWIN*)
                    error "Python 3 is required on Windows to normalize ZIP permissions."
                    ;;
            esac
        fi
    else
        PYTHON_BIN=""
        for py in python3 python py; do
            if command -v "$py" >/dev/null 2>&1 && "$py" -c "import sys" >/dev/null 2>&1; then
                PYTHON_BIN="$py"
                break
            fi
        done
        [ -z "$PYTHON_BIN" ] && error "Cannot create ZIP. Install 'zip' or Python 3."
        ZIP_PATH_PY="$ZIP_PATH"
        if command -v cygpath >/dev/null 2>&1; then
            ZIP_PATH_PY="$(cygpath -w "$ZIP_PATH" 2>/dev/null || printf '%s' "$ZIP_PATH")"
        fi
        "$PYTHON_BIN" - "$ZIP_PATH_PY" <<'PY' || error "Cannot create ZIP. Install 'zip' or Python 3."
import os
import sys
import zipfile

zip_path = sys.argv[1]
executable = {
    "customize.sh", "service.sh", "post-fs-data.sh", "wrapper.sh",
    "share_logs.sh", "bin/a2h_apply", "bin/a2h_patch", "bin/a2h_trigger",
}
with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
    for root, dirs, files in os.walk("."):
        dirs[:] = [d for d in dirs if d not in (".git", "__MACOSX")]
        for filename in files:
            if filename == ".DS_Store":
                continue
            path = os.path.join(root, filename)
            arc = os.path.relpath(path, ".").replace(os.sep, "/")
            info = zipfile.ZipInfo.from_file(path, arc)
            info.create_system = 3
            mode = 0o100755 if arc in executable else 0o100644
            info.external_attr = mode << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as source:
                zf.writestr(info, source.read())
PY
    fi

    cd "$SCRIPT_DIR"
    rm -rf "$TEMP_ZIP"

    info "ZIP created: $ZIP_PATH ($(du -h "$ZIP_PATH" | cut -f1))"
}

# ============================================================
# CI mode: full auto-build
# ============================================================

ci_build() {
    info "CI mode: auto-building everything..."
    check_prereqs
    prepare_dobby
    build_module
    create_zip
    info "CI build complete!"
}

# ============================================================
# Main
# ============================================================

main() {
    cd "$SCRIPT_DIR"
    case "${1:-build}" in
        clean)
            info "Cleaning..."
            rm -rf "$BUILD_DIR"
            rm -f "$OUTPUT_DIR/a2h_hook.so"
            find "$MODULE_DIR" -maxdepth 1 -name 'a2h_hook_*.zip' -type f -delete
            info "Done" ;;
        zip)    create_zip ;;
        ci)     ci_build ;;
        *)      check_prereqs; prepare_dobby; build_module
                info "Done! Run './build.sh zip' to package." ;;
    esac
}

main "$@"
