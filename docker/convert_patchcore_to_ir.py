#!/usr/bin/env python3
"""
PatchCore PyTorch 모델을 OpenVINO IR로 변환하는 스크립트
Usage: python convert_patchcore_to_ir.py <patchcore_model.pt 경로>
"""

import sys
import torch
import torch.nn as nn
from pathlib import Path
import openvino as ov
import numpy as np

class PatchCoreRawWrapper(nn.Module):
    """Wrapper to export only the raw PatchCore model without post-processing"""
    def __init__(self, patchcore_model):
        super().__init__()
        self.model = patchcore_model
    
    def forward(self, x):
        output = self.model(x)
        return output.anomaly_map, output.pred_score

def convert_to_openvino(pt_path: Path, image_size: tuple = (224, 224)):
    """Convert PatchCore PyTorch model to OpenVINO IR"""
    
    print(f"\n🔄 Loading PyTorch model: {pt_path}")
    model = torch.load(pt_path, map_location='cpu')
    
    # Get the internal model (without post-processing)
    raw_model = model.model
    raw_model.eval()
    
    # Wrap it
    wrapper = PatchCoreRawWrapper(raw_model)
    wrapper.eval()
    
    # Create dummy input
    dummy_input = torch.randn(1, 3, image_size[0], image_size[1])
    
    print(f"🔄 Converting to OpenVINO IR...")
    
    # Convert directly from PyTorch to OpenVINO
    ov_model = ov.convert_model(
        wrapper,
        example_input=dummy_input,
        input=[('input', [1, 3, image_size[0], image_size[1]])]
    )
    
    # Save as IR (.xml + .bin)
    output_dir = pt_path.parent
    ir_path = output_dir / "model_raw.xml"
    ov.save_model(ov_model, str(ir_path))
    print(f"✅ OpenVINO IR: {ir_path}")
    print(f"✅ OpenVINO BIN: {output_dir / 'model_raw.bin'}")
    
    # Test inference
    core = ov.Core()
    compiled = core.compile_model(ov_model, 'CPU')
    test_input = np.random.randn(1, 3, image_size[0], image_size[1]).astype(np.float32)
    results = compiled(test_input)
    print(f"✅ OpenVINO test inference passed")
    
    # Copy to pattern name
    pattern_name = output_dir.name
    pattern_ir_xml = output_dir / f"{pattern_name}.xml"
    pattern_ir_bin = output_dir / f"{pattern_name}.bin"
    
    import shutil
    shutil.copy(ir_path, pattern_ir_xml)
    shutil.copy(output_dir / "model_raw.bin", pattern_ir_bin)
    
    print(f"✅ Pattern IR: {pattern_ir_xml}")
    print(f"✅ Pattern BIN: {pattern_ir_bin}")
    
    return ir_path

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python convert_patchcore_to_ir.py <patchcore_model.pt 경로>")
        sys.exit(1)
    
    pt_path = Path(sys.argv[1])
    if not pt_path.exists():
        print(f"❌ File not found: {pt_path}")
        sys.exit(1)
    
    convert_to_openvino(pt_path)
    print("\n✅ Conversion complete!")
