#!/bin/bash
# ============================================================
# A2HHook - Build Script
# Requirements:
#   - Android NDK (r26+ recommended)
#   - CMake 3.18+
#
# Usage:
#   ./build.sh              # Build with NDK
#   ./build.sh clean        # Clean build artifacts
#   ./build.sh zip          # Package the current binaries
#   ./build.sh ci           # CI mode: build native tools and package
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODULE_DIR="$SCRIPT_DIR"
SRC_DIR="$MODULE_DIR/src"
BUILD_DIR="$MODULE_DIR/build"

# NDK configuration
ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-$HOME/Android/Sdk/ndk/27.0.12077973}"
API_LEVEL="${API_LEVEL:-31}"  # Android 12+ native target

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
# Build the default native-only runtime
# ============================================================

build_module() {
    info "Building native patcher and trigger..."

    rm -rf "$BUILD_DIR/arm64-v8a"
    mkdir -p "$BUILD_DIR/arm64-v8a"
    cd "$BUILD_DIR/arm64-v8a"

    "$CMAKE" "$SRC_DIR" \
        $CMAKE_GENERATOR \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a \
        -DANDROID_PLATFORM="$API_LEVEL" \
        -DANDROID_STL=c++_static \
        -DCMAKE_BUILD_TYPE=Release \
        || error "CMake configure failed"

    cmake --build . -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) \
        || error "Build failed"

    [ -f "$MODULE_DIR/bin/a2h_patch" ] || error "a2h_patch not found"
    [ -f "$MODULE_DIR/bin/a2h_trigger" ] || error "a2h_trigger not found"
    info "Output: bin/a2h_patch, bin/a2h_trigger"

    cd "$SCRIPT_DIR"
}

# ============================================================
# Create KernelSU/Magisk ZIP
# ============================================================

create_zip() {
    [ ! -f "$MODULE_DIR/bin/a2h_patch" ] && error "a2h_patch not found. Build first."
    [ ! -f "$MODULE_DIR/bin/a2h_trigger" ] && error "a2h_trigger not found. Build first."

    PYTHON_BIN=""
    for py in python3 python py; do
        if command -v "$py" >/dev/null 2>&1 && "$py" -c "import sys, zipfile" >/dev/null 2>&1; then
            PYTHON_BIN="$py"
            break
        fi
    done
    [ -n "$PYTHON_BIN" ] || error "Python 3 is required to create the release ZIP."

    # package_module.py is the only release manifest. Keeping packaging in one
    # place prevents local and CI builds from recursively including temp files.
    cd "$MODULE_DIR"
    "$PYTHON_BIN" package_module.py . || error "Module packaging failed"

    MODULE_VERSION="$(sed -n 's/^version=//p' module.prop | head -n 1)"
    ZIP_PATH="$MODULE_DIR/a2h_hook_${MODULE_VERSION}.zip"
    [ -f "$ZIP_PATH" ] || error "Expected release ZIP was not created: $ZIP_PATH"
    info "ZIP created: $ZIP_PATH ($(du -h "$ZIP_PATH" | cut -f1))"
}

# ============================================================
# CI mode: full auto-build
# ============================================================

ci_build() {
    info "CI mode: auto-building everything..."
    check_prereqs
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
            MODULE_VERSION="$(sed -n 's/^version=//p' "$MODULE_DIR/module.prop" 2>/dev/null | head -n 1)"
            case "$MODULE_VERSION" in
                ''|*[!A-Za-z0-9._-]*) error "Invalid module version; refusing to clean release ZIPs" ;;
            esac
            rm -f "$MODULE_DIR/a2h_hook_${MODULE_VERSION}.zip"
            info "Done" ;;
        zip)    create_zip ;;
        ci)     ci_build ;;
        *)      check_prereqs; build_module
                info "Done! Run './build.sh zip' to package." ;;
    esac
}

main "$@"
