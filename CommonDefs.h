#ifndef COMMONDEFS_H
#define COMMONDEFS_H

#include <QUuid>
#include <QString>
#include <QRect>
#include <QColor>
#include <QImage>
#include <QMap>
#include <QList>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <opencv2/opencv.hpp>
#include "LanguageManager.h"
#include "CustomMessageBox.h"

// TR 매크로 정의
#ifndef TR
#define TR(key) LanguageManager::instance()->getText(key)
#endif

#define SIMPLE_MOVE_PIXELS      1
#define CAMERA_INTERVAL         33      // 30 frame
#define FRAME_RATE 30
#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240
#define LANGUAGE_FILE "lang.xml"
#define CONFIG_FILE "config.xml"
#define MAX_CAMERAS 4                   // 최대 카메라 연결수

// 이름표 폰트 설정 (패턴 및 검사박스 이름표에 공통 사용)
#define NAMEPLATE_FONT_FAMILY "Arial"
#define NAMEPLATE_FONT_SIZE 12
#define NAMEPLATE_FONT_WEIGHT QFont::Bold

// 라벨 폰트 설정 (모든 이름표에 공통 사용)
#define LABEL_FONT_FAMILY "Arial"
#define LABEL_FONT_SIZE 12
#define LABEL_FONT_WEIGHT QFont::Bold

struct InspectionResult {
    bool isPassed = false;
    QMap<QUuid, bool> fidResults;          // FID 검사 결과 (패턴 ID -> 통과 여부)
    QMap<QUuid, bool> insResults;          // INS 검사 결과 (패턴 ID -> 통과 여부)
    QMap<QUuid, double> matchScores;       // 매칭 점수 (패턴 ID -> 점수)
    QMap<QUuid, double> insScores;         // INS 검사 점수 (패턴 ID -> 점수)
    QMap<QUuid, cv::Point> locations;      // 검출 위치 (패턴 ID -> 위치)
    QMap<QUuid, double> angles;            // 검출 각도 (패턴 ID -> 각도)
    QMap<QUuid, QRectF> adjustedRects;      // 조정된 검사 영역 (패턴 ID -> 영역) - QRectF로 변경
    
    // 부모 FID 관련 추가 멤버 변수
    QMap<QUuid, cv::Point> parentOffsets;  // 부모 FID 위치 오프셋 (패턴 ID -> 오프셋)
    QMap<QUuid, double> parentAngles;      // 부모 FID 회전 각도 (패턴 ID -> 각도)

    QMap<QUuid, cv::Mat> insProcessedImages;  // 처리된 결과 이미지 (패턴 ID -> 결과 이미지)
    QMap<QUuid, int> insMethodTypes;          // 검사 방법 타입 (패턴 ID -> 검사 방법)
    
    // STRIP 두께 검사 전용 측정 위치 정보
    QMap<QUuid, cv::Point> stripThicknessCenters;  // 두께 측정 중심점 (패턴 ID -> 중심점)
    QMap<QUuid, std::pair<cv::Point, cv::Point>> stripThicknessLines; // 두께 측정선 (패턴 ID -> (좌측점, 우측점))
    QMap<QUuid, std::vector<std::pair<cv::Point, cv::Point>>> stripThicknessDetails; // 좌우 두께 측정 상세 좌표 (패턴 ID -> [(상점,하점), (상점,하점)])
    
    // STRIP 목 부분 절단 품질 측정 결과
    QMap<QUuid, double> stripNeckAvgWidths;     // 평균 목 폭 (패턴 ID -> 평균 폭)
    QMap<QUuid, double> stripNeckMinWidths;     // 최소 목 폭 (패턴 ID -> 최소 폭)
    QMap<QUuid, double> stripNeckMaxWidths;     // 최대 목 폭 (패턴 ID -> 최대 폭)
    QMap<QUuid, double> stripNeckStdDevs;       // 목 폭 표준편차 (패턴 ID -> 표준편차)
    QMap<QUuid, int> stripNeckMeasureX;         // 목 폭 측정 X 좌표 (패턴 ID -> X 좌표)
    QMap<QUuid, int> stripNeckMeasureCount;     // 목 폭 측정 포인트 수 (패턴 ID -> 포인트 수)
    
    // STRIP 두께 측정 결과
    QMap<QUuid, int> stripMeasuredThicknessMin; // 측정된 최소 두께 (패턴 ID -> 최소 두께)
    QMap<QUuid, int> stripMeasuredThicknessMax; // 측정된 최대 두께 (패턴 ID -> 최대 두께)
    QMap<QUuid, int> stripMeasuredThicknessAvg; // 측정된 평균 두께 (패턴 ID -> 평균 두께)
    QMap<QUuid, bool> stripThicknessMeasured;   // 두께 측정 완료 여부 (패턴 ID -> 측정 여부)
    
