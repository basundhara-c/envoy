#!/bin/bash

# Master script to start all services for reverse connection testing
# This script starts: backend service, cloud envoy, and on-prem envoy

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=========================================="
echo "Starting Reverse Connection Test Environment"
echo "=========================================="
echo ""

# Function to check if port is in use
check_port() {
    local port=$1
    if lsof -Pi :$port -sTCP:LISTEN -t >/dev/null 2>&1; then
        echo "Warning: Port $port is already in use"
        return 1
    fi
    return 0
}

# Check required ports
echo "Checking port availability..."
check_port 8080 && echo "✓ Port 8080 (backend) available" || echo "✗ Port 8080 (backend) in use"
check_port 8888 && echo "✓ Port 8888 (on-prem admin) available" || echo "✗ Port 8888 (on-prem admin) in use"
check_port 8889 && echo "✓ Port 8889 (cloud admin) available" || echo "✗ Port 8889 (cloud admin) in use"
check_port 9000 && echo "✓ Port 9000 (on-prem reverse conn API) available" || echo "✗ Port 9000 (on-prem reverse conn API) in use"
check_port 9001 && echo "✓ Port 9001 (cloud reverse conn API) available" || echo "✗ Port 9001 (cloud reverse conn API) in use"
check_port 6060 && echo "✓ Port 6060 (on-prem ingress) available" || echo "✗ Port 6060 (on-prem ingress) in use"
check_port 8085 && echo "✓ Port 8085 (cloud egress) available" || echo "✗ Port 8085 (cloud egress) in use"
echo ""

# Start backend service
echo "1. Starting backend service..."
cd "$SCRIPT_DIR"
./start-backend.sh &
BACKEND_PID=$!
sleep 2

# Start cloud envoy
echo "2. Starting cloud Envoy..."
cd "$SCRIPT_DIR"
./start-cloud-envoy.sh &
CLOUD_PID=$!
sleep 3

# Start on-prem envoy
echo "3. Starting on-prem Envoy..."
cd "$SCRIPT_DIR"
./start-on-prem-envoy.sh &
ONPREM_PID=$!

echo ""
echo "=========================================="
echo "All services started!"
echo "=========================================="
echo "Backend service PID: $BACKEND_PID"
echo "Cloud Envoy PID: $CLOUD_PID"
echo "On-prem Envoy PID: $ONPREM_PID"
echo ""
echo "Service URLs:"
echo "  Backend service:     http://localhost:8080/on_prem_service"
echo "  On-prem admin:       http://localhost:8888"
echo "  Cloud admin:         http://localhost:8889"
echo "  On-prem ingress:     http://localhost:6060/on_prem_service"
echo "  Cloud egress:        http://localhost:8085/on_prem_service"
echo ""
echo "To stop all services:"
echo "  kill $BACKEND_PID $CLOUD_PID $ONPREM_PID"
echo "  or run: ./stop-all.sh"
echo ""
echo "To test the reverse connection:"
echo "  curl http://localhost:8085/on_prem_service"
echo ""

# Save PIDs for stop script
echo "$BACKEND_PID $CLOUD_PID $ONPREM_PID" > .pids

# Wait for user to stop
echo "Press Ctrl+C to stop all services..."
trap 'echo "Stopping all services..."; kill $BACKEND_PID $CLOUD_PID $ONPREM_PID 2>/dev/null; rm -f .pids; exit 0' INT
wait
