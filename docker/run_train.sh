#!/usr/bin/env bash
set -euo pipefail

# Usage: run_train.sh <data_dir> <output_dir>
# Example: 
#   run_train.sh /workspace/data /workspace/output

DATA_DIR=${1:-/workspace/data}
OUTPUT_DIR=${2:-/workspace/output}

echo "🔧 Starting container training run"
echo "  data: ${DATA_DIR}"
echo "  output: ${OUTPUT_DIR}"

mkdir -p "${OUTPUT_DIR}"

# Run training (PatchCore with anomalib) - 입력 이미지 크기 그대로 사용
echo ""
echo "🚀 Training PatchCore (anomalib)"
python3 /workspace/docker/train_patchcore_anomalib.py \
  --data-dir "${DATA_DIR}" \
  --output "${OUTPUT_DIR}"

# Summarize outputs
echo ""
echo "📁 Output files:"
find "${OUTPUT_DIR}" -type f \( -name "*.xml" -o -name "*.bin" -o -name "*.onnx" -o -name "*.pt" -o -name "*.txt" \) | head -20

echo ""
echo "🎉 Done. Check ${OUTPUT_DIR} for model files."
