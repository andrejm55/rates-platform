#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV="${ROOT}/.venv"
if [[ ! -x "${ROOT}/build/rates_cli" ]]; then "${ROOT}/scripts/build.sh"; fi
if [[ ! -d "${VENV}" ]]; then python3 -m venv "${VENV}"; fi
"${VENV}/bin/python" -m pip install --upgrade pip
"${VENV}/bin/python" -m pip install -r "${ROOT}/requirements.txt"
cd "${ROOT}"
exec "${VENV}/bin/python" -m uvicorn apps.server.api:app --reload --port 8000
