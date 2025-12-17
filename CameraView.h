#ifndef CAMERAVIEW_H
#define CAMERAVIEW_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
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
#include <array>
#include "CommonDefs.h"
#include "ImageProcessor.h"
#include "LanguageManager.h"

#ifndef TR
#define TR(key) LanguageManager::instance()->getText(key)
#endif

// QGraphicsView를 상속받는 CameraView 클래스
class CameraView : public QGraphicsView
{
    Q_OBJECT

public slots:
    void updateUITexts();

public:
    void setInspectionMode(bool enabled)
    {
        isInspectionMode = enabled;
        if (!enabled)
            hasInspectionResult = false;
        viewport()->update();
    }

    void updateInspectionResult(bool passed, const InspectionResult &result);
    bool getInspectionMode() const { return isInspectionMode; }

    // Strip/Crimp 모드 설정/획득
    void setStripCrimpMode(int mode) { currentStripCrimpMode = mode; }
    int getStripCrimpMode() const { return currentStripCrimpMode; }

    // 검사 결과 필터링
    void setSelectedInspectionPatternId(const QUuid &id)
    {
        selectedInspectionPatternId = id;
        QWidget::update();
    }

    void clearSelectedInspectionPattern()
    {
        selectedInspectionPatternId = QUuid();
        viewport()->update();
    }

    QUuid getSelectedInspectionPatternId() const
    {
        return selectedInspectionPatternId;
    }

    enum EditMode
    {
        View,
        Move,
        Draw,
        Edit
    };

    void setPatternContours(const QUuid &patternId, const QList<QVector<QPoint>> &contours);

    void setEditMode(EditMode mode)
    {
        // 모드가 실제로 변경될 때만 상태 초기화
        if (m_editMode != mode)
        {
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
            viewport()->update();
        }
    }

    EditMode getEditMode() const { return m_editMode; }

    // 원본 배경 이미지 가져오기 (패턴이 그려지기 전)
    QPixmap getBackgroundPixmap() const;

    // 리사이즈 핸들 위치 열거형
    enum class ResizeHandle
    {
        None,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
        Top,
        Left,
        Bottom,
        Right
    };

    QList<PatternInfo> &getPatterns() { return patterns; }
    CameraView(QWidget *parent = nullptr);
    void setStatusInfo(const QString &info)
    {
        statusInfo = info;
        viewport()->update();
    }
    void setCurrentDrawColor(const QColor &color) { currentDrawColor = color; }
    bool hasValidScaling() const { return scaleX != 0.0 && scaleY != 0.0; }
    bool isSameScaling(double newScaleX, double newScaleY) const
    {
        return fabs(scaleX - newScaleX) < 0.001 && fabs(scaleY - newScaleY) < 0.001;
    }
    void setScaling(double newScaleX, double newScaleY)
    {
        scaleX = newScaleX;
        scaleY = newScaleY;
    }

    void setCurrentCameraUuid(const QString &uuid)
    {
        currentCameraUuid = uuid;
        viewport()->update(); // UI 갱신
    }
    QString getCurrentCameraUuid() const
    {
        return currentCameraUuid;
    }
    
    void setCurrentFrameIndex(int frameIdx)
    {
        currentFrameIndex = frameIdx;
        qDebug() << "[CameraView] currentFrameIndex 업데이트:" << currentFrameIndex;
    }
    int getCurrentFrameIndex() const
    {
        return currentFrameIndex;
    }

    void setCurrentCameraName(const QString &name)
    {
        currentCameraName = name;
        viewport()->update(); // UI 갱신
    }
    QString getCurrentCameraName() const
    {
        return currentCameraName;
    }

    // 모든 패턴의 cameraUuid를 현재 카메라로 업데이트 (레시피 로드 후 호출)
    void updateAllPatternsCameraUuid()
    {
        if (!currentCameraUuid.isEmpty())
        {
            for (PatternInfo &pattern : patterns)
            {
                pattern.cameraUuid = currentCameraUuid;
                // 레시피 로드 후 모든 패턴을 활성화
                pattern.enabled = true;
            }
            viewport()->update();
        }
    }

    bool updatePatternById(const QUuid &id, const PatternInfo &pattern);

    // UUID 기반 API로만 제공
    QUuid addPattern(const PatternInfo &pattern);
    void removePattern(const QUuid &id);
    void clearPatterns();

    void setSelectedPatternId(const QUuid &id);
    void updatePatternRect(const QUuid &id, const QRectF &rect);

    // 선택된 패턴 정보 접근 메서드
    QUuid getSelectedPatternId() const { return selectedPatternId; }
    int getSelectedPatternIndex() const;
    const QList<PatternInfo> &getPatterns() const { return patterns; }
    PatternInfo *getPatternById(const QUuid &id);
    const PatternInfo *getPatternById(const QUuid &id) const;
    void updateFidTemplateImage(const QUuid &patternId, const QImage &templateImage);

