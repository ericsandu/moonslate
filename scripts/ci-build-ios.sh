#!/bin/bash
# Cross-builds Moonslate for iOS arm64 (device) as an unsigned .app.
# Basic CI: no selftest (no simulator boot), no codesigning (signing secrets
# are a follow-up); the artifact is for developer sideloading via Xcode.
#
# Required environment:
#   QT_ROOT_DIR  Qt for iOS prefix (from aqt target 'ios', arch 'ios')
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
JOBS="$(sysctl -n hw.ncpu)"
QT_CMAKE="${QT_ROOT_DIR:?QT_ROOT_DIR must point to the Qt for iOS prefix}/bin/qt-cmake"

for dir in moonshine/core/build app/build; do
    if [ -f "$dir/CMakeCache.txt" ] && ! grep -q "CMAKE_GENERATOR:INTERNAL=Ninja" "$dir/CMakeCache.txt"; then
        echo "Removing stale non-Ninja build directory: $dir"
        rm -rf "$dir"
    fi
done

echo "[1] Applying moonshine patches..."
bash scripts/apply-moonshine-patches.sh
sed -i 's/-Werror//g' moonshine/core/CMakeLists.txt

echo "[2] Building Moonshine core (iOS arm64, static)..."
cmake -G Ninja -S moonshine/core -B moonshine/core/build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" ${EXTRA_CMAKE_FLAGS:-}
cmake --build moonshine/core/build --target moonshine -j"$JOBS"

echo "[3] Building Moonslate app (iOS arm64)..."
# CMAKE_PROJECT_INCLUDE defines the Xcode-only helper sentencepiece calls on
# iOS so its FetchContent configure survives under Ninja (see cmake/ios-compat.cmake).
"$QT_ROOT_DIR/bin/qt-cmake" -G Ninja -S app -B app/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_PROJECT_INCLUDE="$ROOT/cmake/ios-compat.cmake" \
    -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" ${EXTRA_CMAKE_FLAGS:-}
perl -pi -e 's/#include <vector>/#include <vector>\n#include <cstdint>/' \
    app/build/_deps/sentencepiece-src/src/sentencepiece_processor.h || true
cmake --build app/build --target moonslate_app -j"$JOBS"

echo "[4] Packaging unsigned .app..."
rm -rf package
mkdir -p package
APP="$(find app/build -maxdepth 2 -name '*.app' -type d | head -n1)"
if [ -z "$APP" ]; then
    echo "error: no .app bundle produced" >&2
    exit 1
fi
echo "bundle: $APP"
ditto -c -k --keepParent "$APP" package/moonslate-ios-arm64-unsigned.zip

echo "=== iOS build complete: package/moonslate-ios-arm64-unsigned.zip ==="
