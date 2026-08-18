from __future__ import annotations
import json
import os
import subprocess
import tempfile
import time
from collections import defaultdict, deque
from pathlib import Path
from typing import Any
from fastapi import Depends, FastAPI, Header, HTTPException, Request
try:
    from apps.server.persistence import PersistenceConfig, ValuationStore
except ModuleNotFoundError:
    from persistence import PersistenceConfig, ValuationStore

ROOT = Path(__file__).resolve().parents[2]
BINARY = Path(os.environ.get("RATES_CLI", ROOT / "build" / "rates_cli"))
API_KEYS = {key.strip() for key in os.environ.get("RATES_API_KEYS", "").split(",") if key.strip()}
RATE_LIMIT_PER_MINUTE = int(os.environ.get("RATES_RATE_LIMIT_PER_MINUTE", "120"))

app = FastAPI(title="Rates Derivatives Engine API", version="0.1.0")
store = ValuationStore(PersistenceConfig.from_env())
request_times: dict[str, deque[float]] = defaultdict(deque)


@app.on_event("startup")
def startup() -> None:
    if store.config.auto_init:
        store.initialize()


def client_identity(request: Request, x_api_key: str | None = Header(default=None)) -> str:
    return x_api_key or (request.client.host if request.client else "unknown")


def authenticate(request: Request, x_api_key: str | None = Header(default=None)) -> str | None:
    if API_KEYS and x_api_key not in API_KEYS:
        raise HTTPException(status_code=401, detail="Missing or invalid API key")
    identity = client_identity(request, x_api_key)
    now = time.monotonic()
    window = request_times[identity]
    while window and now - window[0] > 60.0:
        window.popleft()
    if len(window) >= RATE_LIMIT_PER_MINUTE:
        raise HTTPException(status_code=429, detail="Rate limit exceeded")
    window.append(now)
    return x_api_key


def database_required() -> None:
    if not store.enabled:
        raise HTTPException(status_code=503, detail="DATABASE_URL is not configured")

@app.get("/health")
def health() -> dict[str, Any]:
    return {
        "status": "ok",
        "binary": str(BINARY),
        "binary_exists": BINARY.exists(),
        "database_enabled": store.enabled,
        "auth_enabled": bool(API_KEYS),
        "rate_limit_per_minute": RATE_LIMIT_PER_MINUTE,
    }


@app.post("/price")
def price(payload: dict[str, Any], actor: str | None = Depends(authenticate)) -> dict[str, Any]:
    if not BINARY.exists():
        raise HTTPException(status_code=503, detail=f"CLI binary not found: {BINARY}")
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as handle:
        json.dump(payload, handle)
        path = handle.name
    start = time.monotonic()
    try:
        process = subprocess.run([str(BINARY), "--input", path], capture_output=True, text=True, timeout=120)
        if not process.stdout.strip():
            raise HTTPException(status_code=500, detail=process.stderr or "No output from pricing process")
        response = json.loads(process.stdout)
        if not response.get("success", False):
            raise HTTPException(status_code=400, detail=response.get("error", "Pricing failed"))
        run_id = store.record_valuation(payload, response, int((time.monotonic() - start) * 1000), actor)
        if run_id is not None:
            response["audit"] = {"valuation_run_id": run_id}
        return response
    finally:
        os.unlink(path)


@app.get("/valuations")
def valuations(limit: int = 50, _: str | None = Depends(authenticate)) -> list[dict[str, Any]]:
    database_required()
    return store.list_valuations(limit)


@app.get("/valuations/{run_id}")
def valuation(run_id: int, _: str | None = Depends(authenticate)) -> dict[str, Any]:
    database_required()
    row = store.get_valuation(run_id)
    if row is None:
        raise HTTPException(status_code=404, detail="Valuation run not found")
    return row


@app.get("/markets")
def markets(limit: int = 50, _: str | None = Depends(authenticate)) -> list[dict[str, Any]]:
    database_required()
    return store.list_markets(limit)
