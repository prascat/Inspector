"""PatchCore Training using Anomalib

anomalib 라이브러리를 사용한 정교한 PatchCore 학습

사용법:
    python train_patchcore_anomalib.py --data-dir crimp --output patchcore_model/anomalib_patchcore
    python train_patchcore_anomalib.py --data-dir patchcore_model/temp_dataset --output patchcore_model/anomalib_patchcore
    
    # ROI 모드: 첫 이미지에서 ROI 선택 후 템플릿 매칭으로 전체 이미지 crop 후 학습
    python train_patchcore_anomalib.py --data-dir 11 --output patchcore_model/roi_model --roi

설치:
    pip install anomalib
"""

import argparse
from pathlib import Path
import shutil
import warnings
import os

# PatchCore는 optimizer 없이 학습하므로 경고 무시
warnings.filterwarnings("ignore", message=".*configure_optimizers.*returned.*None.*")
warnings.filterwarnings("ignore", category=UserWarning, module="lightning")

# FAISS 사용 가능 여부 확인
try:
    import faiss
    FAISS_AVAILABLE = True
    print("✅ FAISS detected - will use for faster nearest neighbor search")
except ImportError:
    FAISS_AVAILABLE = False
    print("⚠️  FAISS not available - using PyTorch (slower)")


def setup_dataset(source_dir: Path, output_dir: Path):
    """Setup dataset in Anomalib Folder format
    
    Anomalib expects:
        output_dir/
            train/
                good/
                    image1.png
                    image2.png
            test/
                good/    (optional)
                defect/  (optional)
    """
    output_dir = Path(output_dir)
    train_good = output_dir / "train" / "good"
    train_good.mkdir(parents=True, exist_ok=True)
    
    # Find images in source
    source_dir = Path(source_dir)
    
    # Check if already in correct format
    if (source_dir / "train" / "good").exists():
        print(f"✅ Dataset already in correct format: {source_dir}")
        return source_dir
    
    # Copy images to train/good
    image_files = list(source_dir.glob("*.png")) + list(source_dir.glob("*.jpg"))
    
    if len(image_files) == 0:
        # Try subdirectory
        if (source_dir / "good").exists():
            image_files = list((source_dir / "good").glob("*.png")) + list((source_dir / "good").glob("*.jpg"))
    
    print(f"📁 Found {len(image_files)} images")
    
    for img in image_files:
        shutil.copy(img, train_good / img.name)
        print(f"  ✓ Copied: {img.name}")
    
    print(f"✅ Dataset prepared at: {output_dir}")
    return output_dir


def export_to_openvino(model, output_dir: Path, image_size: tuple = (224, 224), pattern_name: str = "model"):
    """Export model to OpenVINO IR format using mo.convert_model
    
    PyTorch -> OpenVINO IR 직접 변환 (패턴 이름으로 저장)
    """
    import torch
    import torch.nn as nn
    
    class PatchCoreRawWrapper(nn.Module):
        """Wrapper to export only the raw PatchCore model without post-processing"""
        def __init__(self, patchcore_model):
            super().__init__()
            self.model = patchcore_model
        
        def forward(self, x):
            output = self.model(x)
            return output.anomaly_map, output.pred_score
    
    print(f"\n🔄 Converting PyTorch to OpenVINO IR ({pattern_name}.xml)...")
    
    # Get the internal model (without post-processing)
    raw_model = model.model
    raw_model.eval()
    
    # Wrap it
    wrapper = PatchCoreRawWrapper(raw_model)
    wrapper.eval()
    
    # Create dummy input with fixed shape
    dummy_input = torch.randn(1, 3, image_size[0], image_size[1])
    
    # Convert to OpenVINO IR using mo.convert_model
    try:
        import openvino as ov
        from openvino.tools import mo
        
        # PyTorch -> OpenVINO IR 직접 변환
        ov_model = mo.convert_model(
            wrapper,
            input_shape=[1, 3, image_size[0], image_size[1]],
            example_input=dummy_input,
        )
        
        # Save as IR with pattern name (FP16 압축)
        ir_path = output_dir / f"{pattern_name}.xml"
        ov.save_model(ov_model, str(ir_path), compress_to_fp16=True)
        print(f"   ✅ OpenVINO IR (FP16): {ir_path}")
        print(f"   ✅ OpenVINO BIN (FP16): {output_dir / f'{pattern_name}.bin'}")
        
        # Test inference
        import numpy as np
        core = ov.Core()
        compiled = core.compile_model(ov_model, 'CPU')
        test_input = np.random.randn(1, 3, image_size[0], image_size[1]).astype(np.float32)
        results = compiled(test_input)
        print(f"   ✅ OpenVINO test inference passed")
        
        return ir_path
        
    except ImportError:
        print("   ⚠️ OpenVINO not installed. Install with: pip install openvino-dev")
        return None
    except Exception as e:
        print(f"   ⚠️ OpenVINO conversion failed: {e}")
        import traceback
        traceback.print_exc()
        return None
        
    except ImportError:
        print("   ⚠️ OpenVINO not installed. Install with: pip install openvino")
        return None
    except Exception as e:
        print(f"   ⚠️ OpenVINO conversion failed: {e}")
        import traceback
        traceback.print_exc()
        return None


