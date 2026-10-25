FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt update && apt install -y \
    git \
    cmake \
    build-essential \
    python3-pip \
    libssl-dev \
    libboost-all-dev

RUN pip3 install "conan<2.0"

WORKDIR /app

COPY config /app/config
COPY start.sh /app/start.sh

RUN chmod +x /app/start.sh

CMD ["/app/start.sh"]