    // 검사 결과 접근자
    const InspectionResult &getLastInspectionResult() const { return lastInspectionResult; }
    bool hasLastInspectionResult() const { return hasInspectionResult; }
    
    // SSIM 히트맵 실시간 갱신
    void updateSSIMHeatmap(const QUuid &patternId, double ssimNgThreshold);
    
    // ANOMALY 히트맵 실시간 갱신
    void updateAnomalyHeatmap(const QUuid &patternId, double passThreshold);

    // STRIP/CRIMP 모드별 검사 결과 접근자
    void saveInspectionResultForMode(int mode, const InspectionResult &result, const QPixmap &frame);
    void saveCurrentResultForMode(int mode, const QPixmap &frame); // 현재 패턴 상태로 저장
    bool switchToModeResult(int mode);                             // 모드별 결과로 전환, 성공 시 true 반환
    bool hasModeResult(int frameIndex) const { return frameIndex >= 0 && frameIndex < 4 && hasFrameResult[frameIndex]; }
    const InspectionResult& getFrameResult(int frameIndex) const { return frameResults[frameIndex]; }
    void clearModeResults()
    {
        for (int i = 0; i < 4; i++) {
            hasFrameResult[i] = false;
        }
    }

    // 필터 관련 메서드들 (UUID 기반으로 통일)
    void addPatternFilter(const QUuid &patternId, int filterType);
    void removePatternFilter(const QUuid &patternId, int filterIndex);
    void setPatternFilterEnabled(const QUuid &patternId, int filterIndex, bool enabled);
    void setPatternFilterParam(const QUuid &patternId, int filterIndex, const QString &paramName, int value);
    void movePatternFilterUp(const QUuid &patternId, int filterIndex);
    void movePatternFilterDown(const QUuid &patternId, int filterIndex);
    const QList<FilterInfo> &getPatternFilters(const QUuid &patternId) const;

    // 배경 이미지 및 줌/패닝 관련
    void setBackgroundPixmap(const QPixmap &pixmap);
    void setBackgroundImage(const QPixmap &pixmap) { setBackgroundPixmap(pixmap); } // 별칭
    QPixmap getBackgroundImage() const { return backgroundPixmap; }
    QPoint getPanOffset() const { return panOffset; }
    double getZoomFactor() const { return zoomFactor; }
    void setPanOffset(const QPoint &offset)
    {
        panOffset = offset;
        viewport()->update();
    }
    void setZoomFactor(double factor)
    {
        zoomFactor = factor;
        viewport()->update();
    }

    // 사각형 그리기 함수
    void setCurrentRect(const QRect &rect)
    {
        currentRect = rect;
        viewport()->update();
    }
    QRect getCurrentRect() const { return currentRect; }
    void clearCurrentRect()
    {
        currentRect = QRect();
        viewport()->update();
    }

    // 리사이즈 핸들 관련 함수 선언 추가
    QVector<QPoint> getRotatedCorners() const;
    QVector<QPoint> getRotatedCornersForPattern(const PatternInfo &pattern) const;
    int getCornerHandleAt(const QPoint &pos) const;
    QRect rotateHandleRect() const;

    // 스케일링 관련 메서드
    void setScalingInfo(const QSize &origSize, const QSize &displaySize);

    // 좌표 변환 함수 (내부적으로 QGraphicsView의 mapToScene/mapFromScene 사용)
    QPoint displayToOriginal(const QPoint &displayPos) const;
    QPoint originalToDisplay(const QPoint &originalPos) const;
    QRect originalRectToDisplay(const QRect &origRect) const;

    QPoint getRotatedCenter() const;
    int getRotateHandleAt(const QPoint &pos) const;

