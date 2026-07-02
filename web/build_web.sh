#!/usr/bin/env bash
# Builds the SaidaEngine web runtime (Étape 16.4/16.6). Run from the repo root.
#
#   ./web/build_web.sh [Release|Debug]
#
# Requires:
#   - emsdk (activated automatically below if found at ~/emsdk)
#   - the WGSL shaders: cmake --build build --target WebShaders
#
# Output: build-web/index.html + index.js + index.wasm + index.data
# Serve:  python web/serve.py build-web

set -e
CONFIG="${1:-Release}"

if ! command -v emcmake >/dev/null 2>&1; then
    for candidate in "$HOME/emsdk/emsdk_env.sh" "/c/Users/evand/emsdk/emsdk_env.sh"; do
        if [ -f "$candidate" ]; then
            # shellcheck disable=SC1090
            source "$candidate" >/dev/null 2>&1
            break
        fi
    done
fi
command -v emcmake >/dev/null 2>&1 || { echo "emsdk not found — activate it first"; exit 1; }

if [ ! -f build/shaders/wgsl/shader.frag.wgsl ]; then
    echo "WGSL shaders missing — run: cmake --build build --target WebShaders"
    exit 1
fi

emcmake cmake -S web/runtime -B build-web -G Ninja -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build build-web

echo
echo "sizes:"
ls -la build-web/index.* | awk '{printf "  %-18s %10d bytes\n", $NF, $5}'
echo
echo "run: python web/serve.py build-web   ->   http://localhost:8080/index.html"
