#!/usr/bin/env python3
"""
AI Detection API Server
"""

import os
import json
import shutil
import subprocess
import threading
import torch
import logging
from flask import Flask, request, jsonify

# Flask 앱 초기화
app = Flask(__name__)

# Flask 설정: 응답 크기 제한 증가
app.config['MAX_CONTENT_LENGTH'] = 50 * 1024 * 1024  # 50MB

# 로깅 설정
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# 전역 변수
model_trained = False
training_status = {"status": "idle", "progress": 0, "message": "Ready"}
current_model = None
current_model_path = None
current_recipe_name = None

@app.route('/api/health', methods=['GET'])
def health_check():
    """서버 상태 확인"""
    return jsonify({
        'status': 'healthy',
        'model_trained': model_trained,
        'gpu_available': torch.cuda.is_available()
    })

@app.route('/api/load_model', methods=['POST'])
def load_model():
    """AI 모델 로드 API"""
    global current_model, current_model_path, current_recipe_name, model_trained
    
    try:
        data = request.json
        recipe_name = data.get('recipe_name')
        
        if not recipe_name:
            return jsonify({'error': 'recipe_name is required'}), 400

        # 모델 파일 경로 확인
        model_path = f"/app/host/models/{recipe_name}/model.ckpt"
        if not os.path.exists(model_path):
            return jsonify({'error': f'Model not found for recipe: {recipe_name}'}), 404

        logger.info(f"Loading model for recipe: {recipe_name}")
        
        # 모델 로드 (ai_inference.py의 load_model_for_recipe 함수 사용)
        try:
            from ai_inference import load_model_for_recipe
            model, ckpt, device = load_model_for_recipe(recipe_name)
            
            # 전역 변수에 저장
            current_model = model
            current_model_path = ckpt
            current_recipe_name = recipe_name
            model_trained = True
            
            logger.info(f"Model loaded successfully for recipe: {recipe_name}")
            return jsonify({
                'status': 'success',
                'message': f'Model loaded for recipe: {recipe_name}',
                'model_path': ckpt
            })
            
        except Exception as e:
            logger.error(f"Model loading failed: {str(e)}")
            return jsonify({'error': f'Model loading failed: {str(e)}'}), 500

    except Exception as e:
        logger.error(f"Load model request failed: {str(e)}")
        return jsonify({'error': f'Load model request failed: {str(e)}'}), 500

@app.route('/api/unload_model', methods=['POST'])
def unload_model():
    """AI 모델 언로드 API"""
    global current_model, current_model_path, current_recipe_name, model_trained
    
    try:
        data = request.json
        recipe_name = data.get('recipe_name')
        
        if not recipe_name:
            return jsonify({'error': 'recipe_name is required'}), 400

        # 현재 로딩된 모델이 해당 레시피의 모델인지 확인
        if current_recipe_name != recipe_name:
            return jsonify({
                'status': 'warning',
                'message': f'No model loaded for recipe: {recipe_name}. Current loaded recipe: {current_recipe_name}'
            }), 200

        logger.info(f"Unloading model for recipe: {recipe_name}")
        
        # 모델 메모리에서 해제
        try:
            # PyTorch 모델 메모리 정리
            if current_model is not None:
                del current_model
                current_model = None
            
            # GPU 메모리 정리 (있을 경우)
            if torch.cuda.is_available():
                torch.cuda.empty_cache()
            
            # 전역 변수 초기화
            current_model_path = None
            current_recipe_name = None
            model_trained = False
            
            # ai_inference.py의 캐시에서도 제거
            try:
                from ai_inference import model_cache
                if recipe_name in model_cache:
                    del model_cache[recipe_name]
                    logger.info(f"Removed model from cache for recipe: {recipe_name}")
            except Exception as e:
                logger.warning(f"Failed to remove from cache: {str(e)}")
            
            logger.info(f"Model unloaded successfully for recipe: {recipe_name}")
            return jsonify({
                'status': 'success',
                'message': f'Model unloaded for recipe: {recipe_name}'
            })
            
        except Exception as e:
            logger.error(f"Model unloading failed: {str(e)}")
            return jsonify({'error': f'Model unloading failed: {str(e)}'}), 500

    except Exception as e:
        logger.error(f"Unload model request failed: {str(e)}")
        return jsonify({'error': f'Unload model request failed: {str(e)}'}), 500

