FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends cmake g++ make python3 python3-pip python3-venv && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DRATES_BUILD_TESTS=ON && cmake --build build --parallel && ctest --test-dir build --output-on-failure

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends python3 python3-pip python3-venv && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /app /app
RUN python3 -m venv /app/.venv && /app/.venv/bin/pip install --no-cache-dir -r requirements.txt
EXPOSE 8501 8000
CMD ["/app/.venv/bin/python", "-m", "uvicorn", "apps.server.api:app", "--host", "0.0.0.0", "--port", "8000"]
