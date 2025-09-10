#!/usr/bin/env python3
"""Single-image anomaly inference."""

import os
import sys
import cv2
import numpy as np
import torch
import argparse
import json
import time
from anomalib.models import Patchcore
try:
    from anomalib.engine import Engine
except Exception:
    Engine = None
import contextlib
try:
    import onnxruntime as ort
    ONNX_AVAILABLE = True
except ImportError:
    ONNX_AVAILABLE = False
    print("ONNX Runtime not available. Using PyTorch models only.", file=sys.stderr)


GAMMA = 1.0
ALPHA = 0.5
OVERLAY_DARKEN = 0.9


def run_onnx_inference(onnx_session, image_path):
    """ONNX 모델을 사용한 추론"""
    img_bgr = cv2.imread(image_path)
    if img_bgr is None:
        raise FileNotFoundError(f'Could not read image: {image_path}')

    # Determine ONNX model expected input spatial size and resize only if model requires it.
    # onnx_session.get_inputs()[0].shape typically is [batch, channels, height, width]
    inp = onnx_session.get_inputs()[0]
    shape = list(inp.shape)
    expected_h = None
    expected_w = None
    try:
        # Try to extract height/width from shape (handle None or symbolic dims)
        if len(shape) >= 4:
            expected_h = int(shape[2]) if shape[2] not in (None, 'None') else None
            expected_w = int(shape[3]) if shape[3] not in (None, 'None') else None
    except Exception:
        expected_h = expected_w = None

    img_for_infer = img_bgr
    if expected_h and expected_w:
        # model expects a fixed spatial size -> resize to that
        if (img_bgr.shape[0], img_bgr.shape[1]) != (expected_h, expected_w):
            img_for_infer = cv2.resize(img_bgr, (expected_w, expected_h), interpolation=cv2.INTER_LINEAR)
            print(f"ONNX: Resized input from {img_bgr.shape[1]}x{img_bgr.shape[0]} to {expected_w}x{expected_h} to match model", file=sys.stderr)
        else:
            print(f"ONNX: Input size matches model expected {expected_w}x{expected_h}", file=sys.stderr)
    else:
        print(f"ONNX: Model input spatial dims are dynamic or unknown, using original image size {img_bgr.shape[1]}x{img_bgr.shape[0]}", file=sys.stderr)

    # Build input tensor from the potentially resized image (img_for_infer)
    img_rgb = cv2.cvtColor(img_for_infer, cv2.COLOR_BGR2RGB)
    input_tensor = img_rgb.astype(np.float32) / 255.0
    input_tensor = np.transpose(input_tensor, (2, 0, 1))  # HWC to CHW
    input_tensor = np.expand_dims(input_tensor, axis=0)  # Add batch dimension

    # ONNX 추론 실행
    ort_inputs = {onnx_session.get_inputs()[0].name: input_tensor}
    ort_outputs = onnx_session.run(None, ort_inputs)

    print(f"ONNX: Raw outputs count: {len(ort_outputs)}", file=sys.stderr)
    for i, output in enumerate(ort_outputs):
        print(f"ONNX: Output {i} shape: {output.shape}, type: {type(output)}, dtype: {getattr(output, 'dtype', 'N/A')}", file=sys.stderr)
        print(f"ONNX: Output {i} sample values: {output.flatten()[:10] if hasattr(output, 'flatten') else output}", file=sys.stderr)

    # 출력에서 anomaly_map 추출 - 2D 출력 우선 사용
    amap_np = None
    for i, output in enumerate(ort_outputs):
        if isinstance(output, np.ndarray) and output.ndim >= 2:
            amap_np = output
            print(f"ONNX: Using output {i} as anomaly_map", file=sys.stderr)
            break
    
    if amap_np is None:
        # 2D 출력이 없으면 첫 번째 출력 사용
        output = ort_outputs[0]
        if isinstance(output, np.ndarray):
            amap_np = output
        else:
            amap_np = np.array(output)
        print(f"ONNX: No 2D output found, using output 0", file=sys.stderr)

    print(f"ONNX: After conversion - shape: {amap_np.shape}, type: {type(amap_np)}, dtype: {amap_np.dtype}", file=sys.stderr)
    print(f"ONNX: Sample values: {amap_np.flatten()[:20] if amap_np.size > 0 else amap_np}", file=sys.stderr)

    # 출력 차원 확인 및 조정
    if amap_np.ndim == 3:  # [batch, height, width] 형태인 경우
        amap_np = amap_np[0]  # batch 차원 제거
    elif amap_np.ndim == 4:  # [batch, channel, height, width] 형태인 경우
        amap_np = amap_np[0, 0]  # batch와 channel 차원 제거
    elif amap_np.ndim == 0:  # 스칼라 값인 경우 (예상치 못한 출력)
        print(f"ONNX: ERROR - Scalar output detected: {amap_np}", file=sys.stderr)
        # 스칼라 값인 경우 원본 이미지 크기를 참조할 수 있으면 사용, 없으면 64x64 기본
        fallback_size = (img_bgr.shape[0], img_bgr.shape[1]) if img_bgr is not None else (64, 64)
        amap_np = np.full((fallback_size[0], fallback_size[1]), float(amap_np), dtype=np.float32)
    elif amap_np.ndim == 1:  # 1D 배열인 경우
        print(f"ONNX: ERROR - 1D output detected, converting to 2D", file=sys.stderr)
        size = int(np.sqrt(len(amap_np)))
        if size * size == len(amap_np):
            amap_np = amap_np.reshape(size, size)
        else:
            fallback_size = (img_bgr.shape[0], img_bgr.shape[1]) if img_bgr is not None else (64, 64)
            amap_np = np.full((fallback_size[0], fallback_size[1]), np.mean(amap_np), dtype=np.float32)
    else:
        print(f"ONNX: ERROR - Unexpected dimensions: {amap_np.ndim}", file=sys.stderr)
        # 예상치 못한 차원의 경우 원본 이미지 크기를 참조할 수 있으면 사용, 없으면 64x64 기본
        fallback_h, fallback_w = (img_bgr.shape[0], img_bgr.shape[1]) if img_bgr is not None else (64, 64)
        amap_np = np.full((fallback_h, fallback_w), 0.5, dtype=np.float32)

    # 최종 2D 확인 및 강제 변환
    if amap_np.ndim != 2:
        print(f"ONNX: FINAL ERROR - Still not 2D! Shape: {amap_np.shape}, forcing to 2D", file=sys.stderr)
        fallback_h, fallback_w = (img_bgr.shape[0], img_bgr.shape[1]) if img_bgr is not None else (64, 64)
        amap_np = np.full((fallback_h, fallback_w), np.mean(amap_np) if np.size(amap_np) > 0 else 0.5, dtype=np.float32)

    print(f"ONNX: Final output shape: {amap_np.shape}", file=sys.stderr)
    return amap_np


