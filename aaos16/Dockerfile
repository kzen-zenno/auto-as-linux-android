FROM ubuntu:24.04

ARG UID=1000
ARG GID=1000
ARG KVM_GID=993

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    git git-lfs \
    curl wget \
    unzip zip rsync \
    bc bison flex \
    gperf \
    build-essential \
    ccache \
    openssl \
    libssl-dev \
    libxml2-utils \
    xsltproc \
    python3 \
    python3-pip \
    python-is-python3 \
    openjdk-21-jdk \
    mesa-common-dev \
    libgl1-mesa-dev \
    zlib1g-dev \
    libncurses-dev \
    libc6-dev-i386 \
    lib32z1-dev \
    lib32ncurses6 \
    sudo \
    vim nano \
    locales \
    lsb-release \
    ca-certificates

RUN locale-gen en_US.UTF-8

ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

RUN set -eux; \
    existing_group="$(getent group "${GID}" | cut -d: -f1 || true)"; \
    if [ -z "${existing_group}" ]; then \
        groupadd -g "${GID}" builder; \
        existing_group=builder; \
    fi; \
    existing_user="$(getent passwd "${UID}" | cut -d: -f1 || true)"; \
    if [ -n "${existing_user}" ] && [ "${existing_user}" != "builder" ]; then \
        usermod -l builder "${existing_user}"; \
        usermod -d /home/builder -m builder; \
    fi; \
    if id -u builder >/dev/null 2>&1; then \
        usermod -u "${UID}" -g "${existing_group}" -s /bin/bash builder; \
    else \
        useradd -m -u "${UID}" -g "${existing_group}" -s /bin/bash builder; \
    fi; \
    echo "builder ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/builder; \
    chmod 0440 /etc/sudoers.d/builder

# Cuttlefish host support packages (official prebuilt artifacts; no need to
# build from source, see https://github.com/google/android-cuttlefish).
# The cuttlefish-orchestration postinst script requires a "kvm" group to
# already exist, so create one matching the host's /dev/kvm GID first.
RUN set -eux; \
    existing_kvm_group="$(getent group "${KVM_GID}" | cut -d: -f1 || true)"; \
    if [ -z "${existing_kvm_group}" ]; then \
        groupadd -g "${KVM_GID}" kvm; \
    elif [ "${existing_kvm_group}" != "kvm" ]; then \
        groupmod -n kvm "${existing_kvm_group}"; \
    fi

RUN curl -fsSL https://us-apt.pkg.dev/doc/repo-signing-key.gpg \
    -o /etc/apt/trusted.gpg.d/artifact-registry.asc && \
    chmod a+r /etc/apt/trusted.gpg.d/artifact-registry.asc && \
    echo "deb https://us-apt.pkg.dev/projects/android-cuttlefish-artifacts android-cuttlefish main" \
    > /etc/apt/sources.list.d/artifact-registry.list && \
    apt-get update && \
    apt-get install -y cuttlefish-base cuttlefish-user cuttlefish-orchestration && \
    usermod -aG kvm,cvdnetwork builder

USER builder
WORKDIR /home/builder

RUN mkdir -p /home/builder/bin

RUN curl https://storage.googleapis.com/git-repo-downloads/repo \
    -o /home/builder/bin/repo && \
    chmod a+x /home/builder/bin/repo

COPY --chown=builder:builder scripts/init.sh scripts/cvd.sh /home/builder/bin/
RUN chmod +x /home/builder/bin/init.sh /home/builder/bin/cvd.sh

ENV PATH=/home/builder/bin:${PATH}

RUN echo 'export USE_CCACHE=1' >> /home/builder/.bashrc && \
    echo 'export CCACHE_DIR=/ccache' >> /home/builder/.bashrc && \
    echo "alias croot='cd /workspace/aosp'" >> /home/builder/.bashrc && \
    echo 'source /home/builder/bin/init.sh' >> /home/builder/.bashrc

CMD ["/bin/bash"]