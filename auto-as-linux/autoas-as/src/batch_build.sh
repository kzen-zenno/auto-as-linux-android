#!/usr/bin/env bash

set -u

ASPATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${ASPATH}"

# Keep behavior similar to Console.bat: run from repository root and expose local tools in PATH.
export PATH="${ASPATH}/build/posix/GCC/one:${PATH}"

APPS=(
  CanApp CanBL CanSimulator CanDump CanBridge CanSend CanNm OsekNm Loader
  IsoTpSend DoIPSend NetApp AddClientEx AddServerEx CanPubEx CanSubEx
  RadarServiceClient RadarServiceServer SRecCat SRecHexdump Lua CanStdio
  FmwApp
)

CONTINUE_ON_ERROR=0
DO_CLEAN=0
CLEAN_ONLY=0

usage() {
  cat <<'EOF'
Usage: ./BatchBuild.sh [options]

Options:
  --clean               Clean each target before building
  --clean-only          Clean each target and exit
  --continue-on-error   Continue building even if a target fails
  -h, --help            Show this help
EOF
}

run_step() {
  local label="$1"
  shift
  echo "=== ${label} ==="
  if "$@"; then
    return 0
  fi

  echo "[ERROR] ${label}"
  FAILURES+=("${label}")
  if [[ ${CONTINUE_ON_ERROR} -eq 1 ]]; then
    return 0
  fi
  return 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean)
      DO_CLEAN=1
      ;;
    --clean-only)
      DO_CLEAN=1
      CLEAN_ONLY=1
      ;;
    --continue-on-error)
      CONTINUE_ON_ERROR=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      usage
      exit 2
      ;;
  esac
  shift
done

FAILURES=()

if [[ ${DO_CLEAN} -eq 1 ]]; then
  for app in "${APPS[@]}"; do
    run_step "Clean app ${app}" scons -c --app="${app}" || break
  done
  run_step "Clean lib AsOne" scons -c --lib=AsOne || true
fi

if [[ ${CLEAN_ONLY} -eq 0 ]]; then
  # Build shared library first because some app targets (e.g. CanSimulator)
  # require prebuilt/libAsOne.so at SCons script evaluation time.
  run_step "Build lib AsOne" scons --lib=AsOne || true

  for app in "${APPS[@]}"; do
    run_step "Build app ${app}" scons --app="${app}" || break
  done
fi

if [[ ${#FAILURES[@]} -gt 0 ]]; then
  echo
  echo "Failed steps:"
  for item in "${FAILURES[@]}"; do
    echo "  - ${item}"
  done
  exit 1
fi

echo
echo "Batch build finished successfully."
