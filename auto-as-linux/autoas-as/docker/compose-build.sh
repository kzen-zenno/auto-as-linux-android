#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

docker compose up -d as-dev
docker exec autoas-dev bash -lc 'prepare-downloads.sh && chmod +x ./batch_build.sh && ./batch_build.sh'
