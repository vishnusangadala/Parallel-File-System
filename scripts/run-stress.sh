#!/usr/bin/env bash
set -euo pipefail
compose="docker compose"
$compose up -d --build
trap '$compose down -v' EXIT
sleep 3
$compose --profile test run --rm stress
