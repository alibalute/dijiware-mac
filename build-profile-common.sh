# shellcheck shell=bash
# Sourced by build-4mb.sh / build-16mb.sh (do not execute directly).

# ESP-IDF keeps an existing sdkconfig over SDKCONFIG_DEFAULTS. If the saved
# partition table / flash size does not match this profile, drop sdkconfig so
# the next configure picks up sdkconfig.defaults.* correctly.
ensure_sdkconfig_matches_profile() {
  local partition_csv="$1"
  local flash_size_token="$2" # 4MB or 16MB

  [[ -f sdkconfig ]] || return 0

  local stale=0
  if ! grep -q "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"${partition_csv}\"" sdkconfig; then
    stale=1
  fi
  if ! grep -q "CONFIG_ESPTOOLPY_FLASHSIZE_${flash_size_token}=y" sdkconfig; then
    stale=1
  fi

  if (( stale )); then
    echo "==> sdkconfig does not match ${flash_size_token} profile (partition ${partition_csv}); regenerating from defaults..."
    rm -f sdkconfig sdkconfig.old
  fi
}
