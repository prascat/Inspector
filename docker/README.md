# Docker training image for PatchCore (anomalib) → ONNX → OpenVINO IR

This container runs PatchCore training using anomalib (Python) and exports the raw ONNX model + OpenVINO IR (.xml + .bin) which C++ inference code can use.

## Build

From workspace root (where this repository lives):

```bash
cd /path/to/your/repo
docker build -t patchcore-train -f docker/Dockerfile .
```

## Run training & export

Mount your data directory and an output directory so generated weights can be retrieved on host:

```bash
# Example: train using 'crimp' dir inside the repo and save outputs to ./host_out
docker run --rm -it \
  -v "$(pwd)/yolo_copper_segmentation/crimp:/workspace/data:ro" \
  -v "$(pwd)/host_out:/workspace/output" \
  patchcore-train /workspace/data /workspace/output
```


## Host helper script

A convenience script is included at `docker/docker_run_with_data.sh` to make it easy to run the container with a dataset from your host. You can either mount the host data directory (recommended, read-only) or copy the dataset into a temporary container (not usually required).

Examples:

Mount mode (recommended):
```bash
./docker/docker_run_with_data.sh ./yolo_copper_segmentation/crimp ./host_out
```

Copy mode (copies into ephemeral container; use only if you explicitly need data copied):
```bash
./docker/docker_run_with_data.sh ./yolo_copper_segmentation/crimp ./host_out --copy
```

The script will run training inside the container and write outputs into the host output directory you provide so files persist after the container exits.

- If you want FP16 IR for smaller size / faster inference on N100, you can convert with OpenVINO tools after export.

If you'd like, I can also add a small convenience script on the host to pull generated .xml/.bin into a specific C++ inference folder or automate FP16 conversion.
