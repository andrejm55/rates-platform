from __future__ import annotations
from datetime import date
from pathlib import Path
from typing import Any
import json
import os
import subprocess
import tempfile
import altair as alt
import pandas as pd
import streamlit as st

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BINARY = ROOT / "build" / "rates_cli"
DEFAULT_MARKET = ROOT / "data" / "demo" / "market.json"
ARCHITECTURE_PNG = ROOT / "docs" / "rates_platform_architecture.png"
ARCHITECTURE_SVG = ROOT / "docs" / "rates_platform_architecture.svg"

st.set_page_config(page_title="Rates Derivatives Platform", page_icon="📈", layout="wide")


st.markdown(
    """
    <style>
      h1 {
        font-size: 2.25rem !important;
        line-height: 1.15 !important;
      }
      .block-container {
        padding-top: 2rem;
      }
      div[data-testid="stMetricValue"] {
        color: var(--text-color);
      }
      div[data-testid="stDataFrame"] {
        color: var(--text-color);
      }
    </style>
    """,
    unsafe_allow_html=True,
)


def parse_number(value: str) -> float:
    return float(value.replace(",", "").strip())


def parse_integer(value: str) -> int:
    return int(round(parse_number(value)))


def comma_float(value: float, decimals: int = 2) -> str:
    return f"{value:,.{decimals}f}"


def amount_input(label: str, value: float, key: str) -> float:
    current = st.session_state.get(key, comma_float(value))
    text = st.text_input(label, value=str(current), key=key)
    try:
        parsed = parse_number(text)
    except ValueError:
        st.error(f"{label} must be numeric. Commas are allowed.")
        return value
    return parsed


def integer_input(label: str, value: int, key: str) -> int:
    current = st.session_state.get(key, f"{value:,}")
    text = st.text_input(label, value=str(current), key=key)
    try:
        parsed = parse_integer(text)
    except ValueError:
        st.error(f"{label} must be an integer. Commas are allowed.")
        return value
    return parsed


def format_value(value: Any, decimals: int = 4) -> str:
    if isinstance(value, bool):
        return "Yes" if value else "No"
    if isinstance(value, int):
        return f"{value:,}"
    if isinstance(value, float):
        return f"{value:,.{decimals}f}"
    return "" if value is None else str(value)


def formatted_records(records: list[dict[str, Any]], money_columns: set[str] | None = None) -> pd.DataFrame:
    money_columns = money_columns or set()
    rows: list[dict[str, Any]] = []
    for record in records:
        row: dict[str, Any] = {}
        for key, value in record.items():
            if isinstance(value, (int, float)) and key in money_columns:
                row[key] = format_value(float(value), 2)
            elif isinstance(value, float):
                row[key] = format_value(value, 4)
            elif isinstance(value, int):
                row[key] = format_value(value, 0)
            else:
                row[key] = format_value(value)
        rows.append(row)
    return pd.DataFrame(rows)


def show_summary_table(metrics: list[tuple[str, str]]) -> None:
    st.dataframe(pd.DataFrame([{"Measure": label, "Value": value} for label, value in metrics]), use_container_width=True, hide_index=True)


def show_numeric_table(title: str, rows: list[dict[str, Any]], money_columns: set[str] | None = None) -> None:
    if rows:
        st.subheader(title)
        st.dataframe(formatted_records(rows, money_columns), use_container_width=True, hide_index=True)


def render_horizontal_bar(title: str, values: dict[str, Any], color: str = "#0b72c9") -> None:
    rows = [{"Label": key.replace("_", " "), "Value": float(value)} for key, value in values.items()]
    frame = pd.DataFrame(rows)
    chart = (
        alt.Chart(frame)
        .mark_bar(color=color)
        .encode(
            x=alt.X("Value:Q", title=title, axis=alt.Axis(labelFontSize=14, titleFontSize=15, format=",.0f")),
            y=alt.Y("Label:N", sort="-x", title=None, axis=alt.Axis(labelFontSize=14, labelLimit=320)),
            tooltip=[
                alt.Tooltip("Label:N", title="Measure"),
                alt.Tooltip("Value:Q", title="Value", format=",.2f"),
            ],
        )
        .properties(height=max(260, 42 * len(rows)))
    )
    st.altair_chart(chart, use_container_width=True)


