"""PatchCore Training using Anomalib

anomalib 라이브러리를 사용한 정교한 PatchCore 학습

사용법:
    python train_patchcore_anomalib.py --data-dir crimp --output patchcore_model/anomalib_patchcore
    python train_patchcore_anomalib.py --data-dir patchcore_model/temp_dataset --output patchcore_model/anomalib_patchcore
"""

import argparse
from pathlib import Path
import shutil
import warnings
import os
import sys

# PatchCore는 optimizer 없이 학습하므로 경고 무시
warnings.filterwarnings("ignore", message=".*configure_optimizers.*returned.*None.*")
warnings.filterwarnings("ignore", category=UserWarning, module="lightning")
warnings.filterwarnings("ignore", category=UserWarning, module="torch")
warnings.filterwarnings("ignore", category=UserWarning)
# ONNX export 중 TracerWarning 무시
warnings.filterwarnings("ignore", message=".*Converting a tensor to a Python boolean.*")
warnings.filterwarnings("ignore", message=".*Constant folding.*")
warnings.filterwarnings("ignore", message=".*Exporting aten::index operator.*")
warnings.filterwarnings("ignore", message=".*TracerWarning.*")
warnings.filterwarnings("ignore", message=".*Tensor Cores.*")

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


def export_to_onnx_and_tensorrt(model, output_dir: Path, image_size: tuple, pattern_name: str):
    """Export model to ONNX and TensorRT formats"""
    import torch
    import torch.onnx
    
    try:
        # 모델의 모든 서브모듈과 버퍼를 CPU로 명시적 이동
        model = model.cpu()
        if hasattr(model, 'model'):
            model.model = model.model.cpu()
        for module in model.modules():
            module.cpu()
        # 모든 버퍼(memory_bank 등)도 CPU로 이동
        for buffer_name, buffer in model.named_buffers():
            if buffer.is_cuda:
                buffer.data = buffer.data.cpu()
        model.eval()
        
        # Create dummy input on CPU
        dummy_input = torch.randn(1, 3, image_size[0], image_size[1])
        
        # Export to ONNX
        onnx_path = output_dir / f"{pattern_name}.onnx"
        print(f"Exporting to ONNX")
        
        torch.onnx.export(
            model.model,  # Export the internal PatchCore model
            dummy_input,
            str(onnx_path),
            export_params=True,
            opset_version=13,
            do_constant_folding=True,
            input_names=['input'],
            output_names=['anomaly_map', 'pred_score'],
            dynamic_axes={
                'input': {0: 'batch_size'},
                'anomaly_map': {0: 'batch_size'},
                'pred_score': {0: 'batch_size'}
            }
        )
        print(f"ONNX exported")
        
        # Convert ONNX to TensorRT
        trt_path = output_dir / f"{pattern_name}.trt"
        print(f"Converting to TensorRT")
        
        try:
            import subprocess
            
            # Find trtexec
            trtexec_paths = [
                '/usr/src/tensorrt/bin/trtexec',
                'trtexec',
            ]
            trtexec_cmd = None
            for path in trtexec_paths:
                if os.path.exists(path) or shutil.which(path):
                    trtexec_cmd = path
                    break
            
            if not trtexec_cmd:
                print(f"   ⚠️  trtexec not found. Install TensorRT to enable conversion.")
                return
            
            result = subprocess.run([
                trtexec_cmd,
                f'--onnx={onnx_path}',
                f'--saveEngine={trt_path}',
                '--fp16',  # FP16 precision for Jetson
                f'--minShapes=input:1x3x{image_size[0]}x{image_size[1]}',
                f'--optShapes=input:1x3x{image_size[0]}x{image_size[1]}',
                f'--maxShapes=input:1x3x{image_size[0]}x{image_size[1]}',
                '--verbose',  # 진행 상황 출력
                '--builderOptimizationLevel=5',  # 최고 최적화 레벨
                '--timingCacheFile=/tmp/timing.cache',  # 타이밍 캐시 재사용
                '--device=0',  # GPU 0 사용
                '--workspace=4096',  # 4GB workspace (메모리 충분하면 더 빠름)
            ], capture_output=True, text=True, timeout=300)
            
            if result.returncode == 0 and trt_path.exists():
                print(f"TensorRT engine created")
            else:
                print(f"TensorRT conversion failed")
        except FileNotFoundError:
            print(f"trtexec not found")
        except subprocess.TimeoutExpired:
            print(f"TensorRT conversion timeout")
        except Exception as e:
            print(f"TensorRT conversion error: {e}")
            
    except Exception as e:
        print(f"   ❌ Export failed: {e}")
        import traceback
        traceback.print_exc()


