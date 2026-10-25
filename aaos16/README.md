# AAOS 16 Build Environment

A minimal Docker Compose + Ubuntu 24.04 setup for building AOSP/AAOS.
Shares host UID/GID and keeps the source tree and ccache on the host.

# Reference Machine Spec
```
$ sudo lshw -short
processor      AMD Ryzen 9 7940HS w/ Radeon 780M Graphics
memory         64GiB System Memory
```
## Directory Layout

```text
aaos16/
|-- docker-compose.yml
|-- Dockerfile
|-- README.md
|-- patches/
|   |-- 0001-disable-avf-early-vm.patch
|   `-- 0002-vhal-proxy-server-kuksa.patch
|-- tests/
|   `-- kuksa_vhal_e2e.py
|-- scripts/
|   |-- init.sh
|   `-- cvd.sh
`-- workspace/
    |-- aosp/
    `-- ccache/
```

## 1) Create .env

```bash
echo "UID=$(id -u)" > .env
echo "GID=$(id -g)" >> .env
echo "KVM_GID=$(getent group kvm | cut -d: -f3)" >> .env
cat .env
```

`KVM_GID` is looked up fresh each time from the host's `/dev/kvm` group GID, and is
passed straight through to `group_add` in `docker-compose.yml` for launching Cuttlefish
inside the container. If the host has no `kvm` group, this will be empty — in that case,
[enable KVM](https://cloud.google.com/compute/docs/instances/enable-nested-virtualization-vm-instances)
first.

```bash
[ -z "$(getent group kvm)" ] && echo "WARNING: no kvm group found. Check the host's KVM setup."
```

Example:

```text
UID=1000
GID=1000
KVM_GID=993
```

## 2) Build And Start Container

```bash
docker compose build
docker compose up -d
docker compose exec aaos16 bash
```

## 3) AOSP Init/Sync

```bash
cd /workspace
mkdir -p aosp
cd aosp


repo init \
  --partial-clone \
  -u https://android.googlesource.com/platform/manifest \
  -b android16-release

repo sync \
  -c \
  -j4 \
  --jobs-network=2 \
  --jobs-checkout=4 \
  --no-tags
    
```

Note: android.googlesource.com rate-limits parallel access from the same machine, so
tune the CPU core count / job counts to fit your own environment.

## 4) Apply Local Patches

`workspace/aosp` is a plain AOSP checkout that gets re-fetched on every `repo sync`, so
any direct edits there won't survive. This repo keeps the patches you need under
`patches/` (mounted at `/patches` via `docker-compose.yml`) — apply them every time
after a `repo sync` (or after setting things up again from a clean state).

```bash
cd /workspace/aosp/build/release
git apply /patches/0001-disable-avf-early-vm.patch
```

### 0001-disable-avf-early-vm.patch

`aosp_cf_x86_64_auto-aosp_current-userdebug` has the AVF Early VM feature (which runs
Keymint/Gatekeeper inside a Trusty VM) enabled by default, but building it requires
`nsjail`'s strict sandboxing, which fails with `mount()`/`setgroups` returning
`Permission denied` under Docker (due to Ubuntu 24.04's AppArmor restrictions). This
patch overrides the `RELEASE_AVF_ENABLE_EARLY_VM` flag to `false` for `bp2a` (=
`aosp_current`) only, so Keymint/Gatekeeper run through the regular HAL (no isolated
VM) instead, working around the issue. This has no impact on basic AAOS behavior or
app development.

## 5) Build (Cuttlefish Auto Example)

`cd /workspace/aosp && source build/envsetup.sh && lunch aosp_cf_x86_64_auto-aosp_current-userdebug`
is baked into the image as `scripts/init.sh` and auto-sourced from `~/.bashrc`. By the
time you're in via `docker compose exec aaos16 bash` (as long as `/workspace/aosp/build`
already exists), you're already lunched, so all you need is:

```bash
ccache -M 200G
m -j(job core num. ex. -j12)
```

If the auto-lunch hasn't kicked in yet (e.g. before `/workspace/aosp` exists, during
initial setup), run it manually as before:

```bash
cd /workspace/aosp
source build/envsetup.sh
lunch aosp_cf_x86_64_auto-aosp_current-userdebug
```

## 6) Run On Cuttlefish (KVM)

### 6-1) Cuttlefish host packages

Host-side tools like `launch_cvd`/`cvd` use the official prebuilt `.deb` packages
(`cuttlefish-base`/`cuttlefish-user`/`cuttlefish-orchestration`) that Google publishes
on Artifact Registry — no need to build from source (there's also a self-build route via
[google/android-cuttlefish](https://github.com/google/android-cuttlefish)'s
`tools/buildutils/build_packages.sh`, but its Bazel sandbox doesn't play well with this
Docker setup and adds a lot of friction, so it's not used here).

Registering the apt repo, installing, and `usermod -aG kvm,cvdnetwork builder` are all
baked into the `Dockerfile`, so `docker compose build` alone is enough. No further
manual steps are needed.

### 6-2) Initialize network interfaces (once per container start)

Cuttlefish is normally designed for `cuttlefish-host-resources.service` (systemd) to
create the TAP/bridge network interfaces (`cvd-etap-*`, `cvd-mtap-*`, `cvd-wtap-*`,
`cvd-ebr`, `cvd-wbr`, etc.) as root ahead of time. This container has no systemd (PID 1),
so that never runs automatically, and `crosvm` fails to start with
`failed to create tap interface: Operation not permitted`. Run this manually once,
every time the container is (re)created:

```bash
docker compose exec aaos16 bash

