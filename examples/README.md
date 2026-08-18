# Curated demo examples

These requests are intentionally small, static and reviewable. They are designed to show breadth across rates products and model layers without requiring market-data vendors, a database, or external services.

| Example | File | What it demonstrates |
| --- | --- | --- |
| Vanilla GBP fixed-floating IRS | `swap_request.json` | OIS discounting, 3M projection curve, coupon cash-flow report and par-rate/PV output |
| European swaption | `european_swaption_request.json` | Bachelier pricing, expiry-tenor-strike volatility cube lookup, vega and forward Greeks |
| Bermudan swaption | `bermudan_request.json` | Hull-White-style one-factor simulation with Least-Squares Monte Carlo exercise |
| Callable range accrual | `range_accrual_request.json` | Structured payoff, issuer call dates and Monte Carlo convergence diagnostics |
| Multi-curve calibration | `curve_bootstrap_request.json` | OIS discount inputs plus projection deposits, FRAs, futures and basis swaps |
| SABR smile calibration | `sabr_request.json` | Shifted lognormal SABR calibration and strike-by-strike residuals |
| Hull-White calibration | `hull_white_calibration_request.json` | One-factor mean-reversion/volatility fit to normal swaption vol benchmarks |
| Portfolio and risk | `portfolio_request.json`, `swaption_risk_request.json` | Parallel trade valuation, book aggregation, PV01, vega and rate/vol scenarios |

Run the full showcase from the repository root:

```bash
./scripts/demo.sh
```

The script writes refreshed results to `validation/outputs/` and a reviewer-friendly report to `validation/DEMO_VALIDATION_REPORT.md`.