def load_market() -> dict[str, Any]:
    with DEFAULT_MARKET.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def run_request(request: dict[str, Any]) -> dict[str, Any]:
    binary = Path(st.session_state.get("binary_path", str(DEFAULT_BINARY))).expanduser()
    if not binary.exists():
        raise FileNotFoundError(
            f"CLI binary not found at {binary}. Build it first with ./scripts/build.sh."
        )
    payload = {"market": load_market(), "request": request}
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2)
        temp_path = handle.name
    try:
        process = subprocess.run(
            [str(binary), "--input", temp_path],
            check=False,
            capture_output=True,
            text=True,
            timeout=120,
        )
        if not process.stdout.strip():
            raise RuntimeError(process.stderr.strip() or "Pricing process returned no output")
        result = json.loads(process.stdout)
        if not result.get("success", False):
            raise RuntimeError(result.get("error", "Unknown pricing failure"))
        return result
    finally:
        os.unlink(temp_path)


def show_pricing_result(response: dict[str, Any]) -> None:
    result = response["result"]
    show_summary_table([
        ("Present value", f"{result.get('currency', 'GBP')} {result.get('present_value', 0):,.2f}"),
        ("Par rate", f"{result.get('par_rate', 0) * 100:.4f}%"),
        ("Annuity", f"{result.get('annuity', 0):,.2f}"),
        ("Implied volatility", f"{result.get('implied_volatility', 0) * 100:.4f}%"),
    ])

    sensitivities = result.get("sensitivities", {})
    if sensitivities:
        show_numeric_table("Sensitivities", [{"Measure": key, "Value": value} for key, value in sensitivities.items()])

    cash_flows = result.get("cash_flows", [])
    if cash_flows:
        show_numeric_table("Cash flows", cash_flows, {"amount", "present_value"})

    diagnostics = result.get("diagnostics", {})
    with st.expander("Pricing diagnostics", expanded=True):
        st.json(diagnostics)


def common_swap_inputs(prefix: str) -> dict[str, Any]:
    market = load_market()
    curve_ids = [curve["id"] for curve in market.get("curves", [])]
    index_ids = [index["id"] for index in market.get("rate_indices", [])]
    default_discount = "GBP-SONIA-DISCOUNT" if "GBP-SONIA-DISCOUNT" in curve_ids else curve_ids[0]
    default_forward = "GBP-TERM-3M-FORWARD" if "GBP-TERM-3M-FORWARD" in curve_ids else default_discount
    default_index = "GBP-TERM-3M" if "GBP-TERM-3M" in index_ids else (index_ids[0] if index_ids else "")
    left, middle, right = st.columns(3)
    with left:
        notional = amount_input("Notional", 1_000_000.0, key=f"{prefix}_notional")
        fixed_rate = st.number_input("Fixed rate", min_value=-0.05, max_value=0.25, value=0.04, step=0.001, format="%.5f", key=f"{prefix}_fixed")
        discount_curve = st.selectbox("Discount curve", options=curve_ids, index=curve_ids.index(default_discount), key=f"{prefix}_discount_curve")
    with middle:
        effective = st.date_input("Effective date", value=date(2027, 7, 29), key=f"{prefix}_effective")
        maturity = st.date_input("Maturity date", value=date(2032, 7, 29), key=f"{prefix}_maturity")
        forward_curve = st.selectbox("Projection curve", options=curve_ids, index=curve_ids.index(default_forward), key=f"{prefix}_forward_curve")
    with right:
        frequency = st.selectbox("Fixed frequency", options=[3, 6, 12], index=2, format_func=lambda x: f"Every {x} months", key=f"{prefix}_freq")
        floating_frequency = st.selectbox("Floating frequency", options=[1, 3, 6, 12], index=1, format_func=lambda x: f"Every {x} months", key=f"{prefix}_float_freq")
        floating_index = st.selectbox("Floating index", options=[""] + index_ids, index=([""] + index_ids).index(default_index), key=f"{prefix}_floating_index")
        pay_fixed = st.toggle("Pay fixed", value=True, key=f"{prefix}_pay")
    return {
        "currency": "GBP",
        "discount_curve_id": discount_curve,
        "forward_curve_id": forward_curve,
        "floating_index_id": floating_index,
        "effective_date": effective.isoformat(),
        "maturity_date": maturity.isoformat(),
        "notional": notional,
        "fixed_rate": fixed_rate,
        "pay_fixed": pay_fixed,
        "fixed_frequency_months": frequency,
        "floating_frequency_months": floating_frequency,
        "floating_day_count": "actual360",
        "floating_fixing_lag_business_days": 2,
    }


