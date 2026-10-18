# Parallel File System

A distributed parallel file system written in modern C++ with gRPC, Protobuf, and CMake. Files are split into fixed-size stripes, placed round-robin across live storage nodes, transferred concurrently, verified by checksum, and made visible atomically by a metadata coordinator.

## What is implemented

- gRPC/Protobuf APIs for coordinator and storage services
- parallel striped reads and writes across any number of registered nodes
- centralized metadata with atomic snapshot persistence
- read/write lease tokens: concurrent readers or one exclusive writer per path
- write reservation followed by commit, so readers never observe a partial write
- versioned stripe paths and end-to-end FNV-1a checksum verification
- concurrent C++ client library and `pfsctl` command-line client
- round-trip, concurrent-reader, deletion, and 210-client/216-file stress tests
- eight-node Docker Compose deployment
- reproducible CMake build, multi-stage Docker image, and GitHub Actions CI

## Architecture

```text
                       acquire read/write token
    pfsctl / clients  -------------------------->  coordinator
          |                                         | metadata
          | reserve/get stripe layout               | snapshots
          |<---------------------------------------->|
          |
          +====== concurrent gRPC stripe I/O =======+
          |        |        |                 |     |
       storage1 storage2 storage3    ...    storage8
```

For a write, the client obtains an exclusive lease, reserves a new version and layout, uploads stripes concurrently, validates every returned checksum, and commits the layout. The commit is the visibility point. Reads obtain shared leases, fetch the committed layout, download its stripes concurrently, validate checksums, and reassemble the original byte stream in stripe order.

The coordinator allows multiple read tokens for a path or one write token. Requests wait up to 30 seconds; leases expire after 120 seconds so a failed client cannot lock a file forever. Tokens are checked on every metadata operation. This provides strict per-file consistency while allowing unrelated files and shared reads to proceed concurrently.

## Quick start with Docker

Requirements: Docker Engine with Compose v2.

```bash
docker compose up -d --build
printf 'hello parallel world\n' > hello.txt
docker compose --profile test run --rm -v "$PWD:/work" --entrypoint pfsctl integration coordinator:50051 put /work/hello.txt /hello.txt
docker compose --profile test run --rm --entrypoint pfsctl integration coordinator:50051 ls
docker compose --profile test run --rm -v "$PWD:/work" --entrypoint pfsctl integration coordinator:50051 get /hello.txt /work/restored.txt
docker compose down
```

The Compose topology starts one coordinator and eight independently persisted storage nodes. Port `50051` exposes the coordinator to host clients.

## Native build

On Debian/Ubuntu:

```bash
sudo apt-get install build-essential cmake ninja-build protobuf-compiler \
  protobuf-compiler-grpc libprotobuf-dev libgrpc++-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Start a coordinator and at least one storage node:

```bash
./build/pfs-coordinator 0.0.0.0:50051 ./data/metadata.pb
./build/pfs-storage 0.0.0.0:50052 ./data/node1 localhost:50051 localhost:50052 node1
```

Every storage process takes `LISTEN DATA_DIR COORDINATOR ADVERTISED_ADDRESS NODE_ID`. `ADVERTISED_ADDRESS` must be reachable by clients.

## CLI

```text
pfsctl COORDINATOR put LOCAL REMOTE
pfsctl COORDINATOR get REMOTE LOCAL
pfsctl COORDINATOR rm REMOTE
pfsctl COORDINATOR ls
```

Example:

```bash
./build/pfsctl localhost:50051 put ./archive.tar /datasets/archive.tar
./build/pfsctl localhost:50051 get /datasets/archive.tar ./restored.tar
./build/pfsctl localhost:50051 rm /datasets/archive.tar
```

The client library defaults to 1 MiB stripes. The C++ API accepts a different stripe size per write.

## Tests and benchmark scenario

Run the full native suite against an isolated eight-node cluster:

```bash
./scripts/run-native-tests.sh build
```

Or use containers:

```bash
./scripts/run-integration.sh
./scripts/run-stress.sh
```

`integration_test` verifies a multi-stripe round trip, 16 concurrent readers, checksum equality, metadata listing, and deletion. `stress_test` launches 210 concurrent client threads and creates and reads back 216 independently striped files. It exits nonzero on any failed client, missing file, or byte mismatch and prints elapsed time and completed operations per second.

Do not treat a benchmark result as portable across machines: hardware, container runtime, filesystem, and build type materially affect throughput. A run is valid only when `successful_files=216` and `failures=0`.

### Verified reference run

GitHub Actions run [39163729306](https://github.com/vishnusangadala/Parallel-File-System/actions/runs/39163729306) built commit `fce265f` in Release mode on an `ubuntu-24.04` hosted runner and completed the native eight-node suite on October 18, 2026:

```text
PASS round-trip, striping, 16 concurrent readers, delete
clients=210 files=216 successful_files=216 failures=0 elapsed_ms=677 ops_per_sec=638.109
```

The same run also completed the CTest suite and multi-stage Docker image build. This is one CI-run measurement, not a general hardware performance claim.

## Consistency and failure behavior

- Metadata commits are serialized under the coordinator lock and persisted with write-then-rename.
- Readers see either the previous committed version or the new committed version, never a partially uploaded layout.
- A storage RPC failure aborts the client operation; an uncommitted version is never published.
- Checksums are stored in committed metadata and verified on every read.
- Token expiry provides lock recovery after client failure.

This implementation deliberately keeps the coordinator single-leader and uses striping without replication or erasure coding. A storage-node loss makes stripes on that node unavailable; production evolution would add replicated metadata consensus, stripe replication/erasure coding, heartbeats, rebalancing, authentication/TLS, quotas, and garbage collection for abandoned or superseded versions.

## Repository layout

```text
proto/                 service and message definitions
include/pfs/           client API and shared utilities
src/                   coordinator, storage server, client, CLI
tests/                 integration and stress executables
scripts/               local and Docker test runners
.github/workflows/     CI build and verification
```

## License

MIT
