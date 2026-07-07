#!/usr/bin/env bash
# Build (and optionally flash) firmware for 4 MB flash hardware.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

if [[ -z "${IDF_PATH:-}" ]]; then
  echo "IDF_PATH is not set. Source the ESP-IDF export script first, e.g.:" >&2
  echo "  source \$HOME/esp/esp-idf/export.sh" >&2
  exit 1
fi

export SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.4mb"
export DIJILELE_PRODUCT_ID="${DIJILELE_PRODUCT_ID:-1}"

# shellcheck source=build-profile-common.sh
source "$ROOT/build-profile-common.sh"
ensure_sdkconfig_matches_profile "partitions_4mb.csv" "4MB"

echo "==> Dijilele build profile: 4 MB flash (partitions_4mb.csv), product id $DIJILELE_PRODUCT_ID"
idf.py "$@"