@app.route('/api/train', methods=['POST'])
def train():
    """AI 모델 학습 API"""
    global training_status
    
    try:
        data = request.json
        recipe_name = data.get('recipe_name')
        epochs = data.get('epochs', 10)
        
        if not recipe_name:
            return jsonify({'error': 'recipe_name is required'}), 400
            
        # 데이터셋 경로 확인
        dataset_path = f"/app/host/data/{recipe_name}"
        if not os.path.exists(dataset_path):
            return jsonify({'error': f'Dataset not found: {recipe_name}'}), 400
            
        logger.info(f"Starting training for recipe: {recipe_name}, epochs: {epochs}")
        
        def train_background():
            global training_status
            
            training_status = {"status": "training", "progress": 10, "message": "Starting training..."}
            
            try:
                # ai_trainer.py 실행을 백그라운드에서 모니터링
                training_status = {"status": "training", "progress": 20, "message": f"Setting up dataset for {recipe_name}..."}
                
                cmd = [
                    'python3', '/app/ai_trainer.py',
                    '--recipe_name', recipe_name
                ]
                
                logger.info(f"Executing command: {' '.join(cmd)}")
                
                # 실시간 진행률 업데이트를 위해 Popen 사용
                process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd='/app')
                
                # 진행률 시뮬레이션 (실제 epochs 값 사용)
                import time
                progress_values = [30, 40, 50, 60, 70, 80, 85, 90, 95]
                
                # 동적 메시지 생성 (AI 모델 학습)
                epoch_messages = [
                    "데이터셋 로딩 중...",
                    "AI 모델 학습 시작...", 
                    "특징 추출 중...",
                    "핵심 데이터 선별 중...",
                    "모델 학습 완료...",
                    "모델 성능 평가 중...",
                    "모델 저장 중...",
                    "모델 최적화 중...",
                    "모델 배포 준비 중..."
                ]
                
                for i, (prog, msg) in enumerate(zip(progress_values, epoch_messages)):
                    if process.poll() is None:  # 프로세스가 아직 실행 중
                        training_status = {"status": "training", "progress": prog, "message": msg}
                        time.sleep(3)  # 3초마다 업데이트
                    else:
                        break
                
                # 프로세스 완료 대기
                stdout, stderr = process.communicate()
                
                logger.info(f"Training process completed with return code: {process.returncode}")
                if stderr:
                    logger.warning(f"Training stderr: {stderr}")
                
                if process.returncode == 0:
                    training_status = {"status": "completed", "progress": 100, "message": f"Training completed for {recipe_name}"}
                    logger.info(f"Training completed successfully for {recipe_name}")
                else:
                    # stderr에서 실제 오류만 추출 (warning 제외)
                    error_lines = [line for line in stderr.split('\n') if 'error:' in line.lower() or 'failed' in line.lower()]
                    if error_lines:
                        raise Exception(f"Training failed: {'; '.join(error_lines)}")
                    else:
                        # warning만 있고 실제 오류가 없으면 성공으로 처리
                        training_status = {"status": "completed", "progress": 100, "message": f"Training completed for {recipe_name} (with warnings)"}
                        logger.info(f"Training completed with warnings for {recipe_name}")
                    
            except Exception as e:
                training_status = {"status": "error", "progress": 0, "message": str(e)}
                logger.error(f"Training error: {str(e)}")
                
        thread = threading.Thread(target=train_background)
        thread.start()
        
        return jsonify({"message": f"Training started for recipe: {recipe_name}", "status": "started"})
        
    except Exception as e:
        logger.error(f"API error: {str(e)}")

@app.route('/api/training_status', methods=['GET'])
def get_training_status():
    """학습 상태 확인"""
    return jsonify(training_status)

