#!/bin/bash
# Start/stop the Cuttlefish instance.
# Usage: cvd.sh start | cvd.sh stop
set -e

cd /workspace/aosp
source build/envsetup.sh >/dev/null 2>&1
lunch aosp_cf_x86_64_auto-aosp_current-userdebug >/dev/null 2>&1

case "$1" in
    start)
        HOME=$PWD launch_cvd -daemon -report_anonymous_usage_stats=n
        ;;
    stop)
        HOME=$PWD stop_cvd
        ;;
    *)
        echo "Usage: $(basename "$0") {start|stop}" >&2
        exit 1
        ;;
esac