    // STRIP REAR 두께 측정 결과
    QMap<QUuid, int> stripRearMeasuredThicknessMin; // REAR 측정된 최소 두께 (패턴 ID -> 최소 두께)
    QMap<QUuid, int> stripRearMeasuredThicknessMax; // REAR 측정된 최대 두께 (패턴 ID -> 최대 두께)
    QMap<QUuid, int> stripRearMeasuredThicknessAvg; // REAR 측정된 평균 두께 (패턴 ID -> 평균 두께)
    QMap<QUuid, bool> stripRearThicknessMeasured;   // REAR 두께 측정 완료 여부 (패턴 ID -> 측정 여부)
    
    // STRIP 박스 위치 정보 (패턴 중심 기준 상대좌표)
    QMap<QUuid, QPointF> stripFrontBoxCenter;       // FRONT 박스 중심 상대좌표 (패턴 중심 기준)
    QMap<QUuid, QSizeF> stripFrontBoxSize;          // FRONT 박스 크기 (width, height)
    QMap<QUuid, QPointF> stripRearBoxCenter;        // REAR 박스 중심 상대좌표 (패턴 중심 기준)
    QMap<QUuid, QSizeF> stripRearBoxSize;           // REAR 박스 크기 (width, height)
    
    // STRIP 두께 측정 포인트들 (검은색 구간의 시작-끝점 쌍)
    // 절대좌표로 저장 (원본 이미지 기준)
    // 2개씩 쌍으로 저장: [라인1시작, 라인1끝, 라인2시작, 라인2끝, ...]
    QMap<QUuid, QList<QPoint>> stripFrontThicknessPoints;  // FRONT 두께 측정 라인들 (전체 스캔 라인 - 녹색)
    QMap<QUuid, QList<QPoint>> stripRearThicknessPoints;   // REAR 두께 측정 라인들 (전체 스캔 라인 - 녹색)
    QMap<QUuid, QList<QPoint>> stripFrontBlackRegionPoints;  // FRONT 검은색 검출 구간만 (빨간색)
    QMap<QUuid, QList<QPoint>> stripRearBlackRegionPoints;   // REAR 검은색 검출 구간만 (빨간색)
    
    // STRIP 실제 측정 지점 (절대좌표 - 검사 로그의 실제 검출된 위치)
    QMap<QUuid, QPoint> stripStartPoint;            // STRIP 측정 시작점 (절대좌표)
    QMap<QUuid, QPoint> stripMaxGradientPoint;      // STRIP 최대 Gradient 지점 (절대좌표)
    QMap<QUuid, int> stripMeasuredThicknessLeft;   // 측정된 좌측 두께 (픽셀)
    QMap<QUuid, int> stripMeasuredThicknessRight;  // 측정된 우측 두께 (픽셀)
    
    // EDGE 검사 결과 (심선 끝 절단면 품질)
    QMap<QUuid, bool> edgeResults;                  // EDGE 검사 통과 여부 (패턴 ID -> 통과 여부)
    QMap<QUuid, int> edgeIrregularityCount;         // 불규칙성 개수 (패턴 ID -> 개수)
    QMap<QUuid, double> edgeMaxDeviation;           // 최대 편차 mm (패턴 ID -> 편차)
    QMap<QUuid, double> edgeMinDeviation;           // 최소 편차 mm (패턴 ID -> 편차)
    QMap<QUuid, double> edgeAvgDeviation;           // 평균 편차 mm (패턴 ID -> 편차)
    QMap<QUuid, QPointF> edgeBoxCenter;             // EDGE 박스 중심 상대좌표 (패턴 중심 기준)
    QMap<QUuid, QSizeF> edgeBoxSize;                // EDGE 박스 크기 (width, height)
    QMap<QUuid, bool> edgeMeasured;                 // EDGE 측정 완료 여부 (패턴 ID -> 측정 여부)
    QMap<QUuid, QList<QPoint>> edgeAbsolutePoints; // EDGE 절대좌표 포인트들 (패턴 ID -> Qt 포인트 배열)
    QMap<QUuid, QList<double>> edgePointDistances; // EDGE 각 포인트의 거리 mm (패턴 ID -> 거리 배열)
    QMap<QUuid, int> edgeAverageX;                  // 절단면 평균 X 위치 (패턴 ID -> X 좌표)
    QMap<QUuid, double> edgeRegressionSlope;        // EDGE 선형 회귀 기울기 m (패턴 ID -> 기울기)
    QMap<QUuid, double> edgeRegressionIntercept;    // EDGE 선형 회귀 절편 b (패턴 ID -> 절편)
    