st.title("Rates Derivatives and Structured Products Platform")
st.caption("C++20 pricing core with a Python research and control interface")

with st.sidebar:
    st.header("Runtime")
    st.text_input("CLI binary", value=str(DEFAULT_BINARY), key="binary_path")
    st.caption("The GUI calls the same C++ application service as the command-line workflow.")
    st.divider()
    st.header("Demo market")
    market = load_market()
    st.write(f"Snapshot: `{market['snapshot_id']}`")
    st.write(f"Valuation date: `{market['valuation_date']}`")
    st.write(f"Curves: {len(market['curves'])}")
    st.write(f"Volatility smiles: {len(market.get('volatility_smiles', []))}")
    with st.expander("What is this?"):
        st.write(
            "These values are loaded from the static demo market file "
            f"`{DEFAULT_MARKET.relative_to(ROOT)}`. The GUI attaches that market snapshot "
            "to each request before calling the C++ command-line engine."
        )

overview, curve_tab, swap_tab, euro_tab, sabr_tab, bermudan_tab, range_tab, risk_tab = st.tabs(
    ["Overview", "Curve bootstrap", "Swap", "European swaption", "SABR", "Bermudan", "Range accrual", "Portfolio and risk"]
)

with overview:
    st.subheader("Mental model")
    if ARCHITECTURE_PNG.exists():
        st.image(str(ARCHITECTURE_PNG), width=900)
    elif ARCHITECTURE_SVG.exists():
        st.image(str(ARCHITECTURE_SVG), width=900)
    else:
        st.info("Architecture image not found. The complete model is documented in docs/ARCHITECTURE.md.")

    st.subheader("Implemented modules")
    left, right = st.columns(2)
    with left:
        st.markdown(
            """
            - Date, calendar, schedule and day-count engine
            - Immutable market snapshots and interpolated zero curves
            - Swap discounting and par-rate calculations
            - Black 76 and Bachelier swaption pricing
            - SABR volatility and calibration
            """
        )
    with right:
        st.markdown(
            """
            - Hull-White-style Gaussian-factor simulation
            - Bermudan swaption LSMC
            - Callable range-accrual LSMC
            - PV01, vega, scenarios and portfolio aggregation
            - JSON CLI, optional Python bindings and local API
            """
        )
    st.warning("This is an educational and portfolio-quality MVP, not a production trading or valuation system. Review docs/IMPLEMENTATION_STATUS.md and the validation report before relying on results.")


with curve_tab:
    st.subheader("Deposit and par-swap curve bootstrap")
    st.caption("The MVP bootstrapper uses simple deposit rates and regular annual par swaps.")
    deposits_text = st.text_area("Deposits as maturity, rate", value="0.25,0.0405\n0.5,0.0400\n1.0,0.0393", height=100)
    swaps_text = st.text_area("Swaps as maturity, par rate", value="2,0.0382\n3,0.0377\n5,0.0370\n10,0.0364", height=120)
    if st.button("Bootstrap curve", type="primary"):
        try:
            deposits = [{"maturity": float(row.split(",")[0]), "rate": float(row.split(",")[1])} for row in deposits_text.splitlines() if row.strip()]
            swaps = [{"maturity": float(row.split(",")[0]), "par_rate": float(row.split(",")[1]), "payment_interval": 1.0} for row in swaps_text.splitlines() if row.strip()]
            st.session_state["curve_result"] = run_request({"type": "curve_bootstrap", "curve_id": "GUI-BOOTSTRAP", "deposits": deposits, "swaps": swaps})["result"]
        except Exception as exc:
            st.error(str(exc))
    if "curve_result" in st.session_state:
        result = st.session_state["curve_result"]
        frame = pd.DataFrame({"Time": result["times"], "Zero rate": result["zero_rates"], "Discount factor": result["discount_factors"]}).set_index("Time")
        st.line_chart(frame[["Zero rate"]])
        st.dataframe(frame, use_container_width=True)
        st.json(result)

with swap_tab:
    st.subheader("Interest-rate swap")
    request = {"type": "swap", **common_swap_inputs("swap")}
    if st.button("Price swap", type="primary"):
        try:
            st.session_state["swap_response"] = run_request(request)
        except Exception as exc:
            st.error(str(exc))
    if "swap_response" in st.session_state:
        show_pricing_result(st.session_state["swap_response"])

