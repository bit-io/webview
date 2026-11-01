#!/bin/sh
# Builds native/libhwebview.a (the callback shim). Needs only a C compiler and `ar`.
# libwebview itself (>= 0.12.0) must be installed, or build it with HWV_BUILD_WEBVIEW=1 (see build-webview.sh).
set -e
cd "$(dirname "$0")"
CC="${CC:-cc}"

"$CC" -std=c11 -O2 -fPIC -Wall -c hwebview.c -o hwebview.o
ar rcs libhwebview.a hwebview.o
rm -f hwebview.o
echo "built native/libhwebview.a"

have_webview() {
    for d in . /usr/lib /usr/lib64 /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu; do
        ls "$d"/libwebview.so* "$d"/libwebview.a >/dev/null 2>&1 && return 0
    done
    for d in $(echo "${LIBRARY_PATH:-}" | tr ':' ' '); do
        ls "$d"/libwebview.so* "$d"/libwebview.a >/dev/null 2>&1 && return 0
    done
    return 1
}

if have_webview; then
    echo "libwebview found"
elif [ "${HWV_BUILD_WEBVIEW:-0}" = "1" ]; then
    sh ./build-webview.sh
else
    echo "NOTE: libwebview not found. Install it, or re-run with HWV_BUILD_WEBVIEW=1 to build 0.12.0 from source." >&2
fi
