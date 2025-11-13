#include "ImageProcessor.h"
#include <opencv2/imgproc.hpp>
#include <QDebug>
#include <algorithm>
#include <iostream>
#include <ctime>
#include <cstdlib>
#include <cmath> 

ImageProcessor::ImageProcessor() {

}

ImageProcessor::~ImageProcessor() {

}

QList<QVector<QPoint>> ImageProcessor::extractContours(const cv::Mat& src, int threshold, int minArea,
                                                     int contourMode, int contourApprox, int contourTarget) {
    QList<QVector<QPoint>> result;
    
    // OpenCV 이미지 처리를 위한 이미지 준비
    cv::Mat gray;
    if (src.channels() == 3) {
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    } else {
        src.copyTo(gray);
    }
    
    // 이진화 (contourTarget 값에 따라 THRESH_BINARY 또는 THRESH_BINARY_INV 선택)
    cv::Mat binary;
    int threshType = (contourTarget == 0) ? cv::THRESH_BINARY : cv::THRESH_BINARY_INV;
    cv::threshold(gray, binary, threshold, 255, threshType);
    
    // 윤곽선 찾기
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    try {
        cv::findContours(binary, contours, hierarchy, contourMode, contourApprox);
    } catch (const cv::Exception& e) {
        std::cerr << "OpenCV 예외 발생: " << e.what() << std::endl;
        return result;
    }
    
    // 유효한 윤곽선만 변환
    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        
        // 최소 면적 이상이고, 이미지 경계와 일치하지 않는 윤곽선만 추가
        // 이는 테두리 박스가 아닌 실제 물체만 검출하도록 함
        if (area >= minArea) {
            cv::Rect boundRect = cv::boundingRect(contour);
            
            // 이미지 경계와 완전히 일치하는 윤곽선은 무시 (INS 박스 자체는 제외)
            if (!(boundRect.x <= 1 && boundRect.y <= 1 && 
                  boundRect.x + boundRect.width >= gray.cols - 2 && 
                  boundRect.y + boundRect.height >= gray.rows - 2)) {
                  
                QVector<QPoint> qtContour;
                for (const cv::Point& pt : contour) {
                    qtContour.append(QPoint(pt.x, pt.y));
                }
                result.append(qtContour);
            }
        }
    }
    
    return result;
}

