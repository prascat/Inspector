#include "ImageProcessor.h"
#include <opencv2/imgproc.hpp>
#include <QDebug>
#include <algorithm>
#include <iostream> 

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
                                           double passThreshold, double& score, cv::Point& startPoint, 
                                           cv::Point& maxGradientPoint, std::vector<cv::Point>& gradientPoints, 
                                           cv::Mat& resultImage, double angle, 
                                           int* leftThickness, int* rightThickness,
                                           int morphKernelSize, 
                                           float gradientThreshold, int gradientStartPercent,
                                           int gradientEndPercent, int minDataPoints,
                                           double* neckAvgWidth, double* neckMinWidth,
                                           double* neckMaxWidth, double* neckStdDev,
                                           int* neckMeasureX, int* neckMeasureCount,
                                           int thicknessBoxWidth, int thicknessMin, 
                                           int thicknessMax, int thicknessBoxHeight,
                                           int* measuredMinThickness, int* measuredMaxThickness,
                                           int* measuredAvgThickness,
                                           int rearThicknessBoxWidth, int rearThicknessMin,
                                           int rearThicknessMax, int rearThicknessBoxHeight,
                                           int* rearMeasuredMinThickness, int* rearMeasuredMaxThickness,
                                           int* rearMeasuredAvgThickness,
                                           cv::Point* frontBoxTopLeft, cv::Point* rearBoxTopLeft,
                                           const cv::Rect& originalPatternRect) {  // 원본 패턴 박스 좌표
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
        // 디버그: 전달받은 각도 확인
        std::cout << "=== STRIP 검사 시작: 전달받은 각도 = " << angle << "도 ===" << std::endl;
        
        gradientPoints.clear();
        
        // ===== 1단계: 이진화 및 형태학적 연산 =====
        cv::Mat processImage = roiImage;
        
        cv::Mat gray;
        if (processImage.channels() == 3) {
            cv::cvtColor(processImage, gray, cv::COLOR_BGR2GRAY);
        } else {
            processImage.copyTo(gray);
        }
        
        // 이진화 (Otsu's Thresholding)
        cv::Mat binary;
        cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY + cv::THRESH_OTSU);
        
        // 형태학적 연산 (Opening, Closing)
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(morphKernelSize, morphKernelSize));
        cv::Mat processed;
        cv::morphologyEx(binary, processed, cv::MORPH_OPEN, kernel);
        cv::morphologyEx(processed, processed, cv::MORPH_CLOSE, kernel);
        
        // ===== INS 티칭 패턴 영역만 마스킹 (올바른 순서) =====
        // 1. 유효한 패턴 영역 찾기 (검은색이 아닌 영역)
        cv::Mat grayCheck;
        cv::cvtColor(roiImage, grayCheck, cv::COLOR_BGR2GRAY);
        cv::Mat validMask = (grayCheck > 0); // 검은색(0)이 아닌 영역
        
        // 2. 마스킹 적용: 유효하지 않은 영역을 흰색(255)으로 설정
        cv::Mat maskedProcessed = processed.clone();
        maskedProcessed.setTo(255, ~validMask); // 유효하지 않은 영역을 흰색으로
        
        std::cout << "INS 패턴 영역 마스킹 완료: 유효하지 않은 영역을 흰색으로 처리" << std::endl;
        
        // ===== 2단계: 컨투어 검출 (마스킹된 이미지에서) =====
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(maskedProcessed, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        
        // 디버그: 컨투어 개수 출력만
        std::cout << "컨투어 검출 완료: 총 " << contours.size() << "개 컨투어" << std::endl;
        
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
        std::cout << "검출된 컨투어 경계: (" << boundRect.x << "," << boundRect.y << ") " 
                  << boundRect.width << "x" << boundRect.height << std::endl;
        
        // boundRect 유효성 검사
        if (boundRect.width <= 0 || boundRect.height <= 0 || 
            boundRect.x < 0 || boundRect.y < 0 ||
            boundRect.x >= roiImage.cols || boundRect.y >= roiImage.rows) {
            std::cout << "유효하지 않은 boundRect 검출됨" << std::endl;
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
        auto calculateMeaningfulGradients = [gradientThreshold, gradientStartPercent, gradientEndPercent, minDataPoints, &originalPatternRect]
            (const std::vector<float>& thicknesses, const std::vector<cv::Point>& positions) -> std::vector<float> {
            std::vector<float> gradients(thicknesses.size(), 0.0f);
            
            if (thicknesses.size() < static_cast<size_t>(minDataPoints)) return gradients; // 파라미터로 받은 최소 개수
            
            // 원본 패턴 크기 기준으로 start/end X 좌표 계산
            int patternStartX = originalPatternRect.x;
            int patternWidth = originalPatternRect.width;
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
        int measureX = originalPatternRect.x + (originalPatternRect.width * gradientStartPercent / 100);
        
        // measureX 경계 체크 및 보정
        if (measureX < 0) measureX = 0;
        if (measureX >= blackRegions.cols) measureX = blackRegions.cols - 1;
        
        neckMeasureXPos = measureX;
        
        // 각도를 라디안으로 변환
        double angleRad = angle * CV_PI / 180.0;
        
        // 패턴의 가로/세로 방향 벡터 계산 (각도 고려)
        cv::Point2f horizontalVec(cos(angleRad), sin(angleRad));      // 패턴의 가로 방향
        cv::Point2f verticalVec(-sin(angleRad), cos(angleRad));       // 패턴의 세로 방향 (가로에 수직)
        
        std::cout << "=== 목 부분 절단 품질 측정 (각도: " << angle << "도, 20% X지점: " << measureX << ") ===" << std::endl;
        std::cout << "측정 방향 벡터: 가로(" << horizontalVec.x << "," << horizontalVec.y 
                  << ") 세로(" << verticalVec.x << "," << verticalVec.y << ")" << std::endl;
        
        // 20% X 지점부터 width만큼 X를 이동하면서 각 X 위치에서 Y축 방향 검은색 픽셀 개수 측정
        for (int x = measureX; x < originalPatternRect.x + originalPatternRect.width; x++) {
            // 현재 X 위치에서 Y축 방향으로 검은색 픽셀 개수 세기
            int blackPixelCount = 0;
            cv::Point2f startPos(x, originalPatternRect.y);
            cv::Point2f actualStartPos = startPos;
            
            // 위에서 아래로 Y축 스캔하면서 연속된 검은색 픽셀 개수 카운트
            for (int y = originalPatternRect.y; y < originalPatternRect.y + originalPatternRect.height; y++) {
                
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
            
            std::cout << "측정 포인트 수: " << neckWidths.size() << "개" << std::endl;
            std::cout << "평균 검은색 픽셀 개수: " << avgNeckWidth << "px" << std::endl;
            std::cout << "최소 검은색 픽셀 개수: " << minNeckWidth << "px" << std::endl;
            std::cout << "최대 검은색 픽셀 개수: " << maxNeckWidth << "px" << std::endl;
            std::cout << "표준편차: " << neckWidthStdDev << "px" << std::endl;
            std::cout << "편차: " << (maxNeckWidth - minNeckWidth) << "px" << std::endl;
            
            // 목 폭 측정 결과를 매개변수에 저장
            if (neckAvgWidth) *neckAvgWidth = avgNeckWidth;
            if (neckMinWidth) *neckMinWidth = minNeckWidth;
            if (neckMaxWidth) *neckMaxWidth = maxNeckWidth;
            if (neckStdDev) *neckStdDev = neckWidthStdDev;
            if (neckMeasureX) *neckMeasureX = neckMeasureXPos;
            if (neckMeasureCount) *neckMeasureCount = static_cast<int>(neckWidths.size());
            
            // 측정 결과를 이미지 위에 텍스트로 표시 (회전 각도 고려)
            if (!neckWidths.empty()) {
                std::cout << "텍스트 그리기 시작 - 측정 개수: " << neckWidths.size() << std::endl;
                
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
                
                std::cout << "텍스트 위치 (회전 고려): center(" << center.x << "," << center.y 
                          << ") -> textPos(" << textPos.x << "," << textPos.y << ")" << std::endl;
                
                // 배경 박스 그리기 (더 눈에 띄는 색상으로)
                cv::Rect textBgRect(textPos.x, textPos.y, boxWidth, boxHeight);
                cv::rectangle(resultImage, textBgRect, cv::Scalar(0, 0, 0), -1); // 검은색 배경
                cv::rectangle(resultImage, textBgRect, cv::Scalar(0, 255, 0), 2); // 초록색 테두리 (더 두껍게)
                
                // 텍스트 정보 준비
                std::string minMaxText = "Min: " + std::to_string((int)minNeckWidth) + "px";
                std::string maxText = "Max: " + std::to_string((int)maxNeckWidth) + "px";
                std::string avgText = "Avg: " + std::to_string((int)avgNeckWidth) + "px";
                
                std::cout << "텍스트 내용: " << minMaxText << ", " << maxText << ", " << avgText << std::endl;
                
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
                           
                std::cout << "텍스트 그리기 완료" << std::endl;
            } else {
                std::cout << "neckWidths가 비어있음 - 텍스트 그리기 스킵" << std::endl;
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
                
                std::cout << "두께 측정 영역 사각형 그리기 완료 (각도: " << angle << "도)" << std::endl;
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
        
        std::cout << "급격한 변화 탐지 - 임계값: " << sensitiveThreshold << std::endl;
        
        // 상단 컨투어에서 급격한 변화 지점 찾기 (10%-90% 구간으로 확장)
        if (!topPositions.empty() && !topGradients.empty()) {
            size_t startIdx = topPositions.size() * 10 / 100; // 10%
            size_t endIdx = topPositions.size() * 90 / 100;   // 90%
            
            std::cout << "상단 탐색 구간: " << startIdx << " ~ " << endIdx << " (총 " << topPositions.size() << "개)" << std::endl;
            
            // 첫 번째 급격한 변화 지점 (앞쪽에서)
            for (size_t i = startIdx; i < endIdx; i++) {
                if (std::abs(topGradients[i]) >= sensitiveThreshold) {
                    point1 = topPositions[i];
                    hasPoint1 = true;
                    gradientPoints.push_back(point1);
                    std::cout << "찾은 Point1 (상단 첫번째): idx=" << i << ", gradient=" << topGradients[i] << std::endl;
                    break;
                }
            }
            
            // 두 번째 급격한 변화 지점 (뒤쪽에서)
            for (size_t i = endIdx; i > startIdx; i--) {
                if (std::abs(topGradients[i-1]) >= sensitiveThreshold) {
                    point3 = topPositions[i-1];
                    hasPoint3 = true;
                    gradientPoints.push_back(point3);
                    std::cout << "찾은 Point3 (상단 두번째): idx=" << (i-1) << ", gradient=" << topGradients[i-1] << std::endl;
                    break;
                }
            }
        }
        
        // 하단 컨투어에서 급격한 변화 지점 찾기 (10%-90% 구간으로 확장)
        if (!bottomPositions.empty() && !bottomGradients.empty()) {
            size_t startIdx = bottomPositions.size() * 10 / 100; // 10%
            size_t endIdx = bottomPositions.size() * 90 / 100;   // 90%
            
            std::cout << "하단 탐색 구간: " << startIdx << " ~ " << endIdx << " (총 " << bottomPositions.size() << "개)" << std::endl;
            
            // 세 번째 급격한 변화 지점 (앞쪽에서)
            for (size_t i = startIdx; i < endIdx; i++) {
                if (std::abs(bottomGradients[i]) >= sensitiveThreshold) {
                    point2 = bottomPositions[i];
                    hasPoint2 = true;
                    gradientPoints.push_back(point2);
                    std::cout << "찾은 Point2 (하단 첫번째): idx=" << i << ", gradient=" << bottomGradients[i] << std::endl;
                    break;
                }
            }
            
            // 네 번째 급격한 변화 지점 (뒤쪽에서)
            for (size_t i = endIdx; i > startIdx; i--) {
                if (std::abs(bottomGradients[i-1]) >= sensitiveThreshold) {
                    point4 = bottomPositions[i-1];
                    hasPoint4 = true;
                    gradientPoints.push_back(point4);
                    std::cout << "찾은 Point4 (하단 두번째): idx=" << (i-1) << ", gradient=" << bottomGradients[i-1] << std::endl;
                    break;
                }
            }
        }
        
        std::cout << "STRIP 검사 - 급격한 두께 변화 지점 4개:" << std::endl;
        if (hasPoint1) std::cout << "1. 상단 첫번째 변화점: (" << point1.x << "," << point1.y << ") gradient=" << topGradients[std::distance(topPositions.begin(), std::find(topPositions.begin(), topPositions.end(), point1))] << std::endl;
        if (hasPoint2) std::cout << "2. 하단 첫번째 변화점: (" << point2.x << "," << point2.y << ") gradient=" << bottomGradients[std::distance(bottomPositions.begin(), std::find(bottomPositions.begin(), bottomPositions.end(), point2))] << std::endl;
        if (hasPoint3) std::cout << "3. 상단 두번째 변화점: (" << point3.x << "," << point3.y << ") gradient=" << topGradients[std::distance(topPositions.begin(), std::find(topPositions.begin(), topPositions.end(), point3))] << std::endl;
        if (hasPoint4) std::cout << "4. 하단 두번째 변화점: (" << point4.x << "," << point4.y << ") gradient=" << bottomGradients[std::distance(bottomPositions.begin(), std::find(bottomPositions.begin(), bottomPositions.end(), point4))] << std::endl;
        
        std::cout << "STRIP 검사 결과:" << std::endl;
        std::cout << "- 선택된 컨투어: " << (useTopContour ? "상단" : "하단") 
                  << ", 스캔 방향: " << (useReverse ? "역방향" : "정방향") << std::endl;
        std::cout << "- 상단 정방향: " << topMaxGrad << ", 역방향: " << topMaxGradReverse << std::endl;
        std::cout << "- 하단 정방향: " << bottomMaxGrad << ", 역방향: " << bottomMaxGradReverse << std::endl;
        
        if (!selectedPositions.empty()) {
            std::cout << "- 시작점: (" << startPoint.x << "," << startPoint.y << 
                        "), 최대 gradient점: (" << maxGradientPoint.x << "," << maxGradientPoint.y << ")" << std::endl;
        }
        
        std::cout << "총 시각화 포인트: " << gradientPoints.size() << "개" << std::endl;
        
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
            std::cout << "Gradient 분석: 최대값=" << maxGrad << ", 평균값=" << avgGrad << ", 총개수=" << absGradients.size() << std::endl;
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
            
            std::cout << "Max Gradient 탐지 결과: idx=" << maxGradientIdx << ", value=" << maxGradientValue 
                     << ", position=(" << maxGradientPoint.x << "," << maxGradientPoint.y << ")" << std::endl;
        } else {
            maxGradientPoint = positions.back(); // fallback: 가장 오른쪽
            maxGradientIdx = positions.size() - 1;
            std::cout << "Gradient가 없어 fallback 사용: (" << maxGradientPoint.x << "," << maxGradientPoint.y << ")" << std::endl;
        }
        
        // Max gradient 위치에서 좌우 두께 측정
        int leftThick = 0, rightThick = 0;
        if (maxGradientIdx < positions.size()) {
            // 패턴 각도에 따른 수직 방향 계산
            double angleRad = angle * CV_PI / 180.0;
            double perpX = -sin(angleRad); // 수직 방향 X 성분
            double perpY = cos(angleRad);  // 수직 방향 Y 성분
            
            cv::Mat grayForThickness;
            if (processImage.channels() == 3) {
                cv::cvtColor(processImage, grayForThickness, cv::COLOR_BGR2GRAY);
            } else {
                processImage.copyTo(grayForThickness);
            }
            
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
        if (leftThickness) *leftThickness = leftThick;
        if (rightThickness) *rightThickness = rightThick;
        
        // gradient 지점이 없으면 기본값 추가
        if (gradientPoints.empty() && positions.size() > 2) {
            gradientPoints.push_back(positions[positions.size() / 2]); // 중간점
        }
        
        // 회전 전의 원래 위치 저장 (두께 검사용)
        cv::Point originalStartPoint = startPoint;
        cv::Point originalMaxGradientPoint = maxGradientPoint;
        std::vector<cv::Point> originalGradientPoints = gradientPoints;
        
        // 컨투어는 원래 방향으로 검출하고, gradient 분석 결과만 패턴 각도에 맞춰 회전
        if (std::abs(angle) > 0.1) { // 0.1도 이상일 때만 회전 적용
            // 디버그 로그
            std::cout << "STRIP 각도 적용: " << angle << "도" << std::endl;
            std::cout << "회전 전 시작점: (" << startPoint.x << ", " << startPoint.y << ")" << std::endl;
            std::cout << "회전 전 maxGradient점: (" << maxGradientPoint.x << ", " << maxGradientPoint.y << ")" << std::endl;
            
            double angleRad = angle * CV_PI / 180.0; // 각도를 라디안으로 변환
            double cosA = cos(angleRad);
            double sinA = sin(angleRad);
            
            // 이미지 중심점 기준으로 회전
            float centerX = roiImage.cols / 2.0f;
            float centerY = roiImage.rows / 2.0f;
            
            // 시작점 회전
            float relX = startPoint.x - centerX;
            float relY = startPoint.y - centerY;
            float newX = relX * cosA - relY * sinA + centerX;
            float newY = relX * sinA + relY * cosA + centerY;
            startPoint = cv::Point(static_cast<int>(newX), static_cast<int>(newY));
            
            // 끝점 회전
            relX = maxGradientPoint.x - centerX;
            relY = maxGradientPoint.y - centerY;
            newX = relX * cosA - relY * sinA + centerX;
            newY = relX * sinA + relY * cosA + centerY;
            maxGradientPoint = cv::Point(static_cast<int>(newX), static_cast<int>(newY));
            
            // gradient 지점들 회전
            for (cv::Point& gradPoint : gradientPoints) {
                relX = gradPoint.x - centerX;
                relY = gradPoint.y - centerY;
                newX = relX * cosA - relY * sinA + centerX;
                newY = relX * sinA + relY * cosA + centerY;
                gradPoint = cv::Point(static_cast<int>(newX), static_cast<int>(newY));
            }
            
            // 디버그 로그
            std::cout << "회전 후 시작점: (" << startPoint.x << ", " << startPoint.y << ")" << std::endl;
            std::cout << "회전 후 maxGradient점: (" << maxGradientPoint.x << ", " << maxGradientPoint.y << ")" << std::endl;
        } else {
            std::cout << "STRIP 각도가 0.1도 미만이므로 회전 생략: " << angle << "도" << std::endl;
        }
        
        // 디버그: 두께 측정 결과
        std::cout << "두께 측정 결과: 좌측=" << leftThick << "px, 우측=" << rightThick << "px" << std::endl;
        
        // 원본 이미지 복사 (컨투어 선 제거, 연결선과 원만 표시) - 보정된 이미지 사용
        cleanOriginal.copyTo(resultImage);
        
        // 4개 핵심 포인트를 숫자와 점으로 표시 (더 크게, 더 명확하게)
        std::cout << "\n=== 4개 포인트 시각화 ===\n";
        
        if (hasPoint1) {
            std::cout << "Point 1 원본 위치: (" << point1.x << "," << point1.y << ")" << std::endl;
            cv::circle(resultImage, point1, 6, cv::Scalar(0, 0, 255), -1); // 빨간색 원 (작게)
            cv::circle(resultImage, point1, 8, cv::Scalar(255, 255, 255), 2); // 흰색 테두리
            cv::putText(resultImage, "1", cv::Point(point1.x + 12, point1.y - 10), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 2); // 빨간색 숫자 (작게)
        }
        
        if (hasPoint2) {
            std::cout << "Point 2 원본 위치: (" << point2.x << "," << point2.y << ")" << std::endl;
            cv::circle(resultImage, point2, 6, cv::Scalar(0, 255, 0), -1); // 초록색 원 (작게)
            cv::circle(resultImage, point2, 8, cv::Scalar(255, 255, 255), 2); // 흰색 테두리
            cv::putText(resultImage, "2", cv::Point(point2.x + 12, point2.y + 20), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2); // 초록색 숫자 (작게)
        }
        
        if (hasPoint3) {
            std::cout << "Point 3 원본 위치: (" << point3.x << "," << point3.y << ")" << std::endl;
            cv::circle(resultImage, point3, 6, cv::Scalar(255, 0, 0), -1); // 파란색 원 (작게)
            cv::circle(resultImage, point3, 8, cv::Scalar(255, 255, 255), 2); // 흰색 테두리
            cv::putText(resultImage, "3", cv::Point(point3.x - 18, point3.y - 10), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 0), 2); // 파란색 숫자 (작게)
        }
        
        if (hasPoint4) {
            std::cout << "Point 4 원본 위치: (" << point4.x << "," << point4.y << ")" << std::endl;
            cv::circle(resultImage, point4, 6, cv::Scalar(255, 255, 0), -1); // 청록색 원 (작게)
            cv::circle(resultImage, point4, 8, cv::Scalar(255, 255, 255), 2); // 흰색 테두리
            cv::putText(resultImage, "4", cv::Point(point4.x - 18, point4.y + 20), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 0), 2); // 청록색 숫자 (작게)
        }
        
        // 4개 컨투어 라인을 각각 다른 색상으로 표시 (시작점부터 찾은점까지만, ROI 내에서만)
        std::cout << "\n=== 4개 컨투어 라인 그리기 ===\n";
        
        // ROI 경계 확인 함수
        auto isInROI = [&resultImage](const cv::Point& p) -> bool {
            return p.x >= 0 && p.y >= 0 && p.x < resultImage.cols && p.y < resultImage.rows;
        };
        
        // Gradient Start/End 지점 표시 제거 (시각화 간소화)
        // 기존의 START/END 다이아몬드 표시와 텍스트를 제거하여 깔끔한 결과 화면 제공
        
        // Gradient 분석 구간 점선 연결 제거 (시각화 간소화)
        
        // 1. 상단 첫번째 컨투어 (빨간색) - 왼쪽 끝부터 point1까지
        if (hasPoint1 && !topPositions.empty()) {
            auto startIt = topPositions.begin();
            auto endIt = std::find(topPositions.begin(), topPositions.end(), point1);
            if (endIt != topPositions.end()) {
                for (auto it = startIt; it != endIt; ++it) {
                    cv::Point currentPos = *it;
                    cv::Point nextPos = *(it + 1);
                    
                    // ROI 내부에 있는 경우에만 그리기 (회전 적용 안함)
                    if (isInROI(currentPos) && isInROI(nextPos)) {
                        cv::line(resultImage, currentPos, nextPos, cv::Scalar(0, 0, 255), 2); // 빨간색
                    }
                }
                std::cout << "상단 첫번째 컨투어 (빨간색): " << std::distance(startIt, endIt) << "개 선분\n";
            }
        }
        
        // 2. 하단 첫번째 컨투어 (초록색) - 왼쪽 끝부터 point2까지
        if (hasPoint2 && !bottomPositions.empty()) {
            auto startIt = bottomPositions.begin();
            auto endIt = std::find(bottomPositions.begin(), bottomPositions.end(), point2);
            if (endIt != bottomPositions.end()) {
                for (auto it = startIt; it != endIt; ++it) {
                    cv::Point currentPos = *it;
                    cv::Point nextPos = *(it + 1);
                    
                    // ROI 내부에 있는 경우에만 그리기 (회전 적용 안함)
                    if (isInROI(currentPos) && isInROI(nextPos)) {
                        cv::line(resultImage, currentPos, nextPos, cv::Scalar(0, 255, 0), 2); // 초록색
                    }
                }
                std::cout << "하단 첫번째 컨투어 (초록색): " << std::distance(startIt, endIt) << "개 선분\n";
            }
        }
        
        // 3. 상단 두번째 컨투어 (파란색) - point3부터 오른쪽 끝까지
        if (hasPoint3 && !topPositions.empty()) {
            auto startIt = std::find(topPositions.begin(), topPositions.end(), point3);
            auto endIt = topPositions.end() - 1;
            if (startIt != topPositions.end()) {
                for (auto it = startIt; it != endIt; ++it) {
                    cv::Point currentPos = *it;
                    cv::Point nextPos = *(it + 1);
                    
                    // ROI 내부에 있는 경우에만 그리기 (회전 적용 안함)
                    if (isInROI(currentPos) && isInROI(nextPos)) {
                        cv::line(resultImage, currentPos, nextPos, cv::Scalar(255, 0, 0), 2); // 파란색
                    }
                }
                std::cout << "상단 두번째 컨투어 (파란색): " << std::distance(startIt, endIt) << "개 선분\n";
            }
        }
        
        // 4. 하단 두번째 컨투어 (청록색) - point4부터 오른쪽 끝까지
        if (hasPoint4 && !bottomPositions.empty()) {
            auto startIt = std::find(bottomPositions.begin(), bottomPositions.end(), point4);
            auto endIt = bottomPositions.end() - 1;
            if (startIt != bottomPositions.end()) {
                for (auto it = startIt; it != endIt; ++it) {
                    cv::Point currentPos = *it;
                    cv::Point nextPos = *(it + 1);
                    
                    // ROI 내부에 있는 경우에만 그리기 (회전 적용 안함)
                    if (isInROI(currentPos) && isInROI(nextPos)) {
                        cv::line(resultImage, currentPos, nextPos, cv::Scalar(255, 255, 0), 2); // 청록색
                    }
                }
                std::cout << "하단 두번째 컨투어 (청록색): " << std::distance(startIt, endIt) << "개 선분\n";
            }
        }
        
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
        
        // 디버그: STRIP 검사 점수 및 판정 로그
        std::cout << "=== STRIP 검사 결과 ===" << std::endl;
        std::cout << "gradientPoints 개수: " << gradientPoints.size() << std::endl;
        std::cout << "peakQuality: " << peakQuality << std::endl;
        std::cout << "계산된 점수: " << score << std::endl;
        std::cout << "임계값: " << passThreshold << std::endl;
        std::cout << "hasMinimumFeatures: " << (hasMinimumFeatures ? "true" : "false") << std::endl;
        std::cout << "최종 판정: " << (isPassed ? "PASS" : "FAIL") << std::endl;
        
        // STRIP 두께 측정 - 검은색 픽셀 기반 (검출된 boundRect 기준 고정 위치)
        if (!topPositions.empty() && !bottomPositions.empty()) {
            // 검출된 boundRect 기준으로 두께 측정 위치 계산 (고정)
            float startPercent = gradientStartPercent / 100.0f;
            double angleRad = angle * CV_PI / 180.0;
            
            // 검출된 boundRect의 중심점과 크기
            cv::Point boundRectCenter(boundRect.x + boundRect.width / 2, 
                                     boundRect.y + boundRect.height / 2);
            int boundRectWidth = boundRect.width;
            
            // gradient 시작점까지의 거리 (boundRect 왼쪽 끝에서 startPercent만큼)
            float gradientStartX = boundRect.x + (startPercent * boundRectWidth);
            
            // 각도 적용한 실제 gradient 시작점 중앙 계산 (boundRect 중심 기준으로 회전)
            float localX = gradientStartX - boundRectCenter.x;  // boundRect 중심 기준 상대 좌표
            float localY = 0;  // Y는 boundRect 중심선 기준
            
            int boxCenterX = boundRectCenter.x + static_cast<int>(localX * cos(angleRad) - localY * sin(angleRad));
            int boxCenterY = boundRectCenter.y + static_cast<int>(localX * sin(angleRad) + localY * cos(angleRad));
            
            std::cout << "boundRect 기준 두께 측정 위치: (" << boxCenterX << ", " << boxCenterY << ")" << std::endl;
            std::cout << "boundRect 중심: (" << boundRectCenter.x << ", " << boundRectCenter.y << ")" << std::endl;
            std::cout << "패턴 각도: " << angle << "도, startPercent: " << gradientStartPercent << "%" << std::endl;
            std::cout << "두께 측정 영역 크기: " << thicknessBoxWidth << " x " << thicknessBoxHeight << "px" << std::endl;
            
            std::vector<int> thicknesses;
            std::vector<cv::Point> measurementLines; // 측정 라인 저장
            std::vector<cv::Point> blackPixelPoints; // 검은색 픽셀 위치 저장 (시각화용)
            
            // 각도 계산은 위에서 이미 완료됨 (angleRad, cosA, sinA 사용)
            double cosA = cos(angleRad);
            double sinA = sin(angleRad);
            
            // 박스 영역에서 세로 방향으로 스캔하여 검은색 픽셀 두께 측정
            // gradient 시작점을 검사 width의 중앙으로 설정
            for (int dx = -thicknessBoxWidth/2; dx <= thicknessBoxWidth/2; dx += 3) { // 3픽셀 간격으로 스캔
                // 각도를 적용한 세로 스캔 라인 계산
                cv::Point scanTop, scanBottom;
                
                if (std::abs(angle) < 0.1) {
                    // 각도가 거의 없는 경우 - 직선 스캔
                    scanTop = cv::Point(boxCenterX + dx, boxCenterY - thicknessBoxHeight/2);
                    scanBottom = cv::Point(boxCenterX + dx, boxCenterY + thicknessBoxHeight/2);
                } else {
                    // 각도가 있는 경우 - 회전된 스캔 라인
                    int localX = dx;
                    int localTopY = -thicknessBoxHeight/2;
                    int localBottomY = thicknessBoxHeight/2;
                    
                    // 각도 적용하여 회전
                    scanTop.x = boxCenterX + static_cast<int>(localX * cosA - localTopY * sinA);
                    scanTop.y = boxCenterY + static_cast<int>(localX * sinA + localTopY * cosA);
                    
                    scanBottom.x = boxCenterX + static_cast<int>(localX * cosA - localBottomY * sinA);
                    scanBottom.y = boxCenterY + static_cast<int>(localX * sinA + localBottomY * cosA);
                }
                
                // 스캔 라인이 이미지 범위를 벗어나면 건너뛰기
                if (scanTop.x < 0 || scanTop.y < 0 || scanBottom.x < 0 || scanBottom.y < 0 ||
                    scanTop.x >= cleanOriginal.cols || scanTop.y >= cleanOriginal.rows ||
                    scanBottom.x >= cleanOriginal.cols || scanBottom.y >= cleanOriginal.rows) {
                    continue;
                }
                
                // 세로 라인을 따라 검은색 픽셀 구간 찾기
                cv::LineIterator it(cleanOriginal, scanTop, scanBottom, 8);
                std::vector<std::pair<int, int>> blackRegions; // 검은색 구간의 시작과 끝
                int regionStart = -1;
                bool inBlackRegion = false;
                
                // 라인 상의 모든 점들 저장 (시각화용)
                std::vector<cv::Point> linePoints;
                for (int i = 0; i < it.count; i++, ++it) {
                    linePoints.push_back(it.pos());
                }
                
                // 다시 처음부터 스캔하여 검은색 픽셀 찾기
                it = cv::LineIterator(cleanOriginal, scanTop, scanBottom, 8);
                for (int i = 0; i < it.count; i++, ++it) {
                    cv::Vec3b pixel = cleanOriginal.at<cv::Vec3b>(it.pos());
                    // 검은색 픽셀 판단 (RGB 값이 모두 낮은 경우)
                    bool isBlack = (pixel[0] < 50 && pixel[1] < 50 && pixel[2] < 50);
                    
                    if (isBlack && !inBlackRegion) {
                        // 검은색 구간 시작
                        regionStart = i;
                        inBlackRegion = true;
                    } else if (!isBlack && inBlackRegion) {
                        // 검은색 구간 끝
                        int thickness = i - regionStart;
                        if (thickness >= 3) { // 최소 3픽셀 이상만 유효한 두께로 인정
                            thicknesses.push_back(thickness);
                            blackRegions.push_back({regionStart, i});
                            
                            // 검은색 구간의 픽셀들을 시각화용으로 저장
                            for (int j = regionStart; j < i; j++) {
                                if (j < linePoints.size()) {
                                    blackPixelPoints.push_back(linePoints[j]);
                                }
                            }
                        }
                        inBlackRegion = false;
                    }
                }
                
                // 라인 끝에서 검은색 구간이 계속되는 경우
                if (inBlackRegion && regionStart >= 0) {
                    int thickness = it.count - regionStart;
                    if (thickness >= 3) {
                        thicknesses.push_back(thickness);
                        blackRegions.push_back({regionStart, it.count});
                        
                        // 검은색 구간의 픽셀들을 시각화용으로 저장
                        for (int j = regionStart; j < it.count; j++) {
                            if (j < linePoints.size()) {
                                blackPixelPoints.push_back(linePoints[j]);
                            }
                        }
                    }
                }
                
                // 검은색 구간이 발견된 경우 측정 라인 저장
                if (!blackRegions.empty()) {
                    measurementLines.push_back(scanTop);
                    measurementLines.push_back(scanBottom);
                }
            }
            
            // 두께 측정 결과 처리
            if (!thicknesses.empty()) {
                // 최소, 최대, 평균 두께 계산
                int minThickness = *std::min_element(thicknesses.begin(), thicknesses.end());
                int maxThickness = *std::max_element(thicknesses.begin(), thicknesses.end());
                int avgThickness = std::accumulate(thicknesses.begin(), thicknesses.end(), 0) / thicknesses.size();
                
                // 측정 결과를 매개변수에 저장
                if (measuredMinThickness) *measuredMinThickness = minThickness;
                if (measuredMaxThickness) *measuredMaxThickness = maxThickness;
                if (measuredAvgThickness) *measuredAvgThickness = avgThickness;
                
                // 두께 판정
                bool thicknessPassed = (avgThickness >= thicknessMin && avgThickness <= thicknessMax);
                
                // 최종 판정에 두께 조건 추가
                isPassed = isPassed && thicknessPassed;
                
                std::cout << "=== STRIP 두께 측정 검사 (검은색 픽셀 기반) ===" << std::endl;
                std::cout << "가로 검사 범위: " << thicknessBoxWidth << "px (스캔라인 개수: " << (thicknessBoxWidth/3 + 1) << "개)" << std::endl;
                std::cout << "측정된 두께 - 최소: " << minThickness << "px, 최대: " << maxThickness << "px, 평균: " << avgThickness << "px" << std::endl;
                std::cout << "허용 범위: " << thicknessMin << " ~ " << thicknessMax << " px" << std::endl;
                std::cout << "두께 판정: " << (thicknessPassed ? "PASS" : "FAIL") << std::endl;
                
                // 측정 위치에 빨간색 선 그리기
                for (size_t i = 0; i < measurementLines.size(); i += 2) {
                    cv::line(resultImage, measurementLines[i], measurementLines[i+1], cv::Scalar(0, 0, 255), 1); // 빨간색 선
                }
                
                // 검출된 검은색 픽셀들을 초록색으로 표시
                for (const cv::Point& blackPixel : blackPixelPoints) {
                    if (blackPixel.x >= 0 && blackPixel.y >= 0 && 
                        blackPixel.x < resultImage.cols && blackPixel.y < resultImage.rows) {
                        cv::Vec3b& pixel = resultImage.at<cv::Vec3b>(blackPixel);
                        pixel[0] = 0;   // B
                        pixel[1] = 255; // G
                        pixel[2] = 0;   // R - 순수 초록색으로 표시
                    }
                }
                
                // 두께 측정 박스를 점선으로 그리기
                cv::Scalar boxColor = thicknessPassed ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255); // 통과시 초록, 실패시 빨강
                
                // 점선 그리기 함수
                auto drawDottedLine = [&](cv::Point pt1, cv::Point pt2, cv::Scalar color, int thickness = 2) {
                    cv::LineIterator it(resultImage, pt1, pt2, 8);
                    for (int i = 0; i < it.count; i++, ++it) {
                        if (i % 6 < 3) { // 3픽셀 그리고 3픽셀 건너뛰기
                            if (it.pos().x >= 0 && it.pos().y >= 0 && 
                                it.pos().x < resultImage.cols && it.pos().y < resultImage.rows) {
                                cv::Vec3b& pixel = resultImage.at<cv::Vec3b>(it.pos());
                                pixel[0] = color[0];
                                pixel[1] = color[1]; 
                                pixel[2] = color[2];
                            }
                        }
                    }
                };
                
                // 박스 꼭짓점 계산 (각도 적용)
                cv::Point topLeft, topRight, bottomLeft, bottomRight;
                
                if (std::abs(angle) < 0.1) {
                    // 각도가 거의 없는 경우
                    topLeft = cv::Point(boxCenterX - thicknessBoxWidth/2, boxCenterY - thicknessBoxHeight/2);
                    topRight = cv::Point(boxCenterX + thicknessBoxWidth/2, boxCenterY - thicknessBoxHeight/2);
                    bottomLeft = cv::Point(boxCenterX - thicknessBoxWidth/2, boxCenterY + thicknessBoxHeight/2);
                    bottomRight = cv::Point(boxCenterX + thicknessBoxWidth/2, boxCenterY + thicknessBoxHeight/2);
                } else {
                    // 각도 적용한 박스 꼭짓점
                    auto rotatePoint = [&](int localX, int localY) -> cv::Point {
                        int rotatedX = boxCenterX + static_cast<int>(localX * cosA - localY * sinA);
                        int rotatedY = boxCenterY + static_cast<int>(localX * sinA + localY * cosA);
                        return cv::Point(rotatedX, rotatedY);
                    };
                    
                    topLeft = rotatePoint(-thicknessBoxWidth/2, -thicknessBoxHeight/2);
                    topRight = rotatePoint(thicknessBoxWidth/2, -thicknessBoxHeight/2);
                    bottomLeft = rotatePoint(-thicknessBoxWidth/2, thicknessBoxHeight/2);
                    bottomRight = rotatePoint(thicknessBoxWidth/2, thicknessBoxHeight/2);
                }
                
                // 두께 측정 박스 그리기 (점선)
                drawDottedLine(topLeft, topRight, boxColor);       // 상단
                drawDottedLine(bottomLeft, bottomRight, boxColor); // 하단
                drawDottedLine(topLeft, bottomLeft, boxColor);     // 좌측
                drawDottedLine(topRight, bottomRight, boxColor);   // 우측
                
                // FRONT 박스 위치 저장 (Qt 텍스트 그리기용)
                if (frontBoxTopLeft) {
                    *frontBoxTopLeft = topLeft;
                }
                           
            } else {
                std::cout << "=== STRIP 두께 측정 검사 ===" << std::endl;
                std::cout << "검은색 픽셀을 찾을 수 없어서 두께 측정 실패" << std::endl;
                isPassed = false;
                
                // 측정 실패 시에도 박스만 그리기
                cv::Scalar boxColor = cv::Scalar(0, 0, 255); // 빨간색
                
                auto drawDottedLine = [&](cv::Point pt1, cv::Point pt2, cv::Scalar color, int thickness = 2) {
                    cv::LineIterator it(resultImage, pt1, pt2, 8);
                    for (int i = 0; i < it.count; i++, ++it) {
                        if (i % 6 < 3) {
                            if (it.pos().x >= 0 && it.pos().y >= 0 && 
                                it.pos().x < resultImage.cols && it.pos().y < resultImage.rows) {
                                cv::Vec3b& pixel = resultImage.at<cv::Vec3b>(it.pos());
                                pixel[0] = color[0];
                                pixel[1] = color[1]; 
                                pixel[2] = color[2];
                            }
                        }
                    }
                };
                
                cv::Point topLeft, topRight, bottomLeft, bottomRight;
                
                if (std::abs(angle) < 0.1) {
                    topLeft = cv::Point(boxCenterX - thicknessBoxWidth/2, boxCenterY - thicknessBoxHeight/2);
                    topRight = cv::Point(boxCenterX + thicknessBoxWidth/2, boxCenterY - thicknessBoxHeight/2);
                    bottomLeft = cv::Point(boxCenterX - thicknessBoxWidth/2, boxCenterY + thicknessBoxHeight/2);
                    bottomRight = cv::Point(boxCenterX + thicknessBoxWidth/2, boxCenterY + thicknessBoxHeight/2);
                } else {
                    auto rotatePoint = [&](int localX, int localY) -> cv::Point {
                        int rotatedX = boxCenterX + static_cast<int>(localX * cosA - localY * sinA);
                        int rotatedY = boxCenterY + static_cast<int>(localX * sinA + localY * cosA);
                        return cv::Point(rotatedX, rotatedY);
                    };
                    
                    topLeft = rotatePoint(-thicknessBoxWidth/2, -thicknessBoxHeight/2);
                    topRight = rotatePoint(thicknessBoxWidth/2, -thicknessBoxHeight/2);
                    bottomLeft = rotatePoint(-thicknessBoxWidth/2, thicknessBoxHeight/2);
                    bottomRight = rotatePoint(thicknessBoxWidth/2, thicknessBoxHeight/2);
                }
                
                drawDottedLine(topLeft, topRight, boxColor);
                drawDottedLine(bottomLeft, bottomRight, boxColor);
                drawDottedLine(topLeft, bottomLeft, boxColor);
                drawDottedLine(topRight, bottomRight, boxColor);
            }
        }
        
        // ===== REAR 두께 측정 (END 지점 - 80%) =====
        std::cout << "\n=== REAR 두께 측정 시작 (END 지점 80%) ===\n";
        
        // REAR 구간에서 사용할 변수들 재정의 (FRONT 구간과 같은 값들)
        cv::Point boundRectCenter_rear(boundRect.x + boundRect.width / 2, 
                                      boundRect.y + boundRect.height / 2);
        int boundRectWidth_rear = boundRect.width;
        double cosA_rear = cos(angleRad);
        double sinA_rear = sin(angleRad);
        
        // END 지점(80%) 기준으로 두께 측정 위치 계산
        float endPercent = gradientEndPercent / 100.0f;
        
        // gradient 끝점까지의 거리 (boundRect 왼쪽 끝에서 endPercent만큼)
        float gradientEndX_rear = boundRect.x + (endPercent * boundRectWidth_rear);
        
        // 각도 적용한 실제 gradient 끝점 중앙 계산 (boundRect 중심 기준으로 회전)
        float localX_rear = gradientEndX_rear - boundRectCenter_rear.x;  // boundRect 중심 기준 상대 좌표
        float localY_rear = 0;  // Y는 boundRect 중심선 기준
        
        int boxCenterX_rear = boundRectCenter_rear.x + static_cast<int>(localX_rear * cos(angleRad) - localY_rear * sin(angleRad));
        int boxCenterY_rear = boundRectCenter_rear.y + static_cast<int>(localX_rear * sin(angleRad) + localY_rear * cos(angleRad));
        
        std::cout << "REAR 두께 측정 위치: (" << boxCenterX_rear << ", " << boxCenterY_rear << ")" << std::endl;
        std::cout << "패턴 각도: " << angle << "도, endPercent: " << gradientEndPercent << "%" << std::endl;
        
        std::vector<int> thicknesses_rear;
        std::vector<cv::Point> measurementLines_rear; // 측정 라인 저장
        std::vector<cv::Point> blackPixelPoints_rear; // 검은색 픽셀 위치 저장 (시각화용)
        
        // 박스 영역에서 세로 방향으로 스캔하여 검은색 픽셀 두께 측정 (REAR)
        for (int dx = -rearThicknessBoxWidth/2; dx <= rearThicknessBoxWidth/2; dx += 3) { // 3픽셀 간격으로 스캔
            // 각도를 적용한 세로 스캔 라인 계산
            cv::Point scanTop_rear, scanBottom_rear;
            
            if (std::abs(angle) < 0.1) {
                // 각도가 거의 없는 경우 - 직선 스캔
                scanTop_rear = cv::Point(boxCenterX_rear + dx, boxCenterY_rear - rearThicknessBoxHeight/2);
                scanBottom_rear = cv::Point(boxCenterX_rear + dx, boxCenterY_rear + rearThicknessBoxHeight/2);
            } else {
                // 각도가 있는 경우 - 회전된 스캔 라인
                int localX = dx;
                int localTopY = -rearThicknessBoxHeight/2;
                int localBottomY = rearThicknessBoxHeight/2;
                
                // 각도 적용하여 회전
                scanTop_rear.x = boxCenterX_rear + static_cast<int>(localX * cosA_rear - localTopY * sinA_rear);
                scanTop_rear.y = boxCenterY_rear + static_cast<int>(localX * sinA_rear + localTopY * cosA_rear);
                
                scanBottom_rear.x = boxCenterX_rear + static_cast<int>(localX * cosA_rear - localBottomY * sinA_rear);
                scanBottom_rear.y = boxCenterY_rear + static_cast<int>(localX * sinA_rear + localBottomY * cosA_rear);
            }
            
            // 스캔 라인이 이미지 범위를 벗어나면 건너뛰기
            if (scanTop_rear.x < 0 || scanTop_rear.y < 0 || scanBottom_rear.x < 0 || scanBottom_rear.y < 0 ||
                scanTop_rear.x >= cleanOriginal.cols || scanTop_rear.y >= cleanOriginal.rows ||
                scanBottom_rear.x >= cleanOriginal.cols || scanBottom_rear.y >= cleanOriginal.rows) {
                continue;
            }
            
            // 세로 라인을 따라 검은색 픽셀 구간 찾기
            cv::LineIterator it(cleanOriginal, scanTop_rear, scanBottom_rear, 8);
            std::vector<std::pair<int, int>> blackRegions_rear; // 검은색 구간의 시작과 끝
            int regionStart = -1;
            bool inBlackRegion = false;
            
            // 라인 상의 모든 점들 저장 (시각화용)
            std::vector<cv::Point> linePoints_rear;
            for (int i = 0; i < it.count; i++, ++it) {
                linePoints_rear.push_back(it.pos());
            }
            
            // 다시 처음부터 스캔하여 검은색 픽셀 찾기
            it = cv::LineIterator(cleanOriginal, scanTop_rear, scanBottom_rear, 8);
            for (int i = 0; i < it.count; i++, ++it) {
                cv::Vec3b pixel = cleanOriginal.at<cv::Vec3b>(it.pos());
                // 검은색 픽셀 판단 (RGB 값이 모두 낮은 경우)
                bool isBlack = (pixel[0] < 50 && pixel[1] < 50 && pixel[2] < 50);
                
                if (isBlack && !inBlackRegion) {
                    // 검은색 구간 시작
                    regionStart = i;
                    inBlackRegion = true;
                } else if (!isBlack && inBlackRegion) {
                    // 검은색 구간 끝
                    int thickness = i - regionStart;
                    if (thickness >= 3) { // 최소 3픽셀 이상만 유효한 두께로 인정
                        thicknesses_rear.push_back(thickness);
                        blackRegions_rear.push_back({regionStart, i});
                        
                        // 검은색 구간의 픽셀들을 시각화용으로 저장
                        for (int j = regionStart; j < i; j++) {
                            if (j < linePoints_rear.size()) {
                                blackPixelPoints_rear.push_back(linePoints_rear[j]);
                            }
                        }
                    }
                    inBlackRegion = false;
                }
            }
            
            // 라인 끝에서 검은색 구간이 계속되는 경우
            if (inBlackRegion && regionStart >= 0) {
                int thickness = it.count - regionStart;
                if (thickness >= 3) {
                    thicknesses_rear.push_back(thickness);
                    blackRegions_rear.push_back({regionStart, it.count});
                    
                    // 검은색 구간의 픽셀들을 시각화용으로 저장
                    for (int j = regionStart; j < it.count; j++) {
                        if (j < linePoints_rear.size()) {
                            blackPixelPoints_rear.push_back(linePoints_rear[j]);
                        }
                    }
                }
            }
            
            // 검은색 구간이 발견된 경우 측정 라인 저장
            if (!blackRegions_rear.empty()) {
                measurementLines_rear.push_back(scanTop_rear);
                measurementLines_rear.push_back(scanBottom_rear);
            }
        }
        
        // REAR 두께 측정 결과 처리
        if (!thicknesses_rear.empty()) {
            // 최소, 최대, 평균 두께 계산
            int minThickness_rear = *std::min_element(thicknesses_rear.begin(), thicknesses_rear.end());
            int maxThickness_rear = *std::max_element(thicknesses_rear.begin(), thicknesses_rear.end());
            int avgThickness_rear = std::accumulate(thicknesses_rear.begin(), thicknesses_rear.end(), 0) / thicknesses_rear.size();
            
            // 측정 결과를 매개변수에 저장 (REAR)
            if (rearMeasuredMinThickness) *rearMeasuredMinThickness = minThickness_rear;
            if (rearMeasuredMaxThickness) *rearMeasuredMaxThickness = maxThickness_rear;
            if (rearMeasuredAvgThickness) *rearMeasuredAvgThickness = avgThickness_rear;
            
            // 두께 판정 (REAR)
            bool thicknessPassed_rear = (avgThickness_rear >= rearThicknessMin && avgThickness_rear <= rearThicknessMax);
            
            // 최종 판정에 REAR 두께 조건도 추가
            isPassed = isPassed && thicknessPassed_rear;
            
            std::cout << "=== REAR 두께 측정 검사 (검은색 픽셀 기반) ===" << std::endl;
            std::cout << "가로 검사 범위: " << rearThicknessBoxWidth << "px (스캔라인 개수: " << (rearThicknessBoxWidth/3 + 1) << "개)" << std::endl;
            std::cout << "측정된 두께 - 최소: " << minThickness_rear << "px, 최대: " << maxThickness_rear << "px, 평균: " << avgThickness_rear << "px" << std::endl;
            std::cout << "허용 범위: " << rearThicknessMin << " ~ " << rearThicknessMax << " px" << std::endl;
            std::cout << "REAR 두께 판정: " << (thicknessPassed_rear ? "PASS" : "FAIL") << std::endl;
            
            // 측정 위치에 빨간색 선 그리기 (FRONT와 동일한 색상)
            for (size_t i = 0; i < measurementLines_rear.size(); i += 2) {
                cv::line(resultImage, measurementLines_rear[i], measurementLines_rear[i+1], cv::Scalar(0, 0, 255), 1); // 빨간색 선
            }
            
            // 검출된 검은색 픽셀들을 초록색으로 표시 (FRONT와 동일한 색상)
            for (const cv::Point& blackPixel : blackPixelPoints_rear) {
                if (blackPixel.x >= 0 && blackPixel.y >= 0 && 
                    blackPixel.x < resultImage.cols && blackPixel.y < resultImage.rows) {
                    cv::Vec3b& pixel = resultImage.at<cv::Vec3b>(blackPixel);
                    pixel[0] = 0;   // B
                    pixel[1] = 255; // G
                    pixel[2] = 0;   // R - 순수 초록색으로 표시 (FRONT와 동일)
                }
            }
            
            // REAR 두께 측정 박스를 점선으로 그리기
            cv::Scalar boxColor_rear = thicknessPassed_rear ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255); // 통과시 초록, 실패시 빨강
            
            // 점선 그리기 함수
            auto drawDottedLine_rear = [&](cv::Point pt1, cv::Point pt2, cv::Scalar color, int thickness = 2) {
                cv::LineIterator it(resultImage, pt1, pt2, 8);
                for (int i = 0; i < it.count; i++, ++it) {
                    if (i % 6 < 3) { // 3픽셀 그리고 3픽셀 건너뛰기
                        if (it.pos().x >= 0 && it.pos().y >= 0 && 
                            it.pos().x < resultImage.cols && it.pos().y < resultImage.rows) {
                            cv::Vec3b& pixel = resultImage.at<cv::Vec3b>(it.pos());
                            pixel[0] = color[0];
                            pixel[1] = color[1]; 
                            pixel[2] = color[2];
                        }
                    }
                }
            };
            
            // 박스 꼭짓점 계산 (각도 적용)
            cv::Point topLeft_rear, topRight_rear, bottomLeft_rear, bottomRight_rear;
            
            if (std::abs(angle) < 0.1) {
                // 각도가 거의 없는 경우
                topLeft_rear = cv::Point(boxCenterX_rear - rearThicknessBoxWidth/2, boxCenterY_rear - rearThicknessBoxHeight/2);
                topRight_rear = cv::Point(boxCenterX_rear + rearThicknessBoxWidth/2, boxCenterY_rear - rearThicknessBoxHeight/2);
                bottomLeft_rear = cv::Point(boxCenterX_rear - rearThicknessBoxWidth/2, boxCenterY_rear + rearThicknessBoxHeight/2);
                bottomRight_rear = cv::Point(boxCenterX_rear + rearThicknessBoxWidth/2, boxCenterY_rear + rearThicknessBoxHeight/2);
            } else {
                // 각도 적용한 박스 꼭짓점
                auto rotatePoint_rear = [&](int localX, int localY) -> cv::Point {
                    int rotatedX = boxCenterX_rear + static_cast<int>(localX * cosA_rear - localY * sinA_rear);
                    int rotatedY = boxCenterY_rear + static_cast<int>(localX * sinA_rear + localY * cosA_rear);
                    return cv::Point(rotatedX, rotatedY);
                };
                
                topLeft_rear = rotatePoint_rear(-rearThicknessBoxWidth/2, -rearThicknessBoxHeight/2);
                topRight_rear = rotatePoint_rear(rearThicknessBoxWidth/2, -rearThicknessBoxHeight/2);
                bottomLeft_rear = rotatePoint_rear(-rearThicknessBoxWidth/2, rearThicknessBoxHeight/2);
                bottomRight_rear = rotatePoint_rear(rearThicknessBoxWidth/2, rearThicknessBoxHeight/2);
            }
            
            // REAR 두께 측정 박스 그리기 (점선)
            drawDottedLine_rear(topLeft_rear, topRight_rear, boxColor_rear);       // 상단
            drawDottedLine_rear(bottomLeft_rear, bottomRight_rear, boxColor_rear); // 하단
            drawDottedLine_rear(topLeft_rear, bottomLeft_rear, boxColor_rear);     // 좌측
            drawDottedLine_rear(topRight_rear, bottomRight_rear, boxColor_rear);   // 우측
            
            // REAR 박스 위치 저장 (Qt 텍스트 그리기용)
            if (rearBoxTopLeft) {
                *rearBoxTopLeft = topLeft_rear;
            }
                       
        } else {
            std::cout << "=== REAR 두께 측정 검사 ===" << std::endl;
            std::cout << "REAR에서 검은색 픽셀을 찾을 수 없어서 두께 측정 실패" << std::endl;
            isPassed = false;
            
            // REAR 측정 실패 시에도 박스와 텍스트 그리기
            cv::Scalar boxColor_rear = cv::Scalar(0, 0, 255); // 빨간색
            
            auto drawDottedLine_rear = [&](cv::Point pt1, cv::Point pt2, cv::Scalar color, int thickness = 2) {
                cv::LineIterator it(resultImage, pt1, pt2, 8);
                for (int i = 0; i < it.count; i++, ++it) {
                    if (i % 6 < 3) {
                        if (it.pos().x >= 0 && it.pos().y >= 0 && 
                            it.pos().x < resultImage.cols && it.pos().y < resultImage.rows) {
                            cv::Vec3b& pixel = resultImage.at<cv::Vec3b>(it.pos());
                            pixel[0] = color[0];
                            pixel[1] = color[1]; 
                            pixel[2] = color[2];
                        }
                    }
                }
            };
            
            cv::Point topLeft_rear, topRight_rear, bottomLeft_rear, bottomRight_rear;
            
            if (std::abs(angle) < 0.1) {
                topLeft_rear = cv::Point(boxCenterX_rear - rearThicknessBoxWidth/2, boxCenterY_rear - rearThicknessBoxHeight/2);
                topRight_rear = cv::Point(boxCenterX_rear + rearThicknessBoxWidth/2, boxCenterY_rear - rearThicknessBoxHeight/2);
                bottomLeft_rear = cv::Point(boxCenterX_rear - rearThicknessBoxWidth/2, boxCenterY_rear + rearThicknessBoxHeight/2);
                bottomRight_rear = cv::Point(boxCenterX_rear + rearThicknessBoxWidth/2, boxCenterY_rear + rearThicknessBoxHeight/2);
            } else {
                auto rotatePoint_rear = [&](int localX, int localY) -> cv::Point {
                    int rotatedX = boxCenterX_rear + static_cast<int>(localX * cosA_rear - localY * sinA_rear);
                    int rotatedY = boxCenterY_rear + static_cast<int>(localX * sinA_rear + localY * cosA_rear);
                    return cv::Point(rotatedX, rotatedY);
                };
                
                topLeft_rear = rotatePoint_rear(-rearThicknessBoxWidth/2, -rearThicknessBoxHeight/2);
                topRight_rear = rotatePoint_rear(rearThicknessBoxWidth/2, -rearThicknessBoxHeight/2);
                bottomLeft_rear = rotatePoint_rear(-rearThicknessBoxWidth/2, rearThicknessBoxHeight/2);
                bottomRight_rear = rotatePoint_rear(rearThicknessBoxWidth/2, rearThicknessBoxHeight/2);
            }
            
            drawDottedLine_rear(topLeft_rear, topRight_rear, boxColor_rear);
            drawDottedLine_rear(bottomLeft_rear, bottomRight_rear, boxColor_rear);
            drawDottedLine_rear(topLeft_rear, bottomLeft_rear, boxColor_rear);
            drawDottedLine_rear(topRight_rear, bottomRight_rear, boxColor_rear);
            
            // REAR 실패 시에도 박스 위치 저장 (Qt 텍스트 그리기용)
            if (rearBoxTopLeft) {
                *rearBoxTopLeft = topLeft_rear;
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