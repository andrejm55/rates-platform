# Implementation status

## Complete and runnable in release 0.1.0 (current)

- C++20 build system and reusable static library
- Date parsing, date arithmetic and end-of-month handling
- Weekend calendar and custom holiday storage
- Business-day adjustment and regular schedule generation
- Actual/360, Actual/365 Fixed and 30/360 year fractions
- Immutable market snapshot representation
- Interpolated zero-rate curves and discount factors
- Separate discount and projection curve usage for swaps and European swaptions
- Coupon-by-coupon floating-leg projection with fixing and payment lag fields
- Historical floating-rate fixing lookup for already-fixed swap coupons
- Rate-index reference data for projection curve, fixing lag, publication lag, observation shift, lookback, lockout and compounding convention
- Simple deposit and annual par-swap curve bootstrap
- Multi-curve bootstrap request for OIS discount curves and term projection curves using deposits, FRAs, futures and regular basis swaps
- Volatility smile interpolation
- Swaption volatility cube interpolation across expiry, tenor and strike
- Interest-rate swap pricing, par rate and cash-flow report
- Black 76 European swaption pricing and implied volatility
- Bachelier European swaption pricing and implied volatility
- Hagan-style SABR lognormal volatility
- Normal SABR-style volatility conversion for Bachelier pricing
- Multistart SABR alpha, rho and nu calibration with fixed beta
- Hull-White-style Gaussian-factor simulation
- Hull-White one-factor calibration to ATM normal swaption volatilities
- Hull-White analytic factor-option and normal-volatility benchmarks
- Bermudan swaption Least-Squares Monte Carlo
- Callable range-accrual Least-Squares Monte Carlo
- Swap parallel and bucketed PV01
- European swaption PV01, vega and scenarios
- Mixed portfolio valuation with trade-level error isolation
- Parallel trade-level portfolio valuation
- JSON request and response contract
- C++ CLI
- Streamlit GUI
- FastAPI local API wrapper
- Optional PostgreSQL persistence and valuation audit history for architecture demonstration
- Optional API key authentication and in-memory rate limiting for local API wrapping
- Optional Docker Compose service stack for architecture demonstration
- Optional pybind11 application-service binding
- Unit tests, integration smoke tests and CI workflow
- Dockerfile, Codex bootstrap and runbooks

## Partially implemented

- Multi-curve calibration is a staged bootstrap, not a full global non-linear solve across all OIS, term projection and basis instruments.
- Overnight compounding conventions are represented and projected from the curve, but daily observed overnight paths are not yet expanded coupon-by-coupon.
- Cash settlement is represented, but uses a physical annuity approximation.
- Curve calibration is functional for simple deposits and regular annual par swaps, not full OIS and basis instruments.
- Volatility cubes are implemented, but SABR parameter cubes and smoothing are not.
- Hull-White simulation and normal-volatility calibration are functional, but exact theta(t), analytic bond-option calibration and lattice pricing are not implemented.
- Structured-product valuation is functional for one callable range-accrual design with grid-based observations.
- Portfolio aggregation is parallel at trade level but remains single-currency.
- Persistence supports PostgreSQL valuation audit history as an optional architecture demonstration, not as the primary demo workflow.

## Architecture modules for potential future releases

- Full OIS daily compounding with publication calendars, observation shifts, lookbacks and lockouts applied to every overnight observation
- Multi-curve global calibration with robust interpolation choices, quote helpers and simultaneous residual solving
- SABR cube parameter calibration and smoothing across expiry-tenor pillars
- Exact normal SABR asymptotic formula and market-calibrated normal SABR cube
- Exact Hull-White theta(t), analytic bond-option calibration and trinomial tree
- Libor Market Model
- PDE framework
- CMS convexity and smile adjustment
- Callable CMS and steepener products
- Daily range-accrual schedules
- Commodity swing options
- Automatic differentiation
- Full trade capture, market-data lifecycle and user/role management
- Hardened authentication provider integration and distributed rate limiting
- Parallel scenario execution
- QuantLib reference regression suite

## Meaning of complete code

The repository contains complete, compilable and executable code for the implemented release scope. It does not claim that every instrument and infrastructure component in the long-term architecture specification has reached production-ready implementation. Deferred modules are identified here and in the validation report rather than represented by misleading empty classes.