sudo /etc/init.d/cuttlefish-host-resources start
```

The `nvidia-modprobe: not found` warning is expected and harmless on non-GPU setups.
So that `modprobe bridge` can find the host's `bridge` kernel module, `docker-compose.yml`
mounts the host's `/lib/modules` read-only.

You can confirm it worked with:

```bash
ip -brief link | grep cvd
```

If `cvd-ebr`, `cvd-etap-01`, etc. show up in the list, you're good.

### 6-3) Launch

The `launch_cvd`/`stop_cvd` calls (including the assumed `lunch`) are wrapped in
`scripts/cvd.sh`, which — like `init.sh` — is on the image's `PATH`.

```bash
cvd.sh start
cvd.sh stop
```

Under the hood it's equivalent to (useful for manual runs if the auto-lunch hasn't
kicked in):

```bash
cd /workspace/aosp
source build/envsetup.sh
lunch aosp_cf_x86_64_auto-aosp_current-userdebug

HOME=$PWD launch_cvd -daemon -report_anonymous_usage_stats=n
```

Make sure `HOME=$PWD` points at the AOSP root (`/workspace/aosp`). `lunch` needs to run
first to set `ANDROID_PRODUCT_OUT` — without it, `boot.img` won't be found.

**Verify it's working:**

```bash
# instance status
HOME=$PWD cvd_status