    // STRIP 4개 컨투어 포인트 (절대좌표)
    QMap<QUuid, QPoint> stripPoint1;               // STRIP Point 1 (패턴 ID -> 절대좌표)
    QMap<QUuid, QPoint> stripPoint2;               // STRIP Point 2 (패턴 ID -> 절대좌표)
    QMap<QUuid, QPoint> stripPoint3;               // STRIP Point 3 (패턴 ID -> 절대좌표)
    QMap<QUuid, QPoint> stripPoint4;               // STRIP Point 4 (패턴 ID -> 절대좌표)
    QMap<QUuid, bool> stripPointsValid;            // 4개 포인트 유효성 (패턴 ID -> 유효 여부)
    
    // STRIP 길이 검사 결과
    QMap<QUuid, bool> stripLengthResults;          // STRIP 길이 검사 통과 여부 (패턴 ID -> 통과 여부)
    QMap<QUuid, double> stripMeasuredLength;       // 측정된 STRIP 길이 (패턴 ID -> 길이, mm 또는 px)
    QMap<QUuid, double> stripMeasuredLengthPx;     // 측정된 STRIP 길이 픽셀값 원본 (패턴 ID -> 픽셀)
    QMap<QUuid, QPoint> stripLengthStartPoint;     // 길이 측정 시작점 (EDGE 평균선 중점)
    QMap<QUuid, QPoint> stripLengthEndPoint;       // 길이 측정 끝점 (P3,P4 중점)
    
    // STRIP 세부 검사 결과 로그용 (출력 순서 제어)
    QString stripPatternName;                      // STRIP 패턴 이름
    QString stripLengthResult;                     // STRIP LENGTH 결과 (PASS/NG)
    QString stripLengthDetail;                     // STRIP LENGTH 세부 정보
    QString frontResult;                           // FRONT 결과 (PASS/NG)
    QString frontDetail;                           // FRONT 세부 정보
    QString rearResult;                            // REAR 결과 (PASS/NG)
    QString rearDetail;                            // REAR 세부 정보
    QString edgeResult;                            // EDGE 결과 (PASS/NG)
    QString edgeDetail;                            // EDGE 세부 정보
    
    // CRIMP SHAPE 검사 결과
    QMap<QUuid, QList<QVector<QPoint>>> crimpCurrentContours;    // 현재 형상 윤곽선 (패턴 ID -> 윤곽선 리스트)
    QMap<QUuid, QList<QVector<QPoint>>> crimpTemplateContours;   // 템플릿 형상 윤곽선 (패턴 ID -> 윤곽선 리스트)
    QMap<QUuid, cv::Mat> crimpDiffMask;                          // 차이 마스크 (패턴 ID -> 차이 영역)
    QMap<QUuid, QPointF> crimpBoxCenter;                         // SHAPE 박스 중심 상대좌표
    QMap<QUuid, QSizeF> crimpBoxSize;                            // SHAPE 박스 크기
    
    // COLOR, EDGE, BINARY 검사 결과 (CRIMP와 동일한 방식)
    QMap<QUuid, cv::Mat> colorDiffMask;                          // COLOR 차이 마스크 (초록=유사, 빨강=차이)
    QMap<QUuid, cv::Mat> edgeDiffMask;                           // EDGE 차이 마스크
    QMap<QUuid, cv::Mat> binaryDiffMask;                         // BINARY 차이 마스크
};

// 패턴 유형 열거형
enum class PatternType {
    ROI,            // 1. 관심영역 (최상위)
    FID,            // 2. 피듀셜 매칭 (ROI 내부에만 가능)
    INS,            // 3. 검사영역 (피듀셜 내외부 가능)
    FIL             // 4. 필터 (검사영역 내부에만 가능)
};

// 필터 정보 구조체
struct FilterInfo {
    int type;                       // 필터 유형
    QMap<QString, int> params;      // 필터 파라미터
    bool enabled = true;            // 필터 활성화 상태
};

// 패턴 정보 구조체
struct PatternInfo {
    QUuid id;
    QString name;
    QRectF rect;
    QColor color;
    bool enabled = true;
    PatternType type = PatternType::ROI;
    QString cameraUuid;  // 카메라 UUID를 저장하기 위한 필드
    
    // 회전 각도(도 단위) 추가
    double angle = 0.0;
    
