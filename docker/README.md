# PatchCore 학습용 Docker 이미지 (anomalib → OpenVINO IR)

이 컨테이너는 anomalib (Python)을 사용하여 **FAISS 가속**을 통해 PatchCore를 학습하고, C++ 추론 코드에서 사용할 수 있는 OpenVINO IR (.xml + .bin) 형식으로 자동 변환합니다.

## 주요 기능

- ✅ **FAISS 가속**: 학습 중 최근접 이웃 검색을 3-10배 빠르게 수행
- ✅ **Coreset 샘플링 비율 조정 가능**: 기본값 0.001 (0.1%) - 빠른 추론을 위한 최적화
- ✅ **OpenVINO IR 자동 변환**: C++ 추론 코드에서 바로 사용 가능
- ✅ **ROI 모드 지원**: 첫 이미지에서 ROI 선택 후 전체 데이터셋 자동 크롭

## Docker 이미지 빌드

프로젝트 루트 디렉토리에서 실행:

```bash
cd /Users/prascat/Inspector
docker build -t patchcore-trainer -f docker/Dockerfile .
```

빌드 시간: 약 10-20분 (네트워크 속도에 따라 다름)
- PyTorch, anomalib, OpenVINO 등 필요한 패키지 자동 설치
- FAISS 자동 포함

## 학습 실행 방법

### 기본 사용법

데이터 디렉토리와 출력 디렉토리를 마운트하여 실행:

```bash
# 예시: crimp 폴더의 데이터로 학습하고 결과를 ./host_out에 저장
docker run --rm -it \
  -v "$(pwd)/deploy/data/train:/workspace/data:ro" \
  -v "$(pwd)/weights:/workspace/output" \
  patchcore-trainer --data-dir /workspace/data --output /workspace/output --pattern-name my_model
```

### Coreset 비율 조정

```bash
# 1% 샘플링 (권장) - 균형잡힌 성능
docker run --rm -it \
  -v "$(pwd)/deploy/data/train:/workspace/data:ro" \
  -v "$(pwd)/weights:/workspace/output" \
  patchcore-trainer --data-dir /workspace/data --output /workspace/output \
  --pattern-name my_model --coreset-ratio 0.01

# 10% 샘플링 (논문 기본값) - 최고 정확도, 느린 추론
docker run --rm -it \
  -v "$(pwd)/deploy/data/train:/workspace/data:ro" \
  -v "$(pwd)/weights:/workspace/output" \
  patchcore-trainer --data-dir /workspace/data --output /workspace/output \
  --pattern-name my_model --coreset-ratio 0.1
```

### FAISS 비활성화 (문제 발생 시)

```bash
# PyTorch로 최근접 이웃 검색 (느림)
docker run --rm -it \
  -v "$(pwd)/deploy/data/train:/workspace/data:ro" \
  -v "$(pwd)/weights:/workspace/output" \
  patchcore-trainer --data-dir /workspace/data --output /workspace/output \
  --pattern-name my_model --no-use-faiss
```

### ROI 모드 사용

```bash
# 첫 이미지에서 ROI 선택 후 전체 데이터셋 자동 크롭
docker run --rm -it \
  -v "$(pwd)/deploy/data/train:/workspace/data:ro" \
  -v "$(pwd)/weights:/workspace/output" \
  patchcore-trainer --data-dir /workspace/data --output /workspace/output \
  --pattern-name my_model --roi
```

## 학습 옵션 상세 설명

### 필수 옵션

- `--data-dir`: 학습 데이터 디렉토리 경로
  - 이미지 파일들이 있는 폴더 지정
  - 지원 형식: PNG, JPG
  
- `--output`: 학습된 모델 출력 디렉토리
  - OpenVINO IR 파일 (.xml, .bin)이 저장됨
  - C++ 코드에서 직접 로드 가능

- `--pattern-name`: 패턴(모델) 이름
  - 출력 파일명: `{pattern_name}.xml`, `{pattern_name}.bin`

### Coreset 샘플링 비율 (`--coreset-ratio`)

PatchCore의 핵심 파라미터로, 모델 크기와 추론 속도를 결정합니다.

