"""PaDiM Training using Anomalib

anomalib 라이브러리를 사용한 정교한 PaDiM 학습

사용법:
    python train_padim_anomalib.py --data-dir crimp --output padim_model/anomalib_padim
    python train_padim_anomalib.py --data-dir padim_model/temp_dataset --output padim_model/anomalib_padim
"""

import argparse
from pathlib import Path
import shutil
import warnings
import os
import sys

# PaDiM은 optimizer 없이 학습하므로 경고 무시
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
    """Export model to ONNX and TensorRT formats using manual torch.onnx.export"""
    import torch
    
    try:
        # 모델을 CPU로 이동하고 eval 모드 설정
        model = model.cpu()
        model.eval()
        
        print(f"   ╔════════════════════════════════════════════════════════════════╗")
        print(f"   ║  Phase 1: PyTorch → ONNX Export                                ║")
        print(f"   ╚════════════════════════════════════════════════════════════════╝\n")
        print(f"   📤 Exporting to ONNX with corrected output names...")
        
        onnx_path = output_dir / f"{pattern_name}_padim.onnx"
        
        # 더미 입력 생성
        dummy_input = torch.randn(1, 3, image_size[0], image_size[1])
        
        # 수동으로 ONNX export하여 출력 이름 명시적으로 지정
        class ONNXExportWrapper(torch.nn.Module):
            def __init__(self, model):
                super().__init__()
                self.model = model
                
            def forward(self, x):
                # Anomalib 모델 실행
                output = self.model(x)
                
                # InferenceBatch에서 값 추출
                if hasattr(output, 'anomaly_map') and hasattr(output, 'pred_score'):
                    # anomaly_map: (B, H, W) -> (B, 1, H, W)
                    anomaly_map = output.anomaly_map
                    if anomaly_map.dim() == 3:
                        anomaly_map = anomaly_map.unsqueeze(1)
                    
                    # pred_score: (B,) 스칼라
                    pred_score = output.pred_score
                    if pred_score.dim() == 1:
                        pred_score = pred_score.view(-1, 1)
                    
                    return anomaly_map, pred_score
                elif isinstance(output, dict):
                    anomaly_map = output['anomaly_map']
                    pred_score = output['pred_score']
                    if anomaly_map.dim() == 3:
                        anomaly_map = anomaly_map.unsqueeze(1)
                    if pred_score.dim() == 1:
                        pred_score = pred_score.view(-1, 1)
                    return anomaly_map, pred_score
                else:
                    # 텐서 직접 반환 (fallback)
                    return output, torch.tensor([0.0])
        
        wrapped_model = ONNXExportWrapper(model)
        
        # ONNX export with explicit output names
        print(f"   ⏳ Running torch.onnx.export()...")
        torch.onnx.export(
            wrapped_model,
            dummy_input,
            str(onnx_path),
            input_names=['input'],
            output_names=['anomaly_map', 'pred_score'],  # 명시적으로 이름 지정
            dynamic_axes={
                'input': {0: 'batch'},
                'anomaly_map': {0: 'batch'},
                'pred_score': {0: 'batch'}
            },
            opset_version=13,
            do_constant_folding=True,
        )
        
        # ONNX 파일 생성 확인
        if onnx_path.exists():
            import shutil
            print(f"   ✅ ONNX export completed: {onnx_path.stat().st_size / (1024*1024):.2f} MB")
            print(f"      Location: {onnx_path.name}\n")
        else:
            print(f"   ❌ ONNX export failed - file not created")
            return
        
        # Convert ONNX to TensorRT
        trt_path = output_dir / f"{pattern_name}_padim.trt"
        print(f"\n{'='*80}")
        print(f"   🔧 Phase 2: TensorRT Engine Build")
        print(f"{'='*80}")
        print(f"   Input  : {onnx_path.name} ({onnx_path.stat().st_size / (1024*1024):.2f} MB)")
        print(f"   Output : {trt_path.name}")
        print(f"   Image  : {image_size[0]}x{image_size[1]}")
        print(f"   Mode   : FP16 optimization (Jetson)")
        print(f"{'='*80}\n")
        
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
                print(f"   ⚠️  trtexec not found. Skipping TensorRT conversion.")
                print(f"      Install TensorRT to enable .trt engine creation")
                return
            
            print(f"   ⏳ Starting TensorRT Builder (estimated 2-3 minutes)...")
            print(f"      The following verbose output shows layer-by-layer optimization:\n")
            
            # TensorRT 변환 실행 (출력을 실시간으로 보여줌)
            cmd = [
                trtexec_cmd,
                f'--onnx={onnx_path}',
                f'--saveEngine={trt_path}',
                '--fp16',  # FP16 precision for Jetson
                f'--minShapes=input:1x3x{image_size[0]}x{image_size[1]}',
                f'--optShapes=input:1x3x{image_size[0]}x{image_size[1]}',
                f'--maxShapes=input:1x3x{image_size[0]}x{image_size[1]}',
                '--verbose',  # 진행 상황 출력
            ]
            
            # TensorRT 버전에 따라 workspace 옵션 다르게 설정
            try:
                # 최신 TensorRT (8.x+)
                cmd.append('--memPoolSize=workspace:8192M')
            except:
                # 이전 TensorRT
                cmd.append('--workspace=8192')
            
            print(f"   📋 Command: {' '.join(cmd)}\n")
            print(f"{'─'*80}\n")
            
            import time
            start_time = time.time()
            result = subprocess.run(cmd, capture_output=False, timeout=600)
            elapsed_time = time.time() - start_time
            
            print(f"\n{'─'*80}")
            print(f"{'='*80}")
            if result.returncode == 0 and trt_path.exists():
                print(f"   ✅ TensorRT Engine Built Successfully!")
                print(f"{'='*80}")
                print(f"   📊 Engine Size : {trt_path.stat().st_size / (1024*1024):.2f} MB")
                print(f"   ⏱️  Build Time  : {elapsed_time:.1f} seconds")
                print(f"   🎯 Status      : Ready for inference")
            else:
                print(f"   ❌ TensorRT Build Failed")
                print(f"{'='*80}")
                print(f"   Exit Code: {result.returncode}")
                if not trt_path.exists():
                    print(f"   Missing  : {trt_path}")
            print(f"{'='*80}\n")
        except FileNotFoundError:
            print(f"\n   ⚠️  trtexec not found in system PATH")
        except subprocess.TimeoutExpired:
            print(f"\n   ⚠️  TensorRT conversion timeout (>10 minutes)")
        except Exception as e:
            print(f"\n   ❌ TensorRT conversion error: {e}")
            
    except Exception as e:
        print(f"   ❌ Export failed: {e}")
        import traceback
        traceback.print_exc()