    // Strip/Crimp 모드 구분 (0: STRIP, 1: CRIMP)
    int stripCrimpMode = 0;  // 기본값: STRIP

    // 패턴 계층 구조를 위한 필드
    QUuid parentId;  // 부모 패턴의 ID (없으면 null)
    QList<QUuid> childIds;  // 자식 패턴의 ID 목록
    
    // 각 패턴 타입별 특수 속성들
    // ROI 속성
    bool includeAllCamera = false;
    
    // Fiducial 속성
    double matchThreshold = 75.0;  // 매칭 임계값 (0-100%)
    bool useRotation = false;
    double minAngle = -15.0;
    double maxAngle = 15.0;
    double angleStep = 1.0;
    QImage templateImage;
    int fidMatchMethod = 0;     // FID 템플릿 매칭 방법 (0: Coefficient, 1: Correlation)
    bool runInspection = true;  // 추가: 매칭 검사 활성화 여부
    
    // Inspection 속성
    double passThreshold = 0.9;
    bool invertResult = false;
    int insMatchMethod = 0; // 추가: INS 매칭 방법 (0: 템플릿, 1: 특징점, 2: 윤곽선)
    int inspectionMethod = 0;

    // STRIP 검사 전용 파라미터들
    int stripContourMargin = 10;        // 컨투어 검출 마진 (픽셀)
    int stripMorphKernelSize = 3;       // 형태학적 연산 커널 크기
    float stripGradientThreshold = 3.0f; // Gradient 임계값 (픽셀)
    int stripGradientStartPercent = 20;  // Gradient 계산 시작 지점 (%)
    int stripGradientEndPercent = 85;    // Gradient 계산 끝 지점 (%)
    int stripMinDataPoints = 5;          // 최소 데이터 포인트 수
    
    // STRIP 길이검사 관련 파라미터
    bool stripLengthEnabled = true;      // STRIP 길이검사 활성화 여부
    double stripLengthMin = 5.7;         // 최소 길이 (mm)
    double stripLengthMax = 6.0;         // 최대 길이 (mm)
    double stripLengthConversionMm = 6.0; // 수치 변환 (mm) - pixel to mm 변환값
    double stripLengthCalibrationPx = 0.0; // 캘리브레이션 기준 픽셀값
    bool stripLengthCalibrated = false;    // 캘리브레이션 완료 여부
    
    // STRIP 두께 측정 관련 파라미터 (프론트)
    bool stripFrontEnabled = true;       // FRONT 두께 검사 활성화 여부
    int stripThicknessBoxWidth = 100;    // 두께 측정 박스 너비 (픽셀)
    int stripThicknessBoxHeight = 200;   // 두께 측정 박스 높이 (픽셀)
    double stripThicknessMin = 1.0;      // 최소 두께 (mm)
    double stripThicknessMax = 2.0;      // 최대 두께 (mm)

    // STRIP REAR 두께 측정 관련 파라미터
    bool stripRearEnabled = true;            // REAR 두께 검사 활성화 여부
    int stripRearThicknessBoxWidth = 100;    // REAR 두께 측정 박스 너비 (픽셀)
    int stripRearThicknessBoxHeight = 200;   // REAR 두께 측정 박스 높이 (픽셀)
    double stripRearThicknessMin = 1.0;      // REAR 최소 두께 (mm)
    double stripRearThicknessMax = 2.0;      // REAR 최대 두께 (mm)

    // EDGE 검사 관련 파라미터 (심선 끝 절단면 품질)
    bool edgeEnabled = true;                 // EDGE 검사 활성화 여부
    int edgeOffsetX = 75;                    // 패턴 왼쪽에서의 오프셋 (픽셀)
    int edgeBoxWidth = 90;                   // EDGE 검사 박스 너비 (픽셀)
    int edgeBoxHeight = 150;                 // EDGE 검사 박스 높이 (픽셀)
    int edgeMaxOutliers = 4;                 // 허용 최대 불량 포인트 개수 (평균선 거리 기준)
    int edgeStartPercent = 3;                // EDGE 시작 제외 퍼센트 (1-50%)
    int edgeEndPercent = 3;                  // EDGE 끝 제외 퍼센트 (1-50%)
    
    // EDGE 평균선 거리 검사 파라미터
    double edgeDistanceMax = 0.5;           // 평균선에서 최대 허용 거리 (mm)

    // CRIMP SHAPE 검사 파라미터
    bool crimpShapeEnabled = true;          // CRIMP SHAPE 검사 활성화 여부
    int crimpShapeOffsetX = 10;             // 패턴 왼쪽에서의 오프셋 (픽셀)
    int crimpShapeBoxWidth = 100;           // SHAPE 박스 너비 (픽셀)
    int crimpShapeBoxHeight = 100;          // SHAPE 박스 높이 (픽셀)
    double crimpShapeMatchRate = 80.0;      // 매칭율 (%)

