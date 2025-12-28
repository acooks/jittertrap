#!/bin/bash
# run-cluster2.sh - Run Cluster 2: WAN/Internet Video with Network Impairments
#
# Total: ~2304 experiments, ~7.7 hours
#
# This script runs all presets for Cluster 2 sequentially, organizing
# results into a dedicated directory with clear naming.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXPERIMENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUTPUT_DIR="$EXPERIMENT_DIR/results/cluster2"

mkdir -p "$OUTPUT_DIR"

echo "============================================================"
echo "CLUSTER 2: WAN/Internet Video with Network Impairments"
echo "============================================================"
echo "Output directory: $OUTPUT_DIR"
echo "Started at: $(date)"
echo ""
echo "Presets to run:"
echo "  1. wan-loss-core       ( 432 experiments, ~1.4 hours)"
echo "  2. wan-bdp             (1008 experiments, ~3.4 hours)"
echo "  3. starlink-excellent  ( 162 experiments, ~0.5 hours)"
echo "  4. starlink-normal     ( 162 experiments, ~0.5 hours)"
echo "  5. starlink-degraded   ( 162 experiments, ~0.5 hours)"
echo "  6. starlink-severe     ( 162 experiments, ~0.5 hours)"
echo "  7. lte-profiles        ( 216 experiments, ~0.7 hours)"
echo "  TOTAL:                  2304 experiments, ~7.7 hours"
echo ""
echo "============================================================"

# Log file for the entire run
LOGFILE="$OUTPUT_DIR/cluster2.log"
exec > >(tee -a "$LOGFILE") 2>&1

run_preset() {
    local preset="$1"
    local start_time=$(date +%s)
    echo ""
    echo "============================================================"
    echo "[$preset] Starting at $(date)"
    echo "============================================================"

    "$SCRIPT_DIR/run-sweep.sh" --preset "$preset" --output-dir "$OUTPUT_DIR"

    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    echo "[$preset] Completed in $((duration / 60)) minutes"
}

# Run all Cluster 2 presets
CLUSTER_START=$(date +%s)

run_preset "wan-loss-core"
run_preset "wan-bdp"
run_preset "starlink-excellent"
run_preset "starlink-normal"
run_preset "starlink-degraded"
run_preset "starlink-severe"
run_preset "lte-profiles"

CLUSTER_END=$(date +%s)
CLUSTER_DURATION=$((CLUSTER_END - CLUSTER_START))

echo ""
echo "============================================================"
echo "CLUSTER 2 COMPLETE"
echo "============================================================"
echo "Finished at: $(date)"
echo "Total duration: $((CLUSTER_DURATION / 3600)) hours $((CLUSTER_DURATION % 3600 / 60)) minutes"
echo "Results in: $OUTPUT_DIR"
echo ""
echo "CSV files:"
ls -la "$OUTPUT_DIR"/*.csv
echo ""
echo "To analyze results:"
echo "  python3 $EXPERIMENT_DIR/analysis/analyze.py $OUTPUT_DIR/*.csv -o $OUTPUT_DIR/"
echo "============================================================"
