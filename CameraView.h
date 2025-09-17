#ifndef CAMERAVIEW_H
#define CAMERAVIEW_H

#include <QLabel>
#include <QMouseEvent>
#include <QList>
#include <QRect>
#include <QPaintEvent>
#include <QColor>
#include <QPoint>
#include <QUuid>
#include <QMap>
#include <QVector>
#include <QCheckBox>
#include <QSlider>
#include <QComboBox>
#include <QDebug>
#include "CommonDefs.h"
#include "ImageProcessor.h"
#include "LanguageManager.h"

#ifndef TR
#define TR(key) LanguageManager::instance()->getText(key)
#endif

// QLabel을 상속받는 CameraView 클래스
class CameraView : public QLabel {
    Q_OBJECT
    
public slots:
    void updateUITexts();

public:
    void setInspectionMode(bool enabled) {
        isInspectionMode = enabled;
        if (!enabled) hasInspectionResult = false;
        QWidget::update();
    }
    
    void updateInspectionResult(bool passed, const InspectionResult& result);
    bool getInspectionMode() const { return isInspectionMode; }
    
    // 검사 결과 필터링
    void setSelectedInspectionPatternId(const QUuid& id) {
        selectedInspectionPatternId = id;
        QWidget::update();
    }
    
    void clearSelectedInspectionPattern() {
        selectedInspectionPatternId = QUuid();
        QWidget::update();
    }
    
    QUuid getSelectedInspectionPatternId() const {
        return selectedInspectionPatternId;
    }

    enum EditMode {View, Move, Draw, Edit};

    void setPatternContours(const QUuid& patternId, const QList<QVector<QPoint>>& contours);
    
    // 캘리브레이션 관련 함수들 추가
    void setCalibrationMode(bool enabled) {
        m_calibrationMode = enabled;
        if (enabled) {
            m_prevEditMode = m_editMode;
            setEditMode(EditMode::Draw);
            setCursor(Qt::CrossCursor);
        } else {
            setEditMode(m_prevEditMode);
        }
    }
    
    bool isCalibrationMode() const { return m_calibrationMode; }
    
    void setCalibrationInfo(const CalibrationInfo& info) { 
        m_calibrationInfo = info; 
        update(); 
    }
    
    const CalibrationInfo& getCalibrationInfo() const { return m_calibrationInfo; }
    
    void setMeasurementInfo(const QString& text) {
        m_measurementText = text;
        update();
    }
    
    // 물리적 길이 계산 함수 (mm 단위)
    double calculatePhysicalLength(int pixelLength) const {
        if (m_calibrationInfo.isCalibrated && m_calibrationInfo.pixelToMmRatio > 0.0) {
            return pixelLength * m_calibrationInfo.pixelToMmRatio;
        }
        return 0.0;
    }

    void setEditMode(EditMode mode) { 
        // 모드가 실제로 변경될 때만 상태 초기화
        if (m_editMode != mode) {
            m_editMode = mode; 
            
            // 모드 전환 시에만 상태 초기화
            isDrawing = false;
            isDragging = false;
            isResizing = false;
            isRotating = false;
            activeHandle = ResizeHandle::None;
            currentRect = QRect(); // 그리기 사각형도 초기화
            
            // 커서 설정
            setCursor(m_editMode == EditMode::Draw ? Qt::CrossCursor : Qt::ArrowCursor);
            update();
        }
    }

    EditMode getEditMode() const { return m_editMode; }
    
    // 원본 배경 이미지 가져오기 (패턴이 그려지기 전)
    QPixmap getBackgroundPixmap() const;
    
    // 리사이즈 핸들 위치 열거형
    enum class ResizeHandle {
        None,
        TopLeft, TopRight, BottomLeft, BottomRight,
        Top, Left, Bottom, Right
    };
    
    QList<PatternInfo>& getPatterns() { return patterns; }
    CameraView(QWidget *parent = nullptr);
    void setStatusInfo(const QString& info) { statusInfo = info; update(); }
    void setCurrentDrawColor(const QColor& color) { currentDrawColor = color; }
    bool hasValidScaling() const { return scaleX != 0.0 && scaleY != 0.0; }
    bool isSameScaling(double newScaleX, double newScaleY) const {
        return fabs(scaleX - newScaleX) < 0.001 && fabs(scaleY - newScaleY) < 0.001;
    }
    void setScaling(double newScaleX, double newScaleY) {
        scaleX = newScaleX;
        scaleY = newScaleY;
    }

    void setCurrentCameraUuid(const QString& uuid) {
        currentCameraUuid = uuid;
        update(); // UI 갱신
    }
    QString getCurrentCameraUuid() const {
        return currentCameraUuid;
    }
    
    void setCurrentCameraName(const QString& name) {
        currentCameraName = name;
        update(); // UI 갱신
    }
    QString getCurrentCameraName() const {
        return currentCameraName;
    }
    
