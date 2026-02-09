#!/bin/bash
# Results logger for x265 builds - extracts and saves key metrics

set -euo pipefail

RESULTS_DIR="build_results"
TIMESTAMP=$(date -u +"%Y-%m-%d_%H-%M-%S")
BUILD_ID="${BITBUCKET_BUILD_NUMBER:-manual}"
COMMIT="${BITBUCKET_COMMIT:-$(git rev-parse HEAD 2>/dev/null || echo 'unknown')}"
BRANCH="${BITBUCKET_BRANCH:-$(git branch --show-current 2>/dev/null || echo 'unknown')}"

mkdir -p "$RESULTS_DIR"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "=================================================="
echo "Build Results Logger"
echo "=================================================="
echo "Build ID: $BUILD_ID"
echo "Commit: $COMMIT"
echo "Branch: $BRANCH"
echo "Timestamp: $TIMESTAMP"
echo ""

# Initialize results file
RESULTS_FILE="$RESULTS_DIR/build_${BUILD_ID}_${TIMESTAMP}.json"

cat > "$RESULTS_FILE" <<EOF
{
  "build_id": "$BUILD_ID",
  "commit": "$COMMIT",
  "branch": "$BRANCH",
  "timestamp": "$TIMESTAMP",
  "tests": {},
  "encoding_results": [],
  "performance": {},
  "summary": {}
}
EOF

# Function to extract FPS from x265 log
extract_fps() {
    local log_file=$1
    local test_name=$2
    
    if [ ! -f "$log_file" ]; then
        echo "0"
        return
    fi
    
    # Extract fps from x265 output: "encoded ... frames in ...s (...fps, ...kb/s)"
    local fps=$(grep -oP ', \K[0-9.]+(?= fps)' "$log_file" | tail -1 || echo "0")
    echo "$fps"
}

# Function to extract encoding time
extract_time() {
    local log_file=$1
    
    if [ ! -f "$log_file" ]; then
        echo "0"
        return
    fi
    
    # Extract time: "encoded ... frames in ...s"
    local time=$(grep -oP 'encoded.*in \K[0-9.]+(?=s)' "$log_file" | tail -1 || echo "0")
    echo "$time"
}

# Function to extract bitrate
extract_bitrate() {
    local log_file=$1
    
    if [ ! -f "$log_file" ]; then
        echo "0"
        return
    fi
    
    # Extract bitrate: "... kb/s"
    local bitrate=$(grep -oP ', \K[0-9.]+(?= kb/s)' "$log_file" | tail -1 || echo "0")
    echo "$bitrate"
}

# Function to log encoding test result
log_encoding_test() {
    local test_name=$1
    local log_file=$2
    local status=${3:-"unknown"}
    
    echo -e "${BLUE}Processing: $test_name${NC}"
    
    local fps=$(extract_fps "$log_file" "$test_name")
    local time=$(extract_time "$log_file")
    local bitrate=$(extract_bitrate "$log_file")
    local frames=$(grep -oP 'encoded \K[0-9]+(?= frames)' "$log_file" | tail -1 || echo "0")
    
    echo "  FPS: $fps"
    echo "  Time: ${time}s"
    echo "  Bitrate: ${bitrate} kb/s"
    echo "  Frames: $frames"
    
    # Append to results file using jq if available, otherwise manually
    if command -v jq &> /dev/null; then
        local tmp_file=$(mktemp)
        jq --arg name "$test_name" \
           --arg fps "$fps" \
           --arg time "$time" \
           --arg bitrate "$bitrate" \
           --arg frames "$frames" \
           --arg status "$status" \
           '.encoding_results += [{
               "test": $name,
               "fps": ($fps | tonumber),
               "encoding_time": ($time | tonumber),
               "bitrate": ($bitrate | tonumber),
               "frames": ($frames | tonumber),
               "status": $status
           }]' "$RESULTS_FILE" > "$tmp_file"
        mv "$tmp_file" "$RESULTS_FILE"
    fi
}

# Function to generate summary report
generate_summary() {
    echo ""
    echo "╔══════════════════════════════════════════════════════════════════════╗"
    echo -e "║${GREEN}                     BUILD RESULTS SUMMARY                             ${NC}║"
    echo "╚══════════════════════════════════════════════════════════════════════╝"
    echo ""
    
    if command -v jq &> /dev/null; then
        # Calculate average FPS
        local avg_fps=$(jq '[.encoding_results[].fps] | add / length' "$RESULTS_FILE" 2>/dev/null || echo "0")
        local total_time=$(jq '[.encoding_results[].encoding_time] | add' "$RESULTS_FILE" 2>/dev/null || echo "0")
        local test_count=$(jq '[.encoding_results] | length' "$RESULTS_FILE" 2>/dev/null || echo "0")
        
        # Update summary in JSON
        local tmp_file=$(mktemp)
        jq --arg avg_fps "$avg_fps" \
           --arg total_time "$total_time" \
           --arg test_count "$test_count" \
           '.summary = {
               "total_tests": ($test_count | tonumber),
               "average_fps": ($avg_fps | tonumber),
               "total_encoding_time": ($total_time | tonumber)
           }' "$RESULTS_FILE" > "$tmp_file"
        mv "$tmp_file" "$RESULTS_FILE"
        
        # Display beautiful results table
        echo "┌──────────────────────────┬──────────┬──────────┬──────────────┬────────┐"
        echo "│ Test                     │ FPS      │ Time     │ Bitrate      │ Status │"
        echo "├──────────────────────────┼──────────┼──────────┼──────────────┼────────┤"
        
        jq -r '.encoding_results[] | 
            [.test, (.fps|tostring + " fps"), (.encoding_time|tostring + "s"), (.bitrate|tostring + " kb/s"), .status] | 
            @tsv' "$RESULTS_FILE" 2>/dev/null | while IFS=$'\t' read -r test fps time bitrate status; do
            # Truncate test name if too long
            test_short=$(printf "%-24s" "$test" | cut -c1-24)
            fps_fmt=$(printf "%-8s" "$fps")
            time_fmt=$(printf "%-8s" "$time")
            bitrate_fmt=$(printf "%-12s" "$bitrate")
            status_icon=$([ "$status" = "passed" ] && echo "✅" || echo "❌")
            printf "│ %-24s │ %-8s │ %-8s │ %-12s │ %-6s │\n" "$test_short" "$fps_fmt" "$time_fmt" "$bitrate_fmt" "$status_icon"
        done
        
        echo "└──────────────────────────┴──────────┴──────────┴──────────────┴────────┘"
        echo ""
        echo "Performance Summary:"
        echo "  • Total Tests:           $test_count"
        echo "  • Average FPS:           $(printf '%.2f' $avg_fps) fps"
        echo "  • Total Encoding Time:   $(printf '%.2f' $total_time)s"
    else
        echo "Install 'jq' for detailed JSON analysis"
        echo "Results saved to: $RESULTS_FILE"
    fi
    
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
}

