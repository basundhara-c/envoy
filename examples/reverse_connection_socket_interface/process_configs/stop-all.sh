#!/bin/bash

# Stop all services started by start-all.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_FILE="$SCRIPT_DIR/.pids"

if [ -f "$PID_FILE" ]; then
    echo "Stopping all services..."
    PIDS=$(cat "$PID_FILE")
    for pid in $PIDS; do
        if kill -0 "$pid" 2>/dev/null; then
            echo "Stopping process $pid..."
            kill "$pid"
        fi
    done
    rm -f "$PID_FILE"
    echo "All services stopped."
else
    echo "No PID file found. Attempting to stop services by name..."
    
    # Stop nginx backend
    pkill -f 'nginx.*backend-nginx' 2>/dev/null && echo "Stopped backend service"
    
    # Stop envoy processes
    pkill -f 'envoy.*on-prem-envoy-process' 2>/dev/null && echo "Stopped on-prem Envoy"
    pkill -f 'envoy.*cloud-envoy-process' 2>/dev/null && echo "Stopped cloud Envoy"
    
    echo "Cleanup complete."
fi
