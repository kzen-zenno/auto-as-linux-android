#!/bin/bash
# Sourced from ~/.bashrc so a fresh `docker compose exec aaos16 bash` is
# immediately ready for `m` / `launch_cvd` etc. Guarded so it's a no-op
# before `repo sync` has populated /workspace/aosp.
if [ -d /workspace/aosp/build ]; then
    cd /workspace/aosp || return
    source build/envsetup.sh
    lunch aosp_cf_x86_64_auto-aosp_current-userdebug
fi
