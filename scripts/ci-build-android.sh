#!/bin/bash
# Cross-builds Moonslate for Android arm64-v8a on a linux/macOS host and
# packages a debug-signed APK. Basic CI: no selftest (no emulator), no release
# signing (keystore management is a follow-up).
#
# Required environment:
#   QT_ROOT_DIR      Qt for Android prefix (host tools + android arm64 libs)
#   ANDROID_SDK_ROOT Android SDK (github runners provide it preinstalled)
#   ANDROID_NDK_HOME Android NDK (github runners provide it preinstalled)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
ABI=arm64-v8a
PLATFORM=android-28

: "${QT_ROOT_DIR:?QT_ROOT_DIR must point to the Qt for Android prefix}"
: "${ANDROID_SDK_ROOT:?ANDROID_SDK_ROOT must be set}"
: "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must be set}"
TOOLCHAIN="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"

for dir in moonshine/core/build app/build; do
    if [ -f "$dir/CMakeCache.txt" ] && ! grep -q "CMAKE_GENERATOR:INTERNAL=Ninja" "$dir/CMakeCache.txt"; then
        echo "Removing stale non-Ninja build directory: $dir"
        rm -rf "$dir"
    fi
done

echo "[1] Applying moonshine patches..."
bash scripts/apply-moonshine-patches.sh

echo "[2] Building Moonshine core (android $ABI)..."
cmake -G Ninja -S moonshine/core -B moonshine/core/build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DANDROID_ABI=$ABI -DANDROID_PLATFORM=$PLATFORM \
    -DANDROID_STL=c++_shared -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" ${EXTRA_CMAKE_FLAGS:-}
cmake --build moonshine/core/build --target moonshine -j"$JOBS"

echo "[3] Building Moonslate app (android $ABI)..."
cmake -G Ninja -S app -B app/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DANDROID_ABI=$ABI -DANDROID_PLATFORM=$PLATFORM \
    -DANDROID_STL=c++_shared -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" ${EXTRA_CMAKE_FLAGS:-}
perl -pi -e 's/#include <vector>/#include <vector>\n#include <cstdint>/' \
    app/build/_deps/sentencepiece-src/src/sentencepiece_processor.h || true
cmake --build app/build --target moonslate_app -j"$JOBS"

echo "[4] Packaging APK with androiddeployqt + gradle..."
rm -rf package
mkdir -p package
"$QT_ROOT_DIR/bin/androiddeployqt" \
    --input app/build/android-moonslate_app-deployment-settings.json \
    --output app/build/android-build \
    --android-platform android-34 \
    --gradle

cp app/build/android-build/build/outputs/apk/debug/android-build-debug.apk \
   package/moonslate-android-arm64-debug.apk

echo "=== Android build complete: package/moonslate-android-arm64-debug.apk ==="
