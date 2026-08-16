#!/bin/bash
# Builds and packages Moonslate for Linux x86_64/aarch64. Used by CI and by
# build.sh for local development. Set EXTRA_CMAKE_FLAGS to extend the
# configuration (CI uses it to enable ccache).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
JOBS="$(nproc)"


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

echo "[3] Building Moonslate app (CTranslate2, SentencePiece)..."
cmake -G Ninja -S app -B app/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ${EXTRA_CMAKE_FLAGS:-}
# GCC 16 <cstdint> patch to SentencePiece before building
perl -pi -e 's/#include <vector>/#include <vector>\n#include <cstdint>/' \
    app/build/_deps/sentencepiece-src/src/sentencepiece_processor.h || true
cmake --build app/build --target moonslate_app -j"$JOBS"

echo "[4] Headless selftest..."
QT_QPA_PLATFORM=offscreen app/build/moonslate_app --selftest

echo "[5] Packaging..."
case "$(uname -m)" in
    aarch64|arm64) ORT_ARCH=aarch64 ;;
    *) ORT_ARCH=x86_64 ;;
esac
rm -rf package
mkdir -p package/moonslate/bin package/moonslate/share/moonslate/models package/moonslate/share/moonslate/tts-data
cp app/build/moonslate_app package/moonslate/bin/
cp moonshine/core/build/libmoonshine.so package/moonslate/bin/
cp "moonshine/core/third-party/onnxruntime/lib/linux/$ORT_ARCH/libonnxruntime.so.1" package/moonslate/bin/
cp README.md LICENSE package/moonslate/
tar -C package -czf package/moonslate-linux-"$ORT_ARCH".tar.gz moonslate

echo "=== Linux build complete: package/moonslate-linux-$ORT_ARCH.tar.gz ==="
