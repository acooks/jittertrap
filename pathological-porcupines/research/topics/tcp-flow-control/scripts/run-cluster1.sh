#!/bin/bash
# run-cluster1.sh - Run Cluster 1: LAN Video with Stuttering Receiver
#
# Total: ~2592 experiments, ~8.6 hours
#
# This script runs all presets for Cluster 1 sequentially, organizing
# results into a dedicated directory with clear naming.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXPERIMENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUTPUT_DIR="$EXPERIMENT_DIR/results/cluster1"

mkdir -p "$OUTPUT_DIR"

echo "============================================================"
echo "CLUSTER 1: LAN Video with Stuttering Receiver"
echo "============================================================"
echo "Output directory: $OUTPUT_DIR"
echo "Started at: $(date)"
echo ""
echo "Presets to run:"
echo "  1. lan-starvation      (1728 experiments, ~5.8 hours)"
echo "  2. lan-starvation-fine ( 480 experiments, ~1.6 hours)"
echo "  3. lan-multi-bitrate   ( 384 experiments, ~1.3 hours)"
echo "  TOTAL:                  2592 experiments, ~8.6 hours"
echo ""
echo "============================================================"

# Log file for the entire run
LOGFILE="$OUTPUT_DIR/cluster1.log"
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

# Run all Cluster 1 presets
CLUSTER_START=$(date +%s)

run_preset "lan-starvation"
run_preset "lan-starvation-fine"
run_preset "lan-multi-bitrate"

CLUSTER_END=$(date +%s)
CLUSTER_DURATION=$((CLUSTER_END - CLUSTER_START))

echo ""
echo "============================================================"
echo "CLUSTER 1 COMPLETE"
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