def compute_normalization_stats(model, datamodule, image_size):
    """양품 데이터의 이상 점수 통계 계산"""
    import torch
    import numpy as np
    from tqdm import tqdm
    
    model.eval()
    model = model.cpu()
    
    # Setup datamodule
    datamodule.setup('fit')
    
    # Get training dataloader (양품 이미지)
    train_loader = datamodule.train_dataloader()
    
    print(f"   Computing stats from {len(train_loader.dataset)} training images...")
    
    all_pixel_scores = []
    all_image_scores = []
    
    with torch.no_grad():
        for batch in tqdm(train_loader, desc="   Processing", leave=False):
            images = batch['image'].cpu()
            
            # Forward pass
            predictions = model(images)
            
            # Get anomaly map - 다양한 출력 형식 처리
            anomaly_map = None
            pred_score = None
            
            # InferenceBatch 객체 처리
            if hasattr(predictions, 'anomaly_map'):
                anomaly_map = predictions.anomaly_map
                if hasattr(anomaly_map, 'cpu'):
                    anomaly_map = anomaly_map.cpu().numpy()
                elif hasattr(anomaly_map, 'numpy'):
                    anomaly_map = anomaly_map.numpy()
                    
            if hasattr(predictions, 'pred_score'):
                pred_score = predictions.pred_score
                if hasattr(pred_score, 'cpu'):
                    pred_score = pred_score.cpu().numpy()
                elif hasattr(pred_score, 'numpy'):
                    pred_score = pred_score.numpy()
            
            # dict 형식 처리
            if anomaly_map is None and isinstance(predictions, dict):
                if 'anomaly_map' in predictions:
                    anomaly_map = predictions['anomaly_map']
                    if hasattr(anomaly_map, 'cpu'):
                        anomaly_map = anomaly_map.cpu().numpy()
                if 'pred_score' in predictions:
                    pred_score = predictions['pred_score']
                    if hasattr(pred_score, 'cpu'):
                        pred_score = pred_score.cpu().numpy()
            
            # tensor 직접 반환 처리
            if anomaly_map is None and hasattr(predictions, 'cpu'):
                anomaly_map = predictions.cpu().numpy()
            
            # Collect scores
            if anomaly_map is not None:
                all_pixel_scores.extend(anomaly_map.flatten())
            if pred_score is not None:
                all_image_scores.extend(pred_score.flatten())
    
    # Calculate statistics
    if len(all_pixel_scores) > 0:
        all_pixel_scores = np.array(all_pixel_scores)
        mean_pixel = float(np.mean(all_pixel_scores))
        max_pixel = float(np.max(all_pixel_scores))
        std_pixel = float(np.std(all_pixel_scores))
    elif len(all_image_scores) > 0:
        # anomaly_map이 없으면 image score 사용
        all_image_scores = np.array(all_image_scores)
        mean_pixel = float(np.mean(all_image_scores))
        max_pixel = float(np.max(all_image_scores))
        std_pixel = float(np.std(all_image_scores))
    else:
        raise ValueError("No anomaly scores computed!")
    
    print(f"   📊 Normalization stats:")
    print(f"      Mean: {mean_pixel:.6f}")
    print(f"      Max:  {max_pixel:.6f}")
    print(f"      Std:  {std_pixel:.6f}")
    
    return {
        'mean_pixel': mean_pixel,
        'max_pixel': max_pixel,
        'std_pixel': std_pixel,
    }


