FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    ca-certificates \
    build-essential \
    git \
    python3 \
    python3-pip \
    scons \
    uuid-dev \
    && rm -rf /var/lib/apt/lists/*

# Install Python-side dependencies used by this repository build.
RUN python3 -m pip install --no-cache-dir --break-system-packages \
    jinja2 \
    pycrc \
    ply \
    pyserial \
    bitarray \
    fastapi \
    "uvicorn[standard]"

# Host bind mounts may be owned by a different UID; allow git operations in mounted tree.
RUN git config --global --add safe.directory '*'

COPY docker/prepare-downloads.sh /usr/local/bin/prepare-downloads.sh
COPY docker/run-fmw-gateway.sh /usr/local/bin/run-fmw-gateway.sh
RUN chmod +x /usr/local/bin/prepare-downloads.sh /usr/local/bin/run-fmw-gateway.sh

WORKDIR /workspace

CMD ["bash"]
