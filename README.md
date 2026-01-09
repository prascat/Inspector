# Inspector - 머신 비전 검사 시스템

## 개요

Inspector는 Qt6, OpenCV, TensorRT를 기반으로 한 산업용 머신 비전 검사 소프트웨어입니다. 패턴 매칭, 실시간 필터링, AI 기반 이상 탐지 등 포괄적인 품질 검사 기능을 제공합니다.

## 주요 기능

🎯 고급 패턴 매칭 시스템
- FID (Fiducial) 패턴 인식: 서브픽셀 정확도의 기준점 템플릿 매칭
- INS (Inspection) 패턴 검사: 컬러, 이진화, 엣지 검출 등 다중 모드 검사
- ROI (Region of Interest) 관리: 유연한 검사 영역 정의 및 관리
- Filter 패턴 지원: 동적 마스킹 및 필터링 영역

🤖 AI 기반 이상 탐지
- PatchCore 알고리즘: 메모리 뱅크 기반 비정상 탐지
- TensorRT 가속: FP16 최적화로 실시간 추론
- 자동 학습 시스템: GUI에서 바로 모델 학습 및 배포
- 히트맵 시각화: 이상 영역 실시간 표시

🔍 다양한 검사 모드
- Color 검사: RGB 색상 매칭 및 임계값 기반 판정
- Binary 검사: 임계값 기반 이진화 분석
- Edge 검사: Sobel/Canny 엣지 검출 및 윤곽선 분석
- Template 매칭: 회전 지원 상호상관 기반 패턴 인식
- DIFF 검사: 기준 이미지 대비 차이 분석
- ANOMALY 검사: AI 기반 비정상 패턴 탐지

🎨 실시간 이미지 처리 파이프라인
- 10+ 필터 타입: 포괄적인 필터 라이브러리
  - Threshold (Binary/Adaptive)
  - Gaussian Blur & Motion Blur
  - Canny & Sobel Edge Detection
  - Laplacian Sharpening
  - Brightness/Contrast Adjustment
  - Contour Detection & Analysis
  - Dynamic Masking
- 실시간 필터 적용: 즉각적인 피드백과 라이브 프리뷰
- 필터 체인 관리: 순차적 필터 적용 및 파라미터 커스터마이징

🖥️ 멀티 카메라 지원
- FLIR Blackfly S 카메라: USB 3.0 산업용 카메라 지원
- 하드웨어 트리거: Line0 기반 외부 트리거 동기화
- UserSet 관리: LIVE/TRIGGER 모드 자동 전환
- 4분할 화면: 동시 다중 카메라 모니터링

🎛️ 직관적인 사용자 인터페이스
- 듀얼 모드 작동:
  - Recipe 모드: 패턴 생성 및 설정
  - Test 모드: 실시간 검사 및 검증
- 시뮬레이션 모드: 오프라인 이미지 테스트
- 다국어 지원: 한국어/영어 언어 전환
- 계층적 패턴 트리: 체계적인 패턴 및 필터 관리
- 실시간 속성 패널: 즉각적인 파라미터 조정 및 피드백

💾 레시피 관리 시스템
- XML 기반 레시피 저장: 패턴 및 필터 설정 완전 저장
- 템플릿 이미지 저장: Base64 인코딩된 템플릿 이미지
- 패턴 계층 구조: 부모-자식 패턴 관계 관리
- 백업 및 복원: 레시피 백업 및 복구 기능

🔧 고급 필터 기능
- 화이트 영역 마스킹: 템플릿 매칭 정확도 향상을 위한 지능형 마스킹
- 실시간 템플릿 업데이트: 필터 적용 시 템플릿 자동 갱신
- 마스크 기반 템플릿 매칭: OpenCV 마스크 파라미터 지원
- 실시간 필터 프리뷰: 파라미터 조정 중 즉각적인 필터 적용

## 기술 아키텍처