def train_padim(data_dir: Path, output_dir: Path, config: dict):
    """Train PaDiM using Anomalib"""
    
    try:
        from anomalib.data import Folder
        from anomalib.models import Padim
        from lightning.pytorch import Trainer
        import torch
        import gc
        import os
    except ImportError as e:
        print(f"❌ anomalib import error: {e}")
        print("   Install with: pip install anomalib")
        return None, None
    
    # CPU 모드 사용
    print("[INFO] CPU 모드로 학습 진행")
    
    model = None
    engine = None
    datamodule = None
    
    try:
        output_dir = Path(output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)
        
        print(f"\n{'='*80}")
        print(f"🚀 STEP 1/4: Model Configuration")
        print(f"{'='*80}")
        print(f"   Backbone: {config['backbone']}")
        print(f"   Image size: {config['image_size']}")
        print(f"   Num layers: {config['n_features']}")
        print(f"   Batch size: {config['batch_size']}")
        print(f"{'='*80}\n")
        
        # Setup data module
        datamodule = Folder(
            name="copper_anomaly",
            root=str(data_dir),
            normal_dir="train/good",
            abnormal_dir=None,
            normal_test_dir="train/good",
            mask_dir=None,
            train_batch_size=config['batch_size'],
            eval_batch_size=config['batch_size'],
            num_workers=0,
            test_split_mode="from_dir",
            val_split_mode="same_as_test",
        )
        
        # Initialize PaDiM model
        print(f"\n{'='*80}")
        print(f"📊 STEP 2/4: Creating PaDiM Model")
        print(f"{'='*80}")
        print(f"   - backbone: {config['backbone']}")
        print(f"   - n_features: {config['n_features']}")
        print(f"{'='*80}\n")
        
        model = Padim(
            backbone=config['backbone'],
            pre_trained=True,
            n_features=config['n_features'],
        )
        
        # Initialize engine - CPU mode
        import logging
        
        print("[INFO] Using CPU for training")
        
        log_level = logging.INFO
        logging.basicConfig(
            level=log_level,
            format='%(message)s',
            force=True
        )
        
        logging.getLogger("lightning.pytorch").setLevel(log_level)
        logging.getLogger("lightning").setLevel(log_level)
        logging.getLogger("pytorch_lightning").setLevel(log_level)
        
        from lightning.pytorch.callbacks import RichProgressBar
        from lightning.pytorch.callbacks.progress.rich_progress import RichProgressBarTheme
        
        progress_bar = RichProgressBar(
            theme=RichProgressBarTheme(
                description="cyan",
                progress_bar="green",
                progress_bar_finished="green",
                batch_progress="cyan",
                time="grey54",
                processing_speed="grey70",
                metrics="cyan"
            ),
            leave=True
        )
        
        engine = Trainer(
            default_root_dir=str(output_dir),
            max_epochs=1,
            accelerator="cpu",
            devices=1,
            limit_val_batches=0,
            enable_progress_bar=True,
            enable_model_summary=True,
            logger=False,
            enable_checkpointing=False,
            callbacks=[progress_bar],
            log_every_n_steps=1,
        )
        
        # Train
        print(f"\n{'='*80}")
        print(f"🔥 STEP 3/4: Training PaDiM (Feature Extraction)")
        print(f"{'='*80}")
        print(f"   Backbone: {config['backbone']}")
        print(f"   Image size: {config['image_size'][0]}x{config['image_size'][1]}")
        print(f"   Num layers: {config['n_features']}")
        print(f"{'='*80}\n")
        
        engine.fit(model=model, datamodule=datamodule)
        
        print(f"\n{'='*80}")
        print(f"✅ Training Completed Successfully!")
        print(f"{'='*80}\n")
        
        # 양품 데이터로 정규화 기준값 계산
        print(f"\n📊 Computing normalization stats from training data...")
        norm_stats = compute_normalization_stats(model, datamodule, config['image_size'])
        
        # 정규화 통계를 패턴 이름 파일로 저장 (_padim suffix)
        pattern_name = config.get('pattern_name') or output_dir.name
        norm_stats_path = output_dir / f"{pattern_name}_padim"
        with open(norm_stats_path, 'w') as f:
            f.write(f"mean_pixel={norm_stats['mean_pixel']:.6f}\n")
            f.write(f"max_pixel={norm_stats['max_pixel']:.6f}\n")
        print(f"   ✅ Normalization stats saved: {norm_stats_path}")
        
        # Save as torch model
        torch_path = output_dir / f"{pattern_name}_padim.pt"
        torch.save({
            'model_state_dict': model.state_dict(),
            'backbone': config['backbone'],
            'image_size': config['image_size'],
            'n_features': config['n_features'],
        }, torch_path)
        print(f"   ✅ Torch model saved: {torch_path}")
        print(f"      Size: {torch_path.stat().st_size / (1024*1024):.2f} MB")
        
        # Export to ONNX and TensorRT
        print(f"\n{'='*80}")
        print(f"🔄 STEP 4/4: Model Export (PyTorch → ONNX → TensorRT)")
        print(f"{'='*80}\n")
        export_to_onnx_and_tensorrt(model, output_dir, config['image_size'], pattern_name)
        
        # 생성된 파일 확인
        print(f"\n{'='*80}")
        print(f"📦 Training Complete - Generated Files Summary")
        print(f"{'='*80}")
        print(f"   Output Directory: {output_dir}")
        print(f"{'─'*80}")
        print(f"   ✅ PyTorch Model       : {torch_path.name}")
        print(f"      Size                : {torch_path.stat().st_size / (1024*1024):.2f} MB")
        print(f"   ✅ Normalization Stats : {norm_stats_path.name}")
        
        onnx_path = output_dir / f"{pattern_name}_padim.onnx"
        trt_path = output_dir / f"{pattern_name}_padim.trt"
        
        if onnx_path.exists():
            print(f"   ✅ ONNX Model          : {onnx_path.name}")
            print(f"      Size                : {onnx_path.stat().st_size / (1024*1024):.2f} MB")
        else:
            print(f"   ⚠️  ONNX Model         : Not created")
            
        if trt_path.exists():
            print(f"   ✅ TensorRT Engine     : {trt_path.name}")
            print(f"      Size                : {trt_path.stat().st_size / (1024*1024):.2f} MB")
            print(f"      Status              : Ready for deployment")
        else:
            print(f"   ⚠️  TensorRT Engine    : Not created")
        
        print(f"{'='*80}\n")
        
        return None, None
        
    finally:
        # 메모리 명시적 해제
        print(f"🧹 Cleaning up memory...")
        import gc
        if model is not None:
            del model
        if engine is not None:
            del engine
        if datamodule is not None:
            del datamodule
        gc.collect()
        print(f"   ✅ Memory cleaned")


def parse_args():
    parser = argparse.ArgumentParser(description='PaDiM Training with Anomalib')
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
    parser.add_argument('--n-features', type=int, default=3,
                        help='Number of features to extract (1-4, default: 3)')
    parser.add_argument('--batch-size', type=int, default=2,
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
    print(f"   n_features: {args.n_features}")
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
        'n_features': args.n_features,
        'batch_size': args.batch_size,
        'pattern_name': args.pattern_name,
    }
    
    # Train
    model, engine = train_padim(dataset_dir, output_dir, config)
    
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
