#!/usr/bin/env python3
from __future__ import annotations

import copy
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BINARY = Path(os.environ.get("RATES_CLI", ROOT / "build" / "rates_cli")).resolve()
PATH_COUNTS = (2000, 5000, 10000)


def run_request(template_path: Path, paths: int) -> dict[str, object]:
    with template_path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    request = copy.deepcopy(payload["request"])
    request.setdefault("monte_carlo", {})
    request["monte_carlo"]["paths"] = paths
    request["monte_carlo"]["seed"] = 42
    payload["request"] = request

    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as handle:
        json.dump(payload, handle)
        path = handle.name
    try:
        process = subprocess.run(
            [str(BINARY), "--input", path],
            capture_output=True,
            text=True,
            cwd=ROOT,
            timeout=120,
        )
        if process.returncode != 0:
            raise RuntimeError(process.stderr or process.stdout)
        result = json.loads(process.stdout)
    finally:
        os.unlink(path)
    if not result.get("success"):
        raise AssertionError(result)
    pricing = result["result"]
    diagnostics = pricing["diagnostics"]
    return {
        "paths": paths,
        "present_value": pricing["present_value"],
        "standard_error": diagnostics["standard_error"],
    }


def main() -> int:
    if not BINARY.exists():
        raise FileNotFoundError(f"CLI binary not found: {BINARY}")

    output: dict[str, object] = {"path_counts": list(PATH_COUNTS), "cases": {}}
    for case_name in ("bermudan", "range_accrual"):
        template = ROOT / "examples" / f"{case_name}_request.json"
        runs = [run_request(template, paths) for paths in PATH_COUNTS]
        output["cases"][case_name] = runs
        if runs[-1]["standard_error"] >= runs[0]["standard_error"]:
            raise AssertionError(f"{case_name} standard error did not improve: {runs}")

    print(json.dumps(output, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
