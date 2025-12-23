#ifndef TEACHINGWIDGET_H
#define TEACHINGWIDGET_H

#include <QWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QInputDialog>
#include <QFileDialog>
#include <QXmlStreamWriter>
#include <QPushButton>
#include <QTimer>
#include <QIcon>
#include <QDebug>
#include <QCheckBox>
#include <QSlider>
#include <QTableWidget>
#include <QHeaderView>
#include <QGroupBox>
#include <QStackedWidget>
#include <QApplication>
#include <QButtonGroup>
#include <QScrollArea>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QBuffer>
#include <QString>
#include <QMenu>
#include <QMenuBar> 
#include <QMessageBox>
#include <QDateTime>
#include "RecipeManager.h"
#include <QThread>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QRandomGenerator>
#include <QTreeWidgetItem>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QSlider>
#include <QQueue>
#include <QProgressDialog>
#include <QStringList>
#include <QShortcut>
#include <QTextEdit>
#include <atomic>
#include <QThreadPool>
#include <QRunnable>
#include "TrainDialog.h"
#include <QDir>
#include <opencv2/opencv.hpp>

#include "CommonDefs.h"
#include "FilterDialog.h"
#include "InsProcessor.h"
#include "ConfigManager.h"

#ifdef USE_SPINNAKER
#include "Spinnaker.h"
#endif

// 전방 선언
class SerialCommunication;
class SerialSettingsDialog;
class CameraSettingsDialog;
class ClientDialog;
class TestDialog;

class CameraGrabberThread : public QThread {
    Q_OBJECT
    
public:
    CameraGrabberThread(QObject* parent = nullptr);
    ~CameraGrabberThread();
    void setCameraIndex(int index) { m_cameraIndex = index; }
    void stopGrabbing();
    void setPaused(bool paused);
    
signals:
    void frameGrabbed(const cv::Mat& frame, int cameraIndex);  // 카메라 인덱스 추가
    void triggerSignalReceived(const cv::Mat& frame, int cameraIndex);  // 트리거 신호 수신
    
protected:
    void run() override;
    
private: 
    int m_cameraIndex = -1;
    QMutex m_mutex;
    QWaitCondition m_condition;
    std::atomic<bool> m_stopped;
    std::atomic<bool> m_paused;
    bool previousInspectMode = false;
    bool modeInitialized = false;
};

// UI 업데이트 스레드
class UIUpdateThread : public QThread {
    Q_OBJECT
    
public:
    UIUpdateThread(QObject* parent = nullptr);
    ~UIUpdateThread();
    
    void stopUpdating();
    void setPaused(bool paused);
    
signals:
    void updateUI();
    
protected:
    void run() override;
    
private:
    QMutex m_mutex;
    QWaitCondition m_condition;
    std::atomic<bool> m_stopped;
    std::atomic<bool> m_paused;
};

// 드래그 앤 드롭을 지원하는 커스텀 TreeWidget
class CustomPatternTreeWidget : public QTreeWidget {
    Q_OBJECT

public:
    explicit CustomPatternTreeWidget(QWidget* parent = nullptr) : QTreeWidget(parent) {}

protected:
    void dropEvent(QDropEvent* event) override {
        QTreeWidget::dropEvent(event);  // 기본 드롭 처리
        emit dropCompleted();           // 드롭 완료 신호 발송
    }

signals:
    void dropCompleted();
};

class TeachingWidget : public QWidget {
    Q_OBJECT

    // Friend classes for accessing private members
    friend class CameraGrabberThread;

public:
    // RecipeManager에서 접근 가능하도록 public으로 선언
    std::vector<cv::Mat> cameraFrames;
    std::vector<bool> frameUpdatedFlags;  // 각 프레임의 업데이트 플래그 (트리거로 새 데이터 수신됨)
    bool camOff = true;
    int cameraIndex;
    int currentDisplayFrameIndex = 0;  // 현재 메인 뷰에 표시된 프레임 인덱스 (0~3)
    int nextInspectionFrameIndex = -1;  // 다음 트리거 시 검사할 프레임 인덱스 (-1: 미지정)
    
    // 스레드 안전 cameraInfos 접근 함수들
    QVector<CameraInfo> getCameraInfos() const;
    CameraInfo getCameraInfo(int index) const;
    bool setCameraInfo(int index, const CameraInfo& info);
    int getCameraInfosCount() const;
    void clearCameraInfos();
    void appendCameraInfo(const CameraInfo& info);
    void removeCameraInfo(int index);
    bool isValidCameraIndex(int index) const;
    
