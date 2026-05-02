FROM --platform=linux/amd64 gcc:13-bookworm AS build

RUN apt-get update && apt-get install -y --no-install-recommends cmake && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)

FROM --platform=linux/amd64 ubuntu:24.04

WORKDIR /app
COPY --from=build /src/build/engine .
ENTRYPOINT ["./engine"]