def make_overlay_from_map(amap_np, orig_bgr=None):
    try:
        amap = np.asarray(amap_np).squeeze()
        print(f"Heatmap: input shape {np.array(amap_np).shape}, after squeeze {amap.shape}", file=sys.stderr)

        # 차원 확인 및 처리
        if amap.ndim == 0:  # 스칼라 값
            print(f"Heatmap: Scalar value detected {amap}, creating uniform map", file=sys.stderr)
            # 원본 이미지 크기 정보가 있으면 그 크기로, 없으면 64x64 기본
            if orig_bgr is not None:
                amap = np.full((orig_bgr.shape[0], orig_bgr.shape[1]), float(amap), dtype=np.float32)
            else:
                amap = np.full((64, 64), float(amap), dtype=np.float32)
        elif amap.ndim == 1:  # 1D 배열
            print(f"Heatmap: 1D array detected, converting to 2D", file=sys.stderr)
            size = int(np.sqrt(len(amap)))
            if size * size == len(amap):
                amap = amap.reshape(size, size)
            else:
                if orig_bgr is not None:
                    amap = np.full((orig_bgr.shape[0], orig_bgr.shape[1]), np.mean(amap), dtype=np.float32)
                else:
                    amap = np.full((64, 64), np.mean(amap), dtype=np.float32)
        elif amap.ndim > 2:  # 3D 이상
            print(f"Heatmap: High dimensional array {amap.ndim}D, taking first 2D slice", file=sys.stderr)
            amap = amap.reshape(amap.shape[-2:]) if amap.ndim >= 2 else amap
        elif amap.ndim == 2:
            print(f"Heatmap: Perfect! Already 2D shape: {amap.shape}", file=sys.stderr)

        # 최종 2D 확인
        if amap.ndim != 2:
            print(f"Heatmap: Still not 2D after processing! Shape: {amap.shape}, forcing to 2D", file=sys.stderr)
            if orig_bgr is not None:
                amap = np.full((orig_bgr.shape[0], orig_bgr.shape[1]), np.mean(amap) if amap.size > 0 else 0.5, dtype=np.float32)
            else:
                amap = np.full((64, 64), np.mean(amap) if amap.size > 0 else 0.5, dtype=np.float32)

        amap = np.clip(amap, 0.0, 1.0)
        amap_vis = np.power(amap, GAMMA)
        norm_map = (np.clip(amap_vis, 0.0, 1.0) * 255.0).astype(np.uint8)
        heatmap = cv2.applyColorMap(norm_map, cv2.COLORMAP_JET)

        if orig_bgr is None:
            bg = np.zeros_like(heatmap)
        else:
            bg = orig_bgr
            if (bg.shape[0], bg.shape[1]) != (heatmap.shape[0], heatmap.shape[1]):
                heatmap = cv2.resize(heatmap, (bg.shape[1], bg.shape[0]))
        dark = (bg.astype(np.float32) * OVERLAY_DARKEN).clip(0, 255).astype(np.uint8)
        return cv2.addWeighted(dark, 1 - ALPHA, heatmap, ALPHA, 0)

    except Exception as e:
        print(f"Heatmap generation error: {e}", file=sys.stderr)
        # 에러 발생 시 기본 히트맵 생성
        try:
            if orig_bgr is not None:
                size_h, size_w = orig_bgr.shape[0], orig_bgr.shape[1]
                amap = np.full((size_h, size_w), 0.5, dtype=np.float32)
            else:
                size = 64
                amap = np.full((size, size), 0.5, dtype=np.float32)  # 중간 값으로 채움
            amap_vis = np.power(amap, GAMMA)
            norm_map = (np.clip(amap_vis, 0.0, 1.0) * 255.0).astype(np.uint8)
            heatmap = cv2.applyColorMap(norm_map, cv2.COLORMAP_JET)
            return heatmap
        except Exception as e2:
            print(f"Fallback heatmap generation also failed: {e2}", file=sys.stderr)
            return None