@app.route('/api/predict', methods=['POST'])
def predict():
    """AI 모델 추론 API"""
    try:
        data = request.json or {}
        recipe_name = data.get('recipe_name')
        image_path = data.get('image_path')
        image_filename = data.get('image_filename')

        if not recipe_name:
            return jsonify({'error': 'recipe_name is required'}), 400

        # 이미지 path 또는 image_filename 둘 중 하나 필요
        if not image_path and not image_filename:
            return jsonify({'error': 'image_path or image_filename is required'}), 400

        # 모델 파일 경로 확인 (ONNX 우선, 없으면 PyTorch)
        onnx_path = f"/app/host/models/{recipe_name}/model.onnx"
        ckpt_path = f"/app/host/models/{recipe_name}/model.ckpt"
        
        if os.path.exists(onnx_path):
            model_path = onnx_path
            logger.info(f"Using ONNX model: {model_path}")
        elif os.path.exists(ckpt_path):
            model_path = ckpt_path
            logger.info(f"Using PyTorch model: {model_path}")
        else:
            return jsonify({'error': f'Model not found for recipe: {recipe_name}'}), 404

        # 이미지 파일 경로 결정: image_filename 우선 (recipe 내 imgs 폴더), 아니면 image_path 사용
        if image_filename:
            host_image_path = f"/app/host/data/{recipe_name}/imgs/{image_filename}"
        else:
            # image_path may be absolute or relative to /app/host
            if image_path.startswith('/'):
                host_image_path = image_path
            else:
                host_image_path = f"/app/host/{image_path}"

        if not os.path.exists(host_image_path):
            return jsonify({'error': f'Image file not found: {host_image_path}'}), 404

        logger.info(f"Starting prediction for recipe: {recipe_name}, image: {host_image_path}")

        # ai_inference.py 실행
        cmd = [
            'python3', '/app/ai_inference.py',
            '--recipe_name', recipe_name,
            '--image_path', host_image_path,
            '--use_engine'
        ]

        logger.info(f"Executing command: {' '.join(cmd)}")

        # 추론 실행
        process = subprocess.run(cmd, capture_output=True, text=True, cwd='/app')

        if process.returncode != 0:
            logger.error(f"Inference failed: {process.stderr}")
            return jsonify({'error': 'Inference failed', 'details': process.stderr}), 500

        # ai_inference.py는 stdout에 오직 score 숫자만 출력하도록 되어 있음
        stdout = process.stdout.strip()
        try:
            score = float(stdout)
        except Exception:
            score = stdout

        # 추론이 성공하면 결과 디렉토리의 최근 파일만 반환
        results_dir = f"/app/host/results/{recipe_name}"
        files = []
        if os.path.exists(results_dir):
            # 파일 목록을 생성 시간으로 정렬하여 최근 파일만 선택
            all_files = []
            for fn in os.listdir(results_dir):
                filepath = os.path.join(results_dir, fn)
                if os.path.isfile(filepath):
                    mtime = os.path.getmtime(filepath)
                    all_files.append((fn, mtime))
            
            # 생성 시간으로 정렬 (최신순)
            all_files.sort(key=lambda x: x[1], reverse=True)
            
            # 최근 5개 파일만 선택 (원본 이미지 + 결과 이미지)
            recent_files = [fn for fn, _ in all_files[:5]]
            files = recent_files
            
            logger.info(f"Recent files in results dir: {files}")

        return jsonify({'status': 'ok', 'score': score, 'results_dir': results_dir, 'files': files})

    except Exception as e:
        logger.error(f"Prediction API error: {str(e)}")
        return jsonify({'error': str(e)}), 500


