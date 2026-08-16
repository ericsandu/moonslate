#!/bin/bash
set -e

echo "=== Moonslate Build Automation ==="

echo "[1] Initializing and updating Git submodules..."
git submodule update --init --recursive --depth 1

echo "Applying patches to moonshine submodule..."
cd moonshine
if git log -n 5 --oneline | grep -q "Expose raw acoustic fingerprint"; then
    echo "Moonshine patches are already applied!"
else
    git am ../patches/*.patch
    echo "Successfully applied moonshine patches."
fi
cd ../

echo "[2] Building Moonshine Core Library..."
# Upstream now downgrades GCC's bogus -Wrestrict/-Warray-bounds diagnostics itself
# (see core/CMakeLists.txt), so -Werror can stay on for real warnings.

mkdir -p moonshine/core/build
cd moonshine/core/build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
# libmoonshine.so has runpath $ORIGIN but links the vendored ONNX Runtime living in
# third-party; drop a copy next to it so the app resolves it at runtime.
case "$(uname -m)" in
    aarch64|arm64) ORT_ARCH=aarch64 ;;
    *) ORT_ARCH=x86_64 ;;
esac
cp ../../../third-party/onnxruntime/lib/linux/$ORT_ARCH/libonnxruntime.so.1 .
cd ../../../

echo "[3] Building Moonslate Integrations (CTranslate2, SentencePiece)..."
mkdir -p app/build
cd app/build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5

# Apply GCC 16 <cstdint> patch to SentencePiece before building
echo "Patching SentencePiece..."
sed -i 's/#include <vector>/#include <vector>\n#include <cstdint>/' _deps/sentencepiece-src/src/sentencepiece_processor.h || true

cmake --build . -j$(nproc)
cd ../../

echo "=== Build Complete! ==="
echo "Launch Moonslate:"
echo "./app/build/moonslate_app ./moonshine/test-assets/tiny-en"
