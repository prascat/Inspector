#ifndef SIMPLETEACH_H
#define SIMPLETEACH_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QScrollArea>
#include <QKeyEvent>
#include <QSizePolicy>
#include <QTouchEvent>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPixmap>
#include <QPoint>
#include <QMessageBox>
#include <QApplication>
#include <QThread>
#include <QDebug>
#include <QListWidget>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QTimer>
#include "CommonDefs.h"

// Forward declaration
class TeachingWidget;

class SimpleTeach : public QWidget
{
    Q_OBJECT

public:
    explicit SimpleTeach(TeachingWidget* teachingWidget, QWidget *parent = nullptr);
    ~SimpleTeach();
    
    int getSelectedCameraIndex() const;
    CameraInfo getSelectedCameraInfo() const;

     // **현재 누르고 있는 버튼 추적**
    enum ActiveAction {
        None,
        MoveUp, MoveDown, MoveLeft, MoveRight,
        SizeUp, SizeDown, SizeLeft, SizeRight
    };

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    
    bool gestureEvent(QGestureEvent* event);
    void pinchTriggered(QPinchGesture* gesture);

private slots:
    void updateLiveImage();

    void startRepeatAction(ActiveAction action);
    void stopRepeatAction();
    void onRepeatAction();
    void performAction(ActiveAction action);

    void updateCameraSlots();
    void onCameraSelected(int cameraIndex);
    void onBackClicked();
    void onNextClicked();
    
    // 티칭 관련 슬롯들
    void onSaveRecipeClicked();
    void onLoadExistingPatterns();
    
    // 확대/축소 버튼 슬롯들
    void onZoomInClicked();
    void onZoomOutClicked();
    void onZoomResetClicked();
    
    // 패턴 편집 터치 버튼 슬롯들
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onMoveLeftClicked();
    void onMoveRightClicked();
    void onSizeUpClicked();
    void onSizeDownClicked();
    void onSizeLeftClicked();
    void onSizeRightClicked();

private:
    enum Step {
        CAMERA_SELECTION,
        TEACHING_VIEW
    };
    
    void setupUI();
    void createCameraSelectionPage();
    void createTeachingViewPage();
    void createPatternEditButtons();
    void updateStepDisplay();
    void loadExistingPatternsFromTeachingWidget();
    void updateScalingInfo();

    // 터치 확대/축소 관련 함수들
    void setOriginalPixmap(const QPixmap& pixmap);
    void setZoomFactor(double factor);
    void resetZoom();
    void updateDisplayedPixmap();
    QPoint mapToOriginal(const QPoint& displayPoint);
    QPoint mapToDisplay(const QPoint& originalPoint);
    
    // 티칭 관련 함수들
    void drawTeachingPatterns(QPainter& painter);
    void updatePatternList();
    void selectPattern(const QUuid& patternId);
    void moveSelectedPattern(int dx, int dy);
    void resizeSelectedPattern(int dw, int dh);
    void savePatternToTeachingWidget();
    
    // UI 컴포넌트
    QVBoxLayout* mainLayout;
    QStackedWidget* contentStack;
    
    // 카메라 선택 페이지
    QWidget* cameraListWidget;
    QGridLayout* cameraGridLayout;
    QList<QFrame*> cameraFrames;
    QList<QPushButton*> cameraSelectButtons;
    
    // 티칭 영상 페이지
    QWidget* teachingViewWidget;
    QHBoxLayout* teachingLayout;
    QVBoxLayout* rightPanelLayout;
    
    QWidget* patternButtonWidget;
    QVBoxLayout* patternButtonLayout;
    QList<QPushButton*> patternButtons;

    // 티칭 관련 UI
    QPushButton* loadExistingButton;
    QPushButton* saveRecipeButton;
    
    // 확대/축소 버튼들
    QPushButton* zoomInButton;
    QPushButton* zoomOutButton;
    QPushButton* zoomResetButton;
    
    // 패턴 편집 터치 버튼들
    QWidget* patternEditWidget;
    QPushButton* moveUpButton;
    QPushButton* moveDownButton;
    QPushButton* moveLeftButton;
    QPushButton* moveRightButton;
    QPushButton* sizeUpButton;
    QPushButton* sizeDownButton;
    QPushButton* sizeLeftButton;
    QPushButton* sizeRightButton;
    
    // 버튼들
    QPushButton* backButton;
    QPushButton* nextButton;
    
    // 데이터
    TeachingWidget* teachingWidget;
    QVector<CameraInfo> cameraInfos;
    int selectedCameraIndex;
    QString selectedCameraUuid;
    Step currentStep;
    
    // 터치 확대/축소 관련 멤버 변수들
    QPixmap originalPixmap;
    QPixmap scaledPixmap;
    double zoomFactor;
    QPoint panOffset;
    
    // 터치/제스처 관련
    bool isPanning;
    QPoint lastPanPoint;
    QPoint lastTouchPoint1;
    QPoint lastTouchPoint2;
    bool twoFingerMode;
    double lastPinchScale;
    
    // 티칭 관련 멤버 변수들
    QList<PatternInfo> teachingPatterns;
    QUuid selectedPatternId;

    // **반복 동작을 위한 타이머들**
    QTimer* liveUpdateTimer;
    QTimer* moveTimer;
    QTimer* sizeTimer;
    
    ActiveAction currentAction;

signals:
    void patternMoved(const QUuid& patternId, const QRectF& newRect);
    void patternResized(const QUuid& patternId, const QRectF& newRect);

};

#endif // SIMPLETEACH_H