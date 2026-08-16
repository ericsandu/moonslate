#!/bin/bash
# Applies the Moonslate custom patches to the moonshine submodule.
# Idempotent: skips when the patches are already in the submodule's history.
set -euo pipefail

cd "$(dirname "$0")/../moonshine"

if git log -n 5 --oneline | grep -q "Expose raw acoustic fingerprint"; then
    echo "Moonshine patches are already applied!"
else
    git am ../patches/*.patch
    echo "Successfully applied moonshine patches."
fi