    // 이진화 검사를 위한 추가 속성
    int binaryThreshold = 128;        // 이진화 임계값 (0-255)
    int compareMethod = 0;            // 비교 방식 (0: 이상, 1: 이하, 2: 범위 내)
    double lowerThreshold = 0.0;      // 하한 임계값 (범위 검사용)
    double upperThreshold = 1.0;      // 상한 임계값 (범위 검사용)
    bool useWhiteRatio = true;        // true: 흰색 비율, false: 검은색 비율 측정
    int ratioType = 0;
    QList<FilterInfo> filters;  // 패턴에 적용된 필터 목록
};

// 카메라 정보 구조체
// 카메라 정보 구조체 (수정)
struct CameraInfo {
    int index;                  // 카메라 인덱스
    int videoDeviceIndex;       // V4L2 장치 인덱스 (Linux 전용)
    int imageIndex;             // 티칭 이미지 인덱스 (0, 1, 2, ...)
    QString name;               // 카메라 이름
    QString uniqueId;           // 카메라 고유 식별자
    QString locationId;         // 로케이션 ID
    QString serialNumber;       // 시리얼 번호
    QString vendorId;           // 벤더 ID
    QString productId;          // 제품 ID
    cv::VideoCapture* capture;  // 카메라 캡처 객체
    bool isConnected;           // 연결 상태
    
    CameraInfo() : index(-1), videoDeviceIndex(-1), imageIndex(0), capture(nullptr), isConnected(false) {}
    
    CameraInfo(int idx) : 
        index(idx), 
        videoDeviceIndex(idx),
        imageIndex(0),
        name(QString("카메라 %1").arg(idx + 1)), 
        capture(nullptr), 
        isConnected(false) {}
};

// 필터 타입 상수
const int FILTER_THRESHOLD = 0;
const int FILTER_BLUR = 1;
const int FILTER_CANNY = 2;
const int FILTER_SOBEL = 3;
const int FILTER_LAPLACIAN = 4;
const int FILTER_SHARPEN = 5;
const int FILTER_BRIGHTNESS = 6;
const int FILTER_CONTRAST = 7;
const int FILTER_CONTOUR = 8;
const int FILTER_MASK = 10;  

// 필터 타입 목록 (순서 중요)
const QVector<int> FILTER_TYPE_LIST = {
    FILTER_THRESHOLD, FILTER_BLUR, FILTER_CANNY, FILTER_SOBEL,
    FILTER_LAPLACIAN, FILTER_SHARPEN, FILTER_BRIGHTNESS,
    FILTER_CONTRAST, FILTER_CONTOUR, FILTER_MASK
};

inline QString getFilterTypeName(int filterType) {
    switch (filterType) {
        case FILTER_THRESHOLD: return "이진화 (Threshold)";
        case FILTER_BLUR: return "블러 (Blur)";
        case FILTER_CANNY: return "캐니 엣지 (Canny)";
        case FILTER_SOBEL: return "소벨 엣지 (Sobel)";
        case FILTER_LAPLACIAN: return "라플라시안 (Laplacian)";
        case FILTER_SHARPEN: return "선명하게 (Sharpen)";
        case FILTER_BRIGHTNESS: return "밝기 (Brightness)";
        case FILTER_CONTRAST: return "대비 (Contrast)";
        case FILTER_CONTOUR: return "컨투어 (Contour)";
        case FILTER_MASK: return "마스크 (Mask)";
        default: return QString("필터 %1").arg(filterType);
    }
}

// 블러 타입 상수
const int BLUR_GAUSSIAN = 0;
const int BLUR_MEDIAN = 1;
const int BLUR_AVERAGE = 2;
const int BLUR_BILATERAL = 3;

// 색상 공간 타입 상수
const int COLOR_SPACE_RGB = 0;
const int COLOR_SPACE_HSV = 1;
const int COLOR_SPACE_LAB = 2;
const int COLOR_SPACE_YCrCb = 3;

// 이진화 타입 (OpenCV 값 + 추가 값)
const int THRESH_ADAPTIVE_MEAN = 100;
const int THRESH_ADAPTIVE_GAUSSIAN = 101;

// 캘리브레이션 정보를 저장하는 구조체
struct CalibrationInfo {
    bool isCalibrated = false;
    QRect calibrationRect;
    double realWorldLength = 0.0;  // 실제 물리적 길이(mm)
    double pixelToMmRatio = 0.0;   // pixelToCmRatio → pixelToMmRatio
};

