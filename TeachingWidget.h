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
#include <QProgressDialog>
#include <QStringList>
#include <atomic>
#include <opencv2/opencv.hpp>

#include "CommonDefs.h"
#include "FilterDialog.h"
#include "InsProcessor.h"
#include "LogViewer.h"
#include "ConfigManager.h"

#ifdef USE_SPINNAKER
#include "Spinnaker.h"
#endif

// 전방 선언
class LogViewer;
class SerialCommunication;
class SerialSettingsDialog;
class CameraSettingsDialog;

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
    bool camOff = true;
    int cameraIndex;
    
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
    void updateInsTemplateImage(const QUuid& patternId);
    void updateInsTemplateImage(PatternInfo* pattern, const QRectF& newRect);
    void updateAllPatternTemplateImages(); // 모든 패턴의 템플릿 이미지 갱신
    void updatePatternTemplateImage(const QUuid& patternId); // 개별 패턴의 템플릿 이미지 갱신
    cv::Mat extractRotatedRegion(const cv::Mat& image, const QRectF& rect, double angle);
    
    // TEACH 모드 관련
    void onTeachModeToggled(bool checked);
    void setTeachingButtonsEnabled(bool enabled);
    LogViewer* getLogViewer() const { return logViewer; }
    
    // 레시피 매니저 접근
    RecipeManager* getRecipeManager() const { return recipeManager; }
    QTreeWidget* getPatternTree() const { return patternTree; }
    
    // 필터 조정 모드 제어
    void setFilterAdjusting(bool adjusting) { isFilterAdjusting = adjusting; }
    bool getFilterAdjusting() const { return isFilterAdjusting; }
    
    // 패턴 업데이트 중 UI 업데이트 방지
    void setUpdatingPattern(bool updating) { isUpdatingPattern = updating; }
    bool getUpdatingPattern() const { return isUpdatingPattern; }
    
    void saveRecipe();
    void deleRecipe();
    bool loadRecipe(const QString &fileName = QString(), bool showMessageBox = true);
    
    // 레시피 로드 상태 확인
    bool hasLoadedRecipe() const;
    
    // === 레시피 관리 함수들 (camOff에서도 사용) ===
    void newRecipe();
    void openRecipe(bool autoMode = false);
    void saveRecipeAs();
    void manageRecipes();
    void onRecipeSelected(const QString& recipeName);
    
signals:
    void frameProcessed(const cv::Mat& frame);
    void goBack();
    void patternSelectionChanged(int patternIndex);
    