def compute_normalization_stats(model, datamodule, image_size):
    """학습 데이터(양품)로부터 정규화 기준값 계산
    
    Returns:
        dict: max_score, mean_score, std_score, max_pixel, mean_pixel, num_samples
    """
    import torch
    import numpy as np
    
    print("   Inferring on training data to get normalization baseline...")
    
    model.eval()
    raw_model = model.model
    raw_model.eval()
    
    all_pred_scores = []
    all_max_pixels = []
    all_mean_pixels = []
    
    # 학습 데이터 로드
    datamodule.setup(stage="fit")
    train_loader = datamodule.train_dataloader()
    
    with torch.no_grad():
        for batch in train_loader:
            # anomalib 버전에 따라 batch 형식이 다름
            if hasattr(batch, 'image'):
                images = batch.image
            elif isinstance(batch, dict):
                images = batch["image"]
            else:
                images = batch[0] if isinstance(batch, (list, tuple)) else batch
            
            # 추론
            output = raw_model(images)
            
            # anomaly_map: (B, 1, H, W), pred_score: (B,)
            anomaly_map = output.anomaly_map
            pred_score = output.pred_score
            
            # 배치별 통계
            for i in range(images.shape[0]):
                score = pred_score[i].item() if pred_score.dim() > 0 else pred_score.item()
                all_pred_scores.append(score)
                
                pixel_map = anomaly_map[i].cpu().numpy()
                all_max_pixels.append(float(np.max(pixel_map)))
                all_mean_pixels.append(float(np.mean(pixel_map)))
    
    # 통계 계산
    all_pred_scores = np.array(all_pred_scores)
    all_max_pixels = np.array(all_max_pixels)
    all_mean_pixels = np.array(all_mean_pixels)
    
    stats = {
        'max_score': float(np.max(all_pred_scores)),
        'mean_score': float(np.mean(all_pred_scores)),
        'std_score': float(np.std(all_pred_scores)),
        'max_pixel': float(np.max(all_max_pixels)),
        'mean_pixel': float(np.mean(all_mean_pixels)),
        'num_samples': len(all_pred_scores),
    }
    
    print(f"   ✅ Computed from {stats['num_samples']} samples:")
    print(f"      Max pred_score: {stats['max_score']:.4f}")
    print(f"      Mean pred_score: {stats['mean_score']:.4f} ± {stats['std_score']:.4f}")
    print(f"      Max pixel value: {stats['max_pixel']:.4f}")
    print(f"      Mean pixel value: {stats['mean_pixel']:.4f}")
    
    return stats