    // 연결된 모든 카메라의 UUID 목록 반환
    QStringList getConnectedCameraUuids() const;
    int getCurrentCameraIndex() const { return cameraIndex; }
    void switchToCamera(const QString& cameraUuid);
    void detectCameras();
    void startCamera();
    void stopCamera();
    CameraView* getCameraView() const { return cameraView; }
    void updateCameraButtonState(bool isStarted);
    // 시리얼 통신 설정
    void setSerialCommunication(SerialCommunication* serialComm);
    
    explicit TeachingWidget(int cameraIndex, const QString &cameraStatus, QWidget *parent = nullptr);
    virtual ~TeachingWidget();
    cv::Mat getCurrentFrame() const;
    cv::Mat getCurrentFilteredFrame() const;
    void updateFilterParam(const QUuid& patternId, int filterIndex, const QString& paramName, int value);
    InspectionResult runSingleInspection(int specificCameraIndex);
    void stopSingleInspection();
    void resumeToLiveMode();
    void updateCameraFrame();
    void updatePatternTree();
    void updatePatternFilters(int patternIndex);
    void updateFidTemplateImage(const QUuid& patternId);
    void updateFidTemplateImage(PatternInfo* pattern, const QRectF& rect);
    void updateInsMatchTemplate(PatternInfo* pattern); 
    void updateInsTemplateImage(const QUuid& patternId);
    void updateInsTemplateImage(PatternInfo* pattern, const QRectF& newRect);
    void updateAllPatternTemplateImages(); // 모든 패턴의 템플릿 이미지 갱신
    void updatePatternTemplateImage(const QUuid& patternId); // 개별 패턴의 템플릿 이미지 갱신
    cv::Mat extractRotatedRegion(const cv::Mat& image, const QRectF& rect, double angle);
    
    // 회전된 사각형의 bounding box 크기 계산
    static void calculateRotatedBoundingBox(double width, double height, double angle, 
                                           int& bboxWidth, int& bboxHeight);
    
    // TEACH 모드 관련
    void onTeachModeToggled(bool checked);
    void setTeachingButtonsEnabled(bool enabled);
    
    // 레시피 매니저 접근
    RecipeManager* getRecipeManager() const { return recipeManager; }
    QTreeWidget* getPatternTree() const { return patternTree; }
    
    // 필터 조정 모드 제어
    void setFilterAdjusting(bool adjusting) { isFilterAdjusting = adjusting; }
    bool getFilterAdjusting() const { return isFilterAdjusting; }
    
    // 필터 미리보기를 위한 선택 상태 설정
    void selectFilterForPreview(const QUuid& patternId, int filterIndex);
    
    // 패턴 업데이트 중 UI 업데이트 방지
    void setUpdatingPattern(bool updating) { isUpdatingPattern = updating; }
    bool getUpdatingPattern() const { return isUpdatingPattern; }
    
    void saveRecipe();
    void deleRecipe();
    bool loadRecipe(const QString &fileName = QString(), bool showMessageBox = true);
    
    // 레시피 로드 상태 확인
    bool hasLoadedRecipe() const;
    
    // Frame 인덱스 계산
    int getFrameIndex(int cameraIndex) const;
    
    // === 테스트 다이얼로그용 공용 메서드 ===
    void setCameraFrame(int index, const cv::Mat& frame);
    InspectionResult runInspection();
    QString getPatternName(const QUuid& patternId) const;
    void triggerRunButton();
    
