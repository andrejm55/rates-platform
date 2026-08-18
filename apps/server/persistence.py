from __future__ import annotations
import hashlib
import json
import os
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import Any
try:
    import psycopg
    from psycopg.rows import dict_row
    from psycopg.types.json import Json
except ImportError:  # pragma: no cover - exercised when DB deps are intentionally absent.
    psycopg = None  # type: ignore[assignment]
    dict_row = None  # type: ignore[assignment]
    Json = None  # type: ignore[assignment]


SCHEMA = """
CREATE TABLE IF NOT EXISTS market_snapshots (
    id BIGSERIAL PRIMARY KEY,
    snapshot_id TEXT NOT NULL,
    payload JSONB NOT NULL,
    payload_hash TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (snapshot_id, payload_hash)
);

CREATE TABLE IF NOT EXISTS valuation_runs (
    id BIGSERIAL PRIMARY KEY,
    request_type TEXT NOT NULL,
    snapshot_id TEXT NOT NULL,
    market_snapshot_db_id BIGINT REFERENCES market_snapshots(id),
    input_payload JSONB NOT NULL,
    result_payload JSONB NOT NULL,
    input_hash TEXT NOT NULL,
    result_hash TEXT NOT NULL,
    success BOOLEAN NOT NULL,
    warning_count INTEGER NOT NULL DEFAULT 0,
    model_version TEXT NOT NULL,
    actor TEXT,
    duration_ms INTEGER NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS valuation_runs_created_at_idx ON valuation_runs (created_at DESC);
CREATE INDEX IF NOT EXISTS valuation_runs_request_type_idx ON valuation_runs (request_type);
CREATE INDEX IF NOT EXISTS valuation_runs_snapshot_id_idx ON valuation_runs (snapshot_id);
"""


def canonical_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def sha256_json(value: Any) -> str:
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def count_warnings(response: dict[str, Any]) -> int:
    result = response.get("result")
    if not isinstance(result, dict):
        return 0
    diagnostics = result.get("diagnostics")
    if isinstance(diagnostics, dict) and isinstance(diagnostics.get("warnings"), list):
        return len(diagnostics["warnings"])
    warnings = result.get("warnings")
    return len(warnings) if isinstance(warnings, list) else 0


@dataclass(frozen=True)
class PersistenceConfig:
    database_url: str | None
    model_version: str
    auto_init: bool

    @classmethod
    def from_env(cls) -> "PersistenceConfig":
        return cls(
            database_url=os.environ.get("DATABASE_URL"),
            model_version=os.environ.get("RATES_MODEL_VERSION", "0.1.0"),
            auto_init=os.environ.get("RATES_DB_AUTO_INIT", "1") not in {"0", "false", "False"},
        )


class ValuationStore:
    def __init__(self, config: PersistenceConfig | None = None) -> None:
        self.config = config or PersistenceConfig.from_env()

    @property
    def enabled(self) -> bool:
        return bool(self.config.database_url)

    def require_enabled(self) -> None:
        if not self.enabled:
            raise RuntimeError("DATABASE_URL is not configured")
        if psycopg is None:
            raise RuntimeError("psycopg is not installed")

    def connect(self):
        self.require_enabled()
        return psycopg.connect(self.config.database_url, row_factory=dict_row)

    def initialize(self) -> None:
        if not self.enabled:
            return
        self.require_enabled()
        with self.connect() as connection:
            connection.execute(SCHEMA)

    def record_valuation(self, payload: dict[str, Any], response: dict[str, Any], duration_ms: int, actor: str | None) -> int | None:
        if not self.enabled:
            return None
        market = payload.get("market", {})
        snapshot_id = str(response.get("snapshot_id") or market.get("snapshot_id") or "UNKNOWN")
        request_type = str(response.get("request_type") or payload.get("request", {}).get("type") or "unknown")
        market_hash = sha256_json(market)
        input_hash = sha256_json(payload)
        result_hash = sha256_json(response)
        with self.connect() as connection:
            market_row = connection.execute(
                """
                INSERT INTO market_snapshots (snapshot_id, payload, payload_hash)
                VALUES (%s, %s, %s)
                ON CONFLICT (snapshot_id, payload_hash)
                DO UPDATE SET snapshot_id = EXCLUDED.snapshot_id
                RETURNING id
                """,
                (snapshot_id, Json(market), market_hash),
            ).fetchone()
            run_row = connection.execute(
                """
                INSERT INTO valuation_runs (
                    request_type, snapshot_id, market_snapshot_db_id, input_payload, result_payload,
                    input_hash, result_hash, success, warning_count, model_version, actor, duration_ms
                )
                VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
                RETURNING id
                """,
                (
                    request_type,
                    snapshot_id,
                    market_row["id"],
                    Json(payload),
                    Json(response),
                    input_hash,
                    result_hash,
                    bool(response.get("success", False)),
                    count_warnings(response),
                    self.config.model_version,
                    actor,
                    duration_ms,
                ),
            ).fetchone()
            return int(run_row["id"])

    def list_valuations(self, limit: int = 50) -> list[dict[str, Any]]:
        with self.connect() as connection:
            rows = connection.execute(
                """
                SELECT id, request_type, snapshot_id, success, warning_count, model_version, actor, duration_ms, created_at
                FROM valuation_runs
                ORDER BY created_at DESC
                LIMIT %s
                """,
                (max(1, min(limit, 500)),),
            ).fetchall()
        return [self._json_ready(row) for row in rows]

    def get_valuation(self, run_id: int) -> dict[str, Any] | None:
        with self.connect() as connection:
            row = connection.execute(
                """
                SELECT *
                FROM valuation_runs
                WHERE id = %s
                """,
                (run_id,),
            ).fetchone()
        return self._json_ready(row) if row else None

    def list_markets(self, limit: int = 50) -> list[dict[str, Any]]:
        with self.connect() as connection:
            rows = connection.execute(
                """
                SELECT id, snapshot_id, payload_hash, created_at
                FROM market_snapshots
                ORDER BY created_at DESC
                LIMIT %s
                """,
                (max(1, min(limit, 500)),),
            ).fetchall()
        return [self._json_ready(row) for row in rows]

    @staticmethod
    def _json_ready(row: dict[str, Any] | None) -> dict[str, Any]:
        if row is None:
            return {}
        output = dict(row)
        for key, value in list(output.items()):
            if isinstance(value, datetime):
                output[key] = value.astimezone(UTC).isoformat()
        return output
