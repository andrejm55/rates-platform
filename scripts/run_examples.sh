#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="${ROOT}/build/rates_cli"
if [[ ! -x "${BINARY}" ]]; then
  "${ROOT}/scripts/build.sh"
fi
for request in "${ROOT}"/examples/*_request.json; do
  echo
  echo "===== $(basename "${request}") ====="
  "${BINARY}" --input "${request}"
done
