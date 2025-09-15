#!/bin/bash

# Start on-prem Envoy process
# This script starts the on-prem Envoy with debug logging

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="$SCRIPT_DIR/on-prem-envoy-process.yaml"

echo "Starting on-prem Envoy process..."
echo "Config file: $CONFIG_FILE"
echo "Admin interface: http://localhost:8888"
echo "Reverse connection API: http://localhost:9000"
echo "Ingress HTTP: http://localhost:6060"
echo ""

# Check if Envoy binary exists
if ! command -v envoy &> /dev/null; then
    echo "Error: envoy binary not found in PATH"
    echo "Please build Envoy first or add it to your PATH"
    echo "To build: bazel build //source/exe:envoy-static"
    echo "To add to PATH: export PATH=\$PATH:\$(pwd)/bazel-bin/source/exe"
    exit 1
fi

# Start Envoy with debug logging
envoy -c "$CONFIG_FILE" \
      --concurrency 1 \
      -l debug \
      --drain-time-s 3