def train_patchcore(data_dir: Path, output_dir: Path, config: dict):
    """Train PatchCore using Anomalib"""
    
    try:
        from anomalib.data import Folder
        from anomalib.models import Patchcore
        from lightning.pytorch import Trainer
        import torch
        import gc
    except ImportError as e:
        print(f"❌ anomalib import error: {e}")
        print("   Install with: pip install anomalib")
        return None, None
    
    # CUDA 메모리 초기화
    if torch.cuda.is_available():
        torch.cuda.empty_cache()
        torch.cuda.synchronize()
    
    model = None
    engine = None
    datamodule = None
    model = None
    engine = None
    datamodule = None
    
    try:
        output_dir = Path(output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)
        
        print(f"\n🚀 Training PatchCore with Anomalib")
        print(f"   Backbone: {config['backbone']}")
        print(f"   Image size: {config['image_size']}")
        print(f"   Coreset ratio: {config['coreset_sampling_ratio']}")
        print(f"   Num neighbors: {config['num_neighbors']}")
        print(f"   Batch size: {config['batch_size']}")
        
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
            num_workers=0,
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
            pre_trained=True,
            coreset_sampling_ratio=config['coreset_sampling_ratio'],
            num_neighbors=config['num_neighbors'],
        )
        
        # Initialize engine - GPU for faster training
        import logging
        import os
        
        # Lightning 로거 완전 비활성화
        logging.getLogger("lightning.pytorch").setLevel(logging.ERROR)
        logging.getLogger("lightning").setLevel(logging.ERROR)
        logging.getLogger("pytorch_lightning").setLevel(logging.ERROR)
        
        # 환경 변수로 추가 메시지 억제
        os.environ['PYTORCH_ENABLE_MPS_FALLBACK'] = '1'
        
        # Anomalib Engine 대신 직접 PyTorch Lightning Trainer 사용
        from lightning.pytorch import Trainer
        from lightning.pytorch.callbacks import Callback
        
        engine = Trainer(
            default_root_dir=str(output_dir),
            max_epochs=1,  # PatchCore only needs 1 epoch
            accelerator="gpu",
            devices=1,
            limit_val_batches=0,  # 검증 비활성화
            enable_progress_bar=True,
            enable_model_summary=False,  # 모델 요약 테이블 비활성화
            logger=False,  # 로거 비활성화
            enable_checkpointing=False,  # 체크포인트 비활성화
            callbacks=[],  # 명시적으로 빈 리스트
        )
        
        # stderr 임시 리다이렉트로 GPU/TPU 메시지 숨기기
        import contextlib
        import io
        
        stderr_backup = sys.stderr
        sys.stderr = io.StringIO()
        
        try:
            # Train
            print("\n🔥 Starting training...")
            engine.fit(model=model, datamodule=datamodule)
        finally:
            # stderr 복원
            sys.stderr = stderr_backup
        
        print(f"\n✅ Training completed!")
        print(f"   Memory bank size: {model.model.memory_bank.shape}")
        
        # 양품 데이터로 정규화 기준값 계산 (이전 방식 사용)
        print(f"\n📊 Computing normalization stats from training data...")
        norm_stats = compute_normalization_stats(model, datamodule, config['image_size'])
        
        # 정규화 통계를 패턴 이름 파일로 저장 (C++ 사용용)
        pattern_name = config.get('pattern_name') or output_dir.name
        norm_stats_path = output_dir / pattern_name
        with open(norm_stats_path, 'w') as f:
            f.write(f"mean_pixel={norm_stats['mean_pixel']:.6f}\n")
            f.write(f"max_pixel={norm_stats['max_pixel']:.6f}\n")
        print(f"   ✅ Normalization stats saved: {norm_stats_path}")
        
        # Save as torch model
        torch_path = output_dir / "patchcore_model.pt"
        torch.save({
            'model_state_dict': model.state_dict(),
            'memory_bank': model.model.memory_bank.cpu(),
            'backbone': config['backbone'],
            'image_size': config['image_size'],
            'coreset_sampling_ratio': config['coreset_sampling_ratio'],
            'num_neighbors': config['num_neighbors'],
        }, torch_path)
        print(f"   ✅ Torch model: {torch_path}")
        
        # Export to ONNX and TensorRT
        export_to_onnx_and_tensorrt(model, output_dir, config['image_size'], pattern_name)
        
        return None, None
        
    finally:
        # 메모리 명시적 해제 (성공/실패 무관)
        print(f"\n🧹 Cleaning up memory...")
        import gc
        if model is not None:
            del model
        if engine is not None:
            del engine
        if datamodule is not None:
            del datamodule
        gc.collect()
        print(f"   ✅ GPU memory cleaned")


def parse_args():
    parser = argparse.ArgumentParser(description='PatchCore Training with Anomalib')
    parser.add_argument('--data-dir', type=str, required=True,
                        help='Directory containing normal images (or Folder format dataset)')
    parser.add_argument('--output', type=str, required=True,
                        help='Output directory')
    parser.add_argument('--pattern-name', type=str, default=None,
                        help='Pattern name for output files (default: use output folder name)')
    parser.add_argument('--backbone', type=str, default='wide_resnet50_2',
                        choices=['resnet18', 'resnet50', 'wide_resnet50_2'],
                        help='Backbone network')
    parser.add_argument('--image-size', type=int, nargs=2, default=[224, 224],
                        help='Input image size')
    parser.add_argument('--coreset-ratio', type=float, default=0.01,
                        help='Coreset subsampling ratio')
    parser.add_argument('--num-neighbors', type=int, default=9,
                        help='Number of nearest neighbors')
    parser.add_argument('--batch-size', type=int, default=32,
                        help='Batch size')
    return parser.parse_args()


def main():
    args = parse_args()
    
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
    print("="*60 + "\n")
    
    data_dir = Path(args.data_dir)
    output_dir = Path(args.output)
    
    if not data_dir.exists():
        print(f"❌ Data directory not found: {data_dir}")
        return
    
    # Setup dataset
    temp_dataset = output_dir / "temp_dataset"
    dataset_dir = setup_dataset(data_dir, temp_dataset)
    
    # Config
    config = {
        'backbone': args.backbone,
        'image_size': tuple(args.image_size),
        'coreset_sampling_ratio': args.coreset_ratio,
        'num_neighbors': args.num_neighbors,
        'batch_size': args.batch_size,
        'pattern_name': args.pattern_name,
    }
    
    # Train
    model, engine = train_patchcore(dataset_dir, output_dir, config)
    
    if model is not None:
        print(f"\n💡 Training completed successfully!")
    
    # 명시적 메모리 해제
    import gc
    gc.collect()
    print(f"\n🧹 Memory cleanup completed")


if __name__ == '__main__':
    try:
        main()
    finally:
        # 프로세스 종료 전 최종 메모리 정리
        import gc
        gc.collect()

