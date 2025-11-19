#ifndef INSPPROCESSOR_H
#define INSPPROCESSOR_H

#include "CommonDefs.h"
#include <QObject>

class InsProcessor : public QObject {
    Q_OBJECT
    
public:
    InsProcessor(QObject* parent = nullptr);
    ~InsProcessor();
  
    InspectionResult performInspection(const cv::Mat& image, const QList<PatternInfo>& patterns, int stripCrimpMode = 0);
    static QImage matToQImage(const cv::Mat& mat);   
 
    // FID 패턴 매칭 기능
    bool matchFiducial(const cv::Mat& image, const PatternInfo& pattern, double& score, 
        cv::Point& matchLoc, double& matchAngle, const QList<PatternInfo>& allPatterns);
    
    // INS 검사 기능
    bool checkColor(const cv::Mat& image, const PatternInfo& pattern, double& score, InspectionResult& result);
    bool checkEdge(const cv::Mat& image, const PatternInfo& pattern, double& score, InspectionResult& result);
    bool checkBinary(const cv::Mat& image, const PatternInfo& pattern, double& score, InspectionResult& result);
    bool checkStrip(const cv::Mat& image, const PatternInfo& pattern, double& score, InspectionResult& result, const QList<PatternInfo>& patterns);
    bool checkCrimp(const cv::Mat& image, const PatternInfo& pattern, double& score, InspectionResult& result);

    // ROI 추출 함수 (패턴 위치에서 영역 가져오기)
    cv::Mat extractROI(const cv::Mat& image, const QRectF& rect, double angle = 0.0, bool isTemplate = false);
    
    // INS 패턴 내부 좌표점들을 역회전시켜 고정 위치로 변환하는 유틸리티 함수
    static QList<QPoint> transformPatternPoints(const std::vector<cv::Point>& roiPoints, 
                                               const cv::Size& roiSize, 
                                               double patternAngle,
                                               const cv::Point2f& offset);

signals:
    void logMessage(const QString& message);

private:
    void logDebug(const QString& message);
    
    // 회전된 바운딩 박스 추출 함수들
    cv::Mat extractRotatedBoundingBoxForTemplate(const cv::Mat& image, const QRectF& rect, double angle);
    cv::Mat extractRotatedBoundingBoxForInspection(const cv::Mat& image, const QRectF& rect, double angle);
    
    // 내부 헬퍼 함수들
    bool performTemplateMatching(const cv::Mat& image, const cv::Mat& templ, 
                                cv::Point& matchLoc, double& score, double& angle,
                                const PatternInfo& pattern,
                                double minAngle = 0, double maxAngle = 0, double angleStep = 1);
    
    bool performFeatureMatching(const cv::Mat& image, const cv::Mat& templ, 
                               cv::Point& matchLoc, double& score, double& angle);
    
};

#endif