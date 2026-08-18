#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PORT = int(os.environ.get("RATES_DB_API_TEST_PORT", "8768"))
BASE_URL = f"http://127.0.0.1:{PORT}"
API_KEY = next((key.strip() for key in os.environ.get("RATES_API_KEYS", "").split(",") if key.strip()), "db-test-key")


def request_headers(content_type: bool = False) -> dict[str, str]:
    headers: dict[str, str] = {}
    if API_KEY:
        headers["X-API-Key"] = API_KEY
    if content_type:
        headers["Content-Type"] = "application/json"
    return headers


def wait_for_health(deadline_seconds: float = 30.0) -> dict[str, object]:
    deadline = time.monotonic() + deadline_seconds
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            request = urllib.request.Request(f"{BASE_URL}/health", headers=request_headers())
            with urllib.request.urlopen(request, timeout=2.0) as response:
                return json.loads(response.read().decode("utf-8"))
        except (OSError, urllib.error.URLError) as exc:
            last_error = exc
            time.sleep(0.25)
    raise RuntimeError(f"API did not become healthy: {last_error}")


def post_json(path: str, payload: dict[str, object]) -> dict[str, object]:
    request = urllib.request.Request(
        f"{BASE_URL}{path}",
        data=json.dumps(payload).encode("utf-8"),
        headers=request_headers(content_type=True),
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=30.0) as response:
        return json.loads(response.read().decode("utf-8"))


def get_json(path: str) -> object:
    request = urllib.request.Request(f"{BASE_URL}{path}", headers=request_headers())
    with urllib.request.urlopen(request, timeout=30.0) as response:
        return json.loads(response.read().decode("utf-8"))


def main() -> int:
    if not os.environ.get("DATABASE_URL"):
        raise RuntimeError("DATABASE_URL must be set for DB audit integration validation")
    binary = Path(os.environ.get("RATES_CLI", ROOT / "build" / "rates_cli")).resolve()
    if not binary.exists():
        raise FileNotFoundError(f"CLI binary not found: {binary}")

    environment = os.environ.copy()
    environment["RATES_CLI"] = str(binary)
    environment.setdefault("RATES_API_KEYS", "db-test-key")
    environment.setdefault("RATES_DB_AUTO_INIT", "1")
    process = subprocess.Popen(
        [
            sys.executable,
            "-m",
            "uvicorn",
            "apps.server.api:app",
            "--host",
            "127.0.0.1",
            "--port",
            str(PORT),
            "--log-level",
            "warning",
        ],
        cwd=ROOT,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        health = wait_for_health()
        if not health.get("database_enabled"):
            raise AssertionError(f"API health did not report database enabled: {health}")
        if not health.get("auth_enabled"):
            raise AssertionError(f"API health did not report auth enabled: {health}")

        with (ROOT / "examples" / "european_swaption_request.json").open("r", encoding="utf-8") as handle:
            payload = json.load(handle)
        result = post_json("/price", payload)
        run_id = result.get("audit", {}).get("valuation_run_id")
        if not isinstance(run_id, int):
            raise AssertionError(f"Pricing response did not include audit run id: {result}")

        valuation = get_json(f"/valuations/{run_id}")
        if not isinstance(valuation, dict) or valuation.get("id") != run_id:
            raise AssertionError(f"Valuation lookup failed: {valuation}")
        if valuation.get("request_type") != "european_swaption":
            raise AssertionError(f"Unexpected request type in audit row: {valuation}")

        valuations = get_json("/valuations?limit=10")
        if not isinstance(valuations, list) or not any(row.get("id") == run_id for row in valuations):
            raise AssertionError(f"Valuation run was not listed: {valuations}")

        markets = get_json("/markets?limit=10")
        if not isinstance(markets, list) or not markets:
            raise AssertionError(f"Market snapshot was not recorded: {markets}")
        return 0
    finally:
        process.terminate()
        try:
            process.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5.0)


if __name__ == "__main__":
    raise SystemExit(main())
