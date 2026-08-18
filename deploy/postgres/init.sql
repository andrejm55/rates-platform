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
