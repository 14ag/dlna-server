# syntax=docker/dockerfile:1.7
ARG DLNA_VERSION=0.0.00000

FROM debian:bookworm-slim AS build
ARG DLNA_VERSION
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        cmake \
        g++ \
        make \
        pkg-config \
        python3 \
        libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S /src -B /build \
        -DCMAKE_BUILD_TYPE=Release \
        -DDLNA_ENABLE_GTK4_GUI=OFF \
        -DDLNA_VERSION="${DLNA_VERSION}" \
    && cmake --build /build --parallel "$(nproc)" \
    && strip --strip-unneeded /build/dlna-server

FROM debian:bookworm-slim
ARG DLNA_VERSION
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        libcurl4 \
        tini \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 1000 dlna \
    && useradd --uid 1000 --gid 1000 --home-dir /config \
        --create-home --shell /usr/sbin/nologin dlna \
    && install -d -o dlna -g dlna /media

COPY --from=build /build/dlna-server /usr/local/bin/dlna-server

ENV HOME=/config \
    XDG_CONFIG_HOME=/config

USER dlna
VOLUME ["/config", "/media"]
EXPOSE 8200/tcp 1900/udp
STOPSIGNAL SIGTERM

HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
    CMD curl -fsS http://127.0.0.1:8200/description.xml >/dev/null || exit 1

LABEL org.opencontainers.image.title="dlna-server" \
      org.opencontainers.image.description="DLNA media server" \
      org.opencontainers.image.version="${DLNA_VERSION}" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.source="https://github.com/14ag/dlna-server"

# The server must stay in the foreground under PID 1. Docker users cannot
# replace --foreground by passing their own command; extra arguments are
# appended after it.
ENTRYPOINT ["/usr/bin/tini", "--", "dlna-server", "--foreground"]
