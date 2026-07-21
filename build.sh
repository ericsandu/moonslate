#!/bin/bash
set -e

echo "=== Moonslate Build Automation ==="

echo "[1] Initializing and updating Git submodules..."
git submodule update --init --recursive

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
# Strip out strict -Werror to prevent GCC 13/14 false-positive array-bounds warnings from failing the CI
sed -i 's/-Werror//g' moonshine/core/CMakeLists.txt

mkdir -p moonshine/core/build
cd moonshine/core/build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
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