핵심 컴포넌트
- TeachingWidget: 메인 UI 컨트롤러 및 패턴 관리
- CameraView: OpenGL 가속 이미지 디스플레이 및 인터랙티브 패턴 편집
- FilterDialog: 실시간 프리뷰 지원 필터 설정 대화상자
- InsProcessor: 고성능 멀티모드 검사 엔진
- ImageProcessor: 최적화된 OpenCV 필터 처리 파이프라인 및 TensorRT 추론
- RecipeManager: XML 기반 레시피 직렬화 및 관리
- TrainDialog: AI 모델 학습 및 관리 대화상자

패턴 타입
1. FID (Fiducial) 패턴: 좌표계 정렬을 위한 기준 마커
2. INS (Inspection) 패턴: 합격/불합격 기준이 있는 품질 검사 영역
3. ROI (Region of Interest): 범용 분석 영역
4. Filter 패턴: 이미지 전처리를 위한 동적 마스킹 영역

검사 워크플로우
1. 패턴 등록: 검사 영역 및 기준 템플릿 정의
2. 필터 적용: 검출 향상을 위한 전처리 필터 적용
3. 템플릿 생성: 필터 적용된 템플릿 이미지 생성
4. 실시간 검사: 실시간 패턴 매칭 및 품질 검증
5. 결과 분석: 상세 메트릭을 포함한 합격/불합격 리포팅

## 기술 사양

🖥️ 하드웨어 요구사항
- 플랫폼: NVIDIA Jetson Orin Nano Super 8GB 전용
- CUDA: 12.2+ (Jetson Orin 전용 빌드)
- 메모리: 8GB 통합 메모리 (LPDDR5)
- AI 성능: 67 TOPS (INT8)
- CPU: 6-core Arm Cortex-A78AE
- GPU: 1024-core NVIDIA Ampere architecture

> ⚠️ 중요: 이 소프트웨어는 NVIDIA Jetson Orin Nano Super 8GB에 최적화되어 있으며, TensorRT FP16 추론 및 CUDA 가속 기능은 해당 하드웨어에 종속적입니다.

📦 소프트웨어 스택
- 프레임워크: Qt 6.10+ with C++17
- 이미지 처리: OpenCV 4.5+ (CUDA 빌드)
- AI 추론: TensorRT 10.7+ (Jetson Orin 최적화)
- 빌드 시스템: CMake 3.22+
- 운영체제: Ubuntu 22.04 (JetPack 6.0+)
- 카메라 인터페이스: FLIR Spinnaker SDK 4.2+ (ARM64)
- 이미지 형식: RGB, Grayscale, Binary
- 검사 성능: 트리거당 평균 100ms 이하 (AI 검사 포함)
- 메모리 관리: 8GB 메모리 환경 최적화

## 고급 기능

🎯 정밀 템플릿 매칭
- 서브픽셀 정확도: 보간법을 사용한 템플릿 매칭
- 회전 보상: 각도 범위 설정 가능한 템플릿 매칭
- 스케일 불변성: 다중 스케일 템플릿 검출
- 마스크 기반 매칭: 정확도 향상을 위한 화이트 영역 제외

🔄 실시간 처리
- 라이브 필터 프리뷰: 파라미터 조정 중 즉각적인 시각적 피드백
- 템플릿 자동 업데이트: 필터 변경 시 템플릿 동적 재생성
- 멀티스레드 처리: 최적 성능을 위한 병렬 필터 적용
- 메모리 최적화: 효율적인 이미지 버퍼 관리

📊 품질 관리
- 합격/불합격 기준: 설정 가능한 임계값 기반 검사
- 통계 분석: 평균, 표준편차, 히스토그램 분석
- 결함 탐지: 자동 이상 식별
- 측정 도구: 치수 분석 및 검증

## 최근 개선 사항 (2026)

🆕 최신 업데이트
- AI 이상 탐지: PatchCore 알고리즘 통합
- TensorRT 최적화: FP16 추론으로 실시간 AI 검사
- 자동 학습 시스템: GUI 통합 모델 학습
- 히트맵 시각화: 실시간 이상 영역 표시
- 모델 관리: 학습 중 트리거/시리얼/서버 요청 차단
- 디버그 최적화: 불필요한 로그 제거 및 성능 개선

