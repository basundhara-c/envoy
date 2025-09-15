#!/bin/bash

# Start cloud Envoy process
# This script starts the cloud Envoy with debug logging

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="$SCRIPT_DIR/cloud-envoy-process.yaml"

echo "Starting cloud Envoy process..."
echo "Config file: $CONFIG_FILE"
echo "Admin interface: http://localhost:8889"
echo "Reverse connection API: http://localhost:9001"
echo "Egress listener: http://localhost:8085"
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
      -l trace \
      --drain-time-s 3 \
      --log-format '[%Y-%m-%d %T.%e][%t][%l][%n] %v' \
      --component-log-level upstream:debug,http:debug,http2:debug,grpc:debug,router:debug,connection:debug
