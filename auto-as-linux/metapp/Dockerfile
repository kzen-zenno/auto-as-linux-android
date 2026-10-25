FROM python:3.12-slim

# xvfb: virtual X server the PyQt6 xcb platform plugin renders into.
# x11vnc: exposes that virtual display as raw VNC (port 5900).
# novnc + websockify: serve the same display as a *browser-viewable* page
# (port 6080), since a plain VNC port needs a native VNC client, which the
# browser-based workflow in README.md §8.2 avoids. Pulls in nodejs
# transitively (novnc's Debian package depends on it), which noticeably
# increases image size -- accepted for a dev/learning container.
RUN apt-get update && apt-get install -y --no-install-recommends \
    xvfb \
    x11vnc \
    novnc \
    websockify \
    libglib2.0-0 \
    libgl1 \
    libegl1 \
    libxkbcommon0 \
    libxkbcommon-x11-0 \
    libxcb-cursor0 \
    libxcb-icccm4 \
    libxcb-image0 \
    libxcb-keysyms1 \
    libxcb-randr0 \
    libxcb-render-util0 \
    libxcb-shape0 \
    libdbus-1-3 \
    libfontconfig1 \
    libfreetype6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY pyproject.toml ./
COPY src ./src
RUN pip install --no-cache-dir .

COPY start.sh ./start.sh
RUN chmod +x ./start.sh

EXPOSE 5900 6080
CMD ["./start.sh"]
