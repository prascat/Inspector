#ifndef IMAGEPROCESSOR_H
#define IMAGEPROCESSOR_H

#include <opencv2/opencv.hpp>
#include <QString>
#include <QMap>
#include <QList>
#include <memory>
#include "CommonDefs.h"  // 공통 정의 포함

// YOLO11-seg 세그멘테이션 결과 구조체
struct YoloSegResult {
    int classId;                    // 클래스 ID
    float confidence;               // 신뢰도
    cv::Rect bbox;                  // 바운딩 박스
    cv::Mat mask;                   // 세그멘테이션 마스크 (원본 이미지 크기)
    std::vector<cv::Point> contour; // 마스크 외곽선
};

class ImageProcessor {
public:
    ImageProcessor();
    ~ImageProcessor();
    
    // 필터 적용 총괄 함수
    static void applyFilter(cv::Mat& src, cv::Mat& dst, const FilterInfo& filter);
    
    // 여러 필터 순차 적용 함수
    static void applyFilters(cv::Mat& image, const QList<FilterInfo>& filters, const cv::Rect& roi);
    
    // 개별 필터 함수들
    static void applyThresholdFilter(cv::Mat& src, cv::Mat& dst, 
        int threshold, 
        int thresholdType = cv::THRESH_BINARY,
        int blockSize = 11,
        int C = 2);
    static void applyBlurFilter(cv::Mat& src, cv::Mat& dst, int kernelSize);
    static void applyCannyFilter(cv::Mat& src, cv::Mat& dst, int threshold1, int threshold2);
    static void applySobelFilter(cv::Mat& src, cv::Mat& dst, int kernelSize);
    static void applyLaplacianFilter(cv::Mat& src, cv::Mat& dst, int kernelSize);
    static void applySharpenFilter(cv::Mat& src, cv::Mat& dst, int strength);
    static void applyBrightnessFilter(cv::Mat& src, cv::Mat& dst, int value);
    static void applyContrastFilter(cv::Mat& src, cv::Mat& dst, int value);
    static void applyContourFilter(cv::Mat& src, cv::Mat& dst, 
        int threshold = 128, int minArea = 100, int thickness = 2,
        int contourMode = cv::RETR_EXTERNAL, 
        int contourApprox = cv::CHAIN_APPROX_SIMPLE);
    static void applyReflectionRemovalChromaticity(const cv::Mat& src, cv::Mat& dst, double threshold = 200.0, int inpaintRadius = 3);
    static void applyReflectionRemovalInpainting(const cv::Mat& src, cv::Mat& dst, double threshold = 200.0, int inpaintRadius = 5, int method = cv::INPAINT_TELEA);
    

    // ImageProcessor.h 수정
    static QList<QVector<QPoint>> extractContours(const cv::Mat& src, int threshold, int minArea,
        int contourMode, int contourApprox, int contourTarget = 0);    
    static void applyMaskFilter(cv::Mat& src, cv::Mat& dst, const QRect& maskRect, int maskValue = 255);

    static bool compareContours(const cv::Mat& ref, const cv::Mat& target, double threshold, double& diffValue);
    
    // 기존 compareContours 함수를 확장:
    static bool compareContours(const cv::Mat& ref, const cv::Mat& target, 
                               double threshold, double& diffValue,
                               int contourMode = cv::RETR_EXTERNAL, 
                               int contourApprox = cv::CHAIN_APPROX_SIMPLE,
                               int thresholdValue = 128);
    
    // 필터 파라미터 검증 함수 (필요시 파라미터 보정)
    static int validateKernelSize(int size);
    
    // 기본 필터 파라미터 생성
    static QMap<QString, int> getDefaultParams(int filterType);
    
    // STRIP 검사 관련 함수들
    static bool analyzeBlackRegionThickness(const cv::Mat& binaryImage, std::vector<cv::Point>& positions, 
                                          std::vector<float>& thicknesses, QString& direction);
    static int measureVerticalThicknessAtX(const cv::Mat& binaryImage, int x, int yStart, int height);
    static cv::Point findMaxThicknessPosition(const std::vector<cv::Point>& positions, 
                                            const std::vector<float>& thicknesses, float& maxThickness);
    static std::vector<cv::Point> findLocalMaxGradientPositions(const std::vector<cv::Point>& positions, 
                                                               const std::vector<float>& thicknesses, 
                                                               int windowSize = 10, float threshold = 0.5f);
    static cv::Point findMaxThicknessGradientPosition(const std::vector<cv::Point>& positions, 
                                                     const std::vector<float>& thicknesses, 
                                                     float& maxGradientValue, std::vector<float>& gradients);
    // 간소화된 시그니처: 패턴 정보 하나만 넘기면 내부에서 필요한 파라미터를 참조합니다.
    static bool performStripInspection(const cv::Mat& roiImage, const cv::Mat& templateImage,
                                     const PatternInfo& pattern,
                                     double& score, cv::Point& startPoint,
                                     cv::Point& maxGradientPoint, std::vector<cv::Point>& gradientPoints,
                                     cv::Mat& resultImage, std::vector<cv::Point>* edgePoints = nullptr,
                                     bool* stripLengthPassed = nullptr, double* stripMeasuredLength = nullptr, 
                                     cv::Point* stripLengthStartPoint = nullptr, cv::Point* stripLengthEndPoint = nullptr,
                                     std::vector<cv::Point>* frontThicknessPoints = nullptr,
                                     std::vector<cv::Point>* rearThicknessPoints = nullptr,
                                     std::vector<cv::Point>* frontBlackRegionPoints = nullptr,
                                     std::vector<cv::Point>* rearBlackRegionPoints = nullptr,
                                     double* stripMeasuredLengthPx = nullptr,
                                     cv::Point* frontBoxCenter = nullptr, cv::Size* frontBoxSize = nullptr,
                                     cv::Point* rearBoxCenter = nullptr, cv::Size* rearBoxSize = nullptr,
                                     cv::Point* edgeBoxCenter = nullptr, cv::Size* edgeBoxSize = nullptr,
                                     cv::Point* frontMinScanTop = nullptr, cv::Point* frontMinScanBottom = nullptr,
                                     cv::Point* frontMaxScanTop = nullptr, cv::Point* frontMaxScanBottom = nullptr,
                                     cv::Point* rearMinScanTop = nullptr, cv::Point* rearMinScanBottom = nullptr,
                                     cv::Point* rearMaxScanTop = nullptr, cv::Point* rearMaxScanBottom = nullptr,
                                     std::vector<std::pair<cv::Point, cv::Point>>* frontScanLines = nullptr,
                                     std::vector<std::pair<cv::Point, cv::Point>>* rearScanLines = nullptr);
    
