# autoas-as (Docker Compose Build/Run)

This repository is built and run using a single Docker Compose service.
Only `src/` is shared from the host into the container to keep the host development environment clean.

## Overview

- Compose service: `as-dev` (container: `autoas-dev`)
- Shared folder: `./src:/workspace`
- Build command: `batch_build.sh` inside the container
- Run command: `docker exec`

## Prerequisites

- Docker Engine
- Docker Compose plugin (`docker compose`)

## Quick Start

```sh
cd /home/<user>/work/auto-as/autoas-as

# 1) Build image + start container

docker compose build as-dev
docker compose up -d as-dev

# 2) Pre-fetch external dependencies (gitee timeout workaround)

docker exec autoas-dev bash -lc 'prepare-downloads.sh'

# 3) Build all modules

docker exec autoas-dev bash -lc 'chmod +x ./batch_build.sh && ./batch_build.sh'
```

## Build Options

```sh
cd /home/<user>/work/auto-as/autoas-as

docker exec autoas-dev bash -lc 'cd /workspace && ./batch_build.sh --clean'
docker exec autoas-dev bash -lc 'cd /workspace && ./batch_build.sh --clean-only'
docker exec autoas-dev bash -lc 'cd /workspace && ./batch_build.sh --continue-on-error'
```

## Run Examples

```sh
# Terminal 1: FmwApp (SYS-001 CAN signal sample, see docs/fmwapp.md)
cd /home/<user>/work/auto-as/autoas-as
docker exec -it autoas-dev bash -lc 'cd /workspace/build/posix/GCC/FmwApp && ./FmwApp -v 0'

# Terminal 2: webcan, a browser CAN tool for driving/observing FmwApp
cd /home/<user>/work/auto-as/autoas-as
docker exec -it autoas-dev bash -lc 'cd /workspace/tools/webcan && python3 server.py --host 0.0.0.0 --port 8800'
# then open http://localhost:8800/ (port 8800 is published in compose.yaml)
```

webcan also stands in for the missing HVAC ECU. FmwApp transmits an HVAC write
as `AirconCommand` (0x526), and only the HVAC ECU reports the new state in the
`Aircon` frame (0x323). When a 0x526 frame is received, webcan applies the
fields whose value or `MM_*_Cmd` gate changed since the previous 0x526 frame to
its `Aircon` periodic sender (`CmdMode` on change only), so a write comes back
as state. Values set by hand in the UI are not overwritten by a command that
stays asserted on the bus. Switch it with the "HVAC ECU emulation" checkbox in the page header, or with
`curl -X POST localhost:8800/api/hvac_emulation -H 'Content-Type: application/json' -d '{"enabled": false}'`
(`GET /api/hvac_emulation` shows the current setting; it is on by default, and the page picks up a change made through the API within 5 s).

`LD_LIBRARY_PATH=/workspace/build/posix/GCC/prebuilt` is already configured in the Compose service.

## Helper Script

```sh
cd /home/<user>/work/auto-as/autoas-as
chmod +x docker/compose-build.sh
./docker/compose-build.sh
```

## Cleanup

```sh
cd /home/<user>/work/auto-as/autoas-as

# Remove build artifacts
rm -rf src/build/posix/GCC

# Remove container/network

docker compose down
```

## docs Layout

- `docs/build.md`: Docker Compose build/run instructions
- `docs/CanApp.md`: CanApp-related notes
- `docs/asone.md`: asone-related notes
- `docs/file_details.md`: file structure notes
- `docs/fmwapp.md`: how to build an app from a CAN signal spec, the `FmwApp` sample, `lssg`/`wrsg`
  shell introspection, and the `webcan` browser test tool

## Reference

For detailed steps, see `docs/build.md`.