    // 필터 처리 함수들 - ImageProcessor 프록시
    void applyFiltersToImage(cv::Mat &image);
    void applyThresholdFilter(cv::Mat &src, cv::Mat &dst, int threshold)
    {
        ImageProcessor::applyThresholdFilter(src, dst, threshold);
    }
    void applyBlurFilter(cv::Mat &src, cv::Mat &dst, int kernelSize)
    {
        ImageProcessor::applyBlurFilter(src, dst, kernelSize);
    }
    void applyCannyFilter(cv::Mat &src, cv::Mat &dst, int threshold1, int threshold2)
    {
        ImageProcessor::applyCannyFilter(src, dst, threshold1, threshold2);
    }
    void applySobelFilter(cv::Mat &src, cv::Mat &dst, int kernelSize)
    {
        ImageProcessor::applySobelFilter(src, dst, kernelSize);
    }
    void applyLaplacianFilter(cv::Mat &src, cv::Mat &dst, int kernelSize)
    {
        ImageProcessor::applyLaplacianFilter(src, dst, kernelSize);
    }
    void applySharpenFilter(cv::Mat &src, cv::Mat &dst, int strength)
    {
        ImageProcessor::applySharpenFilter(src, dst, strength);
    }
    void applyBrightnessFilter(cv::Mat &src, cv::Mat &dst, int value)
    {
        ImageProcessor::applyBrightnessFilter(src, dst, value);
    }
    void applyContrastFilter(cv::Mat &src, cv::Mat &dst, int value)
    {
        ImageProcessor::applyContrastFilter(src, dst, value);
    }

signals:
    // UUID 기반 시그널
    void enterKeyPressed(const QRect &rect);
    void rectDrawn(const QRect &rect);
    void patternSelected(const QUuid &id);
    void patternRectChanged(const QUuid &id, const QRect &rect);
    void patternAngleChanged(const QUuid &id, double angle);
    void patternAdded(const QUuid &id);
    void patternRemoved(const QUuid &id);
    void patternsGrouped();
    void requestRemovePattern(const QUuid &patternId);
    void requestAddFilter(const QUuid &patternId);
    void patternNameChanged(const QUuid &patternId, const QString &newName);
    void patternEnableStateChanged(const QUuid &patternId, bool enabled);
    void fidTemplateUpdateRequired(const QUuid &patternId);
    void insTemplateUpdateRequired(const QUuid &patternId);
    void selectedInspectionPatternCleared();                  // 검사 결과 필터 해제 시그널
    void pixelInfoChanged(int x, int y, int r, int g, int b); // 픽셀 정보 변경 시그널

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override; // 제스처 이벤트 처리

private:
    bool isInspectionMode = false;
    bool hasInspectionResult = false;
    bool lastInspectionPassed = false;
    int currentStripCrimpMode = 0; // 0: STRIP, 1: CRIMP

    InspectionResult lastInspectionResult;

    // 프레임별 검사 결과 저장 (0,1,2,3)
    std::array<InspectionResult, 4> frameResults;
    std::array<QPixmap, 4> framePixmaps;
    std::array<QList<PatternInfo>, 4> framePatterns;
    std::array<bool, 4> hasFrameResult = {false, false, false, false};
    QUuid selectedInspectionPatternId; // 선택된 검사 결과 패턴 필터링

    // 거리 측정 관련 변수
    bool isMeasuring = false;
    QPoint measureStartPoint;
    QPoint measureEndPoint;

    // 리사이즈/회전 관련 변수
    bool isResizing = false;
    bool isRotating = false;
    int activeHandleIdx = -1;
    QPoint fixedScreenPos;
    QPoint rotateStartPos;
    QPoint rotationCenter; // 회전 중심점 고정용
    double initialAngle = 0.0;

    // 언어 지원을 위한 텍스트 요소들
    QString m_statusText;            // 상태 텍스트
    QMap<QUuid, QString> groupNames; // 그룹 이름 맵핑

    // 패턴별 윤곽선 저장
    QMap<QUuid, QList<QVector<QPoint>>> patternContours;

    QString statusInfo;
    EditMode m_editMode = EditMode::Move; // 기본값은 이동 모드
    QString currentCameraUuid;            // 현재 카메라 UUID
    QString currentCameraName;            // 현재 카메라 이름 (표시용)
    int currentFrameIndex = 0;            // 현재 프레임 인덱스 (0~3)
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
    QPixmap applyZoom(const QPixmap &original);
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

    // QGraphicsView 관련 멤버
    QGraphicsScene *scene = nullptr;
    QGraphicsPixmapItem *bgPixmapItem = nullptr;

    void drawInspectionResults(QPainter &painter, const InspectionResult &result);

    // 검사 결과 시각화 서브 함수들
    void drawROIPatterns(QPainter &painter, const InspectionResult &result);
    void drawFIDPatterns(QPainter &painter, const InspectionResult &result);
    void drawINSPatterns(QPainter &painter, const InspectionResult &result);

    // INS 검사방법별 시각화 함수들
    void drawINSStripVisualization(QPainter &painter, const InspectionResult &result,
                                   const QUuid &patternId, const PatternInfo *patternInfo,
                                   const QRectF &inspRectScene, double insAngle);
    void drawINSDiffVisualization(QPainter &painter, const InspectionResult &result,
                                  const QUuid &patternId, const PatternInfo *patternInfo,
                                  const QRectF &inspRectScene, double insAngle);
    void drawINSCrimpVisualization(QPainter &painter, const InspectionResult &result,
                                   const QUuid &patternId, const PatternInfo *patternInfo,
                                   const QRectF &inspRectScene, double insAngle);
    void drawINSSSIMVisualization(QPainter &painter, const InspectionResult &result,
                                  const QUuid &patternId, const PatternInfo *patternInfo,
                                  const QRectF &inspRectScene, double insAngle);
    void drawINSAnomalyVisualization(QPainter &painter, const InspectionResult &result,
                                     const QUuid &patternId, const PatternInfo *patternInfo,
                                     const QRectF &inspRectScene, double insAngle);

