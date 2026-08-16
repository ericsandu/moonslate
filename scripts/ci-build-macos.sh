#!/bin/bash
# Builds and packages Moonslate for macOS arm64 as a signed .app bundle.
# Requires Qt from Homebrew (brew install qt).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
JOBS="$(sysctl -n hw.ncpu)"
QT_PREFIX="$(brew --prefix qt)"


# Wipe build directories configured with a different generator (e.g. a stale
# local Makefiles build) so the Ninja configure below succeeds.
for dir in moonshine/core/build app/build; do
    if [ -f "$dir/CMakeCache.txt" ] && ! grep -q "CMAKE_GENERATOR:INTERNAL=Ninja" "$dir/CMakeCache.txt"; then
        echo "Removing stale non-Ninja build directory: $dir"
        rm -rf "$dir"
    fi
done

echo "[1] Applying moonshine patches..."
bash scripts/apply-moonshine-patches.sh

echo "[2] Building Moonshine core..."
cmake -G Ninja -S moonshine/core -B moonshine/core/build -DCMAKE_BUILD_TYPE=Release ${EXTRA_CMAKE_FLAGS:-}
cmake --build moonshine/core/build --target moonshine -j"$JOBS"

echo "[3] Building Moonslate app (CTranslate2, SentencePiece, Qt)..."
cmake -G Ninja -S app -B app/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX" ${EXTRA_CMAKE_FLAGS:-}
perl -pi -e 's/#include <vector>/#include <vector>\n#include <cstdint>/' \
    app/build/_deps/sentencepiece-src/src/sentencepiece_processor.h || true
cmake --build app/build --target moonslate_app -j"$JOBS"

echo "[4] Packaging .app bundle..."
rm -rf package
mkdir -p package
APP=app/build/Moonslate.app
"$QT_PREFIX/bin/macdeployqt" "$APP" -always-overwrite
codesign --force --deep --sign - "$APP"

echo "[5] Headless selftest..."
QT_QPA_PLATFORM=offscreen "$APP/Contents/MacOS/Moonslate" --selftest

ditto -c -k --keepParent "$APP" package/moonslate-macos-arm64.zip

echo "=== macOS build complete: package/moonslate-macos-arm64.zip ==="