with euro_tab:
    st.subheader("European swaption")
    underlying = common_swap_inputs("euro")
    c1, c2, c3, c4 = st.columns(4)
    exercise = c1.date_input("Exercise date", value=date(2027, 7, 29), key="euro_exercise")
    strike = c2.number_input("Strike", value=0.04, step=0.001, format="%.5f", key="euro_strike")
    direction = c3.selectbox("Direction", ["payer", "receiver"], key="euro_direction")
    model = c4.selectbox("Model", ["bachelier", "black76", "sabr"], key="euro_model")
    default_vol = 0.008 if model == "bachelier" else 0.20
    volatility = st.number_input("Volatility", min_value=0.00001, value=default_vol, step=0.001, format="%.6f", key="euro_vol")
    request = {
        "type": "european_swaption",
        "underlying": underlying,
        "exercise_date": exercise.isoformat(),
        "strike": strike,
        "direction": direction,
        "model": model,
        "volatility": volatility,
        "sabr": {"alpha": 0.025, "beta": 0.5, "rho": -0.2, "nu": 0.5, "shift": 0.03},
    }
    if st.button("Price European swaption", type="primary"):
        try:
            st.session_state["euro_response"] = run_request(request)
        except Exception as exc:
            st.error(str(exc))
    if "euro_response" in st.session_state:
        show_pricing_result(st.session_state["euro_response"])

with sabr_tab:
    st.subheader("SABR smile calibration")
    c1, c2, c3 = st.columns(3)
    forward = c1.number_input("Forward", value=0.04, step=0.001, format="%.5f")
    expiry = c2.number_input("Expiry in years", value=2.0, min_value=0.01, step=0.25)
    beta = c3.number_input("Fixed beta", value=0.5, min_value=0.0, max_value=1.0, step=0.05)
    strikes_text = st.text_input("Strikes", value="0.02,0.03,0.04,0.05,0.06")
    vols_text = st.text_input("Lognormal volatilities", value="0.28,0.235,0.21,0.205,0.215")
    if st.button("Calibrate SABR", type="primary"):
        try:
            strikes = [float(value.strip()) for value in strikes_text.split(",")]
            vols = [float(value.strip()) for value in vols_text.split(",")]
            st.session_state["sabr_inputs"] = {"strikes": strikes, "vols": vols}
            st.session_state["sabr_response"] = run_request({
                "type": "sabr_calibration",
                "forward": forward,
                "expiry": expiry,
                "strikes": strikes,
                "volatilities": vols,
                "beta": beta,
                "shift": 0.03,
            })
        except Exception as exc:
            st.error(str(exc))
    if "sabr_response" in st.session_state:
        result = st.session_state["sabr_response"]["result"]
        sabr_inputs = st.session_state.get("sabr_inputs", {})
        show_summary_table([
            ("Alpha", f"{result['alpha']:,.6f}"),
            ("Rho", f"{result['rho']:,.6f}"),
            ("Nu", f"{result['nu']:,.6f}"),
            ("RMSE", f"{result['rmse']:,.8f}"),
        ])
        frame = pd.DataFrame({
            "Strike": sabr_inputs.get("strikes", []),
            "Market volatility": sabr_inputs.get("vols", []),
            "Model volatility": result["model_volatilities"],
        }).set_index("Strike")
        st.line_chart(frame)
        st.json(result)

with bermudan_tab:
    st.subheader("Bermudan swaption, Hull-White-style LSMC")
    underlying = common_swap_inputs("berm")
    exercise_text = st.text_input("Exercise dates", value="2027-07-29,2028-07-29,2029-07-29,2030-07-29", key="berm_dates")
    c1, c2, c3, c4 = st.columns(4)
    strike = c1.number_input("Strike", value=0.04, step=0.001, key="berm_strike")
    mean_reversion = c2.number_input("Mean reversion", value=0.03, step=0.005)
    model_vol = c3.number_input("Model volatility", value=0.01, step=0.001)
    with c4:
        paths = integer_input("Paths", 10_000, "berm_paths")
    if st.button("Price Bermudan", type="primary"):
        try:
            request = {
                "type": "bermudan_swaption",
                "underlying": underlying,
                "exercise_dates": [value.strip() for value in exercise_text.split(",")],
                "strike": strike,
                "direction": "payer",
                "hull_white": {"mean_reversion": mean_reversion, "volatility": model_vol},
                "monte_carlo": {"paths": int(paths), "steps_per_year": 12, "seed": 42},
            }
            st.session_state["berm_response"] = run_request(request)
        except Exception as exc:
            st.error(str(exc))
    if "berm_response" in st.session_state:
        show_pricing_result(st.session_state["berm_response"])