private slots:
    void onTriggerSignalReceived(const cv::Mat& frame, int cameraIndex);
    void updateUITexts();
    void openLanguageSettings();
    void showCameraSettings();
    void showSerialSettings();
    void openGeneralSettings() {
        QMessageBox::information(this, TR("GENERAL_SETTINGS"), 
            TR("GENERAL_SETTINGS_INFO"));
    }
    void showAboutDialog() {
        QMessageBox::about(this, TR("ABOUT"), 
                        "KM Inspector\n© 2025 KM DigiTech.\n\n"
                        "이 프로그램은 KM DigiTech.의 소유입니다.\n"
                        "무단 복제 및 배포를 금지합니다.");
    }
    void processGrabbedFrame(const cv::Mat& frame, int camIdx);
    void updateUIElements();
    void addPattern();
    void removePattern();
    void addFilter();
    void onPatternSelected(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onPatternTableDropEvent(const QModelIndex &parent, int start, int end, const QModelIndex &destination, int row);
    void onPatternTreeDropCompleted();
    void syncPatternsFromCameraView();
    void switchToRecipeMode();
    void switchToTestMode();
    bool runInspect(const cv::Mat& frame, int specificCameraIndex = -1);
    void onBackButtonClicked();
    
    // 카메라 모드 슬롯들 (camOn/camOff)
    void onCamModeToggled();
    
private:
    void updateMainCameraUI(const InspectionResult& result, const cv::Mat& frameForInspection);
    void updatePreviewFrames();
    void initializeLanguageSystem();
    PatternInfo* findPatternById(const QUuid& patternId);
    QString getFilterTypeName(int filterType);
    void updateTreeItemTexts(QTreeWidgetItem* item);
    PatternInfo loadPatternFromXml(QXmlStreamReader& xml, const QString& cameraUuid, 
                                 QMap<QString, QStringList>& childrenMap, 
                                 QMap<QString, QTreeWidgetItem*>& itemMap, 
                                 int& patternCount);
    void showImageViewerDialog(const QImage& image, const QString& title);
    void setupCalibrationTools();
    void startCalibration();
    
    // 각도 정규화 함수 (-180° ~ +180° 범위로 변환)
    static double normalizeAngle(double angle);
    void finishCalibration(const QRect& calibRect, double realLength);
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
    
    // 카메라 UUID별 캘리브레이션 정보 맵
    QMap<QString, CalibrationInfo> cameraCalibrationMap;

    // 레시피 관리자
    RecipeManager* recipeManager;
    QString currentRecipeName;
    bool hasUnsavedChanges = false;

    // 메뉴 및 액션
    QMenuBar* menuBar = nullptr;
    QMenu* fileMenu = nullptr;
    QMenu* settingsMenu = nullptr;
    QMenu* toolsMenu = nullptr;
    QMenu* helpMenu = nullptr;
    
    QAction* camModeAction = nullptr;
    QAction* exitAction = nullptr;
    QAction* cameraSettingsAction = nullptr;
    QAction* languageSettingsAction = nullptr;
    QAction* settingsAction = nullptr;
    QAction* loadRecipeAction = nullptr;
    QAction* aboutAction = nullptr;
    QAction* calibrateAction = nullptr;
    QAction* serialSettingsAction = nullptr;
    
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
    QCheckBox* includeAllCameraCheck = nullptr;
    
    // FID 속성
    QCheckBox* fidMatchCheckBox = nullptr;
    QLabel* fidMatchMethodLabel = nullptr;
    QComboBox* fidMatchMethodCombo = nullptr;
    QLabel* fidMatchThreshLabel = nullptr;
    QCheckBox* fidRotationCheck = nullptr;
    
    // INS 속성
    QLabel* insPassThreshLabel = nullptr;
    QLabel* insMethodLabel = nullptr;
    QCheckBox* insInvertCheck = nullptr;
    QLabel* insThreshLabel = nullptr;
    QLabel* insCompareLabel = nullptr;
    QLabel* insTemplateImg = nullptr;
    QWidget* insBinaryPanel = nullptr;

    // INS 이진화 패널 관련 위젯
    QSpinBox* insThreshSpin = nullptr;
    QDoubleSpinBox* insThresholdSpin = nullptr;
    QLabel* insThresholdLabel = nullptr;
    QLabel* insLowerLabel = nullptr;
    QLabel* insUpperLabel = nullptr;
    QLabel* insRatioTypeLabel = nullptr;

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
    QCheckBox* insStripLengthEnabledCheck = nullptr;
    QLabel* insStripLengthMinLabel = nullptr;
    QSpinBox* insStripLengthMinSpin = nullptr;
    QLabel* insStripLengthMaxLabel = nullptr;
    QSpinBox* insStripLengthMaxSpin = nullptr;
    
    // CRIMP 검사 패널 관련 위젯
    QGroupBox* insCrimpPanel = nullptr;
    
    // STRIP 두께 측정 관련 컨트롤
    // FRONT 두께 측정 관련 위젯들
    QCheckBox* insStripFrontEnabledCheck = nullptr;
    QLabel* insStripThicknessWidthLabel = nullptr;
    QSlider* insStripThicknessWidthSlider = nullptr;
    QLabel* insStripThicknessWidthValueLabel = nullptr;
    QLabel* insStripThicknessHeightLabel = nullptr;
    QSlider* insStripThicknessHeightSlider = nullptr;
    QLabel* insStripThicknessHeightValueLabel = nullptr;
    QLabel* insStripThicknessMinLabel = nullptr;
    QSpinBox* insStripThicknessMinSpin = nullptr;
    QLabel* insStripThicknessMaxLabel = nullptr;
    QSpinBox* insStripThicknessMaxSpin = nullptr;

    // REAR 두께 측정 관련 위젯들
    QCheckBox* insStripRearEnabledCheck = nullptr;
    QLabel* insStripRearThicknessWidthLabel = nullptr;
    QSlider* insStripRearThicknessWidthSlider = nullptr;
    QLabel* insStripRearThicknessWidthValueLabel = nullptr;
    QLabel* insStripRearThicknessHeightLabel = nullptr;
    QSlider* insStripRearThicknessHeightSlider = nullptr;
    QLabel* insStripRearThicknessHeightValueLabel = nullptr;
    QLabel* insStripRearThicknessMinLabel = nullptr;
    QSpinBox* insStripRearThicknessMinSpin = nullptr;
    QLabel* insStripRearThicknessMaxLabel = nullptr;
    QSpinBox* insStripRearThicknessMaxSpin = nullptr;

    // EDGE 검사 관련 위젯들
    QCheckBox* insEdgeEnabledCheck = nullptr;
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
    QLabel* insEdgeDistanceMinLabel = nullptr;
    QSpinBox* insEdgeDistanceMinSpin = nullptr;
    QLabel* insEdgeDistanceMaxLabel = nullptr;
    QSpinBox* insEdgeDistanceMaxSpin = nullptr;
    QLabel* insEdgeStartPercentLabel = nullptr;
    QSpinBox* insEdgeStartPercentSpin = nullptr;
    QLabel* insEdgeEndPercentLabel = nullptr;
    QSpinBox* insEdgeEndPercentSpin = nullptr;

    // SLOPE 검사 관련 위젯들
    QCheckBox* insSlopeEnabledCheck = nullptr;
    QLabel* insSlopeTopToleranceLabel = nullptr;
    QDoubleSpinBox* insSlopeTopToleranceSpin = nullptr;
    QLabel* insSlopeBottomToleranceLabel = nullptr;
    QDoubleSpinBox* insSlopeBottomToleranceSpin = nullptr;

    QLabel* fidAngleLabel = nullptr;
    QLabel* fidToLabel = nullptr;
    // 필터 패널
    QLabel* filterDescLabel = nullptr;
    QLabel* filterInfoLabel = nullptr;
    
    // ROI 속성 위젯들
    QCheckBox* roiIncludeAllCheck = nullptr;

    // FID 속성 위젯들
    QDoubleSpinBox* fidMatchThreshSpin = nullptr;
    QDoubleSpinBox* fidMinAngleSpin = nullptr;
    QDoubleSpinBox* fidMaxAngleSpin = nullptr;
    QDoubleSpinBox* fidStepSpin = nullptr;
    QLabel* fidTemplateImg = nullptr;
    QLabel* fidTemplateImgLabel = nullptr;

    // INS 속성 위젯들
    QDoubleSpinBox* insPassThreshSpin = nullptr;
    QComboBox* insMethodCombo = nullptr; 
    QWidget* insPatternMatchPanel = nullptr;
    QSpinBox* insBinaryThreshSpin = nullptr;
    QCheckBox* insRotationCheck = nullptr;
    QDoubleSpinBox* insMinAngleSpin = nullptr;
    QDoubleSpinBox* insMaxAngleSpin = nullptr;
    QDoubleSpinBox* insAngleStepSpin = nullptr;
    QComboBox* insCompareCombo = nullptr;
    QDoubleSpinBox* insLowerSpin = nullptr;
    QDoubleSpinBox* insUpperSpin = nullptr;
    QComboBox* insRatioTypeCombo = nullptr;

    // 패턴 기본 정보 관련 위젯들
    QLabel* patternIdValue = nullptr;      
    QLineEdit* patternNameEdit = nullptr;  
    QLabel* patternTypeValue = nullptr;      

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
    void setupPatternTree();
    void setupPatternTypeButtons(QVBoxLayout *cameraLayout);
    void setupCameraPreviews(QVBoxLayout *cameraLayout);
    void setupButton(QPushButton* button);

    // ===== 레이아웃 생성 함수 =====
    QVBoxLayout* createMainLayout();
    QHBoxLayout* createContentLayout();
    QVBoxLayout* createCameraLayout();
    QVBoxLayout* createRightPanel();
    QPushButton* createActionButton(const QString &text, const QString &color, const QFont &font);
    
    // ===== 이벤트 연결 함수 =====
    void connectEvents();
    void connectButtonEvents(QPushButton* modeToggleButton, QPushButton* saveRecipeButton,
                           QPushButton* startCameraButton, QPushButton* runStopButton);
    void connectItemChangedEvent();
    void connectPropertyPanelEvents();
    
    // ===== UI 업데이트 함수 =====
    void updatePreviewUI();
    void updatePropertyPanel(PatternInfo* pattern, const FilterInfo* filter, const QUuid& patternId, int filterIndex);
    void updatePropertySpinBoxes(const QRect& rect);
    void createPropertyPanels();

    // ===== 패턴 관리 함수 =====
    QString getPatternName(const QUuid& patternId);
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
    // 처리기 및 로깅 관련
    InsProcessor* insProcessor;
    LogViewer* logViewer;
    
    // 패턴 타입 선택 관련
    QWidget* patternTypeWidget;
    QButtonGroup* patternButtonGroup;
    PatternType currentPatternType;
    
    // UI 위젯 관련
    CameraView *cameraView;
    CustomPatternTreeWidget* patternTree;
    QLabel *zoomValueLabel;
    QVBoxLayout *rightPanelLayout;
    FilterDialog* filterDialog;
    
    // 카메라 관련
    QString cameraStatus;
    QVector<CameraInfo> cameraInfos;
    QVector<QLabel*> cameraPreviewLabels;
    QVector<bool> cameraConnected;
    
    // 카메라 모드 관련 (camOn/camOff)
    QVariantMap backupRecipeData; // 레시피 백업 데이터
    
    // 티칭 모드 관련
    bool teachingEnabled = false;  // 티칭 모드 활성화 상태
    
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
    
    // 패턴 백업 관련 (검사 중지 시 원래 상태로 복원용)
    QMap<QUuid, PatternInfo> originalPatternBackup; // 원본 패턴 정보 백업
    
    // 레시피 메뉴
    QMenu* recipeMenu = nullptr;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif // TEACHINGWIDGET_H