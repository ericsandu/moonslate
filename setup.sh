#!/bin/bash
set -e

echo "Initializing submodules..."
git submodule update --init --recursive

echo "Applying patches to moonshine submodule..."
cd moonshine

# Check if patches are already applied to avoid errors
if git log -n 5 --oneline | grep -q "Expose raw acoustic fingerprint"; then
    echo "Patches are already applied!"
else
    git am ../patches/*.patch
    echo "Successfully applied moonshine patches."
fi

cd ..
echo "Setup complete. You can now build the project."
