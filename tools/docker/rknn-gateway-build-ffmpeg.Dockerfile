FROM rkav/aarch64-rknn-build:ubuntu22.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends pkg-config \
    && rm -rf /var/lib/apt/lists/*
