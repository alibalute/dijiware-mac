#!/usr/bin/env bash
# Refresh firmware/ota/dijilele-firmware-latest-<slug>.json from version.txt
# for the product line being built (DIJILELE_PRODUCT_ID).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

VER="$(head -1 version.txt | tr -d '\r\n' | xargs)"
if [[ ! "$VER" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
  echo "update-ota-manifest: invalid version.txt: '$VER'" >&2
  exit 1
fi

MAJOR="${BASH_REMATCH[1]}"
MINOR="${BASH_REMATCH[2]}"
PATCH="${BASH_REMATCH[3]}"

PID="${DIJILELE_PRODUCT_ID:-0}"
case "$PID" in
  1) SLUG="dijilele-m" ;;
  2) SLUG="dijilele-s" ;;
  *)
    echo "update-ota-manifest: skip (set DIJILELE_PRODUCT_ID=1 or 2 to update manifests)"
    exit 0
    ;;
esac

OTA_DIR="$ROOT/firmware/ota"
mkdir -p "$OTA_DIR"

JSON_FILE="$OTA_DIR/dijilele-firmware-latest-${SLUG}.json"
TXT_FILE="$OTA_DIR/dijilele-firmware-version-${SLUG}.txt"

cat > "$JSON_FILE" <<EOF
{
  "major": ${MAJOR},
  "minor": ${MINOR},
  "patch": ${PATCH}
}
EOF

printf '%s\n' "$VER" > "$TXT_FILE"

echo "==> OTA manifest updated for ${SLUG}: ${VER}"
echo "    ${JSON_FILE}"
