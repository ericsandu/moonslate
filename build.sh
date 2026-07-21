#!/bin/bash
set -e

echo "=== Moonslate Build Automation ==="

echo "[1] Initializing and updating Git submodules..."
git submodule update --init --recursive

echo "[2] Building Moonshine Core Library..."
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
echo "Test the integration demo:"
echo "./app/build/integration_demo ./moonshine/test-assets/two_cities.wav ./moonshine/test-assets/tiny-en ./app/models/opus-mt-en-de-ct2"
