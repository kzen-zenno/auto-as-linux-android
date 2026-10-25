#!/bin/bash
#
# Starts a virtual X server (Xvfb), a raw VNC server in front of it
# (x11vnc, port 5900), and a browser-viewable bridge onto that same VNC
# server (websockify + noVNC's static page, port 6080), then runs metapp
# against the virtual display. No password/auth is set on either port:
# docker-compose.yml only publishes 5900/6080 on 127.0.0.1, so the only
# intended access path is `ssh -L <port>:localhost:<port>` (README.md
# §8.2) -- an already-authenticated, encrypted channel.

set -e

DISPLAY_NUM=99
export DISPLAY=":${DISPLAY_NUM}"

Xvfb "$DISPLAY" -screen 0 1024x768x24 -nolisten tcp &

for _ in $(seq 1 50); do
    if [ -S "/tmp/.X11-unix/X${DISPLAY_NUM}" ]; then
        break
    fi
    sleep 0.1
done

x11vnc -display "$DISPLAY" -forever -shared -nopw -rfbport 5900 -quiet &

# Bridges browser WebSocket connections to the raw VNC port above and
# serves noVNC's own vnc.html from the same port, so a browser at
# http://localhost:6080/vnc.html?autoconnect=true just works.
websockify --web=/usr/share/novnc 6080 localhost:5900 &

exec python -m metapp.main