with range_tab:
    st.subheader("Callable range-accrual note")
    c1, c2, c3 = st.columns(3)
    with c1:
        notional = amount_input("Notional", 1_000_000.0, key="range_notional")
    coupon = c2.number_input("Annual coupon", value=0.06, step=0.005, key="range_coupon")
    with c3:
        paths = integer_input("Paths", 10_000, "range_paths")
    c4, c5, c6 = st.columns(3)
    lower = c4.number_input("Lower rate bound", value=0.01, step=0.005)
    upper = c5.number_input("Upper rate bound", value=0.06, step=0.005)
    model_vol = c6.number_input("Hull-White volatility", value=0.01, step=0.001, key="range_hw_vol")
    if st.button("Price range accrual", type="primary"):
        try:
            request = {
                "type": "callable_range_accrual",
                "effective_date": "2026-07-29",
                "maturity_date": "2031-07-29",
                "notional": notional,
                "coupon_rate": coupon,
                "lower_bound": lower,
                "upper_bound": upper,
                "issuer_call_dates": ["2027-07-29", "2028-07-29", "2029-07-29", "2030-07-29"],
                "hull_white": {"mean_reversion": 0.03, "volatility": model_vol},
                "monte_carlo": {"paths": int(paths), "steps_per_year": 12, "seed": 42},
            }
            st.session_state["range_response"] = run_request(request)
        except Exception as exc:
            st.error(str(exc))
    if "range_response" in st.session_state:
        show_pricing_result(st.session_state["range_response"])

with risk_tab:
    st.subheader("Risk and portfolio examples")
    risk_type = st.radio("Workflow", ["Swap risk", "Swaption risk", "Demo portfolio"], horizontal=True)
    if risk_type == "Swap risk":
        request = {"type": "swap_risk", **common_swap_inputs("risk_swap"), "curve_bump": 0.0001}
    elif risk_type == "Swaption risk":
        underlying = common_swap_inputs("risk_option")
        request = {
            "type": "swaption_risk",
            "underlying": underlying,
            "exercise_date": "2027-07-29",
            "strike": 0.04,
            "direction": "payer",
            "model": "bachelier",
            "volatility": 0.008,
        }
    else:
        request = {
            "type": "portfolio",
            "portfolio_id": "GUI-DEMO",
            "default_swaption_volatility": 0.008,
            "monte_carlo": {"paths": 4000, "steps_per_year": 12, "seed": 42},
            "trades": [
                {
                    "trade_id": "SWAP-1",
                    "book": "RATES",
                    "quantity": 1,
                    "instrument_type": "swap",
                    "instrument": {
                        "effective_date": "2027-07-29", "maturity_date": "2032-07-29",
                        "notional": 1_000_000, "fixed_rate": 0.04, "pay_fixed": True
                    },
                },
                {
                    "trade_id": "SWO-1",
                    "book": "OPTIONS",
                    "quantity": 1,
                    "instrument_type": "european_swaption",
                    "instrument": {
                        "underlying": {
                            "effective_date": "2027-07-29", "maturity_date": "2032-07-29",
                            "notional": 1_000_000, "fixed_rate": 0.04
                        },
                        "exercise_date": "2027-07-29", "strike": 0.04, "direction": "payer"
                    },
                },
            ],
        }
    if st.button("Run workflow", type="primary"):
        try:
            st.session_state[f"risk_response_{risk_type}"] = run_request(request)
        except Exception as exc:
            st.error(str(exc))
    risk_key = f"risk_response_{risk_type}"
    if risk_key in st.session_state:
        response = st.session_state[risk_key]
        result = response["result"]
        if risk_type == "Demo portfolio":
            show_summary_table([("Portfolio PV", f"GBP {result['total_present_value']:,.2f}")])
            show_numeric_table("Trades", result["trades"], {"present_value"})
            render_horizontal_bar("Present value by book", result["present_value_by_book"])
        else:
            show_summary_table([("Base PV", f"GBP {result['base_present_value']:,.2f}")])
            show_numeric_table("Sensitivities", [{"Measure": k, "Value": v} for k, v in result["sensitivities"].items()])
            st.subheader("Scenario PnL")
            render_horizontal_bar("Scenario PnL", result["scenario_pnl"])
            if result.get("warnings"):
                for warning in result["warnings"]:
                    st.warning(warning)