    // CRIMP 검사 관련 함수는 현재 비활성화됨 (향후 구현 예정)
    
    // ===== TensorRT PatchCore 관련 (JETSON용) =====
#ifdef USE_TENSORRT
    struct TensorRTPatchCoreModelInfo {
        void* runtime = nullptr;          // nvinfer1::IRuntime*
        void* engine = nullptr;           // nvinfer1::ICudaEngine*
        void* context = nullptr;          // nvinfer1::IExecutionContext*
        void* cudaStream = nullptr;       // cudaStream_t
        void* inputBuffer = nullptr;      // GPU 메모리
        void* outputBuffer = nullptr;     // GPU 메모리 (anomaly_map)
        void* scoreBuffer = nullptr;      // GPU 메모리 (pred_score)
        size_t inputSize = 0;
        size_t outputSize = 0;
        size_t scoreSize = 0;
        int inputWidth = 224;
        int inputHeight = 224;
        float normMin = 0.0f;
        float normMax = 100.0f;
    };
    
    static QMap<QString, TensorRTPatchCoreModelInfo> s_tensorrtPatchCoreModels;
    
    // TensorRT PatchCore 초기화/해제
    static bool initPatchCoreTensorRT(const QString& enginePath, const QString& device = "GPU");
    static void releasePatchCoreTensorRT();
    static bool isTensorRTPatchCoreLoaded();
    
    // TensorRT 추론
    static bool runPatchCoreTensorRTInference(
        const QString& enginePath,
        const cv::Mat& image,
        float& anomalyScore,
        cv::Mat& anomalyMap,
        float threshold = 0.0f
    );
    
    // TensorRT 배치 추론
    static bool runPatchCoreTensorRTBatchInference(
        const QString& enginePath,
        const std::vector<cv::Mat>& images,
        std::vector<float>& anomalyScores,
        std::vector<cv::Mat>& anomalyMaps,
        float threshold = 0.0f
    );
    
    // TensorRT 멀티모델 병렬 추론 (CUDA 스트림 활용)
    static bool runPatchCoreTensorRTMultiModelInference(
        const QMap<QString, std::vector<cv::Mat>>& modelImages,  // key: enginePath, value: images
        QMap<QString, std::vector<float>>& modelScores,
        QMap<QString, std::vector<cv::Mat>>& modelMaps
    );
#endif  // USE_TENSORRT
    
    // ===== ONNX Runtime PatchCore 관련 (x86 Linux용) =====
#ifdef USE_ONNX
    struct ONNXPatchCoreModelInfo {
        void* session = nullptr;          // Ort::Session*
        void* memoryInfo = nullptr;       // Ort::MemoryInfo*
        int inputWidth = 224;
        int inputHeight = 224;
        float normMin = 0.0f;
        float normMax = 100.0f;
    };
    
    static QMap<QString, ONNXPatchCoreModelInfo> s_onnxPatchCoreModels;
    
    // ONNX PatchCore 초기화/해제
    static bool initPatchCoreONNX(const QString& modelPath);
    static void releasePatchCoreONNX();
    static bool isONNXPatchCoreLoaded();
    
    // ONNX 추론
    static bool runPatchCoreONNXInference(
        const QString& modelPath,
        const cv::Mat& image,
        float& anomalyScore,
        cv::Mat& anomalyMap,
        float threshold = 0.0f
    );
    
    // ONNX 배치 추론
    static bool runPatchCoreONNXBatchInference(
        const QString& modelPath,
        const std::vector<cv::Mat>& images,
        std::vector<float>& anomalyScores,
        std::vector<cv::Mat>& anomalyMaps,
        float threshold = 0.0f
    );
#endif  // USE_ONNX
};

#endif // IMAGEPROCESSOR_H