def save_heatmap(output_dir, image_path, amap_np):
    os.makedirs(output_dir, exist_ok=True)
    base = os.path.basename(image_path)
    name_noext = os.path.splitext(base)[0]
    out_path = os.path.join(output_dir, f"{name_noext}.png")
    orig = cv2.imread(str(image_path)) if os.path.exists(image_path) else None
    overlay = make_overlay_from_map(amap_np, orig_bgr=orig)
    if overlay is not None:
        cv2.imwrite(out_path, overlay)
        print(f"Heatmap saved: {out_path}", file=sys.stderr)
        return out_path
    else:
        print(f"Failed to generate heatmap for {image_path}", file=sys.stderr)
        return None


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def ensure_dir(p):
    os.makedirs(p, exist_ok=True)


def load_rects(path):
    with open(path, 'r', encoding='utf-8') as f:
        data = json.load(f)
    return data.get('rects', [])


def save_heatmap_crop(heat_crop, out_path):
    # heat_crop: float [0..1] 또는 정규화된 값
    heat_crop = np.clip(heat_crop, 0.0, 1.0)

    # 임계값을 기준으로 바이너리 히트맵 생성 (높은 값만 표시)
    threshold = 0.3
    binary_heatmap = (heat_crop > threshold).astype(np.uint8) * 255

    # 바이너리 히트맵을 컬러로 변환 (빨간색으로 표시)
    colored = np.zeros((heat_crop.shape[0], heat_crop.shape[1], 3), dtype=np.uint8)
    colored[binary_heatmap > 0] = [0, 0, 255]  # 빨간색

    # 원본 히트맵 값에 따라 투명도 조절
    alpha_channel = (heat_crop * 255).astype(np.uint8)
    colored_with_alpha = np.dstack([colored, alpha_channel])

    # PNG로 저장하기 위해 BGRA 형식으로 변환
    bgra = cv2.cvtColor(colored, cv2.COLOR_BGR2BGRA)
    bgra[:, :, 3] = alpha_channel  # 알파 채널 설정

    ensure_dir(os.path.dirname(out_path))
    success = cv2.imwrite(out_path, bgra)
    if success:
        print(f"Binary heatmap saved successfully: {out_path}", file=sys.stderr)
    else:
        print(f"Failed to save binary heatmap: {out_path}", file=sys.stderr)


