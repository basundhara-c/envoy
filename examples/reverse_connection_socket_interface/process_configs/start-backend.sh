#!/bin/bash

# Start backend service (nginx) for testing
# This simulates the on-prem-service that would normally run in Docker

echo "Starting backend service on port 8080..."

# Check if nginx is available
if ! command -v nginx &> /dev/null; then
    echo "Error: nginx is not installed. Please install nginx first."
    echo "On Ubuntu/Debian: sudo apt-get install nginx"
    echo "On CentOS/RHEL: sudo yum install nginx"
    exit 1
fi

# Create nginx config directory if it doesn't exist
mkdir -p /tmp/backend-nginx

# Copy our nginx config
cp backend-service.yaml /tmp/backend-nginx/nginx.conf

# Start nginx with our config
nginx -c /tmp/backend-nginx/nginx.conf -p /tmp/backend-nginx/

echo "Backend service started on port 8080"
echo "Test with: curl http://localhost:8080/on_prem_service"
echo "Health check: curl http://localhost:8080/health"
echo ""
echo "To stop: pkill -f 'nginx.*backend-nginx'"
