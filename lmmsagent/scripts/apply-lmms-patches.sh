#!/usr/bin/env bash
set -euo pipefail

LMMS_DIR="${1:?Usage: apply-lmms-patches.sh /path/to/lmms}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PATCH_FILE="$REPO_ROOT/integrations/lmms/patches/0001-lmms-agentcontrol-host-support.patch"

if git -C "$LMMS_DIR" apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
  echo "Host patch already applied"
else
  git -C "$LMMS_DIR" apply --3way "$PATCH_FILE"
  echo "Host patch applied"
fi
