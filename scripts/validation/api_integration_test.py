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
PORT = int(os.environ.get("RATES_API_TEST_PORT", "8765"))
BASE_URL = f"http://127.0.0.1:{PORT}"
API_KEY = next((key.strip() for key in os.environ.get("RATES_API_KEYS", "").split(",") if key.strip()), None)


def wait_for_health(deadline_seconds: float = 20.0) -> dict[str, object]:
    deadline = time.monotonic() + deadline_seconds
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(f"{BASE_URL}/health", timeout=2.0) as response:
                return json.loads(response.read().decode("utf-8"))
        except (OSError, urllib.error.URLError) as exc:
            last_error = exc
            time.sleep(0.25)
    raise RuntimeError(f"API did not become healthy: {last_error}")


def post_json(path: str, payload: dict[str, object]) -> dict[str, object]:
    data = json.dumps(payload).encode("utf-8")
    headers = {"Content-Type": "application/json"}
    if API_KEY:
        headers["X-API-Key"] = API_KEY
    request = urllib.request.Request(
        f"{BASE_URL}{path}",
        data=data,
        headers=headers,
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=30.0) as response:
        return json.loads(response.read().decode("utf-8"))


def main() -> int:
    binary = Path(os.environ.get("RATES_CLI", ROOT / "build" / "rates_cli")).resolve()
    if not binary.exists():
        raise FileNotFoundError(f"CLI binary not found: {binary}")

    environment = os.environ.copy()
    environment["RATES_CLI"] = str(binary)
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
        if not health.get("binary_exists"):
            raise AssertionError(f"API health did not see CLI binary: {health}")
        if "database_enabled" not in health or "rate_limit_per_minute" not in health:
            raise AssertionError(f"API health did not expose production controls: {health}")

        with (ROOT / "examples" / "european_swaption_request.json").open("r", encoding="utf-8") as handle:
            payload = json.load(handle)
        result = post_json("/price", payload)
        if not result.get("success"):
            raise AssertionError(f"Pricing request failed: {result}")
        present_value = result.get("result", {}).get("present_value")
        if not isinstance(present_value, (int, float)) or present_value <= 0.0:
            raise AssertionError(f"Unexpected present value: {present_value}")
        return 0
    finally:
        process.terminate()
        try:
            process.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5.0)
        if process.returncode not in (0, -15, 143):
            stdout, stderr = process.communicate()
            print(stdout, file=sys.stdout)
            print(stderr, file=sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main())