def process_rects_from_amap(amap_np, rects, results_dir, write_heatmaps=True, threshold=50.0, roi_params=None):
    """Process a list of rects on the full anomaly map and return per-rect results.

    Returns a list of dicts: {id,x,y,w,h,angle,score,pct,area,passed,heatmap_file}
    pct = percentile value (P-th percentile * 100)
    area = percent of pixels above threshold * 100
    score = combined metric (by default pct / area)
    """
    h_img, w_img = amap_np.shape[:2]
    heatmaps_dir = None
    if write_heatmaps and results_dir is not None:
        heatmaps_dir = os.path.join(results_dir, "heatmaps")
        ensure_dir(heatmaps_dir)

    results = []

    # 전체 히트맵 통계 (한 번만 출력)
    print("전체 히트맵 통계:", file=sys.stderr)
    print(f"  - amap_np shape: {amap_np.shape}", file=sys.stderr)
    print(f"  - amap_np range: [{amap_np.min():.8f}, {amap_np.max():.8f}]", file=sys.stderr)
    print(f"  - amap_np mean: {amap_np.mean():.8f}", file=sys.stderr)
    print(f"  - amap_np std: {amap_np.std():.8f}", file=sys.stderr)
    print(f"  - threshold: {threshold}%", file=sys.stderr)

    if amap_np.max() > 1.0:
        print("히트맵을 0-1 범위로 정규화합니다.", file=sys.stderr)
        amap_np = amap_np / 255.0

    for r in rects:
        # default fields
        rid = str(r.get("id", r.get("name", "rect")))
        x = int(r.get("x", 0))
        y = int(r.get("y", 0))
        w = int(r.get("w", 0))
        h = int(r.get("h", 0))
        angle = float(r.get("angle", 0.0))

        pct = 0.0
        area = 0.0
        score_percentage = 0.0
        heatfile = None

        try:
            cx = x + w / 2.0
            cy = y + h / 2.0
            box = ((cx, cy), (w, h), angle)
            pts = cv2.boxPoints(box).astype(np.int32)

            xs = pts[:, 0]
            ys = pts[:, 1]
            xmin = clamp(int(xs.min()), 0, w_img - 1)
            xmax = clamp(int(xs.max()), 0, w_img - 1)
            ymin = clamp(int(ys.min()), 0, h_img - 1)
            ymax = clamp(int(ys.max()), 0, h_img - 1)

            if xmax <= xmin or ymax <= ymin:
                print(f"Rect {rid}: invalid bbox after clipping (xmin>=xmax or ymin>=ymax)", file=sys.stderr)
                passed = False
                results.append({"id": rid, "x": x, "y": y, "w": w, "h": h, "angle": angle, "score": 0.0, "pct": 0.0, "area": 0.0, "passed": passed, "heatmap_file": None})
                continue

            heat_crop = amap_np[ymin : ymax + 1, xmin : xmax + 1]
            pts_shift = pts - np.array([xmin, ymin])
            mask = np.zeros_like(heat_crop, dtype=np.uint8)
            cv2.fillPoly(mask, [pts_shift], 1)

            # compute masked values
            masked_vals = heat_crop[mask == 1]
            if masked_vals.size == 0:
                print(f"Rect {rid}: no valid pixels in mask", file=sys.stderr)
                passed = False
                results.append({"id": rid, "x": x, "y": y, "w": w, "h": h, "angle": angle, "score": 0.0, "pct": 0.0, "area": 0.0, "passed": passed, "heatmap_file": None})
                continue

            # 1) percentile P (per-request override -> env -> default)
            try:
                if roi_params and 'ROI_PERCENTILE_P' in roi_params:
                    P = int(roi_params.get('ROI_PERCENTILE_P'))
                else:
                    P = int(os.environ.get("ROI_PERCENTILE_P", "95"))
            except Exception:
                P = 95
            pct_value = float(np.percentile(masked_vals, P))
            pct = pct_value * 100.0

            # 2) area (pixels above threshold)
            # area threshold mode: per-request override -> env -> default
            AREA_THRESH_MODE = None
            if roi_params and 'AREA_THRESH_MODE' in roi_params:
                AREA_THRESH_MODE = str(roi_params.get('AREA_THRESH_MODE')).lower()
            if not AREA_THRESH_MODE:
                AREA_THRESH_MODE = os.environ.get("AREA_THRESH_MODE", "relative")

            if AREA_THRESH_MODE == "relative":
                area_thresh = pct_value
            else:
                try:
                    if roi_params and 'AREA_ABS_THRESHOLD' in roi_params:
                        area_thresh = float(roi_params.get('AREA_ABS_THRESHOLD'))
                    else:
                        area_thresh = float(os.environ.get("AREA_ABS_THRESHOLD", "0.5"))
                except Exception:
                    area_thresh = 0.5
            area_ratio = float(np.sum(masked_vals > area_thresh)) / float(masked_vals.size)
            area = area_ratio * 100.0

            # 3) combine
            # combine method: per-request override -> env -> default
            if roi_params and 'ROI_COMBINE_METHOD' in roi_params:
                ROI_COMBINE_METHOD = str(roi_params.get('ROI_COMBINE_METHOD'))
            else:
                ROI_COMBINE_METHOD = os.environ.get("ROI_COMBINE_METHOD", "max")
            safe_area = area if area > 0.0 else 1e-6
            if ROI_COMBINE_METHOD == "max":
                combined = pct / safe_area
            elif ROI_COMBINE_METHOD == "mean":
                combined = (pct + area) / 2.0
            else:
                combined = pct / safe_area
            score_percentage = combined

            # short structured debug line and JSON-line for machine parsing
            passed = score_percentage >= threshold
            print(f"Rect {rid} -> pct={pct:.2f}%, area={area:.2f}%, score={score_percentage:.4f}, passed={passed}", file=sys.stderr)
            try:
                rect_log = {
                    "rect_id": rid,
                    "pct": round(pct, 2),
                    "area": round(area, 2),
                    "score": round(score_percentage, 6),
                    "passed": bool(passed)
                }
                print(json.dumps({"rect_log": rect_log}), file=sys.stderr)
            except Exception:
                # If JSON serialization fails, ignore and continue
                pass

        except Exception as e:
            print(f"Error processing rect {rid}: {e}", file=sys.stderr)
            score_percentage = 0.0
            pct = 0.0
            area = 0.0

        passed = score_percentage >= threshold
        results.append({"id": rid, "x": x, "y": y, "w": w, "h": h, "angle": angle, "score": score_percentage, "pct": pct, "area": area, "passed": passed, "heatmap_file": heatfile})

    return results


