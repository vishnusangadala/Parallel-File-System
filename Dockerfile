FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake ninja-build protobuf-compiler protobuf-compiler-grpc libprotobuf-dev libgrpc++-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel 2 && ctest --test-dir build --output-on-failure
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends libprotobuf32 libgrpc++1.51 && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/pfs-coordinator /src/build/pfs-storage /src/build/pfsctl /src/build/integration_test /src/build/stress_test /usr/local/bin/
ENTRYPOINT ["pfsctl"]
