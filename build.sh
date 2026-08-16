#!/bin/bash
set -e

echo "=== Moonslate Build Automation ==="

echo "[1] Initializing and updating Git submodules..."
git submodule update --init --recursive --depth 1

echo "[2] Building and packaging via the shared CI script..."
bash scripts/ci-build-linux.sh

echo "=== Build Complete! ==="
echo "Run the packaged binary:"
echo "./package/moonslate/bin/moonslate_app"
echo "(or the unpackaged build: ./app/build/moonslate_app)"