bool ImageProcessor::compareContours(const cv::Mat& ref, const cv::Mat& target, double threshold, double& diffValue) {
    // 두 이미지가 비어있는지 확인
    if (ref.empty() || target.empty()) {
        diffValue = std::numeric_limits<double>::max();
        return false;
    }

    // 두 이미지를 그레이스케일로 변환
    cv::Mat refGray, targetGray;

    // 그레이스케일 변환
    if (ref.channels() == 3) {
        cv::cvtColor(ref, refGray, cv::COLOR_BGR2GRAY);
    } else {
        ref.copyTo(refGray);
    }

    if (target.channels() == 3) {
        cv::cvtColor(target, targetGray, cv::COLOR_BGR2GRAY);
    } else {
        target.copyTo(targetGray);
    }

    // 단순 이진화 (이미 INS에서 필터가 적용된 이미지이므로)
    cv::Mat refBin, targetBin;
    int thresholdValue = 128;
    cv::threshold(refGray, refBin, thresholdValue, 255, cv::THRESH_BINARY);
    cv::threshold(targetGray, targetBin, thresholdValue, 255, cv::THRESH_BINARY);

    // 컨투어 추출 (단순하게)
    std::vector<std::vector<cv::Point>> refContours, targetContours;
    std::vector<cv::Vec4i> refHierarchy, targetHierarchy;

    cv::findContours(refBin, refContours, refHierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    cv::findContours(targetBin, targetContours, targetHierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // 유효한 컨투어가 없으면 실패
    if (refContours.empty() || targetContours.empty()) {
        diffValue = std::numeric_limits<double>::max();
        return false;
    }

    // 가장 큰 컨투어 찾기
    std::vector<cv::Point> refMainContour, targetMainContour;
    double maxRefArea = 0, maxTargetArea = 0;
    
    for (const auto& contour : refContours) {
        double area = cv::contourArea(contour);
        if (area > maxRefArea && area > 50) { // 최소 면적 필터링
            maxRefArea = area;
            refMainContour = contour;
        }
    }
    
    for (const auto& contour : targetContours) {
        double area = cv::contourArea(contour);
        if (area > maxTargetArea && area > 50) { // 최소 면적 필터링
            maxTargetArea = area;
            targetMainContour = contour;
        }
    }

    // 유효한 메인 컨투어가 없으면 실패
    if (refMainContour.empty() || targetMainContour.empty()) {
        diffValue = std::numeric_limits<double>::max();
        return false;
    }

    // OpenCV matchShapes 사용 (단순하고 효과적)
    double matchResult = cv::matchShapes(refMainContour, targetMainContour, cv::CONTOURS_MATCH_I1, 0);
    diffValue = matchResult;
    
    // 추가 정보를 위한 면적 비교
    double areaRatio = maxTargetArea / maxRefArea;
    double areaError = abs(1.0 - areaRatio);
    
    printf("[ImageProcessor] 윤곽선 매칭 결과:\n");
    printf("  기준 면적: %.2f, 대상 면적: %.2f (비율: %.4f, 오차: %.4f)\n", 
           maxRefArea, maxTargetArea, areaRatio, areaError);
    printf("  matchShapes 값: %.6f, 임계값: %.6f, 결과: %s\n", 
           matchResult, threshold, (diffValue <= threshold) ? "양품" : "불량");
    fflush(stdout);

    // 임계값 이내면 양품(true), 아니면 불량(false)
    // matchShapes는 값이 작을수록 유사함 (0에 가까울수록 완전 일치)
    return diffValue <= threshold;
}

// 단일 필터 적용 함수
void ImageProcessor::applyFilter(cv::Mat& src, cv::Mat& dst, const FilterInfo& filter) {
    if (!filter.enabled) {
        src.copyTo(dst); // 필터 비활성화 시 원본 그대로 리턴
        return;
    }
    
    switch (filter.type) {
        case FILTER_THRESHOLD: {
            int threshold = filter.params.value("threshold", 128);
            int thresholdType = filter.params.value("thresholdType", cv::THRESH_BINARY);
            int blockSize = filter.params.value("blockSize", 11);
            int C = filter.params.value("C", 2);
            
            applyThresholdFilter(src, dst, threshold, thresholdType, blockSize, C);
            break;
        }
        case FILTER_BLUR: {
            int kernelSize = validateKernelSize(filter.params.value("kernelSize", 3));
            applyBlurFilter(src, dst, kernelSize);
            break;
        }
        case FILTER_CANNY: {
            int threshold1 = filter.params.value("threshold1", 100);
            int threshold2 = filter.params.value("threshold2", 200);
            applyCannyFilter(src, dst, threshold1, threshold2);
            break;
        }
        case FILTER_SOBEL: {
            int kernelSize = validateKernelSize(filter.params.value("sobelKernelSize", 3));
            applySobelFilter(src, dst, kernelSize);
            break;
        }
        case FILTER_LAPLACIAN: {
            int kernelSize = validateKernelSize(filter.params.value("laplacianKernelSize", 3));
            applyLaplacianFilter(src, dst, kernelSize);
            break;
        }
        case FILTER_SHARPEN: {
            int strength = filter.params.value("sharpenStrength", 3);
            applySharpenFilter(src, dst, strength);
            break;
        }
        case FILTER_BRIGHTNESS: {
            int brightness = filter.params.value("brightness", 0);
            applyBrightnessFilter(src, dst, brightness);
            break;
        }
        case FILTER_CONTRAST: {
            int contrast = filter.params.value("contrast", 0);
            applyContrastFilter(src, dst, contrast);
            break;
        }
        case FILTER_CONTOUR: { 
            int threshold = filter.params.value("threshold", 128);
            int minArea = filter.params.value("minArea", 100);
            int thickness = filter.params.value("thickness", 2);
            int contourMode = filter.params.value("contourMode", cv::RETR_EXTERNAL);
            int contourApprox = filter.params.value("contourApprox", cv::CHAIN_APPROX_SIMPLE);
            
            applyContourFilter(src, dst, threshold, minArea, thickness, contourMode, contourApprox);
            break;
        }
        default:
            src.copyTo(dst); // 알 수 없는 필터 유형은 원본 그대로 리턴
            break;
    }
}

// 여러 필터 순차 적용 함수
void ImageProcessor::applyFilters(cv::Mat& image, const QList<FilterInfo>& filters, const cv::Rect& roi) {
    if (filters.isEmpty()) return;
    
    // ROI 유효성 검사
    if (roi.width <= 0 || roi.height <= 0 || 
        roi.x < 0 || roi.y < 0 || 
        roi.x + roi.width > image.cols || 
        roi.y + roi.height > image.rows) {
        return;
    }
    
    // ROI 영역만 복사하여 처리
    cv::Mat roiMat = image(roi).clone();
    
    // 필터 순차 적용
    for (const FilterInfo& filter : filters) {
        if (!filter.enabled) continue;
        cv::Mat processed;
        if (filter.type == FILTER_MASK) {
            int maskValue = filter.params.value("maskValue", 255);
            applyMaskFilter(roiMat, processed, QRect(0, 0, roi.width, roi.height), maskValue);
        } else {
            applyFilter(roiMat, processed, filter);
        }
        if (!processed.empty()) {
            processed.copyTo(roiMat);
        }
        
        // 처리된 영상이 비어있지 않으면 업데이트
        if (!processed.empty()) {
            processed.copyTo(roiMat);
        }
    }
    
    // 처리된 ROI를 원본 이미지에 복사
    roiMat.copyTo(image(roi));
}

void ImageProcessor::applyContourFilter(cv::Mat& src, cv::Mat& dst, 
                                     int threshold, int minArea, int thickness,
                                     int contourMode, int contourApprox) {
    // 결과 이미지 초기화 (그냥 원본 복사, 그리기 행위 없음)
    src.copyTo(dst);
    
    // 그레이스케일 변환
    cv::Mat gray;
    if (src.channels() == 3) {
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    } else {
        src.copyTo(gray);
    }
    
    // 이진화
    cv::Mat binary;
    cv::threshold(gray, binary, threshold, 255, cv::THRESH_BINARY);
    
    // 컨투어 찾기만 수행 - 그리기 행위 없음
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    try {
        cv::findContours(binary, contours, hierarchy, contourMode, contourApprox);
    } catch (const cv::Exception& e) {
        std::cerr << "OpenCV 예외 발생: " << e.what() << std::endl;
        // 예외가 발생하면 기본값으로 시도
        cv::findContours(binary, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    }
}

void ImageProcessor::applyThresholdFilter(cv::Mat& src, cv::Mat& dst, int threshold, int thresholdType, int blockSize, int C) {
    // 입력 이미지를 그레이스케일로 변환
    cv::Mat gray;
    cv::cvtColor(src, gray, cv::COLOR_RGB2GRAY);
    
    // 적응형 이진화 처리
    if (thresholdType == THRESH_ADAPTIVE_MEAN || thresholdType == THRESH_ADAPTIVE_GAUSSIAN) {
        // blockSize 검증 - 반드시 홀수이고 1보다 커야 함
        if (blockSize % 2 == 0) blockSize++;
        if (blockSize <= 1) blockSize = 3;
        
        int adaptiveMethod = (thresholdType == THRESH_ADAPTIVE_MEAN) ? 
                             cv::ADAPTIVE_THRESH_MEAN_C : cv::ADAPTIVE_THRESH_GAUSSIAN_C;
        
        cv::adaptiveThreshold(gray, dst, 255, adaptiveMethod, cv::THRESH_BINARY, blockSize, C);
    }
    // 일반 이진화 처리
    else {
        cv::threshold(gray, dst, threshold, 255, thresholdType);
    }
    
    // 결과를 다시 RGB로 변환 (UI 표시용)
    cv::cvtColor(dst, dst, cv::COLOR_GRAY2RGB);
}


// 블러 필터
void ImageProcessor::applyBlurFilter(cv::Mat& src, cv::Mat& dst, int kernelSize) {
    cv::GaussianBlur(src, dst, cv::Size(kernelSize, kernelSize), 0);
}

// 캐니 엣지 검출 필터
void ImageProcessor::applyCannyFilter(cv::Mat& src, cv::Mat& dst, int threshold1, int threshold2) {
    cv::Mat gray, edges;
    cv::cvtColor(src, gray, cv::COLOR_RGB2GRAY);
    cv::Canny(gray, edges, threshold1, threshold2);
    
    // 엣지 결과를 컬러로 변환 (검은 배경에 흰색 엣지)
    dst = cv::Mat::zeros(src.size(), src.type());
    for (int y = 0; y < edges.rows; y++) {
        for (int x = 0; x < edges.cols; x++) {
            if (edges.at<uchar>(y, x) > 0) {
                dst.at<cv::Vec3b>(y, x) = cv::Vec3b(255, 255, 255);
            }
        }
    }
}

// 소벨 엣지 검출 필터
void ImageProcessor::applySobelFilter(cv::Mat& src, cv::Mat& dst, int kernelSize) {
    cv::Mat gray, grad_x, grad_y, abs_grad_x, abs_grad_y, sobel_result;
    cv::cvtColor(src, gray, cv::COLOR_RGB2GRAY);
    
    cv::Sobel(gray, grad_x, CV_16S, 1, 0, kernelSize);
    cv::Sobel(gray, grad_y, CV_16S, 0, 1, kernelSize);
    
    cv::convertScaleAbs(grad_x, abs_grad_x);
    cv::convertScaleAbs(grad_y, abs_grad_y);
    
    cv::addWeighted(abs_grad_x, 0.5, abs_grad_y, 0.5, 0, sobel_result);
    cv::cvtColor(sobel_result, dst, cv::COLOR_GRAY2RGB);
}

// 라플라시안 필터
void ImageProcessor::applyLaplacianFilter(cv::Mat& src, cv::Mat& dst, int kernelSize) {
    cv::Mat gray, laplacian;
    cv::cvtColor(src, gray, cv::COLOR_RGB2GRAY);
    cv::Laplacian(gray, laplacian, CV_16S, kernelSize);
    
    cv::Mat abs_laplacian;
    cv::convertScaleAbs(laplacian, abs_laplacian);
    cv::cvtColor(abs_laplacian, dst, cv::COLOR_GRAY2RGB);
}

// 선명하게 필터
void ImageProcessor::applySharpenFilter(cv::Mat& src, cv::Mat& dst, int strength) {
    cv::Mat blurred;
    cv::GaussianBlur(src, blurred, cv::Size(5, 5), 0);
    
    // 언샤프 마스킹
    cv::addWeighted(src, 1.0 + (strength * 0.1), blurred, -(strength * 0.1), 0, dst);
}

// 밝기 필터
void ImageProcessor::applyBrightnessFilter(cv::Mat& src, cv::Mat& dst, int value) {
    src.copyTo(dst);
    
    for (int y = 0; y < dst.rows; y++) {
        for (int x = 0; x < dst.cols; x++) {
            for (int c = 0; c < 3; c++) {
                int pixel = dst.at<cv::Vec3b>(y, x)[c] + value;
                dst.at<cv::Vec3b>(y, x)[c] = cv::saturate_cast<uchar>(pixel);
            }
        }
    }
}

// 대비 필터
void ImageProcessor::applyContrastFilter(cv::Mat& src, cv::Mat& dst, int value) {
    src.copyTo(dst);
    double factor = (259.0 * (value + 255)) / (255.0 * (259 - value));
    
    for (int y = 0; y < dst.rows; y++) {
        for (int x = 0; x < dst.cols; x++) {
            for (int c = 0; c < 3; c++) {
                int pixel = static_cast<int>(factor * (dst.at<cv::Vec3b>(y, x)[c] - 128) + 128);
                dst.at<cv::Vec3b>(y, x)[c] = cv::saturate_cast<uchar>(pixel);
            }
        }
    }
}

void ImageProcessor::applyMaskFilter(cv::Mat& src, cv::Mat& dst, const QRect& maskRect, int maskValue) {
    src.copyTo(dst);
    cv::Rect roi(maskRect.x(), maskRect.y(), maskRect.width(), maskRect.height());
    if (roi.x >= 0 && roi.y >= 0 && roi.x + roi.width <= dst.cols && roi.y + roi.height <= dst.rows) {
        cv::Mat maskArea = dst(roi);
        maskArea.setTo(maskValue); // 255: 흰색, 0: 검은색
    }
}

// 커널 크기 검증 (항상 홀수로 만들기)
int ImageProcessor::validateKernelSize(int size) {
    if (size % 2 == 0) return size - 1;
    return size;
}

// 기본 필터 파라미터 생성 함수
QMap<QString, int> ImageProcessor::getDefaultParams(int filterType) {
    QMap<QString, int> params;
    
    switch (filterType) {
        case FILTER_THRESHOLD:
            params["threshold"] = 128;
            params["thresholdType"] = cv::THRESH_BINARY; // 기본 이진화 타입
            params["blockSize"] = 11;  // 적응형 이진화 블록 크기 (홀수여야 함)
            params["C"] = 2;           // 적응형 이진화 상수 C
            break;
        case FILTER_BLUR:
            params["kernelSize"] = 3;
            break;
        case FILTER_CANNY:
            params["threshold1"] = 100;
            params["threshold2"] = 200;
            break;
        case FILTER_SOBEL:
            params["sobelKernelSize"] = 3;
            break;
        case FILTER_LAPLACIAN:
            params["laplacianKernelSize"] = 3;
            break;
        case FILTER_SHARPEN:
            params["sharpenStrength"] = 3;
            break;
        case FILTER_BRIGHTNESS:
            params["brightness"] = 0;
            break;
        case FILTER_CONTRAST:
            params["contrast"] = 0;
            break;
        case FILTER_CONTOUR:
            params["threshold"] = 128;
            params["minArea"] = 100;
            params["thickness"] = 2;
            params["contourMode"] = cv::RETR_EXTERNAL;
            params["contourApprox"] = cv::CHAIN_APPROX_SIMPLE;
            break;
    }
    
    return params;
}

// STRIP 검사 관련 함수들 구현
bool ImageProcessor::analyzeBlackRegionThickness(const cv::Mat& binaryImage, std::vector<cv::Point>& positions, 
                                                std::vector<float>& thicknesses, QString& direction) {
    positions.clear();
    thicknesses.clear();
    direction = "vertical_scan";
    
    // 검은색 영역만 추출 (값이 0인 픽셀들)
    cv::Mat blackRegions;
    cv::threshold(binaryImage, blackRegions, 1, 255, cv::THRESH_BINARY_INV);
    
    // 노이즈 제거
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(blackRegions, blackRegions, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(blackRegions, blackRegions, cv::MORPH_CLOSE, kernel);
    
    // 윤곽선 찾기
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(blackRegions, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    if (contours.empty()) {
        return false;
    }
    
    // 가장 큰 윤곽선 선택
    int maxIdx = 0;
    double maxArea = 0;
    for (size_t i = 0; i < contours.size(); i++) {
        double area = cv::contourArea(contours[i]);
        if (area > maxArea) {
            maxArea = area;
            maxIdx = i;
        }
    }
    
    // 윤곽선의 경계 사각형
    cv::Rect boundRect = cv::boundingRect(contours[maxIdx]);
    
    // X축 방향으로 스캔하면서 각 X 위치에서의 세로 두께 측정
    for (int scanX = boundRect.x; scanX < boundRect.x + boundRect.width; scanX++) {
        int thickness = measureVerticalThicknessAtX(blackRegions, scanX, boundRect.y, boundRect.height);
        if (thickness > 0) {
            positions.push_back(cv::Point(scanX, boundRect.y + boundRect.height / 2));
            thicknesses.push_back(static_cast<float>(thickness));
        }
    }
    
    return !positions.empty();
}

int ImageProcessor::measureVerticalThicknessAtX(const cv::Mat& binaryImage, int x, int yStart, int height) {
    if (x >= binaryImage.cols) {
        return 0;
    }
    
    int maxThickness = 0;
    int currentThickness = 0;
    
    // Y 방향으로 스캔하면서 연속된 검은색 픽셀의 최대 길이 찾기
    for (int y = yStart; y < std::min(yStart + height, binaryImage.rows); y++) {
        // 해당 픽셀이 검은색인지 확인 (값이 255, 검은색 영역을 흰색으로 변환했으므로)
        if (binaryImage.at<uchar>(y, x) == 255) {
            currentThickness++;
        } else {
            // 연속성이 끊어졌을 때 최대값 업데이트
            if (currentThickness > maxThickness) {
                maxThickness = currentThickness;
            }
            currentThickness = 0;
        }
    }
    
    // 마지막 구간 체크
    if (currentThickness > maxThickness) {
        maxThickness = currentThickness;
    }
    
    return maxThickness;
}

cv::Point ImageProcessor::findMaxThicknessPosition(const std::vector<cv::Point>& positions, 
                                                 const std::vector<float>& thicknesses, float& maxThickness) {
    if (thicknesses.empty()) {
        maxThickness = 0;
        return cv::Point(0, 0);
    }
    
    // 최대 두께와 그 위치 찾기
    auto maxIt = std::max_element(thicknesses.begin(), thicknesses.end());
    size_t maxIdx = std::distance(thicknesses.begin(), maxIt);
    maxThickness = *maxIt;
    
    return positions[maxIdx];
}

std::vector<cv::Point> ImageProcessor::findLocalMaxGradientPositions(const std::vector<cv::Point>& positions, 
                                                                    const std::vector<float>& thicknesses, 
                                                                    int windowSize, float threshold) {
    std::vector<cv::Point> localMaxima;
    
    if (thicknesses.size() < static_cast<size_t>(windowSize)) {
        return localMaxima;
    }
    
    // 두께 변화율(gradient) 계산
    std::vector<float> gradients;
    for (size_t i = 1; i < thicknesses.size(); i++) {
        gradients.push_back(thicknesses[i] - thicknesses[i-1]);
    }
    
    // 절댓값으로 변화율의 크기 계산
    std::vector<float> absGradients;
    for (float grad : gradients) {
        absGradients.push_back(std::abs(grad));
    }
    
    // 로컬 최대값들 찾기
    for (size_t i = windowSize; i < absGradients.size() - windowSize; i++) {
        // 현재 지점 주변의 window 내에서 최대값인지 확인
        size_t windowStart = std::max(size_t(0), i - windowSize);
        size_t windowEnd = std::min(absGradients.size(), i + windowSize + 1);
        
        float currentGradient = absGradients[i];
        
        // window 내에서 최대값인지 확인
        bool isLocalMax = true;
        for (size_t j = windowStart; j < windowEnd; j++) {
            if (j != i && absGradients[j] > currentGradient) {
                isLocalMax = false;
                break;
            }
        }
        
        // 현재 지점이 window 내에서 최대값이고 threshold를 넘는지 확인
        if (isLocalMax && currentGradient > threshold) {
            // 중복 제거: 너무 가까운 지점들은 제외
            bool isDuplicate = false;
            for (const cv::Point& existingPos : localMaxima) {
                if (std::abs(positions[i+1].x - existingPos.x) < windowSize) {
                    isDuplicate = true;
                    break;
                }
            }
            
            if (!isDuplicate && i+1 < positions.size()) {
                localMaxima.push_back(positions[i+1]);
            }
        }
    }
    
    return localMaxima;
}

cv::Point ImageProcessor::findMaxThicknessGradientPosition(const std::vector<cv::Point>& positions, 
                                                         const std::vector<float>& thicknesses, 
                                                         float& maxGradientValue, std::vector<float>& gradients) {
    gradients.clear();
    maxGradientValue = 0;
    
    if (thicknesses.size() < 3) {
        return cv::Point(0, 0);
    }
    
    // 두께 변화율(gradient) 계산
    for (size_t i = 1; i < thicknesses.size(); i++) {
        gradients.push_back(thicknesses[i] - thicknesses[i-1]);
    }
    
    // 절댓값으로 변화율의 크기 계산
    std::vector<float> absGradients;
    for (float grad : gradients) {
        absGradients.push_back(std::abs(grad));
    }
    
    // 최대 변화율 지점 찾기
    auto maxIt = std::max_element(absGradients.begin(), absGradients.end());
    size_t maxIdx = std::distance(absGradients.begin(), maxIt);
    maxGradientValue = gradients[maxIdx];
    
    return positions[maxIdx + 1]; // gradient는 인덱스가 1부터 시작하므로 +1
}

bool ImageProcessor::performStripInspection(const cv::Mat& roiImage, const cv::Mat& templateImage, 
                                           const PatternInfo& pattern, double& score, cv::Point& startPoint, 
                                           cv::Point& maxGradientPoint, std::vector<cv::Point>& gradientPoints, 
                                           cv::Mat& resultImage, std::vector<cv::Point>* edgePoints,
                                           bool* stripLengthPassed, double* stripMeasuredLength, 
                                           cv::Point* stripLengthStartPoint, cv::Point* stripLengthEndPoint,
                                           std::vector<cv::Point>* frontThicknessPoints,
                                           std::vector<cv::Point>* rearThicknessPoints,
                                           std::vector<cv::Point>* frontBlackRegionPoints,
                                           std::vector<cv::Point>* rearBlackRegionPoints) {
    
    // 결과 이미지용으로 원본의 깨끗한 복사본 생성 (마스킹 제거)
    cv::Mat cleanOriginal;
    roiImage.copyTo(cleanOriginal);
    
    // 검은색 영역 제거 (마스킹된 부분을 원본 색상으로 복원)
    cv::Mat grayCheck;
    cv::cvtColor(cleanOriginal, grayCheck, cv::COLOR_BGR2GRAY);
    cv::Mat blackMask = (grayCheck == 0);
    
    // 검은색 영역을 주변 평균 색상으로 채우기
    if (cv::countNonZero(blackMask) > 0) {
        cv::Scalar avgColor = cv::mean(cleanOriginal, ~blackMask);
        cleanOriginal.setTo(avgColor, blackMask);
    }
    
    try {

    gradientPoints.clear();

    // 로컬 변수로 패턴의 STRIP 관련 설정을 복사하여
    // 기존 구현에서 사용하던 변수명을 그대로 사용할 수 있게 함
    // NOTE: 패턴의 각도로 검사 수행
    double angle = pattern.angle;  // INS 패턴 각도 적용
    double passThreshold = pattern.passThreshold;
    int morphKernelSize = pattern.stripMorphKernelSize;
    float gradientThreshold = pattern.stripGradientThreshold;
    int gradientStartPercent = pattern.stripGradientStartPercent;
    int gradientEndPercent = pattern.stripGradientEndPercent;
    int minDataPoints = pattern.stripMinDataPoints;
    bool stripFrontEnabled = pattern.stripFrontEnabled;
    bool stripRearEnabled = pattern.stripRearEnabled;
    int thicknessBoxWidth = pattern.stripThicknessBoxWidth;
    int thicknessBoxHeight = pattern.stripThicknessBoxHeight;
    int thicknessMin = pattern.stripThicknessMin;
    int thicknessMax = pattern.stripThicknessMax;
    int rearThicknessBoxWidth = pattern.stripRearThicknessBoxWidth;
    int rearThicknessBoxHeight = pattern.stripRearThicknessBoxHeight;
    int rearThicknessMin = pattern.stripRearThicknessMin;
    int rearThicknessMax = pattern.stripRearThicknessMax;

    // ROI 내부 패턴 영역 계산 (extractROI와 완전히 동일한 로직 적용)
    double angleRadians = std::abs(angle) * M_PI / 180.0;
    double rectWidth = pattern.rect.width();
    double rectHeight = pattern.rect.height();
    
    // extractROI와 완전히 동일한 회전 크기 계산
    double rotatedWidth = std::abs(rectWidth * std::cos(angleRadians)) + std::abs(rectHeight * std::sin(angleRadians));
    double rotatedHeight = std::abs(rectWidth * std::sin(angleRadians)) + std::abs(rectHeight * std::cos(angleRadians));
    
    // padding 제거로 정확한 크기 계산 (extractROI와 일치)
    int maxSize = static_cast<int>(std::max(rotatedWidth, rotatedHeight));
    
    // extractROI가 수정됨: 패턴이 원래 위치 그대로 배치됨
    // ROI 시작점을 기준으로 패턴의 상대 위치 계산
    cv::Point2f patternCenter(pattern.rect.center().x(), pattern.rect.center().y());
    int halfSize = maxSize / 2;
    cv::Point roiStart(std::round(patternCenter.x) - halfSize, 
                      std::round(patternCenter.y) - halfSize);
    
    // extractROI에서 패턴이 원래 위치에 배치되므로 ROI 내 패턴 위치도 그에 맞게 계산
    cv::Rect roiPatternRect(
        static_cast<int>(pattern.rect.x() - roiStart.x),        // 패턴 왼쪽 위 모서리 - ROI 시작점
        static_cast<int>(pattern.rect.y() - roiStart.y),        // 패턴 왼쪽 위 모서리 - ROI 시작점
        static_cast<int>(rectWidth), 
        static_cast<int>(rectHeight)
    );
    


    // EDGE 관련 로컬 파라미터 (패턴에서 가져옴)
    bool edgeEnabled = pattern.edgeEnabled;
    int edgeOffsetX = pattern.edgeOffsetX;
    int edgeBoxWidth = pattern.edgeBoxWidth;
    int edgeBoxHeight = pattern.edgeBoxHeight;
    int edgeMaxOutliers = pattern.edgeMaxOutliers;
    // 이전 시그니처의 out-parameters (placeholder)
    int* edgeIrregularityCount = nullptr;
    double* edgeMaxDeviation = nullptr;
    cv::Point* edgeBoxTopLeft = nullptr;
    int* edgeAverageX = nullptr;
        
        // ===== 1단계: 필터에서 완벽하게 전처리된 이진화 영상 사용 =====
        // 필터에서 이미 이진화 + 형태학적 연산이 완료되었으므로 그대로 사용
        // roiImage는 InsProcessor에서 필터 적용된 상태로 들어옴
        cv::Mat processed;
        if (roiImage.channels() == 3) {
            // 3채널이면 그레이스케일로 변환 (필터 적용된 이진 영상은 모든 채널이 동일)
            cv::cvtColor(roiImage, processed, cv::COLOR_BGR2GRAY);
        } else {
            // 이미 1채널이면 그대로 사용
            processed = roiImage.clone();
        }
           
        // 이미지 통계 확인
        double minVal, maxVal;
        cv::minMaxLoc(processed, &minVal, &maxVal);
        int nonZero = cv::countNonZero(processed);
        
        // ===== 필터에서 이미 완벽하게 전처리된 이미지 사용 (마스킹 불필요) =====
        // extractROI에서 이미 패턴 외부는 흰색, 내부는 필터 적용된 이진화 이미지
        cv::Mat maskedProcessed = processed.clone();
        
        // ===== 2단계: 컨투어 검출 =====
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(maskedProcessed, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
             
        if (contours.empty()) {
            score = 0.0;
            cleanOriginal.copyTo(resultImage);
            return false;
        }
        
        // 가장 큰 윤곽선 선택
        auto largestContour = *std::max_element(contours.begin(), contours.end(),
            [](const std::vector<cv::Point>& a, const std::vector<cv::Point>& b) {
                return cv::contourArea(a) < cv::contourArea(b);
            });
        
        cv::Rect boundRect = cv::boundingRect(largestContour);

        
        // boundRect 유효성 검사
        if (boundRect.width <= 0 || boundRect.height <= 0 || 
            boundRect.x < 0 || boundRect.y < 0 ||
            boundRect.x >= roiImage.cols || boundRect.y >= roiImage.rows) {

            score = 0.0;
            cleanOriginal.copyTo(resultImage);
            return false;
        }
                  
        // ===== 3단계: 두께 분석 (마스킹된 이미지 기반) =====
        cv::Mat blackRegions;
        cv::threshold(maskedProcessed, blackRegions, 1, 255, cv::THRESH_BINARY_INV);
        
        // 노이즈 제거
        cv::Mat kernel2 = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(morphKernelSize, morphKernelSize));
        cv::morphologyEx(blackRegions, blackRegions, cv::MORPH_OPEN, kernel2);
        cv::morphologyEx(blackRegions, blackRegions, cv::MORPH_CLOSE, kernel2);
        
        // X축 방향으로 스캔 - 상단 컨투어와 하단 컨투어 각각 탐색 (원래 방식 유지)
        
        // 1. 상단 컨투어 스캔 (실제 상단 경계선 탐지)
        std::vector<cv::Point> topPositions;
        std::vector<float> topThicknesses;
        
        for (int scanX = boundRect.x; scanX < boundRect.x + boundRect.width; scanX++) {
            if (scanX >= blackRegions.cols) break;
            
            int maxThickness = 0;
            int topY = -1; // 실제 상단 경계선
            
            // 위에서 아래로 스캔하여 상단 경계선과 최대 두께 측정
            int currentThickness = 0;
            for (int y = boundRect.y; y < boundRect.y + boundRect.height; y++) {
                if (y >= blackRegions.rows) break;
                
                if (blackRegions.at<uchar>(y, scanX) == 255) {
                    if (topY == -1) topY = y; // 첫 번째 검은색 픽셀 = 상단 경계선
                    currentThickness++;
                } else {
                    if (currentThickness > maxThickness) {
                        maxThickness = currentThickness;
                    }
                    currentThickness = 0;
                }
            }
            
            // 마지막 구간 체크
            if (currentThickness > maxThickness) {
                maxThickness = currentThickness;
            }
            
            if (maxThickness > 0 && topY != -1) {
                // 실제 상단 경계선 사용
                topPositions.push_back(cv::Point(scanX, topY));
                topThicknesses.push_back(static_cast<float>(maxThickness));
            }
        }
        
        
        // 1-2. 상단 컨투어 역방향 스캔 (오른쪽→왼쪽)
        std::vector<cv::Point> topPositionsReverse;
        std::vector<float> topThicknessesReverse;
        
        for (int scanX = boundRect.x + boundRect.width - 1; scanX >= boundRect.x; scanX--) {
            if (scanX >= blackRegions.cols) continue;
            
            int maxThickness = 0;
            int topY = -1; // 실제 상단 경계선
            
            // 위에서 아래로 스캔하여 상단 경계선과 최대 두께 측정
            int currentThickness = 0;
            for (int y = boundRect.y; y < boundRect.y + boundRect.height; y++) {
                if (y >= blackRegions.rows) break;
                
                if (blackRegions.at<uchar>(y, scanX) == 255) {
                    if (topY == -1) topY = y; // 첫 번째 검은색 픽셀 = 상단 경계선
                    currentThickness++;
                } else {
                    if (currentThickness > maxThickness) {
                        maxThickness = currentThickness;
                    }
                    currentThickness = 0;
                }
            }
            
            // 마지막 구간 체크
            if (currentThickness > maxThickness) {
                maxThickness = currentThickness;
            }
            
            if (maxThickness > 0 && topY != -1) {
                // 실제 상단 경계선 사용 (역방향이므로 앞쪽에 삽입)
                topPositionsReverse.insert(topPositionsReverse.begin(), cv::Point(scanX, topY));
                topThicknessesReverse.insert(topThicknessesReverse.begin(), static_cast<float>(maxThickness));
            }
        }

        // 2. 하단 컨투어 스캔 (실제 하단 경계선 추적)
        std::vector<cv::Point> bottomPositions;
        std::vector<float> bottomThicknesses;
        
        for (int scanX = boundRect.x; scanX < boundRect.x + boundRect.width; scanX++) {
            if (scanX >= blackRegions.cols) break;
            
            int bottomY = -1;
            int totalThickness = 0;
            
            // 아래에서 위로 스캔하여 실제 하단 경계선 찾기
            for (int y = boundRect.y + boundRect.height - 1; y >= boundRect.y; y--) {
                if (y >= blackRegions.rows) continue;
                
                if (blackRegions.at<uchar>(y, scanX) == 255) {
                    if (bottomY == -1) {
                        bottomY = y; // 첫 번째 검은색 픽셀 = 하단 경계선
                    }
                    totalThickness++;
                }
            }
            
            if (bottomY != -1 && totalThickness > 0) {
                bottomPositions.push_back(cv::Point(scanX, bottomY));
                bottomThicknesses.push_back(static_cast<float>(totalThickness));
            }
        }
        
        // 2-2. 하단 컨투어 역방향 스캔 (오른쪽→왼쪽)
        std::vector<cv::Point> bottomPositionsReverse;
        std::vector<float> bottomThicknessesReverse;
        
        for (int scanX = boundRect.x + boundRect.width - 1; scanX >= boundRect.x; scanX--) {
            if (scanX >= blackRegions.cols) continue;
            
            int totalThickness = 0;
            int bottomY = -1; // 실제 하단 경계선
            
            // 아래에서 위로 스캔하여 하단 경계선 찾기
            for (int y = boundRect.y + boundRect.height - 1; y >= boundRect.y; y--) {
                if (y >= blackRegions.rows) continue;
                
                if (blackRegions.at<uchar>(y, scanX) == 255) {
                    if (bottomY == -1) {
                        bottomY = y; // 첫 번째 검은색 픽셀 = 하단 경계선
                    }
                    totalThickness++;
                }
            }
            
            if (bottomY != -1 && totalThickness > 0) {
                // 실제 하단 경계선 사용 (역방향이므로 앞쪽에 삽입)
                bottomPositionsReverse.insert(bottomPositionsReverse.begin(), cv::Point(scanX, bottomY));
                bottomThicknessesReverse.insert(bottomThicknessesReverse.begin(), static_cast<float>(totalThickness));
            }
        }
        
        // 3. 상단과 하단 각각의 의미있는 gradient 계산 (원본 패턴 크기 기준)
        auto calculateMeaningfulGradients = [gradientThreshold, gradientStartPercent, gradientEndPercent, minDataPoints, &roiPatternRect]
            (const std::vector<float>& thicknesses, const std::vector<cv::Point>& positions) -> std::vector<float> {
            std::vector<float> gradients(thicknesses.size(), 0.0f);
            
            if (thicknesses.size() < static_cast<size_t>(minDataPoints)) return gradients; // 파라미터로 받은 최소 개수
            
            // 원본 패턴 크기 기준으로 start/end X 좌표 계산
            int patternStartX = roiPatternRect.x;
            int patternWidth = roiPatternRect.width;
            int gradientStartX = patternStartX + (patternWidth * gradientStartPercent / 100);
            int gradientEndX = patternStartX + (patternWidth * gradientEndPercent / 100);
            
            // 해당 X 좌표 범위에 있는 포인트들만 gradient 계산
            for (size_t i = 1; i < thicknesses.size() - 1; i++) {
                if (i < positions.size()) {
                    int currentX = positions[i].x;
                    
                    // 원본 패턴 기준 gradient 분석 구간 내에 있는지 확인
                    if (currentX >= gradientStartX && currentX <= gradientEndX) {
                        // 중앙 차분으로 gradient 계산
                        float gradient = (thicknesses[i+1] - thicknesses[i-1]) / 2.0f;
                        
                        // 파라미터로 받은 임계값 적용
                        if (std::abs(gradient) >= gradientThreshold) {
                            gradients[i] = gradient;
                        }
                    }
                }
            }
            return gradients;
        };
        
        std::vector<float> topGradients = calculateMeaningfulGradients(topThicknesses, topPositions);
        std::vector<float> bottomGradients = calculateMeaningfulGradients(bottomThicknesses, bottomPositions);
        
        // 역방향 스캔 결과의 gradient 계산
        std::vector<float> topGradientsReverse = calculateMeaningfulGradients(topThicknessesReverse, topPositionsReverse);
        std::vector<float> bottomGradientsReverse = calculateMeaningfulGradients(bottomThicknessesReverse, bottomPositionsReverse);
        
        // 4. 정방향과 역방향 중 더 강한 gradient를 가진 방향 선택
        // 4. 정방향과 역방향 중 더 강한 gradient를 가진 방향 선택
        float topMaxGrad = 0.0f;
        float bottomMaxGrad = 0.0f;
        float topMaxGradReverse = 0.0f;
        float bottomMaxGradReverse = 0.0f;
        
        // 정방향 최대 gradient 계산
        if (!topGradients.empty()) {
            for (float grad : topGradients) {
                topMaxGrad = std::max(topMaxGrad, std::abs(grad));
            }
        }
        
        if (!bottomGradients.empty()) {
            for (float grad : bottomGradients) {
                bottomMaxGrad = std::max(bottomMaxGrad, std::abs(grad));
            }
        }
        
        // 역방향 최대 gradient 계산
        if (!topGradientsReverse.empty()) {
            for (float grad : topGradientsReverse) {
                topMaxGradReverse = std::max(topMaxGradReverse, std::abs(grad));
            }
        }
        
        if (!bottomGradientsReverse.empty()) {
            for (float grad : bottomGradientsReverse) {
                bottomMaxGradReverse = std::max(bottomMaxGradReverse, std::abs(grad));
            }
        }
        
        // 최적 방향과 컨투어 선택
        bool useTopContour = false;
        bool useReverse = false;
        std::vector<cv::Point> selectedPositions;
        std::vector<float> selectedGradients;
        
        // 상단/하단 중 더 강한 gradient를 가진 컨투어 선택
        if (std::max(topMaxGrad, topMaxGradReverse) >= std::max(bottomMaxGrad, bottomMaxGradReverse)) {
            useTopContour = true;
            // 상단에서 정방향/역방향 중 더 강한 것 선택
            if (topMaxGradReverse > topMaxGrad) {
                useReverse = true;
                selectedPositions = topPositionsReverse;
                selectedGradients = topGradientsReverse;
            } else {
                selectedPositions = topPositions;
                selectedGradients = topGradients;
            }
        } else {
            useTopContour = false;
            // 하단에서 정방향/역방향 중 더 강한 것 선택
            if (bottomMaxGradReverse > bottomMaxGrad) {
                useReverse = true;
                selectedPositions = bottomPositionsReverse;
                selectedGradients = bottomGradientsReverse;
            } else {
                selectedPositions = bottomPositions;
                selectedGradients = bottomGradients;
            }
        }
        
        // 5. 선택된 컨투어에서 최대 gradient 위치와 시작점 찾기
        cv::Point maxGradientPoint, startPoint;
        
        // 선택된 컨투어의 max gradient 위치 찾기
        if (!selectedGradients.empty() && !selectedPositions.empty()) {
            auto maxIt = std::max_element(selectedGradients.begin(), selectedGradients.end(),
                [](float a, float b) { return std::abs(a) < std::abs(b); });
            size_t maxIdx = std::distance(selectedGradients.begin(), maxIt);
            maxGradientPoint = selectedPositions[maxIdx];
            startPoint = selectedPositions[0]; // 첫 번째 점이 시작점
        }
        
        // 5.5. 검은색 부분의 가로 두께 측정 (목 부분 절단 품질) - 각도 고려
        // 20% X 지점에서 패턴 각도에 수직인 방향으로 검은색 픽셀 개수 체크
        std::vector<int> neckWidths; // 각 측정 위치에서의 두께
        std::vector<cv::Point> neckMeasurePoints; // 측정 시작 지점들
        std::vector<std::pair<cv::Point, cv::Point>> neckLines; // 측정 라인들 (시작점, 끝점)
        double avgNeckWidth = 0.0, minNeckWidth = 0.0, maxNeckWidth = 0.0, neckWidthStdDev = 0.0;
        int neckMeasureXPos = 0;
        
        // 20% X 지점 계산 - 원본 패턴 박스 기준
        int measureX = roiPatternRect.x + (roiPatternRect.width * gradientStartPercent / 100);
        
        // measureX 경계 체크 및 보정
        if (measureX < 0) measureX = 0;
        if (measureX >= blackRegions.cols) measureX = blackRegions.cols - 1;
        
        neckMeasureXPos = measureX;
        
        // 각도를 라디안으로 변환
        double angleRad = angle * CV_PI / 180.0;
        
        // 패턴의 가로/세로 방향 벡터 계산 (각도 고려)
        cv::Point2f horizontalVec(cos(angleRad), sin(angleRad));      // 패턴의 가로 방향
        cv::Point2f verticalVec(-sin(angleRad), cos(angleRad));       // 패턴의 세로 방향 (가로에 수직)
        

        
        // 20% X 지점부터 width만큼 X를 이동하면서 각 X 위치에서 Y축 방향 검은색 픽셀 개수 측정
        for (int x = measureX; x < roiPatternRect.x + roiPatternRect.width; x++) {
            // 현재 X 위치에서 Y축 방향으로 검은색 픽셀 개수 세기
            int blackPixelCount = 0;
            cv::Point2f startPos(x, roiPatternRect.y);
            cv::Point2f actualStartPos = startPos;
            
            // 위에서 아래로 Y축 스캔하면서 연속된 검은색 픽셀 개수 카운트
            for (int y = roiPatternRect.y; y < roiPatternRect.y + roiPatternRect.height; y++) {
                
                // 경계 체크
                if (x < 0 || x >= blackRegions.cols || y < 0 || y >= blackRegions.rows) break;
                
                // 검은색 픽셀 체크
                if (blackRegions.at<uchar>(y, x) == 255) {
                    if (blackPixelCount == 0) {
                        // 첫 번째 검은색 픽셀을 만났을 때 실제 시작점 업데이트
                        actualStartPos = cv::Point2f(x, y);
                    }
                    blackPixelCount++;
                } else if (blackPixelCount > 0) {
                    break; // 검은색 구간이 끝나면 중단
                }
            }
            
            // 검은색 픽셀이 있는 경우만 저장
            if (blackPixelCount > 0) {
                neckWidths.push_back(blackPixelCount);
                neckMeasurePoints.push_back(cv::Point(x, (int)actualStartPos.y));
                
                // 측정 라인 표시용 (Y축 방향 검은색 구간)
                cv::Point2f endPos(actualStartPos.x, actualStartPos.y + blackPixelCount);
                neckLines.push_back(std::make_pair(cv::Point((int)actualStartPos.x, (int)actualStartPos.y),
                                                  cv::Point((int)endPos.x, (int)endPos.y)));
                
                // 측정된 세로선을 결과 이미지에 붉은색으로 표시 (실제 검은색 구간)
                cv::line(resultImage, 
                        cv::Point((int)actualStartPos.x, (int)actualStartPos.y),
                        cv::Point((int)endPos.x, (int)endPos.y),
                        cv::Scalar(0, 0, 255), 1); // 붉은색 (BGR)
            }
        }
        
        // 통계 계산
        if (!neckWidths.empty()) {
            // 평균 계산
            int sum = 0;
            for (int w : neckWidths) sum += w;
            avgNeckWidth = static_cast<double>(sum) / neckWidths.size();
            
            // 최대, 최소 계산
            minNeckWidth = *std::min_element(neckWidths.begin(), neckWidths.end());
            maxNeckWidth = *std::max_element(neckWidths.begin(), neckWidths.end());
            
            // 표준편차 계산
            double variance = 0.0;
            for (int w : neckWidths) {
                variance += (w - avgNeckWidth) * (w - avgNeckWidth);
            }
            neckWidthStdDev = sqrt(variance / neckWidths.size());
            

            
            // 목 폭 통계는 내부 로컬 변수에 계산됨. 외부 포인터 반환은 더 이상 사용하지 않음.
            
            // 측정 결과를 이미지 위에 텍스트로 표시 (회전 각도 고려)
            if (!neckWidths.empty()) {

                
                // 패턴 중심점 계산
                cv::Point2f center(boundRect.x + boundRect.width / 2.0f, 
                                  boundRect.y + boundRect.height / 2.0f);
                
                // 패턴 위쪽 방향 벡터 계산 (각도 고려)
                cv::Point2f upVector(-sin(angleRad), -cos(angleRad)); // 위쪽 방향
                
                // 텍스트 박스를 패턴 위쪽에 배치 (패턴 크기의 절반만큼 위로)
                float textDistance = std::max(boundRect.width, boundRect.height) * 0.6f; // 패턴에서 충분히 떨어뜨리기
                cv::Point2f textCenter = center + upVector * textDistance;
                
                // 텍스트 박스 위치 (중심 기준으로 박스 생성)
                int boxWidth = 200;
                int boxHeight = 70;
                cv::Point textPos((int)(textCenter.x - boxWidth/2), (int)(textCenter.y - boxHeight/2));
                
                // 화면 경계 체크 및 보정
                if (textPos.x < 0) textPos.x = 10;
                if (textPos.y < 0) textPos.y = 10;
                if (textPos.x + boxWidth >= resultImage.cols) textPos.x = resultImage.cols - boxWidth - 10;
                if (textPos.y + boxHeight >= resultImage.rows) textPos.y = resultImage.rows - boxHeight - 10;
                

                
                // 배경 박스 그리기 (더 눈에 띄는 색상으로)
                cv::Rect textBgRect(textPos.x, textPos.y, boxWidth, boxHeight);
                cv::rectangle(resultImage, textBgRect, cv::Scalar(0, 0, 0), -1); // 검은색 배경
                cv::rectangle(resultImage, textBgRect, cv::Scalar(0, 255, 0), 2); // 초록색 테두리 (더 두껍게)
                
                // 텍스트 정보 준비
                std::string minMaxText = "Min: " + std::to_string((int)minNeckWidth) + "px";
                std::string maxText = "Max: " + std::to_string((int)maxNeckWidth) + "px";
                std::string avgText = "Avg: " + std::to_string((int)avgNeckWidth) + "px";
                

                
                // 폰트 설정 (크기 증가)
                int fontFace = cv::FONT_HERSHEY_SIMPLEX;
                double fontScale = 0.7; // 크기 증가
                int thickness = 2;      // 두께 증가
                cv::Scalar textColor(0, 255, 0); // 초록색 텍스트 (더 눈에 띄게)
                
                // 텍스트 그리기 (박스 내부에 정렬)
                cv::putText(resultImage, minMaxText, cv::Point(textBgRect.x + 10, textBgRect.y + 20), 
                           fontFace, fontScale, textColor, thickness);
                cv::putText(resultImage, maxText, cv::Point(textBgRect.x + 10, textBgRect.y + 40), 
                           fontFace, fontScale, textColor, thickness);
                cv::putText(resultImage, avgText, cv::Point(textBgRect.x + 10, textBgRect.y + 60), 
                           fontFace, fontScale, textColor, thickness);
                           

            } else {

            }
            
            // 두께 측정 영역을 점선 사각형으로 표시 (패턴 각도 적용)
            if (!neckMeasurePoints.empty()) {
                // 측정 영역의 경계 계산
                cv::Point2f minPt(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
                cv::Point2f maxPt(std::numeric_limits<float>::min(), std::numeric_limits<float>::min());
                
                for (const auto& line : neckLines) {
                    minPt.x = std::min({minPt.x, (float)line.first.x, (float)line.second.x});
                    minPt.y = std::min({minPt.y, (float)line.first.y, (float)line.second.y});
                    maxPt.x = std::max({maxPt.x, (float)line.first.x, (float)line.second.x});
                    maxPt.y = std::max({maxPt.y, (float)line.first.y, (float)line.second.y});
                }
                
                // 여백 추가
                float margin = 10.0f;
                minPt.x -= margin; minPt.y -= margin;
                maxPt.x += margin; maxPt.y += margin;
                
                // 패턴 각도에 맞춘 회전된 사각형의 꼭짓점 계산
                cv::Point2f center((minPt.x + maxPt.x) / 2.0f, (minPt.y + maxPt.y) / 2.0f);
                float width = maxPt.x - minPt.x;
                float height = maxPt.y - minPt.y;
                
                // 회전 변환 매트릭스
                cv::Mat rotMat = cv::getRotationMatrix2D(center, angle, 1.0);
                
                // 사각형의 네 꼭짓점 (회전 전)
                std::vector<cv::Point2f> rectPoints = {
                    cv::Point2f(center.x - width/2, center.y - height/2),  // 좌상
                    cv::Point2f(center.x + width/2, center.y - height/2),  // 우상
                    cv::Point2f(center.x + width/2, center.y + height/2),  // 우하
                    cv::Point2f(center.x - width/2, center.y + height/2)   // 좌하
                };
                
                // 회전 적용
                std::vector<cv::Point> rotatedPoints;
                for (const auto& pt : rectPoints) {
                    std::vector<cv::Point2f> src = {pt};
                    std::vector<cv::Point2f> dst;
                    cv::transform(src, dst, rotMat);
                    rotatedPoints.push_back(cv::Point((int)dst[0].x, (int)dst[0].y));
                }
                
                // 점선 스타일로 회전된 사각형 그리기
                cv::Scalar boxColor(255, 255, 0); // 노란색
                int lineType = cv::LINE_8;
                int thickness = 2;
                
                for (int i = 0; i < 4; i++) {
                    cv::Point start = rotatedPoints[i];
                    cv::Point end = rotatedPoints[(i + 1) % 4];
                    
                    // 점선 효과 (선분을 짧게 나누어 그리기)
                    cv::Point diff = end - start;
                    float lineLength = sqrt(diff.x * diff.x + diff.y * diff.y);
                    int numDashes = (int)(lineLength / 10); // 10픽셀마다 점선
                    
                    for (int j = 0; j < numDashes; j += 2) { // 짝수 번째만 그리기 (점선 효과)
                        cv::Point dashStart = start + diff * j / numDashes;
                        cv::Point dashEnd = start + diff * std::min(j + 1, numDashes) / numDashes;
                        cv::line(resultImage, dashStart, dashEnd, boxColor, thickness, lineType);
                    }
                }
                
            }
            
            // 측정 지점들을 결과 이미지에 표시
            for (size_t i = 0; i < neckMeasurePoints.size(); i++) {
                const cv::Point& pt = neckMeasurePoints[i];
                int pixelCount = neckWidths[i];
                
                if (pt.x >= 0 && pt.x < resultImage.cols && pt.y >= 0 && pt.y < resultImage.rows) {
                    // 측정 시작점을 밝은 녹색 원으로 표시
                    cv::circle(resultImage, pt, 2, cv::Scalar(0, 255, 0), -1); // 녹색 원
                    
                    // 측정 끝점을 밝은 파란색 원으로 표시
                    cv::Point endPt(pt.x + pixelCount, pt.y);
                    if (endPt.x >= 0 && endPt.x < resultImage.cols) {
                        cv::circle(resultImage, endPt, 2, cv::Scalar(255, 0, 0), -1); // 파란색 원
                    }
                }
            }
        }
        
        // 6. 시각화를 위한 데이터 준비
        std::vector<cv::Point> positions = selectedPositions; // 선택된 최적 컨투어 사용
        std::vector<float> thicknesses, gradients;
        
        // 선택된 방향과 컨투어에 따라 두께와 gradient 설정
        if (useTopContour) {
            thicknesses = useReverse ? topThicknessesReverse : topThicknesses;
            gradients = useReverse ? topGradientsReverse : topGradients;
        } else {
            thicknesses = useReverse ? bottomThicknessesReverse : bottomThicknesses;
            gradients = useReverse ? bottomGradientsReverse : bottomGradients;
        }
        
        // STRIP 검사의 급격한 두께 변화 지점 4개 찾기 (전체 구간에서 더 민감하게)
        gradientPoints.clear();
        
        cv::Point point1, point2, point3, point4;
        bool hasPoint1 = false, hasPoint2 = false, hasPoint3 = false, hasPoint4 = false;
        
        // 더 민감한 임계값 사용 (기본값의 50%)
        float sensitiveThreshold = gradientThreshold * 0.5f;
        

        
        // 상단 컨투어에서 급격한 변화 지점 찾기 (10%-90% 구간으로 확장)
        if (!topPositions.empty() && !topGradients.empty()) {
            size_t startIdx = topPositions.size() * 10 / 100; // 10%
            size_t endIdx = topPositions.size() * 90 / 100;   // 90%
            

            
            // 첫 번째 급격한 변화 지점 (앞쪽에서)
            for (size_t i = startIdx; i < endIdx; i++) {
                if (std::abs(topGradients[i]) >= sensitiveThreshold) {
                    point1 = topPositions[i];
                    hasPoint1 = true;
                    gradientPoints.push_back(point1);
                    break;
                }
            }
            
            // 두 번째 급격한 변화 지점 (뒤쪽에서)
            for (size_t i = endIdx; i > startIdx; i--) {
                if (std::abs(topGradients[i-1]) >= sensitiveThreshold) {
                    point3 = topPositions[i-1];
                    hasPoint3 = true;
                    gradientPoints.push_back(point3);
                    break;
                }
            }
        }
        
        // 하단 컨투어에서 급격한 변화 지점 찾기 (10%-90% 구간으로 확장)
        if (!bottomPositions.empty() && !bottomGradients.empty()) {
            size_t startIdx = bottomPositions.size() * 10 / 100; // 10%
            size_t endIdx = bottomPositions.size() * 90 / 100;   // 90%
            
            
            // 세 번째 급격한 변화 지점 (앞쪽에서)
            for (size_t i = startIdx; i < endIdx; i++) {
                if (std::abs(bottomGradients[i]) >= sensitiveThreshold) {
                    point2 = bottomPositions[i];
                    hasPoint2 = true;
                    gradientPoints.push_back(point2);
                    break;
                }
            }
            
            // 네 번째 급격한 변화 지점 (뒤쪽에서)
            for (size_t i = endIdx; i > startIdx; i--) {
                if (std::abs(bottomGradients[i-1]) >= sensitiveThreshold) {
                    point4 = bottomPositions[i-1];
                    hasPoint4 = true;
                    gradientPoints.push_back(point4);
                    break;
                }
            }
        }
        
        if (hasPoint1) // removed debug log
        if (hasPoint2) // removed debug log
        if (hasPoint3) // removed debug log
        if (hasPoint4) // removed debug log
        
        // removed debug logs
        
        if (!selectedPositions.empty()) {
            // removed debug log
        }
        
        
        if (positions.empty()) {
            score = 0.0;
            cleanOriginal.copyTo(resultImage);
            return false;
        }
        
        // 7. 절댓값으로 변화율의 크기 계산
        std::vector<float> absGradients;
        for (float grad : gradients) {
            absGradients.push_back(std::abs(grad));
        }
        
        // 디버그: gradient 값들 출력
        if (absGradients.size() > 0) {
            float maxGrad = *std::max_element(absGradients.begin(), absGradients.end());
            float avgGrad = std::accumulate(absGradients.begin(), absGradients.end(), 0.0f) / absGradients.size();
        }
        
        // 4. Peak Detection Algorithm (Python 코드와 동일한 local maxima 탐지)
        std::vector<cv::Point> peakPositions;
        std::vector<float> peakValues;
        int windowSize = 15; // Python 코드와 동일
        float threshold = 1.0f; // Python 코드와 동일
        
        // Local Maxima Detection (Python의 sliding window 방식)
        for (size_t i = windowSize; i < absGradients.size() - windowSize; i++) {
            float currentValue = absGradients[i];
            
            if (currentValue < threshold) continue;
            
            // Sliding Window에서 최대값인지 확인 (Python과 동일)
            bool isLocalMax = true;
            for (size_t j = i - windowSize; j <= i + windowSize; j++) {
                if (j != i && absGradients[j] > currentValue) {
                    isLocalMax = false;
                    break;
                }
            }
            
            if (isLocalMax) {
                // 중복 제거: 너무 가까운 지점들은 제외 (Python 코드 방식)
                bool isDuplicate = false;
                for (const cv::Point& existing : peakPositions) {
                    if (std::abs(positions[i].x - existing.x) < windowSize) {
                        isDuplicate = true;
                        break;
                    }
                }
                
                if (!isDuplicate && i < positions.size()) {
                    peakPositions.push_back(positions[i]);
                    peakValues.push_back(currentValue);
                }
            }
        }
        
        // Non-Maximum Suppression (중복 제거 + 모든 gradient 지점 저장)
        std::vector<std::pair<cv::Point, float>> peaks;
        for (size_t i = 0; i < peakPositions.size(); i++) {
            peaks.push_back({peakPositions[i], peakValues[i]});
        }
        
        // 변화율 크기 순으로 정렬
        std::sort(peaks.begin(), peaks.end(), 
            [](const std::pair<cv::Point, float>& a, const std::pair<cv::Point, float>& b) {
                return a.second > b.second;
            });
        
        // 거리 기반 중복 제거 후 모든 gradient 지점 저장
        int minDistance = 30; // 최소 거리
        
        for (const auto& peak : peaks) {
            bool tooClose = false;
            for (const cv::Point& existing : gradientPoints) {
                if (std::abs(peak.first.x - existing.x) < minDistance) {
                    tooClose = true;
                    break;
                }
            }
            
            if (!tooClose) {
                gradientPoints.push_back(peak.first);
            }
        }
        
        // 3개 지점 설정
        // 시작점: 실제 검은색 물체가 시작되는 첫 번째 지점 (두께가 일정값 이상인 첫 지점)
        float minThicknessForStart = 20.0f; // 최소 두께 기준을 높임 (5.0f -> 20.0f)
        startPoint = positions[0]; // 기본값
        
        for (size_t i = 0; i < thicknesses.size(); i++) {
            if (thicknesses[i] >= minThicknessForStart) {
                startPoint = positions[i];
                break;
            }
        }
        
        // Python의 find_max_thickness_gradient_position과 동일한 방식
        // 전체 gradient에서 절댓값이 가장 큰 지점 찾기 (Global Max Gradient)
        // 더 정확한 max gradient 탐지를 위해 개선된 로직
        size_t maxGradientIdx = 0;
        float maxGradientValue = 0.0f;
        
        if (!absGradients.empty()) {
            // 1단계: 전체에서 상위 20% 이상의 gradient 값들 찾기
            std::vector<float> sortedGradients = absGradients;
            std::sort(sortedGradients.begin(), sortedGradients.end(), std::greater<float>());
            float threshold80th = sortedGradients[std::min(static_cast<size_t>(sortedGradients.size() * 0.2), sortedGradients.size() - 1)];
            
            // 2단계: 상위 gradient 중에서 오른쪽 절반에서 가장 큰 값 찾기 (STRIP 특성상 끝부분에 gradient가 클 가능성)
            size_t startIdx = absGradients.size() / 2; // 오른쪽 절반부터 시작
            
            for (size_t i = startIdx; i < absGradients.size(); i++) {
                if (absGradients[i] >= threshold80th && absGradients[i] > maxGradientValue) {
                    maxGradientValue = absGradients[i];
                    maxGradientIdx = i;
                }
            }
            
            // 3단계: 만약 오른쪽 절반에서 찾지 못했으면 전체에서 최대값 사용
            if (maxGradientValue == 0.0f) {
                auto maxGradientIt = std::max_element(absGradients.begin(), absGradients.end());
                maxGradientIdx = std::distance(absGradients.begin(), maxGradientIt);
                maxGradientValue = *maxGradientIt;
            }
            
            if (maxGradientIdx < positions.size()) {
                maxGradientPoint = positions[maxGradientIdx];  // Global Max Gradient 지점
            } else {
                maxGradientPoint = positions.back(); // fallback
                maxGradientIdx = positions.size() - 1;
            }
            
            // removed debug log
        } else {
            maxGradientPoint = positions.back(); // fallback: 가장 오른쪽
            maxGradientIdx = positions.size() - 1;
        }
        
        // Max gradient 위치에서 좌우 두께 측정
        int leftThick = 0, rightThick = 0;
        if (maxGradientIdx < positions.size()) {
            // 패턴 각도에 따른 수직 방향 계산
            double angleRad = angle * CV_PI / 180.0;
            double perpX = -sin(angleRad); // 수직 방향 X 성분
            double perpY = cos(angleRad);  // 수직 방향 Y 성분
            
            // 두께 측정은 필터 적용된 processed 이미지 사용
            cv::Mat grayForThickness = processed.clone();
            
            const int maxSearchDistance = 100; // 최대 탐색 거리
            const int thresholdDiff = 30; // 밝기 차이 임계값
            
            int centerIntensity = grayForThickness.at<uchar>(maxGradientPoint.y, maxGradientPoint.x);
            
            // 좌측으로 탐색
            for (int i = 1; i <= maxSearchDistance; i++) {
                int searchX = maxGradientPoint.x + static_cast<int>(perpX * (-i));
                int searchY = maxGradientPoint.y + static_cast<int>(perpY * (-i));
                
                if (searchX < 0 || searchX >= grayForThickness.cols || searchY < 0 || searchY >= grayForThickness.rows) {
                    break;
                }
                
                int intensity = grayForThickness.at<uchar>(searchY, searchX);
                if (abs(intensity - centerIntensity) > thresholdDiff) {
                    leftThick = i;
                    break;
                }
            }
            
            // 우측으로 탐색
            for (int i = 1; i <= maxSearchDistance; i++) {
                int searchX = maxGradientPoint.x + static_cast<int>(perpX * i);
                int searchY = maxGradientPoint.y + static_cast<int>(perpY * i);
                
                if (searchX < 0 || searchX >= grayForThickness.cols || searchY < 0 || searchY >= grayForThickness.rows) {
                    break;
                }
                
                int intensity = grayForThickness.at<uchar>(searchY, searchX);
                if (abs(intensity - centerIntensity) > thresholdDiff) {
                    rightThick = i;
                    break;
                }
            }
        }
        
        // 두께 값들을 포인터로 반환
            // 외부 포인터로의 반환(leftThickness/rightThickness)은 더 이상 사용하지 않습니다.
        
        // gradient 지점이 없으면 기본값 추가
        if (gradientPoints.empty() && positions.size() > 2) {
            gradientPoints.push_back(positions[positions.size() / 2]); // 중간점
        }
        
        // 회전 전의 원래 위치 저장 (두께 검사용)
        cv::Point originalStartPoint = startPoint;
        cv::Point originalMaxGradientPoint = maxGradientPoint;
        std::vector<cv::Point> originalGradientPoints = gradientPoints;
        
        // NOTE: roiImage가 회전되지 않았으므로 추가 회전 불필요
        // 스캔 라인에서 각도가 이미 적용되므로, 여기서 포인트 회전 제거
        
        // 디버그: 두께 측정 결과
        
        // 원본 이미지 복사 (컨투어 선 제거, 연결선과 원만 표시) - 보정된 이미지 사용
        cleanOriginal.copyTo(resultImage);
        
        // OpenCV 4점 시각화 제거됨 - Qt에서 처리
        
        // 4개 컨투어 점 시각화 제거 (깔끔한 화면을 위해)

        
        // ROI 경계 확인 함수는 남겨둠 (다른 용도로 사용 가능)
        auto isInROI = [&resultImage](const cv::Point& p) -> bool {
            return p.x >= 0 && p.y >= 0 && p.x < resultImage.cols && p.y < resultImage.rows;
        };
        
        // 컨투어 점 찍기 완전 제거
        
        // 통계만 출력 (실제 점은 안 찍음)
        int totalTopPoints = hasPoint1 && !topPositions.empty() ? topPositions.size() : 0;
        int totalBottomPoints = hasPoint2 && !bottomPositions.empty() ? bottomPositions.size() : 0;
        

        
        // 점수 계산 (Peak 개수와 품질 기반)
        float peakQuality = 0;
        for (const auto& peak : peaks) {
            peakQuality += peak.second;
        }
        
        // 정규화된 점수 계산
        score = std::min(1.0, (gradientPoints.size() * 0.3 + peakQuality / 100.0));
        
        // 최소 1개 이상의 변화율 피크가 있어야 통과
        bool hasMinimumFeatures = gradientPoints.size() >= 1;
        bool isPassed = hasMinimumFeatures && (score >= passThreshold);
        

        
        // STRIP 두께 측정을 위한 공통 변수 정의
        // roiPatternCenter는 roiPatternRect의 중심이어야 함 (두 좌표계 통일)
        cv::Point roiPatternCenter(
            roiPatternRect.x + roiPatternRect.width / 2,
            roiPatternRect.y + roiPatternRect.height / 2
        );
        float patternWidth = static_cast<float>(roiPatternRect.width);
        // angleRad는 이미 위에서 정의됨 (1073줄)
        
        // STRIP 두께 측정 - FRONT 지점
        if (!topPositions.empty() && !bottomPositions.empty() && stripFrontEnabled) {
            float startPercent = gradientStartPercent / 100.0f;
            
            // gradient 시작점까지의 거리 (패턴 왼쪽 끝에서 startPercent만큼)
            float gradientStartX = roiPatternCenter.x - (patternWidth/2) + (startPercent * patternWidth);
            
            // 각도 적용한 실제 gradient 시작점 중앙 계산 (패턴 중심 기준으로 회전)
            float localX = gradientStartX - roiPatternCenter.x;  // 패턴 중심 기준 상대 좌표
            float localY = 0;  // Y는 boundRect 중심선 기준
            
            int boxCenterX = roiPatternCenter.x + static_cast<int>(localX * cos(angleRad) - localY * sin(angleRad));
            int boxCenterY = roiPatternCenter.y + static_cast<int>(localX * sin(angleRad) + localY * cos(angleRad));
            
            std::vector<int> thicknesses;
            std::vector<cv::Point> measurementLines; // 측정 라인 저장
            std::vector<cv::Point> blackPixelPoints; // 검은색 픽셀 위치 저장 (시각화용 - 전체 스캔 라인)
            std::vector<cv::Point> blackRegionPoints; // 검은색이 실제로 검출된 구간만 저장 (빨간색으로 표시용)
            
            // 각도 계산
            double cosA = cos(angleRad);
            double sinA = sin(angleRad);
            
            // 박스 왼쪽 끝 계산
            int boxLeftX = boxCenterX - thicknessBoxWidth/2;
            int boxTopY = boxCenterY - thicknessBoxHeight/2;
            int boxBottomY = boxCenterY + thicknessBoxHeight/2;
            
            // 박스 영역에서 세로 방향으로 스캔하여 검은색 픽셀 두께 측정
            // 박스 테두리를 피해서 안쪽만 그림: dx=2 ~ width-2
            
            // 첫 라인과 마지막 라인의 시작점과 끝점을 미리 계산 (직선 보간용)
            int scanHeight = thicknessBoxHeight - 2;
            int firstDx = 2;
            int lastDx = thicknessBoxWidth - 2;
            
            cv::Point2d firstTop, lastTop, firstBottom, lastBottom;
            
            if (std::abs(angle) < 0.1) {
                // 각도가 거의 없는 경우
                firstTop = cv::Point2d(boxCenterX - thicknessBoxWidth/2 + firstDx, boxCenterY - thicknessBoxHeight/2 + 1);
                lastTop = cv::Point2d(boxCenterX - thicknessBoxWidth/2 + lastDx, boxCenterY - thicknessBoxHeight/2 + 1);
                firstBottom = cv::Point2d(boxCenterX - thicknessBoxWidth/2 + firstDx, boxCenterY + thicknessBoxHeight/2 - 1);
                lastBottom = cv::Point2d(boxCenterX - thicknessBoxWidth/2 + lastDx, boxCenterY + thicknessBoxHeight/2 - 1);
            } else {
                // 각도가 있는 경우
                double cosA = cos(angleRad);
                double sinA = sin(angleRad);
                
                int localX1 = -thicknessBoxWidth/2 + firstDx;
                int localX2 = -thicknessBoxWidth/2 + lastDx;
                int localTopY = -thicknessBoxHeight/2 + 1;
                int localBottomY = thicknessBoxHeight/2 - 1;
                
                firstTop.x = boxCenterX + (localX1 * cosA - localTopY * sinA);
                firstTop.y = boxCenterY + (localX1 * sinA + localTopY * cosA);
                
                lastTop.x = boxCenterX + (localX2 * cosA - localTopY * sinA);
                lastTop.y = boxCenterY + (localX2 * sinA + localTopY * cosA);
                
                firstBottom.x = boxCenterX + (localX1 * cosA - localBottomY * sinA);
                firstBottom.y = boxCenterY + (localX1 * sinA + localBottomY * cosA);
                
                lastBottom.x = boxCenterX + (localX2 * cosA - localBottomY * sinA);
                lastBottom.y = boxCenterY + (localX2 * sinA + localBottomY * cosA);
            }
            
            int totalLines = lastDx - firstDx;
            
            for (int dx = firstDx; dx < thicknessBoxWidth - 1; dx += 1) {
                cv::Point scanTop, scanBottom;
                
                if (std::abs(angle) < 0.1) {
                    // 각도가 거의 없는 경우
                    int localX = -thicknessBoxWidth/2 + dx;
                    int localTopY = -thicknessBoxHeight/2 + 1;
                    scanTop = cv::Point(boxCenterX + localX, boxCenterY + localTopY);
                    scanBottom = cv::Point(boxCenterX + localX, boxCenterY + localTopY + scanHeight);
                } else {
                    // 각도가 있는 경우 - scanTop과 scanBottom 모두 선형 보간
                    double t = (double)(dx - firstDx) / totalLines;
                    
                    scanTop.x = static_cast<int>(std::round(firstTop.x + t * (lastTop.x - firstTop.x)));
                    scanTop.y = static_cast<int>(std::round(firstTop.y + t * (lastTop.y - firstTop.y)));
                    
                    scanBottom.x = static_cast<int>(std::round(firstBottom.x + t * (lastBottom.x - firstBottom.x)));
                    scanBottom.y = static_cast<int>(std::round(firstBottom.y + t * (lastBottom.y - firstBottom.y)));
                }
                
                // 스캔 라인이 이미지 범위를 벗어나면 건너뛰기
                if (scanTop.x < 0 || scanTop.y < 0 || scanBottom.x < 0 || scanBottom.y < 0 ||
                    scanTop.x >= processed.cols || scanTop.y >= processed.rows ||
                    scanBottom.x >= processed.cols || scanBottom.y >= processed.rows) {
                    continue;
                }
                
                // 모든 스캔 라인의 시작-끝점을 저장 (검은색 유무 관계없이)
                blackPixelPoints.push_back(scanTop);
                blackPixelPoints.push_back(scanBottom);
                
                // 세로 라인을 따라 검은색 픽셀 구간 찾기 (두께 측정용)
                cv::LineIterator it(processed, scanTop, scanBottom, 8);
                std::vector<std::pair<int, int>> blackRegions; // 검은색 구간의 시작과 끝
                int regionStart = -1;
                bool inBlackRegion = false;
                int maxThicknessInLine = 0;  // 이 라인에서의 최대 두께
                
                // 라인 상의 모든 점들 저장 (시작-끝점 계산용)
                std::vector<cv::Point> linePoints;
                for (int i = 0; i < it.count; i++, ++it) {
                    linePoints.push_back(it.pos());
                }
                
                // 다시 처음부터 스캔하여 검은색 픽셀 찾기 (두께 측정)
                it = cv::LineIterator(processed, scanTop, scanBottom, 8);
                int firstBlackIdx = -1;  // 첫 번째 검은색 픽셀 인덱스
                int lastBlackIdx = -1;   // 마지막 검은색 픽셀 인덱스
                
                int blackCount = 0;  // 디버그용
                for (int i = 0; i < it.count; i++, ++it) {
                    // 이미 필터링된 이진 영상이므로 그레이스케일로 읽기
                    uchar pixelValue = processed.at<uchar>(it.pos());
                    
                    // 검은색 픽셀 판단 (이진화된 영상: 0=검은색, 255=흰색)
                    bool isBlack = (pixelValue < 127);
                    if (isBlack) blackCount++;
                    
                    if (isBlack && !inBlackRegion) {
                        // 검은색 구간 시작
                        regionStart = i;
                        inBlackRegion = true;
                        
                        // 첫 번째 검은색 구간의 시작점 기록
                        if (firstBlackIdx == -1) {
                            firstBlackIdx = i;
                        }
                    } else if (!isBlack && inBlackRegion) {
                        // 검은색 구간 끝
                        int thickness = i - regionStart;
                        if (thickness >= 3) { // 최소 3픽셀 이상만 유효한 두께로 인정
                            // 이 라인에서 가장 큰 두께만 기록
                            if (thickness > maxThicknessInLine) {
                                maxThicknessInLine = thickness;
                            }
                            blackRegions.push_back({regionStart, i});
                            
                            // 마지막 검은색 구간의 끝점 업데이트
                            lastBlackIdx = i - 1;
                        }
                        inBlackRegion = false;
                    }
                }
                
                // 라인 끝에서 검은색 구간이 계속되는 경우
                if (inBlackRegion && regionStart >= 0) {
                    int thickness = it.count - regionStart;
                    if (thickness >= 3) {
                        // 이 라인에서 가장 큰 두께만 기록
                        if (thickness > maxThicknessInLine) {
                            maxThicknessInLine = thickness;
                        }
                        blackRegions.push_back({regionStart, it.count});
                        
                        // 마지막 검은색 구간의 끝점 업데이트
                        lastBlackIdx = it.count - 1;
                    }
                }
                
                // 첫 번째 검은색 시작점부터 마지막 검은색 끝점까지 하나의 선으로 저장
                if (firstBlackIdx >= 0 && lastBlackIdx >= 0 && 
                    firstBlackIdx < linePoints.size() && lastBlackIdx < linePoints.size()) {
                    blackRegionPoints.push_back(linePoints[firstBlackIdx]);
                    blackRegionPoints.push_back(linePoints[lastBlackIdx]);
                }
                
                // 이 라인의 최대 두께를 thicknesses에 추가 (라인당 1개만)
                if (maxThicknessInLine > 0) {
                    thicknesses.push_back(maxThicknessInLine);
                }
                
                // 검은색 구간이 발견된 경우 측정 라인 저장
                if (!blackRegions.empty()) {
                    measurementLines.push_back(scanTop);
                    measurementLines.push_back(scanBottom);
                }
            }
            
            // 두께 측정 결과 처리
            if (!thicknesses.empty()) {
                // 각 라인별 두께를 frontThicknessPoints에 저장 (InsProcessor에서 통계 계산)
                // cv::Point의 y값에 두께(픽셀)을 저장
                for (size_t i = 0; i < thicknesses.size(); i++) {
                    if (frontThicknessPoints) {
                        frontThicknessPoints->push_back(cv::Point(i, thicknesses[i]));
                    }
                }
                
                // FRONT 검은색 구간 포인트들을 저장 (빨간색 표시용)
                if (frontBlackRegionPoints) {
                    *frontBlackRegionPoints = blackRegionPoints;
                }
                
            } else {
                isPassed = false;
            }
        }
        
        // ===== REAR 두께 측정 (END 지점) =====
        if (stripRearEnabled) {
        // REAR 구간에서 사용할 변수들 (패턴 중심 기준)
        cv::Point roiPatternCenter_rear = roiPatternCenter;  // 동일한 패턴 중심 사용
        double cosA_rear = cos(angleRad);
        double sinA_rear = sin(angleRad);
        
        // END 지점 기준으로 두께 측정 위치 계산
        float endPercent = gradientEndPercent / 100.0f;
        
        // gradient 끝점까지의 거리 (패턴 왼쪽 끝에서 endPercent만큼)
        float gradientEndX_rear = roiPatternCenter_rear.x - (patternWidth/2) + (endPercent * patternWidth);
        
        // 각도 적용한 실제 gradient 끝점 중앙 계산 (패턴 중심 기준으로 회전)
        float localX_rear = gradientEndX_rear - roiPatternCenter_rear.x;  // 패턴 중심 기준 상대 좌표
        float localY_rear = 0;  // Y는 패턴 중심선 기준
        
        int boxCenterX_rear = roiPatternCenter_rear.x + static_cast<int>(localX_rear * cos(angleRad) - localY_rear * sin(angleRad));
        int boxCenterY_rear = roiPatternCenter_rear.y + static_cast<int>(localX_rear * sin(angleRad) + localY_rear * cos(angleRad));
        
        std::vector<int> thicknesses_rear;
        std::vector<cv::Point> measurementLines_rear; // 측정 라인 저장
        std::vector<cv::Point> blackPixelPoints_rear; // 검은색 픽셀 위치 저장 (시각화용 - 전체 스캔 라인)
        std::vector<cv::Point> blackRegionPoints_rear; // 검은색이 실제로 검출된 구간만 저장 (빨간색으로 표시용)
        
        // 박스 왼쪽 끝 계산
        int boxLeftX_rear = boxCenterX_rear - rearThicknessBoxWidth/2;
        int boxTopY_rear = boxCenterY_rear - rearThicknessBoxHeight/2;
        int boxBottomY_rear = boxCenterY_rear + rearThicknessBoxHeight/2;
        
        // 박스 영역에서 세로 방향으로 스캔하여 검은색 픽셀 두께 측정 (REAR)
        // 박스 테두리를 피해서 안쪽만 그림: dx=2 ~ width-1
        
        // 첫 라인과 마지막 라인의 시작점과 끝점을 미리 계산 (직선 보간용)
        int scanHeight_rear = rearThicknessBoxHeight - 2;
        int firstDx_rear = 2;
        int lastDx_rear = rearThicknessBoxWidth - 1;
        
        cv::Point2d firstTop_rear, lastTop_rear, firstBottom_rear, lastBottom_rear;
        
        if (std::abs(angle) < 0.1) {
            // 각도가 거의 없는 경우
            firstTop_rear = cv::Point2d(boxCenterX_rear - rearThicknessBoxWidth/2 + firstDx_rear, boxCenterY_rear - rearThicknessBoxHeight/2 + 1);
            lastTop_rear = cv::Point2d(boxCenterX_rear - rearThicknessBoxWidth/2 + lastDx_rear, boxCenterY_rear - rearThicknessBoxHeight/2 + 1);
            firstBottom_rear = cv::Point2d(boxCenterX_rear - rearThicknessBoxWidth/2 + firstDx_rear, boxCenterY_rear + rearThicknessBoxHeight/2 - 1);
            lastBottom_rear = cv::Point2d(boxCenterX_rear - rearThicknessBoxWidth/2 + lastDx_rear, boxCenterY_rear + rearThicknessBoxHeight/2 - 1);
        } else {
            // 각도가 있는 경우
            int localX1 = -rearThicknessBoxWidth/2 + firstDx_rear;
            int localX2 = -rearThicknessBoxWidth/2 + lastDx_rear;
            int localTopY = -rearThicknessBoxHeight/2 + 1;
            int localBottomY = rearThicknessBoxHeight/2 - 1;
            
            firstTop_rear.x = boxCenterX_rear + (localX1 * cosA_rear - localTopY * sinA_rear);
            firstTop_rear.y = boxCenterY_rear + (localX1 * sinA_rear + localTopY * cosA_rear);
            
            lastTop_rear.x = boxCenterX_rear + (localX2 * cosA_rear - localTopY * sinA_rear);
            lastTop_rear.y = boxCenterY_rear + (localX2 * sinA_rear + localTopY * cosA_rear);
            
            firstBottom_rear.x = boxCenterX_rear + (localX1 * cosA_rear - localBottomY * sinA_rear);
            firstBottom_rear.y = boxCenterY_rear + (localX1 * sinA_rear + localBottomY * cosA_rear);
            
            lastBottom_rear.x = boxCenterX_rear + (localX2 * cosA_rear - localBottomY * sinA_rear);
            lastBottom_rear.y = boxCenterY_rear + (localX2 * sinA_rear + localBottomY * cosA_rear);
        }
        
        int totalLines_rear = lastDx_rear - firstDx_rear;
        
        for (int dx = firstDx_rear; dx < rearThicknessBoxWidth; dx += 1) {
            cv::Point scanTop_rear, scanBottom_rear;
            
            if (std::abs(angle) < 0.1) {
                // 각도가 거의 없는 경우
                int localX = -rearThicknessBoxWidth/2 + dx;
                int localTopY = -rearThicknessBoxHeight/2 + 1;
                scanTop_rear = cv::Point(boxCenterX_rear + localX, boxCenterY_rear + localTopY);
                scanBottom_rear = cv::Point(boxCenterX_rear + localX, boxCenterY_rear + localTopY + scanHeight_rear);
            } else {
                // 각도가 있는 경우 - scanTop과 scanBottom 모두 선형 보간
                double t = (double)(dx - firstDx_rear) / totalLines_rear;
                
                scanTop_rear.x = static_cast<int>(std::round(firstTop_rear.x + t * (lastTop_rear.x - firstTop_rear.x)));
                scanTop_rear.y = static_cast<int>(std::round(firstTop_rear.y + t * (lastTop_rear.y - firstTop_rear.y)));
                
                scanBottom_rear.x = static_cast<int>(std::round(firstBottom_rear.x + t * (lastBottom_rear.x - firstBottom_rear.x)));
                scanBottom_rear.y = static_cast<int>(std::round(firstBottom_rear.y + t * (lastBottom_rear.y - firstBottom_rear.y)));
            }
            
            // 스캔 라인이 이미지 범위를 벗어나면 건너뛰기
            if (scanTop_rear.x < 0 || scanTop_rear.y < 0 || scanBottom_rear.x < 0 || scanBottom_rear.y < 0 ||
                scanTop_rear.x >= processed.cols || scanTop_rear.y >= processed.rows ||
                scanBottom_rear.x >= processed.cols || scanBottom_rear.y >= processed.rows) {
                continue;
            }
            
            // 모든 스캔 라인의 시작-끝점을 저장 (검은색 유무 관계없이)
            blackPixelPoints_rear.push_back(scanTop_rear);
            blackPixelPoints_rear.push_back(scanBottom_rear);
            
            // 세로 라인을 따라 검은색 픽셀 구간 찾기 (두께 측정용)
            cv::LineIterator it(processed, scanTop_rear, scanBottom_rear, 8);
            std::vector<std::pair<int, int>> blackRegions_rear; // 검은색 구간의 시작과 끝
            int regionStart = -1;
            bool inBlackRegion = false;
            int maxThicknessInLine_rear = 0;  // 이 라인에서의 최대 두께
            
            // 라인 상의 모든 점들 저장 (시각화용)
            std::vector<cv::Point> linePoints_rear;
            for (int i = 0; i < it.count; i++, ++it) {
                linePoints_rear.push_back(it.pos());
            }
            
            // 다시 처음부터 스캔하여 검은색 픽셀 찾기 (두께 측정)
            it = cv::LineIterator(processed, scanTop_rear, scanBottom_rear, 8);
            for (int i = 0; i < it.count; i++, ++it) {
                // 이미 필터링된 이진 영상이므로 그레이스케일로 읽기
                uchar pixelValue = processed.at<uchar>(it.pos());
                
                // 검은색 픽셀 판단 (이진화된 영상: 0=검은색, 255=흰색)
                bool isBlack = (pixelValue < 127);
                
                if (isBlack) {
                    if (!inBlackRegion) {
                        // 검은색 구간 시작
                        regionStart = i;
                        inBlackRegion = true;
                    }
                } else {
                    if (inBlackRegion) {
                        // 검은색 구간 끝
                        int thickness = i - regionStart;
                        if (thickness >= 3) { // 최소 3픽셀 이상만 유효한 두께로 인정
                            // 이 라인에서 가장 큰 두께만 기록
                            if (thickness > maxThicknessInLine_rear) {
                                maxThicknessInLine_rear = thickness;
                            }
                            blackRegions_rear.push_back({regionStart, i});
                            
                            // 검은색 구간의 실제 시작점과 끝점 저장 (빨간색 표시용)
                            if (regionStart < linePoints_rear.size() && i - 1 < linePoints_rear.size()) {
                                blackRegionPoints_rear.push_back(linePoints_rear[regionStart]);
                                blackRegionPoints_rear.push_back(linePoints_rear[i - 1]);
                            }
                        }
                        inBlackRegion = false;
                    }
                }
            }
            
            // 라인 끝에서 검은색 구간이 계속되는 경우
            if (inBlackRegion && regionStart >= 0) {
                int thickness = it.count - regionStart;
                if (thickness >= 3) {
                    // 이 라인에서 가장 큰 두께만 기록
                    if (thickness > maxThicknessInLine_rear) {
                        maxThicknessInLine_rear = thickness;
                    }
                    blackRegions_rear.push_back({regionStart, it.count});
                    
                    // 검은색 구간의 실제 시작점과 끝점 저장 (빨간색 표시용)
                    if (regionStart < linePoints_rear.size() && it.count - 1 < linePoints_rear.size()) {
                        blackRegionPoints_rear.push_back(linePoints_rear[regionStart]);
                        blackRegionPoints_rear.push_back(linePoints_rear[it.count - 1]);
                    }
                }
            }
            
            // 이 라인의 최대 두께를 thicknesses_rear에 추가 (라인당 1개만)
            if (maxThicknessInLine_rear > 0) {
                thicknesses_rear.push_back(maxThicknessInLine_rear);
            }
            
            // 검은색 구간이 발견된 경우 측정 라인 저장
            if (!blackRegions_rear.empty()) {
                measurementLines_rear.push_back(scanTop_rear);
                measurementLines_rear.push_back(scanBottom_rear);
            }
        }
        
        // REAR 두께 측정 결과 처리
        if (!thicknesses_rear.empty()) {
                // 각 라인별 두께를 rearThicknessPoints에 저장 (InsProcessor에서 통계 계산)
                // cv::Point의 y값에 두께(픽셀)을 저장
                for (size_t i = 0; i < thicknesses_rear.size(); i++) {
                    if (rearThicknessPoints) {
                        rearThicknessPoints->push_back(cv::Point(i, thicknesses_rear[i]));
                    }
                }
            
            // REAR 검은색 구간 포인트들을 저장 (빨간색 표시용)
            if (rearBlackRegionPoints) {
                *rearBlackRegionPoints = blackRegionPoints_rear;
            }
                       
        } else {
            std::cout << "=== REAR 두께 측정 검사 ===" << std::endl;
            isPassed = false;
        }
        } // stripRearEnabled 조건문 종료
        
        // STRIP 길이 검사 수행 (활성화된 경우)
        if (stripLengthPassed) *stripLengthPassed = true;  // 기본값: PASS
        if (stripMeasuredLength) *stripMeasuredLength = 0.0;
        if (stripLengthStartPoint) *stripLengthStartPoint = cv::Point(0, 0);
        if (stripLengthEndPoint) *stripLengthEndPoint = cv::Point(0, 0);
        
        // EDGE 검사 영역 중심점 계산 (STRIP 길이 측정용)
        cv::Point2f edgeCenter(roiImage.cols / 2.0f, roiImage.rows / 2.0f);
        float edgeOffsetFromCenter = (-patternWidth/2.0f) + edgeOffsetX;
        cv::Point edgeBoxCenter = cv::Point(
            static_cast<int>(edgeCenter.x + edgeOffsetFromCenter),
            static_cast<int>(edgeCenter.y)
        );
        
        if (pattern.stripLengthEnabled && gradientPoints.size() >= 4) {
            // P3(상단 두번째), P4(하단 두번째) 점들 사용
            cv::Point p3 = gradientPoints[1];  // 상단 두번째 변화점
            cv::Point p4 = gradientPoints[3];  // 하단 두번째 변화점
            
            // P3, P4 중간점 계산
            cv::Point p34MidPoint = cv::Point((p3.x + p4.x) / 2, (p3.y + p4.y) / 2);
            
            // EDGE 평균선의 회전 중심점을 시작점으로 사용
            cv::Point edgeStartPoint;
            if (edgeAverageX && *edgeAverageX > 0 && edgePoints && !edgePoints->empty()) {
                // EDGE 포인트들을 Y 좌표 순으로 정렬
                std::vector<cv::Point> sortedPoints = *edgePoints;
                std::sort(sortedPoints.begin(), sortedPoints.end(), 
                         [](const cv::Point& a, const cv::Point& b) { return a.y < b.y; });
                
                // 중간 지점의 실제 EDGE 포인트 찾기
                int centerIdx = sortedPoints.size() / 2;
                cv::Point centerPoint = sortedPoints[centerIdx];
                
                // 중간 포인트가 평균 X값과 너무 차이나면 주변 포인트 검색
                int avgX = *edgeAverageX;
                int bestIdx = centerIdx;
                int minDistance = std::abs(centerPoint.x - avgX);
                
                // 중간 지점 주변 포인트들에서 평균 X에 가장 가까우면서 연속성이 있는 포인트 찾기
                int searchRange = std::min(5, (int)sortedPoints.size() / 3);
                
                for (int i = std::max(0, centerIdx - searchRange); 
                     i <= std::min((int)sortedPoints.size() - 1, centerIdx + searchRange); i++) {
                    
                    int distance = std::abs(sortedPoints[i].x - avgX);
                    
                    // 거리 조건: 평균 X에 더 가까워야 함
                    bool betterDistance = (distance < minDistance);
                    
                    // 연속성 조건: 주변 포인트들과 X값이 크게 다르지 않아야 함
                    bool goodContinuity = true;
                    if (i > 0 && i < (int)sortedPoints.size() - 1) {
                        int prevDiff = std::abs(sortedPoints[i].x - sortedPoints[i-1].x);
                        int nextDiff = std::abs(sortedPoints[i].x - sortedPoints[i+1].x);
                        // X값 변화가 30픽셀 이상이면 이상한 포인트로 판단
                        if (prevDiff > 30 || nextDiff > 30) {
                            goodContinuity = false;
                        }
                    }
                    
                    if (betterDistance && goodContinuity) {
                        minDistance = distance;
                        bestIdx = i;
                    }
                }
                
                // 최적의 포인트를 시작점으로 사용
                edgeStartPoint = sortedPoints[bestIdx];
                
                // 디버그 로그
                printf("[EDGE 시작점 선택] 총 %zu개 포인트, 중간 인덱스: %d, 선택된 인덱스: %d, 좌표: (%d, %d)\n",
                       sortedPoints.size(), centerIdx, bestIdx, edgeStartPoint.x, edgeStartPoint.y);
                
            } else {
                // 폴백: EDGE 검사 영역 중심점 사용
                edgeStartPoint = edgeBoxCenter;
            }
            
            // 두 점 사이의 픽셀 거리 계산
            double lengthDistancePx = cv::norm(p34MidPoint - edgeStartPoint);
            
            // 캘리브레이션 여부에 따라 픽셀 또는 mm로 변환
            double lengthDistance = 0.0;
            bool lengthInRange = false;
            
            if (pattern.stripLengthCalibrated && 
                pattern.stripLengthCalibrationPx > 0.0 && 
                pattern.stripLengthConversionMm > 0.0) {
                // 캘리브레이션이 완료된 경우: mm로 변환
                double pixelToMm = pattern.stripLengthConversionMm / pattern.stripLengthCalibrationPx;
                lengthDistance = lengthDistancePx * pixelToMm;
                
                // 허용 범위 확인 (mm 기준)
                lengthInRange = (lengthDistance >= pattern.stripLengthMin && 
                               lengthDistance <= pattern.stripLengthMax);
            } else {
                // 캘리브레이션이 안된 경우: 픽셀 값 그대로 사용
                lengthDistance = lengthDistancePx;
                
                // 픽셀 기준으로 허용 범위 확인 (필요시)
                lengthInRange = true;  // 캘리브레이션 전에는 일단 통과로 처리
            }
            

            
            // 결과 저장
            if (stripLengthPassed) *stripLengthPassed = lengthInRange;
            if (stripMeasuredLength) *stripMeasuredLength = lengthDistance;
            if (stripLengthStartPoint) *stripLengthStartPoint = edgeStartPoint;
            if (stripLengthEndPoint) *stripLengthEndPoint = p34MidPoint;
            
            isPassed = isPassed && lengthInRange;  // 전체 STRIP 검사 결과에 반영
        } else {
        }
        
        // EDGE 검사 수행 (활성화된 경우)
        if (edgeIrregularityCount) *edgeIrregularityCount = 0;
        if (edgeMaxDeviation) *edgeMaxDeviation = 0.0;
        if (edgeBoxTopLeft) *edgeBoxTopLeft = cv::Point(0, 0);
        if (edgeAverageX) *edgeAverageX = 0;
        
        if (edgeEnabled) {
            try {

                
                // 이미 계산된 EDGE 검사 박스 중심점 사용
                cv::Point2f edgeCenter(edgeBoxCenter.x, edgeBoxCenter.y);
                
                // EDGE 검사 박스 꼭짓점 계산 (수직 박스, 회전 없음)
                float halfWidth = edgeBoxWidth / 2.0f;
                float halfHeight = edgeBoxHeight / 2.0f;
                
                cv::Point2f corners[4];
                corners[0] = cv::Point2f(edgeCenter.x - halfWidth, edgeCenter.y - halfHeight);  // 좌상
                corners[1] = cv::Point2f(edgeCenter.x + halfWidth, edgeCenter.y - halfHeight);  // 우상
                corners[2] = cv::Point2f(edgeCenter.x + halfWidth, edgeCenter.y + halfHeight);  // 우하
                corners[3] = cv::Point2f(edgeCenter.x - halfWidth, edgeCenter.y + halfHeight);  // 좌하
                

                
                // 검사 영역이 이미지 범위 내에 있는지 확인
                bool inBounds = true;
                for (int i = 0; i < 4; i++) {
                    if (corners[i].x < 0 || corners[i].x >= roiImage.cols || 
                        corners[i].y < 0 || corners[i].y >= roiImage.rows) {
                        inBounds = false;
                        break;
                    }
                }
                
                if (inBounds) {
                    // 박스 좌상단 좌표 저장 (Qt 텍스트 표시용)
                    if (edgeBoxTopLeft) *edgeBoxTopLeft = cv::Point(static_cast<int>(corners[0].x), static_cast<int>(corners[0].y));
                    
                    // roiImage가 이미 필터링된 이진 영상이므로 그대로 사용
                    cv::Mat binaryImageForEdge;
                    if (roiImage.channels() == 3) {
                        // 3채널이면 그레이스케일로 변환 (이진화된 경우 모든 채널 동일)
                        cv::cvtColor(roiImage, binaryImageForEdge, cv::COLOR_BGR2GRAY);
                    } else {
                        binaryImageForEdge = roiImage.clone();
                    }
                    
                    // EDGE 검사 영역에서 절단면 분석 (Y별 수평 스캔)
                    // 퍼센트를 고려해서 스캔 범위 조정
                    float startPercentOffset = pattern.edgeStartPercent / 100.0f;  // 시작 퍼센트
                    float endPercentOffset = pattern.edgeEndPercent / 100.0f;      // 끝 퍼센트
                    
                    float effectiveHeight = edgeBoxHeight * (1.0f - startPercentOffset - endPercentOffset);  // 유효한 스캔 높이
                    int scanLines = static_cast<int>(effectiveHeight);  // 유효 높이만큼 스캔
                    
                    float startY = edgeCenter.y - edgeBoxHeight * 0.5f + (edgeBoxHeight * startPercentOffset);  // 시작 퍼센트만큼 아래에서 시작
                    float stepY = effectiveHeight / scanLines;  // Y 방향 스텝
                    

                    
                    std::vector<cv::Point> leftEdgePoints;  // 절단면 포인트들
                    
                    // 회전 각도 계산 (라디안)
                    float angleRad = angle * M_PI / 180.0f;
                    float cosAngle = cos(angleRad);
                    float sinAngle = sin(angleRad);
                    
                    for (int i = 0; i < scanLines; i++) {
                        float scanY = startY + i * stepY;  // Y를 1씩 증가
                        
                        // 검사박스 내부에서만 수평 스캔 (왼쪽에서 오른쪽으로)
                        float edgeBoxLeft = edgeCenter.x - edgeBoxWidth / 2.0f;
                        float edgeBoxRight = edgeCenter.x + edgeBoxWidth / 2.0f;
                        
                        // 검사박스 안에서만 스캔 (회전 고려)
                        cv::Point edgePoint(-1, -1);  // 초기값: 찾지 못함
                        
                        for (float x = edgeBoxLeft; x < edgeBoxRight; x += 0.5f) {
                            // 회전된 스캔라인 계산: 박스 중심에서의 상대 좌표를 회전
                            float relX = x - edgeCenter.x;
                            float relY = scanY - edgeCenter.y;
                            
                            // 회전 변환 적용
                            float rotatedX = edgeCenter.x + (relX * cosAngle - relY * sinAngle);
                            float rotatedY = edgeCenter.y + (relX * sinAngle + relY * cosAngle);
                            
                            int px = static_cast<int>(rotatedX);
                            int py = static_cast<int>(rotatedY);
                            
                            // 이미지 경계 체크
                            if (px >= 0 && px < binaryImageForEdge.cols && 
                                py >= 0 && py < binaryImageForEdge.rows) {
                                
                                // 이진화된 영상: 0=검은색, 255=흰색 (127 기준)
                                bool isBlack = (binaryImageForEdge.at<uchar>(py, px) < 127);
                                
                                if (isBlack) {
                                    // 검사박스 안에서 첫 번째 검은색 픽셀 발견 (절단면의 시작점)
                                    edgePoint = cv::Point(px, py);
                                    break;
                                }
                            }
                        }
                        
                        // 이번 Y 스캔 라인에서 절단면 포인트를 찾았으면 추가
                        if (edgePoint.x >= 0 && edgePoint.y >= 0) {
                            leftEdgePoints.push_back(edgePoint);
                        }
                    }
                    

                    
                    // 수평선 제거: 기울기가 너무 수평인 구간 필터링
                    if (leftEdgePoints.size() > 10) {
                        std::vector<cv::Point> filteredPoints;
                        
                        for (size_t i = 0; i < leftEdgePoints.size(); i++) {
                            bool isValidPoint = true;
                            
                            // 현재 점 주변 5개 점의 기울기 검사
                            if (i >= 2 && i < leftEdgePoints.size() - 2) {
                                cv::Point p1 = leftEdgePoints[i-2];
                                cv::Point p2 = leftEdgePoints[i+2];
                                
                                float dx = abs(p2.x - p1.x);
                                float dy = abs(p2.y - p1.y);
                                
                                // 기울기가 너무 수평이면 (X 변화량이 Y 변화량의 3배 이상) 제외
                                if (dy > 0 && dx > dy * 3.0f) {
                                    isValidPoint = false;
                                }
                                
                                // X 좌표가 급격히 변하는 경우도 제외 (노이즈)
                                if (dx > 50) {  // 50픽셀 이상 급변하면 노이즈로 판단
                                    isValidPoint = false;
                                }
                            }
                            
                            if (isValidPoint) {
                                filteredPoints.push_back(leftEdgePoints[i]);
                            }
                        }
                        
                        leftEdgePoints = filteredPoints;

                    }
                    
                    // EDGE 포인트들을 out 파라미터로 전달 (InsProcessor에서 통계 계산)
                    if (edgePoints) {
                        *edgePoints = leftEdgePoints;
                    }
                    
                    // 평균 X 위치를 결과에 저장 (간단한 평균만 계산, 상세 통계는 InsProcessor에서)
                    if (!leftEdgePoints.empty()) {
                        double sumX = 0.0;
                        for (const auto& pt : leftEdgePoints) {
                            sumX += pt.x;
                        }
                        double avgX = sumX / leftEdgePoints.size();
                        if (edgeAverageX) *edgeAverageX = static_cast<int>(avgX);
                    }
                    
                    // 검사 통과 (실제 판정은 InsProcessor와 CameraView에서)
                    if (edgeIrregularityCount) *edgeIrregularityCount = 0;
                    if (edgeMaxDeviation) *edgeMaxDeviation = 0.0;
                    
                } else {
                }
            } catch (const cv::Exception& e) {
            }
        }
        
        // removed debug log
        
        // 오버레이 크기를 ROI 크기에 정확히 맞춤
        if (!resultImage.empty() && (resultImage.cols != roiImage.cols || resultImage.rows != roiImage.rows)) {
            try {
                cv::resize(resultImage, resultImage, cv::Size(roiImage.cols, roiImage.rows));
            } catch (const cv::Exception& e) {
                // removed debug log
            }
        }
        
        return isPassed;
        
    } catch (const cv::Exception& e) {
        score = 0.0;
        startPoint = cv::Point(0, 0);
        maxGradientPoint = cv::Point(0, 0);
        gradientPoints.clear();
        cleanOriginal.copyTo(resultImage);
        return false;
    }
}

