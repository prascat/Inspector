#!/usr/bin/env bash
set -euo pipefail

# Helper script to run patchcore-train container with a dataset mounted or copied.
# Usage:
#   docker/docker_run_with_data.sh <host_data_dir> <host_output_dir> [--copy] [--image patchcore-train]
# Examples:
#   # Preferred (mount host data read-only into container):
#   ./docker/docker_run_with_data.sh ./yolo_copper_segmentation/crimp ./host_out
#
#   # If you want to copy the dataset into the container's workspace (not typically necessary):
#   ./docker/docker_run_with_data.sh ./yolo_copper_segmentation/crimp ./host_out --copy

HOST_DATA_DIR=${1:-}
HOST_OUTPUT_DIR=${2:-}
PATTERN_NAME=${3:-model}
COPY_MODE=false
IMAGE=${4:-patchcore-trainer}

if [[ -z "${HOST_DATA_DIR}" || -z "${HOST_OUTPUT_DIR}" ]]; then
  echo "Usage: $(basename "$0") <host_data_dir> <host_output_dir> [--copy] [--image patchcore-train]"
  exit 1
fi

if [[ "${3:-}" == "--copy" ]]; then
  COPY_MODE=true
fi

# Normalize paths
HOST_DATA_DIR=$(cd "${HOST_DATA_DIR}" && pwd)
mkdir -p "${HOST_OUTPUT_DIR}"
HOST_OUTPUT_DIR=$(cd "${HOST_OUTPUT_DIR}" && pwd)

echo "Running training container"
echo "  dataset: ${HOST_DATA_DIR}"
echo "  output: ${HOST_OUTPUT_DIR}"
echo "  image: ${IMAGE}"
echo "  copy_mode: ${COPY_MODE}"

if [[ "${COPY_MODE}" == "false" ]]; then
  # Mount dataset read-only (recommended) and mount output so artifacts are accessible on host
  docker run --rm \
    -v "${HOST_DATA_DIR}:/workspace/data:ro" \
    -v "${HOST_OUTPUT_DIR}:/workspace/output" \
    ${IMAGE} --data-dir /workspace/data --output /workspace/output --pattern-name "${PATTERN_NAME}"
else
  # Copy mode: create a temporary container, copy dataset into container FS, then run training inside it
  TMP_NAME="patchcore_tmp_$RANDOM"
  echo "Copying data into ephemeral container '${TMP_NAME}' and running training (data will NOT be persisted unless output is mounted)"
  docker create --name ${TMP_NAME} ${IMAGE} /bin/bash -c 'sleep infinity'
  docker cp "${HOST_DATA_DIR}" ${TMP_NAME}:/workspace/data
  docker start ${TMP_NAME}
  docker exec ${TMP_NAME} /usr/local/bin/run_train.sh /workspace/data /workspace/output
  docker stop ${TMP_NAME}
  docker rm ${TMP_NAME}

  echo "Note: Since data was copied into a temporary container, you must mount a host output dir to persist results."
fi

echo "Done. Check ${HOST_OUTPUT_DIR} for exported artifacts (model.ckpt, model_raw.onnx, model_raw.xml, model_raw.bin)"
