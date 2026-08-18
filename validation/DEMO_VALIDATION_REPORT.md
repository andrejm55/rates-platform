# Demo validation report

Generated: `2026-08-18 16:46:13 UTC`

This report is generated from the runnable portfolio-demo workflow. It is intended to make the project easy to review as a CV artifact, not to claim production model validation.

## Curated regression examples

| Example | Request type | Status | Headline result |
| --- | --- | --- | --- |
| Vanilla IRS | swap | pass | PV GBP -7,080.42, par rate 0.0383597 |
| European swaption | european_swaption | pass | PV GBP 10,229.99, par rate 0.0383597 |
| Bermudan swaption | bermudan_swaption | pass | PV GBP 18,137.38 |
| Callable range accrual | callable_range_accrual | pass | PV GBP 1,012,070.23 |
| Multi-curve bootstrap | multi_curve_bootstrap | pass | max abs residuals: discount 4.857e-17, projection 1.527e-16 |
| SABR smile calibration | sabr_calibration | pass | RMSE 0.000304384, alpha 0.051029, rho -0.12415, nu 0.74408 |
| Hull-White calibration | hull_white_calibration | pass | RMSE 7.467e-13, a 0.03, sigma 0.01 |
| Portfolio and risk | portfolio | pass | total PV GBP 8,187.63 |
| Swaption risk | swaption_risk | pass | base PV GBP 10,561.73, PV01 186.266 |

### Monte Carlo convergence

**Bermudan**

| Paths | Present value | Std error |
| --- | --- | --- |
| 2000 | 18,022.33 | 511.02 |
| 5000 | 18,142.77 | 321.619 |
| 10000 | 18,137.38 | 228.668 |

**Range Accrual**

| Paths | Present value | Std error |
| --- | --- | --- |
| 2000 | 1,011,515.51 | 977.355 |
| 5000 | 1,011,960.15 | 605.232 |
| 10000 | 1,012,070.23 | 425.361 |

### Curve calibration residuals

| Curve | Nodes | Max abs residual |
| --- | --- | --- |
| GBP-OIS-CALIBRATED | 7 | 4.857e-17 |
| GBP-TERM-3M-CALIBRATED | 7 | 1.527e-16 |

### SABR calibration residuals

| Strike | Market vol | Model vol | Residual |
| --- | --- | --- | --- |
| 0.02 | 0.28 | 0.279891 | -0.000109006 |
| 0.03 | 0.235 | 0.235338 | 0.000338347 |
| 0.04 | 0.21 | 0.209549 | -0.000450858 |
| 0.05 | 0.205 | 0.205346 | 0.000345573 |
| 0.06 | 0.215 | 0.214881 | -0.000119139 |

Calibrated parameters: alpha `0.0510293`, beta `0.5`, rho `-0.124148`, nu `0.744085`, shift `0.03`.

### Hull-White calibration

| Mean reversion | Volatility | RMSE |
| --- | --- | --- |
| 0.03 | 0.01 | 7.467e-13 |

## Model warnings

- **Vanilla IRS:** Floating coupons are projected coupon-by-coupon from the configured forward curve; historical fixings, publication lags and overnight compounding are simplified in this release.
- **Bermudan swaption:** Hull-White conditional bond prices use a curve-consistent Gaussian-factor approximation rather than a fully calibrated theta(t) implementation.
- **Bermudan swaption:** LSMC regression uses quadratic basis functions and should be convergence-tested for each product.
- **Callable range accrual:** Range accrual observations are discretised to the simulation grid rather than daily business-day observations.
- **Callable range accrual:** Issuer call decisions use a quadratic one-factor regression and omit funding, credit, and smile effects.
- **Multi-curve bootstrap:** Bootstrap uses simple deposit rates and regular fixed-for-floating par swaps with one shared discount curve.
- **Multi-curve bootstrap:** Multi-curve calibration supports simple OIS discount bootstrapping and projection nodes from deposits, FRAs, futures and regular basis swaps; it does not yet implement a global nonlinear solve.
- **Swaption risk:** Finite-difference PV01 bumps zero-rate nodes directly and does not rebuild curves from instrument quotes.

## Deliberate limitations

- Demo market data is static and embedded in JSON examples.
- No Bloomberg, Refinitiv, database, identity-provider or production deployment dependency is required for the portfolio showcase.
- The Docker/PostgreSQL/API path is retained as optional architecture demonstration code only.
- Multi-curve calibration is staged rather than a full global nonlinear calibration.
- SABR and Hull-White implementations are educational approximations and are not independently model-validated.
