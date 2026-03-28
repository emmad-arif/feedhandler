#!/usr/bin/env bash
# Usage:
#   ./scripts/run.sh [config]
#
# config defaults to config/sandbox.yaml.
# Pass config/main.yaml to run the feed handler.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG="${1:-$ROOT_DIR/config/sandbox.yaml}"

# Parse "target:" line from YAML (no external deps needed).
TARGET_NAME="$(grep -E '^target:' "$CONFIG" | sed 's/target:[[:space:]]*//')"

if [[ -z "$TARGET_NAME" ]]; then
    echo "error: 'target' field not found in $CONFIG" >&2
    exit 1
fi

BINARY="$ROOT_DIR/build/$TARGET_NAME"

cd "$ROOT_DIR"
make -s

echo "→ running $BINARY"
exec "$BINARY"
