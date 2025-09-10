#!/usr/bin/env python3
"""
학습된 PyTorch 모델을 ONNX 형식으로 변환하는 스크립트
"""

import os
import sys
import torch
import numpy as np
import argparse
from anomalib.models import Patchcore

def convert_checkpoint_to_onnx(ckpt_path, onnx_path, input_size=(3, 256, 256)):
    """체크포인트 파일을 로드하여 ONNX로 변환"""
    try:
        print(f"Loading checkpoint from: {ckpt_path}", file=sys.stderr)

        # 체크포인트에서 모델 로드
        model = Patchcore.load_from_checkpoint(ckpt_path)
        device = "cuda" if torch.cuda.is_available() else "cpu"
        model.to(device)
        model.eval()

        print(f"Model loaded successfully. Device: {device}", file=sys.stderr)

        # 더미 입력 생성 (배치 크기 1)
        dummy_input = torch.randn(1, *input_size).to(device)

        # ONNX로 변환
        print(f"Converting to ONNX: {onnx_path}", file=sys.stderr)

        torch.onnx.export(
            model,
            dummy_input,
            onnx_path,
            export_params=True,
            opset_version=11,
            do_constant_folding=True,
            input_names=['input'],
            output_names=['output'],
            dynamic_axes={'input': {0: 'batch_size'}, 'output': {0: 'batch_size'}}
        )

        print(f"✅ ONNX model saved to: {onnx_path}", file=sys.stderr)
        return True

    except Exception as e:
        print(f"❌ ONNX conversion failed: {str(e)}", file=sys.stderr)
        import traceback
        traceback.print_exc(file=sys.stderr)
        return False

def main():
    parser = argparse.ArgumentParser(description='Convert PyTorch checkpoint to ONNX')
    parser.add_argument('--ckpt_path', required=True, help='Path to PyTorch checkpoint file')
    parser.add_argument('--onnx_path', required=True, help='Path to save ONNX model')
    parser.add_argument('--input_size', default='3,256,256', help='Input size (channels,height,width)')

    args = parser.parse_args()

    # 입력 크기 파싱
    try:
        input_size = tuple(map(int, args.input_size.split(',')))
    except:
        input_size = (3, 256, 256)

    # 파일 존재 확인
    if not os.path.exists(args.ckpt_path):
        print(f"❌ Checkpoint file not found: {args.ckpt_path}", file=sys.stderr)
        sys.exit(1)

    # 변환 실행
    success = convert_checkpoint_to_onnx(args.ckpt_path, args.onnx_path, input_size)

    if success:
        print("✅ ONNX conversion completed successfully", file=sys.stderr)
        sys.exit(0)
    else:
        print("❌ ONNX conversion failed", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
