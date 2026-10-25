# Ubuntu 24.04 Docker Compose Build/Run Notes

This document defines a single-service Docker Compose workflow for this repository.

Policy in this document:

- Source code stays on the host and is bind-mounted into the container.
- Build and run are executed inside one persistent Compose container.
- Build all modules with `batch_build.sh`.
- Run binaries with `docker exec`.

## Scope

The upstream document at `src/doc/EN/build-env-setup.md` is Windows-focused.
This file defines the Linux Docker Compose workflow verified for this repository.

## Repository Root

All commands below assume this repository root on host:

```sh
cd /home/<user>/work/auto-as/autoas-as
```

## Compose Topology

`compose.yaml` has exactly one service:

- service name: `as-dev`
- container name: `autoas-dev`
- command: `sleep infinity` (persistent runtime for `docker exec` operations)
- host bind mount: `./src:/workspace`

## Prerequisites (Host)

- Docker Engine
- Docker Compose plugin (`docker compose` command)

No host Python or host SCons setup is required for normal build/run.

## Build Image and Start Container

```sh
cd /home/<user>/work/auto-as/autoas-as
docker compose build as-dev
docker compose up -d as-dev
```

The image includes:

- Ubuntu 24.04
- build-essential, git, python3, python3-pip, scons, uuid-dev
- Python packages: jinja2, pycrc, ply, pyserial, bitarray

## Prepare External Dependencies (Inside Container)

The project may fetch third-party sources from `gitee` and can time out.
Run the helper script once in the container to prepare GitHub mirrors under `download/`.

```sh
cd /home/<user>/work/auto-as/autoas-as
docker exec autoas-dev bash -lc 'prepare-downloads.sh'
```

This prepares:

- `download/mbedtls` from `Mbed-TLS/mbedtls` tag `v3.6.3`
- `download/nlohmann-json` from `nlohmann/json` branch `develop`
- `download/lua` from `lua/lua` tag `v5.4.6`

## Build All Modules (batch_build.sh)

Use `batch_build.sh` inside the running container.

```sh
cd /home/<user>/work/auto-as/autoas-as
docker exec autoas-dev bash -lc 'chmod +x ./batch_build.sh && ./batch_build.sh'
```

Clean + build:

```sh
cd /home/<user>/work/auto-as/autoas-as
docker exec autoas-dev bash -lc 'cd /workspace && ./batch_build.sh --clean'
```

Clean only:

```sh
cd /home/<user>/work/auto-as/autoas-as
docker exec autoas-dev bash -lc 'cd /workspace && ./batch_build.sh --clean-only'
```

Continue on error:

```sh
cd /home/<user>/work/auto-as/autoas-as
docker exec autoas-dev bash -lc 'cd /workspace && ./batch_build.sh --continue-on-error'
```

Helper script (equivalent):

```sh
cd /home/<user>/work/auto-as/autoas-as
chmod +x docker/compose-build.sh
./docker/compose-build.sh
```

## Run Binaries with docker exec

### Terminal 1: Run CanApp

```sh
cd /home/<user>/work/auto-as/autoas-as
docker exec -it autoas-dev bash -lc 'cd /workspace/build/posix/GCC/CanApp && ./CanApp'
```

### Terminal 2: Run IsoTpSend tester

```sh
cd /home/<user>/work/auto-as/autoas-as
docker exec -it autoas-dev bash -lc 'cd /workspace && ./build/posix/GCC/IsoTpSend/IsoTpSend -v 1001'
```

`LD_LIBRARY_PATH=/workspace/build/posix/GCC/prebuilt` is set in the service definition.

## Clean and Shutdown

Remove host build outputs:

```sh
cd /home/<user>/work/auto-as/autoas-as
rm -rf src/build/posix/GCC
```

Stop/remove container and network:

```sh
cd /home/<user>/work/auto-as/autoas-as
docker compose down
```

## Known Limitations

- `LinApp` still has generator dependency ordering constraints.
- `TestXf` still needs `GEN` path preparation before build.
- `asone` GUI target still needs Qt in the container image if you want to build it.

These are project-side constraints and are not changed by Compose itself.
