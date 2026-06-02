#!/bin/bash
# Run Buccaneer tests in a Docker container with memory limits
# This prevents OOM conditions from affecting the host

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUCCANEER_DIR="$(dirname "$SCRIPT_DIR")"

# Memory limit (256MB should be plenty for these tests)
MEMORY_LIMIT="256m"

# CPU limit (1 CPU)
CPU_LIMIT="1"

# Timeout for entire container (60 seconds)
CONTAINER_TIMEOUT=60

echo "=== Buccaneer Test Runner (Containerized) ==="
echo "Memory limit: $MEMORY_LIMIT"
echo "CPU limit: $CPU_LIMIT"
echo "Container timeout: ${CONTAINER_TIMEOUT}s"
echo ""

# Build the Docker image
echo "Building test container..."
docker build -t buccaneer-tests -f "$SCRIPT_DIR/Dockerfile" "$BUCCANEER_DIR"

echo ""
echo "Running tests..."
echo "----------------------------------------"

# Run with memory and CPU limits, and a timeout
docker run --rm \
    --memory="$MEMORY_LIMIT" \
    --memory-swap="$MEMORY_LIMIT" \
    --cpus="$CPU_LIMIT" \
    --pids-limit=50 \
    --name buccaneer-test-run \
    buccaneer-tests

echo "----------------------------------------"
echo "Tests completed successfully!"