| 비율 | 설명 | 모델 크기 | 추론 속도 | 정확도 | 추천 용도 |
|------|------|-----------|-----------|--------|-----------|
| `0.1` | 논문 기본값 (10%) | 매우 큼 | 느림 | 최고 | 복잡한 결함 패턴 |
| `0.01` | **권장값 (1%)** | 중간 | 빠름 | 우수 | 대부분의 경우 |
| `0.001` | **기본값 (0.1%)** | 작음 | 매우 빠름 | 양호 | 단순/균일한 결함 |

**예시:**
- 학습 이미지 1000장 × 0.001 비율 = 메모리 뱅크에 약 1000개 feature 저장
- 더 작은 메모리 뱅크 = C++ 추론 시 더 빠른 최근접 이웃 검색

### FAISS 가속 (`--use-faiss`, `--no-use-faiss`)

- **기본 동작**: FAISS가 설치되어 있으면 자동으로 사용
- **효과**: 학습 중 coreset 샘플링 속도 3-10배 향상
- **C++ 추론**: 영향 없음 (이미 최적화된 메모리 뱅크 사용)
- **비활성화**: `--no-use-faiss` 옵션 사용 (PyTorch로 대체)

### 기타 옵션

- `--backbone`: Feature 추출 백본 (기본값: `wide_resnet50_2`)
  - 옵션: `resnet18`, `resnet50`, `wide_resnet50_2`
  
- `--layers`: Feature를 추출할 레이어 (기본값: `layer2 layer3`)
  - 더 깊은 레이어 = 더 추상적인 feature
  
- `--image-size`: 입력 이미지 크기 (기본값: `224 224`)
  - 더 큰 이미지 = 더 세밀한 검사, 느린 추론
  
- `--batch-size`: 학습 배치 크기 (기본값: `32`)
  - 메모리에 따라 조정
  
- `--num-neighbors`: 최근접 이웃 개수 (기본값: `9`)
  - 이상 스코어 계산에 사용

- `--roi`: ROI 모드 활성화
  - 첫 이미지에서 관심 영역 선택
  - 템플릿 매칭으로 전체 데이터셋 자동 크롭
  
- `--roi-threshold`: ROI 템플릿 매칭 임계값 (기본값: `0.5`)


## 편의 스크립트 사용

`docker/docker_run_with_data.sh` 스크립트를 사용하면 더 쉽게 컨테이너를 실행할 수 있습니다. 호스트의 데이터 디렉토리를 마운트(권장)하거나 컨테이너에 복사할 수 있습니다.

### 마운트 모드 (권장)
```bash
./docker/docker_run_with_data.sh ./deploy/data/train ./weights
```

### 복사 모드
```bash
# 데이터를 컨테이너 내부로 복사 (특별한 경우에만 사용)
./docker/docker_run_with_data.sh ./deploy/data/train ./weights --copy
```

스크립트는 컨테이너 내부에서 학습을 실행하고, 결과를 호스트의 출력 디렉토리에 저장하여 컨테이너 종료 후에도 파일이 유지됩니다.

## 성능 최적화 가이드

### 속도 vs 정확도 트레이드오프

#### 1. Coreset 샘플링 비율 선택

학습 데이터의 특성에 따라 적절한 비율을 선택하세요:

**균일한 정상 이미지 (단순한 결함):**
- `0.001` (0.1%) - 기본값 ✅
- 장점: 매우 빠른 추론, 작은 모델 크기
- 단점: 복잡한 결함 패턴 놓칠 수 있음
- 예시: 구리 와이어 스트립/크림프 검사

**다양한 정상 패턴 (중간 복잡도):**
- `0.01` (1%) - 권장 ✅
- 장점: 우수한 정확도, 적당한 추론 속도
- 단점: 모델 크기 약간 증가
- 예시: 다양한 제품 라인, 여러 종류의 결함

**매우 복잡한 정상 패턴:**
- `0.1` (10%) - 논문 기본값
- 장점: 최고 정확도
- 단점: 큰 모델 크기, 느린 추론
- 예시: 텍스처가 복잡한 표면, 다양한 조명 조건

