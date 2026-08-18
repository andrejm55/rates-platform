#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="${ROOT}/build/rates_cli"
OUTPUT_DIR="${ROOT}/validation/outputs"
LAUNCH_GUI=0

for arg in "$@"; do
  case "${arg}" in
    --launch-gui)
      LAUNCH_GUI=1
      ;;
    --help|-h)
      cat <<'USAGE'
Usage: ./scripts/demo.sh [--launch-gui]

Builds the C++ engine, runs tests, executes curated pricing/calibration examples,
refreshes validation artifacts, and writes validation/DEMO_VALIDATION_REPORT.md.

Use --launch-gui to start the Streamlit GUI after the report is generated.
USAGE
      exit 0
      ;;
    *)
      echo "Unknown argument: ${arg}" >&2
      exit 2
      ;;
  esac
done

mkdir -p "${OUTPUT_DIR}"

echo "==> Building C++ engine"
"${ROOT}/scripts/build.sh"

echo "==> Running C++ tests"
ctest --test-dir "${ROOT}/build" --output-on-failure | tee "${OUTPUT_DIR}/ctest.txt"

echo "==> Checking Python interface files"
python3 -m compileall "${ROOT}/apps/server" "${ROOT}/scripts/validation" >/dev/null

run_example() {
  local request="$1"
  local output="$2"
  local label="$3"
  echo "==> ${label}"
  "${BINARY}" --input "${ROOT}/examples/${request}" | python3 -m json.tool > "${OUTPUT_DIR}/${output}"
}

run_example "swap_request.json" "swap_result.json" "Vanilla GBP fixed-floating IRS"
run_example "european_swaption_request.json" "european_swaption_result.json" "European swaption with normal-vol cube"
run_example "bermudan_request.json" "bermudan_result.json" "Bermudan swaption LSMC"
run_example "range_accrual_request.json" "range_accrual_result.json" "Callable range accrual LSMC"
run_example "curve_bootstrap_request.json" "curve_bootstrap_result.json" "OIS discount plus term projection bootstrap"
run_example "sabr_request.json" "sabr_result.json" "SABR smile calibration"
run_example "hull_white_calibration_request.json" "hull_white_calibration_result.json" "Hull-White normal-vol calibration"
run_example "portfolio_request.json" "portfolio_result.json" "Mixed portfolio valuation"
run_example "swaption_risk_request.json" "swaption_risk_result.json" "Swaption risk scenarios"

echo "==> Running API smoke test"
RATES_API_TEST_PORT="${RATES_API_TEST_PORT:-8765}" python3 "${ROOT}/scripts/validation/api_integration_test.py"

echo "==> Running GUI server smoke test"
python3 "${ROOT}/scripts/validation/gui_server_smoke.py"

echo "==> Running Monte Carlo convergence validation"
python3 "${ROOT}/scripts/validation/monte_carlo_convergence.py" > "${OUTPUT_DIR}/monte_carlo_convergence.json"

echo "==> Generating portfolio validation report"
python3 "${ROOT}/scripts/validation/generate_validation_report.py"

cat <<SUMMARY

Demo complete.

Key artifacts:
  ${ROOT}/validation/DEMO_VALIDATION_REPORT.md
  ${ROOT}/docs/screenshots/demo-summary.svg
  ${ROOT}/docs/screenshots/calibration-summary.svg
  ${ROOT}/validation/outputs/

SUMMARY

if [[ "${LAUNCH_GUI}" -eq 1 ]]; then
  echo "==> Launching Streamlit GUI"
  exec "${ROOT}/scripts/run_gui.sh"
fi
