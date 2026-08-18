#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "Repository: ${ROOT}"
command -v cmake >/dev/null || { echo "cmake is required"; exit 1; }
command -v c++ >/dev/null || { echo "A C++20 compiler is required"; exit 1; }
command -v python3 >/dev/null || { echo "python3 is required for the GUI"; exit 1; }

"${ROOT}/scripts/build.sh"
ctest --test-dir "${ROOT}/build" --output-on-failure
"${ROOT}/build/rates_cli" --input "${ROOT}/examples/european_swaption_request.json" >/tmp/rates_engine_smoke_test.json
python3 - <<'PY'
import json
with open('/tmp/rates_engine_smoke_test.json') as handle:
    result = json.load(handle)
assert result['success'] is True, result
print('CLI smoke test passed. PV:', result['result']['present_value'])
PY

echo "Bootstrap complete. Read AGENTS.md and CODEX_RUN.md before editing."