#### 2. FAISS 가속 효과

**학습 단계:**
- Coreset 샘플링 시 최근접 이웃 검색 3-10배 빠름
- 특히 큰 데이터셋(1000장 이상)에서 효과적
- CPU만으로도 충분히 빠름 (GPU 불필요)

**추론 단계 (C++):**
- FAISS 사용 여부와 무관하게 동일한 IR 파일 생성
- Coreset 비율이 작을수록 C++ 추론도 자동으로 빠름
- N100 같은 저전력 CPU에서도 실시간 처리 가능

#### 3. 모델 크기와 추론 속도

실제 예시:

| 학습 이미지 | Coreset 비율 | Memory Bank 크기 | 모델 파일 크기 | 추론 시간 (N100) |
|-------------|--------------|------------------|----------------|------------------|
| 1000장 | 0.1 (10%) | ~100,000 features | ~400MB | ~200ms |
| 1000장 | 0.01 (1%) | ~10,000 features | ~40MB | ~30ms |
| 1000장 | 0.001 (0.1%) | ~1,000 features | ~4MB | ~5ms |

**결론**: 대부분의 경우 0.01 (1%)이 최적의 선택입니다.

### 실전 팁

#### 학습 데이터 준비
1. **정상 이미지만 필요**: 불량 이미지는 학습에 사용하지 않음
2. **최소 이미지 수**: 100장 이상 권장 (더 많을수록 좋음)
3. **이미지 품질**: 일관된 조명, 해상도 유지
4. **다양성**: 정상 범위 내에서 다양한 변화 포함

#### ROI 모드 활용
```bash
# 불필요한 배경 제거하여 정확도 향상
docker run --rm -it \
  -v "$(pwd)/deploy/data/train:/workspace/data:ro" \
  -v "$(pwd)/weights:/workspace/output" \
  patchcore-trainer --data-dir /workspace/data --output /workspace/output \
  --pattern-name my_model --roi --roi-threshold 0.7
```

- ROI 선택으로 검사 영역만 집중
- 템플릿 매칭으로 전체 데이터셋 자동 정렬
- 배경 노이즈 제거로 정확도 향상

#### FP16 변환 (선택사항)
```bash
# OpenVINO 모델 최적화 도구로 FP16 변환
mo --input_model output/my_model.xml \
   --data_type FP16 \
   --output_dir output/fp16
```

- 모델 크기 50% 감소
- N100에서 추론 속도 약간 향상
- 정확도는 거의 동일

## 학습 결과 확인

학습이 완료되면 출력 디렉토리에 다음 파일들이 생성됩니다:

```
weights/
  └── my_model/
      ├── my_model.xml          # OpenVINO IR 모델 (C++에서 로드)
      ├── my_model.bin          # 모델 가중치
      ├── checkpoint.ckpt       # PyTorch 체크포인트 (재학습용)
      └── config.yaml           # 학습 설정 정보
```

### C++ 코드에서 사용

```cpp
// Inspector C++ 코드에서 자동으로 로드됨
QString modelPath = "weights/my_model/my_model.xml";
ImageProcessor::initPatchCoreModel(modelPath, "CPU");
```

## 문제 해결

### FAISS 관련 오류
```bash
# FAISS 비활성화하고 재시도
docker run ... --no-use-faiss
```

### 메모리 부족
```bash
# 배치 크기 줄이기
docker run ... --batch-size 16
```

### 학습 시간 너무 길음
```bash
# 더 작은 coreset 비율 사용
docker run ... --coreset-ratio 0.001
```

### Docker 빌드 실패
```bash
# Docker Desktop이 실행 중인지 확인
docker info

# 캐시 없이 재빌드
docker build --no-cache -t patchcore-trainer -f docker/Dockerfile .
```

## 추가 정보

- **논문**: [PatchCore: Towards Total Recall in Industrial Anomaly Detection](https://arxiv.org/abs/2106.08265)
- **Anomalib 문서**: https://anomalib.readthedocs.io/
- **OpenVINO 문서**: https://docs.openvino.ai/