    // 모든 패턴의 cameraUuid를 현재 카메라로 업데이트 (레시피 로드 후 호출)
    void updateAllPatternsCameraUuid() {
        if (!currentCameraUuid.isEmpty()) {
            for (PatternInfo& pattern : patterns) {
                pattern.cameraUuid = currentCameraUuid;
                // 레시피 로드 후 모든 패턴을 활성화
                pattern.enabled = true;
            }
            update();
        }
    }
    

    bool updatePatternById(const QUuid& id, const PatternInfo& pattern);
   
    // UUID 기반 API로만 제공
    QUuid addPattern(const PatternInfo& pattern);
    void removePattern(const QUuid& id);
    void clearPatterns();
    
    void setSelectedPatternId(const QUuid& id);
    void updatePatternRect(const QUuid& id, const QRectF& rect);
    
    // 선택된 패턴 정보 접근 메서드
    QUuid getSelectedPatternId() const { return selectedPatternId; }
    int getSelectedPatternIndex() const;
    const QList<PatternInfo>& getPatterns() const { return patterns; }
    PatternInfo* getPatternById(const QUuid& id);
    const PatternInfo* getPatternById(const QUuid& id) const;
    void updateFidTemplateImage(const QUuid& patternId, const QImage& templateImage);

    // 필터 관련 메서드들 (UUID 기반으로 통일)
    void addPatternFilter(const QUuid& patternId, int filterType);
    void removePatternFilter(const QUuid& patternId, int filterIndex);
    void setPatternFilterEnabled(const QUuid& patternId, int filterIndex, bool enabled);
    void setPatternFilterParam(const QUuid& patternId, int filterIndex, const QString& paramName, int value);
    void movePatternFilterUp(const QUuid& patternId, int filterIndex);
    void movePatternFilterDown(const QUuid& patternId, int filterIndex);
    const QList<FilterInfo>& getPatternFilters(const QUuid& patternId) const;
    
    // 배경 이미지 및 줌/패닝 관련
    void setBackgroundPixmap(const QPixmap &pixmap);
    QPoint getPanOffset() const { return panOffset; }
    double getZoomFactor() const { return zoomFactor; }
    void setPanOffset(const QPoint& offset) { panOffset = offset; update(); }
    void setZoomFactor(double factor) { zoomFactor = factor; update(); }
    
    // 사각형 그리기 함수
    void setCurrentRect(const QRect& rect) { currentRect = rect; update(); }
    QRect getCurrentRect() const { return currentRect; }
    void clearCurrentRect() { currentRect = QRect(); update(); }
    
    // 리사이즈 핸들 관련 함수 선언 추가
    QVector<QPoint> getRotatedCorners() const;
    QVector<QPoint> getRotatedCornersForPattern(const PatternInfo& pattern) const;
    int getCornerHandleAt(const QPoint& pos) const;
    QRect rotateHandleRect() const;

    // 스케일링 관련 메서드
    void setScalingInfo(const QSize& origSize, const QSize& displaySize);
    QPoint displayToOriginal(const QPoint& displayPos);
    QPoint originalToDisplay(const QPoint& originalPos);
    QPoint originalToDisplay(const QPoint& originalPos) const;
    QPoint getRotatedCenter() const;
    int getRotateHandleAt(const QPoint& pos) const;
    
    // 필터 처리 함수들 - ImageProcessor 프록시
    void applyFiltersToImage(cv::Mat& image);
    void applyThresholdFilter(cv::Mat& src, cv::Mat& dst, int threshold) {
        ImageProcessor::applyThresholdFilter(src, dst, threshold);
    }
    void applyBlurFilter(cv::Mat& src, cv::Mat& dst, int kernelSize) {
        ImageProcessor::applyBlurFilter(src, dst, kernelSize);
    }
    void applyCannyFilter(cv::Mat& src, cv::Mat& dst, int threshold1, int threshold2) {
        ImageProcessor::applyCannyFilter(src, dst, threshold1, threshold2);
    }
    void applySobelFilter(cv::Mat& src, cv::Mat& dst, int kernelSize) {
        ImageProcessor::applySobelFilter(src, dst, kernelSize);
    }
    void applyLaplacianFilter(cv::Mat& src, cv::Mat& dst, int kernelSize) {
        ImageProcessor::applyLaplacianFilter(src, dst, kernelSize);
    }
    void applySharpenFilter(cv::Mat& src, cv::Mat& dst, int strength) {
        ImageProcessor::applySharpenFilter(src, dst, strength);
    }
    void applyBrightnessFilter(cv::Mat& src, cv::Mat& dst, int value) {
        ImageProcessor::applyBrightnessFilter(src, dst, value);
    }
    void applyContrastFilter(cv::Mat& src, cv::Mat& dst, int value) {
        ImageProcessor::applyContrastFilter(src, dst, value);
    }
    
signals:
    // UUID 기반 시그널
    void enterKeyPressed(const QRect& rect); 
    void rectDrawn(const QRect& rect);
    void patternSelected(const QUuid& id);
    void patternRectChanged(const QUuid& id, const QRect& rect);
    void patternAngleChanged(const QUuid& id, double angle);
    void patternAdded(const QUuid& id);
    void patternRemoved(const QUuid& id);
    void patternsGrouped(); 
    void requestRemovePattern(const QUuid& patternId);
    void requestAddFilter(const QUuid& patternId);
    void patternNameChanged(const QUuid& patternId, const QString& newName);
    void patternEnableStateChanged(const QUuid& patternId, bool enabled);
    void fidTemplateUpdateRequired(const QUuid& patternId);
    void insTemplateUpdateRequired(const QUuid& patternId);
    void calibrationRectDrawn(const QRect& rect);
    void selectedInspectionPatternCleared(); // 검사 결과 필터 해제 시그널

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    
private:
    bool isInspectionMode = false;
    bool hasInspectionResult = false;
    bool lastInspectionPassed = false;