# does adb see it as a device?
out/host/linux-x86/bin/adb connect 127.0.0.1:6520
out/host/linux-x86/bin/adb devices
```

If `launch_cvd`'s output includes `Virtual device booted successfully` /
`VIRTUAL_DEVICE_BOOT_COMPLETED`, it booted successfully. If `adb devices` shows
`127.0.0.1:6520	device`, connectivity is confirmed too.

**To stop:**

```bash
cvd.sh stop
```

**Troubleshooting: `adb connect` fails / something's off after launch**

Running `launch_cvd` (`cvd.sh start`) from multiple `docker compose exec` sessions at
the same time against the same `cvd-1` instance causes the later launch to fail on a
network resource conflict like `cvd-wifiap-01` (`launcher.log` will show
`There appears to be another cuttlefish device already running`). The existing process
keeps running, but the `adb`/vsock path can end up unstable. Do a clean restart:

```bash
cvd.sh stop
rm -rf /tmp/cf_avd_1000 /tmp/cf_env_1000 /tmp/vsock_3_1000
cvd.sh start
```

Avoid running `cvd.sh start`/`stop` (= `launch_cvd`/`stop_cvd`) from multiple sessions
at once.

**To view the screen in a browser**, the in-container WebRTC UI (`https://localhost:8443`)
is also published to the host's `127.0.0.1:8443` via `docker-compose.yml`'s
`ports: ["127.0.0.1:8443:8443"]` (bound to `127.0.0.1` only, so it's not reachable from
elsewhere on the host's network).

If the host itself is a remote server, forward it the rest of the way to your local
machine over SSH.

```bash
ssh -L 8443:localhost:8443 <user>@<host> -p <port> -i <identity>
```

Then open `https://localhost:8443` in your local browser. You'll get a self-signed
certificate warning — that's expected, since Cuttlefish's WebRTC UI itself uses one.

**To connect adb directly from your local machine**, the same idea applies: it's
published at the host's `127.0.0.1:6520` via `docker-compose.yml`'s
`ports: ["127.0.0.1:6520:6520"]`. Add an SSH port forward if you're going through a
remote server (you can combine it with `8443` in a single SSH command).

```bash
ssh -L 8443:localhost:8443 -L 6520:localhost:6520 <user>@<host> -p <port> -i <identity>
```

If you have Android SDK Platform Tools (`adb`) locally:

```bash
adb connect localhost:6520
adb devices
```

`docker-compose.yml` grants `privileged: true` and passes through devices like
`/dev/kvm` for all of the above. This nearly disables the container's isolation, so
don't run other important workloads alongside this build-only container.

## 7) Connect the VHAL to KUKSA (auto-as-linux)

Cuttlefish's guest VHAL talks gRPC to the host-side `vhal_proxy_server`. With
`patches/0002-vhal-proxy-server-kuksa.patch` applied, `vhal_proxy_server` can
mirror the KUKSA Databroker of `../auto-as-linux` into the VHAL (design:
`specs/swarch/SWARCH-003-aaos-vhal-bridge.md`).

```bash
# 1. Start the Databroker stack (published on host port 55555)
cd ../auto-as-linux && docker compose up -d

# 2. Apply the patch (once after every repo sync) and rebuild
docker compose exec aaos16 bash
cd /workspace/aosp/device/google/cuttlefish
git apply /patches/0002-vhal-proxy-server-kuksa.patch
m -j12 vhal_proxy_server

# 3. (Re)start Cuttlefish; vhal_proxy_server is started by launch_cvd
cvd.sh stop; cvd.sh start
```

`KUKSA_ADDR` (set in `docker-compose.yml`, default `host.docker.internal:55555`)
selects the Databroker; unset it to get the plain fake VHAL. The container
reaches the host through `extra_hosts: host.docker.internal:host-gateway`.

Check from the host (webcan injects CAN signals, then KUKSA and VHAL values are
compared, and an HVAC write is sent through the VHAL):

```bash
python3 tests/kuksa_vhal_e2e.py
```

Connection status of the bridge is printed in
`workspace/aosp/cuttlefish_runtime/logs/launcher.log` (`KuksaVehicleHardware`).
After editing the sources under `device/google/cuttlefish/host/commands/vhal_proxy_server`,
regenerate the patch with `git add -N host/commands/vhal_proxy_server && git diff -- host/commands/vhal_proxy_server > /patches/0002-vhal-proxy-server-kuksa.patch`.

## Persistent Data

These survive even if the container is removed:

- workspace/aosp
- workspace/ccache

So the Android source, `.repo`, `out`, and ccache are all reusable.

## Dockerfile Note (UID/GID Collision Safe)

The Dockerfile's user-creation logic is tuned to avoid failing in these cases:

- The given GID already exists
- The given UID already exists
- The builder user/group already exists

sudo privileges are granted via /etc/sudoers.d/builder.

## Shell Setup

The `USE_CCACHE`/`CCACHE_DIR` exports and the `croot` alias are baked into `~/.bashrc`
on the Dockerfile side, so they survive container recreation (`docker compose down` →
`up`). `/home/builder` is not volume-mounted (it lives in the container layer), so
manually appending to `~/.bashrc` would be lost on recreation. To add more shell setup,
edit the block below in the Dockerfile and then `docker compose build`.

```dockerfile
RUN echo 'export USE_CCACHE=1' >> /home/builder/.bashrc && \
    echo 'export CCACHE_DIR=/ccache' >> /home/builder/.bashrc && \
    echo "alias croot='cd /workspace/aosp'" >> /home/builder/.bashrc && \
    echo 'source /home/builder/bin/init.sh' >> /home/builder/.bashrc
```

The `ccache -M 200G` setting itself is written to `workspace/ccache/ccache.conf` (a
host-mounted volume), so it's preserved even when the container is recreated.

`scripts/init.sh` (auto-lunch) and `scripts/cvd.sh` (`start`/`stop`) are likewise baked
into `/home/builder/bin` (on `PATH`) via the Dockerfile's
`COPY --chown=builder:builder scripts/init.sh scripts/cvd.sh /home/builder/bin/`. To
change their behavior, edit `scripts/*.sh` in this repo and `docker compose build`.

## AAOS 16 Memo

`trout` was officially discontinued as of 2026-08-14, so
[Cuttlefish](https://source.android.com/docs/automotive/virtualization/reference_platform)
(`aosp_cf_x86_64_auto`) is used as the reference platform instead.

`trunk_staging` doesn't exist on the `android16-release` branch, so `aosp_current`
(currently an alias for `bp2a`) is specified as the release config.
