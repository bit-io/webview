#!/bin/sh
# Builds a shared libwebview (0.12.0) from upstream into native/. Linux: needs git, cmake, a C++ compiler,
# and the dev packages for GTK 3 + WebKitGTK:
#   Debian/Ubuntu: sudo apt install git cmake g++ pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev
#   Fedora:        sudo dnf install git cmake gcc-c++ pkgconf gtk3-devel webkit2gtk4.1-devel
#   Arch:          sudo pacman -S git cmake gcc pkgconf gtk3 webkit2gtk-4.1
# A SHARED build is used on purpose: libwebview.so records its own GTK/WebKit dependencies, so your H#
# program needs no extra link flags.
set -e
cd "$(dirname "$0")"
TAG="${WEBVIEW_TAG:-0.12.0}"
SRC="build-src/webview"
mkdir -p build-src
[ -d "$SRC" ] || git clone --depth 1 --branch "$TAG" https://github.com/webview/webview "$SRC"
cmake -S "$SRC" -B build-src/out -DCMAKE_BUILD_TYPE=Release \
    -DWEBVIEW_BUILD_SHARED_LIBRARY=ON -DWEBVIEW_BUILD_STATIC_LIBRARY=OFF \
    -DWEBVIEW_BUILD_TESTS=OFF -DWEBVIEW_BUILD_EXAMPLES=OFF -DWEBVIEW_BUILD_DOCS=OFF \
    -DWEBVIEW_INSTALL_TARGETS=OFF -DWEBVIEW_ENABLE_PACKAGING=OFF
cmake --build build-src/out --config Release
cp -P build-src/out/core/libwebview.so* . 2>/dev/null || cp -P $(find build-src/out -name 'libwebview.so*' | head -n 3) .
echo "built native/libwebview.so (run your program with LD_LIBRARY_PATH=<this dir> or install it system-wide)"
