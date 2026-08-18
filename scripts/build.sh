#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
BUILD_TYPE="${BUILD_TYPE:-Release}"

cmake -S "${ROOT}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DRATES_BUILD_TESTS=ON \
  -DRATES_BUILD_PYTHON="${RATES_BUILD_PYTHON:-OFF}"
cmake --build "${BUILD_DIR}" --parallel

echo "Built: ${BUILD_DIR}/rates_cli"
