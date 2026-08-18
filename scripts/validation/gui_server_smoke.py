#!/usr/bin/env python3
from __future__ import annotations

import os
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PORT = int(os.environ.get("RATES_GUI_TEST_PORT", "8766"))
URL = f"http://127.0.0.1:{PORT}"


def wait_for_page(deadline_seconds: float = 30.0) -> str:
    deadline = time.monotonic() + deadline_seconds
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(URL, timeout=2.0) as response:
                return response.read().decode("utf-8", errors="replace")
        except (OSError, urllib.error.URLError) as exc:
            last_error = exc
            time.sleep(0.5)
    raise RuntimeError(f"GUI did not serve a page: {last_error}")


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
            "streamlit",
            "run",
            "apps/gui/app.py",
            "--server.headless=true",
            f"--server.port={PORT}",
            "--browser.gatherUsageStats=false",
        ],
        cwd=ROOT,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        html = wait_for_page()
        if "streamlit" not in html.lower():
            raise AssertionError("GUI page did not look like a Streamlit response")
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