🔧 AI 학습 시스템
- 자동 데이터셋 생성: ROI 자동 크롭 및 리사이징
- FID 기반 정렬: FID 패턴 기준 자동 이미지 정렬
- ONNX/TensorRT 변환: 원클릭 모델 배포
- 프로그레스 모니터링: 실시간 학습 진행 상황 표시

## 설치 및 사용법

빌드 요구사항
```bash
# 필수 하드웨어
NVIDIA Jetson Orin Nano Super 8GB

# 필수 소프트웨어
Ubuntu 22.04 (JetPack 6.0+)
Qt 6.10+
OpenCV 4.5+ (CUDA 빌드)
TensorRT 10.7+ (Jetson Orin 최적화)
CUDA 12.2+
CMake 3.22+
C++17 호환 컴파일러
FLIR Spinnaker SDK 4.2+ (ARM64)

# 빌드 명령
mkdir build && cd build
cmake ..
make -j6
```

빠른 시작
1. 애플리케이션 실행: build 디렉토리에서 `./Inspector` 실행
2. 카메라 설정: 카메라 연결 및 파라미터 설정
3. 레시피 생성: 패턴 정의 및 검사 기준 설정
4. 검사 테스트: 패턴 감지 및 품질 관리 검증
5. 설정 저장: 생산 사용을 위한 레시피 저장

## 활용 분야

산업 적용 사례
- 전자 제조: PCB 검사 및 부품 확인
- 자동차 품질 관리: 부품 정렬 및 결함 감지
- 제약 포장: 라벨 확인 및 용기 검사
- 식품 가공: 품질 평가 및 오염 감지
- 섬유 산업: 패턴 매칭 및 결함 식별

검사 기능
- 치수 측정: 교정된 카메라를 사용한 정밀 측정
- 표면 품질 평가: 스크래치, 덴트, 변색 감지
- 조립 확인: 부품 존재 및 올바른 방향 확인
- 코드 읽기: 바코드 및 QR 코드 확인
- 색상 매칭: 정밀한 색상 일관성 확인

## FLIR 카메라 통합

지원 카메라
FLIR Blackfly S (BFS-U3-16S2C)

#카메라 사양
- 해상도: 1440 × 1080 픽셀
- 픽셀 크기: 5.5 μm
- 센서 타입: 글로벌 셔터 CMOS
- 최대 프레임 레이트: 30 FPS @ 전체 해상도
- 컬러 출력: RGB24 (Bayer RG8 센서 + 디모자이킹)
- 인터페이스: USB 3.0 Super Speed
- 전원 공급: USB 버스 파워
- 작동 온도: 0 ~ 40°C
- ROI 지원: 최적화된 캡처를 위한 ROI 지원
- 트리거 모드: 하드웨어 트리거 (Line 기반) 및 소프트웨어 트리거

#통합 방법
- SDK: FLIR Spinnaker SDK 4.2.0.88+
- API 레벨: GenICam (GenApi) 전체 노드맵 액세스
- 스트리밍 프로토콜: USB3Vision (USB3V)

UserSet 설정 시스템

FLIR SpinView 기능과 일치하는 정교한 UserSet 관리 시스템을 구현합니다:

#UserSet 개념

1. UserSetSelector - 현재 활성 UserSet
```
읽기/쓰기 작업을 위해 현재 선택된 UserSet
- 어떤 UserSet의 파라미터에 액세스할 수 있는지 결정
- 예: UserSet0 선택 → UserSet0 파라미터 읽기/쓰기 가능
- 사용 시점: 파라미터 수정, UserSet 로드
```

2. UserSetDefault - 부팅 시 기본 UserSet
```
카메라 전원 켜질 때 자동으로 로드됨
- 카메라 재시작 및 애플리케이션 재시작 시에도 유지
- 목적: 일관된 초기 카메라 상태
- 예: UserSetDefault = UserSet0 설정 → 카메라 항상 LIVE 모드로 부팅
- 설정: 일관성을 위해 SpinView와 동기화
```

3. FileAccessControl - 파라미터 수정 권한
```
UserSet 파라미터 수정 가능 여부 제어
- Read/Write: 전체 파라미터 수정 가능
- Read Only: 파라미터 잠금 (보호 모드)
- 목적: 실수로 인한 파라미터 변경 방지
- 설정: UserSet 로드 중 동기화
```

