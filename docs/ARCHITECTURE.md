# Architecture summary

## Mental model

```text
Market quotes and reference data
              |
              v
Validated immutable market snapshot
              |
              v
Curves, volatility smiles and model parameters
              |
              +--------------------+
              |                    |
              v                    v
      Instrument definitions    Calibration
              |                    |
              +---------+----------+
                        v
             Pricing and numerical engines
        Analytic | SABR | Monte Carlo | LSMC
                        |
                        v
      Price, cash flows, Greeks and diagnostics
                        |
              +---------+----------+
              |                    |
              v                    v
        Risk and scenarios     Portfolio aggregation
              |                    |
              +---------+----------+
                        v
             CLI, GUI, API and reports
```

## Separation of responsibilities

- Instruments define contractual terms.
- Market snapshots define the valuation environment.
- Models define financial dynamics.
- Pricing engines combine instruments, market state and models.
- Risk engines produce shocked snapshots and repeat valuation.
- Application services validate and orchestrate workflows.
- Interfaces never contain independent pricing formulas.

## Main runtime path

1. The CLI, GUI, API or Python binding receives a complete JSON payload.
2. `ApplicationService` parses and validates the market snapshot.
3. It constructs the requested instrument or portfolio.
4. It selects the requested pricing engine.
5. The engine produces a standard `PricingResult` or `RiskReport`.
6. The service serialises results, diagnostics and warnings to JSON.

## C++ component map

```text
include/rates/core
    Date, Calendar, SchedulePeriod, day counts

include/rates/market
    YieldCurve, VolatilitySmile, MarketDataSnapshot

include/rates/instruments
    Swap, EuropeanSwaption, BermudanSwaption, CallableRangeAccrual

include/rates/models
    Black76, Bachelier, SABR, HullWhiteModel

include/rates/pricing
    SwapPricer, EuropeanSwaptionPricer, BermudanSwaptionPricer,
    CallableRangeAccrualPricer, PricingResult

include/rates/risk
    RiskEngine and RiskReport

include/rates/portfolio
    PortfolioPricer and trade-level results

include/rates/persistence
    Dependency-free JSON parser and writer

include/rates/services
    ApplicationService request dispatcher
```

## Extending the platform

In case of a new product, it should be added in the following order:

1. instrument definition
2. request parser
3. pricing engine
4. pricing diagnostics
5. tests
6. example request
7. risk support
8. GUI workflow
9. documentation and limitations
