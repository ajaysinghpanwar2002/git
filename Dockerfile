FROM debian:bullseye AS builder

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      clang \
      make \
      pkg-config \
      libssl-dev \
      zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY Makefile ./
COPY src/ ./src/

RUN clang \
      -Wall -Wextra -std=c17 -g \
      $(pkg-config --cflags openssl) \
      -o mygit src/main.c \
      $(pkg-config --libs openssl) -lz

# Final stage: a slimmer Debian image that only has runtime deps
FROM debian:bullseye-slim

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      libssl1.1 \
      zlib1g \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/mygit ./mygit
ENTRYPOINT ["./mygit"]