def train_patchcore(data_dir: Path, output_dir: Path, config: dict):
    """Train PatchCore using Anomalib"""
    
    try:
        from anomalib.data import Folder
        from anomalib.models import Patchcore
        from anomalib.engine import Engine
        import torch
    except ImportError as e:
        print(f"❌ anomalib import error: {e}")
        print("   Install with: pip install anomalib")
        return None, None
    
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    torch_path = output_dir / "patchcore_model.pt"
    
    # 이미 학습된 모델이 있으면 변환만 진행
    if torch_path.exists():
        print(f"\n✅ 기존 모델 발견: {torch_path}")
        print(f"   학습을 건너뛰고 OpenVINO 변환만 진행합니다.")
        
        # 모델 로드
        checkpoint = torch.load(torch_path, map_location='cpu')
        
        # 모델 재구성
        model = Patchcore(
            backbone=checkpoint.get('backbone', 'wide_resnet50_2'),
            layers=checkpoint.get('layers', ['layer2', 'layer3']),
            pre_trained=True,
            coreset_sampling_ratio=config.get('coreset_sampling_ratio', 0.1),
            num_neighbors=config.get('num_neighbors', 9),
        )
        model.load_state_dict(checkpoint['model_state_dict'])
        model.eval()
        
        print(f"   Memory bank size: {model.model.memory_bank.shape}")
        
        # 패턴 이름으로 OpenVINO 변환 (config에서 전달받거나 output 폴더 이름 사용)
        pattern_name = config.get('pattern_name') or output_dir.name
        ir_path = export_to_openvino(model, output_dir, checkpoint.get('image_size', (224, 224)), pattern_name)
        
        if ir_path and ir_path.exists():
            print(f"\n✅ Conversion completed!")
            print(f"   Pattern IR: {ir_path}")
        else:
            print(f"\n⚠️ Conversion failed!")
        
        return model, None
    
    print(f"\n🚀 Training PatchCore with Anomalib")
    print(f"   Backbone: {config['backbone']}")
    print(f"   Layers: {config['layers']}")
    print(f"   Image size: {config['image_size']}")
    print(f"   Coreset ratio: {config['coreset_sampling_ratio']}")
    print(f"   Num neighbors: {config['num_neighbors']}")
    print(f"   Batch size: {config['batch_size']}")
    
    # FAISS 사용 설정
    if config.get('use_faiss', False):
        print("🚀 Using FAISS for nearest neighbor search (3-10x faster)")
        os.environ['ANOMALIB_USE_FAISS'] = '1'
    else:
        print("⚙️  Using PyTorch for nearest neighbor search")
        os.environ.pop('ANOMALIB_USE_FAISS', None)
    
    # Setup data module - let anomalib handle transforms
    datamodule = Folder(
        name="copper_anomaly",
        root=str(data_dir),
        normal_dir="train/good",
        abnormal_dir=None,  # No abnormal samples for training
        normal_test_dir="train/good",  # Use same as train for validation
        mask_dir=None,
        train_batch_size=config['batch_size'],
        eval_batch_size=config['batch_size'],
        num_workers=0,  # Mac compatibility
        test_split_mode="from_dir",
        val_split_mode="same_as_test",
    )
    
    # Initialize model
    print(f"\n📊 Creating PatchCore model:")
    print(f"   - backbone: {config['backbone']}")
    print(f"   - coreset_sampling_ratio: {config['coreset_sampling_ratio']}")
    print(f"   - num_neighbors: {config['num_neighbors']}")
    model = Patchcore(
        backbone=config['backbone'],
        layers=config['layers'],
        pre_trained=True,
        coreset_sampling_ratio=config['coreset_sampling_ratio'],
        num_neighbors=config['num_neighbors'],
    )
    
    # 생성된 모델의 파라미터 확인 (config 값으로 확인)
    print(f"\n✅ Model created successfully with config:")
    print(f"   - coreset_sampling_ratio: {config['coreset_sampling_ratio']}")
    print(f"   - num_neighbors: {config['num_neighbors']}")
    
    # Setup output directory
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Initialize engine
    engine = Engine(
        default_root_dir=str(output_dir),
        max_epochs=1,  # PatchCore only needs 1 epoch
        accelerator="auto",
        devices=1,
    )
    
    # Train
    print("\n🔥 Starting training...")
    engine.fit(model=model, datamodule=datamodule)
    
    print(f"\n✅ Training completed!")
    print(f"   Memory bank size: {model.model.memory_bank.shape}")
    
    # 양품 데이터로 정규화 기준값 계산
    print("\n📊 Computing normalization stats from training data...")
    norm_stats = compute_normalization_stats(model, datamodule, config['image_size'])
    
    # Export model
    print("\n📦 Exporting model...")
    
    # Save as torch model
    torch_path = output_dir / "patchcore_model.pt"
    torch.save({
        'model_state_dict': model.state_dict(),
        'memory_bank': model.model.memory_bank,
        'backbone': config['backbone'],
        'layers': config['layers'],
        'image_size': config['image_size'],
        'coreset_sampling_ratio': config['coreset_sampling_ratio'],
        'num_neighbors': config['num_neighbors'],
        'norm_stats': norm_stats,  # 정규화 기준값 포함
    }, torch_path)
    print(f"   ✅ Torch model: {torch_path}")
    print(f"   📊 Model config:")
    print(f"      - backbone: {config['backbone']}")
    print(f"      - coreset_sampling_ratio: {config['coreset_sampling_ratio']}")
    print(f"      - num_neighbors: {config['num_neighbors']}")
    print(f"      - memory_bank_size: {model.model.memory_bank.shape}")
    
    # 정규화 기준값을 별도 파일로도 저장 (C++ 사용용)
    norm_stats_path = output_dir / "norm_stats.txt"
    with open(norm_stats_path, 'w') as f:
        f.write(f"max_score={norm_stats['max_score']:.6f}\n")
        f.write(f"mean_score={norm_stats['mean_score']:.6f}\n")
        f.write(f"std_score={norm_stats['std_score']:.6f}\n")
        f.write(f"max_pixel={norm_stats['max_pixel']:.6f}\n")
        f.write(f"mean_pixel={norm_stats['mean_pixel']:.6f}\n")
        f.write(f"num_samples={norm_stats['num_samples']}\n")
    print(f"   ✅ Normalization stats: {norm_stats_path}")
    
    # Export to OpenVINO IR
    print("\n🔄 Exporting to OpenVINO IR...")
    
    # 패턴 이름 결정 (config에서 전달받거나 output 폴더 이름 사용)
    pattern_name = config.get('pattern_name') or output_dir.name
    pattern_ir_xml = output_dir / f"{pattern_name}.xml"
    pattern_ir_bin = output_dir / f"{pattern_name}.bin"
    
    try:
        ir_path = export_to_openvino(model, output_dir, config['image_size'], pattern_name)
        
        if ir_path and ir_path.exists():
            print(f"   ✅ Pattern OpenVINO IR: {pattern_ir_xml}")
            print(f"   ✅ Pattern OpenVINO BIN: {pattern_ir_bin}")
        else:
            print(f"   ⚠️ OpenVINO export failed - IR files not created")
    except Exception as e:
        print(f"   ❌ OpenVINO export failed: {e}")
        import traceback
        traceback.print_exc()
        print(f"      Expected: {model_raw_xml}")
        traceback.print_exc()
    
    print(f"\n✅ Training completed!")
    print(f"   Model saved to: {output_dir}")
    print(f"   Memory bank size: {model.model.memory_bank.shape}")
    
    if ir_path:
        print(f"\n🎯 C++ inference용 모델:")
        print(f"   {ir_path}")
        print(f"   {ir_path.parent / 'model_raw.bin'}")
    
    return model, engine