import sys
import os
import json
import numpy as np
import cv2
import torch
import contextlib
import argparse

# anomalib Patchcore import 시도 (여러 가지 방법)
try:
    from anomalib.models.patchcore import Patchcore
    print("Successfully imported Patchcore from anomalib.models.patchcore", file=sys.stderr)
except ImportError as e:
    print(f"Failed to import from anomalib.models.patchcore: {e}", file=sys.stderr)
    try:
        from anomalib.models import Patchcore
        print("Successfully imported Patchcore from anomalib.models", file=sys.stderr)
    except ImportError as e:
        print(f"Failed to import from anomalib.models: {e}", file=sys.stderr)
        try:
            # anomalib이 설치되어 있는지 확인
            import anomalib
            print(f"anomalib version: {anomalib.__version__}", file=sys.stderr)
            print(f"anomalib path: {anomalib.__file__}", file=sys.stderr)
            # anomalib의 구조 확인
            import pkgutil
            import anomalib.models
            print("Available models in anomalib.models:", file=sys.stderr)
            for importer, modname, ispkg in pkgutil.iter_modules(anomalib.models.__path__):
                print(f"  - {modname}", file=sys.stderr)
        except ImportError as e:
            print(f"anomalib not found: {e}", file=sys.stderr)
        raise ImportError("Could not import Patchcore. Please check anomalib installation.")