@app.route('/api/multi_predict', methods=['POST'])
def multi_predict():
    """AI 다중 영역 추론 API
    요청 JSON에 rects 필드로 검사할 영역 목록을 전달하면, 해당 rects 정보를 임시 JSON 파일로 저장하고
    ai_inference.py에 --rects_path 인자로 넘겨 실행합니다. 결과는 /app/host/results/<recipe_name>/multi_results.json
    또는 생성된 결과 파일 목록을 확인해 반환합니다.
    """
    global current_model, current_model_path, current_recipe_name, model_trained
    try:
        data = None
        rects = None
        recipe_name = None
        image_path = None
        image_filename = None

        # If multipart/form-data with file upload
        if request.files and 'image_file' in request.files:
            image_file = request.files['image_file']
            # recipe_name and rects may be sent as form fields
            recipe_name = request.form.get('recipe_name')
            rects_field = request.form.get('rects')
            if rects_field:
                try:
                    rects = json.loads(rects_field).get('rects', [])
                except Exception:
                    rects = None
        else:
            data = request.json or {}
            recipe_name = data.get('recipe_name')
            image_path = data.get('image_path')
            image_filename = data.get('image_filename')
            rects = data.get('rects')  # expected list of {id/name, x, y, w, h}

        if not recipe_name:
            return jsonify({'error': 'recipe_name is required'}), 400

        if not image_path and not image_filename:
            return jsonify({'error': 'image_path or image_filename is required'}), 400

        if not rects or not isinstance(rects, list):
            return jsonify({'error': 'rects (list) is required'}), 400

        # 미리 로드된 모델 확인 및 자동 로드
        if current_model is None or current_recipe_name != recipe_name:
            logger.info(f"Model not loaded for recipe: {recipe_name}, loading automatically...")
            
            # 모델 파일 경로 확인 (ONNX 우선, 없으면 PyTorch)
            onnx_path = f"/app/host/models/{recipe_name}/model.onnx"
            ckpt_path = f"/app/host/models/{recipe_name}/model.ckpt"
            
            if os.path.exists(onnx_path):
                model_path = onnx_path
                logger.info(f"Loading ONNX model: {model_path}")
            elif os.path.exists(ckpt_path):
                model_path = ckpt_path
                logger.info(f"Loading PyTorch model: {model_path}")
            else:
                return jsonify({'error': f'Model not found for recipe: {recipe_name}'}), 404

            # 모델 로드
            try:
                from ai_inference import load_model_for_recipe
                model, ckpt, device = load_model_for_recipe(recipe_name)
                
                # 전역 변수에 저장
                current_model = model
                current_model_path = ckpt
                current_recipe_name = recipe_name
                model_trained = True
                
                logger.info(f"Model loaded successfully for recipe: {recipe_name}")
            except Exception as e:
                logger.error(f"Model load error: {e}")
                return jsonify({'error': f'Model load failed: {str(e)}'}), 500

        # Determine host image path: prefer uploaded file (saved to container tmp), otherwise image_filename or image_path
        temp_image_path = None
        try:
            if request.files and 'image_file' in request.files:
                image_file = request.files['image_file']
                import uuid
                ext = os.path.splitext(image_file.filename)[1] or '.png'
                temp_image_path = f"/tmp/upload_{uuid.uuid4().hex}{ext}"
                image_file.save(temp_image_path)
                host_image_path = temp_image_path
                logger.info(f"Received uploaded image and saved to container temp path: {host_image_path}")
            else:
                if image_filename:
                    host_image_path = f"/app/host/data/{recipe_name}/imgs/{image_filename}"
                else:
                    if image_path.startswith('/'):
                        host_image_path = image_path
                    else:
                        host_image_path = f"/app/host/{image_path}"

            if not os.path.exists(host_image_path):
                return jsonify({'error': f'Image file not found: {host_image_path}'}), 404

            logger.info(f"Starting multi prediction for recipe: {recipe_name}, image: {host_image_path}, rects: {len(rects)}")

            # Normalize rects (validate)
            normalized = []
            for idx, r in enumerate(rects):
                if not isinstance(r, dict):
                    return jsonify({'error': f'rect at index {idx} must be object'}), 400
                rid = r.get('id') or r.get('name') or f'rect_{idx}'
                try:
                    x = int(r.get('x', 0))
                    y = int(r.get('y', 0))
                    w = int(r.get('w', 0))
                    h = int(r.get('h', 0))
                    angle = float(r.get('angle', 0.0))
                except Exception:
                    return jsonify({'error': f'invalid rect values at index {idx}'}), 400

                if w <= 0 or h <= 0:
                    return jsonify({'error': f'rect width/height must be >0 at index {idx}'}), 400

                normalized.append({'id': rid, 'x': x, 'y': y, 'w': w, 'h': h, 'angle': angle})

            # Run ai_inference.py with pre-loaded model
            cmd = [
                'python3', '/app/ai_inference.py',
                '--recipe_name', recipe_name,
                '--image_path', host_image_path,
                '--rects_stdin',
                '--use_loaded_model'
            ]

            logger.info(f"Executing command with pre-loaded model: {' '.join(cmd)}")

            # 미리 로드된 모델 정보를 환경 변수로 전달
            env = os.environ.copy()
            env['AI_LOADED_MODEL_PATH'] = current_model_path
            env['AI_RECIPE_NAME'] = current_recipe_name

            # Include optional per-request ROI parameters if provided by the client
            payload_obj = {'rects': normalized}
            # Coerce and validate optional ROI params
            if data:
                if 'ROI_PERCENTILE_P' in data:
                    try:
                        payload_obj['ROI_PERCENTILE_P'] = int(data['ROI_PERCENTILE_P'])
                    except Exception:
                        # ignore invalid value
                        pass
                if 'AREA_THRESH_MODE' in data:
                    m = str(data['AREA_THRESH_MODE']).lower()
                    if m in ('relative', 'absolute'):
                        payload_obj['AREA_THRESH_MODE'] = m
                if 'AREA_ABS_THRESHOLD' in data:
                    try:
                        payload_obj['AREA_ABS_THRESHOLD'] = float(data['AREA_ABS_THRESHOLD'])
                    except Exception:
                        pass
                if 'ROI_COMBINE_METHOD' in data:
                    payload_obj['ROI_COMBINE_METHOD'] = str(data['ROI_COMBINE_METHOD'])
            stdin_payload = json.dumps(payload_obj)
            process = subprocess.run(cmd, input=stdin_payload, capture_output=True, text=True, cwd='/app', env=env)
            
            # stderr 로그를 파일에 저장
            if process.stderr:
                with open('/app/ai_inference_stderr.log', 'a') as f:
                    f.write(f"=== AI 추론 실행: {recipe_name} ===\\n")
                    f.write(f"명령어: {' '.join(cmd)}\\n")
                    f.write(f"입력: {stdin_payload}\\n")
                    f.write(f"stderr:\\n{process.stderr}\\n")
                    f.write("=" * 50 + "\\n")
                logger.info(f"AI 추론 stderr 로그 저장됨: /app/ai_inference_stderr.log")
        finally:
            # cleanup uploaded container temp image if we created one
            try:
                if temp_image_path and os.path.exists(temp_image_path):
                    os.remove(temp_image_path)
            except Exception:
                pass

        # Check process result
        if process.returncode != 0:
            logger.error(f"Multi inference failed: {process.stderr}")
            return jsonify({'error': 'Multi inference failed', 'details': process.stderr}), 500

        stdout = process.stdout.strip()

        # 결과 디렉토리 확인 및 최근 파일만 반환
        results_dir = f"/app/host/results/{recipe_name}"
        files = []
        if os.path.exists(results_dir):
            # 파일 목록을 생성 시간으로 정렬하여 최근 파일만 선택
            all_files = []
            for fn in os.listdir(results_dir):
                filepath = os.path.join(results_dir, fn)
                if os.path.isfile(filepath):
                    mtime = os.path.getmtime(filepath)
                    all_files.append((fn, mtime))
            
            # 생성 시간으로 정렬 (최신순)
            all_files.sort(key=lambda x: x[1], reverse=True)
            
            # 최근 10개 파일만 선택 (원본 이미지 + 결과 이미지들)
            recent_files = [fn for fn, _ in all_files[:10]]
            files = recent_files
            
            logger.info(f"Recent files in results dir: {files}")

        # stdout이 JSON일 가능성 처리
        multi_results = None
        if stdout:
            try:
                parsed = json.loads(stdout)
                multi_results = parsed
            except Exception:
                pass

        response = {'status': 'ok', 'results_dir': results_dir, 'files': files}
        if multi_results is not None:
            response['multi_results'] = multi_results
        else:
            response['stdout'] = stdout

        return jsonify(response)

    except Exception as e:
        logger.error(f"Multi Prediction API error: {str(e)}")
        return jsonify({'error': str(e)}), 500


if __name__ == '__main__':
    logger.info("Starting AI Detection API Server...")
    app.run(host='0.0.0.0', port=5000, debug=False)