def parse_args():
    parser = argparse.ArgumentParser(description='PatchCore Training with Anomalib')
    parser.add_argument('--data-dir', type=str, default='crimp',
                        help='Directory containing normal images (or Folder format dataset)')
    parser.add_argument('--output', type=str, default='patchcore_model/anomalib_patchcore',
                        help='Output directory')
    parser.add_argument('--pattern-name', type=str, default=None,
                        help='Pattern name for output files (default: use output folder name)')
    parser.add_argument('--backbone', type=str, default='wide_resnet50_2',
                        choices=['resnet18', 'resnet50', 'wide_resnet50_2'],
                        help='Backbone network')
    parser.add_argument('--layers', type=str, nargs='+', default=['layer2', 'layer3'],
                        help='Layers to extract features from')
    parser.add_argument('--image-size', type=int, nargs=2, default=[224, 224],
                        help='Input image size')
    parser.add_argument('--coreset-ratio', type=float, default=0.01,
                        help='Coreset subsampling ratio')
    parser.add_argument('--num-neighbors', type=int, default=9,
                        help='Number of nearest neighbors')
    parser.add_argument('--batch-size', type=int, default=32,
                        help='Batch size')
    parser.add_argument('--roi', action='store_true',
                        help='ROI 모드: 첫 이미지에서 ROI 선택 후 템플릿 매칭으로 전체 crop')
    parser.add_argument('--roi-threshold', type=float, default=0.5,
                        help='ROI 템플릿 매칭 임계값 (0.0-1.0)')
    parser.add_argument('--use-faiss', action='store_true', default=FAISS_AVAILABLE,
                        help='Use FAISS for faster nearest neighbor search (default: auto-detect)')
    parser.add_argument('--no-use-faiss', dest='use_faiss', action='store_false',
                        help='Disable FAISS (use default sklearn)')
    return parser.parse_args()


