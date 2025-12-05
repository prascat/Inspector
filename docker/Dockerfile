# Docker image for training PatchCore with Anomalib and exporting ONNX/OpenVINO IR
# Uses Ubuntu 22.04 + Python 3.10

FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive

# minimal system deps
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3.10 python3.10-venv python3-pip python3.10-dev git wget curl build-essential ca-certificates libgl1-mesa-glx libglib2.0-0 \
    && rm -rf /var/lib/apt/lists/*

# Ensure pip is up to date
RUN python3 -m pip install --upgrade pip setuptools wheel

# Set workdir
WORKDIR /workspace

# Install PyTorch CPU version first
RUN pip install --no-cache-dir torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cpu

# Install anomalib and dependencies
RUN pip install --no-cache-dir \
    anomalib \
    opencv-python \
    matplotlib \
    scikit-learn \
    scikit-image \
    pandas \
    tqdm \
    albumentations \
    timm \
    imgaug \
    onnx \
    onnxscript

# Install OpenVINO dev tools
RUN pip install --no-cache-dir openvino-dev

# Copy the project (only what we need) into the image
# We intentionally copy the whole repo so training/export scripts are available
COPY . /workspace

# Default entrypoint uses train_patchcore_anomalib.py directly
ENTRYPOINT ["python3", "/workspace/train_patchcore_anomalib.py"]

# helpful labels
LABEL org.opencontainers.image.description="Train PatchCore (anomalib) and export ONNX/OpenVINO IR for C++ inference"
LABEL maintainer="you@example.com"
