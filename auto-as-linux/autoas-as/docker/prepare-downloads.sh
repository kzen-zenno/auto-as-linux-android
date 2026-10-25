#!/usr/bin/env bash
set -euo pipefail

WORKSPACE_DIR="${WORKSPACE_DIR:-/workspace}"
DOWNLOAD_DIR="${WORKSPACE_DIR}/download"

mkdir -p "${DOWNLOAD_DIR}"

clone_or_update() {
  local name="$1"
  local repo_url="$2"
  local branch="$3"
  local target_dir="${DOWNLOAD_DIR}/${name}"

  if [[ -d "${target_dir}/.git" ]]; then
    echo "[prepare-downloads] ${name} already exists, skip"
    return 0
  fi

  rm -rf "${target_dir}"
  echo "[prepare-downloads] cloning ${name} from ${repo_url} (${branch})"
  git clone --depth 1 --branch "${branch}" "${repo_url}" "${target_dir}"
}

clone_or_update "mbedtls" "https://github.com/Mbed-TLS/mbedtls.git" "v3.6.3"
clone_or_update "nlohmann-json" "https://github.com/nlohmann/json.git" "develop"
clone_or_update "lua" "https://github.com/lua/lua.git" "v5.4.6"

echo "[prepare-downloads] done"
