# Mutineer BBS — production container image
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config \
    libsqlite3-dev libssl-dev libargon2-dev libzstd-dev libarchive-dev libnotcurses-core-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel "$(nproc)" --target mutineer tools plank plugins \
    && VERSION=docker PLATFORM=debian ./scripts/build-release.sh

# ---------------------------------------------------------------------------
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libsqlite3-0 libssl3 libargon2-1 libzstd1 libarchive13 libnotcurses-core3 sqlite3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/mutineer

COPY --from=builder /src/dist/releases/mutineer-docker-x86_64-debian/ ./

COPY docker/mutineer.docker.conf conf/mutineer.docker.conf
COPY docker/mutineer.wfc.conf conf/mutineer.wfc.conf
COPY docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh mutineer bin/* plank/bin/* 2>/dev/null || true \
    && chmod +x plugins/*.so 2>/dev/null || true

EXPOSE 2929

VOLUME ["/opt/mutineer/data", "/opt/mutineer/logs"]

ENV MUTINEER_CONFIG=conf/mutineer.docker.conf

HEALTHCHECK --interval=15s --timeout=5s --start-period=25s --retries=5 \
  CMD bash -c 'timeout 1 bash -c "cat < /dev/null > /dev/tcp/127.0.0.1/2929"' || exit 1

ENTRYPOINT ["/entrypoint.sh"]