    // STRIP 세부 시각화 함수들
    void drawStripRearBox(QPainter &painter, const InspectionResult &result,
                          const QUuid &patternId, const PatternInfo *patternInfo,
                          const QRectF &inspRectScene, double insAngle,
                          double currentScale, double cosA, double sinA,
                          const QPointF &centerViewport, const QPointF &rearBoxCenterVP);
    void drawStripFrontBox(QPainter &painter, const InspectionResult &result,
                           const QUuid &patternId, const PatternInfo *patternInfo,
                           const QRectF &inspRectScene, double insAngle,
                           double currentScale, double cosA, double sinA,
                           const QPointF &centerViewport, const QPointF &frontBoxCenterVP);
    void drawStripEdgeVisualization(QPainter &painter, const InspectionResult &result,
                                    const QUuid &patternId, const PatternInfo *patternInfo,
                                    const QRectF &inspRectScene, double insAngle,
                                    double currentScale, double cosA, double sinA,
                                    const QPointF &centerViewport, const QPointF &patternCenterScene);
    void drawStripContourPoints(QPainter &painter, const InspectionResult &result,
                                const QUuid &patternId);

    // 공통 헬퍼 함수들
    void drawRotatedBox(QPainter &painter, const QRectF &rect, const QPointF &center,
                        double angle, const QPen &pen, const QBrush &brush = Qt::NoBrush);
    void drawRotatedLabel(QPainter &painter, const QString &text, const QRectF &rect,
                          const QPointF &center, double angle, const QColor &bgColor,
                          const QColor &textColor, const QFont &font);
    void drawYellowBoundingBox(QPainter &painter, const QSizeF &originalSize,
                               const QPointF &center, double angle, double scale);
    void drawPassNGLabel(QPainter &painter, bool passed, const QRectF &rect, const QFont &font);

    // paintEvent 세부 렌더링 함수들
    void drawTeachingModePatterns(QPainter &painter);
    void drawSelectedPatternHandles(QPainter &painter);
    void drawStripGradientRange(QPainter &painter, const PatternInfo &pattern);
    void drawStripThicknessBoxes(QPainter &painter, const PatternInfo &pattern);
    void drawCrimpBarrelBoxes(QPainter &painter, const PatternInfo &pattern);
    void drawMeasurementLine(QPainter &painter);
    void drawCurrentDrawingRect(QPainter &painter);

    // 그룹 바운딩 박스 그리기 함수
    void drawGroupBoundingBox(QPainter &painter, const QList<PatternInfo> &groupPatterns);

    // 패턴 그룹화 관련 함수
    QList<QUuid> findPatternsInSelection() const;
    void showContextMenu(const QPoint &pos);
    void groupPatternsInSelection(const QList<QUuid> &patternIds);
    void ungroupPatternsInSelection(const QList<QUuid> &patternIds);

    // 히트 테스트 및 리사이즈 헬퍼 함수
    QUuid hitTest(const QPoint &pos);
    ResizeHandle getResizeHandle(const QPoint &pos, const QUuid &patternId);
    QCursor getResizeCursor(ResizeHandle handle);
    QRect getResizedRect(const QRect &rect, const QPoint &pos, ResizeHandle handle);
    void drawResizeHandles(QPainter &painter, const QRect &rect);

    void updateUIFromFilters();
    void connectFilterSignals(int filterType, const QString &paramName, QSlider *slider, QLabel *valueLabel);
    void connectFilterComboSignals(int filterType, const QString &paramName, QComboBox *combo);
    void updateFilterParam(int filterType, const QString &paramName, int value);
    QUuid getPatternId(int index) const;

    // 필터 관련 데이터
    QVector<int> filterTypes;
    QMap<int, QString> filterNames;
    QMap<int, QCheckBox *> filterCheckboxes;
    QMap<int, QMap<QString, QSlider *>> filterSliders;
    QMap<int, QMap<QString, QLabel *>> filterValueLabels;
    QMap<int, QMap<QString, QComboBox *>> filterCombos;
    QMap<int, FilterInfo> appliedFilters;
    QMap<int, QMap<QString, int>> defaultParams;
};

#endif // CAMERAVIEW_H