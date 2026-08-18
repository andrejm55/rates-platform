# Operations

This project is intended to run as a local demo and portfolio showcase. The primary workflow does not require Docker, PostgreSQL, authentication, or production infrastructure.

## Local Demo Workflow

Build, test, run examples, and generate validation outputs:

```bash
./scripts/demo.sh
```

Launch the Streamlit GUI:

```bash
./scripts/run_gui.sh
```

The GUI uses the local C++ pricing engine and static demo market data. No database is required.

## Local API

The API can be run locally to demonstrate JSON-based access to the pricing engine:

```bash
./scripts/run_api.sh
```

Check the API health endpoint:

```bash
curl http://127.0.0.1:8000/health
```

## Future Infrastructure Extension

PostgreSQL persistence, Dockerized deployment, API authentication, and audit-history storage are not part of the current demo operating model. They are potential future extensions if the project were ever developed beyond a local CV/demo application.

A possible future Docker/PostgreSQL setup could use:

```bash
cp .env.example .env
docker compose up --build
```

In that future setup, PostgreSQL could store valuation audit records in a Docker volume such as:

```text
rates_postgres_data
```

## Future API Authentication

If authentication were enabled in a future deployment, `RATES_API_KEYS` could be used to require API keys:

```bash
export RATES_API_KEYS=dev-api-key
curl -H 'X-API-Key: dev-api-key' http://127.0.0.1:8000/health
```

Multiple API keys could be supplied as comma-separated values.

## Future Audit History

If `DATABASE_URL` were configured in a future persistence-enabled setup, successful `POST /price` calls could record request and result JSON, input/result hashes, model version, actor, warning count, duration, and timestamp.

Recent valuation runs could then be queried with:

```bash
curl -H 'X-API-Key: dev-api-key' http://127.0.0.1:8000/valuations
```

## Production Scope

Internet-facing deployment is deliberately out of scope for this project. A production version would require TLS, managed identity, secret management, centralized rate limiting, structured logging, backups, monitoring, and operational support.
