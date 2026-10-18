#!/usr/bin/env bash
set -euo pipefail
build_dir=${1:-build}
coordinator=127.0.0.1:50051
cluster_dir=$(mktemp -d)
pids=()
cleanup() {
  for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
  wait 2>/dev/null || true
  rm -rf "$cluster_dir"
}
trap cleanup EXIT
"$build_dir/pfs-coordinator" "$coordinator" "$cluster_dir/metadata.pb" & pids+=($!)
for i in $(seq 1 8); do
  port=$((50100+i))
  "$build_dir/pfs-storage" "127.0.0.1:$port" "$cluster_dir/node$i" "$coordinator" "127.0.0.1:$port" "node$i" & pids+=($!)
done
sleep 3
"$build_dir/integration_test" "$coordinator"
"$build_dir/stress_test" "$coordinator" 210 216
