FROM rust:latest

RUN apt update && apt install -y cmake

WORKDIR /app

RUN git clone \
    https://github.com/eclipse-kuksa/kuksa-databroker.git

WORKDIR /app/kuksa-databroker

RUN cargo build --release

# Kept after the build so editing the overlay does not invalidate the (slow)
# cargo layer. --vss takes a comma-separated list and processes each file
# independently, registering one entry per leaf datapoint, so the overlay
# only adds its own leaves and never conflicts with the standard catalog.
COPY vss_fmw_overlay.json /app/vss_fmw_overlay.json

CMD ["./target/release/databroker", "--vss", "data/vss-core/vss_release_6.0.json,/app/vss_fmw_overlay.json"]
