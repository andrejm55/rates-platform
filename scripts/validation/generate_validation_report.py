#!/usr/bin/env python3
from __future__ import annotations

import html
import json
import math
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
OUTPUT_DIR = ROOT / "validation" / "outputs"
REPORT_PATH = ROOT / "validation" / "DEMO_VALIDATION_REPORT.md"
SCREENSHOT_DIR = ROOT / "docs" / "screenshots"

CURATED_OUTPUTS = [
    ("Vanilla IRS", "swap_result.json"),
    ("European swaption", "european_swaption_result.json"),
    ("Bermudan swaption", "bermudan_result.json"),
    ("Callable range accrual", "range_accrual_result.json"),
    ("Multi-curve bootstrap", "curve_bootstrap_result.json"),
    ("SABR smile calibration", "sabr_result.json"),
    ("Hull-White calibration", "hull_white_calibration_result.json"),
    ("Portfolio and risk", "portfolio_result.json"),
    ("Swaption risk", "swaption_risk_result.json"),
]


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def fmt_number(value: Any, digits: int = 6) -> str:
    if not isinstance(value, (int, float)):
        return ""
    if not math.isfinite(float(value)):
        return str(value)
    if abs(float(value)) >= 1000:
        return f"{float(value):,.2f}"
    if abs(float(value)) < 0.0001 and value != 0:
        return f"{float(value):.3e}"
    return f"{float(value):.{digits}g}"


def headline_metric(payload: dict[str, Any]) -> str:
    result = payload.get("result", {})
    request_type = payload.get("request_type", "")
    if request_type == "multi_curve_bootstrap":
        discount = max_abs(result.get("discount_residuals", []))
        projection = max_abs(result.get("projection_residuals", []))
        return f"max abs residuals: discount {fmt_number(discount, 3)}, projection {fmt_number(projection, 3)}"
    if request_type == "sabr_calibration":
        return f"RMSE {fmt_number(result.get('rmse'), 6)}, alpha {fmt_number(result.get('alpha'), 5)}, rho {fmt_number(result.get('rho'), 5)}, nu {fmt_number(result.get('nu'), 5)}"
    if request_type == "hull_white_calibration":
        return f"RMSE {fmt_number(result.get('rmse'), 6)}, a {fmt_number(result.get('mean_reversion'), 5)}, sigma {fmt_number(result.get('volatility'), 5)}"
    if request_type == "portfolio":
        return f"total PV GBP {fmt_number(result.get('total_present_value'))}"
    if request_type == "swaption_risk":
        return f"base PV GBP {fmt_number(result.get('base_present_value'))}, PV01 {fmt_number(result.get('sensitivities', {}).get('parallel_pv01'))}"
    if "present_value" in result:
        metric = f"PV GBP {fmt_number(result.get('present_value'))}"
        if isinstance(result.get("par_rate"), (int, float)) and result.get("par_rate"):
            metric += f", par rate {fmt_number(result.get('par_rate'))}"
        return metric
    return "completed"


def max_abs(values: list[Any]) -> float:
    numbers = [abs(float(value)) for value in values if isinstance(value, (int, float))]
    return max(numbers) if numbers else 0.0


def markdown_table(headers: list[str], rows: list[list[str]]) -> str:
    output = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    for row in rows:
        output.append("| " + " | ".join(cell.replace("\n", " ") for cell in row) + " |")
    return "\n".join(output)


