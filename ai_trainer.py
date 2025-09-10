#!/usr/bin/env python3

import os
import sys
import json
import argparse
import cv2
import numpy as np
from pathlib import Path
import torch
import logging
import warnings
from anomalib.data import Folder
from anomalib.models import Patchcore
from anomalib.engine import Engine
from tqdm import tqdm
try:
    import onnxruntime as ort
    ONNX_AVAILABLE = True
except ImportError:
    ONNX_AVAILABLE = False
    print("ONNX Runtime not available. ONNX conversion will be skipped.", file=sys.stderr)

# 로깅 레벨 설정 - anomalib 관련 로그 숨기기
logging.getLogger("anomalib").setLevel(logging.ERROR)
logging.getLogger("lightning").setLevel(logging.ERROR)
logging.getLogger("pytorch_lightning").setLevel(logging.ERROR)
warnings.filterwarnings("ignore")


def train_ai_model(recipe_name):
    print(f"레시피 이름: {recipe_name}")
    
    # 데이터셋 경로 설정 (host/ 디렉토리에서 데이터 찾기)
    dataset_root = f"/app/host/data/{recipe_name}"
    
    if not os.path.exists(dataset_root):
        print(f"❌ 데이터셋을 찾을 수 없습니다: {dataset_root}")
        return {
            "status": "error",
            "message": f"Dataset not found: {dataset_root}"
        }
    
    train_dir = os.path.join(dataset_root, "train", "good")
    test_dir = os.path.join(dataset_root, "test", "good")
    
    if not os.path.exists(train_dir):
        print(f"❌ 학습 데이터를 찾을 수 없습니다: {train_dir}")
        return {
            "status": "error", 
            "message": f"Training data not found: {train_dir}"
        }
    
    if not os.path.exists(test_dir):
        print(f"❌ 테스트 데이터를 찾을 수 없습니다: {test_dir}")
        return {
            "status": "error",
            "message": f"Test data not found: {test_dir}"
        }
    
    # Anomalib 데이터 모듈 설정
    datamodule = Folder(
        name=recipe_name,
        root=dataset_root,
        normal_dir="train/good",
        abnormal_dir=None,     
        normal_test_dir="test/good",  
        train_batch_size=32,
        eval_batch_size=32,
        num_workers=0,
        extensions=[".bmp", ".jpg", ".png", ".jpeg"]
    )
    
    # 데이터셋 설정 및 로드
    print(f"데이터셋 경로: {dataset_root}")
    print(f"정상 이미지 학습 폴더: {datamodule.normal_dir}")
    print(f"정상 이미지 테스트 폴더: {datamodule.normal_test_dir}")
    
    try:
        datamodule.setup()
        
        print(f"학습 데이터 샘플 수: {len(datamodule.train_data.samples)}")
        print(f"테스트 데이터 샘플 수: {len(datamodule.test_data.samples)}")
        
    except Exception as e:
        print(f"❌ 데이터셋 설정 중 오류: {str(e)}")
        return {
            "status": "error",
            "message": f"Dataset setup failed: {str(e)}"
        }
    
    model = Patchcore(
        backbone="wide_resnet50_2"
    )
    
    result_dir = f"/app/results/{recipe_name}"
    engine = Engine(
        default_root_dir=result_dir,
        max_epochs=1,
        accelerator="gpu" if torch.cuda.is_available() else "cpu",
        devices=1
    )
    
    try:
        print("🚀 AI 모델 학습을 시작합니다...")
        print("📊 데이터 분석 중...")
        
        # 표준 출력 임시 리다이렉션으로 anomalib 로그 숨기기
        import sys
        from io import StringIO
        
        # 원래 stdout 저장
        original_stdout = sys.stdout
        
        # Engine fit 실행 (출력 숨기기)
        sys.stdout = StringIO()
        try:
            engine.fit(datamodule=datamodule, model=model)
        finally:
            sys.stdout = original_stdout
        
        print("✅ 모델 학습이 완료되었습니다!")
        print("🧪 모델 성능 평가 중...")
        
        # Test 실행 (출력 숨기기) 
        sys.stdout = StringIO()
        try:
            test_results = engine.test(datamodule=datamodule, model=model)
        finally:
            sys.stdout = original_stdout
        
        print("📈 성능 평가가 완료되었습니다!")
        
        # 체크포인트 파일 찾기
        checkpoint_file = None
        possible_paths = [
        os.path.join(result_dir, "AI", recipe_name, "latest", "weights", "lightning", "model.ckpt"),
            os.path.join(result_dir, "weights", "model.ckpt"),
            os.path.join(result_dir, "checkpoints", "model.ckpt")
        ]

        print(f"체크포인트 파일 검색 중...")
        for path in possible_paths:
            print(f"검색 경로: {path}")
            if os.path.exists(path):
                checkpoint_file = path
                print(f"✅ 체크포인트 발견: {checkpoint_file}")
                break

        # 체크포인트를 찾지 못했으면 전체 디렉토리 스캔
        if not checkpoint_file:
            print(f"기본 경로에서 찾지 못함. 전체 디렉토리 스캔 시작...")
            for root, dirs, files in os.walk(result_dir):
                for file in files:
                    if file.endswith('.ckpt'):
                        checkpoint_file = os.path.join(root, file)
                        print(f"✅ 체크포인트 발견: {checkpoint_file}")
                        break
                if checkpoint_file:
                    break
        
        if checkpoint_file and os.path.exists(checkpoint_file):
            # 학습된 모델을 host/models/레시피이름/model.ckpt로 복사
            host_model_dir = f"/app/host/models/{recipe_name}"
            host_model_path = os.path.join(host_model_dir, "model.ckpt")
            
            os.makedirs(host_model_dir, exist_ok=True)
            
            import shutil
            shutil.copy2(checkpoint_file, host_model_path)

            # ONNX 모델로 변환하여 저장 (별도 프로세스로 실행)
            onnx_path = os.path.join(host_model_dir, "model.onnx")
            print("ONNX 모델 변환 시작...")
            
            # 별도 프로세스로 ONNX 변환 실행
            try:
                import subprocess
                convert_cmd = [
                    'python3', '/app/convert_to_onnx.py',
                    '--ckpt_path', host_model_path,
                    '--onnx_path', onnx_path
                ]
                result = subprocess.run(convert_cmd, capture_output=True, text=True, cwd='/app')
                
                if result.returncode == 0:
                    print(f"✅ ONNX 모델이 저장되었습니다: {onnx_path}")
                else:
                    print(f"⚠️ ONNX 변환 실패: {result.stderr}")
                    print("PyTorch 모델만 사용합니다.")
            except Exception as e:
                print(f"⚠️ ONNX 변환 실행 실패: {str(e)}")
                print("PyTorch 모델만 사용합니다.")

            # Engine의 test 메소드가 자동으로 테스트 결과 생성
            print("모델 테스트 및 결과 생성 중...")
            test_results = engine.test(datamodule=datamodule, model=model)
            print("테스트 완료:", test_results)
            
            # 생성된 테스트 결과 이미지들을 host/models 디렉토리로 복사
            images_source_dir = os.path.join(result_dir, "AI", recipe_name)
            
            # v0, v1, latest 등의 버전 폴더 찾기
            version_dirs = []
            if os.path.exists(images_source_dir):
                for item in os.listdir(images_source_dir):
                    version_path = os.path.join(images_source_dir, item)
                    if os.path.isdir(version_path) and os.path.exists(os.path.join(version_path, "images")):
                        version_dirs.append(version_path)
            
            if version_dirs:
                # 최신 버전 디렉토리 선택 (가장 최근에 수정된 것)
                latest_version_dir = max(version_dirs, key=os.path.getmtime)
                images_dir = os.path.join(latest_version_dir, "images")
                
                if os.path.exists(images_dir):
                    # host/models/{recipe_name}/test_results/ 디렉토리 생성
                    test_results_dir = os.path.join(host_model_dir, "test_results")
                    os.makedirs(test_results_dir, exist_ok=True)
                    
                    # 모든 이미지 파일을 복사
                    import shutil
                    copied_count = 0
                    for root, dirs, files in os.walk(images_dir):
                        for file in files:
                            if file.lower().endswith(('.bmp', '.jpg', '.png', '.jpeg')):
                                src_path = os.path.join(root, file)
                                dst_path = os.path.join(test_results_dir, file)
                                shutil.copy2(src_path, dst_path)
                                copied_count += 1
                    
                    print(f"테스트 결과 이미지 {copied_count}개가 {test_results_dir}에 복사되었습니다.")
                else:
                    print(f"이미지 디렉토리를 찾을 수 없습니다: {images_dir}")
            else:
                print(f"버전 디렉토리를 찾을 수 없습니다: {images_source_dir}")
            
            print(f"학습 완료! 모델이 {host_model_path}에 저장되었습니다.")
            print(f"TensorBoard 로그는 '{result_dir}'에 저장되었습니다.")
            
            result_info = {
                "status": "success",
                "recipe_name": recipe_name,
                "training_samples": len(datamodule.train_data.samples),
                "test_samples": len(datamodule.test_data.samples),
                "test_results": test_results,
                "model_path": host_model_path,
                "result_dir": result_dir
            }
            
        else:
            result_info = {
                "status": "error",
                "message": "체크포인트 파일을 찾을 수 없습니다."
            }
    
    except Exception as e:
        print(f"학습 중 오류 발생: {str(e)}")
        result_info = {
            "status": "error",
            "message": str(e)
        }
    
    return result_info

def main():
    parser = argparse.ArgumentParser(description='AI 학습')
    parser.add_argument('--recipe_name', type=str, required=True, 
                       help='레시피 이름 (데이터셋 폴더 이름)')
    
    args = parser.parse_args()
    
    # 데이터셋 경로 확인
    dataset_path = f"/app/host/data/{args.recipe_name}"
    if not os.path.exists(dataset_path):
        print(f"❌ 데이터셋을 찾을 수 없습니다: {dataset_path}")
        sys.exit(1)
    
    result = train_ai_model(args.recipe_name)
    
    if result["status"] == "success":
        print(f"✅ 학습 성공: {args.recipe_name}")
        sys.exit(0)
    else:
        print(f"❌ 학습 실패: {result.get('message', 'Unknown error')}")
        sys.exit(1)

if __name__ == "__main__":
    main()