namespace InspectionMethod {
    const int COLOR = 0;        // 색상 검사
    const int EDGE = 1;         // 엣지 검사
    const int BINARY = 2;       // 이진화 검사
    const int STRIP = 3;        // STRIP 검사 
    const int CRIMP = 4;        // CRIMP 검사
    
    // 검사 방법 이름 반환 함수
    inline QString getName(int method) {
        switch (method) {
            case COLOR:
                return "COLOR";
            case EDGE:
                return "EDGE";
            case BINARY:
                return "BINARY";
            case STRIP:
                return "STRIP";
            case CRIMP:
                return "CRIMP";
            default:
                return "UNKNOWN";
        }
    }
    
    // 검사 방법 개수
    const int COUNT = 5;
}

// Strip/Crimp 모드 정의
namespace StripCrimpMode {
    const int STRIP_MODE = 0;   // Strip 모드
    const int CRIMP_MODE = 1;   // Crimp 모드
}

namespace UIColors {
    // 패턴 타입별 색상 - 더 부드러운 색상으로 변경
    const QColor ROI_COLOR = QColor("#E6C27C");      // 연한 노란색 (ROI)
    const QColor FIDUCIAL_COLOR = QColor("#7094DB"); // 연한 파란색 (FID)
    const QColor INSPECTION_COLOR = QColor("#8BCB8B"); // 연한 초록색 (INS)
    const QColor FILTER_COLOR = QColor("#FFB74D"); // 연한 오렌지색 (FILTER)
    const QColor GROUP_COLOR = QColor("#FF00FF");  // 마젠타 (GROUP)
    
    // CAM, RUN 등 토글 버튼용 색상 - 더 부드러운 색상으로 변경
    const QColor BTN_CAM_OFF_COLOR = QColor("#E57373");  // 연한 빨간색(OFF)
    const QColor BTN_CAM_ON_COLOR  = QColor("#81C784");  // 연한 초록색(ON)
    
    // TEACH 버튼용 색상 - CAM 버튼과 동일한 색상
    const QColor BTN_TEACH_OFF_COLOR = QColor("#E57373"); // 연한 빨간색(OFF) - CAM과 동일
    const QColor BTN_TEACH_ON_COLOR  = QColor("#81C784"); // 연한 초록색(ON) - CAM과 동일
    
    // RUN 버튼도 CAM 버튼과 동일한 색상 체계 사용
    const QColor BTN_RUN_OFF_COLOR = QColor("#E57373");  // 연한 빨간색(OFF) - CAM과 동일
    const QColor BTN_RUN_ON_COLOR  = QColor("#81C784");  // 연한 초록색(ON) - CAM과 동일
    
    // LIVE/INSPECT 모드 토글 버튼용 색상
    const QColor BTN_LIVE_COLOR = QColor("#64B5F6");     // 연한 파란색 (LIVE 모드)
    const QColor BTN_INSPECT_COLOR = QColor("#FFB74D");  // 연한 주황색 (INSPECT 모드)

    // DRAW, MOVE 토글 버튼용 색상 - 더 부드러운 색상으로 변경
    const QColor BTN_DRAW_COLOR = QColor("#FF8A65");     // 연한 주황색(DRAW)
    const QColor BTN_MOVE_COLOR = QColor("#7986CB");     // 연한 블루바이올렛(MOVE)

    // 버튼 색상 - 주황색
    const QColor BTN_SAVE_COLOR = QColor("#FF8A65");      // 연한 주황색 - 저장
    const QColor BTN_ADD_COLOR = QColor("#FF8A65");       // 연한 주황색 - 추가
    const QColor BTN_REMOVE_COLOR = QColor("#FF8A65");    // 연한 주황색 - 삭제
    const QColor BTN_FILTER_COLOR = QColor("#FF8A65");    // 연한 주황색 - 필터
    
    // 슬라이더 색상
    const QColor SLIDER_HANDLE_COLOR = QColor("#64B5F6");   // 연한 파란색 - 슬라이더 핸들
    const QColor SLIDER_ACTIVE_COLOR = QColor("#90CAF9");   // 더 연한 파란색 - 활성 구간
        
    // 필터 관련 색상
    const QColor FILTER_BG_COLOR = QColor(174, 213, 239); // 연한 라이트 스카이 블루 
    const QColor FILTER_SELECTED_COLOR = QColor(143, 190, 240); // 연한 필터 선택 색상
    