#UserSet 로드 워크플로우

```
애플리케이션 UserSet 로드 프로세스:
1. 카메라에서 NodeMap 가져오기
2. 카메라 스트리밍 상태 확인 → 스트리밍 중이면 중지
3. UserSetSelector를 대상 UserSet으로 설정 (UserSet0 또는 UserSet1)
4. UserSetLoad 명령 실행
5. UserSetDefault를 대상 UserSet으로 설정 (영구 저장)
6. FileAccessControl을 Read/Write로 설정 (수정 허용)
7. 노드 캐시 새로고침 (DeviceRegistersStreamingStart)
8. 로드된 파라미터 확인 (TriggerMode, TriggerSource 등)
9. 이전에 활성화되어 있었다면 스트리밍 재개

예상 출력 로그:
[UserSet] UserSetDefault set to: UserSet0
[UserSet] FileAccessControl set to Read/Write
[UserSet] Current TriggerMode after load: On
[UserSet] Current TriggerSource after load: Line0
[UserSet] DeviceRegistersStreamingStart executed
```

#사전 설정된 UserSet

UserSet0 - LIVE 모드 (연속 획득)
- TriggerMode: Off (자유 실행)
- AcquisitionMode: Continuous
- FrameRate: 30 FPS
- 목적: 실시간 라이브 프리뷰 및 카메라 테스트
- 사용 사례: 패턴 생성, 템플릿 생성, 수동 검사

UserSet1 - TRIGGER 모드 (트리거 획득)
- TriggerMode: On
- TriggerSource: Line0 (하드웨어 트리거 입력)
- TriggerSelector: FrameStart
- AcquisitionMode: SingleFrame
- 목적: 하드웨어 트리거 생산 검사
- 사용 사례: 외부 장비와 동기화된 캡처, 생산 라인 통합

#하드웨어 트리거 연결

Blackfly S 커넥터 핀아웃 (GPIO/Trigger)
- Line0 입력: 하드웨어 트리거 신호 입력
- 전압 레벨: TTL/LVCMOS 호환 (0-5V 로직)
- 신호 로직: 상승 엣지 트리거 (기본 설정)
- 디바운스: 내장 하드웨어 디바운싱 (일반적으로 10+ µs)

## 시리얼/서버 통신

검사 요청 프로토콜
- 시리얼 통신: UART 기반 검사 명령 수신
- 서버 통신: TCP/IP 기반 검사 명령 수신
- 프레임 인덱스 제어: 카메라별 검사 프레임 지정
- 결과 전송: 합격/불합격 결과 자동 전송

모델 관리 다이얼로그
- 학습 중 차단: TrainDialog 활성화 시 모든 트리거/시리얼/서버 요청 무시
- 이미지 수집: 학습용 이미지 자동 캡처 (선택적)
- 배치 학습: 여러 패턴 순차 학습 지원

## 문제 해결

트리거 감지 안됨
1. UserSet1이 로드되었는지 확인 (콘솔: `[UserSet] Current TriggerMode after load: On`)
2. Line0의 하드웨어 트리거 배선 확인
3. 트리거 신호 전압 확인 (일반적으로 0-5V 로직 레벨)
4. 트리거 소스 설정이 하드웨어 연결과 일치하는지 확인

UserSet 로드 실패
1. 카메라 연결 및 전원 공급 확인
2. 다른 애플리케이션(SpinView)에서 카메라를 독점 사용 중인지 확인
3. SDK 버전 호환성 확인 (Spinnaker 4.2.0.88+)
4. 특정 오류 메시지에 대한 콘솔 로그 검토

AI 모델 학습 실패
1. Python 환경 확인 (`python3 --version`)
2. 필요한 패키지 설치 확인 (anomalib, torch, onnx 등)
3. 충분한 학습 이미지 확인 (최소 5장 이상 권장)
4. 디스크 공간 확인

---

© 2026 KM DigiTech. Inspector - 머신 비전 검사 시스템. All rights reserved.  
*AI 기반 이상 탐지와 고급 패턴 매칭을 갖춘 전문 산업용 머신 비전 솔루션*