#!/usr/bin/env bash
set -euo pipefail
compose="docker compose"
$compose up -d --build
trap '$compose down -v' EXIT
for _ in {1..60}; do $compose exec -T coordinator true 2>/dev/null && sleep 2 && break; sleep 1; done
$compose --profile test run --rm integration
