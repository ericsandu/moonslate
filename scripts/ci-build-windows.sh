#!/bin/bash
# Builds and packages Moonslate for Windows x86_64. Runs under git-bash on a
# GitHub Windows runner after ilammy/msvc-dev-cmd has put MSVC in PATH.
# Requires QT_ROOT_DIR (install-qt-action) to point at the Qt installation.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
JOBS="$(nproc)"
: "${QT_ROOT_DIR:?QT_ROOT_DIR must point at the Qt installation}"


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

echo "[2] Building Moonshine core (MSVC)..."
# Build moonshine as a DLL like on the other platforms; the Windows default is
# a static moonshine.lib whose private static-library dependencies
# (moonshine-utils, ort-utils) are not linked into consumer binaries.
cmake -G Ninja -S moonshine/core -B moonshine/core/build -DCMAKE_BUILD_TYPE=Release \
    -DMOONSHINE_BUILD_SHARED=ON ${EXTRA_CMAKE_FLAGS:-}
cmake --build moonshine/core/build --target moonshine -j"$JOBS"

echo "[3] Building Moonslate app (CTranslate2, SentencePiece, Qt)..."
cmake -G Ninja -S app -B app/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" ${EXTRA_CMAKE_FLAGS:-}
perl -pi -e 's/#include <vector>/#include <vector>\n#include <cstdint>/' \
    app/build/_deps/sentencepiece-src/src/sentencepiece_processor.h || true
# CTranslate2 pins the static MSVC runtime when built as a static library, but
# Qt's MSVC binaries (and our app) require the dynamic /MD runtime; Ninja
# re-configures automatically because the touched CMakeLists is a dependency.
perl -pi -e 's/"MultiThreaded\$</"MultiThreadedDLL\$</' \
    app/build/_deps/ctranslate2-src/CMakeLists.txt || true
cmake --build app/build --target moonslate_app -j"$JOBS"

echo "[4] Packaging (windeployqt gathers the Qt runtime)..."
rm -rf package
mkdir -p package/moonslate/bin package/moonslate/share/moonslate/models package/moonslate/share/moonslate/tts-data
cp app/build/moonslate_app.exe package/moonslate/bin/
cp moonshine/core/build/moonshine.dll package/moonslate/bin/
cp moonshine/core/third-party/onnxruntime/lib/windows/x86_64/onnxruntime.dll package/moonslate/bin/
cp README.md LICENSE package/moonslate/
# --qmldir makes windeployqt scan the QML sources for imports and deploy the
# Qt Quick modules the qrc-compiled shell needs.
"$QT_ROOT_DIR/bin/windeployqt.exe" package/moonslate/bin/moonslate_app.exe --release --no-translations \
    --qmldir "$ROOT/app/qml"

echo "[5] Headless selftest..."
QT_QPA_PLATFORM=offscreen package/moonslate/bin/moonslate_app.exe --selftest

cd package
7z a ../moonslate-windows-x86_64.zip moonslate

echo "=== Windows build complete ==="
