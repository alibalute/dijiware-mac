#!/usr/bin/env bash
set -e

echo "=== ESP-IDF Health Check ==="

echo "IDF_PATH=$IDF_PATH"
if [ -z "$IDF_PATH" ]; then
  echo "ERROR: IDF_PATH not set."
  exit 1
fi

if [ -f "$IDF_PATH/export.sh" ]; then
  source "$IDF_PATH/export.sh"
else
  echo "ERROR: $IDF_PATH/export.sh not found."
  exit 1
fi

echo "Python: $(python --version 2>&1)"
echo "idf.py version: $(idf.py --version 2>&1)"

echo "Running idf.py build (dry-run via -n to check generator)"
idf.py -n build

echo "Health check success. To build fully run: idf.py build"