    // === 레시피 관리 함수들 (camOff에서도 사용) ===
    void newRecipe();
    void openRecipe(bool autoMode = false);
    void saveRecipeAs();
    void manageRecipes();
    void onRecipeSelected(const QString& recipeName);
    void clearAllRecipeData();
    
signals:
    void frameProcessed(const cv::Mat& frame);
    void goBack();
    void patternSelectionChanged(int patternIndex);
    void serverConnected();
    void serverDisconnected();
    void messageReceived(const QString& message);
    
private slots:
    void onTriggerSignalReceived(const cv::Mat& frame, int cameraIndex);
    void onStripCrimpModeChanged(int mode);  // 서버로부터 STRIP/CRIMP 메시지 수신
    void onFrameIndexReceived(int frameIndex);  // 서버로부터 프레임 인덱스 수신 (0~3)
    void updateUITexts();
    void openLanguageSettings();
    void showCameraSettings();
    void showServerSettings();
    void showSerialSettings();
    void showModelManagement();
    void showTestDialog();
    void openGeneralSettings() {
        QMessageBox::information(this, TR("GENERAL_SETTINGS"), 
            TR("GENERAL_SETTINGS_INFO"));
    }
    void showAboutDialog() {
        CustomMessageBox(this, CustomMessageBox::Information, TR("ABOUT"), 
                        "KM Inspector\n© 2025 KM DigiTech.\n\n"
                        "이 프로그램은 KM DigiTech.의 소유입니다.\n"
                        "무단 복제 및 배포를 금지합니다.").exec();
    }
    void saveCurrentImage();
    void saveImageAsync(const cv::Mat &frame, bool isPassed);
    void loadTeachingImage();
    void processGrabbedFrame(const cv::Mat& frame, int camIdx);
    void processGrabbedFrame(const cv::Mat& frame, int camIdx, int forceFrameIndex);  // 프레임 인덱스 강제 지정
    void processNextInspection(int frameIdx);  // 큐에서 다음 검사 처리
    void updateUIElements();
    void addPattern();
    void removePattern();
    void addFilter();
    void onPatternSelected(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onPatternTableDropEvent(const QModelIndex &parent, int start, int end, const QModelIndex &destination, int row);
    void onPatternTreeDropCompleted();

    void switchToRecipeMode();
    void switchToTestMode();
    bool runInspect(const cv::Mat& frame, int specificCameraIndex = -1, bool updateMainView = true, int frameIndexForResult = -1);
    void onBackButtonClicked();
    void toggleFullScreenMode();
    
    // 카메라 모드 슬롯들 (camOn/camOff)
    void onCamModeToggled();
    
public slots:
    void receiveLogMessage(const QString& message);
    
private:
    void updateMainCameraUI(const InspectionResult& result, const cv::Mat& frameForInspection);
    void updatePreviewFrames();
    void updateSinglePreview(int frameIndex);
    void initializeLanguageSystem();
    void saveImageAsync(const cv::Mat& frame);
    PatternInfo* findPatternById(const QUuid& patternId);
    QString getFilterTypeName(int filterType);
    void updateTreeItemTexts(QTreeWidgetItem* item);
    PatternInfo loadPatternFromXml(QXmlStreamReader& xml, const QString& cameraUuid, 
                                 QMap<QString, QStringList>& childrenMap, 
                                 QMap<QString, QTreeWidgetItem*>& itemMap, 
                                 int& patternCount);
    void showImageViewerDialog(const QImage& image, const QString& title);
    
    // ANOMALY 모델 학습 메서드
    void trainAnomalyPattern(const QString& patternName);
    
    // 각도 정규화 함수 (-180° ~ +180° 범위로 변환)
    static double normalizeAngle(double angle);
    double calculatePhysicalLength(int pixelLength) const;
    void updateCameraInfoForSimulation(const QString& imagePath);
    void updateCameraInfoForDisconnected();
    void enablePatternEditingFeatures();
    void enableFilterWidgets();
    
    // 스레드 안전성을 위한 뮤텍스
    mutable QMutex cameraInfosMutex;
    
    // 필터 설정 중 프로퍼티 패널 업데이트 방지 플래그
    bool isFilterAdjusting = false;
    
    // 패턴 업데이트 중 UI 업데이트 방지 플래그
    bool isUpdatingPattern = false;
    
    // 순차 프레임 인덱스 (0, 1, 2, 3 순환)
    int sequentialFrameIndex = 0;
    
    // 서버로부터 받은 STRIP/CRIMP 모드 (0=STRIP, 1=CRIMP)
    int currentStripCrimpMode = 0;
    
    // 선택된 필터 정보 (티칭 모드 필터 표시용)
    QUuid selectedPatternId;
    int selectedFilterIndex = -1;

    // 레시피 관리자
    RecipeManager* recipeManager;
    QString currentRecipeName;
    bool hasUnsavedChanges = false;
    bool isLoadingRecipe = false;  // 레시피 로드 중 플래그 (템플릿 자동 업데이트 방지)

    // 메뉴 및 액션
    QMenuBar* menuBar = nullptr;
    QMenu* fileMenu = nullptr;
    QMenu* settingsMenu = nullptr;
    QMenu* helpMenu = nullptr;
    
    QAction* camModeAction = nullptr;
    QAction* saveImageAction = nullptr;
    QAction* exitAction = nullptr;
    QAction* cameraSettingsAction = nullptr;
    QAction* languageSettingsAction = nullptr;
    QAction* settingsAction = nullptr;
    QAction* loadRecipeAction = nullptr;
    QAction* aboutAction = nullptr;
    QAction* serverSettingsAction = nullptr;
    QAction* serialSettingsAction = nullptr;
    QAction* modelManagementAction = nullptr;
    QAction* testDialogAction = nullptr;
    
    // 버튼 멤버 변수들
    QPushButton* modeToggleButton = nullptr;
    QPushButton* teachModeButton = nullptr;  // TEACH ON/OFF 버튼 추가
    QPushButton* startCameraButton = nullptr;
    QPushButton* runStopButton = nullptr;
    QPushButton* saveRecipeButton = nullptr;
    QPushButton* addPatternButton = nullptr;
    QPushButton* addFilterButton = nullptr;
    QPushButton* removeButton = nullptr;
    QPushButton* roiButton = nullptr;
    QPushButton* fidButton = nullptr;
    QPushButton* insButton = nullptr;
    
    // 패널 및 라벨 멤버 변수들
    QLabel* emptyPanelLabel = nullptr;
    QLabel* basicInfoLabel = nullptr;
    QLabel* patternIdLabel = nullptr;
    QLabel* patternNameLabel = nullptr;
    QLabel* patternTypeLabel = nullptr;
    QLabel* patternCameraLabel = nullptr;
    QLabel* positionSizeLabel = nullptr;
    QLabel* positionLabel = nullptr;
    QLabel* sizeLabel = nullptr;
    QLabel* angleLabel = nullptr;
    QLabel* angleValue = nullptr;
    QLineEdit* angleEdit = nullptr;
    QLabel* fidStepLabel = nullptr;
    
    // 속성 패널 위젯들
    QStackedWidget* propertyStackWidget = nullptr;
    QStackedWidget* specialPropStack = nullptr;
    
    // ROI 속성
    // includeAllCameraCheck 제거됨
    
    // FID 속성
    QGroupBox* fidMatchCheckBox = nullptr;  // GroupBox를 체크박스로 사용
    QLabel* fidMatchMethodLabel = nullptr;
    QComboBox* fidMatchMethodCombo = nullptr;
    QLabel* fidMatchThreshLabel = nullptr;
    QCheckBox* fidRotationCheck = nullptr;
    
    // INS 속성
    QLabel* insPassThreshLabel = nullptr;
    QLabel* insMethodLabel = nullptr;
    // insInvertCheck 제거됨
    QLabel* insThreshLabel = nullptr;
    QLabel* insCompareLabel = nullptr;
    QLabel* insTemplateImg = nullptr;
    QLabel* insMatchTemplateImg = nullptr;  // 패턴 매칭용 템플릿 이미지

    // 패턴 매칭 (Fine Alignment) 관련 위젯
    QGroupBox* insPatternMatchGroup = nullptr;
    QLabel* insPatternMatchMethodLabel = nullptr;
    QComboBox* insPatternMatchMethodCombo = nullptr;
    QLabel* insPatternMatchThreshLabel = nullptr;
    QDoubleSpinBox* insPatternMatchThreshSpin = nullptr;
    QCheckBox* insPatternMatchRotationCheck = nullptr;
    QLabel* insPatternMatchMinAngleLabel = nullptr;
    QDoubleSpinBox* insPatternMatchMinAngleSpin = nullptr;
    QLabel* insPatternMatchMaxAngleLabel = nullptr;
    QDoubleSpinBox* insPatternMatchMaxAngleSpin = nullptr;
    QLabel* insPatternMatchStepLabel = nullptr;
    QDoubleSpinBox* insPatternMatchStepSpin = nullptr;

    // STRIP 검사 패널 관련 위젯
    QGroupBox* insStripPanel = nullptr;
    QLabel* insStripKernelLabel = nullptr;
    QSpinBox* insStripKernelSpin = nullptr;
    QLabel* insStripGradThreshLabel = nullptr;
    QDoubleSpinBox* insStripGradThreshSpin = nullptr;
    QLabel* insStripStartLabel = nullptr;
    QSlider* insStripStartSlider = nullptr;
    QLabel* insStripStartValueLabel = nullptr;
    QLabel* insStripEndLabel = nullptr;
    QSlider* insStripEndSlider = nullptr;
    QLabel* insStripEndValueLabel = nullptr;
    QLabel* insStripMinPointsLabel = nullptr;
    QSpinBox* insStripMinPointsSpin = nullptr;
    
    // STRIP 길이검사 관련 위젯들
    QGroupBox* insStripLengthGroup = nullptr;
    QCheckBox* insStripLengthEnabledCheck = nullptr;
    QLabel* insStripLengthMinLabel = nullptr;
    QLineEdit* insStripLengthMinEdit = nullptr;
    QLabel* insStripLengthMaxLabel = nullptr;
    QLineEdit* insStripLengthMaxEdit = nullptr;
    QLabel* insStripLengthConversionLabel = nullptr;
    QLineEdit* insStripLengthConversionEdit = nullptr;
    QLabel* insStripLengthMeasuredLabel = nullptr;
    QPushButton* insStripLengthRefreshButton = nullptr;
    
    // STRIP 두께 검사 그룹들
    QGroupBox* insStripFrontGroup = nullptr;
    QGroupBox* insStripRearGroup = nullptr;
    QGroupBox* insEdgeGroup = nullptr;
    
    // CRIMP 검사 패널 관련 위젯
    QGroupBox* insCrimpPanel = nullptr;
    
    // CRIMP BARREL 기준 왼쪽/오른쪽 스트리핑 길이 검사 관련 위젯들
    QGroupBox* insBarrelLeftStripGroup = nullptr;
    QSlider* insBarrelLeftStripOffsetSlider = nullptr;
    QLabel* insBarrelLeftStripOffsetValueLabel = nullptr;
    QSlider* insBarrelLeftStripWidthSlider = nullptr;
    QLabel* insBarrelLeftStripWidthValueLabel = nullptr;
    QSlider* insBarrelLeftStripHeightSlider = nullptr;
    QLabel* insBarrelLeftStripHeightValueLabel = nullptr;
    QLineEdit* insBarrelLeftStripMinEdit = nullptr;
    QLineEdit* insBarrelLeftStripMaxEdit = nullptr;
    
    // 베럴 기준 오른쪽 스트리핑 길이 검사
    QGroupBox* insBarrelRightStripGroup = nullptr;
    QSlider* insBarrelRightStripOffsetSlider = nullptr;
    QLabel* insBarrelRightStripOffsetValueLabel = nullptr;
    QSlider* insBarrelRightStripWidthSlider = nullptr;
    QLabel* insBarrelRightStripWidthValueLabel = nullptr;
    QSlider* insBarrelRightStripHeightSlider = nullptr;
    QLabel* insBarrelRightStripHeightValueLabel = nullptr;
    QLineEdit* insBarrelRightStripMinEdit = nullptr;
    QLineEdit* insBarrelRightStripMaxEdit = nullptr;
    
    // STRIP 두께 측정 관련 컨트롤
    // FRONT 두께 측정 관련 위젯들
    QGroupBox* insStripFrontEnabledCheck = nullptr;  // GroupBox를 체크박스로 사용
    QLabel* insStripThicknessWidthLabel = nullptr;
    QSlider* insStripThicknessWidthSlider = nullptr;
    QLabel* insStripThicknessWidthValueLabel = nullptr;
    QLabel* insStripThicknessHeightLabel = nullptr;
    QSlider* insStripThicknessHeightSlider = nullptr;
    QLabel* insStripThicknessHeightValueLabel = nullptr;
    QLabel* insStripThicknessMinLabel = nullptr;
    QLineEdit* insStripThicknessMinEdit = nullptr;
    QLabel* insStripThicknessMaxLabel = nullptr;
    QLineEdit* insStripThicknessMaxEdit = nullptr;

    // REAR 두께 측정 관련 위젯들
    QGroupBox* insStripRearEnabledCheck = nullptr;  // GroupBox를 체크박스로 사용
    QLabel* insStripRearThicknessWidthLabel = nullptr;
    QSlider* insStripRearThicknessWidthSlider = nullptr;
    QLabel* insStripRearThicknessWidthValueLabel = nullptr;
    QLabel* insStripRearThicknessHeightLabel = nullptr;
    QSlider* insStripRearThicknessHeightSlider = nullptr;
    QLabel* insStripRearThicknessHeightValueLabel = nullptr;
    QLabel* insStripRearThicknessMinLabel = nullptr;
    QLineEdit* insStripRearThicknessMinEdit = nullptr;
    QLabel* insStripRearThicknessMaxLabel = nullptr;
    QLineEdit* insStripRearThicknessMaxEdit = nullptr;

    // EDGE 검사 관련 위젯들
    QGroupBox* insEdgeEnabledCheck = nullptr;  // GroupBox를 체크박스로 사용
    QLabel* insEdgeOffsetXLabel = nullptr;
    QSlider* insEdgeOffsetXSlider = nullptr;
    QLabel* insEdgeOffsetXValueLabel = nullptr;
    QLabel* insEdgeWidthLabel = nullptr;
    QSlider* insEdgeWidthSlider = nullptr;
    QLabel* insEdgeWidthValueLabel = nullptr;
    QLabel* insEdgeHeightLabel = nullptr;
    QSlider* insEdgeHeightSlider = nullptr;
    QLabel* insEdgeHeightValueLabel = nullptr;

    QLabel* insEdgeMaxIrregularitiesLabel = nullptr;
    QSpinBox* insEdgeMaxIrregularitiesSpin = nullptr;
    QLabel* insEdgeDistanceMaxLabel = nullptr;
    QLineEdit* insEdgeDistanceMaxEdit = nullptr;
    QLabel* insEdgeStartPercentLabel = nullptr;
    QSpinBox* insEdgeStartPercentSpin = nullptr;
    QLabel* insEdgeEndPercentLabel = nullptr;
    QSpinBox* insEdgeEndPercentSpin = nullptr;

    QLabel* fidAngleLabel = nullptr;
    QLabel* fidToLabel = nullptr;
    // 필터 패널
    QLabel* filterDescLabel = nullptr;
    QLabel* filterInfoLabel = nullptr;
    
    // ROI 속성 위젯들
    // roiIncludeAllCheck 제거됨

    // FID 속성 위젯들
    QDoubleSpinBox* fidMatchThreshSpin = nullptr;
    QDoubleSpinBox* fidMinAngleSpin = nullptr;
    QDoubleSpinBox* fidMaxAngleSpin = nullptr;
    QDoubleSpinBox* fidStepSpin = nullptr;
    QLabel* fidTemplateImg = nullptr;
    QLabel* fidTemplateImgLabel = nullptr;

    // INS 속성 위젯들
    QDoubleSpinBox* insPassThreshSpin = nullptr;
    QSlider* insPassThreshSlider = nullptr;  // ANOMALY용 슬라이더
    QLabel* insPassThreshValue = nullptr;    // ANOMALY용 값 표시
    QComboBox* insMethodCombo = nullptr; 
    QWidget* insPatternMatchPanel = nullptr;
    QCheckBox* insRotationCheck = nullptr;
    QDoubleSpinBox* insMinAngleSpin = nullptr;
    QDoubleSpinBox* insMaxAngleSpin = nullptr;
    QDoubleSpinBox* insAngleStepSpin = nullptr;
    
    // SSIM 검사 전용 위젯들
    QWidget* ssimSettingsWidget = nullptr;
    QLabel* ssimNgThreshLabel = nullptr;
    QSlider* ssimNgThreshSlider = nullptr;
    QLabel* ssimNgThreshValue = nullptr;
    QSlider* allowedNgRatioSlider = nullptr;
    QLabel* allowedNgRatioValue = nullptr;
    QLabel* ssimColorBar = nullptr;

    // ANOMALY 검사 전용 위젯들
    QWidget* anomalySettingsWidget = nullptr;
    QSpinBox* anomalyMinBlobSizeSpin = nullptr;
    QSpinBox* anomalyMinDefectWidthSpin = nullptr;
    QSpinBox* anomalyMinDefectHeightSpin = nullptr;
    QPushButton* anomalyTrainButton = nullptr;  // ANOMALY 학습 버튼

    // 패턴 기본 정보 관련 위젯들
    QLabel* patternIdValue = nullptr;      
    QLineEdit* patternNameEdit = nullptr;  
    QLabel* patternTypeValue = nullptr;
    QLabel* patternCameraValue = nullptr;

    // 위치 및 크기 관련 위젯들
    QLabel* patternXValue = nullptr;        
    QLabel* patternYValue = nullptr;         
    QLabel* patternWValue = nullptr;        
    QLabel* patternHValue = nullptr;         
    QSpinBox* patternXSpin = nullptr;        
    QSpinBox* patternYSpin = nullptr;       
    QSpinBox* patternWSpin = nullptr;       
    QSpinBox* patternHSpin = nullptr; 

    // 필터 관련 위젯
    QWidget* filterPropertyContainer = nullptr;

#ifdef USE_SPINNAKER
    Spinnaker::SystemPtr m_spinSystem;
    Spinnaker::CameraList m_spinCamList;
    std::vector<Spinnaker::CameraPtr> m_spinCameras;
    bool m_useSpinnaker;

    // Spinnaker SDK 관련 함수
    bool initSpinnakerSDK();
    void releaseSpinnakerSDK();
    bool connectSpinnakerCamera(int index, CameraInfo& info);
    cv::Mat grabFrameFromSpinnakerCamera(Spinnaker::CameraPtr& camera);
#endif

    // ===== 초기화 및 설정 함수 =====
    void initBasicSettings();
    void initYoloModel();  // SEG 모델 초기화
    void setupPatternTree();
    void setupPatternTypeButtons(QVBoxLayout *cameraLayout);
    void setupPreviewOverlay();
    void setupStatusPanel();
    void updateStatusPanel();
    void updateStatusPanelPosition();
    void setupLogOverlay();
    void updateLogOverlayPosition();
    void setupButton(QPushButton* button);

    // ===== 레이아웃 생성 함수 =====
    QVBoxLayout* createMainLayout();
    QHBoxLayout* createContentLayout();
    QVBoxLayout* createCameraLayout();
    QVBoxLayout* createRightPanel();
    void setupRightPanelOverlay();
    QPushButton* createActionButton(const QString &text, const QString &color, const QFont &font);
    
    // ===== 이벤트 연결 함수 =====
    void connectEvents();
    void connectButtonEvents(QPushButton* modeToggleButton, QPushButton* saveRecipeButton,
                           QPushButton* startCameraButton, QPushButton* runStopButton);
    void connectItemChangedEvent();
    void connectPropertyPanelEvents();
    
    // ===== UI 업데이트 함수 =====
    void updatePropertyPanel(PatternInfo* pattern, const FilterInfo* filter, const QUuid& patternId, int filterIndex);
    void updatePropertySpinBoxes(const QRect& rect);
    void createPropertyPanels();

    // ===== 패턴 관리 함수 =====
    QUuid getPatternIdFromItem(QTreeWidgetItem* item);
    QColor getButtonColorForPatternType(PatternType type);
    QTreeWidgetItem* createPatternTreeItem(const PatternInfo& pattern);
    QTreeWidgetItem* findItemById(QTreeWidgetItem* parent, const QUuid& id);
    bool selectItemById(QTreeWidgetItem* item, const QUuid& id);
    bool findAndUpdatePatternName(QTreeWidgetItem* parentItem, const QUuid& patternId, const QString& newName);
    bool findAndUpdatePatternEnabledState(QTreeWidgetItem* parentItem, const QUuid& patternId, bool enabled);

    // ===== 필터 관리 함수 =====
    void addFiltersToTreeItem(QTreeWidgetItem* parentItem, const PatternInfo& pattern);
    QString getFilterParamSummary(const FilterInfo& filter);
    
    // ===== 카메라 관리 함수 =====
    void updateCameraDetailInfo(CameraInfo& info);
    void connectAllCameras();
    QString getCameraName(int index);
    QColor getNextColor();
    QFrame* createCameraPreviewFrame(int index);
    
    // ===== 시뮬레이션 모드 함수 =====
    void toggleSimulationMode();
    void onSimulationImageSelected(const cv::Mat& image, const QString& imagePath, const QString& projectName);
    void onSimulationProjectNameChanged(const QString& projectName);
    void onSimulationProjectSelected(const QString& projectName); // 새로 추가
    
    // 시뮬레이션 레시피 패턴 로드
    void loadSimulationRecipePatterns(const QString& projectName);
    void showBatchResults(const QStringList& results);
    void showBatchResults(const QStringList& results, int passedCount, int failedCount);
    InspectionResult runSingleInspection(const cv::Mat& image);
    
    // camOff 레시피 관리
    QString getCurrentRecipeName() const;
    
    // ===== 레시피 파일 관리 함수 =====
    void readPatternDetails(QXmlStreamReader& xml, PatternInfo& pattern);
    void readFilters(QXmlStreamReader& xml, PatternInfo& pattern);
    void savePatternToXml(QXmlStreamWriter& xml, const PatternInfo& pattern, const QList<PatternInfo>& allPatterns);

    // ===== 멤버 변수 =====
    // 처리기 관련
    InsProcessor* insProcessor;
    
    // 패턴 타입 선택 관련
    QWidget* patternTypeWidget;
    QButtonGroup* patternButtonGroup;
    PatternType currentPatternType;
    
    // UI 위젯 관련
    CameraView *cameraView;
    CustomPatternTreeWidget* patternTree;
    QLabel *zoomValueLabel;
    QVBoxLayout *rightPanelLayout;
    
public:
    QWidget *rightPanelOverlay = nullptr;
    bool rightPanelCollapsed = false;
    int rightPanelExpandedHeight = 600;
    QWidget *rightPanelContent = nullptr;
    QPushButton *rightPanelCollapseButton = nullptr;
    
private:
    QPoint rightPanelDragPos;
    bool rightPanelDragging = false;
    bool rightPanelResizing = false;
    enum class ResizeEdge { None, Right, Bottom, BottomRight };
    ResizeEdge rightPanelResizeEdge = ResizeEdge::None;
    FilterDialog* filterDialog;
    TestDialog* testDialog = nullptr;
    
    // 카메라 관련
    QString cameraStatus;
    QVector<CameraInfo> cameraInfos;
    QLabel* previewOverlayLabel = nullptr;  // 메인 화면 오른쪽 상단 미리보기 (하위 호환용)
    QLabel* previewOverlayLabels[4] = {nullptr, nullptr, nullptr, nullptr};  // 4개 미리보기 (0:CAM0_STRIP, 1:CAM0_CRIMP, 2:CAM1_STRIP, 3:CAM1_CRIMP)
    
    // 상태 표시 패널
    QLabel* serverStatusLabel = nullptr;
    QLabel* serialStatusLabel = nullptr;
    QLabel* diskSpaceLabel = nullptr;
    QLabel* pixelInfoLabel = nullptr;
    QTimer* statusUpdateTimer = nullptr;
    QVector<bool> cameraConnected;
    
    // 로그 오버레이
    QWidget* logOverlayWidget = nullptr;
    QTextEdit* logTextEdit = nullptr;
    QStringList logMessages;
    QPoint logDragStartPos;
    bool logDragging = false;
    bool logResizing = false;
    QPoint logResizeStartPos;
    int logResizeStartHeight = 0;
    
    // 카메라 모드 관련 (camOn/camOff)
    QVariantMap backupRecipeData; // 레시피 백업 데이터
    
    // 티칭 모드 관련
    bool teachingEnabled = false;  // 티칭 모드 활성화 상태
    bool triggerProcessing = false;  // 트리거 처리 중 플래그
    
    // 프레임별 검사 중 플래그 (비동기 검사용)
    std::array<std::atomic<bool>, 4> frameInspecting = {false, false, false, false};
    std::array<QMutex, 4> frameMutexes;
    
    // 프레임별 검사 큐 (트리거 순차 처리)
    std::array<QQueue<cv::Mat>, 4> inspectionQueues;
    std::array<QMutex, 4> queueMutexes;
    
    // 패턴 스타일링 관련
    QVector<QColor> patternColors;
    int nextColorIndex = 0;
    
    UIUpdateThread* uiUpdateThread = nullptr;
    // 카메라별 스레드 보관
    QVector<CameraGrabberThread*> cameraThreads;
    
    // 시리얼 통신 관련
    SerialCommunication* serialCommunication = nullptr;
    SerialSettingsDialog* serialSettingsDialog = nullptr;
    CameraSettingsDialog* cameraSettingsDialog = nullptr;  // 카메라 설정 다이얼로그
    ClientDialog* clientDialog = nullptr;  // 서버 연결 설정 다이얼로그
    
    // 패턴 백업 관련 (검사 중지 시 원래 상태로 복원용)
    QMap<QUuid, PatternInfo> originalPatternBackup; // 원본 패턴 정보 백업
    
    // 전체화면 모드 관련
    bool isFullScreenMode;
    QRect windowedGeometry;
    QShortcut* fullscreenShortcut;
    
    // 레시피 메뉴
    QMenu* recipeMenu = nullptr;
    
    // Docker 학습 프로세스 (종료 시 정리용)
    QProcess* dockerTrainProcess = nullptr;
    
    // TrainDialog 인스턴스 (학습 이미지 수집용)
    TrainDialog* activeTrainDialog = nullptr;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif // TEACHINGWIDGET_H