# Create human-readable summary
create_text_summary() {
    local summary_file="$RESULTS_DIR/build_${BUILD_ID}_${TIMESTAMP}.txt"
    
    cat > "$summary_file" <<SUMMARY_EOF
╔══════════════════════════════════════════════════════════════════════╗
║                     X265 BUILD RESULTS SUMMARY                        ║
╚══════════════════════════════════════════════════════════════════════╝

Build Information:
  Build ID:     $BUILD_ID
  Commit:       $COMMIT
  Branch:       $BRANCH
  Timestamp:    $TIMESTAMP

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

ENCODING TEST RESULTS:

SUMMARY_EOF

    if command -v jq &> /dev/null && [ -f "$RESULTS_FILE" ]; then
        jq -r '.encoding_results[] | 
            "  \(.test):\n" +
            "    FPS:          \(.fps)\n" +
            "    Time:         \(.encoding_time)s\n" +
            "    Bitrate:      \(.bitrate) kb/s\n" +
            "    Frames:       \(.frames)\n" +
            "    Status:       \(.status)\n"' "$RESULTS_FILE" >> "$summary_file" 2>/dev/null || echo "  No results available" >> "$summary_file"
        
        echo "" >> "$summary_file"
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" >> "$summary_file"
        echo "" >> "$summary_file"
        echo "PERFORMANCE SUMMARY:" >> "$summary_file"
        echo "" >> "$summary_file"
        
        jq -r '.summary | 
            "  Total Tests:           \(.total_tests)\n" +
            "  Average FPS:           \(.average_fps)\n" +
            "  Total Encoding Time:   \(.total_encoding_time)s"' "$RESULTS_FILE" >> "$summary_file" 2>/dev/null || echo "  No summary available" >> "$summary_file"
    else
        echo "  Results file not found or jq not installed" >> "$summary_file"
    fi
    
    echo "" >> "$summary_file"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" >> "$summary_file"
    echo "" >> "$summary_file"
    echo "Results saved to:" >> "$summary_file"
    echo "  JSON: $RESULTS_FILE" >> "$summary_file"
    echo "  Text: $summary_file" >> "$summary_file"
    
    echo -e "\n${GREEN}✓ Text summary created: $summary_file${NC}"
}

# Create historical comparison
create_historical_report() {
    local history_file="$RESULTS_DIR/build_history.csv"
    
    # Create CSV header if file doesn't exist
    if [ ! -f "$history_file" ]; then
        echo "build_id,timestamp,commit,branch,avg_fps,total_time,test_count" > "$history_file"
    fi
    
    if command -v jq &> /dev/null && [ -f "$RESULTS_FILE" ]; then
        local avg_fps=$(jq -r '.summary.average_fps // 0' "$RESULTS_FILE")
        local total_time=$(jq -r '.summary.total_encoding_time // 0' "$RESULTS_FILE")
        local test_count=$(jq -r '.summary.total_tests // 0' "$RESULTS_FILE")
        
        echo "$BUILD_ID,$TIMESTAMP,$COMMIT,$BRANCH,$avg_fps,$total_time,$test_count" >> "$history_file"
        echo -e "${GREEN}✓ Added to build history: $history_file${NC}"
    fi
}

# Export functions for use in pipeline
export -f extract_fps
export -f extract_time
export -f extract_bitrate
export -f log_encoding_test
export -f generate_summary
export -f create_text_summary
export -f create_historical_report

# If running standalone, show usage
if [ "${BASH_SOURCE[0]}" == "${0}" ]; then
    echo "Results Logger v1.0"
    echo ""
    echo "Usage: source this file in your pipeline, then call:"
    echo "  log_encoding_test <test_name> <log_file> <status>"
    echo "  generate_summary"
    echo "  create_text_summary"
    echo "  create_historical_report"
    echo ""
    echo "Example:"
    echo "  source .ci/results_logger.sh"
    echo "  log_encoding_test 'Test1-Fast' 'test1.log' 'passed'"
    echo "  generate_summary"
fi
