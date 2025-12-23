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

# Install PyTorch CPU FIRST (prevents GPU version download)
# pip 캐시 사용 (--no-cache-dir 제거)
RUN pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cpu

# Copy requirements and install remaining packages
COPY requirements.txt /workspace/requirements.txt
RUN pip install -r requirements.txt

# Copy the training scripts to /workspace
COPY train_patchcore_anomalib.py /workspace/train_patchcore_anomalib.py
COPY run_train.sh /workspace/run_train.sh
COPY convert_patchcore_to_ir.py /workspace/convert_patchcore_to_ir.py
RUN chmod +x /workspace/run_train.sh

# Default entrypoint uses train_patchcore_anomalib.py directly
ENTRYPOINT ["python3", "/workspace/train_patchcore_anomalib.py"]

# helpful labels
LABEL org.opencontainers.image.description="Train PatchCore (anomalib) and export ONNX/OpenVINO IR for C++ inference"
LABEL maintainer=""
