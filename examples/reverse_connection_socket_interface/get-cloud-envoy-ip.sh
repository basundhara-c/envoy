#!/bin/bash

# Change to the script directory
cd "$(dirname "$0")"

# Start only the cloud-envoy container first
echo "Starting cloud-envoy container..."
docker-compose up -d cloud-envoy

# Wait a moment for the container to start
sleep 5

# Get the IP address of the cloud-envoy container
echo "Getting cloud-envoy IP address..."

# Get the container ID first
CONTAINER_ID=$(docker ps --format "{{.ID}} {{.Names}}" | grep cloud-envoy | awk '{print $1}')

if [ -z "$CONTAINER_ID" ]; then
    echo "Error: Could not find cloud-envoy container"
    exit 1
fi

echo "Found container ID: $CONTAINER_ID"

# Get the IP address from the specific network
CLOUD_ENVOY_IP=$(docker inspect -f '{{range .NetworkSettings.Networks}}{{if eq .NetworkID "reverse_connection_socket_interface_envoy-network"}}{{.IPAddress}}{{end}}{{end}}' $CONTAINER_ID 2>/dev/null)

# If no IP found, try the general method
if [ -z "$CLOUD_ENVOY_IP" ] || [ "$CLOUD_ENVOY_IP" = "<no value>" ]; then
    echo "Trying general method to get IP..."
    CLOUD_ENVOY_IP=$(docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' $CONTAINER_ID 2>/dev/null)
fi

# If still no IP, show debug info and exit
if [ -z "$CLOUD_ENVOY_IP" ] || [ "$CLOUD_ENVOY_IP" = "<no value>" ]; then
    echo "Error: Could not get cloud-envoy IP address"
    echo "Container details:"
    docker inspect $CONTAINER_ID 2>/dev/null | grep -A 20 "NetworkSettings" || echo "Container not found"
    exit 1
fi

echo "Cloud-envoy IP address: $CLOUD_ENVOY_IP"

# Update the on-prem-envoy configuration with the IP address
echo "Updating on-prem-envoy configuration with IP: $CLOUD_ENVOY_IP"
sed -i "s/address: cloud-envoy/address: $CLOUD_ENVOY_IP/" on-prem-envoy-custom-resolver.yaml

echo "Configuration updated. You can now start the full stack with:"
echo "docker-compose up"
