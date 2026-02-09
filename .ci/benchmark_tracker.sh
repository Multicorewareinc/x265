#!/bin/bash
# Performance benchmark tracker for x265
# Detects performance regressions across builds

set -euo pipefail

BENCHMARK_FILE="benchmark_results.json"
REGRESSION_THRESHOLD=10  # 10% regression threshold

log_benchmark() {
    local test_name=$1
    local metric=$2
    local value=$3
    local timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
    local git_commit=${BITBUCKET_COMMIT:-"unknown"}
    
    echo "Recording benchmark: $test_name - $metric = $value"
    
    # Create JSON entry
    cat >> "$BENCHMARK_FILE" <<EOF
{
  "timestamp": "$timestamp",
  "commit": "$git_commit",
  "test": "$test_name",
  "metric": "$metric",
  "value": $value
}
EOF
}

check_regression() {
    local test_name=$1
    local current_value=$2
    local baseline_value=$3
    
    if [ "$baseline_value" == "0" ]; then
        echo "No baseline for $test_name, setting baseline to $current_value"
        return 0
    fi
    
    local change=$(echo "scale=2; (($current_value - $baseline_value) / $baseline_value) * 100" | bc)
    local abs_change=$(echo "$change" | tr -d '-')
    
    echo "Performance change for $test_name: ${change}%"
    
    if (( $(echo "$abs_change > $REGRESSION_THRESHOLD" | bc -l) )); then
        if (( $(echo "$change > 0" | bc -l) )); then
            echo "⚠️  REGRESSION DETECTED: $test_name is ${change}% slower!"
            return 1
        else
            echo "✓ Performance improved by ${abs_change}%"
        fi
    fi
    
    return 0
}

extract_encoding_time() {
    local log_file=$1
    # Extract encoding time from x265 output
    # Look for "encoded ... frames in ...s"
    grep -oP 'encoded.*in \K[0-9.]+(?=s)' "$log_file" || echo "0"
}

extract_fps() {
    local log_file=$1
    # Extract fps from x265 output
    grep -oP ', \K[0-9.]+(?= fps)' "$log_file" | tail -1 || echo "0"
}

# Initialize benchmark file
mkdir -p "$(dirname "$BENCHMARK_FILE")"
echo "[]" > "$BENCHMARK_FILE"

echo "Benchmark tracker initialized"
echo "Regression threshold: ${REGRESSION_THRESHOLD}%"
