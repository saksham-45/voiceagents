#!/usr/bin/env bash
set -euo pipefail

LMMS_DIR="${1:?Usage: install-agentcontrol.sh /path/to/lmms}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PLUGIN_SRC="$REPO_ROOT/integrations/lmms/AgentControl"
PATCH_FILE="$REPO_ROOT/integrations/lmms/patches/0001-lmms-agentcontrol-host-support.patch"

if [[ ! -d "$LMMS_DIR/.git" ]]; then
  echo "Expected an LMMS git checkout at $LMMS_DIR" >&2
  exit 1
fi

mkdir -p "$LMMS_DIR/plugins/AgentControl"
cp "$PLUGIN_SRC"/* "$LMMS_DIR/plugins/AgentControl/"

if git -C "$LMMS_DIR" apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
  echo "Host patch already applied"
else
  git -C "$LMMS_DIR" apply --3way "$PATCH_FILE"
  echo "Host patch applied"
fi

echo "AgentControl copied into $LMMS_DIR/plugins/AgentControl"
