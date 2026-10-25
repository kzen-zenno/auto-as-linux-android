#!/usr/bin/env bash
#
# Entry point for the "gateway-ecu" container (see the repository-root
# docker-compose.yml): builds FmwApp (the
# real AUTOSAR-CP Gateway ECU, autoas-as/src/app/fmw -- see its README.md)
# if needed, starts it in the background bound to this container's own
# network address, then runs webcan in the foreground so `docker compose`
# keeps the container alive and `docker logs` shows webcan's own output.
#
# AS_LOCAL_IP must be set to this container's actual address on the shared
# network (see autoas-as/src/app/fmw/README.md section 10.2's note on
# SOME/IP-SD's "offer: ipv4 addr not matched" check) -- the compose service
# sets it explicitly since it is also the static "unicast" address
# vehicle-state-provider's vsomeip.json expects to find this service at.
set -euo pipefail

WORKSPACE_DIR="${WORKSPACE_DIR:-/workspace}"
cd "${WORKSPACE_DIR}"

if [[ -z "${AS_LOCAL_IP:-}" ]]; then
  echo "[run-fmw-gateway] ERROR: AS_LOCAL_IP is not set (see script header)." >&2
  exit 1
fi
export AS_LOCAL_IP

echo "[run-fmw-gateway] building FmwApp..."
scons --app=FmwApp -j"$(nproc)"

echo "[run-fmw-gateway] starting FmwApp (AS_LOCAL_IP=${AS_LOCAL_IP})..."
"${WORKSPACE_DIR}/build/posix/GCC/FmwApp/FmwApp" -v "${FMWAPP_LOG_LEVEL:-1}" < /dev/null \
  > "${WORKSPACE_DIR}/fmwapp.log" 2>&1 &
FMWAPP_PID=$!

cleanup() {
  echo "[run-fmw-gateway] stopping..."
  kill "${FMWAPP_PID}" ${WEBCAN_PID:-} 2>/dev/null || true
  wait "${FMWAPP_PID}" 2>/dev/null || true
  [[ -n "${WEBCAN_PID:-}" ]] && wait "${WEBCAN_PID}" 2>/dev/null || true
}
trap cleanup TERM INT

sleep 2
if ! kill -0 "${FMWAPP_PID}" 2>/dev/null; then
  echo "[run-fmw-gateway] ERROR: FmwApp exited immediately, see fmwapp.log:" >&2
  cat "${WORKSPACE_DIR}/fmwapp.log" >&2
  exit 1
fi

echo "[run-fmw-gateway] starting webcan on :8800 ..."
cd "${WORKSPACE_DIR}/tools/webcan"
python3 server.py --host 0.0.0.0 --port 8800 &
WEBCAN_PID=$!

# webcan used to be exec'd here, which reparented FmwApp to PID 1 (webcan
# itself) with nobody waiting on it. When FmwApp died it became a zombie, the
# container stayed "Up" because webcan was still serving :8800, and the only
# visible symptom was that SOME/IP-SD OFFERs silently stopped and the Linux
# side lost its subscription -- easy to misread as a vsomeip problem.
# Supervise both instead: whichever dies first takes the container down with a
# diagnosable message, and compose's `restart: unless-stopped` brings the
# Gateway ECU back.
while kill -0 "${FMWAPP_PID}" 2>/dev/null && kill -0 "${WEBCAN_PID}" 2>/dev/null; do
  sleep 2
done

set +e
if ! kill -0 "${FMWAPP_PID}" 2>/dev/null; then
  wait "${FMWAPP_PID}"
  status=$?
  echo "[run-fmw-gateway] ERROR: FmwApp exited (status ${status}); last fmwapp.log lines:" >&2
  tail -50 "${WORKSPACE_DIR}/fmwapp.log" >&2
else
  wait "${WEBCAN_PID}"
  status=$?
  echo "[run-fmw-gateway] ERROR: webcan exited (status ${status})." >&2
fi
cleanup
exit 1
