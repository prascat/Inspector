#ifndef IMAGEPROCESSOR_H
#define IMAGEPROCESSOR_H

#include <opencv2/opencv.hpp>
#include <QString>
#include <QMap>
#include <QList>
#include "CommonDefs.h"  // 공통 정의 포함

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
                                     cv::Point* rearBoxCenter = nullptr, cv::Size* rearBoxSize = nullptr);
    
    // CRIMP SHAPE 검사 관련 함수
    static bool performShapeMatching(const cv::Mat& roiImage, const cv::Mat& templateImage,
                                   const PatternInfo& pattern,
                                   double& matchScore, cv::Point& matchLocation,
                                   std::vector<std::vector<cv::Point>>& currentContours,
                                   std::vector<std::vector<cv::Point>>& templateContours,
                                   cv::Mat& diffMask);
};

#endif // IMAGEPROCESSOR_H