# 모델 캐시를 위한 전역 변수
model_cache = {}

def load_model_for_recipe(recipe_name):
    # 캐시에 모델이 있으면 재사용
    if recipe_name in model_cache:
        print(f"🎯 CACHE HIT: Using cached model for recipe: {recipe_name}", file=sys.stderr)
        print(f"Cache status: {list(model_cache.keys())}", file=sys.stderr)
        return model_cache[recipe_name]['model'], model_cache[recipe_name]['ckpt'], model_cache[recipe_name]['device']

    print(f"🔄 CACHE MISS: Loading new model for recipe: {recipe_name}", file=sys.stderr)

    # ONNX 모델 우선 시도
    onnx_path = f"/app/host/models/{recipe_name}/model.onnx"
    if ONNX_AVAILABLE and os.path.exists(onnx_path):
        print(f"🚀 Loading ONNX model for recipe {recipe_name} from {onnx_path}", file=sys.stderr)
        try:
            # ONNX 런타임 세션 생성
            providers = ['CUDAExecutionProvider', 'CPUExecutionProvider'] if ort.get_device() == 'GPU' else ['CPUExecutionProvider']
            ort_session = ort.InferenceSession(onnx_path, providers=providers)

            # 모델을 캐시에 저장
            model_cache[recipe_name] = {
                'model': ort_session,
                'ckpt': onnx_path,
                'device': 'onnx',
                'is_onnx': True
            }
            print(f"✅ ONNX model cached for recipe: {recipe_name}", file=sys.stderr)
            print(f"Cache status: {list(model_cache.keys())}", file=sys.stderr)

            return ort_session, onnx_path, 'onnx'
        except Exception as e:
            print(f"⚠️ ONNX model loading failed: {str(e)}, falling back to PyTorch", file=sys.stderr)

    # PyTorch 모델 로딩 (fallback)
    candidates = [
        f"/app/host/models/{recipe_name}/model.ckpt",
        f"/models/{recipe_name}/model.ckpt",
        f"/app/host/models/{recipe_name}/latest/weights/lightning/model.ckpt",
    ]
    ckpt = next((p for p in candidates if os.path.exists(p)), candidates[0])
    print(f"Loading PyTorch model for recipe {recipe_name} from {ckpt}", file=sys.stderr)
    model = Patchcore.load_from_checkpoint(ckpt)
    device = "cuda" if torch.cuda.is_available() else "cpu"
    model.to(device)
    model.eval()

    # 모델을 캐시에 저장
    model_cache[recipe_name] = {
        'model': model,
        'ckpt': ckpt,
        'device': device,
        'is_onnx': False
    }
    print(f"✅ PyTorch model cached for recipe: {recipe_name}", file=sys.stderr)
    print(f"Cache status: {list(model_cache.keys())}", file=sys.stderr)

    return model, ckpt, device


