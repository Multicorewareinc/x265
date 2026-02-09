#!/bin/bash
# Test execution with retry logic for flaky tests

set -uo pipefail

MAX_RETRIES=3
RETRY_DELAY=5

run_with_retry() {
    local test_name=$1
    shift
    local cmd="$@"
    local attempt=1
    local exit_code=0
    
    echo "=================================================="
    echo "Running: $test_name"
    echo "Command: $cmd"
    echo "=================================================="
    
    while [ $attempt -le $MAX_RETRIES ]; do
        echo "Attempt $attempt/$MAX_RETRIES..."
        
        if eval "$cmd"; then
            echo "✓ $test_name PASSED on attempt $attempt"
            return 0
        else
            exit_code=$?
            echo "✗ $test_name FAILED on attempt $attempt (exit code: $exit_code)"
            
            if [ $attempt -lt $MAX_RETRIES ]; then
                echo "Retrying in ${RETRY_DELAY}s..."
                sleep $RETRY_DELAY
                attempt=$((attempt + 1))
            else
                echo "❌ $test_name FAILED after $MAX_RETRIES attempts"
                return $exit_code
            fi
        fi
    done
}

# Export function for use in pipeline
export -f run_with_retry
