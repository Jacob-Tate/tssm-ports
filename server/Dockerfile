# TSSM co-op relay server.
# Build from the repository root:
#   docker build -f server/Dockerfile -t tssm-server .
# Run (UDP port 7865):
#   docker run -d --name tssm-server --restart unless-stopped -p 7865:7865/udp tssm-server

FROM debian:bookworm-slim AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake g++ make git ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY include/netplay include/netplay
COPY server server
RUN cmake -S server -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel \
    && strip build/tssm-server build/tssm-server-test

FROM debian:bookworm-slim
# Links the ghcr.io package to the repository.
LABEL org.opencontainers.image.source="https://github.com/aidp2/tssm-ports" \
      org.opencontainers.image.description="TSSM co-op relay server"
RUN useradd --system --no-create-home tssm
COPY --from=build /src/build/tssm-server /usr/local/bin/tssm-server
# The integration test client ships too, so a deployment can be checked in place:
#   docker exec tssm-server tssm-server-test 127.0.0.1 7865
COPY --from=build /src/build/tssm-server-test /usr/local/bin/tssm-server-test
ENV TSSM_SERVER_PORT=7865
USER tssm
EXPOSE 7865/udp
ENTRYPOINT ["tssm-server"]