def run_per_image_forward(model, device, image_path):
    # ONNX 모델인 경우
    if device == 'onnx':
        return run_onnx_inference(model, image_path)

    # PyTorch 모델인 경우 (기존 로직)
    img_bgr = cv2.imread(image_path)
    if img_bgr is None:
        raise FileNotFoundError(f'Could not read image: {image_path}')

    # Do not force resize here; preserve original image size
    print(f"PyTorch: Using original image size {img_bgr.shape[1]}x{img_bgr.shape[0]}", file=sys.stderr)

    img_rgb = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2RGB)
    input_tensor = torch.from_numpy(img_rgb).permute(2, 0, 1).float().unsqueeze(0) / 255.0
    input_tensor = input_tensor.to(device)
    with torch.no_grad():
        output = model(input_tensor)

    amap = None
    if hasattr(output, 'anomaly_map'):
        amap = getattr(output, 'anomaly_map')
    elif isinstance(output, dict) and 'anomaly_map' in output:
        amap = output['anomaly_map']
    elif hasattr(output, 'predictions'):
        preds = getattr(output, 'predictions')
        if isinstance(preds, (list, tuple)) and preds:
            first = preds[0]
            if hasattr(first, 'anomaly_map'):
                amap = getattr(first, 'anomaly_map')

    if amap is None:
        raise RuntimeError('No anomaly_map in model output')
    try:
        amap_np = amap.squeeze().cpu().numpy() if hasattr(amap, 'cpu') else np.array(amap)
    except Exception:
        amap_np = np.array(amap)
    return amap_np


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--recipe_name', required=True)
    parser.add_argument('--image_path', required=True)
    parser.add_argument('--use_engine', action='store_true')
    parser.add_argument('--use_loaded_model', action='store_true', help='Use pre-loaded model from environment variables')
    parser.add_argument('--rects_path', required=False, help='Path to rects JSON (host path)')
    parser.add_argument('--rects_stdin', action='store_true', help='Read rects JSON from stdin')
    args = parser.parse_args()

    recipe = args.recipe_name
    image_path = args.image_path
    use_loaded_model = args.use_loaded_model

    output_dir = f"/app/host/results/{recipe}"
    os.makedirs(output_dir, exist_ok=True)

    try:
        if use_loaded_model:
            # 미리 로드된 모델 사용
            loaded_model_path = os.environ.get('AI_LOADED_MODEL_PATH')
            loaded_recipe_name = os.environ.get('AI_RECIPE_NAME')
            
            if not loaded_model_path or not loaded_recipe_name:
                print("Error: Pre-loaded model information not found in environment variables", file=sys.stderr)
                sys.exit(2)
                
            if loaded_recipe_name != recipe:
                print(f"Error: Pre-loaded model recipe ({loaded_recipe_name}) does not match requested recipe ({recipe})", file=sys.stderr)
                sys.exit(2)
                
            print(f"Using pre-loaded model: {loaded_model_path}", file=sys.stderr)
            # 모델을 직접 로드하지 않고, 이미 로드된 모델을 사용한다고 가정
            # 실제로는 모델 객체를 공유하기 어려우므로, 모델을 다시 로드하되 캐시된 상태를 활용
            model, ckpt, device = load_model_for_recipe(recipe)
        else:
            model, ckpt, device = load_model_for_recipe(recipe)
    except Exception as e:
        print(f"Model load error: {e}", file=sys.stderr)
        sys.exit(2)

    try:
        print("=" * 50, file=sys.stderr)
        print("AI 추론 시작", file=sys.stderr)
        print("=" * 50, file=sys.stderr)

        amap_np = run_per_image_forward(model, device, image_path)
    except Exception as e:
        print(f"Inference error: {e}", file=sys.stderr)
        sys.exit(3)

    try:
        # Compute image-level score using the same ROI logic: percentile P and area ratio combined
        try:
            P = int(os.environ.get('ROI_PERCENTILE_P', '95'))
        except Exception:
            P = 95
        # Flatten full map values
        flat_vals = amap_np.flatten()
        if flat_vals.size == 0:
            raise ValueError('Empty anomaly map')
        pct_value = float(np.percentile(flat_vals, P))
        pct = pct_value * 100.0

        AREA_THRESH_MODE = os.environ.get('AREA_THRESH_MODE', 'relative')
        if AREA_THRESH_MODE == 'relative':
            area_thresh = pct_value
        else:
            try:
                area_thresh = float(os.environ.get('AREA_ABS_THRESHOLD', '0.5'))
            except Exception:
                area_thresh = 0.5

        area_ratio = float(np.sum(flat_vals > area_thresh)) / float(flat_vals.size)
        area = area_ratio * 100.0

        ROI_COMBINE_METHOD = os.environ.get('ROI_COMBINE_METHOD', 'max')
        safe_area = area if area > 0.0 else 1e-6
        if ROI_COMBINE_METHOD == 'max':
            combined = pct / safe_area
        elif ROI_COMBINE_METHOD == 'mean':
            combined = (pct + area) / 2.0
        else:
            combined = pct / safe_area

        score = combined / 100.0  # normalize to 0..something; later multiplied to percent
        print(f"Image score calculation (roi): shape={amap_np.shape}, pct={pct:.4f}, area={area:.4f}, combined={combined:.6f}", file=sys.stderr)
    except Exception as e:
        print(f"Score calculation error: {e}, falling back to max(amap)", file=sys.stderr)
        try:
            score = float(np.max(amap_np))
        except Exception:
            score = 0.5

    try:
        save_heatmap(output_dir, image_path, amap_np)
    except Exception as e:
        print(f"Save heatmap error: {e}", file=sys.stderr)

    rects_path = args.rects_path if hasattr(args, 'rects_path') else None
    rects_from_stdin = args.rects_stdin if hasattr(args, 'rects_stdin') else False
    if rects_path or rects_from_stdin:
        try:
            if rects_from_stdin:
                stdin_text = sys.stdin.read()
                try:
                    stdin_obj = json.loads(stdin_text) if stdin_text.strip() else {}
                except Exception as e:
                    print(f"Failed to parse rects from stdin: {e}", file=sys.stderr)
                    sys.exit(6)
                rects = stdin_obj.get('rects', [])
                # extract optional per-request ROI params
                roi_params = {
                    'ROI_PERCENTILE_P': stdin_obj.get('ROI_PERCENTILE_P'),
                    'AREA_THRESH_MODE': stdin_obj.get('AREA_THRESH_MODE'),
                    'AREA_ABS_THRESHOLD': stdin_obj.get('AREA_ABS_THRESHOLD'),
                    'ROI_COMBINE_METHOD': stdin_obj.get('ROI_COMBINE_METHOD')
                }
                roi_params = {k: v for k, v in roi_params.items() if v is not None}
                wrote_multi_json = False
            else:
                rects = load_rects(rects_path)
                wrote_multi_json = True

            per_rect_results = process_rects_from_amap(amap_np, rects, output_dir, write_heatmaps=False, threshold=80.0, roi_params=(roi_params if rects_from_stdin else {}))
            multi = {'image': os.path.basename(image_path), 'model': os.path.basename(ckpt), 'results': per_rect_results}
            if wrote_multi_json:
                multi_json_path = os.path.join(output_dir, 'multi_results.json')
                with open(multi_json_path, 'w', encoding='utf-8') as mf:
                    json.dump(multi, mf, ensure_ascii=False, indent=2)
            # print JSON to stdout (always)
            print(json.dumps(multi))
        except Exception as e:
            print(f"Rect processing error: {e}", file=sys.stderr)
            sys.exit(5)
    else:
        # 전체 이미지 score도 백분율로 변환
        score_percentage = score * 100.0
        print(f"{score_percentage:.2f}")


if __name__ == '__main__':
    main()
