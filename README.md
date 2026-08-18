# Rates Derivatives and Structured Products Engine

A runnable C++20 quant-finance platform for rates curves, swaps, swaptions, SABR calibration, early-exercise Monte Carlo, structured-product analytics, risk, portfolio valuation, and graphical exploration.

## Implemented functionality

### Financial foundation

- ISO date parsing and arithmetic
- Weekend and custom-holiday calendars
- Following, Modified Following, Preceding, Modified Preceding and Unadjusted conventions
- Actual/360, Actual/365 Fixed and 30/360 day counts
- Coupon schedule generation
- Immutable market-data snapshots
- Continuously compounded zero curves
- Linear zero-rate interpolation
- Parallel and node curve shocks

### Products and pricing

- Fixed-for-floating interest-rate swaps
- European payer and receiver swaptions
- Physical and approximate cash settlement
- Black 76 pricing
- Bachelier normal pricing
- Implied-volatility inversion
- SABR Hagan-style lognormal volatility
- SABR alpha, rho and nu calibration with fixed beta
- Bermudan swaptions using a Hull-White-style Gaussian factor and Least-Squares Monte Carlo
- Callable range-accrual notes using regression-based issuer call decisions

### Risk and portfolio

- Swap parallel PV01
- Bucketed curve PV01
- Swaption PV01 and vega
- Rate and volatility scenarios
- Mixed portfolio valuation
- Trade-level failure isolation
- Diagnostics, warnings and Monte Carlo standard errors

### Interfaces

- JSON command-line interface
- Streamlit GUI
- FastAPI local API wrapper
- Optional pybind11 module
- Example requests
- Unit and integration tests
- Dockerfile and automation scripts

## Quick start

### 1. Build and test

```bash
./scripts/build.sh
ctest --test-dir build --output-on-failure
```

Requirements:

- CMake 3.20 or newer
- A C++20 compiler, such as GCC 11+, Clang 14+, or recent MSVC
- Python 3.10+ for the GUI and API

### 2. Run a pricing request

```bash
./build/rates_cli --input examples/european_swaption_request.json
```

Other examples:

```bash
./build/rates_cli --input examples/swap_request.json
./build/rates_cli --input examples/sabr_request.json
./build/rates_cli --input examples/bermudan_request.json
./build/rates_cli --input examples/range_accrual_request.json
./build/rates_cli --input examples/swaption_risk_request.json
./build/rates_cli --input examples/portfolio_request.json
```

Run every example:

```bash
./scripts/run_examples.sh
```

### 3. Launch the GUI

```bash
./scripts/run_gui.sh
```

This creates a local virtual environment, installs the Python requirements, and starts Streamlit.

### 4. Launch the local API

```bash
./scripts/run_api.sh
```

Then send the same JSON structure used by the CLI to `POST /price`.

### 5. Codex bootstrap

```bash
./scripts/codex_bootstrap.sh
```

Codex should read `AGENTS.md` and `CODEX_RUN.md` before changing the repository.

## Input contract

Every CLI request contains:

```json
{
  "market": {
    "valuation_date": "2026-07-27",
    "snapshot_id": "GBP-DEMO-2026-07-27",
    "curves": [],
    "volatility_smiles": []
  },
  "request": {
    "type": "european_swaption"
  }
}
```

The application service validates and dispatches the request to the appropriate C++ pricing engine. Results are returned as JSON with prices, sensitivities, cash flows, metrics and warnings.

## Repository map

```text
include/rates/       Public C++ interfaces
src/                 C++ implementations
apps/cli/            JSON command-line application
apps/gui/            Streamlit graphical interface
apps/server/         FastAPI wrapper
bindings/python/     Optional pybind11 module
examples/            Runnable pricing and risk requests
data/demo/           Demonstration market snapshot
config/              Default modelling settings
tests/               Dependency-free C++ test executable
benchmarks/          Basic performance harness
docs/                Architecture and model documentation
scripts/             Build, test, GUI, API and Codex workflows
```

## Design rule

Instruments define contractual economics. Market objects define the valuation environment. Models define financial dynamics. Pricing engines combine the three. Risk engines perturb the market state and repeat valuation.

## Important limitations

This system is not approved for trading, accounting, regulatory reporting, client valuation or investment decisions. Some advanced products use explicitly documented approximations. See `docs/IMPLEMENTATION_STATUS.md` and `validation/DEMO_VALIDATION_REPORT.md` for the current implementation scope and validation notes.