    // 패널 색상
    const QColor PANEL_BG_COLOR = QColor("#f0f0f0");  // 약간 더 연한 회색 - 패널 배경색
    const QColor PANEL_HEADER_COLOR = QColor("#555"); // 약간 더 연한 회색 - 패널 헤더 텍스트 색상
    
    // 포커스/선택 색상
    const QColor FOCUS_COLOR = QColor("#e8f5ff");     // 연한 포커스된 항목 배경색
    const QColor FOCUS_TEXT_COLOR = QColor("#4F94DB"); // 연한 포커스된 항목 텍스트 색상
  
    
    // 헬퍼 함수들
    inline bool isDark(const QColor& color) {
        return (color.red() * 0.299 + color.green() * 0.587 + color.blue() * 0.114) < 128;
    }
    
    inline QColor getTextColor(const QColor& bgColor) {
        return isDark(bgColor) ? ::Qt::white : ::Qt::black; // ::Qt로 전역 네임스페이스 참조
    }
    
    inline QColor getPatternColor(PatternType type) {
        switch (type) {
            case PatternType::ROI:
                return ROI_COLOR;
            case PatternType::FID:
                return FIDUCIAL_COLOR;
            case PatternType::INS:
                return INSPECTION_COLOR;
            case PatternType::FIL:
                return FILTER_COLOR;
            default:
                return ::Qt::gray; // ::Qt로 전역 네임스페이스 참조
        }
    }
    
    inline QString messageBoxStyle() {
        return QString(
            "QMessageBox {"
            "    background-color: white;"
            "    color: black;"
            "}"
            "QMessageBox QLabel {"
            "    background-color: white;"
            "    color: black;"
            "    font-size: 12px;"
            "}"
            "QMessageBox QPushButton {"
            "    background-color: #f0f0f0;"
            "    color: black;"
            "    border: 1px solid #CCCCCC;"
            "    padding: 5px 15px;"
            "    margin: 2px;"
            "    border-radius: 3px;"
            "}"
            "QMessageBox QPushButton:hover {"
            "    background-color: #e0e0e0;"
            "    border-color: #999999;"
            "}"
            "QMessageBox QPushButton:pressed {"
            "    background-color: #d0d0d0;"
            "}"
            "QMessageBox QPushButton:default {"
            "    background-color: #f0f0f0;"
            "    color: black;"
            "    border: 1px solid #CCCCCC;"
            "}"
            "QMessageBox QPushButton:default:hover {"
            "    background-color: #e0e0e0;"
            "    border-color: #999999;"
            "}"
        );
    }

    inline QString contextMenuStyle() {
        return QString(
            "QMenu {"
            "    background-color: white;"
            "    color: black;"
            "    border: 1px solid #CCCCCC;"
            "    selection-background-color: #3498db;"
            "    selection-color: white;"
            "}"
            "QMenu::item {"
            "    background-color: white;"
            "    color: black;"
            "    padding: 5px 20px;"
            "    margin: 1px;"
            "}"
            "QMenu::item:selected {"
            "    background-color: #3498db;"
            "    color: white;"
            "}"
            "QMenu::item:disabled {"
            "    background-color: #F5F5F5;"
            "    color: #999999;"
            "}"
            "QMenu::separator {"
            "    height: 1px;"
            "    background-color: #CCCCCC;"
            "    margin: 2px 5px;"
            "}"
        );
    }

    inline QString buttonStyle(const QColor& color) {
        QColor textColor = getTextColor(color);
        return QString(
            "QPushButton {"
            "  background-color: %1;"
            "  color: %2;"
            "  border: 2px solid %1;"
            "  border-radius: 4px;"
            "  padding: 5px 10px;"
            "  margin: 0px;"
            "  min-width: 60px;"
            "  min-height: 32px;"
            "  font-weight: bold;"
            "}"
            "QPushButton:hover {"
            "  background-color: %3;"
            "  margin: 0px;"
            "}"
            "QPushButton:pressed {"
            "  background-color: %4;"
            "  margin: 0px;"
            "}"
        )
        .arg(color.name())
        .arg(textColor.name())
        .arg(color.darker(110).name())
        .arg(color.darker(120).name());
    }