    InspectionResult lastInspectionResult;
    QUuid selectedInspectionPatternId; // 선택된 검사 결과 패턴 필터링

    // 리사이즈/회전 관련 변수
    bool isResizing = false;
    bool isRotating = false;
    int activeHandleIdx = -1;
    QPoint fixedScreenPos;
    QPoint rotateStartPos;
    QPoint rotationCenter;  // 회전 중심점 고정용
    double initialAngle = 0.0;

    // 언어 지원을 위한 텍스트 요소들
    QString m_calibrationText;             // 캘리브레이션 텍스트
    QString m_statusText;                  // 상태 텍스트
    QMap<QUuid, QString> groupNames;       // 그룹 이름 맵핑

    // 패턴별 윤곽선 저장
    QMap<QUuid, QList<QVector<QPoint>>> patternContours;

    // 캘리브레이션 관련 변수
    bool m_calibrationMode = false;
    EditMode m_prevEditMode = EditMode::Move;
    CalibrationInfo m_calibrationInfo;
    QString m_measurementText;

    QString statusInfo;
    EditMode m_editMode = EditMode::Move; // 기본값은 이동 모드
    QString currentCameraUuid;  // 현재 카메라 UUID
    QString currentCameraName;  // 현재 카메라 이름 (표시용)
    QColor currentDrawColor = Qt::green;
    // 줌/패닝 관련
    double zoomFactor = 1.0;
    QPoint zoomCenter;
    bool isZooming = false;
    QPoint panOffset = QPoint(0, 0);
    QPoint panStartPos;
    QPoint panStartOffset;
    bool isPanning = false;
    double scaleX = 1.0, scaleY = 1.0;
    QSize originalImageSize;
    QPixmap applyZoom(const QPixmap& original);
    void updateZoomedView();
    
    // 패턴 관련
    QList<PatternInfo> patterns;
    QRect currentRect;
    QPoint startPoint;
    QPoint dragEndPoint;
    bool isDrawing = false;
    bool isDragging = false;
    QUuid selectedPatternId;
    QPoint dragOffset;
    ResizeHandle activeHandle = ResizeHandle::None;
    int resizeHandleSize = 8;
    QPixmap backgroundPixmap;
    bool m_inspectionMode = false;
    QVector<bool> m_patternResults;

    void drawInspectionResultsVector(QPainter& painter, const InspectionResult& result);
    
    // 그룹 바운딩 박스 그리기 함수
    void drawGroupBoundingBox(QPainter& painter, const QList<PatternInfo>& groupPatterns);

    // 패턴 그룹화 관련 함수
    QList<QUuid> findPatternsInSelection() const;
    void showContextMenu(const QPoint& pos);
    void groupPatternsInSelection(const QList<QUuid>& patternIds);
    void ungroupPatternsInSelection(const QList<QUuid>& patternIds);
   
    // 히트 테스트 및 리사이즈 헬퍼 함수
    QUuid hitTest(const QPoint& pos);
    ResizeHandle getResizeHandle(const QPoint& pos, const QUuid& patternId);
    QCursor getResizeCursor(ResizeHandle handle);
    QRect getResizedRect(const QRect& rect, const QPoint& pos, ResizeHandle handle);
    void drawResizeHandles(QPainter& painter, const QRect& rect);
    
    void updateUIFromFilters();
    void connectFilterSignals(int filterType, const QString& paramName, QSlider* slider, QLabel* valueLabel);
    void connectFilterComboSignals(int filterType, const QString& paramName, QComboBox* combo);
    void updateFilterParam(int filterType, const QString& paramName, int value);
    QUuid getPatternId(int index) const;
    
    // 필터 관련 데이터
    QVector<int> filterTypes;
    QMap<int, QString> filterNames;
    QMap<int, QCheckBox*> filterCheckboxes;
    QMap<int, QMap<QString, QSlider*>> filterSliders;
    QMap<int, QMap<QString, QLabel*>> filterValueLabels;
    QMap<int, QMap<QString, QComboBox*>> filterCombos;
    QMap<int, FilterInfo> appliedFilters;
    QMap<int, QMap<QString, int>> defaultParams;
};

#endif // CAMERAVIEW_H