def calibration_tables(outputs: dict[str, dict[str, Any]]) -> list[str]:
    sections: list[str] = []
    curve = outputs.get("curve_bootstrap_result.json", {}).get("result", {})
    if curve:
        sections.append(
            "### Curve calibration residuals\n\n"
            + markdown_table(
                ["Curve", "Nodes", "Max abs residual"],
                [
                    [
                        curve.get("discount_curve", {}).get("curve_id", "discount"),
                        str(len(curve.get("discount_residuals", []))),
                        fmt_number(max_abs(curve.get("discount_residuals", [])), 3),
                    ],
                    [
                        curve.get("projection_curve", {}).get("curve_id", "projection"),
                        str(len(curve.get("projection_residuals", []))),
                        fmt_number(max_abs(curve.get("projection_residuals", [])), 3),
                    ],
                ],
            )
        )

    sabr = outputs.get("sabr_result.json", {}).get("result", {})
    request = load_json(ROOT / "examples" / "sabr_request.json").get("request", {}) if sabr else {}
    if sabr:
        rows = []
        for strike, market_vol, model_vol in zip(
            request.get("strikes", []),
            request.get("volatilities", []),
            sabr.get("model_volatilities", []),
            strict=False,
        ):
            rows.append([
                fmt_number(strike, 5),
                fmt_number(market_vol, 6),
                fmt_number(model_vol, 6),
                fmt_number(float(model_vol) - float(market_vol), 6),
            ])
        sections.append(
            "### SABR calibration residuals\n\n"
            + markdown_table(["Strike", "Market vol", "Model vol", "Residual"], rows)
            + f"\n\nCalibrated parameters: alpha `{fmt_number(sabr.get('alpha'))}`, beta `{fmt_number(sabr.get('beta'))}`, rho `{fmt_number(sabr.get('rho'))}`, nu `{fmt_number(sabr.get('nu'))}`, shift `{fmt_number(sabr.get('shift'))}`."
        )

    hw = outputs.get("hull_white_calibration_result.json", {}).get("result", {})
    if hw:
        sections.append(
            "### Hull-White calibration\n\n"
            + markdown_table(
                ["Mean reversion", "Volatility", "RMSE"],
                [[fmt_number(hw.get("mean_reversion")), fmt_number(hw.get("volatility")), fmt_number(hw.get("rmse"), 6)]],
            )
        )
    return sections


def monte_carlo_section() -> str:
    path = OUTPUT_DIR / "monte_carlo_convergence.json"
    if not path.exists():
        return "### Monte Carlo convergence\n\nNo Monte Carlo convergence output was found. Run `./scripts/demo.sh` to regenerate it."
    payload = load_json(path)
    sections = ["### Monte Carlo convergence"]
    for case_name, runs in payload.get("cases", {}).items():
        rows = [
            [
                str(run.get("paths", "")),
                fmt_number(run.get("present_value")),
                fmt_number(run.get("standard_error")),
            ]
            for run in runs
        ]
        sections.append(f"\n**{case_name.replace('_', ' ').title()}**\n\n" + markdown_table(["Paths", "Present value", "Std error"], rows))
    return "\n".join(sections)


def warning_section(outputs: dict[str, dict[str, Any]]) -> str:
    warnings: list[str] = []
    for label, filename in CURATED_OUTPUTS:
        result = outputs.get(filename, {}).get("result", {})
        diagnostics = result.get("diagnostics", {}) if isinstance(result, dict) else {}
        for warning in diagnostics.get("warnings", []) if isinstance(diagnostics, dict) else []:
            warnings.append(f"- **{label}:** {warning}")
        for warning in result.get("warnings", []) if isinstance(result, dict) else []:
            warnings.append(f"- **{label}:** {warning}")
    if not warnings:
        return "## Model warnings\n\nNo model warnings were emitted by the curated examples."
    return "## Model warnings\n\n" + "\n".join(warnings)