    inline QString overlayButtonStyle(const QColor& color) {
        return QString(
            "QPushButton {"
            "  background-color: rgba(%1, %2, %3, 0.4);"
            "  color: %4;"
            "  border: 2px solid %4;"
            "  border-radius: 4px;"
            "  padding: 5px 10px;"
            "  margin: 0px;"
            "  min-width: 60px;"
            "  min-height: 32px;"
            "  font-weight: bold;"
            "}"
            "QPushButton:hover {"
            "  background-color: rgba(%1, %2, %3, 0.6);"
            "  border: 2px solid %5;"
            "  margin: 0px;"
            "}"
            "QPushButton:pressed {"
            "  background-color: rgba(%1, %2, %3, 1.0);"
            "  margin: 0px;"
            "}"
        )
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.name())
        .arg(color.lighter(120).name());
    }

    inline QString toggleButtonStyle(const QColor& uncheckedColor, const QColor& checkedColor, bool isChecked = false) {
        QColor uncheckedTextColor = getTextColor(uncheckedColor);
        QColor checkedTextColor = getTextColor(checkedColor);
    
        QString style = QString(
            "QPushButton {"
            "  background-color: %2;"
            "  color: %3;"
            "  border: 2px solid %1;"
            "  border-radius: 4px;"
            "  padding: 5px 10px;"
            "  margin: 0px;"
            "  min-width: 60px;"
            "  min-height: 32px;"
            "  font-weight: bold;"
            "}"
            "QPushButton:hover {"
            "  background-color: %4;"
            "}"
            "QPushButton:pressed {"
            "  background-color: %5;"
            "}"
            "QPushButton:checked {"
            "  background-color: %6;"
            "  color: %7;"
            "  border: 2px solid #FFFFFF;"
            "  padding: 5px 10px;"
            "  margin: 0px;"
            "  min-width: 60px;"
            "  min-height: 32px;"
            "  font-weight: bold;"
            "}"
            "QPushButton:checked:hover {"
            "  background-color: %8;"
            "}"
            "QPushButton:checked:pressed {"
            "  background-color: %9;"
            "}"
        )
        .arg(uncheckedColor.darker(130).name())
        .arg(uncheckedColor.name())
        .arg(uncheckedTextColor.name())
        .arg(uncheckedColor.darker(110).name())
        .arg(uncheckedColor.darker(120).name())
        .arg(checkedColor.name())
        .arg(checkedTextColor.name())
        .arg(checkedColor.darker(110).name())
        .arg(checkedColor.darker(120).name());
    
        return style;
    }

    inline QString overlayToggleButtonStyle(const QColor& uncheckedColor, const QColor& checkedColor, bool isChecked = false) {
        QString style = QString(
            "QPushButton {"
            "  background-color: rgba(%1, %2, %3, 0.4);"
            "  color: %4;"
            "  border: 2px solid %4;"
            "  border-radius: 4px;"
            "  padding: 5px 10px;"
            "  margin: 0px;"
            "  min-width: 60px;"
            "  min-height: 32px;"
            "  font-weight: bold;"
            "}"
            "QPushButton:hover {"
            "  background-color: rgba(%1, %2, %3, 0.6);"
            "  border: 2px solid %5;"
            "}"
            "QPushButton:pressed {"
            "  background-color: rgba(%1, %2, %3, 0.8);"
            "}"
            "QPushButton:checked {"
            "  background-color: rgba(%6, %7, %8, 0.4);"
            "  color: %9;"
            "  border: 2px solid %9;"
            "  padding: 5px 10px;"
            "  margin: 0px;"
            "  min-width: 60px;"
            "  min-height: 32px;"
            "  font-weight: bold;"
            "}"
            "QPushButton:checked:hover {"
            "  background-color: rgba(%6, %7, %8, 0.6);"
            "  border: 2px solid %10;"
            "}"
            "QPushButton:checked:pressed {"
            "  background-color: rgba(%6, %7, %8, 0.8);"
            "}"
        )
        .arg(uncheckedColor.red())
        .arg(uncheckedColor.green())
        .arg(uncheckedColor.blue())
        .arg(uncheckedColor.name())
        .arg(uncheckedColor.lighter(120).name())
        .arg(checkedColor.red())
        .arg(checkedColor.green())
        .arg(checkedColor.blue())
        .arg(checkedColor.name())
        .arg(checkedColor.lighter(120).name());
    
        return style;
    }

    inline QString sliderStyle() {
        return QString(
            "QSlider::groove:horizontal {"
            "  background: #f0f0f0;"
            "  height: 6px;"
            "  border-radius: 3px;"
            "}"
            "QSlider::handle:horizontal {"
            "  background: %1;"
            "  width: 18px;"
            "  height: 18px;"
            "  border-radius: 9px;"
            "  margin: -6px 0;"
            "}"
            "QSlider::sub-page:horizontal {"
            "  background: %2;"
            "  border-radius: 3px;"
            "}"
        )
        .arg(SLIDER_HANDLE_COLOR.name())
        .arg(SLIDER_ACTIVE_COLOR.name());
    }
}

#endif // COMMONDEFS_H