# Reverse Connection Socket Interface - Process Mode

This directory contains configuration files and scripts to run the reverse connection socket interface example using Envoy processes instead of Docker containers.

## Port Configuration

To avoid conflicts between on-prem and cloud Envoy instances, the following ports are used:

| Service | Port | Description |
|---------|------|-------------|
| Backend Service | 8080 | Simulated on-prem backend service |
| On-prem Admin | 8888 | On-prem Envoy admin interface |
| Cloud Admin | 8889 | Cloud Envoy admin interface |
| On-prem Reverse Conn API | 9000 | On-prem reverse connection API |
| Cloud Reverse Conn API | 9001 | Cloud reverse connection API |
| On-prem Ingress | 6060 | On-prem HTTP ingress listener |
| Cloud Egress | 8085 | Cloud HTTP egress listener |

## Prerequisites

1. **Build Envoy**: First build Envoy with your custom extensions:
   ```bash
   cd /home/basundhara.c/envoy_upstream_final
   bazel build //source/exe:envoy-static
   ```

2. **Add Envoy to PATH**:
   ```bash
   export PATH=$PATH:$(pwd)/bazel-bin/source/exe
   ```

3. **Install nginx** (for backend service simulation):
   ```bash
   # Ubuntu/Debian
   sudo apt-get install nginx
   
   # CentOS/RHEL
   sudo yum install nginx
   ```

## Quick Start

### Option 1: Start All Services at Once
```bash
cd examples/reverse_connection_socket_interface/process_configs
chmod +x *.sh
./start-all.sh
```

### Option 2: Start Services Individually

1. **Start backend service**:
   ```bash
   ./start-backend.sh
   ```

2. **Start cloud Envoy** (in a new terminal):
   ```bash
   ./start-cloud-envoy.sh
   ```

3. **Start on-prem Envoy** (in a new terminal):
   ```bash
   ./start-on-prem-envoy.sh
   ```

## Testing

### Test Backend Service
```bash
curl http://localhost:8080/on_prem_service
curl http://localhost:8080/health
```

### Test On-prem Ingress
```bash
curl http://localhost:6060/on_prem_service
```

### Test Cloud Egress (Reverse Connection)
```bash
curl http://localhost:8085/on_prem_service
```

### Check Admin Interfaces
- On-prem admin: http://localhost:8888
- Cloud admin: http://localhost:8889

## Debugging

All Envoy processes are started with trace-level logging and component-specific debug logging for:
- upstream
- http
- http2
- grpc
- router
- connection

Logs will show detailed information about:
- Reverse connection establishment
- gRPC handshake process
- HTTP/2 protocol usage
- Request routing through reverse connections

## Stopping Services

### Stop All Services
```bash
./stop-all.sh
```

### Stop Individual Services
```bash
# Stop backend
pkill -f 'nginx.*backend-nginx'

# Stop on-prem Envoy
pkill -f 'envoy.*on-prem-envoy-process'

# Stop cloud Envoy
pkill -f 'envoy.*cloud-envoy-process'
```

## Configuration Files

- `on-prem-envoy-process.yaml`: On-prem Envoy configuration
- `cloud-envoy-process.yaml`: Cloud Envoy configuration
- `backend-service.yaml`: nginx configuration for backend service
- `start-*.sh`: Individual service startup scripts
- `start-all.sh`: Master startup script
- `stop-all.sh`: Cleanup script

## Troubleshooting

1. **Port conflicts**: Check if ports are already in use with `lsof -i :PORT`
2. **Envoy not found**: Ensure Envoy is built and in PATH
3. **Backend service issues**: Check nginx installation and configuration
4. **Reverse connection not working**: Check logs for gRPC handshake errors

## Differences from Docker Mode

- Services run as local processes instead of containers
- Port mappings are direct (no Docker port forwarding)
- Backend service uses nginx instead of Docker container
- Process management via shell scripts instead of docker-compose