def write_svg(path: Path, title: str, rows: list[tuple[str, str]], accent: str) -> None:
    width = 980
    row_height = 54
    height = 148 + row_height * len(rows)
    items = []
    for idx, (name, value) in enumerate(rows):
        y = 122 + idx * row_height
        items.append(
            f'<text x="48" y="{y}" class="label">{html.escape(name)}</text>'
            f'<text x="932" y="{y}" class="value" text-anchor="end">{html.escape(value)}</text>'
            f'<line x1="48" y1="{y + 18}" x2="932" y2="{y + 18}" stroke="#d8dee8" stroke-width="1"/>'
        )
    content = "\n".join(items)
    path.write_text(
        f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
  <rect width="980" height="{height}" fill="#f7f9fc"/>
  <rect x="28" y="26" width="924" height="{height - 52}" rx="8" fill="#ffffff" stroke="#c9d2df"/>
  <rect x="28" y="26" width="924" height="10" rx="5" fill="{accent}"/>
  <text x="48" y="82" class="title">{html.escape(title)}</text>
  <text x="48" y="106" class="sub">Generated from runnable demo validation artifacts</text>
  {content}
  <style>
    .title {{ font: 700 28px Arial, sans-serif; fill: #182230; }}
    .sub {{ font: 400 14px Arial, sans-serif; fill: #667085; }}
    .label {{ font: 600 18px Arial, sans-serif; fill: #344054; }}
    .value {{ font: 500 18px Arial, sans-serif; fill: #101828; }}
  </style>
</svg>
""",
        encoding="utf-8",
    )


def write_visuals(outputs: dict[str, dict[str, Any]]) -> None:
    SCREENSHOT_DIR.mkdir(parents=True, exist_ok=True)
    summary_rows = []
    for label, filename in CURATED_OUTPUTS[:6]:
        payload = outputs.get(filename)
        if payload:
            summary_rows.append((label, headline_metric(payload)))
    write_svg(SCREENSHOT_DIR / "demo-summary.svg", "Rates Engine Demo Summary", summary_rows, "#2457a6")

    curve = outputs.get("curve_bootstrap_result.json", {}).get("result", {})
    sabr = outputs.get("sabr_result.json", {}).get("result", {})
    model_rows = [
        ("Discount calibration residual", fmt_number(max_abs(curve.get("discount_residuals", [])), 3)),
        ("Projection calibration residual", fmt_number(max_abs(curve.get("projection_residuals", [])), 3)),
        ("SABR smile RMSE", fmt_number(sabr.get("rmse"), 6)),
        ("SABR alpha / rho / nu", f"{fmt_number(sabr.get('alpha'))} / {fmt_number(sabr.get('rho'))} / {fmt_number(sabr.get('nu'))}"),
    ]
    write_svg(SCREENSHOT_DIR / "calibration-summary.svg", "Calibration Validation Snapshot", model_rows, "#1f7a5a")


def main() -> int:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    outputs = {filename: load_json(OUTPUT_DIR / filename) for _, filename in CURATED_OUTPUTS if (OUTPUT_DIR / filename).exists()}

    rows = []
    for label, filename in CURATED_OUTPUTS:
        payload = outputs.get(filename)
        if not payload:
            rows.append([label, filename, "missing", ""])
            continue
        rows.append([
            label,
            payload.get("request_type", ""),
            "pass" if payload.get("success") else "fail",
            headline_metric(payload),
        ])

    generated_at = datetime.now(UTC).strftime("%Y-%m-%d %H:%M:%S UTC")
    report = [
        "# Demo validation report",
        "",
        f"Generated: `{generated_at}`",
        "",
        "This report is generated from the runnable portfolio-demo workflow. It is intended to make the project easy to review as a CV artifact, not to claim production model validation.",
        "",
        "## Curated regression examples",
        "",
        markdown_table(["Example", "Request type", "Status", "Headline result"], rows),
        "",
        monte_carlo_section(),
        "",
    ]
    for section in calibration_tables(outputs):
        report.extend([section, ""])
    report.extend([
        warning_section(outputs),
        "",
        "## Deliberate limitations",
        "",
        "- Demo market data is static and embedded in JSON examples.",
        "- No Bloomberg, Refinitiv, database, identity-provider or production deployment dependency is required for the portfolio showcase.",
        "- The Docker/PostgreSQL/API path is retained as optional architecture demonstration code only.",
        "- Multi-curve calibration is staged rather than a full global nonlinear calibration.",
        "- SABR and Hull-White implementations are educational approximations and are not independently model-validated.",
    ])
    REPORT_PATH.write_text("\n".join(report) + "\n", encoding="utf-8")
    write_visuals(outputs)
    print(f"Wrote {REPORT_PATH}")
    print(f"Wrote {SCREENSHOT_DIR / 'demo-summary.svg'}")
    print(f"Wrote {SCREENSHOT_DIR / 'calibration-summary.svg'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