def main():
    args = parse_args()
    
    # 받은 인자 출력 (디버깅용)
    print("\n" + "="*60)
    print("📋 Training Arguments:")
    print(f"   data_dir: {args.data_dir}")
    print(f"   output: {args.output}")
    print(f"   pattern_name: {args.pattern_name}")
    print(f"   backbone: {args.backbone}")
    print(f"   coreset_ratio: {args.coreset_ratio}")
    print(f"   num_neighbors: {args.num_neighbors}")
    print(f"   batch_size: {args.batch_size}")
    print(f"   image_size: {args.image_size}")
    print(f"   use_faiss: {args.use_faiss}")
    print("="*60 + "\n")
    
    data_dir = Path(args.data_dir)
    output_dir = Path(args.output)
    
    if not data_dir.exists():
        print(f"❌ Data directory not found: {data_dir}")
        return
    
    # ROI 모드: 첫 이미지에서 ROI 선택 후 템플릿 매칭으로 전체 crop
    if args.roi:
        print("\n🎯 ROI 모드 활성화")
        print("   첫 이미지에서 학습할 영역을 선택하세요.")
        
        try:
            from roi_crop_dataset import interactive_roi_crop
        except ImportError:
            print("❌ roi_crop_dataset.py를 찾을 수 없습니다.")
            return
        
        # crop된 데이터셋 저장 위치
        cropped_dir = output_dir / "cropped_roi_dataset"
        
        roi = interactive_roi_crop(data_dir, cropped_dir, args.roi_threshold)
        
        if roi is None:
            print("❌ ROI 선택 취소됨")
            return
        
        # crop된 데이터로 학습 진행
        data_dir = cropped_dir
        print(f"\n✅ ROI crop 완료, 학습 데이터: {data_dir}")
    
    # Setup dataset
    temp_dataset = output_dir / "temp_dataset"
    dataset_dir = setup_dataset(data_dir, temp_dataset)
    
    # Config
    config = {
        'backbone': args.backbone,
        'layers': args.layers,
        'image_size': tuple(args.image_size),
        'coreset_sampling_ratio': args.coreset_ratio,
        'num_neighbors': args.num_neighbors,
        'batch_size': args.batch_size,
        'pattern_name': args.pattern_name,  # 패턴 이름 전달
        'use_faiss': args.use_faiss and FAISS_AVAILABLE,
    }
    
    # Train
    model, engine = train_patchcore(dataset_dir, output_dir, config)
    
    if model is not None:
        print(f"\n💡 To run inference:")
        print(f"   python inference_patchcore_anomalib.py --model {output_dir} --source test.png")


if __name__ == '__main__':
    main()
