#include "TeachingWidget.h"
#include "SimulationDialog.h"
#include "ImageProcessor.h"
#include "FilterDialog.h"
#include "LogViewer.h"
#include "CameraSettingsDialog.h"
#include "LanguageSettingsDialog.h"
#include "SerialSettingsDialog.h"
#include "SerialCommunication.h"
#include "AITrainer.h"
#include "LanguageManager.h"
#include "RecipeManager.h"
#include "ConfigManager.h"
#include <QTimer>
#include <QProgressDialog>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QInputDialog>
#include <QMessageBox>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

cv::Mat TeachingWidget::getCurrentFrame() const { 
    // **시뮬레이션 모드 처리**
    if (simulationMode && !currentSimulationImage.empty()) {
        return currentSimulationImage; 
    }
    
    // **메인 카메라의 프레임 반환**
    if (cameraIndex >= 0 && cameraIndex < static_cast<int>(cameraFrames.size()) && 
        !cameraFrames[cameraIndex].empty()) {
        return cameraFrames[cameraIndex]; 
    }
    return cv::Mat(); // 빈 프레임 반환
}

cv::Mat TeachingWidget::getCurrentFilteredFrame() const {
    cv::Mat sourceFrame;
    
    // **시뮬레이션 모드 처리**
    if (simulationMode && !currentSimulationImage.empty()) {
        sourceFrame = currentSimulationImage.clone();
    } else if (cameraIndex >= 0 && cameraIndex < static_cast<int>(cameraFrames.size()) && 
               !cameraFrames[cameraIndex].empty()) {
        // **메인 카메라의 프레임 가져오기**
        sourceFrame = cameraFrames[cameraIndex].clone();
    }
    
    if (!sourceFrame.empty()) {
        // 필터 적용 (cameraView의 applyFiltersToImage 함수 사용)
        cameraView->applyFiltersToImage(sourceFrame);
        return sourceFrame;
    }
    
    return cv::Mat();
}

CameraGrabberThread::CameraGrabberThread(QObject* parent)
    : QThread(parent), m_cameraIndex(-1), m_stopped(false), m_paused(false)  // m_camera 제거
{
}

CameraGrabberThread::~CameraGrabberThread()
{
    stopGrabbing();
    wait(); // 스레드가 종료될 때까지 대기
}

void CameraGrabberThread::stopGrabbing()
{
    m_stopped = true;
    m_condition.wakeAll(); // 대기중인 스레드 깨우기
}

void CameraGrabberThread::setPaused(bool paused)
{
    m_paused = paused;
    if (!paused)
        m_condition.wakeAll(); // 일시정지 해제시 스레드 깨우기
}

void CameraGrabberThread::run()
{
    while (!m_stopped)
    {
        // 일시정지 상태 확인
        if (m_paused)
        {
            QMutexLocker locker(&m_mutex);
            m_condition.wait(&m_mutex);
            continue;
        }

        cv::Mat frame;
        bool grabbed = false;

        // **부모 위젯에서 카메라 객체에 직접 접근**
        TeachingWidget* parent = qobject_cast<TeachingWidget*>(this->parent());
        if (parent && m_cameraIndex >= 0) {
            if (parent->isValidCameraIndex(m_cameraIndex)) {
                CameraInfo info = parent->getCameraInfo(m_cameraIndex);
                if (info.capture && info.capture->isOpened()) {
                    grabbed = info.capture->read(frame);
                }
            }
        }

        if (grabbed && !frame.empty())
        {
            emit frameGrabbed(frame, m_cameraIndex);
        }

        // 카메라 프레임 레이트에 맞춰 딜레이
        QThread::msleep(CAMERA_INTERVAL);
    }
}

// UIUpdateThread 구현
UIUpdateThread::UIUpdateThread(QObject* parent)
    : QThread(parent), m_stopped(false), m_paused(false)
{
}

UIUpdateThread::~UIUpdateThread()
{
    stopUpdating();
    wait(); // 스레드가 종료될 때까지 대기
}

void UIUpdateThread::stopUpdating()
{
    m_stopped = true;
    m_condition.wakeAll(); // 대기중인 스레드 깨우기
}

void UIUpdateThread::setPaused(bool paused)
{
    m_paused = paused;
    if (!paused)
        m_condition.wakeAll(); // 일시정지 해제시 스레드 깨우기
}

void UIUpdateThread::run()
{
    while (!m_stopped)
    {
        // 일시정지 상태 확인
        if (m_paused)
        {
            QMutexLocker locker(&m_mutex);
            m_condition.wait(&m_mutex); // 일시정지 상태에서는 대기
            continue;
        }
        
        emit updateUI(); // UI 업데이트 시그널 발생
        
        msleep(CAMERA_INTERVAL);
    }
}

class QObjectEventFilter : public QObject {
    public:
        using FilterFunction = std::function<bool(QObject*, QEvent*)>;
        
        QObjectEventFilter(FilterFunction filter) : filter(filter) {}
        
    protected:
        bool eventFilter(QObject* obj, QEvent* event) override {
            return filter(obj, event);
        }
        
    private:
        FilterFunction filter;
    };    

TeachingWidget::TeachingWidget(int cameraIndex, const QString &cameraStatus, QWidget *parent)
    : QWidget(parent), cameraIndex(cameraIndex), cameraStatus(cameraStatus)
#ifdef USE_SPINNAKER
    , m_useSpinnaker(false)
#endif
{
    // 언어 시스템을 가장 먼저 초기화
    initializeLanguageSystem();
    
    // cv::Mat 타입을 메타타입으로 등록 (시그널/슬롯에서 사용 가능)
    qRegisterMetaType<cv::Mat>("cv::Mat");

    #ifdef USE_SPINNAKER
        // Spinnaker SDK 초기화 시도
        m_useSpinnaker = initSpinnakerSDK();
        if (m_useSpinnaker) {
        } else {
        }
    #endif
    
    // 기본 초기화 및 설정
    initBasicSettings();
    
    // 레시피 관리자 초기화
    recipeManager = new RecipeManager();
    
    // 로그 뷰어 초기화
    logViewer = new LogViewer(this);
    logViewer->setWindowFlag(Qt::Window); 
    connect(insProcessor, &InsProcessor::logMessage, logViewer, &LogViewer::receiveLogMessage);
    
    // 레이아웃 구성
    QVBoxLayout *mainLayout = createMainLayout();
    QHBoxLayout *contentLayout = createContentLayout();
    mainLayout->addLayout(contentLayout);
    
    // 왼쪽 패널 (카메라 뷰 및 컨트롤) 설정
    QVBoxLayout *cameraLayout = createCameraLayout();
    contentLayout->addLayout(cameraLayout, 2); // 2:1 비율로 왼쪽 패널이 더 크게
    
    // 오른쪽 패널 (패턴 및 필터 컨트롤) 설정
    rightPanelLayout = createRightPanel();
    contentLayout->addLayout(rightPanelLayout, 1);
    
    // 패턴 테이블 설정
    setupPatternTree();
    
    // 프로퍼티 패널 생성
    createPropertyPanels();
    
    // 카메라 포인터 초기화
    // camera = nullptr; 
    
    // 필터 다이얼로그 초기화
    filterDialog = new FilterDialog(cameraView, -1, this);
    
    // 이벤트 연결
    connectEvents();

    // 캘리브레이션 도구 설정
    setupCalibrationTools();

    uiUpdateThread = new UIUpdateThread(this);
    
    // UI 업데이트 이벤트 연결
    connect(uiUpdateThread, &UIUpdateThread::updateUI,
            this, &TeachingWidget::updateUIElements, Qt::QueuedConnection);

    // 언어 변경 시그널 연결 (즉시 처리)
    connect(LanguageManager::instance(), &LanguageManager::languageChanged, 
            this, &TeachingWidget::updateUITexts, Qt::DirectConnection);
     
    // UI 텍스트 초기 갱신
    QTimer::singleShot(100, this, &TeachingWidget::updateUITexts);
    
    // 최근 사용한 레시피 자동 로드는 카메라가 시작될 때만 수행
    // QTimer::singleShot(500, this, &TeachingWidget::loadLastRecipe); // 제거됨
}

void TeachingWidget::initializeLanguageSystem() {
    // ConfigManager에서 설정 로드
    ConfigManager::instance()->loadConfig();
    
    // 언어 파일 경로 찾기
    QStringList possiblePaths = {
        QCoreApplication::applicationDirPath() + "/" + QString(LANGUAGE_FILE),
        QString(LANGUAGE_FILE),
        QString("build/") + QString(LANGUAGE_FILE)
    };
    
    QString languageFile;
    for (const QString& path : possiblePaths) {
        if (QFile::exists(path)) {
            languageFile = path;
            break;
        }
    }
    
    // 언어 파일 로드
    if (!languageFile.isEmpty()) {
        LanguageManager::instance()->loadLanguage(languageFile);
        // ConfigManager에서 저장된 언어 설정 사용
        QString savedLanguage = ConfigManager::instance()->getLanguage();
        LanguageManager::instance()->setCurrentLanguage(savedLanguage);
        qDebug() << "[TeachingWidget] 저장된 언어 설정 적용:" << savedLanguage;
    }
    
    // ConfigManager의 언어 변경 시그널 연결
    connect(ConfigManager::instance(), &ConfigManager::languageChanged,
            this, [this](const QString& newLanguage) {
                LanguageManager::instance()->setCurrentLanguage(newLanguage);
                qDebug() << "[TeachingWidget] 언어 변경됨:" << newLanguage;
            });
}

void TeachingWidget::showCameraSettings() {
    // 카메라 스레드가 실행 중인지 확인
    if (!cameraThreads.isEmpty()) {
        UIColors::showWarning(this, "카메라 설정", 
            "카메라가 실행 중입니다.\n카메라를 중지한 후 다시 시도해주세요.");
        return;
    }
    
    // 카메라 정보 업데이트
    detectCameras();
    
    // 카메라가 없으면 경고
    if (cameraInfos.isEmpty()) {
        UIColors::showWarning(this, "카메라 설정", "연결된 카메라가 없습니다.");
        return;
    }
    
    // **현재 카메라 인덱스 유효성 검사 및 수정**
    if (cameraIndex < 0 || cameraIndex >= cameraInfos.size()) {
        cameraIndex = 0; // 첫 번째 카메라로 초기화
    }
    
    // 카메라 설정 다이얼로그 생성
    CameraSettingsDialog dialog(cameraInfos, this);
    
    // 현재 레시피에 사용되는 카메라 UUID 설정
    QString currentRecipeCameraUuid = cameraInfos[cameraIndex].uniqueId;
    dialog.setRecipeCamera(currentRecipeCameraUuid);
    
    // 레시피 재할당 시그널 연결
    connect(&dialog, &CameraSettingsDialog::recipeReassigned, this, 
            [this](int sourceIndex, int targetIndex, int methodCode, 
                  const QString& sourceUuid, const QString& targetUuid) {
        
        // 인덱스 유효성 검사
        if (!isValidCameraIndex(sourceIndex) || !isValidCameraIndex(targetIndex) || sourceIndex == targetIndex) {
            return;
        }
        
        // 레시피 관리 로직
        QList<PatternInfo> patterns = cameraView->getPatterns();
        
        if (methodCode == 0) {  // 복사
            for (const PatternInfo& pattern : patterns) {
                if (pattern.cameraUuid == sourceUuid) {
                    PatternInfo newPattern = pattern;
                    newPattern.id = QUuid::createUuid();
                    newPattern.cameraUuid = targetUuid;
                    cameraView->addPattern(newPattern);
                }
            }
        } else if (methodCode == 1) {  // 교환
            for (PatternInfo& pattern : patterns) {
                if (pattern.cameraUuid == sourceUuid) {
                    // ★★★ CameraView에서 최신 패턴 정보 가져와서 동기화 ★★★
                    PatternInfo* latestPattern = cameraView->getPatternById(pattern.id);
                    if (latestPattern) {
                        latestPattern->cameraUuid = targetUuid;
                        cameraView->updatePatternById(pattern.id, *latestPattern);
                        // TeachingWidget 배열도 동기화
                        pattern = *latestPattern;
                    }
                } else if (pattern.cameraUuid == targetUuid) {
                    // ★★★ CameraView에서 최신 패턴 정보 가져와서 동기화 ★★★
                    PatternInfo* latestPattern = cameraView->getPatternById(pattern.id);
                    if (latestPattern) {
                        latestPattern->cameraUuid = sourceUuid;
                        cameraView->updatePatternById(pattern.id, *latestPattern);
                        // TeachingWidget 배열도 동기화
                        pattern = *latestPattern;
                    }
                }
            }
        } else if (methodCode == 2) {  // 이동
            for (PatternInfo& pattern : patterns) {
                if (pattern.cameraUuid == sourceUuid) {
                    // ★★★ CameraView에서 최신 패턴 정보 가져와서 동기화 ★★★
                    PatternInfo* latestPattern = cameraView->getPatternById(pattern.id);
                    if (latestPattern) {
                        latestPattern->cameraUuid = targetUuid;
                        cameraView->updatePatternById(pattern.id, *latestPattern);
                        // TeachingWidget 배열도 동기화
                        pattern = *latestPattern;
                    }
                }
            }
        }
        
        // UI 업데이트
        updatePatternTree();
        cameraView->update();
        
        QString methodText = (methodCode == 0) ? "복사" : (methodCode == 1) ? "교환" : "이동";
        UIColors::showInformation(this, "완료", 
            QString("카메라 %1의 레시피가 카메라 %2로 %3되었습니다.")
            .arg(sourceIndex + 1).arg(targetIndex + 1).arg(methodText));
    });
    
    // 다이얼로그 실행
    dialog.exec();
}

void TeachingWidget::deleRecipe() {
   // 현재 카메라 정보 확인
    if (cameraInfos.isEmpty() || cameraIndex < 0 || cameraIndex >= cameraInfos.size()) {
        UIColors::showWarning(this, "레시피 삭제 오류", "연결된 카메라가 없습니다.");
        return;
    }

    // 삭제 확인 메시지 표시
    QString cameraName = cameraInfos[cameraIndex].name;
    QString message = QString("현재 카메라(%1)의 모든 패턴과 레시피가 삭제됩니다.\n계속하시겠습니까?").arg(cameraName);
    
    QMessageBox::StandardButton reply = UIColors::showQuestion(this, "레시피 삭제 확인", 
                                                              message,
                                                              QMessageBox::Yes | QMessageBox::No,
                                                              QMessageBox::No);
    
    if (reply != QMessageBox::Yes) {
        return;  // 사용자가 취소함
    }
    
    // 패턴 트리 비우기
    patternTree->clear();
    
    // 현재 카메라에 해당하는 모든 패턴 찾기
    QList<QUuid> patternsToRemove;
    QString currentCameraUuid;
    if (isValidCameraIndex(cameraIndex)) {
        currentCameraUuid = getCameraInfo(cameraIndex).uniqueId;
    }
    
    const QList<PatternInfo>& allPatterns = cameraView->getPatterns();
    for (const PatternInfo& pattern : allPatterns) {
        if (pattern.cameraUuid == currentCameraUuid) {
            patternsToRemove.append(pattern.id);
        }
    }
    
    // 패턴 삭제 (CameraView에서)
    for (const QUuid& id : patternsToRemove) {
        cameraView->removePattern(id);
    }
    
    // 속성 패널 초기화
    if (propertyStackWidget) {
        propertyStackWidget->setCurrentIndex(0);
    }
    
    // 캘리브레이션 정보도 초기화
    CalibrationInfo emptyCalib;
    cameraView->setCalibrationInfo(emptyCalib);
    
    // **현재 카메라의 패턴만 삭제했으므로 레시피 파일 전체를 삭제하지 않음**
    // 대신 수정된 레시피를 다시 저장
    saveRecipe();
    
    // 삭제 완료 메시지
    UIColors::showInformation(this, "레시피 삭제 완료", 
                           QString("현재 카메라(%1)의 모든 패턴이 삭제되었습니다.\n레시피 파일이 업데이트되었습니다.").arg(cameraName));
    
    // 카메라 뷰 업데이트
    cameraView->update();
}

void TeachingWidget::openRecipe(bool autoMode) {
    QStringList availableRecipes = recipeManager->getAvailableRecipes();
    
    if (availableRecipes.isEmpty()) {
        if (!autoMode) {
            UIColors::showInformation(this, "레시피 없음", "사용 가능한 레시피가 없습니다.");
        } else {
            qDebug() << "사용 가능한 레시피가 없습니다.";
        }
        return;
    }
    
    QString selectedRecipe;
    
    if (autoMode) {
        // 자동 모드: 최근 레시피 또는 첫 번째 레시피 선택
        QString lastRecipePath = ConfigManager::instance()->getLastRecipePath();
        
        if (!lastRecipePath.isEmpty() && availableRecipes.contains(lastRecipePath)) {
            selectedRecipe = lastRecipePath;
            qDebug() << QString("최근 사용한 레시피 '%1'을 자동 로드합니다.").arg(selectedRecipe);
        } else {
            selectedRecipe = availableRecipes.first();
            qDebug() << QString("최근 레시피가 없어 첫 번째 레시피 '%1'을 로드합니다.").arg(selectedRecipe);
        }
    } else {
        // 수동 모드: 사용자 선택
        bool ok;
        selectedRecipe = QInputDialog::getItem(this, "레시피 열기", 
            "열려는 레시피를 선택하세요:", availableRecipes, 0, false, &ok);
        
        if (!ok || selectedRecipe.isEmpty()) {
            return;
        }
    }
    
    // 공통 로드 로직
    qDebug() << QString("레시피 로드 시작: %1 (자동모드: %2)").arg(selectedRecipe).arg(autoMode);
    
    // 레시피 파일 경로 생성
    QString recipeFileName = QDir(recipeManager->getRecipesDirectory()).absoluteFilePath(QString("%1/%1.xml").arg(selectedRecipe));
    
    // 빈 캘리브레이션 맵 (로드 시 채워짐)
    QMap<QString, CalibrationInfo> calibrationMap;
    
    // 기존 loadRecipe 함수 사용
    if (recipeManager->loadRecipe(recipeFileName, cameraInfos, calibrationMap, cameraView, patternTree)) {
        qDebug() << QString("loadRecipe 성공");
        
        // 현재 레시피 이름 설정 (Save 버튼으로 저장할 때 사용)
        currentRecipeName = selectedRecipe;
        hasUnsavedChanges = false;
        qDebug() << QString("현재 레시피 이름 설정: %1").arg(currentRecipeName);
            
        if (autoMode) {
            qDebug() << QString("레시피 '%1' 자동 로드 성공").arg(selectedRecipe);
            // 최근 사용한 레시피로 저장
            ConfigManager::instance()->setLastRecipePath(selectedRecipe);
            ConfigManager::instance()->saveConfig();
        } else {
            UIColors::showInformation(this, "레시피 로드 성공", 
                QString("레시피 '%1'이 성공적으로 로드되었습니다.").arg(selectedRecipe));
        }
    } else {
        qDebug() << QString("loadRecipe 실패");
        if (autoMode) {
            qDebug() << "레시피 로드에 실패했습니다.";
        } else {
            QMessageBox::critical(this, "로드 실패", "레시피 로드에 실패했습니다.");
        }
    }
}

void TeachingWidget::initBasicSettings() {
    insProcessor = new InsProcessor(this);
    
    // AI 트레이너 초기화
    aiTrainer = new AITrainer(this);
    
    // 8개 카메라 미리보기를 고려하여 크기 확장
    setMinimumSize(1280, 800);
    patternColors << QColor("#FF5252") << QColor("#448AFF") << QColor("#4CAF50") 
                  << QColor("#FFC107") << QColor("#9C27B0") << QColor("#00BCD4")
                  << QColor("#FF9800") << QColor("#607D8B") << QColor("#E91E63");
    setFocusPolicy(Qt::StrongFocus);
}

QVBoxLayout* TeachingWidget::createMainLayout() {
        QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSpacing(5);
    
    // 메뉴바 생성
    menuBar = new QMenuBar(this);
    
    // 파일 메뉴
    fileMenu = menuBar->addMenu(TR("FILE_MENU"));

    // 종료 액션만 추가
    exitAction = fileMenu->addAction(TR("EXIT"));

    // === 레시피 메뉴 추가 ===
    recipeMenu = menuBar->addMenu("레시피");
    recipeMenu->setEnabled(true);
    
    // 레시피 액션들 생성
    QAction* newRecipeAction = recipeMenu->addAction("새 레시피");
    QAction* saveRecipeAsAction = recipeMenu->addAction("다른 이름으로 저장");
    QAction* saveCurrentRecipeAction = recipeMenu->addAction("현재 레시피 저장");
    recipeMenu->addSeparator();
    QAction* manageRecipesAction = recipeMenu->addAction("레시피 관리");
    
    // 레시피 액션들 연결
    connect(newRecipeAction, &QAction::triggered, this, &TeachingWidget::newRecipe);
    connect(saveRecipeAsAction, &QAction::triggered, this, &TeachingWidget::saveRecipeAs);
    connect(saveCurrentRecipeAction, &QAction::triggered, this, &TeachingWidget::saveRecipe);
    connect(manageRecipesAction, &QAction::triggered, this, &TeachingWidget::manageRecipes);

    // 설정 메뉴
    settingsMenu = menuBar->addMenu(TR("SETTINGS_MENU"));
    settingsMenu->setEnabled(true);

    cameraSettingsAction = settingsMenu->addAction(TR("CAMERA_RECIPE_SETTINGS"));
    cameraSettingsAction->setEnabled(true);

    languageSettingsAction = settingsMenu->addAction(TR("LANGUAGE_SETTINGS"));
    languageSettingsAction->setEnabled(true);

    // 시뮬레이션 메뉴 (바로 실행되는 단일 액션)
    simulateMenu = menuBar->addMenu(TR("SIMULATE_MENU"));
    simulateMenu->setEnabled(true);
    
    // 시뮬레이션 열기 액션 추가
    simulateAction = simulateMenu->addAction(TR("OPEN_SIMULATION"));
    simulateAction->setEnabled(true);
    connect(simulateAction, &QAction::triggered, this, &TeachingWidget::showSimulationDialog);

    // 도구 메뉴
    toolsMenu = menuBar->addMenu(TR("TOOLS_MENU"));

    // 캘리브레이션 도구 액션 추가
    calibrateAction = toolsMenu->addAction(TR("LENGTH_CALIBRATION"));
    
    // 시리얼 설정 액션 추가
    serialSettingsAction = toolsMenu->addAction(TR("SERIAL_SETTINGS"));

    // 도움말 메뉴
    helpMenu = menuBar->addMenu(TR("HELP_MENU"));
    helpMenu->setEnabled(true);
    
    // macOS에서 시스템 메뉴로 인식되지 않도록 설정
    helpMenu->menuAction()->setMenuRole(QAction::NoRole);

    aboutAction = helpMenu->addAction(TR("ABOUT"));
    aboutAction->setEnabled(true);
    
    // About 액션도 시스템 About으로 인식되지 않도록 설정
    aboutAction->setMenuRole(QAction::NoRole);
    aboutAction->setEnabled(true);  // 기본 활성화

    // 메뉴 액션 연결
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    connect(cameraSettingsAction, &QAction::triggered, this, &TeachingWidget::showCameraSettings);
    connect(languageSettingsAction, &QAction::triggered, this, &TeachingWidget::openLanguageSettings);
    connect(serialSettingsAction, &QAction::triggered, this, &TeachingWidget::showSerialSettings);
    connect(aboutAction, &QAction::triggered, this, &TeachingWidget::showAboutDialog);
    
    // 메뉴바 추가
    layout->setMenuBar(menuBar);
    // 헤더 부분 - 제목과 버튼들
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(5, 5, 5, 5);
    headerLayout->setSpacing(20);
    
    // 버튼 폰트 설정
    QFont buttonFont = QFont("Arial", 14, QFont::Bold);
    
    // 버튼 설정 헬퍼 함수
    auto setupHeaderButton = [&buttonFont](QPushButton* button) {
        button->setFont(buttonFont);
    };
    
    // 1. ROI/FID/INS 패턴 타입 버튼들 - 첫 번째 그룹
    QHBoxLayout* patternTypeLayout = new QHBoxLayout();
    patternTypeLayout->setSpacing(10);
    patternTypeLayout->setContentsMargins(0, 0, 0, 0);
    
    roiButton = new QPushButton(TR("ROI"), this);
    fidButton = new QPushButton(TR("FID"), this);
    insButton = new QPushButton(TR("INS"), this);
    
    // 체크 가능 설정
    roiButton->setCheckable(true);
    fidButton->setCheckable(true);
    insButton->setCheckable(true);
    
    // 스타일 설정
    setupHeaderButton(roiButton);
    setupHeaderButton(fidButton);
    setupHeaderButton(insButton);
    
    // 스타일시트 적용
    roiButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::ROI_COLOR, UIColors::ROI_COLOR, roiButton->isChecked()));
    fidButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::FIDUCIAL_COLOR, UIColors::FIDUCIAL_COLOR, fidButton->isChecked()));
    insButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::INSPECTION_COLOR, UIColors::INSPECTION_COLOR, insButton->isChecked()));
        
    // 버튼 그룹으로 묶기
    patternButtonGroup = new QButtonGroup(this);
    patternButtonGroup->addButton(roiButton, static_cast<int>(PatternType::ROI));
    patternButtonGroup->addButton(fidButton, static_cast<int>(PatternType::FID));
    patternButtonGroup->addButton(insButton, static_cast<int>(PatternType::INS));
    patternButtonGroup->setExclusive(true);
    
    // 초기 상태 설정
    roiButton->setChecked(true);
    currentPatternType = PatternType::ROI;
    
    // 패턴 타입 레이아웃에 추가
    patternTypeLayout->addWidget(roiButton);
    patternTypeLayout->addWidget(fidButton);
    patternTypeLayout->addWidget(insButton);
    
    // 2. 토글 버튼 그룹 (DRAW/MOVE, CAM, RUN) - 두 번째 그룹
    QHBoxLayout* toggleButtonLayout = new QHBoxLayout();
    toggleButtonLayout->setSpacing(10);
    toggleButtonLayout->setContentsMargins(0, 0, 0, 0);
    
    // DRAW/MOVE 모드 토글 버튼
    modeToggleButton = new QPushButton("DRAW", this);
    modeToggleButton->setObjectName("modeToggleButton");
    modeToggleButton->setCheckable(true);
    modeToggleButton->setChecked(true); // 기본값 DRAW 모드
    setupHeaderButton(modeToggleButton);
    modeToggleButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_MOVE_COLOR, UIColors::BTN_DRAW_COLOR, true));

    // CAM START/STOP 버튼
    startCameraButton = new QPushButton("CAM OFF", this);
    startCameraButton->setCheckable(true);
    setupHeaderButton(startCameraButton);
    startCameraButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_CAM_OFF_COLOR, UIColors::BTN_CAM_ON_COLOR, false));

    // RUN 버튼 - 일반 푸시 버튼으로 변경
    runStopButton = new QPushButton("RUN", this);
    runStopButton->setObjectName("runStopButton");
    runStopButton->setCheckable(true); // 토글 버튼으로 변경
    setupHeaderButton(runStopButton);
    runStopButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));
    
    // 토글 버튼 레이아웃에 추가
    toggleButtonLayout->addWidget(modeToggleButton);
    toggleButtonLayout->addWidget(startCameraButton);
    toggleButtonLayout->addWidget(runStopButton);
    
    // 3. 액션 버튼 그룹 (SAVE, 패턴추가, 패턴삭제, 필터추가) - 세 번째 그룹
    QHBoxLayout* actionButtonLayout = new QHBoxLayout();
    actionButtonLayout->setSpacing(10);
    actionButtonLayout->setContentsMargins(0, 0, 0, 0);
    
    // SAVE 버튼
    QPushButton* saveRecipeButton = new QPushButton("SAVE", this);
    saveRecipeButton->setObjectName("saveRecipeButton");
    setupHeaderButton(saveRecipeButton);
    saveRecipeButton->setStyleSheet(UIColors::buttonStyle(UIColors::BTN_SAVE_COLOR));
    
    // 패턴 추가 버튼
    QPushButton* addPatternButton = new QPushButton("ADD", this);
    addPatternButton->setObjectName("addPatternButton");
    setupHeaderButton(addPatternButton);
    addPatternButton->setStyleSheet(UIColors::buttonStyle(UIColors::BTN_ADD_COLOR));
    
    // 필터 추가 버튼
    QPushButton* addFilterButton = new QPushButton("FILTER", this);
    addFilterButton->setObjectName("addFilterButton");
    setupHeaderButton(addFilterButton);
    addFilterButton->setStyleSheet(UIColors::buttonStyle(UIColors::BTN_FILTER_COLOR));

    // 패턴 삭제 버튼
    QPushButton* removeButton = new QPushButton("DELETE", this);
    removeButton->setObjectName("removeButton");
    removeButton->setEnabled(false);
    setupHeaderButton(removeButton);
    removeButton->setStyleSheet(UIColors::buttonStyle(UIColors::BTN_REMOVE_COLOR));
    
    if (!removeButton->isEnabled()) {
        removeButton->setStyleSheet(UIColors::buttonStyle(UIColors::BTN_REMOVE_COLOR));
    }
    
    // 액션 버튼 레이아웃에 추가
    actionButtonLayout->addWidget(saveRecipeButton);
    actionButtonLayout->addWidget(addPatternButton);
    actionButtonLayout->addWidget(addFilterButton);
    actionButtonLayout->addWidget(removeButton);
    
    // 모든 버튼 그룹을 헤더 레이아웃에 추가
    headerLayout->addLayout(patternTypeLayout);
    headerLayout->addLayout(toggleButtonLayout);
    headerLayout->addLayout(actionButtonLayout);
    headerLayout->addStretch(1);
    
    // 이벤트 연결
    connectButtonEvents(modeToggleButton, saveRecipeButton, startCameraButton, runStopButton);
    connect(addPatternButton, &QPushButton::clicked, this, &TeachingWidget::addPattern);
    connect(removeButton, &QPushButton::clicked, this, &TeachingWidget::removePattern);
    connect(addFilterButton, &QPushButton::clicked, this, &TeachingWidget::addFilter);
    
    // 헤더 레이아웃을 메인 레이아웃에 추가
    layout->addLayout(headerLayout);
    
    // 구분선 전에 공간 추가 - 버튼과 구분선 사이 여백
    layout->addSpacing(15);
    
    // 구분선 추가
    QFrame* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setMinimumHeight(2); // 구분선 높이 설정
    layout->addWidget(line);
    
    // 구분선 아래 여백 추가
    layout->addSpacing(10);
    
    return layout;
}

QHBoxLayout* TeachingWidget::createContentLayout() {
    QHBoxLayout *layout = new QHBoxLayout();
    layout->setSpacing(5); // 간격 줄이기
    return layout;
}

QVBoxLayout* TeachingWidget::createCameraLayout() {
    QVBoxLayout *cameraLayout = new QVBoxLayout();
    cameraLayout->setSpacing(5);
    
    // 1. 카메라 뷰 초기화 및 추가
    cameraView = new CameraView(this);
    cameraLayout->addWidget(cameraView);
    
    // 2. 패턴 타입 버튼 추가
    setupPatternTypeButtons(cameraLayout);
    
    // 3. 카메라 미리보기 영역 추가
    setupCameraPreviews(cameraLayout);
    
    return cameraLayout;
}

void TeachingWidget::setupButton(QPushButton* button) {
    button->setMinimumSize(40, 40);
    button->setMaximumSize(80, 40);
    button->setIconSize(QSize(20, 20));
}

void TeachingWidget::setupPatternTypeButtons(QVBoxLayout *cameraLayout) {
    if (cameraView) {
        cameraView->setEditMode(CameraView::EditMode::Draw);  // 기본 모드: DRAW
        cameraView->setCurrentDrawColor(UIColors::ROI_COLOR); // 초기값: ROI (노란색)
    }
}

void TeachingWidget::connectButtonEvents(QPushButton* modeToggleButton, QPushButton* saveRecipeButton,
                                         QPushButton* startCameraButton, QPushButton* runStopButton) {
    connect(modeToggleButton, &QPushButton::toggled, this, [this, modeToggleButton](bool checked) {
        if (cameraView) {
            CameraView::EditMode newMode = checked ? CameraView::EditMode::Draw : CameraView::EditMode::Move;
            cameraView->setEditMode(newMode);
            
            // 버튼 텍스트 및 스타일 업데이트
                if (checked) {
                // DRAW 모드
                modeToggleButton->setText(TR("DRAW"));
                // 오렌지색(DRAW)과 블루바이올렛(MOVE) 색상 사용 - DRAW 모드에서는 오렌지색이 적용됨
                modeToggleButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_MOVE_COLOR, UIColors::BTN_DRAW_COLOR, true));
            } else {
                // MOVE 모드
                modeToggleButton->setText(TR("MOVE"));
                // 오렌지색(DRAW)과 블루바이올렛(MOVE) 색상 사용 - MOVE 모드에서는 블루바이올렛이 적용됨
                modeToggleButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_MOVE_COLOR, UIColors::BTN_DRAW_COLOR, false));
            }
        }
    });
    
    connect(runStopButton, &QPushButton::toggled, this, [this](bool checked) {
        QPushButton* btn = qobject_cast<QPushButton*>(sender());
        if (btn) {
            if (checked) {
                // **RUN 버튼 눌림 - 검사 모드로 전환**
                
                // 1. 기본 안전성 검사
                if (!cameraView || !insProcessor) {
                    btn->blockSignals(true);
                    btn->setChecked(false);
                    btn->blockSignals(false);
                    UIColors::showWarning(this, "오류", "시스템이 초기화되지 않았습니다.");
                    return;
                }
                
                // 2. 카메라 및 프레임 확인 (시뮬레이션 모드 고려)
                if (simulationMode) {
                    // 시뮬레이션 모드: 현재 시뮬레이션 이미지가 있는지 확인
                    if (!cameraView || currentSimulationImage.empty()) {
                        btn->blockSignals(true);
                        btn->setChecked(false);
                        btn->blockSignals(false);
                        UIColors::showWarning(this, "검사 실패", "시뮬레이션 이미지가 없습니다. 시뮬레이션 다이얼로그에서 이미지를 선택해주세요.");
                        return;
                    }
                } else {
                    // 실제 카메라 모드: 카메라 프레임 확인
                    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(cameraFrames.size()) || 
                        cameraFrames[cameraIndex].empty()) {
                        btn->blockSignals(true);
                        btn->setChecked(false);
                        btn->blockSignals(false);
                        UIColors::showWarning(this, "검사 실패", "카메라 영상이 없습니다. 카메라를 시작해주세요.");
                        return;
                    }
                }
                
                // 3. 패턴 확인 (시뮬레이션 모드 고려)
                QList<PatternInfo> patterns = cameraView->getPatterns();
                bool hasEnabledPatterns = false;
                QString currentCameraUuid;
                
                if (simulationMode) {
                    // 시뮬레이션 모드: 현재 시뮬레이션 카메라 UUID 사용
                    currentCameraUuid = cameraView->getCurrentCameraUuid();
                } else {
                    // 실제 카메라 모드: 카메라 인덱스로 UUID 가져오기
                    if (isValidCameraIndex(cameraIndex)) {
                        currentCameraUuid = getCameraInfo(cameraIndex).uniqueId;
                    }
                }
                
                for (const PatternInfo& pattern : patterns) {
                    if (pattern.enabled && pattern.cameraUuid == currentCameraUuid) {
                        hasEnabledPatterns = true;
                        break;
                    }
                }
                
                if (!hasEnabledPatterns) {
                    btn->blockSignals(true);
                    btn->setChecked(false);
                    btn->blockSignals(false);
                    UIColors::showWarning(this, "검사 실패", "활성화된 패턴이 없습니다. 패턴을 추가하고 활성화하세요.");
                    return;
                }
                
                QApplication::processEvents();
                
                // **4. 패턴 원본 정보 백업 (검사 중지 시 복원용)**
                originalPatternBackup.clear();
                for (const PatternInfo& pattern : patterns) {
                    originalPatternBackup[pattern.id] = pattern;
                }
                qDebug() << QString("[검사 시작] %1개 패턴 백업 완료").arg(originalPatternBackup.size());
                
                // **5. 로그 뷰어 표시**
                if (logViewer) {
                    logViewer->show();
                }
                
                // **6. 검사 모드 활성화**
                if (cameraView) {
                    cameraView->setInspectionMode(true);
                }
                
                // **7. 검사 실행 - 현재 프레임 또는 시뮬레이션 이미지로**
                try {
                    cv::Mat inspectionFrame;
                    int inspectionCameraIndex;
                    
                    if (simulationMode) {
                        // 시뮬레이션 모드: 현재 시뮬레이션 이미지 사용
                        if (currentSimulationImage.empty()) {
                            btn->blockSignals(true);
                            btn->setChecked(false);
                            btn->blockSignals(false);
                            UIColors::showWarning(this, "검사 실패", "시뮬레이션 이미지가 없습니다.");
                            return;
                        }
                        inspectionFrame = currentSimulationImage.clone();
                        inspectionCameraIndex = -1; // 시뮬레이션 모드 표시
                    } else {
                        // 실제 카메라 모드: 현재 프레임 사용
                        inspectionFrame = cameraFrames[cameraIndex].clone();
                        inspectionCameraIndex = cameraIndex;
                    }
                    
                    bool passed = runInspection(inspectionFrame, inspectionCameraIndex);
                    
                    // **8. 버튼 상태 업데이트**
                    btn->setText(TR("STOP"));
                    btn->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_REMOVE_COLOR, QColor("#FF5722"), true));
                    
                } catch (const std::exception& e) {
                    // 오류 발생 시 라이브 모드로 복귀
                    resumeToLiveMode();

                    
                    btn->blockSignals(true);
                    btn->setChecked(false);
                    btn->blockSignals(false);
                    return;
                } catch (...) {                   
                    // 오류 발생 시 라이브 모드로 복귀
                    resumeToLiveMode();

                    
                    btn->blockSignals(true);
                    btn->setChecked(false);
                    btn->blockSignals(false);
                    UIColors::showCritical(this, "검사 오류", "검사 실행 중 알 수 없는 오류가 발생했습니다.");
                    return;
                }
                
            } else {
                // **STOP 버튼 눌림 - 라이브 모드로 복귀**
            
                try {
                    resumeToLiveMode();
                    
                    // 버튼 상태 복원
                    btn->setText(TR("RUN"));
                    btn->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));
                    
                    
                } catch (const std::exception& e) {
                    btn->blockSignals(true);
                    btn->setChecked(true);
                    btn->blockSignals(false);
                } catch (...) {
                    btn->blockSignals(true);
                    btn->setChecked(true);
                    btn->blockSignals(false);
                }
            }
        }
    });
    
    // 저장 버튼 이벤트
    connect(saveRecipeButton, &QPushButton::clicked, this, &TeachingWidget::saveRecipe);
    
    // 카메라 시작/정지 토글 이벤트
    connect(startCameraButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            // 카메라 시작
            startCamera(); 
        } else {
            // 카메라 중지
            stopCamera();
        }
    });
        
    // 패턴 타입 버튼 그룹 이벤트
    connect(patternButtonGroup, &QButtonGroup::idClicked, this, [this](int id) {
        currentPatternType = static_cast<PatternType>(id);
        
        // 버튼 스타일 업데이트
        roiButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::ROI_COLOR, UIColors::ROI_COLOR, roiButton->isChecked()));
        fidButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::FIDUCIAL_COLOR, UIColors::FIDUCIAL_COLOR, fidButton->isChecked()));
        insButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::INSPECTION_COLOR, UIColors::INSPECTION_COLOR, insButton->isChecked()));
        
        // 디버깅: 패턴 버튼 클릭 확인
        QString typeName;
        switch (currentPatternType) {
            case PatternType::ROI: typeName = "ROI"; break;
            case PatternType::FID: typeName = "FID"; break;
            case PatternType::INS: typeName = "INS"; break;
            case PatternType::FIL: typeName = "Filter"; break;
        }
        
        QColor drawColor;
        QString patternTypeText;
        switch (currentPatternType) {
            case PatternType::ROI:
                drawColor = UIColors::ROI_COLOR; 
                break;
            case PatternType::FID:
                drawColor = UIColors::FIDUCIAL_COLOR;
                break;
            case PatternType::INS:
                drawColor = UIColors::INSPECTION_COLOR;
                break;
            case PatternType::FIL:
                drawColor = UIColors::FILTER_COLOR;
                break;
        }
        cameraView->setCurrentDrawColor(drawColor);
        
        // 패턴 버튼이 클릭되면 CameraView를 Draw 모드로 전환
        cameraView->setEditMode(CameraView::EditMode::Draw);
    });
    

    // 레시피 불러오기 버튼 이벤트 연결
    if (loadRecipeAction) {
        connect(loadRecipeAction, &QAction::triggered, this, [this]() {
            QString fileName = QFileDialog::getOpenFileName(this, 
                "레시피 불러오기", 
                "", 
                "레시피 파일 (*.config);;모든 파일 (*)");
                
            if (!fileName.isEmpty()) {
                loadRecipe(fileName);
            }
        });
    }
}

void TeachingWidget::updateFilterParam(const QUuid& patternId, int filterIndex, const QString& paramName, int value) {
    PatternInfo* pattern = cameraView->getPatternById(patternId);
    if (!pattern || filterIndex < 0 || filterIndex >= pattern->filters.size()) {
        return;
    }

    // 이전 값과 비교 (변경되었는지 확인)
    int oldValue = pattern->filters[filterIndex].params.value(paramName, -1);
    if (oldValue == value) {
        return; // 변경 없으면 종료
    }
    
    // 필터 파라미터 업데이트
    pattern->filters[filterIndex].params[paramName] = value;
    
    // 컨투어 필터 특별 처리
    if (pattern->filters[filterIndex].type == FILTER_CONTOUR) {
        // 필터가 적용된 현재 프레임 가져오기
        cv::Mat filteredFrame = getCurrentFilteredFrame();
        if (!filteredFrame.empty()) {
            // ROI 영역 추출
            cv::Rect roi(pattern->rect.x(), pattern->rect.y(), 
                       pattern->rect.width(), pattern->rect.height());
            
            if (roi.x >= 0 && roi.y >= 0 && 
                roi.x + roi.width <= filteredFrame.cols &&
                roi.y + roi.height <= filteredFrame.rows) {
                
                // ROI 영역 잘라내기
                cv::Mat roiMat = filteredFrame(roi).clone();
                
                // 필터 파라미터 가져오기
                int threshold = pattern->filters[filterIndex].params.value("threshold", 128);
                int minArea = pattern->filters[filterIndex].params.value("minArea", 100);
                int contourMode = pattern->filters[filterIndex].params.value("contourMode", cv::RETR_EXTERNAL);
                int contourApprox = pattern->filters[filterIndex].params.value("contourApprox", cv::CHAIN_APPROX_SIMPLE);
                int contourTarget = pattern->filters[filterIndex].params.value("contourTarget", 0);
                
                // 윤곽선 정보 추출
                QList<QVector<QPoint>> contours = ImageProcessor::extractContours(
                    roiMat, threshold, minArea, contourMode, contourApprox, contourTarget);
                        
                // ROI 오프셋 적용하여 전체 이미지 기준으로 변환
                for (QVector<QPoint>& contour : contours) {
                    for (QPoint& pt : contour) {
                        pt += QPoint(roi.x, roi.y);
                    }
                }
                
                // CameraView에 윤곽선 정보 전달 (그리기용)
                cameraView->setPatternContours(patternId, contours);
            }
        }
    }
    
    // 화면 갱신 - 컨투어 필터 실시간 반영을 위해 추가
    cameraView->update();
    
    // 필터 조정 중임을 표시
    setFilterAdjusting(true);
    
    // 실시간 필터 적용을 위한 화면 업데이트 추가
    printf("[TeachingWidget] updateFilterParam - 필터 실시간 적용\n");
    fflush(stdout);
    updateCameraFrame();
    
    // 모든 패턴의 템플릿 이미지 실시간 갱신 (필터 변경으로 인한 영향을 고려)
    printf("[TeachingWidget] Real-time template update after filter parameter change\n");
    fflush(stdout);
    updateAllPatternTemplateImages();
    
    // 필터 조정 완료
    setFilterAdjusting(false);
    
    // 메인 카메라뷰 패턴 실시간 갱신을 위한 추가 업데이트
    updateCameraFrame();
    
    // 필터 상태 텍스트 업데이트 (트리 아이템)
    QTreeWidgetItem* selectedItem = patternTree->currentItem();
    if (selectedItem) {
        selectedItem->setText(2, getFilterParamSummary(pattern->filters[filterIndex]));
    }
}

void TeachingWidget::setupCameraPreviews(QVBoxLayout *cameraLayout) {
    // MAX_CAMERAS 상수에 따라 동적으로 카메라 미리보기 생성
    // 4개 이하면 한 줄, 5개 이상이면 두 줄로 배치
    int camerasPerRow = (MAX_CAMERAS <= 4) ? MAX_CAMERAS : ((MAX_CAMERAS + 1) / 2);
    int totalRows = (MAX_CAMERAS + camerasPerRow - 1) / camerasPerRow; // 올림 계산
    
    int cameraIndex = 0;
    
    for (int row = 0; row < totalRows && cameraIndex < MAX_CAMERAS; row++) {
        QHBoxLayout *previewLayout = new QHBoxLayout();
        previewLayout->setSpacing(10);
        previewLayout->setContentsMargins(0, 5, 0, 5);
        previewLayout->setAlignment(Qt::AlignCenter);
        
        // 각 행에 카메라 미리보기 추가
        int camerasInThisRow = qMin(camerasPerRow, MAX_CAMERAS - cameraIndex);
        for (int col = 0; col < camerasInThisRow; col++) {
            QFrame *cameraFrame = createCameraPreviewFrame(cameraIndex);
            previewLayout->addWidget(cameraFrame, 1); // 크기 비율을 1로 설정하여 동일하게 유지
            cameraIndex++;
        }
        
        cameraLayout->addLayout(previewLayout);
    }
}

QFrame* TeachingWidget::createCameraPreviewFrame(int index) {
    QFrame *cameraFrame = new QFrame(this);
    cameraFrame->setFrameStyle(QFrame::Box | QFrame::Raised);
    cameraFrame->setLineWidth(1);
    // 고정 크기 대신 유동적인 크기 정책 사용
    cameraFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // 카메라 개수에 따라 최소 크기 조정 (8개까지 고려)
    int minWidth = (MAX_CAMERAS <= 4) ? 120 : 100;
    int minHeight = (MAX_CAMERAS <= 4) ? 90 : 75;
    cameraFrame->setMinimumSize(minWidth, minHeight);
    
    QVBoxLayout *frameLayout = new QVBoxLayout(cameraFrame);
    frameLayout->setContentsMargins(1, 1, 1, 1);
    frameLayout->setSpacing(0);
    
    QLabel *previewLabel = new QLabel(cameraFrame);
    // 고정 크기 대신 유동적 크기 정책 사용
    previewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setStyleSheet("background-color: black; color: white;");
    previewLabel->setText(QString(TR("CAMERA_NO_CONNECTION")).arg(index + 1));
    
    frameLayout->addWidget(previewLabel);
    cameraPreviewLabels.append(previewLabel);
    
    // 클릭 이벤트 처리를 위한 이벤트 필터 설치
    previewLabel->installEventFilter(this);
    previewLabel->setProperty("cameraIndex", index);
    
    return cameraFrame;
}


QVBoxLayout* TeachingWidget::createRightPanel() {
    QVBoxLayout *layout = new QVBoxLayout();
    layout->setContentsMargins(5, 5, 5, 5); // 왼쪽 레이아웃과 동일한 마진 설정
    layout->setSpacing(5); // 왼쪽 레이아웃과 동일한 간격 설정
    return layout;
}

void TeachingWidget::setupPatternTree() {
    // 패턴 테이블 생성 및 설정
    patternTree = new CustomPatternTreeWidget(this);
    
    // 초기 헤더 설정 (언어 시스템 사용)
    QStringList headers;
    headers << TR("PATTERN_NAME") << TR("PATTERN_TYPE") << TR("PATTERN_STATUS");
    patternTree->setHeaderLabels(headers);
    
    patternTree->setColumnWidth(0, 150);
    patternTree->setColumnWidth(1, 80);
    patternTree->setColumnWidth(2, 80);
    patternTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    patternTree->setSelectionMode(QAbstractItemView::SingleSelection);
    patternTree->setAlternatingRowColors(true);
    
    // 헤더 텍스트 중앙 정렬 설정
    QHeaderView* header = patternTree->header();
    header->setDefaultAlignment(Qt::AlignCenter);

    // 드래그 앤 드롭 설정
    patternTree->setDragEnabled(true);
    patternTree->setAcceptDrops(true);
    patternTree->setDropIndicatorShown(true);
    patternTree->setDragDropMode(QAbstractItemView::InternalMove);
    rightPanelLayout->addWidget(patternTree);
    
    // 이벤트 연결
    connect(patternTree, &QTreeWidget::currentItemChanged, this, &TeachingWidget::onPatternSelected);
    connect(patternTree->model(), &QAbstractItemModel::rowsMoved, this, &TeachingWidget::onPatternTableDropEvent);
    
    // 커스텀 드롭 완료 신호 연결
    connect(patternTree, &CustomPatternTreeWidget::dropCompleted, this, &TeachingWidget::onPatternTreeDropCompleted);
    
    connectItemChangedEvent();

}

QPushButton* TeachingWidget::createActionButton(const QString &text, const QString &color, const QFont &font) {
    QPushButton *button = new QPushButton(text, this);
    button->setMinimumHeight(40);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setFont(font);
    
    QString hoverColor = color;
    hoverColor.replace("#", "#");  // 이 부분은 원하는 hover 색상으로 변경 가능
    
    button->setStyleSheet(
        "QPushButton { "
        "   background-color: " + color + "; "
        "   color: white; "
        "   border: 1px solid #a0a0a0; "
        "   border-radius: 5px; "
        "   padding: 8px; "
        "}"
        "QPushButton:hover { background-color: " + hoverColor + "; }"
        "QPushButton:disabled { background-color: #BDBDBD; color: white; }"
    );
    
    return button;
}

void TeachingWidget::connectEvents() {
    connect(LanguageManager::instance(), &LanguageManager::languageChanged, 
        this, &TeachingWidget::updateUITexts);

    // FID 템플릿 이미지 갱신 필요 시그널 연결
    connect(cameraView, &CameraView::fidTemplateUpdateRequired, this, 
            [this](const QUuid& patternId) {
        // 현재 프레임이 있으면 템플릿 이미지 갱신
        if (cameraIndex >= 0 && cameraIndex < static_cast<int>(cameraFrames.size()) && 
            !cameraFrames[cameraIndex].empty()) {
            PatternInfo* pattern = cameraView->getPatternById(patternId);
            if (pattern && pattern->type == PatternType::FID) {
                // 필터 적용된 이미지로 템플릿 갱신
                updateFidTemplateImage(pattern, pattern->rect);
            }
        }
    });

    // INS 템플릿 이미지 갱신 시그널 연결 추가
    connect(cameraView, &CameraView::insTemplateUpdateRequired, this, 
            [this](const QUuid& patternId) {
        // 현재 프레임이 있으면 템플릿 이미지 갱신
        if (cameraIndex >= 0 && cameraIndex < static_cast<int>(cameraFrames.size()) && 
            !cameraFrames[cameraIndex].empty()) {
            PatternInfo* pattern = cameraView->getPatternById(patternId);
            if (pattern && pattern->type == PatternType::INS) {
                // 필터 적용된 이미지로 템플릿 갱신
                updateInsTemplateImage(pattern, pattern->rect);
            }
        }
    });
    
    connect(cameraView, &CameraView::requestRemovePattern, this, &TeachingWidget::removePattern);
    connect(cameraView, &CameraView::requestAddFilter, this, [this](const QUuid& patternId) {
        // 필터 다이얼로그 설정
        if (filterDialog) {
            PatternInfo* pattern = cameraView->getPatternById(patternId);
            if (pattern) {
                // 필터 다이얼로그 설정 및 표시
                filterDialog->setPatternId(patternId);
                filterDialog->exec();
            }
        }
    });
    connect(cameraView, &CameraView::enterKeyPressed, this, &TeachingWidget::addPattern);
    connect(cameraView, &CameraView::rectDrawn, this, [this](const QRect& rect) {
        const CalibrationInfo& calibInfo = cameraView->getCalibrationInfo();
        if (calibInfo.isCalibrated) {
            // 물리적 길이 계산 및 표시
            double widthMm = cameraView->calculatePhysicalLength(rect.width());
            double heightMm = cameraView->calculatePhysicalLength(rect.height());
            
            // 측정 정보 표시
            cameraView->setMeasurementInfo(QString("%1 × %2 mm")
                                        .arg(widthMm, 0, 'f', 1)
                                        .arg(heightMm, 0, 'f', 1));
        }
    });
    
    connect(cameraView, &CameraView::patternSelected, this, [this](const QUuid& id) {
        // ID가 빈 값이면 선택 취소
        if (id.isNull()) {
            patternTree->clearSelection();
            return;
        }
        
        // 패턴 ID로 트리 아이템 찾아서 선택 - 여기서 색상 스타일이 처리되도록 함
    for (int i = 0; i < patternTree->topLevelItemCount(); i++) {
        if (selectItemById(patternTree->topLevelItem(i), id)) {
            // 선택된 아이템이 화면에 표시되도록 스크롤
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                patternTree->scrollToItem(selectedItem);
                
                // 현재 아이템을 한 번 선택 해제했다가 다시 선택하여 스타일 일관성 유지
                patternTree->setCurrentItem(nullptr);
                patternTree->setCurrentItem(selectedItem);
            }
            return;
        }
    }
    });
    
     connect(cameraView, &CameraView::patternRectChanged, this, [this](const QUuid& id, const QRect& rect) {
        PatternInfo* pattern = cameraView->getPatternById(id);
        if (!pattern) return;
        
        QTreeWidgetItem* currentItem = patternTree->currentItem();
        if (currentItem && getPatternIdFromItem(currentItem) == id) {
            // 현재 선택된 패턴의 위치/크기가 변경된 경우 프로퍼티 업데이트
            updatePropertySpinBoxes(rect);
            
            // 각도 정보도 실시간 업데이트
            if (angleEdit) {
                angleEdit->blockSignals(true);
                angleEdit->setText(QString::number(pattern->angle, 'f', 1));
                angleEdit->blockSignals(false);
            }
        }
        
        // 추가: 패턴 크기가 변경될 때 템플릿 이미지 업데이트 (시뮬레이션 모드 지원)
        // FID일 경우
        if (pattern->type == PatternType::FID) {
            updateFidTemplateImage(pattern, rect);
        }
        // INS일 경우
        else if (pattern->type == PatternType::INS) {
            updateInsTemplateImage(pattern, rect);
        }
    });
    
    connect(cameraView, &CameraView::patternsGrouped, this, [this]() {
        // 패턴 그룹화/해제 이후 트리 업데이트
        updatePatternTree();
    });
    
    // 패턴 각도 변경 시 프로퍼티 패널 실시간 업데이트
    connect(cameraView, &CameraView::patternAngleChanged, this, [this](const QUuid& id, double angle) {
        // 각도를 -180° ~ +180° 범위로 정규화
        angle = normalizeAngle(angle);
        
        PatternInfo* pattern = cameraView->getPatternById(id);
        if (!pattern) return;
        
        // 정규화된 각도로 패턴 업데이트
        pattern->angle = angle;
        cameraView->updatePatternById(id, *pattern);
        
        QTreeWidgetItem* currentItem = patternTree->currentItem();
        if (currentItem && getPatternIdFromItem(currentItem) == id) {
            // 현재 선택된 패턴의 각도가 변경된 경우 프로퍼티 패널 업데이트
            if (angleEdit) {
                angleEdit->blockSignals(true);
                angleEdit->setText(QString::number(angle, 'f', 2));
                angleEdit->blockSignals(false);
            }
        }
        
        // 패턴 각도 변경 시 템플릿 이미지 업데이트 (시뮬레이션 모드 지원)
        if (pattern->type == PatternType::FID) {
            updateFidTemplateImage(pattern, pattern->rect);
        } else if (pattern->type == PatternType::INS) {
            updateInsTemplateImage(pattern, pattern->rect);
        }
    });
    
    // CameraView 빈 공간 클릭 시 검사 결과 필터 해제
    connect(cameraView, &CameraView::selectedInspectionPatternCleared, this, [this]() {
        patternTree->clearSelection();
    });
}

bool TeachingWidget::findAndUpdatePatternName(QTreeWidgetItem* parentItem, const QUuid& patternId, const QString& newName) {
    if (!parentItem) return false;
    
    // 모든 자식 아이템 검색
    for (int i = 0; i < parentItem->childCount(); i++) {
        QTreeWidgetItem* childItem = parentItem->child(i);
        QString idStr = childItem->data(0, Qt::UserRole).toString();
        if (idStr == patternId.toString()) {
            childItem->setText(0, newName);
            return true;
        }
        
        // 재귀적으로 자식의 자식 검색
        if (findAndUpdatePatternName(childItem, patternId, newName)) {
            return true;
        }
    }
    
    return false;
}

bool TeachingWidget::findAndUpdatePatternEnabledState(QTreeWidgetItem* parentItem, const QUuid& patternId, bool enabled) {
    if (!parentItem) return false;
    
    // 모든 자식 아이템 검색
    for (int i = 0; i < parentItem->childCount(); i++) {
        QTreeWidgetItem* childItem = parentItem->child(i);
        QString idStr = childItem->data(0, Qt::UserRole).toString();
        if (idStr == patternId.toString()) {
            childItem->setDisabled(!enabled);
            return true;
        }
        
        // 재귀적으로 자식의 자식 검색
        if (findAndUpdatePatternEnabledState(childItem, patternId, enabled)) {
            return true;
        }
    }
    
    return false;
}

void TeachingWidget::updatePropertySpinBoxes(const QRect& rect) {
    // 읽기 전용 라벨로 변경
    QLabel* xValueLabel = findChild<QLabel*>("patternXValue");
    if (xValueLabel) {
        xValueLabel->setText(QString::number(rect.x()));
    }
    
    QLabel* yValueLabel = findChild<QLabel*>("patternYValue");
    if (yValueLabel) {
        yValueLabel->setText(QString::number(rect.y()));
    }
    
    QLabel* wValueLabel = findChild<QLabel*>("patternWValue");
    if (wValueLabel) {
        wValueLabel->setText(QString::number(rect.width()));
    }
    
    QLabel* hValueLabel = findChild<QLabel*>("patternHValue");
    if (hValueLabel) {
        hValueLabel->setText(QString::number(rect.height()));
    }
    // FID 패턴인 경우 템플릿 이미지 업데이트
    QTreeWidgetItem* selectedItem = patternTree->currentItem();
    if (selectedItem) {
        QUuid patternId = getPatternIdFromItem(selectedItem);
        if (!patternId.isNull()) {
            PatternInfo* pattern = cameraView->getPatternById(patternId);
            if (pattern) {
                // 각도 정보 업데이트
                if (angleEdit) {
                    angleEdit->blockSignals(true);
                    angleEdit->setText(QString::number(pattern->angle, 'f', 1));
                    angleEdit->blockSignals(false);
                }
                
                // FID 패턴인 경우 템플릿 이미지 업데이트
                if (pattern->type == PatternType::FID) {
                    updateFidTemplateImage(pattern, rect);
                }
            }
        }
    }
}

void TeachingWidget::onPatternTableDropEvent(const QModelIndex &parent, int start, int end, const QModelIndex &destination, int row) {
    
    qDebug() << "=== onPatternTableDropEvent 호출됨 ===";
    qDebug() << "parent valid:" << parent.isValid() << "start:" << start << "end:" << end;
    qDebug() << "destination valid:" << destination.isValid() << "row:" << row;
    
    // 드롭된 아이템이 필터인지 확인
    QTreeWidgetItem *item = nullptr;
    QTreeWidgetItem *targetItem = nullptr;
    
    // 부모가 유효한 경우 (자식 아이템이 이동된 경우)
    if (parent.isValid()) {
        // QModelIndex를 대신하는 방법
        QTreeWidgetItem* parentItem = nullptr;
        if (parent.parent().isValid()) {
            // 2단계 이상의 깊이인 경우
            int grandParentRow = parent.parent().row();
            QTreeWidgetItem* grandParentItem = patternTree->topLevelItem(grandParentRow);
            if (grandParentItem) {
                parentItem = grandParentItem->child(parent.row());
            }
        } else {
            // 1단계 깊이인 경우
            parentItem = patternTree->topLevelItem(parent.row());
        }
        
        if (parentItem && start < parentItem->childCount()) {
            item = parentItem->child(start);
        }
    } else {
        // 최상위 아이템인 경우
        if (start < patternTree->topLevelItemCount()) {
            item = patternTree->topLevelItem(start);
        }
    }
    
    // 드롭 대상이 유효한 경우
    if (destination.isValid()) {
        // QModelIndex를 대신하는 방법
        if (destination.parent().isValid()) {
            // 2단계 이상의 깊이인 경우
            int parentRow = destination.parent().row();
            QTreeWidgetItem* parentItem = patternTree->topLevelItem(parentRow);
            if (parentItem) {
                targetItem = parentItem->child(destination.row());
            }
        } else {
            // 1단계 깊이인 경우
            targetItem = patternTree->topLevelItem(destination.row());
        }
    } else if (row >= 0 && row < patternTree->topLevelItemCount()) {
        targetItem = patternTree->topLevelItem(row);
    }
    
    // item 또는 targetItem이 null인 경우
    if (!item) {
        return;
    }
    
    // 드래그된 아이템이 필터인지 패턴인지 확인
    QVariant filterIndexVar = item->data(0, Qt::UserRole + 1);
    QVariant patternIdVar = item->data(0, Qt::UserRole);
    
    // 1. 필터 이동 처리
    if (filterIndexVar.isValid()) {
        // 타겟 아이템이 필터인지 확인 (필터를 필터 하위로 넣는 것 방지)
        if (targetItem && targetItem->data(0, Qt::UserRole + 1).isValid()) {
            updatePatternTree();  // 원래 상태로 복원
            return;
        }
        
        // 부모 항목이 같은지 확인 (같은 패턴 내에서만 이동 가능)
        QTreeWidgetItem *sourceParent = item->parent();
        QTreeWidgetItem *destParent = targetItem ? targetItem->parent() : nullptr;
        
        if (sourceParent != destParent) {
            // 다른 패턴으로 이동 시도하면 원래 위치로 복원
            updatePatternTree();
            return;
        }
    }
    // 2. 패턴 이동 처리 (패턴을 다른 패턴의 하위로)
    else if (patternIdVar.isValid() && targetItem) {
        QUuid sourcePatternId = QUuid(patternIdVar.toString());
        QVariant targetPatternIdVar = targetItem->data(0, Qt::UserRole);
        
        if (targetPatternIdVar.isValid()) {
            QUuid targetPatternId = QUuid(targetPatternIdVar.toString());
            
            PatternInfo* sourcePattern = cameraView->getPatternById(sourcePatternId);
            PatternInfo* targetPattern = cameraView->getPatternById(targetPatternId);
            
            if (sourcePattern && targetPattern) {
                // INS 패턴을 FID 패턴 하위로 이동하는 경우만 허용
                if (sourcePattern->type == PatternType::INS && targetPattern->type == PatternType::FID) {
                    qDebug() << "패턴 그룹화 시도 (시뮬레이션 모드:" << simulationMode << "):" << sourcePattern->name << "-> 부모:" << targetPattern->name;
                    qDebug() << "변경 전 parentId:" << sourcePattern->parentId.toString();
                    
                    // 기존 부모에서 제거
                    if (!sourcePattern->parentId.isNull()) {
                        PatternInfo* oldParent = cameraView->getPatternById(sourcePattern->parentId);
                        if (oldParent) {
                            oldParent->childIds.removeAll(sourcePatternId);
                            cameraView->updatePatternById(oldParent->id, *oldParent);
                        }
                    }
                    
                    // 부모-자식 관계 설정
                    sourcePattern->parentId = targetPatternId;
                    
                    // 대상 패턴의 childIds에 추가
                    qDebug() << "=== childIds 추가 과정 ===";
                    qDebug() << "소스 패턴 ID:" << sourcePatternId.toString();
                    qDebug() << "대상 패턴" << targetPattern->name << "의 현재 childIds:";
                    for (int i = 0; i < targetPattern->childIds.size(); i++) {
                        qDebug() << "  [" << i << "]" << targetPattern->childIds[i].toString();
                    }
                    
                    bool alreadyContains = targetPattern->childIds.contains(sourcePatternId);
                    qDebug() << "이미 포함되어 있나?" << alreadyContains;
                    
                    if (!alreadyContains) {
                        qDebug() << "대상 패턴 업데이트 전 childIds 수:" << targetPattern->childIds.size();
                        targetPattern->childIds.append(sourcePatternId);
                        qDebug() << "대상 패턴 업데이트 후 childIds 수:" << targetPattern->childIds.size();
                        bool targetUpdateResult = cameraView->updatePatternById(targetPatternId, *targetPattern);
                        qDebug() << "대상 패턴 업데이트 결과:" << targetUpdateResult;
                        
                        // 업데이트 후 다시 확인
                        PatternInfo* verifyTarget = cameraView->getPatternById(targetPatternId);
                        if (verifyTarget) {
                            qDebug() << "업데이트 후 대상 패턴 확인 - childIds 수:" << verifyTarget->childIds.size();
                        }
                    } else {
                        qDebug() << "이미 존재하는 자식이므로 추가하지 않음";
                    }
                    
                    qDebug() << "변경 후 parentId:" << sourcePattern->parentId.toString();
                    qDebug() << "대상 패턴의 childIds 수:" << targetPattern->childIds.size();
                    
                    // 대상 패턴의 childIds 확인
                    PatternInfo* updatedTargetPattern = cameraView->getPatternById(targetPatternId);
                    if (updatedTargetPattern) {
                        qDebug() << "업데이트 후 대상 패턴의 childIds 수:" << updatedTargetPattern->childIds.size();
                        for (const QUuid& childId : updatedTargetPattern->childIds) {
                            qDebug() << "자식 ID:" << childId.toString();
                        }
                    }
                    
                    // CameraView에 패턴 업데이트 알리기
                    bool updateResult = cameraView->updatePatternById(sourcePatternId, *sourcePattern);
                    qDebug() << "updatePatternById 결과:" << updateResult;
                    
                    // 업데이트 후 다시 확인
                    PatternInfo* updatedPattern = cameraView->getPatternById(sourcePatternId);
                    if (updatedPattern) {
                        qDebug() << "업데이트 후 확인된 parentId:" << updatedPattern->parentId.toString();
                    }
                    
                    // 시뮬레이션 모드에서는 즉시 저장하여 데이터 지속성 보장
                    if (simulationMode) {
                        qDebug() << "시뮬레이션 모드: 패턴 그룹화 후 즉시 저장";
                        saveRecipe();
                    }
                    
                    // 패턴 트리 업데이트
                    updatePatternTree();
                    
                    // 업데이트 후 최종 확인
                    PatternInfo* finalTargetPattern = cameraView->getPatternById(targetPatternId);
                    if (finalTargetPattern) {
                        qDebug() << "updatePatternTree 후 대상 패턴 확인 - childIds 수:" << finalTargetPattern->childIds.size();
                        for (const QUuid& childId : finalTargetPattern->childIds) {
                            qDebug() << "  - 자식 ID:" << childId.toString();
                        }
                    }
                    
                    qDebug() << "=== 패턴 드래그 앤 드롭 완료 ===";
                    qDebug() << "패턴 그룹화:" << sourcePattern->name << "→" << targetPattern->name;
                    qDebug() << "패턴 관계 변경 완료 - 저장 버튼으로 저장하세요";
                    
                    // 카메라 뷰 업데이트
                    cameraView->update();
                    
                    return;
                }
                // 그룹화 해제 (INS를 최상위로 이동)
                else if (sourcePattern->type == PatternType::INS && !targetItem->parent()) {
                    qDebug() << "패턴 그룹화 해제 시도 (시뮬레이션 모드:" << simulationMode << "):" << sourcePattern->name;
                    qDebug() << "변경 전 parentId:" << sourcePattern->parentId.toString();
                    
                    // 기존 부모에서 제거
                    if (!sourcePattern->parentId.isNull()) {
                        PatternInfo* oldParent = cameraView->getPatternById(sourcePattern->parentId);
                        if (oldParent) {
                            oldParent->childIds.removeAll(sourcePatternId);
                            cameraView->updatePatternById(oldParent->id, *oldParent);
                        }
                    }
                    
                    sourcePattern->parentId = QUuid();
                    
                    qDebug() << "변경 후 parentId:" << sourcePattern->parentId.toString();
                    
                    // CameraView에 패턴 업데이트 알리기
                    bool updateResult = cameraView->updatePatternById(sourcePatternId, *sourcePattern);
                    qDebug() << "updatePatternById 결과:" << updateResult;
                    
                    // 업데이트 후 다시 확인
                    PatternInfo* updatedPattern = cameraView->getPatternById(sourcePatternId);
                    if (updatedPattern) {
                        qDebug() << "업데이트 후 확인된 parentId:" << updatedPattern->parentId.toString();
                    }
                    
                    // 시뮬레이션 모드에서는 즉시 저장하여 데이터 지속성 보장
                    if (simulationMode) {
                        qDebug() << "시뮬레이션 모드: 패턴 그룹화 해제 후 즉시 저장";
                        saveRecipe();
                    }
                    
                    // 패턴 트리 업데이트
                    updatePatternTree();
                    
                    // 카메라 뷰 업데이트
                    cameraView->update();
                    
                    return;
                }
            }
        }
        
        // 허용되지 않는 패턴 이동은 복원
        updatePatternTree();
        return;
    }
    // 유효하지 않은 아이템
    else {
        return;
    }
    
    
    // 필터 이동 처리
    if (filterIndexVar.isValid()) {
        QTreeWidgetItem *sourceParent = item->parent();
        
        if (sourceParent) {
            // 같은 패턴 내에서 필터 순서 변경
            QString patternIdStr = sourceParent->data(0, Qt::UserRole).toString();
            QUuid patternId = QUuid(patternIdStr);
            if (patternId.isNull()) {
                return;
            }
            
            PatternInfo* pattern = cameraView->getPatternById(patternId);
            if (!pattern) {
                return;
            }
            
            // 필터 인덱스 가져오기
            int filterIdx = filterIndexVar.toInt();
            int newIdx = destination.isValid() ? destination.row() : row;
            
            // 같은 부모 내에서 위치 조정 (패턴 안에서의 상대적 위치)
            if (newIdx > filterIdx) newIdx--;
            
            
            // 실제 필터 순서 변경
            if (filterIdx >= 0 && filterIdx < pattern->filters.size() && 
                newIdx >= 0 && newIdx < pattern->filters.size() && filterIdx != newIdx) {
                
                // 필터 이동
                FilterInfo filter = pattern->filters.takeAt(filterIdx);
                pattern->filters.insert(newIdx, filter);
                
                // 패턴 트리 업데이트
                updatePatternTree();
                
                // 카메라 뷰 업데이트
                cameraView->update();
            }
        }
    }
}

QUuid TeachingWidget::getPatternIdFromItem(QTreeWidgetItem* item) {
    if (!item) return QUuid();
    return QUuid(item->data(0, Qt::UserRole).toString());
}

void TeachingWidget::updatePatternTree() {
    
    // ★★★ 트리 업데이트 전 항상 최신 패턴 정보로 동기화 ★★★
    syncPatternsFromCameraView();
    
    // 현재 선택된 패턴 ID 저장
    QUuid selectedId = cameraView->getSelectedPatternId();
    
    // 트리 위젯 초기화
    patternTree->clear();
    
    // 컬럼 헤더 설정
    patternTree->setHeaderLabels(QStringList() << TR("PATTERN_NAME") << TR("PATTERN_TYPE") << TR("PATTERN_STATUS"));
    
    // 현재 카메라의 UUID (시뮬레이션 모드 고려)
    QString currentCameraUuid;
    
    // 시뮬레이션 모드 확인
    if (simulationMode) {
        // 시뮬레이션 모드: simulationCameraName을 우선적으로 사용, 없으면 currentCameraUuid 사용
        currentCameraUuid = cameraView->getSimulationCameraName();
        if (currentCameraUuid.isEmpty()) {
            currentCameraUuid = cameraView->getCurrentCameraUuid();
        }
        qDebug() << QString("updatePatternTree - 시뮬레이션 모드, 카메라 UUID: '%1'").arg(currentCameraUuid);
    } else if (isValidCameraIndex(cameraIndex)) {
        // 일반 모드: 실제 카메라 UUID 사용
        currentCameraUuid = getCameraInfo(cameraIndex).uniqueId;
        qDebug() << QString("updatePatternTree - 라이브 모드, 카메라 인덱스: %1, 카메라 UUID: '%2'").arg(cameraIndex).arg(currentCameraUuid);
    } else {
        // 마지막으로 cameraView에서 직접 가져오기
        currentCameraUuid = cameraView->getCurrentCameraUuid();
    }

    // 모든 패턴 가져오기
    const QList<PatternInfo>& allPatterns = cameraView->getPatterns();
    
    // 현재 카메라의 패턴만 필터링
    QList<PatternInfo> currentCameraPatterns;
    
    // currentCameraUuid가 비어있으면 첫 번째 카메라 사용
    if (currentCameraUuid.isEmpty() && !allPatterns.isEmpty()) {
        // 첫 번째 패턴의 카메라 UUID를 기본값으로 설정
        for (const PatternInfo& pattern : allPatterns) {
            if (!pattern.cameraUuid.isEmpty()) {
                currentCameraUuid = pattern.cameraUuid;
                break;
            }
        }
    }
    
    // 패턴 필터링: 현재 카메라의 패턴만 추가
    for (const PatternInfo& pattern : allPatterns) {
        QString patternCameraUuid = pattern.cameraUuid.isEmpty() ? "default" : pattern.cameraUuid;
        
        // 현재 카메라 UUID가 설정되어 있으면 해당 카메라만 필터링
        if (!currentCameraUuid.isEmpty() && patternCameraUuid != currentCameraUuid) {
            continue;
        }
        
        currentCameraPatterns.append(pattern);
    }
    
    // 디버그: 패턴 정보 출력
    qDebug() << QString("updatePatternTree - 현재 카메라 UUID: '%1'").arg(currentCameraUuid);
    qDebug() << QString("updatePatternTree - 현재 카메라 패턴 수: %1").arg(currentCameraPatterns.size());
    qDebug() << QString("updatePatternTree - 전체 패턴 수: %1").arg(allPatterns.size());
    
    // 패턴 ID에 대한 트리 아이템 맵핑 저장 (부모-자식 관계 구성 시 사용)
    QMap<QUuid, QTreeWidgetItem*> itemMap;
    
    // 1. 모든 최상위 패턴 먼저 추가 (부모가 없는 패턴)
    int addedPatterns = 0;
    
    for (const PatternInfo& pattern : currentCameraPatterns) {
        // 부모가 없는 패턴만 최상위 항목으로 추가
        if (pattern.parentId.isNull()) {
            QTreeWidgetItem* item = createPatternTreeItem(pattern);
            if (item) {
                patternTree->addTopLevelItem(item);
                itemMap[pattern.id] = item;
                addedPatterns++;
                
                // 해당 패턴의 필터들도 자식으로 추가
                addFiltersToTreeItem(item, pattern);
            }
        }
    }
    
    // 2. 자식 패턴 추가 (부모가 있는 패턴) - 다단계 부모-자식 관계 지원
    // 다단계 부모-자식 관계를 처리하기 위해 최대 3번 반복
    for (int pass = 0; pass < 3; pass++) {
        bool addedInThisPass = false;
        
        for (const PatternInfo& pattern : currentCameraPatterns) {
            // 부모가 있는 패턴만 처리 (아직 itemMap에 없는 것만)
            if (!pattern.parentId.isNull() && !itemMap.contains(pattern.id)) {
                QTreeWidgetItem* parentItem = itemMap.value(pattern.parentId);
                if (parentItem) {
                    QTreeWidgetItem* childItem = createPatternTreeItem(pattern);
                    parentItem->addChild(childItem);
                    itemMap[pattern.id] = childItem;
                    
                    // 해당 패턴의 필터들도 자식으로 추가
                    addFiltersToTreeItem(childItem, pattern);
                    addedInThisPass = true;
                    
                    // 자식이 있는 부모 항목은 펼치기
                    parentItem->setExpanded(true);
                }
            }
        }
        
        // 이번 패스에서 추가된 패턴이 없으면 종료
        if (!addedInThisPass) {
            break;
        }
    }
    
    // 모든 최상위 항목 확장
    patternTree->expandAll();
    
    // 이전에 선택된 패턴 다시 선택
    if (!selectedId.isNull()) {
        for (int i = 0; i < patternTree->topLevelItemCount(); i++) {
            if (selectItemById(patternTree->topLevelItem(i), selectedId)) {
                break;
            }
        }
    }
}

// 필터 파라미터 요약 문자열 생성 함수
QString TeachingWidget::getFilterParamSummary(const FilterInfo& filter) {
    QString summary;
    
    switch (filter.type) {
        case FILTER_THRESHOLD: {
            int type = filter.params.value("thresholdType", 0);
            int threshold = filter.params.value("threshold", 128);
            
            if (type == THRESH_ADAPTIVE_MEAN || type == THRESH_ADAPTIVE_GAUSSIAN) {
                int blockSize = filter.params.value("blockSize", 7);
                int C = filter.params.value("C", 5);
                summary = QString("적응형, 블록:%1, C:%2").arg(blockSize).arg(C);
            } else {
                summary = QString("임계값:%1").arg(threshold);
            }
            break;
        }
        case FILTER_BLUR: {
            int kernelSize = filter.params.value("kernelSize", 3);
            summary = QString("커널:%1×%1").arg(kernelSize);
            break;
        }
        case FILTER_CANNY: {
            int threshold1 = filter.params.value("threshold1", 100);
            int threshold2 = filter.params.value("threshold2", 200);
            summary = QString("하한:%1, 상한:%2").arg(threshold1).arg(threshold2);
            break;
        }
        case FILTER_SOBEL: {
            int kernelSize = filter.params.value("sobelKernelSize", 3);
            summary = QString("커널:%1×%1").arg(kernelSize);
            break;
        }
        case FILTER_LAPLACIAN: {
            int kernelSize = filter.params.value("laplacianKernelSize", 3);
            summary = QString("커널:%1×%1").arg(kernelSize);
            break;
        }
        case FILTER_SHARPEN: {
            int strength = filter.params.value("sharpenStrength", 3);
            summary = QString("강도:%1").arg(strength);
            break;
        }
        case FILTER_BRIGHTNESS: {
            int brightness = filter.params.value("brightness", 0);
            summary = QString("값:%1").arg(brightness);
            break;
        }
        case FILTER_CONTRAST: {
            int contrast = filter.params.value("contrast", 0);
            summary = QString("값:%1").arg(contrast);
            break;
        }
        case FILTER_CONTOUR: {
            int threshold = filter.params.value("threshold", 128);
            int minArea = filter.params.value("minArea", 100);
            summary = QString("임계값:%1, 최소면적:%2").arg(threshold).arg(minArea);
            break;
        }
        default:
            summary = "기본 설정";
            break;
    }
    
    return summary;
}

void TeachingWidget::connectItemChangedEvent() {
    connect(patternTree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (column == 0) {  // 체크박스 열
            QString idStr = item->data(0, Qt::UserRole).toString();
            QUuid patternId = QUuid(idStr);
            
            // 필터 아이템인지 확인 (UserRole + 1에 필터 인덱스가 저장됨)
            QVariant filterIndexVar = item->data(0, Qt::UserRole + 1);
            
            if (filterIndexVar.isValid()) {
                // 필터 아이템
                int filterIndex = filterIndexVar.toInt();
                bool checked = (item->checkState(0) == Qt::Checked);
                
                // 필터 활성화/비활성화
                cameraView->setPatternFilterEnabled(patternId, filterIndex, checked);
                
                // 상태 표시 업데이트
                item->setText(2, checked ? getFilterParamSummary(cameraView->getPatternFilters(patternId)[filterIndex]) : TR("INACTIVE"));
                
                // 부모 패턴이 FID 타입인지 확인
                QTreeWidgetItem* parentItem = item->parent();
                if (parentItem) {
                    QString parentIdStr = parentItem->data(0, Qt::UserRole).toString();
                    QUuid parentId = QUuid(parentIdStr);
                    PatternInfo* parentPattern = cameraView->getPatternById(parentId);
                    
                    // 부모가 FID 타입이면 템플릿 이미지 업데이트
                    if (parentPattern && parentPattern->type == PatternType::FID && 
                        cameraIndex >= 0 && cameraIndex < static_cast<int>(cameraFrames.size()) && 
                        !cameraFrames[cameraIndex].empty()) {
                        updateFidTemplateImage(parentPattern, parentPattern->rect);
                        
                        // 추가: 현재 선택된 아이템이 이 부모 패턴이라면, 프로퍼티 패널의 템플릿 이미지도 업데이트
                        QTreeWidgetItem* currentItem = patternTree->currentItem();
                        if (currentItem && getPatternIdFromItem(currentItem) == parentId) {
                            updatePropertyPanel(parentPattern, nullptr, parentId, -1);
                        }
                    }
                    // **여기가 수정된 부분**
                    else if (parentPattern && parentPattern->type == PatternType::INS && 
                            cameraIndex >= 0 && cameraIndex < static_cast<int>(cameraFrames.size()) && 
                            !cameraFrames[cameraIndex].empty()) {
                        updateInsTemplateImage(parentPattern, parentPattern->rect);
                        
                        // 추가: 현재 선택된 아이템이 이 부모 패턴이라면, 프로퍼티 패널의 템플릿 이미지도 업데이트
                        QTreeWidgetItem* currentItem = patternTree->currentItem();
                        if (currentItem && getPatternIdFromItem(currentItem) == parentId) {
                            updatePropertyPanel(parentPattern, nullptr, parentId, -1);
                        }
                    }
                }
                
                // 화면 갱신
                cameraView->update();
            } else if (!patternId.isNull()) {
                // 패턴 아이템
                bool checked = (item->checkState(0) == Qt::Checked);
                
                // 패턴 활성화/비활성화
                PatternInfo* pattern = cameraView->getPatternById(patternId);
                if (pattern) {
                    pattern->enabled = checked;
                    
                    // 상태 표시 업데이트
                    item->setText(2, checked ? TR("ACTIVE") : TR("INACTIVE"));
                    
                    // 비활성화된 패턴이 선택된 상태면 선택 해제
                    if (!checked && cameraView->getSelectedPatternId() == patternId) {
                        cameraView->setSelectedPatternId(QUuid());
                    }
                    
                    // FID 패턴이면 템플릿 이미지 업데이트
                    if (pattern->type == PatternType::FID && 
                        cameraIndex >= 0 && cameraIndex < static_cast<int>(cameraFrames.size()) && 
                        !cameraFrames[cameraIndex].empty()) {
                        updateFidTemplateImage(pattern, pattern->rect);
                    }
                    // INS 패턴이면 템플릿 이미지 업데이트 - **여기도 수정됨**
                    if (pattern->type == PatternType::INS && 
                        cameraIndex >= 0 && cameraIndex < static_cast<int>(cameraFrames.size()) && 
                        !cameraFrames[cameraIndex].empty()) {
                        updateInsTemplateImage(pattern, pattern->rect);
                    }
                    
                    cameraView->update();
                }
            }
        }
    });
}

// 필터 타입 이름을 번역된 텍스트로 반환하는 함수 추가
QString TeachingWidget::getFilterTypeName(int filterType) {
    switch (filterType) {
        case FILTER_THRESHOLD:
            return TR("THRESHOLD_FILTER");
        case FILTER_BLUR:
            return TR("BLUR_FILTER");
        case FILTER_CANNY:
            return TR("CANNY_FILTER");
        case FILTER_SOBEL:
            return TR("SOBEL_FILTER");
        case FILTER_LAPLACIAN:
            return TR("LAPLACIAN_FILTER");
        case FILTER_SHARPEN:
            return TR("SHARPEN_FILTER");
        case FILTER_BRIGHTNESS:
            return TR("BRIGHTNESS_FILTER");
        case FILTER_CONTRAST:
            return TR("CONTRAST_FILTER");
        case FILTER_CONTOUR:
            return TR("CONTOUR_FILTER");
        default:
            return TR("UNKNOWN_FILTER");
    }
}

void TeachingWidget::addFiltersToTreeItem(QTreeWidgetItem* parentItem, const PatternInfo& pattern) {
    if (pattern.filters.isEmpty()) {
        return;
    }
   
    // 각 필터를 자식 항목으로 추가
    for (int i = 0; i < pattern.filters.size(); i++) {
        const FilterInfo& filter = pattern.filters[i];
        
        // 필터 이름/유형 획득
        QString filterName = getFilterTypeName(filter.type);
        
        // 필터 파라미터 요약 생성
        QString paramSummary = getFilterParamSummary(filter);
        
        // 필터를 위한 트리 아이템 생성
        QTreeWidgetItem* filterItem = new QTreeWidgetItem();
        
        // 필터 이름은 0번 열에
        filterItem->setText(0, filterName);
        
        // 필터 타입 정보는 1번 열에
        filterItem->setText(1, TR("FIL"));
        
        // 파라미터 요약은 2번 열에
        filterItem->setText(2, filter.enabled ? TR("ACTIVE") : TR("INACTIVE"));
       
        // 필터 식별을 위해 사용자 데이터 설정
        // 패턴 ID와 필터 인덱스를 함께 저장
        filterItem->setData(0, Qt::UserRole, pattern.id.toString());
        filterItem->setData(0, Qt::UserRole + 1, i); // 필터 인덱스 저장
        
        // 활성화 체크박스 설정
        filterItem->setFlags(filterItem->flags() | Qt::ItemIsUserCheckable);
        filterItem->setCheckState(0, filter.enabled ? Qt::Checked : Qt::Unchecked);
        
        // 텍스트 색상 설정
        filterItem->setForeground(0, QColor(Qt::white)); // 필터 이름은 흰색
        filterItem->setForeground(1, QColor(Qt::white)); // 필터 타입은 검정색
        filterItem->setForeground(2, QColor(Qt::white)); // 필터 상태는 흰색
        
        // 부모 아이템에 추가
        parentItem->addChild(filterItem);
    }
}

// ★★★ CameraView에서 최신 패턴 정보를 가져와서 동기화 ★★★
void TeachingWidget::syncPatternsFromCameraView() {
    if (!cameraView) return;
    
    // CameraView에서 현재 패턴들을 가져옴
    QList<PatternInfo> patterns = cameraView->getPatterns();
    for (const PatternInfo& pattern : patterns) {
        // 패턴 동기화 처리
    }
}

void TeachingWidget::onPatternSelected(QTreeWidgetItem* current, QTreeWidgetItem* previous) {
    // ★★★ 패턴 선택 시 항상 최신 정보로 동기화 ★★★
    syncPatternsFromCameraView();
    
     // 삭제 버튼 활성화 상태 관리 - 함수 시작 부분에 추가
     QPushButton* removeButton = findChild<QPushButton*>("removeButton");
     if (removeButton) {
         removeButton->setEnabled(current != nullptr);
     }
    
    if (!current) {
        if (propertyStackWidget) propertyStackWidget->setCurrentIndex(0);
        // 선택 해제 시 카메라뷰에서 검사 결과 필터링 해제
        if (cameraView) {
            cameraView->clearSelectedInspectionPattern();
        }
        return;
    }
    
    // 선택된 트리 아이템에서 패턴 ID 가져오기
    QString idStr = current->data(0, Qt::UserRole).toString();
    QUuid patternId = QUuid(idStr);
    
    // 카메라뷰에 선택된 패턴 전달 (검사 결과 필터링용)
    if (cameraView) {
        cameraView->setSelectedInspectionPatternId(patternId);
    }
    
    // 필터 아이템인지 확인
    QVariant filterIndexVar = current->data(0, Qt::UserRole + 1);
    bool isFilterItem = filterIndexVar.isValid();
    
    if (isFilterItem) {
        // 필터 아이템이 선택된 경우
        int filterIndex = filterIndexVar.toInt();
        
        // 부모 패턴 찾기 (필터는 항상 패턴의 자식)
        QTreeWidgetItem* parentItem = current->parent();
        if (parentItem) {
            QString parentIdStr = parentItem->data(0, Qt::UserRole).toString();
            QUuid parentId = QUuid(parentIdStr);
            PatternInfo* parentPattern = cameraView->getPatternById(parentId);
            
            if (parentPattern && filterIndex >= 0 && filterIndex < parentPattern->filters.size()) {
                
                // 패널 전환 전에 확인
                
                // 필터 프로퍼티 패널 업데이트 - 직접 인덱스 설정
                propertyStackWidget->setCurrentIndex(2);
                
                // 필터 내용 업데이트 - 별도 함수 호출 대신 직접 코드 삽입
                if (!filterPropertyContainer) {
                    return;
                }
                
                // 기존 필터 위젯 모두 제거
                QLayout* containerLayout = filterPropertyContainer->layout();
                if (containerLayout) {
                    QLayoutItem* item;
                    while ((item = containerLayout->takeAt(0)) != nullptr) {
                        if (item->widget()) {
                            item->widget()->deleteLater();
                        }
                        delete item;
                    }
                }
                
                // 필터 정보 라벨 생성
                const FilterInfo& filter = parentPattern->filters[filterIndex];
                
                // 필터 프로퍼티 위젯 생성 및 추가
                FilterPropertyWidget* filterPropWidget = new FilterPropertyWidget(filter.type, filterPropertyContainer);
                filterPropWidget->setObjectName("filterPropertyWidget");
                filterPropWidget->setParams(filter.params);
                filterPropWidget->setEnabled(filter.enabled);
                containerLayout->addWidget(filterPropWidget);
                
                connect(filterPropWidget, &FilterPropertyWidget::paramChanged,
                    [this, parentId, filterIndex](const QString& paramName, int value) {
                        updateFilterParam(parentId, filterIndex, paramName, value);
                    });
                
                connect(filterPropWidget, &FilterPropertyWidget::enableStateChanged,
                        [this, parentId, filterIndex](bool enabled) {
                    cameraView->setPatternFilterEnabled(parentId, filterIndex, enabled);
                    
                    QTreeWidgetItem* selectedItem = patternTree->currentItem();
                    if (selectedItem) {
                        selectedItem->setCheckState(0, enabled ? Qt::Checked : Qt::Unchecked);
                    }
                });
                
                return;
            }
        }
    }
    
    // 일반 패턴 아이템이 선택된 경우 (기존 코드 유지)
    PatternInfo* pattern = cameraView->getPatternById(patternId);
    updatePropertyPanel(pattern, nullptr, QUuid(), -1);
    
    if (pattern) {
        cameraView->setSelectedPatternId(pattern->id);
    }
}

void TeachingWidget::createPropertyPanels() {
    // 1. 프로퍼티 패널을 담을 스택 위젯 생성
    propertyStackWidget = new QStackedWidget(this);
    rightPanelLayout->insertWidget(3, propertyStackWidget);
   
    // 2. 빈 상태를 위한 기본 패널
    QWidget* emptyPanel = new QWidget(propertyStackWidget);
    QVBoxLayout* emptyLayout = new QVBoxLayout(emptyPanel);
    emptyPanelLabel = new QLabel("패턴을 선택하면 속성이 표시됩니다", emptyPanel);
    emptyPanelLabel->setAlignment(Qt::AlignCenter);
    emptyPanelLabel->setStyleSheet("color: gray; font-style: italic;");
    emptyLayout->addWidget(emptyPanelLabel);
    propertyStackWidget->addWidget(emptyPanel);

    // 3. 패턴 속성 패널
    QWidget* patternPanel = new QWidget(propertyStackWidget);
    QVBoxLayout* patternContentLayout = new QVBoxLayout(patternPanel);
    patternContentLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea* scrollArea = new QScrollArea(patternPanel);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QWidget* scrollContent = new QWidget();
    QVBoxLayout* mainContentLayout = new QVBoxLayout(scrollContent);
    mainContentLayout->setContentsMargins(5, 5, 5, 5);
    mainContentLayout->setSpacing(8);

    // === 공통 기본 정보 그룹 ===
    QGroupBox* basicInfoGroup = new QGroupBox("기본 정보", scrollContent);
    basicInfoGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    QFormLayout* basicInfoLayout = new QFormLayout(basicInfoGroup);
    basicInfoLayout->setVerticalSpacing(5);
    basicInfoLayout->setContentsMargins(10, 15, 10, 10);
    
    // 패턴 ID
    patternIdLabel = new QLabel("ID:", basicInfoGroup);
    patternIdValue = new QLabel(basicInfoGroup);
    patternIdValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    patternIdValue->setStyleSheet("color: #666; font-family: monospace;");
    basicInfoLayout->addRow(patternIdLabel, patternIdValue);
    
    // 패턴 이름
    patternNameLabel = new QLabel("이름:", basicInfoGroup);
    patternNameEdit = new QLineEdit(basicInfoGroup);
    patternNameEdit->setFixedHeight(24);
    basicInfoLayout->addRow(patternNameLabel, patternNameEdit);
    
    // 패턴 타입 (동적 색상 적용)
    patternTypeLabel = new QLabel("타입:", basicInfoGroup);
    patternTypeValue = new QLabel(basicInfoGroup);
    patternTypeValue->setAlignment(Qt::AlignCenter);
    patternTypeValue->setFixedHeight(24);
    patternTypeValue->setStyleSheet(
        "QLabel { "
        "  border: 1px solid #ccc; "
        "  border-radius: 4px; "
        "  padding: 2px 8px; "
        "  font-weight: bold; "
        "  color: white; "
        "}"
    );
    basicInfoLayout->addRow(patternTypeLabel, patternTypeValue);

    mainContentLayout->addWidget(basicInfoGroup);

    // === 위치 및 크기 그룹 ===
    QGroupBox* positionSizeGroup = new QGroupBox("위치 및 크기", scrollContent);
    positionSizeGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    QFormLayout* positionSizeLayout = new QFormLayout(positionSizeGroup);
    positionSizeLayout->setVerticalSpacing(5);
    positionSizeLayout->setContentsMargins(10, 15, 10, 10);
    
    // 좌표 설정
    positionLabel = new QLabel("좌표:", positionSizeGroup);
    QWidget* posWidget = new QWidget(positionSizeGroup);
    QHBoxLayout* posLayout = new QHBoxLayout(posWidget);
    posLayout->setContentsMargins(0, 0, 0, 0);
    posLayout->setSpacing(8);
    
    QLabel* xLabel = new QLabel("X:", posWidget);
    xLabel->setFixedWidth(15);
    xLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    patternXSpin = new QSpinBox(posWidget);
    patternXSpin->setFixedHeight(24);
    patternXSpin->setRange(0, 9999);
    
    QLabel* yLabel = new QLabel("Y:", posWidget);
    yLabel->setFixedWidth(15);
    yLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    patternYSpin = new QSpinBox(posWidget);
    patternYSpin->setFixedHeight(24);
    patternYSpin->setRange(0, 9999);
    
    posLayout->addWidget(xLabel);
    posLayout->addWidget(patternXSpin, 1);
    posLayout->addWidget(yLabel);
    posLayout->addWidget(patternYSpin, 1);
    positionSizeLayout->addRow(positionLabel, posWidget);
    
    // 크기 설정
    sizeLabel = new QLabel("크기:", positionSizeGroup);
    QWidget* sizeWidget = new QWidget(positionSizeGroup);
    QHBoxLayout* sizeLayout = new QHBoxLayout(sizeWidget);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->setSpacing(8);
    
    QLabel* wLabel = new QLabel("W:", sizeWidget);
    wLabel->setFixedWidth(15);
    wLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    patternWSpin = new QSpinBox(sizeWidget);
    patternWSpin->setFixedHeight(24);
    patternWSpin->setRange(1, 9999);
    
    QLabel* hLabel = new QLabel("H:", sizeWidget);
    hLabel->setFixedWidth(15);
    hLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    patternHSpin = new QSpinBox(sizeWidget);
    patternHSpin->setFixedHeight(24);
    patternHSpin->setRange(1, 9999);
    
    sizeLayout->addWidget(wLabel);
    sizeLayout->addWidget(patternWSpin, 1);
    sizeLayout->addWidget(hLabel);
    sizeLayout->addWidget(patternHSpin, 1);
    positionSizeLayout->addRow(sizeLabel, sizeWidget);

    // 회전 각도
    angleLabel = new QLabel("각도:", positionSizeGroup);
    QWidget* angleWidget = new QWidget(positionSizeGroup);
    QHBoxLayout* angleLayout = new QHBoxLayout(angleWidget);
    angleLayout->setContentsMargins(0, 0, 0, 0);
    angleLayout->setSpacing(5);
    
    angleEdit = new QLineEdit(angleWidget);
    angleEdit->setFixedHeight(24);
    angleEdit->setText("0.0");
    angleEdit->setPlaceholderText("0.0");
    
    QLabel* degreeLabel = new QLabel("°", angleWidget);
    
    angleLayout->addWidget(angleEdit, 1);
    angleLayout->addWidget(degreeLabel);
    positionSizeLayout->addRow(angleLabel, angleWidget);

    mainContentLayout->addWidget(positionSizeGroup);

    // 패턴 타입별 특수 속성 스택
    specialPropStack = new QStackedWidget(scrollContent);
    mainContentLayout->addWidget(specialPropStack);

    // 1. ROI 속성 - 체크박스 왼쪽 정렬
    QWidget* roiPropWidget = new QWidget(specialPropStack);
    QVBoxLayout* roiLayout = new QVBoxLayout(roiPropWidget);
    roiLayout->setContentsMargins(0, 0, 0, 0);
    roiLayout->setSpacing(3);
    roiLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop); // 왼쪽 상단 정렬

    roiIncludeAllCheck = new QCheckBox("전체 카메라 영역 포함", roiPropWidget);
    roiLayout->addWidget(roiIncludeAllCheck);
    specialPropStack->addWidget(roiPropWidget);

    // 2. FID 속성 - QVBoxLayout 사용
    QWidget* fidPropWidget = new QWidget(specialPropStack);
    QVBoxLayout* fidLayout = new QVBoxLayout(fidPropWidget);
    fidLayout->setContentsMargins(0, 0, 0, 0);
    fidLayout->setSpacing(3);
    fidLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop); // 왼쪽 상단 정렬

    // 매칭 검사 활성화 체크박스
    fidMatchCheckBox = new QCheckBox("매칭 검사 활성화", fidPropWidget);
    fidLayout->addWidget(fidMatchCheckBox);

    // FID 패턴에서 매칭 방법 및 매칭 검사 옵션 추가
    fidMatchMethodLabel = new QLabel("매칭 방법:", fidPropWidget);
    fidMatchMethodCombo = new QComboBox(fidPropWidget);
    fidMatchMethodCombo->addItem("템플릿 매칭", 0);
    fidMatchMethodCombo->addItem("특징점 매칭", 1);
    
    QHBoxLayout* fidMatchMethodLayout = new QHBoxLayout();
    fidMatchMethodLayout->addWidget(fidMatchMethodLabel);
    fidMatchMethodLayout->addWidget(fidMatchMethodCombo);
    fidMatchMethodLayout->addStretch();
    fidLayout->addLayout(fidMatchMethodLayout);
    
    // 매칭 임계값 (불량 판정 기준)
    QHBoxLayout* fidMatchThreshLayout = new QHBoxLayout();
    fidMatchThreshLabel = new QLabel("매칭 임계값:", fidPropWidget);
    fidMatchThreshSpin = new QDoubleSpinBox(fidPropWidget);
    fidMatchThreshSpin->setRange(0.1, 1.0);
    fidMatchThreshSpin->setSingleStep(0.05);
    fidMatchThreshSpin->setValue(0.7);
    fidMatchThreshLayout->addWidget(fidMatchThreshLabel);
    fidMatchThreshLayout->addWidget(fidMatchThreshSpin);
    fidMatchThreshLayout->addStretch();
    fidLayout->addLayout(fidMatchThreshLayout);

    // 회전 허용 - 체크박스 왼쪽 정렬
    fidRotationCheck = new QCheckBox("회전 허용", fidPropWidget);
    fidLayout->addWidget(fidRotationCheck);

    // 회전 각도 범위
    QHBoxLayout* fidAngleLayout = new QHBoxLayout();
    fidAngleLayout->setContentsMargins(0, 0, 0, 0);
    fidAngleLayout->setSpacing(5);
    fidAngleLabel = new QLabel("회전 각도 범위:", fidPropWidget);
    fidMinAngleSpin = new QDoubleSpinBox(fidPropWidget);
    fidMinAngleSpin->setFixedHeight(22);
    fidMinAngleSpin->setRange(-15, 0);
    fidMinAngleSpin->setSingleStep(1);
    fidMinAngleSpin->setValue(-5);
    fidMinAngleSpin->setSuffix("°");
    fidToLabel = new QLabel("~", fidPropWidget);
    fidMaxAngleSpin = new QDoubleSpinBox(fidPropWidget);
    fidMaxAngleSpin->setFixedHeight(22);
    fidMaxAngleSpin->setRange(0, 15);
    fidMaxAngleSpin->setSingleStep(1);
    fidMaxAngleSpin->setValue(5);
    fidMaxAngleSpin->setSuffix("°");
    fidAngleLayout->addWidget(fidAngleLabel);
    fidAngleLayout->addWidget(fidMinAngleSpin);
    fidAngleLayout->addWidget(fidToLabel);
    fidAngleLayout->addWidget(fidMaxAngleSpin);
    fidAngleLayout->addStretch();
    fidLayout->addLayout(fidAngleLayout);

    // 각도 스텝
    QHBoxLayout* fidStepLayout = new QHBoxLayout();
    fidStepLayout->setContentsMargins(0, 0, 0, 0);
    fidStepLayout->setSpacing(5);
    fidStepLabel = new QLabel("각도 스텝:", fidPropWidget);
    fidStepSpin = new QDoubleSpinBox(fidPropWidget);
    fidStepSpin->setFixedHeight(22);
    fidStepSpin->setRange(0.1, 10);
    fidStepSpin->setSingleStep(0.5);
    fidStepSpin->setValue(1.0);
    fidStepSpin->setSuffix("°");
    fidStepLayout->addWidget(fidStepLabel);
    fidStepLayout->addWidget(fidStepSpin);
    fidStepLayout->addStretch();
    fidLayout->addLayout(fidStepLayout);

    // 템플릿 이미지 미리보기
    QHBoxLayout* fidImageLayout = new QHBoxLayout();
    fidImageLayout->setContentsMargins(0, 0, 0, 0);
    fidImageLayout->setSpacing(5);
    fidTemplateImgLabel = new QLabel("템플릿 이미지:", fidPropWidget);
    fidTemplateImg = new QLabel(fidPropWidget);
    fidTemplateImg->setFixedSize(120, 90);
    fidTemplateImg->setAlignment(Qt::AlignCenter);
    fidTemplateImg->setStyleSheet("background-color: #eee; border: 1px solid #ccc;");
    fidTemplateImg->setText(TR("NO_IMAGE"));
    fidTemplateImg->setCursor(Qt::PointingHandCursor);
    fidTemplateImg->installEventFilter(this);
    fidImageLayout->addWidget(fidTemplateImgLabel);
    fidImageLayout->addWidget(fidTemplateImg);
    fidImageLayout->addStretch();
    fidLayout->addLayout(fidImageLayout);

    specialPropStack->addWidget(fidPropWidget);

    // 3. INS 속성 패널 생성 (카테고리별 그룹화)
    QWidget* insPropWidget = new QWidget(specialPropStack);
    QVBoxLayout* insMainLayout = new QVBoxLayout(insPropWidget);
    insMainLayout->setContentsMargins(0, 0, 0, 0);
    insMainLayout->setSpacing(8);

    // === 기본 검사 설정 그룹 ===
    QGroupBox* basicInspectionGroup = new QGroupBox("기본 검사 설정", insPropWidget);
    basicInspectionGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    QFormLayout* basicInspectionLayout = new QFormLayout(basicInspectionGroup);
    basicInspectionLayout->setVerticalSpacing(5);
    basicInspectionLayout->setContentsMargins(10, 15, 10, 10);

    // 검사 방법
    insMethodLabel = new QLabel("검사 방법:", basicInspectionGroup);
    insMethodCombo = new QComboBox(basicInspectionGroup);
    insMethodCombo->addItem(InspectionMethod::getName(InspectionMethod::COLOR));
    insMethodCombo->addItem(InspectionMethod::getName(InspectionMethod::EDGE));
    insMethodCombo->addItem(InspectionMethod::getName(InspectionMethod::BINARY));
    insMethodCombo->addItem(InspectionMethod::getName(InspectionMethod::AI_MATCH1));
    insMethodCombo->addItem(InspectionMethod::getName(InspectionMethod::STRIP));
    basicInspectionLayout->addRow(insMethodLabel, insMethodCombo);

    // 합격 임계값
    insPassThreshLabel = new QLabel("합격 임계값:", basicInspectionGroup);
    insPassThreshSpin = new QDoubleSpinBox(basicInspectionGroup);
    insPassThreshSpin->setFixedHeight(22);
    insPassThreshSpin->setRange(0.1, 1.0);
    insPassThreshSpin->setSingleStep(0.05);
    insPassThreshSpin->setValue(0.9);
    basicInspectionLayout->addRow(insPassThreshLabel, insPassThreshSpin);

    // 결과 반전
    insInvertCheck = new QCheckBox("결과 반전 (예: 결함 검출)", basicInspectionGroup);
    basicInspectionLayout->addRow("", insInvertCheck);

    insMainLayout->addWidget(basicInspectionGroup);

    // === 이진화 검사 설정 그룹 ===
    insBinaryPanel = new QGroupBox("이진화 검사 설정", insPropWidget);
    insBinaryPanel->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    QFormLayout* insBinaryLayout = new QFormLayout(insBinaryPanel);
    insBinaryLayout->setVerticalSpacing(5);
    insBinaryLayout->setContentsMargins(10, 15, 10, 10);

    // 이진화 임계값
    insThreshLabel = new QLabel("이진화 임계값:", insBinaryPanel);
    insThreshSpin = new QSpinBox(insBinaryPanel);
    insThreshSpin->setRange(0, 255);
    insThreshSpin->setValue(128);
    insBinaryLayout->addRow(insThreshLabel, insThreshSpin);

    // 비교 방식
    insCompareLabel = new QLabel("비교 방식:", insBinaryPanel);
    insCompareCombo = new QComboBox(insBinaryPanel);
    insCompareCombo->addItem("이상 (>=)");
    insCompareCombo->addItem("이하 (<=)");
    insCompareCombo->addItem("범위 내");
    insBinaryLayout->addRow(insCompareLabel, insCompareCombo);

    // 합격 기준
    insThresholdLabel = new QLabel("합격 기준:", insBinaryPanel);
    insThresholdSpin = new QDoubleSpinBox(insBinaryPanel);
    insThresholdSpin->setRange(0.0, 1.0);
    insThresholdSpin->setSingleStep(0.01);
    insThresholdSpin->setValue(0.5);
    insBinaryLayout->addRow(insThresholdLabel, insThresholdSpin);

    // 범위 설정 (범위 내 옵션용)
    QWidget* rangeWidget = new QWidget(insBinaryPanel);
    QHBoxLayout* rangeLayout = new QHBoxLayout(rangeWidget);
    rangeLayout->setContentsMargins(0, 0, 0, 0);
    rangeLayout->setSpacing(5);
    
    insLowerLabel = new QLabel("하한:", rangeWidget);
    insLowerSpin = new QDoubleSpinBox(rangeWidget);
    insLowerSpin->setRange(0.0, 1.0);
    insLowerSpin->setSingleStep(0.01);
    insLowerSpin->setValue(0.3);
    
    insUpperLabel = new QLabel("상한:", rangeWidget);
    insUpperSpin = new QDoubleSpinBox(rangeWidget);
    insUpperSpin->setRange(0.0, 1.0);
    insUpperSpin->setSingleStep(0.01);
    insUpperSpin->setValue(0.7);
    
    rangeLayout->addWidget(insLowerLabel);
    rangeLayout->addWidget(insLowerSpin);
    rangeLayout->addWidget(insUpperLabel);
    rangeLayout->addWidget(insUpperSpin);
    rangeLayout->addStretch();
    
    insBinaryLayout->addRow("범위 설정:", rangeWidget);

    // 측정 대상
    insRatioTypeLabel = new QLabel("측정 대상:", insBinaryPanel);
    insRatioTypeCombo = new QComboBox(insBinaryPanel);
    insRatioTypeCombo->addItem("흰색 픽셀 비율");
    insRatioTypeCombo->addItem("검은색 픽셀 비율");
    insBinaryLayout->addRow(insRatioTypeLabel, insRatioTypeCombo);

    insMainLayout->addWidget(insBinaryPanel);

    // === 템플릿 이미지 그룹 ===
    QGroupBox* templateGroup = new QGroupBox("템플릿 이미지", insPropWidget);
    templateGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    QVBoxLayout* templateLayout = new QVBoxLayout(templateGroup);
    templateLayout->setContentsMargins(10, 15, 10, 10);

    // 템플릿 이미지 미리보기 - 중앙정렬
    insTemplateImg = new QLabel(templateGroup);
    insTemplateImg->setFixedSize(120, 90);
    insTemplateImg->setAlignment(Qt::AlignCenter);
    insTemplateImg->setStyleSheet(
        "background-color: #f5f5f5; "
        "border: 2px dashed #ccc; "
        "border-radius: 4px;"
    );
    insTemplateImg->setText("클릭하여\n이미지 선택");
    insTemplateImg->setCursor(Qt::PointingHandCursor);
    insTemplateImg->installEventFilter(this);
    
    // 이미지를 중앙에 배치
    QHBoxLayout* insImageCenterLayout = new QHBoxLayout();
    insImageCenterLayout->addStretch();
    insImageCenterLayout->addWidget(insTemplateImg);
    insImageCenterLayout->addStretch();
    
    templateLayout->addLayout(insImageCenterLayout);
    insMainLayout->addWidget(templateGroup);

    // === STRIP 검사 파라미터 그룹 ===
    insStripPanel = new QGroupBox("STRIP 검사 파라미터", insPropWidget);
    insStripPanel->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
    );
    QFormLayout* insStripLayout = new QFormLayout(insStripPanel);
    insStripLayout->setVerticalSpacing(5);
    insStripLayout->setContentsMargins(10, 15, 10, 10);

    // 형태학적 커널 크기
    insStripKernelLabel = new QLabel("형태학적 커널:", insStripPanel);
    insStripKernelSpin = new QSpinBox(insStripPanel);
    insStripKernelSpin->setRange(3, 15);
    insStripKernelSpin->setSingleStep(2);  // 홀수만
    insStripKernelSpin->setValue(3);
    insStripLayout->addRow(insStripKernelLabel, insStripKernelSpin);

    // Gradient 임계값
    insStripGradThreshLabel = new QLabel("Gradient 임계값:", insStripPanel);
    insStripGradThreshSpin = new QDoubleSpinBox(insStripPanel);
    insStripGradThreshSpin->setRange(0.5, 20.0);
    insStripGradThreshSpin->setSingleStep(0.5);
    insStripGradThreshSpin->setValue(3.0);
    insStripGradThreshSpin->setSuffix(" px");
    insStripLayout->addRow(insStripGradThreshLabel, insStripGradThreshSpin);

    // Gradient 계산 범위
    QWidget* gradientRangeWidget = new QWidget(insStripPanel);
    QHBoxLayout* gradientRangeLayout = new QHBoxLayout(gradientRangeWidget);
    gradientRangeLayout->setContentsMargins(0, 0, 0, 0);
    gradientRangeLayout->setSpacing(5);
    
    insStripStartLabel = new QLabel("시작:", gradientRangeWidget);
    insStripStartSpin = new QSpinBox(gradientRangeWidget);
    insStripStartSpin->setRange(0, 50);
    insStripStartSpin->setValue(20);
    insStripStartSpin->setSuffix("%");
    
    insStripEndLabel = new QLabel("끝:", gradientRangeWidget);
    insStripEndSpin = new QSpinBox(gradientRangeWidget);
    insStripEndSpin->setRange(50, 100);
    insStripEndSpin->setValue(80);
    insStripEndSpin->setSuffix("%");
    
    gradientRangeLayout->addWidget(insStripStartLabel);
    gradientRangeLayout->addWidget(insStripStartSpin);
    gradientRangeLayout->addWidget(insStripEndLabel);
    gradientRangeLayout->addWidget(insStripEndSpin);
    gradientRangeLayout->addStretch();
    
    insStripLayout->addRow("Gradient 범위:", gradientRangeWidget);

    // 최소 데이터 포인트
    insStripMinPointsLabel = new QLabel("최소 포인트:", insStripPanel);
    insStripMinPointsSpin = new QSpinBox(insStripPanel);
    insStripMinPointsSpin->setRange(3, 20);
    insStripMinPointsSpin->setValue(5);
    insStripLayout->addRow(insStripMinPointsLabel, insStripMinPointsSpin);

    insMainLayout->addWidget(insStripPanel);

    // 여백 추가
    insMainLayout->addStretch();

    // 패널 초기 설정 - 검사 방법에 따라 표시
    insBinaryPanel->setVisible(false);  // 처음에는 숨김
    insStripPanel->setVisible(false);   // STRIP 패널도 처음에는 숨김

    // 검사 방법에 따른 패널 표시 설정
    connect(insMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
        [this](int index) {
            insBinaryPanel->setVisible(index == InspectionMethod::BINARY);  // 이진화
            insStripPanel->setVisible(index == InspectionMethod::STRIP);    // STRIP
            // AI 기반 검사에서는 결과 반전 옵션 필요 없음
            if (insInvertCheck) {
                bool visible = (index != InspectionMethod::AI_MATCH1);
                insInvertCheck->setVisible(visible);
                if (!visible) insInvertCheck->setChecked(false);
            }
    });

    // 특수 속성 스택에 INS 패널 추가
    specialPropStack->addWidget(insPropWidget);

    // 스크롤 영역에 컨텐츠 설정
    scrollArea->setWidget(scrollContent);
    patternContentLayout->addWidget(scrollArea);
    propertyStackWidget->addWidget(patternPanel);
    
    // 4. 필터 속성 패널을 위한 컨테이너 추가
    QWidget* filterPanelContainer = new QWidget(propertyStackWidget);
    QVBoxLayout* filterContainerLayout = new QVBoxLayout(filterPanelContainer);
    filterContainerLayout->setContentsMargins(0, 0, 0, 0);
    
    // 필터 설명 레이블
    filterDescLabel = new QLabel("필터 설정", filterPanelContainer);
    filterDescLabel->setStyleSheet("font-weight: bold; color: #333; font-size: 11pt; margin-top: 4px; margin-bottom: 1px;");
    filterContainerLayout->addWidget(filterDescLabel);
    
    // 스크롤 영역 추가
    QScrollArea* filterScrollArea = new QScrollArea(filterPanelContainer);
    filterScrollArea->setWidgetResizable(true);
    filterScrollArea->setFrameShape(QFrame::NoFrame);
    
    // 필터 위젯이 여기에 추가됨
    filterPropertyContainer = new QWidget(filterScrollArea);
    QVBoxLayout* filterLayout = new QVBoxLayout(filterPropertyContainer);
    filterLayout->setContentsMargins(5, 5, 5, 5);
    
    // 기본 안내 라벨
    filterInfoLabel = new QLabel("필터를 선택하면 여기에 설정이 표시됩니다", filterPropertyContainer);
    filterInfoLabel->setAlignment(Qt::AlignCenter);
    filterInfoLabel->setStyleSheet("color: gray; font-style: italic;");
    filterLayout->addWidget(filterInfoLabel);
    
    // 스크롤 영역에 필터 위젯 추가
    filterScrollArea->setWidget(filterPropertyContainer);
    filterContainerLayout->addWidget(filterScrollArea);
    
    // 필터 패널을 스택에 추가
    propertyStackWidget->addWidget(filterPanelContainer);
    
    // 이벤트 연결 설정
    connectPropertyPanelEvents();
    
    // 처음에는 빈 패널 표시
    propertyStackWidget->setCurrentIndex(0);
}

void TeachingWidget::showImageViewerDialog(const QImage& image, const QString& title) {
    // 기본 대화상자 생성
    QDialog* imageDialog = new QDialog(this);
    imageDialog->setWindowTitle(title);
    imageDialog->setMinimumSize(400, 400);
    imageDialog->resize(600, 500);
    
    QVBoxLayout* layout = new QVBoxLayout(imageDialog);
    
    // 스케일 표시용 레이블 (미리 생성)
    QLabel* scaleLabel = new QLabel("Scale: 100%", imageDialog);
    
    // 스크롤 영역 생성
    QScrollArea* scrollArea = new QScrollArea(imageDialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setAlignment(Qt::AlignCenter);
    
    // 이미지 표시용 확장 레이블 클래스 생성
    class ZoomableImageLabel : public QLabel {
    public:
        ZoomableImageLabel(QLabel* scaleLabel, QWidget* parent = nullptr) 
            : QLabel(parent), 
              scale(1.0), 
              isDragging(false), 
              originalPixmap(),
              scaleLabel(scaleLabel) {
            setAlignment(Qt::AlignCenter);
            setCursor(Qt::OpenHandCursor);
            
            // 초기 포커스 정책 설정
            setFocusPolicy(Qt::StrongFocus);
            setMouseTracking(true);
        }
        
        void setOriginalPixmap(const QPixmap& pixmap) {
            originalPixmap = pixmap;
            if (!originalPixmap.isNull()) {
                // 초기 설정: 최소 충분한 크기로 설정
                setMinimumSize(originalPixmap.width(), originalPixmap.height());
                updatePixmap();
            }
        }
        
        void setScale(double newScale) {
            scale = qBound(0.1, newScale, 10.0); // 최소 0.1x, 최대 10x
            updatePixmap();
            
            // 스케일 정보 표시 - 직접 scaleLabel 업데이트
            if (scaleLabel) {
                scaleLabel->setText(QString("Scale: %1%").arg(qRound(scale * 100)));
            }
            
            // 크기 변경 - 스크롤바 제대로 표시되도록
            if (!originalPixmap.isNull()) {
                int newWidth = qRound(originalPixmap.width() * scale);
                int newHeight = qRound(originalPixmap.height() * scale);
                setMinimumSize(newWidth, newHeight);
            }
            
            // 프로퍼티 업데이트 (버튼 이벤트에서 사용)
            setProperty("scale", scale);
        }
        
        double getScale() const {
            return scale;
        }
        
        void fitToView(const QSize& viewSize) {
            if (originalPixmap.isNull()) return;
            
            double widthScale = (double)viewSize.width() / originalPixmap.width();
            double heightScale = (double)viewSize.height() / originalPixmap.height();
            double fitScale = qMin(widthScale, heightScale) * 0.95; // 약간의 여백
            
            setScale(fitScale);
            scrollOffset = QPoint(0, 0); // 스크롤 위치 초기화
            updatePixmap();
        }
        
    protected:
        void wheelEvent(QWheelEvent* event) override {
            int delta = event->angleDelta().y();
            double factor = (delta > 0) ? 1.1 : 0.9;
            setScale(scale * factor);
            event->accept();
        }
        
        void mousePressEvent(QMouseEvent* event) override {
            if (event->button() == Qt::LeftButton) {
                isDragging = true;
                lastDragPos = event->pos();
                setCursor(Qt::ClosedHandCursor);
            }
            QLabel::mousePressEvent(event);
        }
        
        void mouseMoveEvent(QMouseEvent* event) override {
            if (isDragging) {
                QPoint delta = event->pos() - lastDragPos;
                scrollOffset += delta;
                lastDragPos = event->pos();
                updatePixmap();
            }
            QLabel::mouseMoveEvent(event);
        }
        
        void mouseReleaseEvent(QMouseEvent* event) override {
            if (event->button() == Qt::LeftButton) {
                isDragging = false;
                setCursor(Qt::OpenHandCursor);
            }
            QLabel::mouseReleaseEvent(event);
        }
        
        void resizeEvent(QResizeEvent* event) override {
            QLabel::resizeEvent(event);
            if (!originalPixmap.isNull()) {
                updatePixmap();
            }
        }
        
    private:
        void updatePixmap() {
            if (originalPixmap.isNull()) return;
            
            // 원본 크기 가져오기
            int originalWidth = originalPixmap.width();
            int originalHeight = originalPixmap.height();
            
            // 새로운 크기 계산
            int newWidth = qRound(originalWidth * scale);
            int newHeight = qRound(originalHeight * scale);
            
            // 스케일된 픽스맵 생성
            QPixmap scaledPixmap = originalPixmap.scaled(
                newWidth, newHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            
            // 화면 중앙 기준으로 오프셋 적용
            QPixmap finalPixmap(qMax(width(), newWidth), qMax(height(), newHeight));
            finalPixmap.fill(Qt::transparent);
            
            QPainter painter(&finalPixmap);
            
            // 이미지 중앙 계산
            int centerX = width() / 2 + scrollOffset.x();
            int centerY = height() / 2 + scrollOffset.y();
            
            // 이미지 그리기
            int x = centerX - scaledPixmap.width() / 2;
            int y = centerY - scaledPixmap.height() / 2;
            painter.drawPixmap(x, y, scaledPixmap);
            
            setPixmap(finalPixmap);
        }
        
        double scale;
        bool isDragging;
        QPoint lastDragPos;
        QPoint scrollOffset;
        QPixmap originalPixmap;
        QLabel* scaleLabel; // 스케일 표시용 레이블 참조
    };
    
    // 확대/축소 가능한 레이블 생성
    ZoomableImageLabel* imageLabel = new ZoomableImageLabel(scaleLabel, scrollArea);
    imageLabel->setOriginalPixmap(QPixmap::fromImage(image));
    
    // 스크롤 영역에 레이블 설정
    scrollArea->setWidget(imageLabel);
    
    // 버튼 레이아웃
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    // 확대/축소 버튼
    QPushButton* zoomInButton = new QPushButton("+", imageDialog);
    QPushButton* zoomOutButton = new QPushButton("-", imageDialog);
    QPushButton* resetButton = new QPushButton("원본 크기", imageDialog);
    QPushButton* fitButton = new QPushButton("화면에 맞춤", imageDialog);
    QPushButton* closeButton = new QPushButton("닫기", imageDialog);
    
    // 버튼 레이아웃에 위젯 추가
    buttonLayout->addWidget(zoomInButton);
    buttonLayout->addWidget(zoomOutButton);
    buttonLayout->addWidget(resetButton);
    buttonLayout->addWidget(fitButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(scaleLabel);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    
    // 메인 레이아웃에 위젯 추가
    layout->addWidget(scrollArea);
    layout->addLayout(buttonLayout);
    
    // 버튼 이벤트 연결
    connect(zoomInButton, &QPushButton::clicked, [imageLabel]() {
        imageLabel->setScale(imageLabel->getScale() * 1.2);
    });
    
    connect(zoomOutButton, &QPushButton::clicked, [imageLabel]() {
        imageLabel->setScale(imageLabel->getScale() / 1.2);
    });
    
    connect(resetButton, &QPushButton::clicked, [imageLabel]() {
        imageLabel->setScale(1.0);
    });
    
    connect(fitButton, &QPushButton::clicked, [imageLabel, scrollArea]() {
        imageLabel->fitToView(scrollArea->viewport()->size());
    });
    
    connect(closeButton, &QPushButton::clicked, imageDialog, &QDialog::accept);
    
    // 초기 스케일 정보 저장
    imageLabel->setProperty("scale", 1.0);
    
    // 도움말 추가
    QLabel* helpLabel = new QLabel("마우스 휠: 확대/축소 | 드래그: 이동", imageDialog);
    helpLabel->setAlignment(Qt::AlignCenter);
    helpLabel->setStyleSheet("color: gray; font-style: italic;");
    layout->addWidget(helpLabel);
    
    imageDialog->adjustSize();
    // 기본 배율을 100%로 설정 (원본 크기)
    imageLabel->setScale(1.0);
    
    // 대화상자 표시
    imageDialog->exec();
    
    // 사용 후 메모리 해제
    delete imageDialog;
}

void TeachingWidget::updateFidTemplateImage(const QUuid& patternId) {
    PatternInfo* pattern = cameraView->getPatternById(patternId);
    if (pattern && pattern->type == PatternType::FID) {
        updateFidTemplateImage(pattern, pattern->rect);
    }
}

void TeachingWidget::updateInsTemplateImage(const QUuid& patternId) {
    PatternInfo* pattern = cameraView->getPatternById(patternId);
    if (pattern && pattern->type == PatternType::INS) {
        updateInsTemplateImage(pattern, pattern->rect);
    }
}

void TeachingWidget::updateInsTemplateImage(PatternInfo* pattern, const QRectF& newRect) {
    if (!pattern || pattern->type != PatternType::INS) {
        return;
    }
    
    // **검사 모드일 때는 템플릿 이미지 갱신 금지**
    if (cameraView && cameraView->getInspectionMode()) {
        return;
    }
    
    
    cv::Mat sourceFrame;
    
    // 시뮬레이션 모드인지 확인
    if (simulationMode && !currentSimulationImage.empty()) {
        // 시뮬레이션 모드: 현재 시뮬레이션 이미지 사용
        sourceFrame = currentSimulationImage.clone();
    } else {
        // 일반 카메라 모드: 카메라 프레임 사용
        if (cameraIndex < 0 || cameraIndex >= static_cast<int>(cameraFrames.size()) || 
            cameraFrames[cameraIndex].empty()) {
            return;
        }
        sourceFrame = cameraFrames[cameraIndex].clone();
    }
    
    // 1. 전체 프레임 복사 (원본 이미지 사용 - 필터 적용 안함)
    cv::Mat originalFrame = sourceFrame.clone();
    
    // 2. INS 템플릿 이미지는 원본에서 생성 (필터 적용하지 않음)
    
    // 3. INS 템플릿 이미지: 회전 고려하여 추출하되 ROI 크기 유지
    cv::Mat roiMat;
    
    // INS 템플릿 이미지: FID와 동일한 방식으로 정사각형으로 자르고 마스킹
    cv::Point2f center(newRect.x() + newRect.width()/2.0f, newRect.y() + newRect.height()/2.0f);
    
    // 회전각에 따른 최소 필요 사각형 크기 계산
    double angleRad = std::abs(pattern->angle) * M_PI / 180.0;
    double width = newRect.width();
    double height = newRect.height();
    
    // 회전된 사각형의 경계 상자 크기 계산
    double rotatedWidth = std::abs(width * std::cos(angleRad)) + std::abs(height * std::sin(angleRad));
    double rotatedHeight = std::abs(width * std::sin(angleRad)) + std::abs(height * std::cos(angleRad));
    
    // 정사각형 크기는 회전된 경계 상자 중 더 큰 값 + 여유분
    int maxSize = static_cast<int>(std::max(rotatedWidth, rotatedHeight)) + 10;
    
    // 정사각형 ROI 영역 계산 (중심점 기준)
    int halfSize = maxSize / 2;
    cv::Rect squareRoi(
        static_cast<int>(center.x) - halfSize,
        static_cast<int>(center.y) - halfSize,
        maxSize,
        maxSize
    );
    
    // 이미지 경계와 교집합 구하기
    cv::Rect imageBounds(0, 0, originalFrame.cols, originalFrame.rows);
    cv::Rect validRoi = squareRoi & imageBounds;
    
    if (validRoi.width > 0 && validRoi.height > 0) {
        // 정사각형 결과 이미지 생성 (검은색 배경)
        roiMat = cv::Mat::zeros(maxSize, maxSize, originalFrame.type());
        
        // 유효한 영역만 복사
        int offsetX = validRoi.x - squareRoi.x;
        int offsetY = validRoi.y - squareRoi.y;
        
        cv::Mat validImage = originalFrame(validRoi);
        cv::Rect resultRect(offsetX, offsetY, validRoi.width, validRoi.height);
        validImage.copyTo(roiMat(resultRect));
        
        // 패턴 영역 외부 마스킹 (패턴 영역만 보이도록)
        cv::Mat mask = cv::Mat::zeros(maxSize, maxSize, CV_8UC1);
        
        // 정사각형 중심을 기준으로 패턴 영역 계산
        cv::Point2f patternCenter(maxSize / 2.0f, maxSize / 2.0f);
        cv::Size2f patternSize(newRect.width(), newRect.height());
        
        if (std::abs(pattern->angle) > 0.1) {
            // 회전된 패턴의 경우: 회전된 사각형 마스크
            cv::Point2f vertices[4];
            cv::RotatedRect rotatedRect(patternCenter, patternSize, pattern->angle);
            rotatedRect.points(vertices);
            
            std::vector<cv::Point> points;
            for (int i = 0; i < 4; i++) {
                points.push_back(cv::Point(static_cast<int>(vertices[i].x), 
                                         static_cast<int>(vertices[i].y)));
            }
            
            cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{points}, cv::Scalar(255));
        } else {
            // 회전 없는 경우: 일반 사각형 마스크
            cv::Rect patternRect(
                static_cast<int>(patternCenter.x - patternSize.width / 2),
                static_cast<int>(patternCenter.y - patternSize.height / 2),
                static_cast<int>(patternSize.width),
                static_cast<int>(patternSize.height)
            );
            cv::rectangle(mask, patternRect, cv::Scalar(255), -1);
        }
        
        // 마스크 반전: 패턴 영역 외부를 검은색으로 설정
        cv::Mat invertedMask;
        cv::bitwise_not(mask, invertedMask);
        
        // 패턴 영역 외부를 검은색으로 마스킹
        roiMat.setTo(cv::Scalar(0, 0, 0), invertedMask);
    } else {
        return;
    }
            
        
        if (roiMat.empty()) {
            return;
        }
        
        // 4. 자신의 필터 적용 (필요하다면)
    for (const FilterInfo& filter : pattern->filters) {
        if (filter.enabled) {
            cv::Mat filtered;
            ImageProcessor processor;
            processor.applyFilter(roiMat, filtered, filter);
            if (!filtered.empty()) {
                roiMat = filtered.clone();
            }
        }
    }
    
    // 5. INS 패턴이 이진화 검사(BINARY)를 사용하는 경우, 이진화 타입 반영
    if (pattern->inspectionMethod == InspectionMethod::BINARY) {
        cv::Mat gray;
        if (roiMat.channels() == 3) {
            cv::cvtColor(roiMat, gray, cv::COLOR_BGR2GRAY);
        } else {
            roiMat.copyTo(gray);
        }
        
        // 이진화 타입 설정 - 패턴 속성에서 가져옴
        int thresholdType = cv::THRESH_BINARY;
        if (pattern->ratioType == 1) { // 검은색 비율 사용 시 반전 이진화
            thresholdType = cv::THRESH_BINARY_INV;
        }
        
        cv::Mat binary;
        cv::threshold(gray, binary, pattern->binaryThreshold, 255, thresholdType);
        
        // 이진화된 결과를 다시 컬러 이미지로 변환 (QImage 호환성을 위해)
        cv::cvtColor(binary, roiMat, cv::COLOR_GRAY2BGR);
    }

    // 6. BGR -> RGB 변환 (QImage 생성용)
    if (roiMat.channels() == 3) {
        cv::cvtColor(roiMat, roiMat, cv::COLOR_BGR2RGB);
    }

    // 7. QImage로 변환
    QImage qimg;
    if (roiMat.isContinuous()) {
        qimg = QImage(roiMat.data, roiMat.cols, roiMat.rows, roiMat.step, QImage::Format_RGB888);
    } else {
        qimg = QImage(roiMat.cols, roiMat.rows, QImage::Format_RGB888);
        for (int y = 0; y < roiMat.rows; y++) {
            memcpy(qimg.scanLine(y), roiMat.ptr<uchar>(y), roiMat.cols * 3);
        }
    }
    
    // 8. 패턴의 템플릿 이미지 업데이트
    pattern->templateImage = qimg.copy();
    
    qDebug() << QString("FID 패턴 '%1' 템플릿 이미지 설정: 크기=%2x%3, null=%4")
                .arg(pattern->name)
                .arg(pattern->templateImage.width())
                .arg(pattern->templateImage.height())
                .arg(pattern->templateImage.isNull());

    // UI 업데이트
    if (insTemplateImg) {
        if (!pattern->templateImage.isNull()) {
            QPixmap pixmap = QPixmap::fromImage(pattern->templateImage);
            if (!pixmap.isNull()) {
                insTemplateImg->setPixmap(pixmap.scaled(
                    insTemplateImg->width(), insTemplateImg->height(), Qt::KeepAspectRatio));
                insTemplateImg->setText("");
            } else {
                insTemplateImg->setText(TR("IMAGE_CONVERSION_FAILED"));
            }
        } else {
            insTemplateImg->setPixmap(QPixmap());
            insTemplateImg->setText(TR("NO_IMAGE"));
        }
    }
}

void TeachingWidget::updateFidTemplateImage(PatternInfo* pattern, const QRectF& newRect) {
    if (!pattern || pattern->type != PatternType::FID) {
        return;
    }

    // **검사 모드일 때는 템플릿 이미지 갱신 금지**
    if (cameraView && cameraView->getInspectionMode()) {
        return;
    }

    cv::Mat sourceFrame;
    
    // 시뮬레이션 모드인지 확인
    if (simulationMode && !currentSimulationImage.empty()) {
        // 시뮬레이션 모드: 현재 시뮬레이션 이미지 사용
        sourceFrame = currentSimulationImage.clone();
    } else {
        // 일반 카메라 모드: 카메라 프레임 사용
        if (cameraIndex < 0 || cameraIndex >= static_cast<int>(cameraFrames.size()) || 
            cameraFrames[cameraIndex].empty()) {
            return;
        }
        sourceFrame = cameraFrames[cameraIndex].clone();
    }

    cv::Mat roiMat;
    
    // FID 템플릿 이미지: 회전각에 따라 유동적으로 사각형 크기 계산
    cv::Point2f center(newRect.x() + newRect.width()/2.0f, newRect.y() + newRect.height()/2.0f);
    
    // 회전각에 따른 최소 필요 사각형 크기 계산
    double angleRad = std::abs(pattern->angle) * M_PI / 180.0;
    double width = newRect.width();
    double height = newRect.height();
    
    // 회전된 사각형의 경계 상자 크기 계산
    double rotatedWidth = std::abs(width * std::cos(angleRad)) + std::abs(height * std::sin(angleRad));
    double rotatedHeight = std::abs(width * std::sin(angleRad)) + std::abs(height * std::cos(angleRad));
    
    // 정사각형 크기는 회전된 경계 상자 중 더 큰 값 + 여유분
    int maxSize = static_cast<int>(std::max(rotatedWidth, rotatedHeight)) + 10;
    
    // 정사각형 ROI 영역 계산 (중심점 기준)
    int halfSize = maxSize / 2;
    cv::Rect squareRoi(
        static_cast<int>(center.x) - halfSize,
        static_cast<int>(center.y) - halfSize,
        maxSize,
        maxSize
    );
    
    // 이미지 경계와 교집합 구하기
    cv::Rect imageBounds(0, 0, sourceFrame.cols, sourceFrame.rows);
    cv::Rect validRoi = squareRoi & imageBounds;
    
    if (validRoi.width > 0 && validRoi.height > 0) {
        // 정사각형 결과 이미지 생성 (검은색 배경)
        roiMat = cv::Mat::zeros(maxSize, maxSize, sourceFrame.type());
        
        // 유효한 영역만 복사
        int offsetX = validRoi.x - squareRoi.x;
        int offsetY = validRoi.y - squareRoi.y;
        
        cv::Mat validImage = sourceFrame(validRoi);
        cv::Rect resultRect(offsetX, offsetY, validRoi.width, validRoi.height);
        validImage.copyTo(roiMat(resultRect));
        
        // 패턴 영역 외부 마스킹 (패턴 영역만 보이도록)
        cv::Mat mask = cv::Mat::zeros(maxSize, maxSize, CV_8UC1);
        
        // 정사각형 중심을 기준으로 패턴 영역 계산
        cv::Point2f patternCenter(maxSize / 2.0f, maxSize / 2.0f);
        cv::Size2f patternSize(newRect.width(), newRect.height());
        
        if (std::abs(pattern->angle) > 0.1) {
            // 회전된 패턴의 경우: 회전된 사각형 마스크
            cv::Point2f vertices[4];
            cv::RotatedRect rotatedRect(patternCenter, patternSize, pattern->angle);
            rotatedRect.points(vertices);
            
            std::vector<cv::Point> points;
            for (int i = 0; i < 4; i++) {
                points.push_back(cv::Point(static_cast<int>(vertices[i].x), 
                                         static_cast<int>(vertices[i].y)));
            }
            
            cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{points}, cv::Scalar(255));
        } else {
            // 회전 없는 경우: 일반 사각형 마스크
            cv::Rect patternRect(
                static_cast<int>(patternCenter.x - patternSize.width / 2),
                static_cast<int>(patternCenter.y - patternSize.height / 2),
                static_cast<int>(patternSize.width),
                static_cast<int>(patternSize.height)
            );
            cv::rectangle(mask, patternRect, cv::Scalar(255), -1);
        }
        
        // 마스크 반전: 패턴 영역 외부를 흰색으로 설정
        cv::Mat invertedMask;
        cv::bitwise_not(mask, invertedMask);
        
        // 패턴 영역 외부를 흰색으로 마스킹
        roiMat.setTo(cv::Scalar(255, 255, 255), invertedMask);
    }
    
    if (roiMat.empty()) {
        return;
    }

    // 활성화된 모든 필터(마스크 포함) 순차 적용
    for (const FilterInfo& filter : pattern->filters) {
        if (filter.enabled) {
            cv::Mat filtered;
            ImageProcessor processor;
            processor.applyFilter(roiMat, filtered, filter);
            if (!filtered.empty()) {
                roiMat = filtered.clone();
            }
        }
    }

    // BGR -> RGB 변환
    cv::cvtColor(roiMat, roiMat, cv::COLOR_BGR2RGB);

    // QImage로 변환
    QImage qimg(roiMat.data, roiMat.cols, roiMat.rows, roiMat.step, QImage::Format_RGB888);

    // 패턴의 템플릿 이미지 업데이트
    pattern->templateImage = qimg.copy();

    // UI 업데이트
    if (fidTemplateImg) {
        fidTemplateImg->setPixmap(QPixmap::fromImage(pattern->templateImage.scaled(
            fidTemplateImg->width(), fidTemplateImg->height(), Qt::KeepAspectRatio)));
    }
}

cv::Mat TeachingWidget::extractRotatedRegion(const cv::Mat& image, const QRectF& rect, double angle) {
    if (image.empty() || rect.width() <= 0 || rect.height() <= 0) {
        return cv::Mat();
    }
    
    // 회전된 사각형의 4개 꼭짓점 계산
    double centerX = rect.x() + rect.width() / 2.0;
    double centerY = rect.y() + rect.height() / 2.0;
    double halfWidth = rect.width() / 2.0;
    double halfHeight = rect.height() / 2.0;
    
    // 회전 각도를 라디안으로 변환
    double radians = angle * M_PI / 180.0;
    double cosA = std::cos(radians);
    double sinA = std::sin(radians);
    
    // 회전되지 않은 꼭짓점들
    std::vector<cv::Point2f> corners = {
        cv::Point2f(centerX - halfWidth, centerY - halfHeight), // top-left
        cv::Point2f(centerX + halfWidth, centerY - halfHeight), // top-right
        cv::Point2f(centerX + halfWidth, centerY + halfHeight), // bottom-right
        cv::Point2f(centerX - halfWidth, centerY + halfHeight)  // bottom-left
    };
    
    // 회전 적용
    std::vector<cv::Point2f> rotatedCorners(4);
    for (int i = 0; i < 4; i++) {
        double dx = corners[i].x - centerX;
        double dy = corners[i].y - centerY;
        
        rotatedCorners[i].x = centerX + dx * cosA - dy * sinA;
        rotatedCorners[i].y = centerY + dx * sinA + dy * cosA;
    }
    
    // 회전된 꼭짓점들의 바운딩 박스 계산
    float minX = rotatedCorners[0].x, maxX = rotatedCorners[0].x;
    float minY = rotatedCorners[0].y, maxY = rotatedCorners[0].y;
    
    for (int i = 1; i < 4; i++) {
        minX = std::min(minX, rotatedCorners[i].x);
        maxX = std::max(maxX, rotatedCorners[i].x);
        minY = std::min(minY, rotatedCorners[i].y);
        maxY = std::max(maxY, rotatedCorners[i].y);
    }
    
    // 바운딩 박스가 이미지 범위를 벗어나지 않도록 클리핑
    int boundingX = std::max(0, static_cast<int>(std::floor(minX)));
    int boundingY = std::max(0, static_cast<int>(std::floor(minY)));
    int boundingWidth = std::min(image.cols - boundingX, static_cast<int>(std::ceil(maxX)) - boundingX);
    int boundingHeight = std::min(image.rows - boundingY, static_cast<int>(std::ceil(maxY)) - boundingY);
    
    if (boundingWidth <= 0 || boundingHeight <= 0) {
        return cv::Mat(static_cast<int>(rect.height()), static_cast<int>(rect.width()), 
                       image.type(), cv::Scalar(255, 255, 255));
    }
    
    // 바운딩 박스 크기의 결과 이미지 생성 (흰색으로 초기화)
    cv::Mat result(boundingHeight, boundingWidth, image.type(), cv::Scalar(255, 255, 255));
    
    // 회전된 사각형 영역의 마스크 생성
    cv::Mat mask = cv::Mat::zeros(boundingHeight, boundingWidth, CV_8UC1);
    
    // 바운딩 박스 좌표계로 변환된 회전된 꼭짓점들
    std::vector<cv::Point> maskCorners(4);
    for (int i = 0; i < 4; i++) {
        maskCorners[i].x = static_cast<int>(rotatedCorners[i].x - boundingX);
        maskCorners[i].y = static_cast<int>(rotatedCorners[i].y - boundingY);
    }
    
    // 마스크에 회전된 사각형 그리기
    cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{maskCorners}, cv::Scalar(255));
    
    // 바운딩 박스 영역의 원본 이미지 복사
    cv::Rect boundingRect(boundingX, boundingY, boundingWidth, boundingHeight);
    cv::Mat boundingRegion = image(boundingRect);
    
    // 마스크를 사용해서 회전된 영역만 복사
    for (int y = 0; y < boundingHeight; y++) {
        for (int x = 0; x < boundingWidth; x++) {
            if (mask.at<uchar>(y, x) > 0) {
                if (image.channels() == 3) {
                    result.at<cv::Vec3b>(y, x) = boundingRegion.at<cv::Vec3b>(y, x);
                } else {
                    result.at<uchar>(y, x) = boundingRegion.at<uchar>(y, x);
                }
            }
        }
    }
    
    return result;
}


void TeachingWidget::updatePatternFilters(int patternIndex) {
    updatePatternTree(); // 간단히 트리 전체 업데이트로 대체
}

// 프로퍼티 패널의 이벤트 연결을 처리하는 함수
void TeachingWidget::connectPropertyPanelEvents() {
    // 이름 변경 이벤트
    if (patternNameEdit) {
        connect(patternNameEdit, &QLineEdit::textChanged, [this](const QString &text) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = QUuid(selectedItem->data(0, Qt::UserRole).toString());
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId); 
                    if (pattern) {
                        pattern->name = text;
                        selectedItem->setText(0, text);
                        cameraView->update();
                    }
                }
            }
        });
    }
    
    // ROI 전체 카메라 영역 포함 체크박스
    if (includeAllCameraCheck) {
        connect(includeAllCameraCheck, &QCheckBox::toggled, [this](bool checked) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = QUuid(selectedItem->data(0, Qt::UserRole).toString());
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::ROI) {
                        pattern->includeAllCamera = checked;
                        cameraView->update();
                    }
                }
            }
        });
    }
    
    // FID 패턴 매칭 방법 콤보박스
    if (fidMatchMethodCombo) {
        connect(fidMatchMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
                [this](int index) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::FID) {
                        pattern->fidMatchMethod = index;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // FID 매칭 검사 활성화 체크박스
    if (fidMatchCheckBox) {
        connect(fidMatchCheckBox, &QCheckBox::toggled, [this](bool checked) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::FID) {
                        pattern->runInspection = checked;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // FID 패턴 임계값 이벤트
    if (fidMatchThreshSpin) {
        connect(fidMatchThreshSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
                [this](double value) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::FID) {
                        pattern->matchThreshold = value;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // FID 회전 사용 체크박스
    if (fidRotationCheck) {
        connect(fidRotationCheck, &QCheckBox::toggled, [this](bool checked) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::FID) {
                        pattern->useRotation = checked;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // FID 최소 각도 설정
    if (fidMinAngleSpin) {
        connect(fidMinAngleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
                [this](double value) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::FID) {
                        pattern->minAngle = value;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // FID 최대 각도 설정
    if (fidMaxAngleSpin) {
        connect(fidMaxAngleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
                [this](double value) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::FID) {
                        pattern->maxAngle = value;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // FID 각도 스텝 설정
    if (fidStepSpin) {
        connect(fidStepSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
                [this](double value) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::FID) {
                        pattern->angleStep = value;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // INS 합격 임계값 설정
    if (insPassThreshSpin) {
        connect(insPassThreshSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
                [this](double value) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->passThreshold = value;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // INS 검사 방법 콤보박스
    if (insMethodCombo) {
        connect(insMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
                [this](int index) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->inspectionMethod = index;
                        
                        // 이진화 검사 패널 표시 설정
                        if (insBinaryPanel) {
                            insBinaryPanel->setVisible(index == InspectionMethod::BINARY);
                        }
                        
                        // AI 기반 검사에서는 결과 반전 옵션 필요 없음
                        if (insInvertCheck) {
                            bool visible = (index != InspectionMethod::AI_MATCH1);
                            insInvertCheck->setVisible(visible);
                            if (!visible) insInvertCheck->setChecked(false);
                        }

                        // 패턴 매칭 패널 표시 설정
                        if (insPatternMatchPanel) {
                            insPatternMatchPanel->setVisible(index == InspectionMethod::COLOR && pattern->runInspection);
                        }
                        
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }    
    
    // INS 결과 반전 체크박스
    if (insInvertCheck) {
        connect(insInvertCheck, &QCheckBox::toggled, [this](bool checked) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->invertResult = checked;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // INS 회전 체크박스
    if (insRotationCheck) {
        connect(insRotationCheck, &QCheckBox::toggled, [this](bool checked) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->useRotation = checked;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }

    // INS 최소 회전 각도
    if (insMinAngleSpin) {
        connect(insMinAngleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double value) {
                QTreeWidgetItem* selectedItem = patternTree->currentItem();
                if (selectedItem) {
                    QUuid patternId = getPatternIdFromItem(selectedItem);
                    if (!patternId.isNull()) {
                        PatternInfo* pattern = cameraView->getPatternById(patternId);
                        if (pattern && pattern->type == PatternType::INS) {
                            pattern->minAngle = value;
                            cameraView->updatePatternById(patternId, *pattern);
                        }
                    }
                }
            });
    }

    // INS 최대 회전 각도
    if (insMaxAngleSpin) {
        connect(insMaxAngleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double value) {
                QTreeWidgetItem* selectedItem = patternTree->currentItem();
                if (selectedItem) {
                    QUuid patternId = getPatternIdFromItem(selectedItem);
                    if (!patternId.isNull()) {
                        PatternInfo* pattern = cameraView->getPatternById(patternId);
                        if (pattern && pattern->type == PatternType::INS) {
                            pattern->maxAngle = value;
                            cameraView->updatePatternById(patternId, *pattern);
                        }
                    }
                }
            });
    }

    // INS 회전 간격
    if (insAngleStepSpin) {
        connect(insAngleStepSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double value) {
                QTreeWidgetItem* selectedItem = patternTree->currentItem();
                if (selectedItem) {
                    QUuid patternId = getPatternIdFromItem(selectedItem);
                    if (!patternId.isNull()) {
                        PatternInfo* pattern = cameraView->getPatternById(patternId);
                        if (pattern && pattern->type == PatternType::INS) {
                            pattern->angleStep = value;
                            cameraView->updatePatternById(patternId, *pattern);
                        }
                    }
                }
            });
    }
    
    // 위치 및 크기 변경 연결
    auto connectPatternSpinBox = [this](QSpinBox* spinBox, std::function<void(int)> updateFunc) {
        if (spinBox) {
            connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), [this, updateFunc](int value) {
                QTreeWidgetItem* selectedItem = patternTree->currentItem();
                if (selectedItem) {
                    QUuid patternId = getPatternIdFromItem(selectedItem);
                    if (!patternId.isNull()) {
                        PatternInfo* pattern = cameraView->getPatternById(patternId);
                        if (pattern) {
                            updateFunc(value);
                            cameraView->updatePatternRect(patternId, pattern->rect);
                        }
                    }
                }
            });
        }
    };
    
    connectPatternSpinBox(patternXSpin, [this](int value) {
        QTreeWidgetItem* selectedItem = patternTree->currentItem();
        if (selectedItem) {
            QUuid patternId = getPatternIdFromItem(selectedItem);
            if (!patternId.isNull()) {
                PatternInfo* pattern = cameraView->getPatternById(patternId);
                if (pattern) {
                    pattern->rect.setX(value);
                    cameraView->updatePatternById(patternId, *pattern);
                    cameraView->update();
                    
                    // 템플릿 이미지 업데이트 (시뮬레이션 모드 지원)
                    if (pattern->type == PatternType::FID) {
                        updateFidTemplateImage(pattern, pattern->rect);
                    } else if (pattern->type == PatternType::INS) {
                        updateInsTemplateImage(pattern, pattern->rect);
                    }
                }
            }
        }
    });
    
    connectPatternSpinBox(patternYSpin, [this](int value) {
        QTreeWidgetItem* selectedItem = patternTree->currentItem();
        if (selectedItem) {
            QUuid patternId = getPatternIdFromItem(selectedItem);
            if (!patternId.isNull()) {
                PatternInfo* pattern = cameraView->getPatternById(patternId);
                if (pattern) {
                    pattern->rect.setY(value);
                    cameraView->updatePatternById(patternId, *pattern);
                    cameraView->update();
                    
                    // 템플릿 이미지 업데이트 (시뮬레이션 모드 지원)
                    if (pattern->type == PatternType::FID) {
                        updateFidTemplateImage(pattern, pattern->rect);
                    } else if (pattern->type == PatternType::INS) {
                        updateInsTemplateImage(pattern, pattern->rect);
                    }
                }
            }
        }
    });
    
    connectPatternSpinBox(patternWSpin, [this](int value) {
        QTreeWidgetItem* selectedItem = patternTree->currentItem();
        if (selectedItem) {
            QUuid patternId = getPatternIdFromItem(selectedItem);
            if (!patternId.isNull()) {
                PatternInfo* pattern = cameraView->getPatternById(patternId);
                if (pattern) {
                    pattern->rect.setWidth(value);
                    cameraView->updatePatternById(patternId, *pattern);
                    cameraView->update();
                    
                    // 템플릿 이미지 업데이트 (시뮬레이션 모드 지원)
                    if (pattern->type == PatternType::FID) {
                        updateFidTemplateImage(pattern, pattern->rect);
                    } else if (pattern->type == PatternType::INS) {
                        updateInsTemplateImage(pattern, pattern->rect);
                    }
                }
            }
        }
    });
    
    connectPatternSpinBox(patternHSpin, [this](int value) {
        QTreeWidgetItem* selectedItem = patternTree->currentItem();
        if (selectedItem) {
            QUuid patternId = getPatternIdFromItem(selectedItem);
            if (!patternId.isNull()) {
                PatternInfo* pattern = cameraView->getPatternById(patternId);
                if (pattern) {
                    pattern->rect.setHeight(value);
                    cameraView->updatePatternById(patternId, *pattern);
                    cameraView->update();
                    
                    // 템플릿 이미지 업데이트 (시뮬레이션 모드 지원)
                    if (pattern->type == PatternType::FID) {
                        updateFidTemplateImage(pattern, pattern->rect);
                    } else if (pattern->type == PatternType::INS) {
                        updateInsTemplateImage(pattern, pattern->rect);
                    }
                }
            }
        }
    });
    
    // 이진화 검사 관련 연결
    // 이진화 임계값
    if (insBinaryThreshSpin) {
        connect(insBinaryThreshSpin, QOverload<int>::of(&QSpinBox::valueChanged), 
                [this](int value) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->binaryThreshold = value;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // 비교 방식
    if (insCompareCombo) {
        connect(insCompareCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
                [this](int index) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->compareMethod = index;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // 하한 임계값
    if (insLowerSpin) {
        connect(insLowerSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
                [this](double value) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->lowerThreshold = value;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // 상한 임계값

    if (insUpperSpin) {
        connect(insUpperSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
                [this](double value) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->upperThreshold = value;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // 비율 타입
    if (insRatioTypeCombo) {
        connect(insRatioTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
                [this](int index) {
                    QTreeWidgetItem* selectedItem = patternTree->currentItem();
                    if (selectedItem) {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        PatternInfo* pattern = cameraView->getPatternById(patternId);
                        if (pattern && pattern->type == PatternType::INS) {
                            pattern->ratioType = index;
                            
                            // 비율 타입 변경 후 템플릿 이미지 업데이트
                            
                            // 템플릿 이미지 업데이트 (이진화 타입이 반영되도록)
                            updateInsTemplateImage(pattern, pattern->rect);
                            
                            cameraView->update();
                        }
                    }
                });
    }
    
    // === STRIP 검사 파라미터 이벤트 연결 ===
    
    // 컨투어 마진
    // 형태학적 커널 크기
    if (insStripKernelSpin) {
        connect(insStripKernelSpin, QOverload<int>::of(&QSpinBox::valueChanged), 
                [this](int value) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        // 홀수로 강제 조정
                        if (value % 2 == 0) value++;
                        pattern->stripMorphKernelSize = value;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // Gradient 임계값
    if (insStripGradThreshSpin) {
        connect(insStripGradThreshSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
                [this](double value) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->stripGradientThreshold = static_cast<float>(value);
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // Gradient 시작 지점
    if (insStripStartSpin) {
        connect(insStripStartSpin, QOverload<int>::of(&QSpinBox::valueChanged), 
                [this](int value) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->stripGradientStartPercent = value;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // Gradient 끝 지점
    if (insStripEndSpin) {
        connect(insStripEndSpin, QOverload<int>::of(&QSpinBox::valueChanged), 
                [this](int value) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->stripGradientEndPercent = value;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // 최소 데이터 포인트
    if (insStripMinPointsSpin) {
        connect(insStripMinPointsSpin, QOverload<int>::of(&QSpinBox::valueChanged), 
                [this](int value) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->stripMinDataPoints = value;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }
    
    // 패턴 각도 텍스트박스
    if (angleEdit) {
        connect(angleEdit, &QLineEdit::textChanged, [this](const QString &text) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern) {
                        bool ok;
                        double angle = text.toDouble(&ok);
                        if (ok) {
                            // 각도를 -180° ~ +180° 범위로 정규화
                            angle = normalizeAngle(angle);
                            pattern->angle = angle;
                            cameraView->updatePatternById(patternId, *pattern);
                            cameraView->update();
                            
                            // 입력 필드도 정규화된 각도로 업데이트
                            angleEdit->blockSignals(true);
                            angleEdit->setText(QString::number(angle, 'f', 2));
                            angleEdit->blockSignals(false);
                            
                            // 템플릿 이미지도 업데이트
                            if (pattern->type == PatternType::FID) {
                                updateFidTemplateImage(pattern, pattern->rect);
                            } else if (pattern->type == PatternType::INS) {
                                updateInsTemplateImage(pattern, pattern->rect);
                            }
                        }
                    }
                }
            }
        });
    }
}

void TeachingWidget::updatePropertyPanel(PatternInfo* pattern, const FilterInfo* filter, const QUuid& patternId, int filterIndex) {
    // 필터가 제공된 경우 필터 속성 패널 표시
    if (filter) {
        propertyStackWidget->setCurrentIndex(2);
        
        if (!filterPropertyContainer) {
            return;
        }
        
        // 기존 필터 위젯 모두 제거
        QLayout* containerLayout = filterPropertyContainer->layout();
        if (containerLayout) {
            QLayoutItem* item;
            while ((item = containerLayout->takeAt(0)) != nullptr) {
                if (item->widget()) {
                    item->widget()->deleteLater();
                }
                delete item;
            }
        }
           
        // 필터 타입에 맞는 FilterPropertyWidget 생성
        FilterPropertyWidget* filterPropWidget = new FilterPropertyWidget(filter->type, filterPropertyContainer);
        
        // 필터 정보로 속성 설정
        filterPropWidget->setParams(filter->params);
        filterPropWidget->setEnabled(filter->enabled);
        
        // 레이아웃에 추가
        containerLayout->addWidget(filterPropWidget);
        
        // 공간 추가
        QWidget* spacer = new QWidget(filterPropertyContainer);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        containerLayout->addWidget(spacer);
        
        // 파라미터 변경 이벤트 연결
        connect(filterPropWidget, &FilterPropertyWidget::paramChanged, 
                [this, patternId, filterIndex](const QString& paramName, int value) {
            updateFilterParam(patternId, filterIndex, paramName, value);

        });
        
        // 필터 활성화 상태 변경 이벤트 연결
        connect(filterPropWidget, &FilterPropertyWidget::enableStateChanged,
                [this, patternId, filterIndex](bool enabled) {
            // 필터 활성화 상태 변경
            cameraView->setPatternFilterEnabled(patternId, filterIndex, enabled);
            
            // 체크박스 상태 업데이트 (트리 아이템)
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                selectedItem->setCheckState(0, enabled ? Qt::Checked : Qt::Unchecked);
            }
        });
        
        return; // 필터 프로퍼티 패널을 표시했으므로 여기서 함수 종료
    }
    
    // 패턴 없으면 빈 패널 표시
    if (!pattern) {
        propertyStackWidget->setCurrentIndex(0);
        return;
    }
    
    // 패턴 타입에 따른 프로퍼티 패널 (기존 코드와 동일)
    if (propertyStackWidget) {
        // 패턴 패널로 전환
        propertyStackWidget->setCurrentIndex(1);
        
        // 기본 정보 설정
        if (patternIdValue) {
            // ID 필드에는 패턴 ID 표시 (UUID)
            patternIdValue->setText(pattern->id.toString());
        }
        
        if (patternNameEdit) {
            // 이름 필드에는 패턴 이름 표시
            patternNameEdit->setText(pattern->name);
        }
        
        if (patternTypeValue) {
            QString typeText;
            QColor typeColor;
            
            switch (pattern->type) {
                case PatternType::ROI:
                    typeText = "ROI";
                    typeColor = UIColors::ROI_COLOR;
                    break;
                case PatternType::FID:
                    typeText = "FID";
                    typeColor = UIColors::FIDUCIAL_COLOR;
                    break;
                case PatternType::INS:
                    typeText = "INS";
                    typeColor = UIColors::INSPECTION_COLOR;
                    break;
                case PatternType::FIL:
                    typeText = "FIL";
                    typeColor = UIColors::FILTER_COLOR;
                    break;
                default:
                    typeText = "UNKNOWN";
                    typeColor = Qt::gray;
                    break;
            }
            
            patternTypeValue->setText(typeText);
            patternTypeValue->setStyleSheet(QString("background-color: %1; color: %2; border-radius: 3px; padding: 2px 5px;")
                                   .arg(typeColor.name())
                                   .arg(UIColors::getTextColor(typeColor).name()));
        }
        
        // 위치 정보 업데이트
        if (patternXSpin) {
            patternXSpin->blockSignals(true);
            patternXSpin->setValue(pattern->rect.x());
            patternXSpin->blockSignals(false);
        }
        
        if (patternYSpin) {
            patternYSpin->blockSignals(true);
            patternYSpin->setValue(pattern->rect.y());
            patternYSpin->blockSignals(false);
        }
        
        if (patternWSpin) {
            patternWSpin->blockSignals(true);
            patternWSpin->setValue(pattern->rect.width());
            patternWSpin->blockSignals(false);
        }
        
        if (patternHSpin) {
            patternHSpin->blockSignals(true);
            patternHSpin->setValue(pattern->rect.height());
            patternHSpin->blockSignals(false);
        }
        
        // 각도 정보 업데이트
        if (angleEdit) {
            angleEdit->blockSignals(true);
            angleEdit->setText(QString::number(pattern->angle, 'f', 1));
            angleEdit->blockSignals(false);
        }
        
        // 패턴 타입별 특수 속성 설정
        if (specialPropStack) {
            switch (pattern->type) {
                case PatternType::ROI: {
                    specialPropStack->setCurrentIndex(0);
                    if (includeAllCameraCheck) {
                        includeAllCameraCheck->setChecked(pattern->includeAllCamera);
                    }
                    break;
                }
                case PatternType::FID: {
                    specialPropStack->setCurrentIndex(1);
                    
                    // FID 속성 업데이트
                    if (fidMatchMethodCombo) {
                        fidMatchMethodCombo->setCurrentIndex(pattern->fidMatchMethod);
                    }
                    
                    if (fidMatchCheckBox) {
                        fidMatchCheckBox->setChecked(pattern->runInspection);
                    }
                    if (fidMatchThreshSpin) {
                        fidMatchThreshSpin->setValue(pattern->matchThreshold);
                    }
          
                    if (fidRotationCheck) {
                        fidRotationCheck->setChecked(pattern->useRotation);
                    }
                    
                    if (fidMinAngleSpin) {
                        fidMinAngleSpin->setValue(pattern->minAngle);
                    }
                    
                    if (fidMaxAngleSpin) {
                        fidMaxAngleSpin->setValue(pattern->maxAngle);
                    }
                    
                    if (fidStepSpin) {
                        fidStepSpin->setValue(pattern->angleStep);
                    }
                    
                    // 템플릿 이미지 업데이트
                    if (fidTemplateImg) {
                        if (!pattern->templateImage.isNull()) {
                            fidTemplateImg->setPixmap(QPixmap::fromImage(pattern->templateImage.scaled(
                                fidTemplateImg->width(), fidTemplateImg->height(), Qt::KeepAspectRatio)));
                            fidTemplateImg->setText(""); // 이미지가 있을 때는 텍스트 삭제
                        } else {
                            fidTemplateImg->setPixmap(QPixmap()); // 빈 픽스맵으로 설정
                            fidTemplateImg->setText(TR("NO_IMAGE"));
                        }
                    }
                    break;
                }
                case PatternType::INS: {
                    specialPropStack->setCurrentIndex(2);
                    
                    // 검사 방법 콤보박스 설정
                    if (insMethodCombo) {
                        insMethodCombo->blockSignals(true);
                        insMethodCombo->setCurrentIndex(pattern->inspectionMethod);
                        insMethodCombo->blockSignals(false);
                    } 
                    if (insRotationCheck) {
                        insRotationCheck->setChecked(pattern->useRotation);
                    }
                    
                    if (insMinAngleSpin) {
                        insMinAngleSpin->setValue(pattern->minAngle);
                    }
                    
                    if (insMaxAngleSpin) {
                        insMaxAngleSpin->setValue(pattern->maxAngle);
                    }
                    
                    if (insAngleStepSpin) {
                        insAngleStepSpin->setValue(pattern->angleStep);
                    }
                    
                    if (insPassThreshSpin) {
                        insPassThreshSpin->setValue(pattern->passThreshold);
                    }
                    
                    if (insInvertCheck) {
                        bool visible = (pattern->inspectionMethod != InspectionMethod::AI_MATCH1);
                        insInvertCheck->setVisible(visible);
                        insInvertCheck->setChecked(visible ? pattern->invertResult : false);
                    }
                    
                    // 이진화 패널 표시 설정
                    if (insBinaryPanel) {
                        insBinaryPanel->setVisible(pattern->inspectionMethod == InspectionMethod::BINARY);
                    }
                    
                    // STRIP 패널 표시 설정
                    if (insStripPanel) {
                        insStripPanel->setVisible(pattern->inspectionMethod == InspectionMethod::STRIP);
                    }
                    
                    // STRIP 파라미터 로드
                    if (insStripKernelSpin) {
                        insStripKernelSpin->blockSignals(true);
                        insStripKernelSpin->setValue(pattern->stripMorphKernelSize);
                        insStripKernelSpin->blockSignals(false);
                    }
                    
                    if (insStripGradThreshSpin) {
                        insStripGradThreshSpin->blockSignals(true);
                        insStripGradThreshSpin->setValue(pattern->stripGradientThreshold);
                        insStripGradThreshSpin->blockSignals(false);
                    }
                    
                    if (insStripStartSpin) {
                        insStripStartSpin->blockSignals(true);
                        insStripStartSpin->setValue(pattern->stripGradientStartPercent);
                        insStripStartSpin->blockSignals(false);
                    }
                    
                    if (insStripEndSpin) {
                        insStripEndSpin->blockSignals(true);
                        insStripEndSpin->setValue(pattern->stripGradientEndPercent);
                        insStripEndSpin->blockSignals(false);
                    }
                    
                    if (insStripMinPointsSpin) {
                        insStripMinPointsSpin->blockSignals(true);
                        insStripMinPointsSpin->setValue(pattern->stripMinDataPoints);
                        insStripMinPointsSpin->blockSignals(false);
                    }
                    
                    if (insBinaryThreshSpin) {
                        insBinaryThreshSpin->setValue(pattern->binaryThreshold);
                    }
                    
                    if (insCompareCombo) {
                        insCompareCombo->setCurrentIndex(pattern->compareMethod);
                    }
                    
                    if (insLowerSpin) {
                        insLowerSpin->setValue(pattern->lowerThreshold);
                    }
                    
                    if (insUpperSpin) {
                        insUpperSpin->setValue(pattern->upperThreshold);
                    }
                    
                    if (insRatioTypeCombo) {
                        insRatioTypeCombo->setCurrentIndex(pattern->ratioType);
                    }
                    
                    // INS 패턴의 템플릿 이미지 업데이트
                    if (insTemplateImg) {
                        if (!pattern->templateImage.isNull()) {
                            QPixmap pixmap = QPixmap::fromImage(pattern->templateImage);
                            if (!pixmap.isNull()) {
                                insTemplateImg->setPixmap(pixmap.scaled(
                                    insTemplateImg->width(), insTemplateImg->height(), Qt::KeepAspectRatio));
                                insTemplateImg->setText("");
                            } else {
                                insTemplateImg->setPixmap(QPixmap());
                                insTemplateImg->setText(TR("IMAGE_CONVERSION_FAILED"));
                            }
                        } else {
                            insTemplateImg->setPixmap(QPixmap());
                            insTemplateImg->setText(TR("NO_IMAGE"));
                        }
                    }
                    break;
                }
                case PatternType::FIL: {
                    // 필터 타입은 특별한 패널이 없음, 기본 패널 표시
                    specialPropStack->setCurrentIndex(0);
                    break;
                }
                default: {
                    // 알 수 없는 패턴 타입
                    specialPropStack->setCurrentIndex(0); // 기본 패널 표시
                    break;
                }
            }
        }
    }
}

void TeachingWidget::detectCameras() {
    // **프로그레스 다이얼로그 생성**
    QProgressDialog* progressDialog = new QProgressDialog("카메라 검색 중...", "취소", 0, 100, this);
    progressDialog->setWindowTitle("카메라 검색");
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setMinimumDuration(0);  // 즉시 표시
    progressDialog->setValue(0);
    progressDialog->show();
    QApplication::processEvents();  // UI 즉시 업데이트
    
    // 실제 연결된 카메라 수 카운트
    int connectedCameras = 0;
    
    // 카메라 정보 초기화
    progressDialog->setLabelText("기존 카메라 정보 정리 중...");
    progressDialog->setValue(5);
    QApplication::processEvents();
    
    int cameraCount = getCameraInfosCount();
    for (int i = 0; i < cameraCount; i++) {
        CameraInfo info = getCameraInfo(i);
        if (info.capture) {
            // 수정된 info를 다시 설정해야 함
            info.capture->release();
            delete info.capture;
            info.capture = nullptr;
            setCameraInfo(i, info);
        }
    }
    clearCameraInfos();  // 마지막에 전체 클리어

#ifdef USE_SPINNAKER
    // Spinnaker SDK 사용 가능한 경우
    if (m_useSpinnaker) {
        progressDialog->setLabelText("Spinnaker 카메라 검색 중...");
        progressDialog->setValue(10);
        QApplication::processEvents();
        
        try {
            // 기존 카메라 목록 초기화
            if (m_spinCamList.GetSize() > 0) {
                m_spinCamList.Clear();
            }
            m_spinCameras.clear();
            
            progressDialog->setValue(15);
            QApplication::processEvents();
            
            // 사용 가능한 카메라 목록 가져오기
            m_spinCamList = m_spinSystem->GetCameras();
            unsigned int numCameras = m_spinCamList.GetSize();
            
            progressDialog->setLabelText(QString("Spinnaker 카메라 %1개 발견, 연결 중...").arg(numCameras));
            progressDialog->setValue(20);
            QApplication::processEvents();
            
            if (numCameras > 0) {
                
                // 각 카메라에 대해 처리
                for (unsigned int i = 0; i < numCameras; i++) {
                    if (progressDialog->wasCanceled()) {
                        progressDialog->deleteLater();
                        return;
                    }
                    
                    progressDialog->setLabelText(QString("Spinnaker 카메라 %1/%2 연결 중...").arg(i+1).arg(numCameras));
                    int progressValue = 20 + (i * 30 / numCameras);  // 20-50%
                    progressDialog->setValue(progressValue);
                    QApplication::processEvents();
                    
                    CameraInfo info;
                    info.index = i;
                    
                    if (connectSpinnakerCamera(i, info)) {
                        // 성공적으로 연결된 카메라 추가
                        appendCameraInfo(info);
                        connectedCameras++;
                    }
                }
                
                // Spinnaker 카메라를 연결했으면 OpenCV 카메라 검색 건너뛰기
                if (connectedCameras > 0) {
                    progressDialog->setLabelText("미리보기 레이블 초기화 중...");
                    progressDialog->setValue(95);
                    QApplication::processEvents();
                    
                    // 미리보기 레이블에 카메라 인덱스 매핑 초기화
                    for (int i = 0; i < cameraPreviewLabels.size(); i++) {
                        if (cameraPreviewLabels[i]) {
                            cameraPreviewLabels[i]->setProperty("uniqueCameraId", "");
                        }
                    }
                    
                    progressDialog->setValue(100);
                    progressDialog->deleteLater();
                    return;
                }
            }
        }
        catch (Spinnaker::Exception& e) {
            // Spinnaker 오류 무시하고 OpenCV로 계속
        }
    }
#endif
    
#ifdef __linux__
    // **개선된 Linux 카메라 검색**
    progressDialog->setLabelText("Linux 카메라 장치 검색 중...");
    progressDialog->setValue(50);
    QApplication::processEvents();
    
    setenv("GST_DEBUG", "1", 1);
    setenv("OPENCV_VIDEOIO_PRIORITY_GSTREAMER", "0", 1);
    
    QList<int> realCameraIndices;
    
    // /dev/video* 장치 스캔
    int totalDevices = 20;
    for (int deviceIndex = 0; deviceIndex < totalDevices; deviceIndex += 2) {
        if (progressDialog->wasCanceled()) {
            progressDialog->deleteLater();
            return;
        }
        
        progressDialog->setLabelText(QString("장치 /dev/video%1 확인 중...").arg(deviceIndex));
        int progressValue = 50 + (deviceIndex * 20 / totalDevices);  // 50-70%
        progressDialog->setValue(progressValue);
        QApplication::processEvents();
        
        QString devicePath = QString("/dev/video%1").arg(deviceIndex);
        
        if (!QFile::exists(devicePath)) {
            continue;
        }
        
        cv::VideoCapture testCapture(deviceIndex, cv::CAP_V4L2);
        if (testCapture.isOpened()) {
            cv::Mat testFrame;
            bool canRead = testCapture.read(testFrame);
            testCapture.release();
            
            if (canRead && !testFrame.empty() && testFrame.cols > 0 && testFrame.rows > 0) {
                realCameraIndices.append(deviceIndex);
            }
        }
    }
    
    
    progressDialog->setLabelText(QString("실제 카메라 %1개 발견, 연결 중...").arg(realCameraIndices.size()));
    progressDialog->setValue(70);
    QApplication::processEvents();
    
    // **각 실제 카메라에 대해 순차적 인덱스 할당**
    for (int i = 0; i < realCameraIndices.size(); i++) {
        if (progressDialog->wasCanceled()) {
            progressDialog->deleteLater();
            return;
        }
        
        int deviceIndex = realCameraIndices[i];
        
        progressDialog->setLabelText(QString("카메라 %1/%2 연결 중... (/dev/video%3)").arg(i+1).arg(realCameraIndices.size()).arg(deviceIndex));
        int progressValue = 70 + (i * 20 / realCameraIndices.size());  // 70-90%
        progressDialog->setValue(progressValue);
        QApplication::processEvents();
        
        cv::VideoCapture* capture = new cv::VideoCapture(deviceIndex, cv::CAP_V4L2);
        if (capture->isOpened()) {
            // 기본 설정
            capture->set(cv::CAP_PROP_FPS, FRAME_RATE);
            capture->set(cv::CAP_PROP_FRAME_WIDTH, FRAME_WIDTH);
            capture->set(cv::CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT);
            capture->set(cv::CAP_PROP_BUFFERSIZE, 1);
            
            CameraInfo info;
            info.index = i;  // **순차적 인덱스 (0, 1, 2, ...)**
            info.videoDeviceIndex = deviceIndex;  // **실제 장치 번호 (0, 2, 4, ...)**
            info.capture = capture;
            info.isConnected = true;
            info.name = QString("카메라 %1 (장치 %2)").arg(i + 1).arg(deviceIndex);
            
            // 고유 ID 생성
            updateCameraDetailInfo(info);
            
            appendCameraInfo(info);
            connectedCameras++;
            
                        .arg(i + 1).arg(deviceIndex).arg(i);
        } else {
            delete capture;
        }
    }
    
#else
    // Windows/macOS용 카메라 검색 코드
    progressDialog->setLabelText("USB 카메라 검색 중...");
    progressDialog->setValue(50);
    QApplication::processEvents();
    
    int totalCameras = 8;
    for (int i = 0; i < totalCameras; i++) {
        if (progressDialog->wasCanceled()) {
            progressDialog->deleteLater();
            return;
        }
        
        progressDialog->setLabelText(QString("카메라 %1/%2 확인 중...").arg(i+1).arg(totalCameras));
        int progressValue = 50 + (i * 40 / totalCameras);  // 50-90%
        progressDialog->setValue(progressValue);
        QApplication::processEvents();
        
        cv::VideoCapture* capture = new cv::VideoCapture(i);
        if (capture->isOpened()) {
            // 기본 설정 적용
            capture->set(cv::CAP_PROP_FPS, FRAME_RATE);
            capture->set(cv::CAP_PROP_FRAME_WIDTH, FRAME_WIDTH);
            capture->set(cv::CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT);
            capture->set(cv::CAP_PROP_BUFFERSIZE, 1);
            
            CameraInfo info;
            info.index = i;
            info.videoDeviceIndex = i;
            info.capture = capture;
            info.isConnected = true;
            info.name = QString("카메라 %1").arg(i + 1);
            
            // 카메라 연결 시 시뮬레이션 모드 해제
            if (simulationMode) {
                simulationMode = false;
                currentSimulationImage = cv::Mat(); // 시뮬레이션 이미지 초기화
                updateUIForSimulationMode(false);  // UI 업데이트
                
                // 카메라뷰의 시뮬레이션 상태도 초기화
                if (cameraView) {
                    cameraView->setSimulationCameraName("");
                }
            }
            
            // 상세 정보 업데이트
            updateCameraDetailInfo(info);
            
            appendCameraInfo(info);
            connectedCameras++;
        } else {
            delete capture;
        }
    }
#endif
    
    // 미리보기 레이블에 카메라 인덱스 매핑 초기화
    progressDialog->setLabelText("미리보기 레이블 초기화 중...");
    progressDialog->setValue(95);
    QApplication::processEvents();
    
    for (int i = 0; i < cameraPreviewLabels.size(); i++) {
        if (cameraPreviewLabels[i]) {
            cameraPreviewLabels[i]->setProperty("uniqueCameraId", "");
        }
    }
    
    // 완료
    progressDialog->setLabelText(QString("카메라 검색 완료! %1개 카메라 발견").arg(connectedCameras));
    progressDialog->setValue(100);
    QApplication::processEvents();
    
    // 잠시 대기 후 다이얼로그 닫기
    QTimer::singleShot(500, progressDialog, &QProgressDialog::deleteLater);
    
}

void TeachingWidget::processGrabbedFrame(const cv::Mat& frame, int camIdx) {
    // 프레임이 비어 있으면 무시
    if (frame.empty()) {
        return;
    }

    if (camIdx >= MAX_CAMERAS) return;
    
    // 벡터 크기를 4개로 한 번만 설정
    if (cameraFrames.size() != MAX_CAMERAS) {
        cameraFrames.resize(MAX_CAMERAS);
    }
    
    // 기존 프레임이 있으면 해제하고 새로 할당
    cameraFrames[camIdx] = frame.clone();
    
    // **메인 카메라 처리**
    if (camIdx == cameraIndex) {
        try {
            // **검사 모드가 아닐 때만 화면 업데이트**
            if (!cameraView || !cameraView->getInspectionMode()) {
                // 필터 적용
                cv::Mat filteredFrame = frame.clone();
                cameraView->applyFiltersToImage(filteredFrame);
                
                // RGB 변환
                cv::Mat displayFrame;
                cv::cvtColor(filteredFrame, displayFrame, cv::COLOR_BGR2RGB);
                
                // QImage로 변환
                QImage image(displayFrame.data, displayFrame.cols, displayFrame.rows, 
                           displayFrame.step, QImage::Format_RGB888);
                QPixmap pixmap = QPixmap::fromImage(image.copy());
                
                // UI 업데이트
                if (cameraView) {
                    cameraView->setBackgroundPixmap(pixmap);
                    cameraView->update();
                }
            }
        }
        catch (const std::exception& e) {
        }
        return;
    }
    
    // **미리보기 카메라 처리**
    updatePreviewFrames();
}

void TeachingWidget::updatePreviewFrames() {
    // 모든 미리보기 레이블 순회
    for (int labelIdx = 0; labelIdx < cameraPreviewLabels.size(); labelIdx++) {
        QLabel* previewLabel = cameraPreviewLabels[labelIdx];
        if (!previewLabel) continue;
        
        // 이 레이블에 할당된 카메라 UUID 가져오기
        QString assignedUuid = previewLabel->property("uniqueCameraId").toString();
        
        if (assignedUuid.isEmpty()) {
            // UUID가 없으면 "연결 없음" 표시
            previewLabel->clear();
            previewLabel->setText(TR("NO_CONNECTION"));
            previewLabel->setStyleSheet("background-color: black; color: white;");
            continue;
        }
        
        // UUID로 카메라 인덱스 찾기
        int camIdx = -1;
        QString cameraName;
        int cameraCount = getCameraInfosCount();
        for (int i = 0; i < cameraCount; i++) {
            CameraInfo info = getCameraInfo(i);
            if (info.uniqueId == assignedUuid) {
                camIdx = i;
                cameraName = info.name;
                break;
            }
        }
        
        // 카메라 인덱스가 유효하고 프레임이 있는지 확인
        if (camIdx >= 0 && camIdx < static_cast<int>(cameraFrames.size()) && 
            !cameraFrames[camIdx].empty()) {
            
            try {
                // 프레임 복사 및 크기 조정
                cv::Mat previewFrame = cameraFrames[camIdx].clone();
                cv::resize(previewFrame, previewFrame, cv::Size(160, 120));
                cv::cvtColor(previewFrame, previewFrame, cv::COLOR_BGR2RGB);
                
                // QPixmap 변환
                QImage image(previewFrame.data, previewFrame.cols, previewFrame.rows, 
                           previewFrame.step, QImage::Format_RGB888);
                QPixmap pixmap = QPixmap::fromImage(image.copy());
                
                // 레이블에 설정
                QSize labelSize = previewLabel->size();
                if (labelSize.width() > 0 && labelSize.height() > 0) {
                    QPixmap scaledPixmap = pixmap.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    
                    previewLabel->setPixmap(scaledPixmap);
                    previewLabel->setScaledContents(true);
                    previewLabel->setStyleSheet("background-color: black;");
                    
                    // 툴팁 설정
                    previewLabel->setToolTip(QString("클릭하여 %1로 전환\nUUID: %2")
                                           .arg(cameraName).arg(assignedUuid));
                }
            }
            catch (const std::exception& e) {
                previewLabel->clear();
                previewLabel->setText(TR("PROCESSING_ERROR"));
                previewLabel->setStyleSheet("background-color: red; color: white;");
            }
        } else {
            // 프레임이 없으면 "신호 없음" 표시
            previewLabel->clear();
            previewLabel->setText(TR("NO_SIGNAL"));
            previewLabel->setStyleSheet("background-color: gray; color: white;");
        }
    }
}

void TeachingWidget::startCamera() {
    qDebug() << "startCamera() 함수 시작";
    
    // 1. 카메라 정보 갱신
    detectCameras();

    // 2. 기존 스레드 중지 및 정리
    for (CameraGrabberThread* thread : cameraThreads) {
        if (thread && thread->isRunning()) {
            thread->stopGrabbing();
            thread->wait();
            delete thread;
        }
    }
    cameraThreads.clear();

    if (uiUpdateThread && uiUpdateThread->isRunning()) {
        uiUpdateThread->stopUpdating();
        uiUpdateThread->wait();
    }

    // 3. 카메라가 하나도 연결되어 있지 않은 경우
    if (cameraInfos.isEmpty()) {
        UIColors::showWarning(this, "카메라 오류", "연결된 카메라가 없습니다.");
        disableAllUIElements();
        updateCameraButtonState(false);  // 버튼 상태 업데이트
        return;
    }

    // 4. 메인 카메라 설정
    enableAllUIElements();
    cameraIndex = 0;
  
    // 현재 카메라 UUID 설정
    if (cameraView) {
        cameraView->setCurrentCameraUuid(cameraInfos[cameraIndex].uniqueId);
    }

    // 5. 미리보기 레이블 초기화 및 할당
    for (int i = 0; i < cameraPreviewLabels.size(); i++) {
        if (cameraPreviewLabels[i]) {
            cameraPreviewLabels[i]->clear();
            cameraPreviewLabels[i]->setProperty("uniqueCameraId", "");
            cameraPreviewLabels[i]->setText(TR("NO_CONNECTION"));
            cameraPreviewLabels[i]->setStyleSheet("background-color: black; color: white;");
        }
    }

    QSet<int> usedCameras;
    usedCameras.insert(cameraIndex); // 메인 카메라는 이미 사용 중
    
    int previewLabelIndex = 0;
    for (int i = 0; i < cameraInfos.size(); i++) {
        if (usedCameras.contains(i)) continue;
        
        if (previewLabelIndex < cameraPreviewLabels.size() && cameraPreviewLabels[previewLabelIndex]) {
            cameraPreviewLabels[previewLabelIndex]->setProperty("uniqueCameraId", cameraInfos[i].uniqueId);
            cameraPreviewLabels[previewLabelIndex]->installEventFilter(this);
            cameraPreviewLabels[previewLabelIndex]->setCursor(Qt::PointingHandCursor);
            usedCameras.insert(i);
            cameraPreviewLabels[previewLabelIndex]->clear();
            previewLabelIndex++;
        }
    }

    // 6. 미리보기 UI 업데이트
    updatePreviewUI();

    // 7. 카메라 스레드 생성 및 시작
    for (int i = 0; i < cameraInfos.size(); i++) {
        if (cameraInfos[i].isConnected && cameraInfos[i].capture) {
            CameraGrabberThread* thread = new CameraGrabberThread(this);
            thread->setCameraIndex(i);
            connect(thread, &CameraGrabberThread::frameGrabbed,
                    this, &TeachingWidget::processGrabbedFrame, Qt::QueuedConnection);
            thread->start(QThread::NormalPriority);
            cameraThreads.append(thread);  
        }
    }

    // 8. UI 업데이트 스레드 시작
    if (uiUpdateThread)
        uiUpdateThread->start(QThread::NormalPriority);

    // 9. 카메라가 실제로 연결되고 라이브 모드가 시작된 경우에만 레시피 로드
    bool cameraStarted = false;
    for (const auto& cameraInfo : cameraInfos) {
        if (cameraInfo.isConnected && cameraInfo.capture) {
            cameraStarted = true;
            break;
        }
    }
    
    if (cameraStarted) {
        qDebug() << "startCamera: 카메라가 연결되어 레시피 로드 시작";
        
        // 자동 레시피 로드
        openRecipe(true);  // true = 자동 모드
        
        qDebug() << "startCamera: 레시피 로드 완료";
    } else {
        qDebug() << "startCamera: 카메라가 연결되지 않아 레시피 로드하지 않음";
    }
    
    // 10. 패턴 트리 업데이트 (라이브 모드 시작 시 현재 카메라 패턴 표시)
    qDebug() << "startCamera: 패턴 트리 업데이트 호출, 현재 카메라 UUID:" << cameraInfos[cameraIndex].uniqueId;
    updatePatternTree();
    qDebug() << "startCamera: updatePatternTree() 호출 완료";
    
    // 11. CAM 버튼 상태 업데이트
    updateCameraButtonState(true);
    
}

void TeachingWidget::updateCameraButtonState(bool isStarted) {
    if (!startCameraButton) return;
    
    startCameraButton->blockSignals(true);
    
    if (isStarted) {
        // 카메라 시작됨 - 영상 스트리밍 중
        startCameraButton->setChecked(true);
        startCameraButton->setText(TR("CAM ON"));  // 또는 "STREAMING"
        startCameraButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_CAM_OFF_COLOR, UIColors::BTN_CAM_ON_COLOR, true));
    } else {
        // 카메라 중지됨 - 영상 없음
        startCameraButton->setChecked(false);
        startCameraButton->setText(TR("CAM OFF"));  // 또는 "NO VIDEO"
        startCameraButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_CAM_OFF_COLOR, UIColors::BTN_CAM_ON_COLOR, false));
    }
    
    startCameraButton->blockSignals(false);
    
    // UI 요소들 활성화/비활성화
    if (isStarted) {
        enableAllUIElements();
    } else {
        disableAllUIElements();
    }
}

void TeachingWidget::stopCamera() {
    
    // UI 요소들 비활성화
    disableAllUIElements();

    // 1. 멀티 카메라 스레드 중지
    for (CameraGrabberThread* thread : cameraThreads) {
        if (thread && thread->isRunning()) {
            thread->stopGrabbing();
            thread->wait();
            delete thread;
        }
    }
    cameraThreads.clear();
    
    // 2. UI 업데이트 스레드 중지
    if (uiUpdateThread && uiUpdateThread->isRunning()) {
        uiUpdateThread->stopUpdating();
        uiUpdateThread->wait();
    }
    
#ifdef USE_SPINNAKER
    // 3. Spinnaker 카메라 정리
    if (m_useSpinnaker) {
        try {
            for (auto& camera : m_spinCameras) {
                if (camera && camera->IsStreaming()) {
                    camera->EndAcquisition();
                }
                if (camera && camera->IsInitialized()) {
                    camera->DeInit();
                }
            }
            m_spinCameras.clear();
            if (m_spinCamList.GetSize() == 0) {
                m_spinCamList.Clear();
            }
        }
        catch (Spinnaker::Exception& e) {
        }
    }
#endif
    
    // 4. OpenCV 카메라 자원 해제
    for (int i = 0; i < cameraInfos.size(); i++) {
        if (cameraInfos[i].capture && !cameraInfos[i].uniqueId.startsWith("SPINNAKER_")) {
            cameraInfos[i].capture->release();
            delete cameraInfos[i].capture;
            cameraInfos[i].capture = nullptr;
        }
        cameraInfos[i].isConnected = false;
    }
    
    // 5. 미리보기 레이블 초기화
    for (int i = 0; i < cameraPreviewLabels.size(); i++) {
        QLabel* previewLabel = cameraPreviewLabels[i];
        if (previewLabel) {
            previewLabel->clear();
            previewLabel->setProperty("uniqueCameraId", "");
            previewLabel->setScaledContents(false);
            previewLabel->setAlignment(Qt::AlignCenter);
            previewLabel->setStyleSheet("background-color: black; color: white;");
            previewLabel->setText(TR("NO_CONNECTION"));
        }
    }
    
    // 6. 메인 카메라 뷰 초기화
    if (cameraView) {
        cameraView->setInspectionMode(false);
        cameraFrames.clear();
        
        // 모든 패턴들 지우기
        cameraView->clearPatterns();
        
        // 백그라운드 이미지도 지우기
        QPixmap emptyPixmap;
        cameraView->setBackgroundPixmap(emptyPixmap);
        cameraView->update();
    }
    
    // 패턴 목록 UI도 업데이트
    updatePatternTree();
    
    // 카메라 정보를 "연결 없음"으로 표시
    updateCameraInfoForDisconnected();
    
    // 7. CAM 버튼 상태 업데이트
    updateCameraButtonState(false);
     
    // 8. RUN 버튼 상태 초기화
    if (runStopButton && runStopButton->isChecked()) {
        runStopButton->blockSignals(true);
        runStopButton->setChecked(false);
        runStopButton->setText("RUN");
        runStopButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));
        runStopButton->blockSignals(false);
    }
    
    // 9. 카메라 정보 목록 비우기
    cameraInfos.clear();
    cameraIndex = -1;
    
}

void TeachingWidget::updateUITexts() {    
    // **언어매니저 내부 번역 맵 확인**
    const auto& translations = LanguageManager::instance()->getAllTranslations();
        
    // 기본 UI 텍스트 업데이트
    if (roiButton) roiButton->setText(TR("ROI"));
    if (fidButton) fidButton->setText(TR("FID")); 
    if (insButton) insButton->setText(TR("INS"));
    
    // 메뉴 텍스트 업데이트 및 활성화 상태 유지
    if (fileMenu) fileMenu->setTitle(TR("FILE_MENU"));
    if (settingsMenu) {
        settingsMenu->setTitle(TR("SETTINGS_MENU"));
        settingsMenu->setEnabled(true);  // 활성화 상태 유지
    }
    if (toolsMenu) {
        toolsMenu->setTitle(TR("TOOLS_MENU"));
        toolsMenu->setEnabled(true);  // 활성화 상태 유지
    }
    if (simulateMenu) {
        simulateMenu->setTitle(TR("SIMULATE_MENU"));
        simulateMenu->setEnabled(true);  // 활성화 상태 유지
    }
    if (helpMenu) {
        helpMenu->setTitle(TR("HELP_MENU"));
        helpMenu->setEnabled(true);  // 활성화 상태 유지
    }
    
    // 액션 텍스트 업데이트 및 활성화 상태 유지
    if (simulateAction) {
        simulateAction->setText(TR("OPEN_SIMULATION"));
        simulateAction->setEnabled(true);  // 활성화 상태 유지
    }
    if (exitAction) exitAction->setText(TR("EXIT"));
    if (cameraSettingsAction) {
        cameraSettingsAction->setText(TR("CAMERA_RECIPE_SETTINGS"));
        cameraSettingsAction->setEnabled(true);  // 활성화 상태 유지
    }
    if (languageSettingsAction) {
        languageSettingsAction->setText(TR("LANGUAGE_SETTINGS"));
        languageSettingsAction->setEnabled(true);  // 활성화 상태 유지
    }
    if (calibrateAction) {
        calibrateAction->setText(TR("LENGTH_CALIBRATION"));
        calibrateAction->setEnabled(true);  // 활성화 상태 유지
    }
    if (aboutAction) {
        aboutAction->setText(TR("ABOUT"));
        aboutAction->setEnabled(true);  // 활성화 상태 유지
    }
    
    // 시뮬레이션 액션도 활성화 상태 유지
    if (simulateMenu) {
        QList<QAction*> simulateActions = simulateMenu->actions();
        for (QAction* action : simulateActions) {
            action->setEnabled(true);
        }
    }
    
    // **패턴 트리 헤더 업데이트**
    if (patternTree) {
        QStringList headers;
        headers << TR("PATTERN_NAME") << TR("PATTERN_TYPE") << TR("PATTERN_STATUS");
        patternTree->setHeaderLabels(headers);
        
        // 헤더 뷰 갱신
        QHeaderView* header = patternTree->header();
        header->update();
        header->repaint();
                
        // 기존 패턴들의 텍스트도 갱신
        updateTreeItemTexts(nullptr);
    }
    
    // 나머지 UI 텍스트들도 TR로 처리
    if (emptyPanelLabel) emptyPanelLabel->setText(TR("EMPTY_PANEL_MESSAGE"));
    if (basicInfoLabel) basicInfoLabel->setText(TR("BASIC_INFO"));
    if (patternIdLabel) patternIdLabel->setText(TR("PATTERN_ID"));
    if (patternNameLabel) patternNameLabel->setText(TR("PATTERN_NAME_LABEL"));
    if (patternTypeLabel) patternTypeLabel->setText(TR("PATTERN_TYPE_LABEL"));
    if (positionSizeLabel) positionSizeLabel->setText(TR("POSITION_SIZE"));
    if (positionLabel) positionLabel->setText(TR("POSITION"));
    if (sizeLabel) sizeLabel->setText(TR("SIZE"));
    
    // CameraView의 텍스트도 업데이트
    if (cameraView) {
        cameraView->updateUITexts();
    }
    
    // **강제로 모든 메뉴 활성화 (언어 변경 후에도 유지)**
    if (menuBar) {
        // 도움말 메뉴가 없으면 다시 생성
        if (!helpMenu) {
            helpMenu = menuBar->addMenu(TR("HELP_MENU"));
            helpMenu->setEnabled(true);
            helpMenu->menuAction()->setMenuRole(QAction::NoRole);
            
            if (!aboutAction) {
                aboutAction = helpMenu->addAction(TR("ABOUT"));
                aboutAction->setEnabled(true);
                aboutAction->setMenuRole(QAction::NoRole);
                connect(aboutAction, &QAction::triggered, this, &TeachingWidget::showAboutDialog);
            }
        }
        
        // 시뮬레이션 메뉴가 없으면 다시 생성
        if (!simulateMenu) {
            simulateMenu = menuBar->addMenu(TR("SIMULATE_MENU"));
            simulateMenu->setEnabled(true);
            
            simulateAction = simulateMenu->addAction(TR("OPEN_SIMULATION"));
            simulateAction->setEnabled(true);
            connect(simulateAction, &QAction::triggered, this, &TeachingWidget::showSimulationDialog);
        }
        
        QList<QAction*> actions = menuBar->actions();
        for (QAction* action : actions) {
            action->setEnabled(true);
            if (action->menu()) {
                action->menu()->setEnabled(true);
                QList<QAction*> subActions = action->menu()->actions();
                for (QAction* subAction : subActions) {
                    subAction->setEnabled(true);
                }
            }
        }
    }
    
    // 전체 위젯 강제 갱신 및 즉시 처리
    this->repaint();
    QApplication::processEvents(); // 즉시 화면 갱신
    
    // 모든 자식 위젯들도 강제 갱신
    QList<QWidget*> childWidgets = this->findChildren<QWidget*>();
    for (QWidget* child : childWidgets) {
        child->update();
    }
}

void TeachingWidget::updateTreeItemTexts(QTreeWidgetItem* item) {
    // item이 null이면 모든 최상위 아이템부터 시작
    if (!item) {
        for (int i = 0; i < patternTree->topLevelItemCount(); i++) {
            updateTreeItemTexts(patternTree->topLevelItem(i));
        }
        return;
    }
    
    // 현재 아이템의 텍스트 갱신
    QString idStr = item->data(0, Qt::UserRole).toString();
    QVariant filterIndexVar = item->data(0, Qt::UserRole + 1);
    
    if (filterIndexVar.isValid()) {
        // 필터 아이템인 경우
        int filterIndex = filterIndexVar.toInt();
        QUuid patternId = QUuid(idStr);
        PatternInfo* pattern = cameraView->getPatternById(patternId);
        
        if (pattern && filterIndex >= 0 && filterIndex < pattern->filters.size()) {
            const FilterInfo& filter = pattern->filters[filterIndex];
            
            // 필터 이름을 번역된 텍스트로 변경
            QString filterName = getFilterTypeName(filter.type);
            item->setText(0, filterName);
            item->setText(1, TR("FILTER_TYPE_ABBREV")); // "FIL" 등
            
            // 상태 텍스트도 번역
            item->setText(2, filter.enabled ? TR("ACTIVE") : TR("INACTIVE"));
        }
    } else {
        // 패턴 아이템인 경우
        QUuid patternId = QUuid(idStr);
        PatternInfo* pattern = cameraView->getPatternById(patternId);
        
        if (pattern) {
            // 패턴 타입 텍스트 번역
            QString typeText;
            switch (pattern->type) {
                case PatternType::ROI:
                    typeText = TR("ROI");
                    break;
                case PatternType::FID:
                    typeText = TR("FID");
                    break;
                case PatternType::INS:
                    typeText = TR("INS");
                    break;
                case PatternType::FIL:
                    typeText = TR("FILTER_TYPE_ABBREV");
                    break;
            }
            item->setText(1, typeText);
            
            // 상태 텍스트 번역
            item->setText(2, pattern->enabled ? TR("ACTIVE") : TR("INACTIVE"));
        }
    }
    
    // 재귀적으로 모든 자식 아이템 처리
    for (int i = 0; i < item->childCount(); i++) {
        updateTreeItemTexts(item->child(i));
    }
}

void TeachingWidget::setSerialCommunication(SerialCommunication* serialComm) {
    serialCommunication = serialComm;
}

void TeachingWidget::showSerialSettings() {
    // 시리얼 통신 객체가 없으면 에러
    if (!serialCommunication) {
        QMessageBox::warning(this, TR("WARNING"), "시리얼 통신이 초기화되지 않았습니다.");
        return;
    }
    
    // 시리얼 설정 다이얼로그가 없으면 생성
    if (!serialSettingsDialog) {
        serialSettingsDialog = new SerialSettingsDialog(serialCommunication, this);
    }
    
    // 다이얼로그 표시
    serialSettingsDialog->exec();
}

void TeachingWidget::openLanguageSettings() {
    LanguageSettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        // 언어가 변경된 경우 UI 텍스트 업데이트
        updateUITexts();
    }
}

void TeachingWidget::updatePreviewUI() {
    // 미리보기 레이블 업데이트
    for (int i = 0; i < cameraPreviewLabels.size(); i++) {
        if (i >= cameraPreviewLabels.size()) continue;
        QLabel* previewLabel = cameraPreviewLabels[i];
        if (!previewLabel) continue;
        
        QString uniqueCameraId = previewLabel->property("uniqueCameraId").toString();
        if (uniqueCameraId.isEmpty()) {
            previewLabel->clear();
            previewLabel->setText(TR("NO_CONNECTION"));
            previewLabel->setStyleSheet("background-color: black; color: white;");
            continue;
        }
        
        // 고유 ID로 카메라 정보 찾기
        int foundCameraIndex = -1;
        int cameraCount = getCameraInfosCount();
        for (int j = 0; j < cameraCount; j++) {
            CameraInfo info = getCameraInfo(j);
            if (info.uniqueId == uniqueCameraId) {
                foundCameraIndex = j;
                break;
            }
        }
        
        // 레이블에 카메라 정보 표시
        if (foundCameraIndex >= 0) {
            CameraInfo info = getCameraInfo(foundCameraIndex);
            if (info.isConnected) {
                // 카메라가 연결되어 있으면 빈 텍스트로 설정 (영상이 표시될 것임)
                previewLabel->setText("");
                previewLabel->setStyleSheet("");
                // 여기서 setPixmap()을 호출하지 않음 - processGrabbedFrame에서 처리함
            } else {
                // 연결되지 않은 카메라 정보 표시
                previewLabel->clear();
                previewLabel->setText(TR("NO_CONNECTION"));
                previewLabel->setStyleSheet("background-color: black; color: white;");
            }
        } else {
            // 매핑된 카메라를 찾을 수 없는 경우
            previewLabel->clear();
            previewLabel->setText(TR("NO_CONNECTION"));
            previewLabel->setStyleSheet("background-color: black; color: white;");
        }
    }
}

void TeachingWidget::updateCameraFrame() {
    // **시뮬레이션 모드 처리**
    if (simulationMode && !currentSimulationImage.empty()) {
        printf("[TeachingWidget] 시뮬레이션 모드에서 필터 적용\n");
        fflush(stdout);
        
        // 시뮬레이션 이미지에 필터 적용
        cv::Mat filteredFrame = currentSimulationImage.clone();
        cameraView->applyFiltersToImage(filteredFrame);
        
        // RGB 변환 및 UI 업데이트
        cv::Mat displayFrame;
        if (filteredFrame.channels() == 3) {
            cv::cvtColor(filteredFrame, displayFrame, cv::COLOR_BGR2RGB);
        } else {
            displayFrame = filteredFrame.clone();
        }
        
        QImage image;
        if (displayFrame.channels() == 3) {
            image = QImage(displayFrame.data, displayFrame.cols, displayFrame.rows, 
                          displayFrame.step, QImage::Format_RGB888);
        } else {
            image = QImage(displayFrame.data, displayFrame.cols, displayFrame.rows, 
                          displayFrame.step, QImage::Format_Grayscale8);
        }
        
        QPixmap pixmap = QPixmap::fromImage(image);
        
        QSize origSize(currentSimulationImage.cols, currentSimulationImage.rows);
        cameraView->setScalingInfo(origSize, cameraView->size());
        cameraView->setStatusInfo("SIM");
        
        cameraView->setBackgroundPixmap(pixmap);
        cameraView->update();
        
        printf("[TeachingWidget] 시뮬레이션 모드 필터 적용 완료\n");
        fflush(stdout);
        return;
    }
    
    // **메인 카메라 프레임 업데이트만 처리**
    if (cameraIndex >= 0 && cameraIndex < cameraInfos.size() && 
        cameraInfos[cameraIndex].isConnected) {
        
#ifdef USE_SPINNAKER
        // Spinnaker 카메라 확인
        if (m_useSpinnaker && cameraInfos[cameraIndex].uniqueId.startsWith("SPINNAKER_") && 
            cameraIndex < static_cast<int>(m_spinCameras.size())) {
            
            cv::Mat frame = grabFrameFromSpinnakerCamera(m_spinCameras[cameraIndex]);
  
            if (!frame.empty()) {
                // **벡터에 저장**
                if (cameraIndex >= static_cast<int>(cameraFrames.size())) {
                    cameraFrames.resize(cameraIndex + 1);
                }
                
                cv::Mat bgrFrame;
                cv::cvtColor(frame, bgrFrame, cv::COLOR_RGB2BGR);
                cameraFrames[cameraIndex] = bgrFrame.clone();
                
                // 필터 적용된 프레임 생성
                cv::Mat filteredFrame = cameraFrames[cameraIndex].clone();
                cameraView->applyFiltersToImage(filteredFrame);
                
                // RGB 변환 및 UI 업데이트
                cv::Mat displayFrame;
                cv::cvtColor(filteredFrame, displayFrame, cv::COLOR_BGR2RGB);
                
                QImage image(displayFrame.data, displayFrame.cols, displayFrame.rows, 
                             displayFrame.step, QImage::Format_RGB888);
                QPixmap pixmap = QPixmap::fromImage(image);
                
                QSize origSize(frame.cols, frame.rows);
                cameraView->setScalingInfo(origSize, cameraView->size());
                cameraView->setStatusInfo(QString("CAM%1").arg(cameraIndex + 1));
                
                cameraView->setBackgroundPixmap(pixmap);
            }
        } else
#endif
        // OpenCV 카메라 사용
        if (cameraInfos[cameraIndex].capture) {
            cv::Mat frame;
            if (cameraInfos[cameraIndex].capture->read(frame)) {
                // **벡터에 저장**
                if (cameraIndex >= static_cast<int>(cameraFrames.size())) {
                    cameraFrames.resize(cameraIndex + 1);
                }
                
                cameraFrames[cameraIndex] = frame.clone();
                
                // 필터 적용된 프레임 생성
                cv::Mat filteredFrame = cameraFrames[cameraIndex].clone();
                cameraView->applyFiltersToImage(filteredFrame);
                
                // RGB 변환 및 UI 업데이트
                cv::Mat displayFrame;
                cv::cvtColor(filteredFrame, displayFrame, cv::COLOR_BGR2RGB);
                
                QImage image(displayFrame.data, displayFrame.cols, displayFrame.rows, 
                             displayFrame.step, QImage::Format_RGB888);
                QPixmap pixmap = QPixmap::fromImage(image);
                
                QSize origSize(frame.cols, frame.rows);
                cameraView->setScalingInfo(origSize, cameraView->size());
                cameraView->setStatusInfo(QString("CAM%1").arg(cameraIndex + 1));
                
                cameraView->setBackgroundPixmap(pixmap);
            }
        }
    }
}

bool TeachingWidget::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            
            // **미리보기 레이블 클릭 확인**
            for (int i = 0; i < cameraPreviewLabels.size(); i++) {
                if (watched == cameraPreviewLabels[i]) {
                    QString cameraUuid = cameraPreviewLabels[i]->property("uniqueCameraId").toString();
                    
                    if (!cameraUuid.isEmpty()) {
                        // 현재 카메라와 다른 경우에만 전환
                        if (isValidCameraIndex(cameraIndex)) {
                            QString currentUuid = getCameraInfo(cameraIndex).uniqueId;
                            if (cameraUuid != currentUuid) {
                                // **단순한 카메라 전환만**
                                switchToCamera(cameraUuid);
                            }
                        }
                        return true;
                    }
                    break;
                }
            }
            
            // **템플릿 이미지 클릭 처리 (기존 코드)**
            if (watched == fidTemplateImg || watched == insTemplateImg) {
                QLabel* imageLabel = qobject_cast<QLabel*>(watched);
                if (imageLabel) {
                    QTreeWidgetItem* selectedItem = patternTree->currentItem();
                    if (selectedItem) {
                        QString idStr = selectedItem->data(0, Qt::UserRole).toString();
                        QUuid patternId = QUuid(idStr);
                        PatternInfo* pattern = cameraView->getPatternById(patternId);
                        
                        if (pattern && !pattern->templateImage.isNull()) {
                            QString title = QString("%1 템플릿 이미지").arg(pattern->name);
                            showImageViewerDialog(pattern->templateImage, title);
                        }
                    }
                    return true;
                }
            }
        }
    }
    
    return QWidget::eventFilter(watched, event);
}

void TeachingWidget::switchToCamera(const QString& cameraUuid) {
    // 인자로 받은 UUID가 현재 카메라와 같은지 확인
    if (cameraIndex >= 0 && cameraIndex < cameraInfos.size() && 
        cameraUuid == cameraInfos[cameraIndex].uniqueId) {
        return;
    }
    
    // **RUN 버튼 상태 확인 - 검사 모드인지 체크**
    bool wasInInspectionMode = false;
    if (runStopButton && runStopButton->isChecked()) {
        wasInInspectionMode = true;
        
        // **일단 라이브 모드로 전환**
        resumeToLiveMode();
    }
    
    // **검사 결과 및 UI 상태 정리**
    if (cameraView) {
        cameraView->setInspectionMode(false);
        cameraView->setCalibrationMode(false);
        cameraView->clearCurrentRect();
    }
    
    // **프로퍼티 패널 정리**
    if (propertyStackWidget) {
        propertyStackWidget->setCurrentIndex(0); // 빈 패널로 초기화
    }
    
    // **패턴 트리 선택 해제**
    if (patternTree) {
        patternTree->clearSelection();
    }
    
    // UUID로 카메라 인덱스 찾기
    int newCameraIndex = -1;
    int cameraCount = getCameraInfosCount();
    for (int i = 0; i < cameraCount; i++) {
        CameraInfo info = getCameraInfo(i);
        if (info.uniqueId == cameraUuid) {
            newCameraIndex = i;
            break;
        }
    }
    
    if (newCameraIndex < 0) {
        return;
    }
    
    // 새로운 메인 카메라 인덱스로 업데이트
    cameraIndex = newCameraIndex;
 
    // 현재 카메라에 맞는 캘리브레이션 정보 적용
    if (cameraCalibrationMap.contains(cameraUuid)) {
        CalibrationInfo calibInfo = cameraCalibrationMap[cameraUuid];
        cameraView->setCalibrationInfo(calibInfo);
    } else {
        CalibrationInfo emptyCalib;
        cameraView->setCalibrationInfo(emptyCalib);
    }

    // CameraView에 현재 카메라 UUID 설정
    if (cameraView) {
        cameraView->setCurrentCameraUuid(cameraUuid);
    }

    // **미리보기 레이블 재할당**
    for (int i = 0; i < cameraPreviewLabels.size(); i++) {
        if (cameraPreviewLabels[i]) {
            cameraPreviewLabels[i]->setProperty("uniqueCameraId", "");
            cameraPreviewLabels[i]->clear();
            cameraPreviewLabels[i]->setText(TR("NO_CONNECTION"));
            cameraPreviewLabels[i]->setStyleSheet("background-color: black; color: white;");
        }
    }

    // **새로운 미리보기 할당 - 메인 카메라 제외**
    int previewLabelIndex = 0;
    for (int i = 0; i < cameraInfos.size(); i++) {
        if (i == cameraIndex) continue; // 메인 카메라 제외
        
        if (previewLabelIndex < cameraPreviewLabels.size() && cameraPreviewLabels[previewLabelIndex]) {
            // **UUID로 할당 (인덱스가 아닌 UUID 기반)**
            cameraPreviewLabels[previewLabelIndex]->setProperty("uniqueCameraId", cameraInfos[i].uniqueId);
            cameraPreviewLabels[previewLabelIndex]->installEventFilter(this);
            cameraPreviewLabels[previewLabelIndex]->setCursor(Qt::PointingHandCursor);
            cameraPreviewLabels[previewLabelIndex]->setToolTip(QString("클릭하여 %1로 전환").arg(cameraInfos[i].name));
            
            previewLabelIndex++;
        }
    }
    
    // **즉시 미리보기 업데이트**
    updatePreviewFrames();
    
    // **UI 갱신**
    updatePatternTree();
    
    // **화면 강제 갱신**
    if (cameraView) {
        cameraView->update();
    }
    
    // **이벤트 처리**
    QApplication::processEvents();

    // **검사 모드였다면 다시 검사 모드로 전환**
    if (wasInInspectionMode) {
        // 잠깐 대기 후 검사 모드 재개 (UI 업데이트 시간 확보)
        QTimer::singleShot(200, this, [this]() {
            if (runStopButton && !runStopButton->isChecked()) {
                
                // RUN 버튼을 다시 체크된 상태로 만들기
                runStopButton->blockSignals(true);
                runStopButton->setChecked(true);
                runStopButton->blockSignals(false);
                
                // RUN 버튼 이벤트 수동 트리거
                runStopButton->clicked(true);
            }
        });
    }
}

QTreeWidgetItem* TeachingWidget::createPatternTreeItem(const PatternInfo& pattern) {
    QTreeWidgetItem* item = new QTreeWidgetItem();
    
    // 패턴 이름 - 카메라 UUID와 같으면 타입별 기본 이름 사용
    QString name = pattern.name;
    
    // 패턴 이름이 카메라 UUID 형태이거나 비어있으면 타입별 기본 이름 사용
    if (name.isEmpty() || name.startsWith("CV_") || name.contains("_0_0_")) {
        QString typePrefix;
        switch (pattern.type) {
            case PatternType::ROI: typePrefix = "ROI"; break;
            case PatternType::FID: typePrefix = "FID"; break;
            case PatternType::INS: typePrefix = "INS"; break;
            case PatternType::FIL: typePrefix = "FIL"; break;
        }
        name = QString("%1_%2").arg(typePrefix).arg(pattern.id.toString().left(8));
    }
    
    item->setText(0, name);
    
    // 패턴 타입
    QString typeText;
    switch (pattern.type) {
        case PatternType::ROI: typeText = TR("ROI"); break;
        case PatternType::FID: typeText = TR("FID"); break;
        case PatternType::INS: typeText = TR("INS"); break;
        case PatternType::FIL: typeText = TR("FIL"); break;
    }
    item->setText(1, typeText);
    
    // 활성화 상태
    item->setText(2, pattern.enabled ? TR("ACTIVE") : TR("INACTIVE"));
    
    // 패턴 ID 저장
    item->setData(0, Qt::UserRole, pattern.id.toString());
    
    // 활성화 체크박스 설정
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(0, pattern.enabled ? Qt::Checked : Qt::Unchecked);
    
    return item;
}

bool TeachingWidget::selectItemById(QTreeWidgetItem* item, const QUuid& id) {
    if (!item) return false;
    
    // 현재 아이템의 ID 확인
    QString idStr = item->data(0, Qt::UserRole).toString();
    QUuid itemId = QUuid(idStr);
    
    if (itemId == id) {
        patternTree->setCurrentItem(item);
        patternTree->scrollToItem(item);
        item->setSelected(true);
        return true;
    }
    
    // 재귀적으로 자식 아이템 확인
    for (int i = 0; i < item->childCount(); i++) {
        if (selectItemById(item->child(i), id)) {
            return true;
        }
    }
    
    return false;
}

QTreeWidgetItem* TeachingWidget::findItemById(QTreeWidgetItem* parent, const QUuid& id) {
    if (!parent) return nullptr;
    
    // 현재 아이템과 ID 비교
    if (getPatternIdFromItem(parent) == id) {
        return parent;
    }
    
    // 모든 자식에서 찾기
    for (int i = 0; i < parent->childCount(); i++) {
        QTreeWidgetItem* found = findItemById(parent->child(i), id);
        if (found) return found;
    }
    
    return nullptr;
}

// 패턴 이름 가져오기 (ID로)
QString TeachingWidget::getPatternName(const QUuid& patternId) {
    PatternInfo* pattern = cameraView->getPatternById(patternId);
    if (!pattern) return "알 수 없음";
    return pattern->name.isEmpty() ? QString("패턴 %1").arg(patternId.toString().left(8)) : pattern->name;
}

void TeachingWidget::updateCameraDetailInfo(CameraInfo& info) {
#ifdef __APPLE__
    // Mac에서 카메라 장치 정보 가져오기 - 개선된 버전
    
    // 1. `system_profiler` 명령어로 직접 출력 파싱 (JSON보다 더 안정적)
    QProcess process;
    process.start("system_profiler", QStringList() << "SPCameraDataType" << "SPUSBDataType");
    process.waitForFinished();
    
    QString output = process.readAllStandardOutput();
    QStringList lines = output.split('\n');
    
    // 카메라 섹션 찾기
    int cameraCount = -1;
    bool inCameraSection = false;
    QString cameraName;
    
    for (const QString& line : lines) {
        if (line.contains("Camera:") || line.contains("Cameras:") || line.contains("FaceTime")) {
            inCameraSection = true;
            cameraCount = -1; // 카메라 카운트 초기화
            continue;
        }
        
        // 카메라 섹션 내에서만 처리
        if (inCameraSection) {
            // 들여쓰기 레벨로 섹션 구분 (들여쓰기 없으면 새 섹션)
            if (!line.startsWith(" ") && !line.isEmpty()) {
                inCameraSection = false;
                continue;
            }
            
            if (line.trimmed().startsWith("Camera")) {
                cameraCount++;
                // 현재 카메라 인덱스와 일치하는지 확인
                if (cameraCount == info.index) {
                    cameraName = line.trimmed();
                    if (cameraName.contains(":")) {
                        cameraName = cameraName.section(':', 1).trimmed();
                    }
                    info.name = cameraName;
                }
            }
            
            // 현재 카메라에 대한 정보만 처리
            if (cameraCount == info.index) {
                if (line.contains("Unique ID:")) {
                    info.serialNumber = line.section(':', 1).trimmed();
                }
                
                if (line.contains("Product ID:")) {
                    info.productId = line.section(':', 1).trimmed();
                }
                
                if (line.contains("Vendor ID:")) {
                    info.vendorId = line.section(':', 1).trimmed();
                }
            }
        }
    }
    
    // 2. UUID 및 장치 경로 찾기 (디바이스 고유 식별에 더 좋음)
    QProcess avProcess;
    avProcess.start("system_profiler", QStringList() << "SPCameraDataType" << "-xml");
    avProcess.waitForFinished();
    
    QByteArray xmlOutput = avProcess.readAllStandardOutput();
    QBuffer buffer(&xmlOutput);
    buffer.open(QIODevice::ReadOnly);
    
    QXmlStreamReader xml(&buffer);
    bool inCameraArray = false;
    int cameraIndex = -1;
    
    while (!xml.atEnd()) {
        xml.readNext();
        
        if (xml.isStartElement()) {
            if (xml.name() == "array") {
                QString arrayKey = xml.attributes().value("key").toString();
                if (arrayKey == "_items") {
                    inCameraArray = true;
                }
            } else if (inCameraArray && xml.name() == "dict") {
                cameraIndex++;
            } else if (inCameraArray && cameraIndex == info.index) {
                QString key = xml.attributes().value("key").toString();
                
                // 다음 텍스트 요소 읽기
                if (key == "_name" || key == "spcamera_unique-id" || 
                    key == "spcamera_model-id" || key == "spcamera_device-path") {
                    xml.readNext();
                    if (xml.isCharacters()) {
                        QString value = xml.text().toString();
                        if (key == "_name") {
                            info.name = value;
                        } else if (key == "spcamera_unique-id") {
                            info.serialNumber = value;
                        } else if (key == "spcamera_model-id") {
                            info.productId = value;
                        } else if (key == "spcamera_device-path") {
                            info.locationId = value;
                        }
                    }
                }
            }
        } else if (xml.isEndElement()) {
            if (inCameraArray && xml.name() == "array") {
                inCameraArray = false;
            }
        }
    }
    
    // 3. IORegistry에서 직접 정보 가져오기 (가장 신뢰할만한 정보)
    QProcess ioregProcess;
    ioregProcess.start("ioreg", QStringList() << "-p" << "IOUSB" << "-w" << "0");
    ioregProcess.waitForFinished();
    
    QString ioregOutput = ioregProcess.readAllStandardOutput();
    QStringList ioregLines = ioregOutput.split('\n');
    
    // IORegistry에서 USB 장치 확인
    bool inUSBDevice = false;
    bool foundMatchingDevice = false;
    QString currentName;
    QString currentVID;
    QString currentPID;
    QString currentSerial;
    QString currentLocation;
    
    for (const QString& line : ioregLines) {
        // 새 USB 장치 시작
        if (line.contains("+-o")) {
            // 이전 장치가 카메라와 일치했다면 정보 저장
            if (foundMatchingDevice) {
                if (!currentName.isEmpty()) info.name = currentName;
                if (!currentVID.isEmpty()) info.vendorId = currentVID;
                if (!currentPID.isEmpty()) info.productId = currentPID;
                if (!currentSerial.isEmpty()) info.serialNumber = currentSerial;
                if (!currentLocation.isEmpty()) info.locationId = currentLocation;
                
                break;
            }
            
            // 새로운 장치 시작 - 변수 초기화
            inUSBDevice = true;
            foundMatchingDevice = false;
            currentName = "";
            currentVID = "";
            currentPID = "";
            currentSerial = "";
            currentLocation = "";
        }
        
        if (inUSBDevice) {
            // 장치 클래스가 카메라/비디오 관련인지 확인
            if (line.contains("bDeviceClass") && (line.contains("0e") || line.contains("0E") || line.contains("14"))) {
                foundMatchingDevice = true;
            }
            
            // 장치 이름이 "FaceTime" 또는 "Camera"를 포함하는지 확인
            if (line.contains("USB Product Name") && 
                (line.contains("FaceTime", Qt::CaseInsensitive) || 
                 line.contains("Camera", Qt::CaseInsensitive) || 
                 line.contains("CAM", Qt::CaseInsensitive))) {
                foundMatchingDevice = true;
                currentName = line.section('"', 1, 1); // 따옴표 사이의 텍스트 추출
            }
            
            // 장치가 카메라 인터페이스 클래스를 가지는지 확인
            if (line.contains("bInterfaceClass") && (line.contains("0e") || line.contains("0E") || line.contains("14"))) {
                foundMatchingDevice = true;
            }
            
            // 장치 정보 수집
            if (line.contains("idVendor")) {
                currentVID = line.section('=', 1).trimmed();
                currentVID = currentVID.section(' ', 0, 0); // 첫 번째 단어만 추출
            }
            
            if (line.contains("idProduct")) {
                currentPID = line.section('=', 1).trimmed();
                currentPID = currentPID.section(' ', 0, 0); // 첫 번째 단어만 추출
            }
            
            if (line.contains("USB Serial Number")) {
                currentSerial = line.section('"', 1, 1); // 따옴표 사이의 텍스트 추출
            }
            
            if (line.contains("locationID")) {
                currentLocation = line.section('=', 1).trimmed();
                currentLocation = currentLocation.section(' ', 0, 0); // 첫 번째 단어만 추출
            }
        }
    }
    
    // 4. 최후의 방법: 카메라 인덱스를 기반으로 생성된 고유 ID 
    // (다른 방법으로 찾지 못한 경우 적어도 카메라 식별은 가능하게)
    if (info.serialNumber.isEmpty() && info.locationId.isEmpty()) {
        // OpenCV의 카메라 프레임에서 직접 카메라 정보 추출 시도
        if (info.capture && info.capture->isOpened()) {
            double deviceId = info.capture->get(cv::CAP_PROP_POS_FRAMES); // 실패하면 0
            double apiId = info.capture->get(cv::CAP_PROP_PVAPI_PIXELFORMAT); // 실패하면 0
            double backend = info.capture->get(cv::CAP_PROP_BACKEND); // 카메라 백엔드 ID
            
            QString generatedId = QString("CV_%1_%2_%3_%4")
                .arg(info.index)
                .arg(deviceId)
                .arg(apiId)
                .arg(backend);
            
            info.serialNumber = generatedId; // 시리얼 번호로 사용
            info.locationId = QString("USB_CAM_%1").arg(info.index); // 위치 ID로 사용
            
        } else {
            // 카메라가 열려있지 않거나 접근할 수 없는 경우 - 인덱스만 사용
            info.serialNumber = QString("CAM_S%1").arg(info.index);
            info.locationId = QString("CAM_L%1").arg(info.index);
        }
    }
    
    // 최소한의 고유 식별자 보장
    if (info.uniqueId.isEmpty()) {
        if (!info.serialNumber.isEmpty()) {
            info.uniqueId = info.serialNumber;
        } else if (!info.locationId.isEmpty()) {
            info.uniqueId = info.locationId;
        } else if (!info.vendorId.isEmpty() && !info.productId.isEmpty()) {
            info.uniqueId = QString("VID_%1_PID_%2").arg(info.vendorId).arg(info.productId);
        } else {
            // 최후의 방법: 랜덤 문자와 함께 인덱스 사용
            const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            QString randStr;
            for (int i = 0; i < 6; i++) {
                randStr += chars.at(QRandomGenerator::global()->bounded(chars.length()));
            }
            info.uniqueId = QString("CAM_%1_%2").arg(info.index).arg(randStr);
        }
    }
    
    
#elif defined(_WIN32)
    // Windows에서 카메라 장치 정보 가져오기
    
    // 1. 장치 관리자에서 카메라 정보 가져오기
    QProcess deviceProcess;
    deviceProcess.start("wmic", QStringList() << "path" << "Win32_PnPEntity" << "where" 
                       << "ClassGuid=\"{4d36e96c-e325-11ce-bfc1-08002be10318}\"" << "get" 
                       << "Caption,DeviceID,PNPDeviceID,Description" << "/format:csv");
    deviceProcess.waitForFinished();
    QString deviceOutput = deviceProcess.readAllStandardOutput();
    QStringList deviceLines = deviceOutput.split("\n");
    
    // 2. 디바이스 ID와 이름 매핑
    QMap<QString, QString> deviceNameMap;  // 디바이스 ID -> 이름 매핑
    QMap<QString, QString> devicePnpMap;   // 디바이스 ID -> PNP ID 매핑
    
    // CSV 출력에서 라인 읽기 (Node,Caption,Description,DeviceID,PNPDeviceID 형식)
    for (int i = 1; i < deviceLines.size(); i++) { // 첫 라인은 헤더이므로 건너뜀
        QString line = deviceLines[i].trimmed();
        if (line.isEmpty()) continue;
        
        QStringList parts = line.split(",");
        if (parts.size() >= 5) {
            QString nodeName = parts[0];
            QString caption = parts[1];
            QString description = parts[2];
            QString deviceId = parts[3];
            QString pnpId = parts[4];
            
            // 카메라/웹캠 관련 디바이스 필터링
            if (caption.contains("camera", Qt::CaseInsensitive) || 
                caption.contains("webcam", Qt::CaseInsensitive) || 
                description.contains("camera", Qt::CaseInsensitive) || 
                description.contains("webcam", Qt::CaseInsensitive)) {
                
                deviceNameMap[deviceId] = caption;
                devicePnpMap[deviceId] = pnpId;
                
            }
        }
    }
    
    // 3. 디바이스 세부 정보 추출
    if (info.index < deviceNameMap.size()) {
        // 디바이스 목록에서 인덱스에 해당하는 장치 선택
        auto it = deviceNameMap.begin();
        std::advance(it, info.index);
        
        QString deviceId = it.key();
        QString deviceName = it.value();
        QString pnpId = devicePnpMap[deviceId];
        
        // 디바이스 이름 설정
        info.name = deviceName;
        
        // PNP ID에서 VID/PID 추출 (형식: USB\VID_XXXX&PID_YYYY...)
        QRegularExpression vidRegex("VID_([0-9A-F]{4})", QRegularExpression::CaseInsensitiveOption);
        QRegularExpression pidRegex("PID_([0-9A-F]{4})", QRegularExpression::CaseInsensitiveOption);
        
        QRegularExpressionMatch vidMatch = vidRegex.match(pnpId);
        QRegularExpressionMatch pidMatch = pidRegex.match(pnpId);
        
        if (vidMatch.hasMatch()) {
            info.vendorId = vidMatch.captured(1);
        }
        
        if (pidMatch.hasMatch()) {
            info.productId = pidMatch.captured(1);
        }
        
        // 시리얼 번호 추출 (사용 가능한 경우)
        QRegularExpression serialRegex("\\\\\([^\\\\]+\\)$");
        QRegularExpressionMatch serialMatch = serialRegex.match(pnpId);
        if (serialMatch.hasMatch()) {
            info.serialNumber = serialMatch.captured(1);
        }
        
        // 직접적인 디바이스 경로 저장
        info.locationId = deviceId;
        
        // 고유 ID 설정 (VID+PID+일부 디바이스 ID)
        if (!info.vendorId.isEmpty() && !info.productId.isEmpty()) {
            info.uniqueId = QString("VID_%1_PID_%2").arg(info.vendorId).arg(info.productId);
            
            // 시리얼 번호가 있으면 추가
            if (!info.serialNumber.isEmpty()) {
                info.uniqueId += "_" + info.serialNumber;
            } else {
                // 시리얼 번호가 없으면 디바이스 ID 일부를 추가
                info.uniqueId += "_" + deviceId.right(8).remove("{").remove("}").remove("-");
            }
        } else {
            // VID/PID를 추출할 수 없는 경우 인덱스 기반 ID 사용
            info.uniqueId = QString("WIN_CAM_%1").arg(info.index);
        }
    } else {
        // 인덱스에 해당하는 카메라가 없으면 기본값 사용
        info.name = QString("카메라 %1").arg(info.index + 1);
        info.uniqueId = QString("WIN_CAM_%1").arg(info.index);
    }
    
    // 4. 카메라 프레임에서 추가 정보 수집
    if (info.capture && info.capture->isOpened()) {
        try {
            // OpenCV에서 가능한 카메라 정보 수집
            double width = info.capture->get(cv::CAP_PROP_FRAME_WIDTH);
            double height = info.capture->get(cv::CAP_PROP_FRAME_HEIGHT);
            double fps = info.capture->get(cv::CAP_PROP_FPS);
            double backend = info.capture->get(cv::CAP_PROP_BACKEND);
            
            
            // 캡처 백엔드가 DirectShow(200)인 경우, USB 카메라로 간주
            if (backend == 200) {
                if (info.uniqueId.isEmpty() || !info.uniqueId.startsWith("VID_")) {
                    info.uniqueId = QString("DSHOW_%1_%2x%3_%4")
                        .arg(info.index)
                        .arg((int)width)
                        .arg((int)height)
                        .arg(QRandomGenerator::global()->bounded(1000, 9999));
                }
            }
        }
        catch (const cv::Exception& e) {
        }
    }
    
    // 5. 최소 고유 ID 보장
    if (info.uniqueId.isEmpty()) {
        // 고유 ID 생성
        const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        QString randStr;
        for (int i = 0; i < 6; i++) {
            randStr += chars.at(QRandomGenerator::global()->bounded(chars.length()));
        }
        info.uniqueId = QString("WIN_CAM_%1_%2").arg(info.index).arg(randStr);
    }
    

#elif defined(__linux__)
    // Linux에서 카메라 장치 정보 가져오기
    
    // 1. V4L2 장치 목록 가져오기
    QProcess v4lProcess;
    v4lProcess.start("v4l2-ctl", QStringList() << "--list-devices");
    v4lProcess.waitForFinished();
    QString v4lOutput = v4lProcess.readAllStandardOutput();
    QStringList v4lLines = v4lOutput.split("\n");
    
    // 2. 장치 정보 파싱
    QList<QPair<QString, QString>> cameraDevices; // 이름, 경로 쌍
    QString currentName;
    
    for (const QString& line : v4lLines) {
        if (line.isEmpty()) continue;
        
        // 탭으로 시작하지 않는 줄은 카메라 이름
        if (!line.startsWith("\t")) {
            currentName = line.trimmed();
            if (currentName.endsWith(":")) {
                currentName = currentName.left(currentName.length() - 1).trimmed();
            }
        }
        // 탭으로 시작하는 줄은 장치 경로
        else if (!currentName.isEmpty()) {
            QString devicePath = line.trimmed();
            if (devicePath.startsWith("/dev/video")) {
                cameraDevices.append(qMakePair(currentName, devicePath));
            }
        }
    }
    
    // 3. 인덱스에 해당하는 장치 정보 추출
    if (info.index < cameraDevices.size()) {
        QString deviceName = cameraDevices[info.index].first;
        QString devicePath = cameraDevices[info.index].second;
        
        // 카메라 이름 설정
        info.name = deviceName;
        
        // 장치 경로를 위치 ID로 사용
        info.locationId = devicePath;
        
        // USB 정보 추출 (udevadm 명령어 사용)
        QProcess udevProcess;
        udevProcess.start("udevadm", QStringList() << "info" << "--name=" + devicePath << "--attribute-walk");
        udevProcess.waitForFinished();
        QString udevOutput = udevProcess.readAllStandardOutput();
        QStringList udevLines = udevOutput.split("\n");
        
        // USB 정보 파싱
        QString idVendor, idProduct, serial;
        
        for (const QString& line : udevLines) {
            if (line.contains("idVendor")) {
                QRegularExpression re("idVendor==\"?([0-9a-fA-F]{4})\"?");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch()) idVendor = match.captured(1);
            } else if (line.contains("idProduct")) {
                QRegularExpression re("idProduct==\"?([0-9a-fA-F]{4})\"?");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch()) idProduct = match.captured(1);
            } else if (line.contains("serial")) {
                QRegularExpression re("serial==\"?([^\"]+)\"?");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch()) serial = match.captured(1);
            }
        }
        
        // 추출한 정보 저장
        info.vendorId = idVendor;
        info.productId = idProduct;
        info.serialNumber = serial;
        
        // 고유 ID 설정
        if (!idVendor.isEmpty() && !idProduct.isEmpty()) {
            info.uniqueId = QString("VID_%1_PID_%2").arg(idVendor).arg(idProduct);
            
            // 시리얼 번호가 있으면 추가
            if (!serial.isEmpty()) {
                info.uniqueId += "_" + serial;
            } else {
                // 장치 경로에서 번호만 추출
                QRegularExpression numRe("/dev/video(\\d+)");
                QRegularExpressionMatch numMatch = numRe.match(devicePath);
                if (numMatch.hasMatch()) {
                    info.uniqueId += "_DEV" + numMatch.captured(1);
                }
            }
        } else {
            // VID/PID를 추출할 수 없는 경우 장치 경로 기반 ID 사용
            QRegularExpression numRe("/dev/video(\\d+)");
            QRegularExpressionMatch numMatch = numRe.match(devicePath);
            if (numMatch.hasMatch()) {
                info.uniqueId = QString("LNX_VIDEO%1").arg(numMatch.captured(1));
            } else {
                info.uniqueId = QString("LNX_CAM_%1").arg(info.index);
            }
        }
        
        // 카메라 추가 정보 출력 (v4l2-ctl --all)
        QProcess v4lInfoProcess;
        v4lInfoProcess.start("v4l2-ctl", QStringList() << "--device=" + devicePath << "--all");
        v4lInfoProcess.waitForFinished();
        QString v4lInfoOutput = v4lInfoProcess.readAllStandardOutput();
        
        // 드라이버 정보 추출
        QRegularExpression driverRe("Driver name\\s*:\\s*(.+)");
        QRegularExpression busRe("Bus info\\s*:\\s*(.+)");
        QRegularExpressionMatch driverMatch = driverRe.match(v4lInfoOutput);
        QRegularExpressionMatch busMatch = busRe.match(v4lInfoOutput);
        
        if (driverMatch.hasMatch()) {
        }
        
        if (busMatch.hasMatch()) {
        }
    } else {
        // 인덱스에 해당하는 카메라가 없으면 기본값 사용
        info.name = QString("카메라 %1").arg(info.index + 1);
        info.uniqueId = QString("LNX_CAM_%1").arg(info.index);
    }
    
    // 4. 추가 정보 수집 (OpenCV)
    if (info.capture && info.capture->isOpened()) {
        try {
            double width = info.capture->get(cv::CAP_PROP_FRAME_WIDTH);
            double height = info.capture->get(cv::CAP_PROP_FRAME_HEIGHT);
            double fps = info.capture->get(cv::CAP_PROP_FPS);
            
        }
        catch (const cv::Exception& e) {
        }
    }
    
    // 5. 최소 고유 ID 보장
    if (info.uniqueId.isEmpty()) {
        const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        QString randStr;
        for (int i = 0; i < 6; i++) {
            randStr += chars.at(QRandomGenerator::global()->bounded(chars.length()));
        }
        info.uniqueId = QString("LNX_CAM_%1_%2").arg(info.index).arg(randStr);
    }
    
#endif
}

// 카메라 ID 및 이름 읽기 함수
QString TeachingWidget::getCameraName(int index) {
    // 기본 이름 (카메라를 찾지 못했을 경우)
    QString cameraName = QString("카메라 %1").arg(index);
    
    #ifdef __APPLE__
    // Mac에서 카메라 정보 가져오기
    QProcess process;
    process.start("system_profiler", QStringList() << "SPCameraDataType" << "SPUSBDataType" << "-json");
    process.waitForFinished();
    
    QByteArray output = process.readAllStandardOutput();
    QJsonDocument doc = QJsonDocument::fromJson(output);
    QJsonObject root = doc.object();
    
    // 1. 먼저 카메라 정보에서 찾기
    if (root.contains("SPCameraDataType")) {
        QJsonArray cameras = root["SPCameraDataType"].toArray();
        
        // 연결된 카메라 수 확인
        if (index < cameras.size()) {
            QJsonObject camera = cameras[index].toObject();
            QString deviceName;
            QString deviceID;
            
            if (camera.contains("_name")) {
                deviceName = camera["_name"].toString();
            }
            
            // 2. 이제 USB 정보에서 해당 카메라의 장치 ID 찾기
            if (root.contains("SPUSBDataType")) {
                QJsonArray usbDevices = root["SPUSBDataType"].toArray();
                
                // 모든 USB 장치 순회
                for (const QJsonValue &usbDeviceValue : usbDevices) {
                    QJsonObject usbDevice = usbDeviceValue.toObject();
                    
                    // USB 장치에 연결된 모든 항목 검색
                    if (usbDevice.contains("_items")) {
                        QJsonArray items = usbDevice["_items"].toArray();
                        
                        for (const QJsonValue &itemValue : items) {
                            QJsonObject item = itemValue.toObject();
                            
                            // 장치 이름이 카메라 이름과 일치하는지 확인
                            if (item.contains("_name") && item["_name"].toString() == deviceName) {
                                // 장치 ID 찾기
                                if (item.contains("location_id")) {
                                    deviceID = item["location_id"].toString();
                                } else if (item.contains("serial_num")) {
                                    deviceID = item["serial_num"].toString();
                                } else if (item.contains("vendor_id")) {
                                    deviceID = QString("VID_%1_PID_%2")
                                        .arg(item["vendor_id"].toString())
                                        .arg(item.contains("product_id") ? item["product_id"].toString() : "UNKNOWN");
                                }
                                
                                if (!deviceID.isEmpty()) {
                                    return QString("%1 [%2]").arg(deviceName).arg(deviceID);
                                }
                            }
                        }
                    }
                }
            }
            
            // ID를 찾지 못했지만 이름은 있는 경우
            if (!deviceName.isEmpty()) {
                return deviceName;
            }
        }
    }
    #elif defined(_WIN32)
    // Windows에서 카메라 정보 가져오기
    QProcess process;
    process.start("wmic", QStringList() << "path" << "Win32_PnPEntity" << "where" 
                 << "ClassGuid=\"{4d36e96c-e325-11ce-bfc1-08002be10318}\"" << "get" 
                 << "Caption,DeviceID,PNPDeviceID" << "/format:csv");
    process.waitForFinished();
    QByteArray output = process.readAllStandardOutput();
    QStringList lines = QString(output).split("\n");
    
    // 카메라 디바이스 목록 구성
    QList<QPair<QString, QString>> cameraDevices; // 이름, 디바이스 ID 쌍
    
    for (const QString& line : lines) {
        if (line.trimmed().isEmpty() || line.startsWith("Node")) continue;
        
        QStringList parts = line.split(",");
        if (parts.size() >= 3) {
            QString deviceName = parts[2].trimmed();
            QString deviceId = parts[3].trimmed();
            
            // 웹캠/카메라 관련 키워드 포함 여부 확인
            if (deviceName.contains("webcam", Qt::CaseInsensitive) || 
                deviceName.contains("camera", Qt::CaseInsensitive) ||
                deviceName.contains("cam", Qt::CaseInsensitive)) {
                cameraDevices.append(qMakePair(deviceName, deviceId));
            }
        }
    }
    
    // index에 해당하는 카메라 반환
    if (index < cameraDevices.size()) {
        QString deviceId = cameraDevices[index].second;
        QString deviceName = cameraDevices[index].first;
        
        // 고유 ID 추출 (USB\VID_xxxx&PID_yyyy&MI_zz 형식에서)
        QString vid, pid;
        QRegularExpression reVid("VID_([0-9A-F]{4})", QRegularExpression::CaseInsensitiveOption);
        QRegularExpression rePid("PID_([0-9A-F]{4})", QRegularExpression::CaseInsensitiveOption);
        
        QRegularExpressionMatch vidMatch = reVid.match(deviceId);
        QRegularExpressionMatch pidMatch = rePid.match(deviceId);
        
        if (vidMatch.hasMatch()) vid = vidMatch.captured(1);
        if (pidMatch.hasMatch()) pid = pidMatch.captured(1);
        
        if (!vid.isEmpty() && !pid.isEmpty()) {
            return QString("%1 [VID_%2_PID_%3]").arg(deviceName).arg(vid).arg(pid);
        }
        
        return deviceName;
    }
    #elif defined(__linux__)
    // Linux에서 카메라 정보 가져오기
    QProcess processV4L;
    processV4L.start("v4l2-ctl", QStringList() << "--list-devices");
    processV4L.waitForFinished();
    QByteArray outputV4L = processV4L.readAllStandardOutput();
    QStringList linesV4L = QString(outputV4L).split("\n");
    
    QList<QPair<QString, QString>> cameraDevices; // 이름, 디바이스 경로 쌍
    QString currentName;
    
    for (const QString& line : linesV4L) {
        if (line.isEmpty()) continue;
        
        // 탭으로 시작하지 않으면 카메라 이름
        if (!line.startsWith("\t")) {
            currentName = line.trimmed();
            // 끝의 괄호와 콜론 제거
            if (currentName.endsWith(":")) {
                currentName = currentName.left(currentName.length() - 1);
            }
        } 
        // 탭으로 시작하면 디바이스 경로
        else if (!currentName.isEmpty()) {
            QString devicePath = line.trimmed();
            if (devicePath.startsWith("/dev/video")) {
                cameraDevices.append(qMakePair(currentName, devicePath));
            }
        }
    }
    
    // index에 해당하는 카메라 반환
    if (index < cameraDevices.size()) {
        QString deviceName = cameraDevices[index].first;
        QString devicePath = cameraDevices[index].second;
        
        // USB 버스 및 장치 정보 가져오기 위한 추가 명령어
        QString usbInfo;
        QProcess processUSB;
        processUSB.start("udevadm", QStringList() << "info" << "--name=" + devicePath << "--attribute-walk");
        processUSB.waitForFinished();
        QByteArray outputUSB = processUSB.readAllStandardOutput();
        QStringList linesUSB = QString(outputUSB).split("\n");
        
        QString idVendor, idProduct, serial;
        
        for (const QString& line : linesUSB) {
            if (line.contains("idVendor")) {
                QRegularExpression re("idVendor==\"?([0-9a-fA-F]{4})\"?");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch()) idVendor = match.captured(1);
            } else if (line.contains("idProduct")) {
                QRegularExpression re("idProduct==\"?([0-9a-fA-F]{4})\"?");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch()) idProduct = match.captured(1);
            } else if (line.contains("serial")) {
                QRegularExpression re("serial==\"?([^\"]+)\"?");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch()) serial = match.captured(1);
            }
        }
        
        // 고유 ID 정보 추가
        if (!idVendor.isEmpty() && !idProduct.isEmpty()) {
            if (!serial.isEmpty()) {
                return QString("%1 [%2]").arg(deviceName).arg(serial);
            } else {
                return QString("%1 [VID_%2_PID_%3]").arg(deviceName).arg(idVendor).arg(idProduct);
            }
        }
        
        return QString("%1 [%2]").arg(deviceName).arg(devicePath);
    }
    #endif
    
    return cameraName;
}

bool TeachingWidget::runInspection(const cv::Mat& frame, int specificCameraIndex) {
    if (frame.empty()) {
        return false;
    }
    
    if (!cameraView || !insProcessor) {
        return false;
    }
    
    QList<PatternInfo> allPatterns = cameraView->getPatterns();
    QList<PatternInfo> cameraPatterns;
    QString currentCameraUuid;
    
    if (specificCameraIndex == -1) {
        // 시뮬레이션 모드: CameraView에서 현재 UUID 가져오기
        currentCameraUuid = cameraView->getCurrentCameraUuid();
    } else {
        // 실제 카메라 모드: 카메라 인덱스로 UUID 가져오기
        if (specificCameraIndex >= 0 && specificCameraIndex < cameraInfos.size()) {
            currentCameraUuid = cameraInfos[specificCameraIndex].uniqueId;
        } else {
            return false;
        }
    }
    
    for (const PatternInfo& pattern : allPatterns) {
        if (pattern.enabled && pattern.cameraUuid == currentCameraUuid) {
            cameraPatterns.append(pattern);
        }
    }

    try {
        InspectionResult result = insProcessor->performInspection(frame, cameraPatterns);
        
        // **추가**: 검사 결과를 기반으로 패턴들을 FID 중심으로 그룹 회전
        if (!result.angles.isEmpty()) {
  
            QList<PatternInfo> updatedPatterns = cameraView->getPatterns();
            
            // FID 패턴별로 처리
            for (auto it = result.angles.begin(); it != result.angles.end(); ++it) {
                QUuid fidId = it.key();
                double detectedAngle = it.value();
                
                qDebug() << QString("패턴 ID: %1, 각도: %2°").arg(fidId.toString()).arg(detectedAngle);
                
                // 해당 FID 패턴 찾기
                PatternInfo* fidPattern = nullptr;
                for (PatternInfo& pattern : updatedPatterns) {
                    if (pattern.id == fidId && pattern.type == PatternType::FID) {
                        fidPattern = &pattern;
                        break;
                    }
                }
                
                if (!fidPattern) continue;
                
                // FID의 원본 티칭 각도와 위치
                double originalFidAngle = fidPattern->angle;
                QPointF originalFidCenter = fidPattern->rect.center();
                
                // FID 매칭된 실제 위치
                QPointF detectedFidCenter = originalFidCenter;
                if (result.locations.contains(fidId)) {
                    cv::Point loc = result.locations[fidId];
                    detectedFidCenter = QPointF(loc.x, loc.y);
                }
                
                // 각도 차이 계산 (검출 각도 - 원본 각도)
                double angleDiff = detectedAngle - originalFidAngle;
                
                qDebug() << QString("★ 패턴 '%1' FID 중심 그룹 회전: 티칭각도=%2°, 검출각도=%3°, 차이=%4°")
                        .arg(fidPattern->name)
                        .arg(originalFidAngle)
                        .arg(detectedAngle)
                        .arg(angleDiff);
                
                // FID 패턴 업데이트 (위치와 각도)
                fidPattern->rect.moveCenter(detectedFidCenter);
                fidPattern->angle = detectedAngle;
                
                // 같은 그룹의 INS 패턴들을 FID 중심으로 회전 이동
                for (PatternInfo& pattern : updatedPatterns) {
                    if (pattern.type == PatternType::INS && 
                        pattern.parentId == fidId) {
                        
                        // INS의 원본 위치에서 FID까지의 상대 벡터
                        QPointF insOriginalCenter = pattern.rect.center();
                        QPointF relativeVector = insOriginalCenter - originalFidCenter;
                        
                        // 상대 벡터를 각도 차이만큼 회전
                        double radians = angleDiff * M_PI / 180.0;
                        double cosAngle = cos(radians);
                        double sinAngle = sin(radians);
                        
                        double rotatedX = relativeVector.x() * cosAngle - relativeVector.y() * sinAngle;
                        double rotatedY = relativeVector.x() * sinAngle + relativeVector.y() * cosAngle;
                        
                        // 새로운 INS 위치 = 검출된 FID 위치 + 회전된 상대 벡터
                        QPointF newInsCenter = detectedFidCenter + QPointF(rotatedX, rotatedY);
                        
                        // INS 패턴 업데이트 (위치와 각도 모두 FID 회전에 맞춰 조정)
                        pattern.rect.moveCenter(newInsCenter);
                        pattern.angle = pattern.angle + angleDiff; // INS 원본 각도 + FID 회전 차이
                        
                        qDebug() << QString("INS 패턴 '%1' FID 중심 덩어리 회전: (%2,%3) -> (%4,%5), 각도 %6° -> %7°")
                                .arg(pattern.name)
                                .arg(insOriginalCenter.x())
                                .arg(insOriginalCenter.y())
                                .arg(newInsCenter.x())
                                .arg(newInsCenter.y())
                                .arg(pattern.angle - angleDiff) // 원본 각도
                                .arg(pattern.angle);            // 새 각도
                    }
                }
            }
            
            // 업데이트된 패턴들을 CameraView에 적용
            cameraView->getPatterns() = updatedPatterns;
        }
        
        // --- AI_MATCH1 패턴이 있으면 multi_predict 호출하여 heatmap/score 병합 ---
        QList<QJsonObject> aiRects;
        QJsonArray rectsArray;
        QMap<QUuid, QRectF> aiRectsMap;  // 패턴 ID별 rect 정보 저장
        bool hasAiMatch1 = false;
        for (auto it = result.insMethodTypes.begin(); it != result.insMethodTypes.end(); ++it) {
            if (it.value() == InspectionMethod::AI_MATCH1) {
                QUuid pid = it.key();
                qDebug() << "runInspection: found AI_MATCH1 pattern" << pid.toString();
                if (result.adjustedRects.contains(pid)) {
                    QRectF rf = result.adjustedRects[pid];
                    double angle = result.parentAngles.contains(pid) ? result.parentAngles[pid] : 0.0;
                    QJsonObject rj;
                    rj["id"] = pid.toString();
                    rj["x"] = static_cast<int>(std::lround(rf.x()));
                    rj["y"] = static_cast<int>(std::lround(rf.y()));
                    rj["w"] = static_cast<int>(std::lround(rf.width()));
                    rj["h"] = static_cast<int>(std::lround(rf.height()));
                    rj["angle"] = angle;
                    rectsArray.append(rj);
                    aiRectsMap[pid] = rf;  // rect 정보 저장
                    hasAiMatch1 = true;
                    qDebug() << "runInspection: added AI rect for pattern" << pid.toString() << "rect:" << rf;
                }
            }
        }

        // 검사 결과를 CameraView에 전달 - AI_MATCH1이 없는 경우에만 먼저 호출
        if (!hasAiMatch1) {
            cameraView->updateInspectionResult(result.isPassed, result);
        }

        // AI_MATCH1이 있는 경우에만 AI 처리 수행
        if (hasAiMatch1 && !rectsArray.isEmpty()) {
            // 이미지 파일을 /deploy/results/<recipe>/input_<ts>.png 형식으로 저장
            QString recipeName = getCurrentRecipeName();
            if (recipeName.isEmpty()) {
                // Try to discover recipe from deploy/models or deploy/results
                QString detected;
                QString modelsBase = QDir::cleanPath(QDir::currentPath() + "/deploy/models");
                QDir dmodels(modelsBase);
                QStringList dirs = dmodels.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                if (!dirs.isEmpty()) {
                    detected = dirs.first();
                    qDebug() << "runInspection: detected recipe from deploy/models:" << detected;
                } else {
                    QString resultsBase = QDir::cleanPath(QDir::currentPath() + "/deploy/results");
                    QDir dres(resultsBase);
                    QStringList rdirs = dres.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                    if (!rdirs.isEmpty()) {
                        detected = rdirs.first();
                        qDebug() << "runInspection: detected recipe from deploy/results:" << detected;
                    }
                }

                if (!detected.isEmpty()) {
                    recipeName = detected;
                } else {
                    qWarning() << "runInspection: recipeName is empty and no recipe detected, falling back to 'default_recipe'";
                    recipeName = "default_recipe";
                }
            }

            // Prefer using the original simulation input image (if available) so server can use data/<recipe>/teach/<filename>
            QString sendPath; // this will be passed to multi_predict (could be a plain filename)
            if (simulationDialog) {
                QString simImg = simulationDialog->getCurrentImagePath();
                if (!simImg.isEmpty() && QFile::exists(simImg)) {
                    // Use only the filename so AITrainer will map it to app/host/data/<recipe>/teach/<filename>
                    sendPath = QFileInfo(simImg).fileName();
                }
            }

            if (sendPath.isEmpty()) {
                // No original simulation image available — cannot perform inspection
                qWarning() << "runInspection: no simulation image available; cannot perform AI inspection for recipe" << recipeName;
                return false;
            }

            AITrainer trainer;
            QJsonObject resp;

            resp = trainer.multi_predict(sendPath, recipeName, rectsArray);

            // multi_results 병합
            if (resp.contains("multi_results") && resp["multi_results"].isObject()) {
                QJsonObject mr = resp["multi_results"].toObject();
                if (mr.contains("results") && mr["results"].isArray()) {
                    QJsonArray resultsArr = mr["results"].toArray();
                    qDebug() << "runInspection: processing" << resultsArr.size() << "AI results";

                    // Preload full heatmap image (if present) once to avoid repeated filesystem checks
                    QString fullHeatmapPath = QDir::cleanPath(QDir::currentPath() + "/results/" + recipeName + "/" + QFileInfo(sendPath).baseName() + ".png");
                    cv::Mat preloadedFullHeatmap;
                    bool hasPreloadedFullHeatmap = false;
                    if (QFile::exists(fullHeatmapPath)) {
                        qDebug() << "runInspection: preloading full heatmap from:" << fullHeatmapPath;
                        preloadedFullHeatmap = cv::imread(fullHeatmapPath.toStdString(), cv::IMREAD_COLOR);
                        if (!preloadedFullHeatmap.empty()) {
                            hasPreloadedFullHeatmap = true;
                            qDebug() << "runInspection: preloaded full heatmap loaded, size:" << preloadedFullHeatmap.cols << "x" << preloadedFullHeatmap.rows;
                        } else {
                            qWarning() << "runInspection: failed to preload full heatmap image" << fullHeatmapPath;
                        }
                    }

                    for (const QJsonValue& val : resultsArr) {
                        if (!val.isObject()) continue;
                        QJsonObject item = val.toObject();
                        QString id = item.value("id").toString();
                        double score = item.value("score").toDouble(0.0);
                        QString heatmap = item.value("heatmap_file").toString();

                        QUuid pid(id);
                        result.insScores[pid] = score * 100.0;  // 0.0-1.0 → 0-100% 변환

                        // 해당 패턴의 임계값을 찾아서 불량 판별 (백분율 기준)
                        double threshold = 80.0; // 80% (0-100 범위)
                        QList<PatternInfo> patterns = cameraView->getPatterns();
                        for (const PatternInfo& p : patterns) {
                            if (p.id == pid) {
                                threshold = p.passThreshold * 100.0; // 0-1 범위를 0-100 범위로 변환
                                break;
                            }
                        }

                        bool passed = (score * 100.0) >= threshold;  // 높은 score = 정상, 낮은 score = 불량
                        result.insResults[pid] = passed;
                        result.insMethodTypes[pid] = InspectionMethod::AI_MATCH1;

                        qDebug() << "runInspection: stored AI result for pattern" << pid.toString()
                                << "score:" << result.insScores[pid] << "%"
                                << "threshold:" << threshold << "%"
                                << "passed:" << result.insResults[pid];

                        // 히트맵 이미지에서 해당 영역을 잘라서 insProcessedImages에 저장
                        QString heatmapImagePath;
                        cv::Mat heatmapImage;

                        if (!heatmap.isEmpty()) {
                            heatmapImagePath = QDir::cleanPath(QDir::currentPath() + "/results/" + recipeName + "/" + heatmap);
                            qDebug() << "runInspection: trying heatmap path:" << heatmapImagePath;
                            if (QFile::exists(heatmapImagePath)) {
                                heatmapImage = cv::imread(heatmapImagePath.toStdString(), cv::IMREAD_COLOR);
                                if (heatmapImage.empty()) {
                                    qWarning() << "runInspection: failed to load heatmap image" << heatmapImagePath;
                                }
                            } else {
                                // heatmap specified but file missing; log once
                                qWarning() << "runInspection: heatmap file specified but not found:" << heatmapImagePath;
                            }
                        }

                        // If heatmapImage not loaded from per-rect file, use preloaded full heatmap if available
                        if (heatmapImage.empty() && hasPreloadedFullHeatmap) {
                            heatmapImage = preloadedFullHeatmap;
                            qDebug() << "runInspection: using preloaded full heatmap for pattern" << pid.toString();
                        }

                        if (!heatmapImage.empty()) {
                            qDebug() << "runInspection: heatmap image ready, size:" << heatmapImage.cols << "x" << heatmapImage.rows;
                            // 이미지 경계 체크
                            int imgWidth = heatmapImage.cols;
                            int imgHeight = heatmapImage.rows;
                            QRectF rf = aiRectsMap[pid];  // 저장된 rect 정보 사용
                            int x = static_cast<int>(std::lround(rf.x()));
                            int y = static_cast<int>(std::lround(rf.y()));
                            int w = static_cast<int>(std::lround(rf.width()));
                            int h = static_cast<int>(std::lround(rf.height()));

                            x = std::max(0, std::min(x, imgWidth - 1));
                            y = std::max(0, std::min(y, imgHeight - 1));
                            w = std::max(1, std::min(w, imgWidth - x));
                            h = std::max(1, std::min(h, imgHeight - y));

                            cv::Rect roi(x, y, w, h);
                            cv::Mat cropped = heatmapImage(roi);
                            if (!cropped.empty()) {
                                result.insProcessedImages[pid] = cropped.clone();
                                qDebug() << "runInspection: cropped heatmap image for pattern" << pid.toString()
                                        << "rect:" << x << y << w << h << "cropped size:" << cropped.cols << "x" << cropped.rows;
                                // 잘라낸 히트맵을 파일로 저장
                                QString savePath = QDir::cleanPath(QDir::currentPath() + "/results/" + recipeName + "/cropped_heatmap_" + pid.toString() + ".png");
                                if (cv::imwrite(savePath.toStdString(), cropped)) {
                                    qDebug() << "runInspection: saved cropped heatmap to" << savePath;
                                } else {
                                    qWarning() << "runInspection: failed to save cropped heatmap to" << savePath;
                                }
                            }
                        } else {
                            qWarning() << "runInspection: no heatmap available for pattern" << pid.toString();
                        }
                    }
                }
            } else {
                qWarning() << "runInspection: no multi_results in response";
            }
        } else if (hasAiMatch1) {
            // AI_MATCH1이 있지만 rects가 없는 경우에도 결과 업데이트
        }
        
        // AI_MATCH1이 있는 경우 결과 업데이트 (AI 처리 완료 후)
        if (hasAiMatch1) {
            cameraView->updateInspectionResult(result.isPassed, result);
        }
        
        // 배경은 원본 이미지만 설정 (검사 결과 오버레이 없이)
        QImage originalImage = InsProcessor::matToQImage(frame);
        if (!originalImage.isNull()) {
            QPixmap pixmap = QPixmap::fromImage(originalImage);
            cameraView->setBackgroundPixmap(pixmap);
            cameraView->update();
        }
        
        return result.isPassed;
        
    } catch (...) {
        return false;
    }
}

// 라이브 모드로 복귀하는 헬퍼 함수 - 버튼 상태 고려
void TeachingWidget::resumeToLiveMode() {
    // **UI 스레드에서 실행되는지 확인**
    if (QThread::currentThread() != QApplication::instance()->thread()) {
        // UI 스레드로 호출 예약
        QMetaObject::invokeMethod(this, "resumeToLiveMode", Qt::QueuedConnection);
        return;
    }
    
    // **중복 호출 방지: 이미 라이브 모드라면 리턴**
    static bool isResuming = false;
    if (isResuming) {
        return;
    }
    isResuming = true;
    
    try {
        // **1. RUN/STOP 버튼 상태 확인 및 강제로 STOP 상태로 만들기**
        if (runStopButton && runStopButton->isChecked()) {
            // 버튼이 RUN 상태(검사 중)라면 STOP 상태로 변경
            runStopButton->blockSignals(true);
            runStopButton->setChecked(false);
            runStopButton->setText("RUN");
            runStopButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));
            runStopButton->blockSignals(false);
        }
        
        // **2. 시뮬레이션 모드 해제**
        if (simulationMode) {
            simulationMode = false;
            currentSimulationImage = cv::Mat(); // 시뮬레이션 이미지 클리어
            qDebug() << "[라이브 모드 복귀] 시뮬레이션 모드 해제";
            
        }
        
        // **2.5. 카메라가 꺼져있으면 다시 시작**
        if (startCameraButton && !startCameraButton->isChecked()) {
            qDebug() << "[라이브 모드 복귀] 카메라가 꺼져있어서 다시 시작합니다";
            startCamera();
        }
        
        // **3. 검사 모드 해제**
        if (cameraView) {
            cameraView->setInspectionMode(false);
        }
        
        // **4. 패턴들을 원래 티칭 상태로 복원**
        if (!originalPatternBackup.isEmpty() && cameraView) {
            QList<PatternInfo> currentPatterns = cameraView->getPatterns();
            
            for (int i = 0; i < currentPatterns.size(); ++i) {
                QUuid patternId = currentPatterns[i].id;
                if (originalPatternBackup.contains(patternId)) {
                    // 검사 중 변경된 각도와 위치를 원본으로 복원
                    PatternInfo& currentPattern = currentPatterns[i];
                    const PatternInfo& originalPattern = originalPatternBackup[patternId];
                    
                    currentPattern.angle = originalPattern.angle;
                    currentPattern.rect = originalPattern.rect;
                }
            }
            
            // CameraView에 복원된 패턴들 적용
            cameraView->getPatterns() = currentPatterns;
            
            // 백업 정보 초기화
            originalPatternBackup.clear();
        }
        
        // **5. UI 업데이트 스레드만 재개 (카메라 스레드는 계속 실행 중)**
        if (uiUpdateThread) {
            if (uiUpdateThread->isRunning()) {
                uiUpdateThread->setPaused(false);
            } else if (uiUpdateThread->isFinished()) {
                uiUpdateThread->start(QThread::NormalPriority);
            }
        }
        
        if (cameraIndex >= 0 && cameraIndex < static_cast<int>(cameraFrames.size())) {
            cameraFrames[cameraIndex] = cv::Mat();
        }
        
        // **6. UI 이벤트 처리**
        QApplication::processEvents();
        
        // **7. 강제로 화면 갱신 및 카메라 프레임 업데이트**
        if (cameraView) {
            cameraView->update();
            // 라이브 모드로 전환 시 카메라 프레임 강제 업데이트
            updateCameraFrame();
        }
        
    } catch (const std::exception& e) {
        // 최소한의 복구
        if (cameraView) {
            cameraView->setInspectionMode(false);
        }
        
        // 버튼 상태도 복구
        if (runStopButton && runStopButton->isChecked()) {
            runStopButton->blockSignals(true);
            runStopButton->setChecked(false);
            runStopButton->setText("RUN");
            runStopButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));
            runStopButton->blockSignals(false);
        }
    } catch (...) {
        // 최소한의 복구
        if (cameraView) {
            cameraView->setInspectionMode(false);
        }
        
        // 버튼 상태도 복구
        if (runStopButton && runStopButton->isChecked()) {
            runStopButton->blockSignals(true);
            runStopButton->setChecked(false);
            runStopButton->setText("RUN");
            runStopButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));
            runStopButton->blockSignals(false);
        }
    }
    
    // **플래그 해제**
    isResuming = false;
}

void TeachingWidget::switchToTestMode() {
    if (logViewer) {
        logViewer->show();
    }
    
    cameraView->setInspectionMode(true);

    // 카메라가 열려있는지 확인
    cv::Mat testFrame;
    bool gotFrame = false;
    
#ifdef USE_SPINNAKER
    // Spinnaker 카메라 확인
    if (m_useSpinnaker && cameraIndex >= 0 && cameraIndex < cameraInfos.size() && 
        cameraInfos[cameraIndex].uniqueId.startsWith("SPINNAKER_")) {
        
        if (!m_spinCameras.empty() && cameraIndex < static_cast<int>(m_spinCameras.size())) {
            // Spinnaker 카메라에서 프레임 가져오기
            testFrame = grabFrameFromSpinnakerCamera(m_spinCameras[cameraIndex]);
            if (!testFrame.empty()) {
                gotFrame = true;
                // Spinnaker는 RGB로 들어오므로 BGR로 변환
                cv::cvtColor(testFrame, testFrame, cv::COLOR_RGB2BGR);
            }
        }
    } else 
#endif
    // **OpenCV 카메라 처리 - camera 포인터 대신 cameraInfos 사용**
    if (cameraIndex >= 0 && cameraIndex < cameraInfos.size() && 
        cameraInfos[cameraIndex].capture && cameraInfos[cameraIndex].capture->isOpened()) {
        
        // OpenCV 카메라에서 프레임 가져오기
        if (cameraInfos[cameraIndex].capture->read(testFrame)) {
            gotFrame = true;
        }
    }
    
    // 프레임을 가져왔거나 기존 프레임이 있는 경우 사용
    if (gotFrame) {
        // **벡터에 저장**
        if (cameraIndex >= static_cast<int>(cameraFrames.size())) {
            cameraFrames.resize(cameraIndex + 1);
        }
        cameraFrames[cameraIndex] = testFrame.clone();
        
        cv::Mat displayFrame;
        cv::cvtColor(cameraFrames[cameraIndex], displayFrame, cv::COLOR_BGR2RGB);
        
        QImage image(displayFrame.data, displayFrame.cols, displayFrame.rows, 
                  displayFrame.step, QImage::Format_RGB888);
        QPixmap pixmap = QPixmap::fromImage(image);
        cameraView->setBackgroundPixmap(pixmap);
    }
    else if (cameraIndex >= 0 && cameraIndex < static_cast<int>(cameraFrames.size()) && 
             !cameraFrames[cameraIndex].empty()) {
        // 기존 프레임 사용
        cv::Mat displayFrame;
        cv::cvtColor(cameraFrames[cameraIndex], displayFrame, cv::COLOR_BGR2RGB);
        
        QImage image(displayFrame.data, displayFrame.cols, displayFrame.rows, 
                  displayFrame.step, QImage::Format_RGB888);
        QPixmap pixmap = QPixmap::fromImage(image);
        cameraView->setBackgroundPixmap(pixmap);
    }
}

void TeachingWidget::switchToRecipeMode() {
    cameraView->setInspectionMode(false);
    
    if (uiUpdateThread && uiUpdateThread->isRunning()) {
        uiUpdateThread->setPaused(false);
    }

    // --- 실시간 필터 적용: 카메라뷰에 필터 적용된 이미지를 표시 ---
    if (cameraIndex >= 0 && cameraIndex < static_cast<int>(cameraFrames.size()) && 
        !cameraFrames[cameraIndex].empty()) {
        
        cv::Mat filteredFrame = cameraFrames[cameraIndex].clone();
        cameraView->applyFiltersToImage(filteredFrame);
        cv::Mat rgbFrame;
        cv::cvtColor(filteredFrame, rgbFrame, cv::COLOR_BGR2RGB);
        QImage image(rgbFrame.data, rgbFrame.cols, rgbFrame.rows, rgbFrame.step, QImage::Format_RGB888);
        QPixmap pixmap = QPixmap::fromImage(image);
        cameraView->setBackgroundPixmap(pixmap);
    }
}

void TeachingWidget::finishCalibration(const QRect& calibRect, double realLength) {
    // 현재 카메라 UUID 확인
    if (cameraIndex < 0 || cameraIndex >= cameraInfos.size()) {
        UIColors::showWarning(this, TR("CALIBRATION_ERROR"), 
                             TR("INVALID_CAMERA_INDEX"));
        cameraView->setCalibrationMode(false);
        return;
    }
    
    QString currentCameraUuid = cameraInfos[cameraIndex].uniqueId;
    
    // 캘리브레이션 정보 계산 및 저장
    CalibrationInfo calibInfo;
    calibInfo.isCalibrated = true;
    calibInfo.calibrationRect = calibRect;
    calibInfo.realWorldLength = realLength;
    
    // 픽셀당 밀리미터 비율 계산
    double pixelLength = sqrt(calibRect.width() * calibRect.width() + calibRect.height() * calibRect.height());
    calibInfo.pixelToMmRatio = realLength / pixelLength;
    
    // 카메라별 캘리브레이션 맵에 저장
    cameraCalibrationMap[currentCameraUuid] = calibInfo;
    
    // 캘리브레이션 정보 설정 (현재 활성 카메라에만 적용)
    cameraView->setCalibrationInfo(calibInfo);
    
    // 캘리브레이션 모드 종료
    cameraView->setCalibrationMode(false);
    
    // 사용자에게 완료 메시지 표시 (mm 단위 표시)
    UIColors::showInformation(this, 
                             TR("CALIBRATION_COMPLETE_TITLE"), 
                             QString("%1\n%2: %3\n%4: %5 mm = %6 px\n%7: %8 mm/px")
                                 .arg(TR("CALIBRATION_COMPLETE_MSG"))
                                 .arg(TR("CAMERA"))
                                 .arg(cameraInfos[cameraIndex].name)
                                 .arg(TR("LENGTH"))
                                 .arg(realLength, 0, 'f', 1)
                                 .arg(pixelLength, 0, 'f', 1)
                                 .arg(TR("RATIO"))
                                 .arg(calibInfo.pixelToMmRatio, 0, 'f', 6));
}

void TeachingWidget::updateAllPatternTemplateImages() {
    if (!cameraView) {
        return;
    }
    
    
    // 현재 이미지 가져오기 (패턴이 그려지기 전의 원본 이미지)
    cv::Mat currentImage;
    if (simulationMode) {
        if (currentSimulationImage.empty()) {
            return;
        }
        currentImage = currentSimulationImage.clone();
    } else {
        // CameraView의 backgroundPixmap에서 원본 이미지 가져오기
        if (cameraView) {
            QPixmap bgPixmap = cameraView->getBackgroundPixmap();
            if (!bgPixmap.isNull()) {
                QImage qimg = bgPixmap.toImage().convertToFormat(QImage::Format_RGB888);
                cv::Mat tempMat(qimg.height(), qimg.width(), CV_8UC3, (void*)qimg.constBits(), qimg.bytesPerLine());
                cv::cvtColor(tempMat, currentImage, cv::COLOR_RGB2BGR);
            } else {
                currentImage = getCurrentFrame();
            }
        } else {
            currentImage = getCurrentFrame();
            qDebug() << QString("검사 실행 - 현재 카메라 인덱스: %1, 전체 카메라 수: %2")
                        .arg(cameraIndex).arg(cameraFrames.size());
            if (cameraIndex >= 0 && cameraIndex < cameraFrames.size()) {
                qDebug() << QString("검사 실행 - 현재 카메라 영상 크기: %1x%2")
                            .arg(currentImage.cols).arg(currentImage.rows);
            }
        }
        if (currentImage.empty()) {
            return;
        }
    }
    
    // 모든 패턴 가져오기
    QList<PatternInfo> patterns = cameraView->getPatterns();
    
    for (PatternInfo pattern : patterns) {  // 복사본으로 작업
        // FID와 INS 패턴만 템플릿 이미지가 필요함
        if (pattern.type == PatternType::FID || pattern.type == PatternType::INS) {
            // **중요**: 이미 템플릿 이미지가 있으면 재생성하지 않음 (중복 마스킹 방지)
            if (!pattern.templateImage.isNull()) {
                printf("[TeachingWidget] 패턴 '%s': 이미 템플릿 이미지가 있으므로 재생성하지 않음\n", 
                       pattern.name.toStdString().c_str());
                fflush(stdout);
                continue;
            }
            
            try {
                // FID/INS 템플릿 이미지: 가로세로 최대 크기 정사각형으로 추출하고 패턴 영역 외부만 마스킹
                cv::Mat templateRegion;
                cv::Point2f center(pattern.rect.x() + pattern.rect.width()/2.0f, 
                                 pattern.rect.y() + pattern.rect.height()/2.0f);
                
                // 회전각에 따른 실제 필요한 사각형 크기 계산
                double width = pattern.rect.width();
                double height = pattern.rect.height();
                double angleRad = pattern.angle * M_PI / 180.0;
                
                // 회전된 사각형의 경계 박스 크기 계산
                double rotatedWidth = std::abs(width * cos(angleRad)) + std::abs(height * sin(angleRad));
                double rotatedHeight = std::abs(width * sin(angleRad)) + std::abs(height * cos(angleRad));
                
                // 최종 정사각형 크기 (회전된 영역이 완전히 들어갈 수 있는 크기)
                int squareSize = static_cast<int>(std::max(rotatedWidth, rotatedHeight)) + 10; // 약간의 여유분
                
                // 정사각형 ROI 영역 계산 (중심점 기준)
                int halfSize = squareSize / 2;
                cv::Rect squareRoi(
                    static_cast<int>(center.x) - halfSize,
                    static_cast<int>(center.y) - halfSize,
                    squareSize,
                    squareSize
                );
                
                // 이미지 경계와 교집합 구하기
                cv::Rect imageBounds(0, 0, currentImage.cols, currentImage.rows);
                cv::Rect validRoi = squareRoi & imageBounds;
                
                if (validRoi.width > 0 && validRoi.height > 0) {
                    // 정사각형 결과 이미지 생성 (검은색 배경)
                    templateRegion = cv::Mat::zeros(squareSize, squareSize, currentImage.type());
                    
                    // 유효한 영역만 복사
                    int offsetX = validRoi.x - squareRoi.x;
                    int offsetY = validRoi.y - squareRoi.y;
                    
                    cv::Mat validImage = currentImage(validRoi);
                    cv::Rect resultRect(offsetX, offsetY, validRoi.width, validRoi.height);
                    validImage.copyTo(templateRegion(resultRect));
                    
                    // INS 패턴의 경우 마스킹 적용 (패턴 영역만 보이도록)
                    if (pattern.type == PatternType::INS) {
                        cv::Mat mask = cv::Mat::zeros(squareSize, squareSize, CV_8UC1);
                        
                        // 정사각형 중심을 기준으로 패턴 영역 계산
                        cv::Point2f patternCenter(squareSize / 2.0f, squareSize / 2.0f);
                        cv::Size2f patternSize(pattern.rect.width(), pattern.rect.height());
                        
                        if (std::abs(pattern.angle) > 0.1) {
                            // 회전된 패턴의 경우: 회전된 사각형 마스크
                            cv::Point2f vertices[4];
                            cv::RotatedRect rotatedRect(patternCenter, patternSize, pattern.angle);
                            rotatedRect.points(vertices);
                            
                            std::vector<cv::Point> points;
                            for (int i = 0; i < 4; i++) {
                                points.push_back(cv::Point(static_cast<int>(vertices[i].x), 
                                                         static_cast<int>(vertices[i].y)));
                            }
                            
                            cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{points}, cv::Scalar(255));
                        } else {
                            // 회전 없는 경우: 일반 사각형 마스크
                            cv::Rect patternRect(
                                static_cast<int>(patternCenter.x - patternSize.width / 2),
                                static_cast<int>(patternCenter.y - patternSize.height / 2),
                                static_cast<int>(patternSize.width),
                                static_cast<int>(patternSize.height)
                            );
                            cv::rectangle(mask, patternRect, cv::Scalar(255), -1);
                        }
                        
                        // 마스크 반전: 패턴 영역 외부를 검은색으로 설정
                        cv::Mat invertedMask;
                        cv::bitwise_not(mask, invertedMask);
                        
                        // 패턴 영역 외부를 검은색으로 마스킹
                        templateRegion.setTo(cv::Scalar(0, 0, 0), invertedMask);
                    } else {
                        // FID 패턴의 경우 기존 방식 (패턴 영역만 추출)
                        cv::Point2f patternCenter(squareSize / 2.0f, squareSize / 2.0f);
                        cv::Size2f patternSize(pattern.rect.width(), pattern.rect.height());
                        
                        // 최소 크기 보장
                        if (patternSize.width < 10) patternSize.width = 10;
                        if (patternSize.height < 10) patternSize.height = 10;
                        
                        // 패턴 영역만 잘라내기
                        cv::Rect extractRect;
                        if (std::abs(pattern.angle) < 0.1) {
                            // 회전 없는 경우: 단순 사각형 추출
                            extractRect = cv::Rect(
                                static_cast<int>(patternCenter.x - patternSize.width / 2),
                                static_cast<int>(patternCenter.y - patternSize.height / 2),
                                static_cast<int>(patternSize.width),
                                static_cast<int>(patternSize.height)
                            );
                            
                            // 경계 체크
                            extractRect = extractRect & cv::Rect(0, 0, templateRegion.cols, templateRegion.rows);
                            
                            if (extractRect.width > 0 && extractRect.height > 0) {
                                templateRegion = templateRegion(extractRect).clone();
                            }
                        }
                        // 회전된 경우는 복잡하므로 일단 기존 방식 유지
                    }
                    
                    // 패턴의 자체 필터 적용
                    printf("[TeachingWidget] 패턴 '%s'에 %d개 필터 적용\n", pattern.name.toStdString().c_str(), pattern.filters.size());
                    fflush(stdout);
                    for (const FilterInfo& filter : pattern.filters) {
                        if (filter.enabled) {
                            cv::Mat filtered;
                            ImageProcessor processor;
                            processor.applyFilter(templateRegion, filtered, filter);
                            if (!filtered.empty()) {
                                templateRegion = filtered.clone();
                                printf("[TeachingWidget] 필터 타입 %d 적용 완료\n", filter.type);
                                fflush(stdout);
                            }
                        }
                    }
                }
                
                if (!templateRegion.empty()) {
                    // OpenCV Mat을 QImage로 변환
                    QImage templateImage;
                    if (templateRegion.channels() == 3) {
                        cv::Mat rgbImage;
                        cv::cvtColor(templateRegion, rgbImage, cv::COLOR_BGR2RGB);
                        templateImage = QImage(rgbImage.data, rgbImage.cols, rgbImage.rows, 
                                             rgbImage.step, QImage::Format_RGB888).copy();
                    } else {
                        templateImage = QImage(templateRegion.data, templateRegion.cols, templateRegion.rows, 
                                             templateRegion.step, QImage::Format_Grayscale8).copy();
                    }
                    
                    // 패턴의 템플릿 이미지 갱신
                    pattern.templateImage = templateImage;
                    cameraView->updatePatternById(pattern.id, pattern);
                }
            } catch (const std::exception& e) {
                // 에러 처리 (무시)
            }
        }
    }
    cameraView->update(); // 화면 갱신 
    
    // 필터 설정 중이 아닐 때만 프로퍼티 패널의 템플릿 이미지도 업데이트
    if (!isFilterAdjusting) {
        QTreeWidgetItem* currentItem = patternTree->currentItem();
        if (currentItem) {
            QUuid selectedPatternId = getPatternIdFromItem(currentItem);
            PatternInfo* selectedPattern = cameraView->getPatternById(selectedPatternId);
            if (selectedPattern && (selectedPattern->type == PatternType::FID || selectedPattern->type == PatternType::INS)) {
                updatePropertyPanel(selectedPattern, nullptr, selectedPatternId, -1);
            }
        }
    }
}

void TeachingWidget::saveRecipe() {
    qDebug() << QString("saveRecipe() 호출됨 - 현재 레시피 이름: '%1', 시뮬레이션 모드: %2").arg(currentRecipeName).arg(simulationMode);
    
    // 현재 레시피 이름이 있으면 개별 파일로 저장, 없으면 자동 이름 생성해서 저장
    if (currentRecipeName.isEmpty()) {
        qDebug() << "currentRecipeName이 비어있어 새로운 타임스탬프 이름을 생성합니다.";
        // 자동으로 타임스탬프 이름 생성 (사용자에게 묻지 않음)
        QDateTime now = QDateTime::currentDateTime();
        currentRecipeName = now.toString("yyyyMMdd_HHmmss_zzz");
        qDebug() << QString("새로 생성된 레시피 이름: %1").arg(currentRecipeName);
    } else {
        qDebug() << QString("기존 레시피 '%1'에 덮어쓰기 저장합니다.").arg(currentRecipeName);
    }
    
    // 현재 편집 모드 저장 (저장 후 복원하기 위해)
    CameraView::EditMode currentMode = cameraView->getEditMode();
    bool currentModeToggleState = modeToggleButton->isChecked();
    
    // 모드별 이미지 저장 처리
    if (!simulationMode) {
        // 라이브 모드: 모든 연결된 카메라의 현재 프레임을 저장
        qDebug() << "라이브 모드: 현재 프레임을 레시피에 저장합니다.";
        if (!cameraInfos.isEmpty()) {
            // 모든 연결된 카메라의 프레임을 저장
            for (int i = 0; i < cameraInfos.size(); i++) {
                if (cameraInfos[i].isConnected && i < cameraFrames.size() && !cameraFrames[i].empty()) {
                    // 각 카메라의 현재 프레임을 해당 카메라 UUID로 저장
                    QString cameraUuid = cameraInfos[i].uniqueId;
                    
                    // 레시피 teach 폴더 생성
                    QString recipeDir = QString("recipes/%1").arg(currentRecipeName);
                    QString teachDir = QString("%1/teach").arg(recipeDir);
                    QDir().mkpath(teachDir);
                    
                    // 해당 카메라의 기존 이미지들 삭제 (1장만 유지하기 위해)
                    QDir dir(teachDir);
                    QStringList filters;
                    filters << QString("%1.jpg").arg(cameraUuid) << QString("%1.png").arg(cameraUuid) 
                            << QString("%1.bmp").arg(cameraUuid) << QString("%1.jpeg").arg(cameraUuid);
                    QStringList existingFiles = dir.entryList(filters, QDir::Files);
                    for (const QString& file : existingFiles) {
                        QString filePath = QString("%1/%2").arg(teachDir).arg(file);
                        if (QFile::remove(filePath)) {
                            qDebug() << QString("기존 카메라 %1 이미지 삭제: %2").arg(cameraUuid).arg(filePath);
                        }
                    }
                    
                    // 카메라 UUID로만 파일명 생성 (타임스탬프 제거)
                    QString fileName = QString("%1.jpg").arg(cameraUuid);
                    QString fullPath = QString("%1/%2").arg(teachDir).arg(fileName);
                    
                    // 이미지 저장
                    if (cv::imwrite(fullPath.toStdString(), cameraFrames[i])) {
                        qDebug() << QString("카메라 %1 프레임을 저장했습니다: %2").arg(cameraUuid).arg(fullPath);
                    } else {
                        qDebug() << QString("카메라 %1 프레임 저장 실패: %2").arg(cameraUuid).arg(fullPath);
                    }
                }
            }
        }
    } else {
        // 시뮬레이션 모드: 임시 파일들을 현재 카메라명으로 변경하고 정리
        qDebug() << "시뮬레이션 모드: 임시 파일들을 현재 카메라명으로 변경하고 정리합니다.";
        saveSimulationTeachingImage();
    }
    
    // 개별 레시피 파일로 저장
    RecipeManager manager;
    
    // 레시피 파일 경로 생성
    QString recipeFileName = QDir(manager.getRecipesDirectory()).absoluteFilePath(QString("%1/%1.xml").arg(currentRecipeName));
    
    // 빈 시뮬레이션 이미지 패스와 빈 캘리브레이션 맵 (필요시 나중에 추가)
    QStringList simulationImagePaths;
    QMap<QString, CalibrationInfo> calibrationMap;
    
    // 기존 saveRecipe 함수 사용
    if (manager.saveRecipe(recipeFileName, cameraInfos, cameraIndex, calibrationMap, cameraView, simulationImagePaths)) {
        hasUnsavedChanges = false;
        
        // 최근 사용한 레시피를 ConfigManager에 저장
        ConfigManager::instance()->setLastRecipePath(currentRecipeName);
        ConfigManager::instance()->saveConfig();
        qDebug() << QString("최근 레시피 저장: %1").arg(currentRecipeName);
        
        UIColors::showInformation(this, "레시피 저장", 
            QString("'%1' 레시피가 성공적으로 저장되었습니다.").arg(currentRecipeName));
    } else {
        QMessageBox::critical(this, "레시피 저장 실패", 
            QString("레시피 저장에 실패했습니다:\n%1").arg(manager.getLastError()));
    }
    
    // 저장 전 모드 복원
    cameraView->setEditMode(currentMode);
    modeToggleButton->setChecked(currentModeToggleState);
    
    // 버튼 텍스트와 스타일도 복원
    if (currentMode == CameraView::EditMode::Draw) {
        modeToggleButton->setText("DRAW");
        modeToggleButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_MOVE_COLOR, UIColors::BTN_DRAW_COLOR, true));
    } else {
        modeToggleButton->setText("MOVE");
        modeToggleButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_DRAW_COLOR, UIColors::BTN_MOVE_COLOR, false));
    }
}


bool TeachingWidget::loadRecipe(const QString &fileName) {
    if (fileName.isEmpty()) {
        // 파일명이 없으면 사용 가능한 첫 번째 레시피 로드
        RecipeManager recipeManager;
        QStringList availableRecipes = recipeManager.getAvailableRecipes();
        if (availableRecipes.isEmpty()) {
            return false;
        }
        onRecipeSelected(availableRecipes.first());
        return true;
    }
    
    // 더 이상 직접 파일 로드를 지원하지 않음 - 개별 레시피 시스템만 사용
    qWarning() << "직접 파일 로드는 지원되지 않습니다. 레시피 관리 시스템을 사용하세요.";
    return false;
}

void TeachingWidget::loadLastRecipe() {
    // ConfigManager에서 최근 사용한 레시피 경로 가져오기
    QString lastRecipePath = ConfigManager::instance()->getLastRecipePath();
    
    if (lastRecipePath.isEmpty()) {
        qDebug() << "최근 사용한 레시피가 없습니다.";
        return;
    }
    
    // 레시피가 실제로 존재하는지 확인
    RecipeManager recipeManager;
    QStringList availableRecipes = recipeManager.getAvailableRecipes();
    
    if (!availableRecipes.contains(lastRecipePath)) {
        qDebug() << QString("최근 레시피 '%1'을 찾을 수 없습니다. 사용 가능한 레시피: %2")
                    .arg(lastRecipePath).arg(availableRecipes.join(", "));
        
        // 최근 레시피가 없으면 첫 번째 사용 가능한 레시피 로드
        if (!availableRecipes.isEmpty()) {
            qDebug() << QString("대신 첫 번째 레시피 '%1'을 로드합니다.").arg(availableRecipes.first());
            onRecipeSelected(availableRecipes.first());
        }
        return;
    }
    
    // 최근 레시피 로드
    qDebug() << QString("최근 사용한 레시피 '%1'을 자동 로드합니다.").arg(lastRecipePath);
    onRecipeSelected(lastRecipePath);
}

QVector<CameraInfo> TeachingWidget::getCameraInfos() const {
    QMutexLocker locker(&cameraInfosMutex);
    return cameraInfos;
}

CameraInfo TeachingWidget::getCameraInfo(int index) const {
    QMutexLocker locker(&cameraInfosMutex);
    if (index >= 0 && index < cameraInfos.size()) {
        return cameraInfos[index];
    }
    return CameraInfo();
}

bool TeachingWidget::setCameraInfo(int index, const CameraInfo& info) {
    QMutexLocker locker(&cameraInfosMutex);
    if (index >= 0 && index < cameraInfos.size()) {
        cameraInfos[index] = info;
        return true;
    }
    return false;
}

int TeachingWidget::getCameraInfosCount() const {
    QMutexLocker locker(&cameraInfosMutex);
    return cameraInfos.size();
}

void TeachingWidget::clearCameraInfos() {
    QMutexLocker locker(&cameraInfosMutex);
    for (auto& info : cameraInfos) {
        if (info.capture) {
            info.capture->release();
            delete info.capture;
            info.capture = nullptr;
        }
    }
    cameraInfos.clear();
}

void TeachingWidget::appendCameraInfo(const CameraInfo& info) {
    QMutexLocker locker(&cameraInfosMutex);
    cameraInfos.append(info);
}

void TeachingWidget::removeCameraInfo(int index) {
    QMutexLocker locker(&cameraInfosMutex);
    if (index >= 0 && index < cameraInfos.size()) {
        if (cameraInfos[index].capture) {
            cameraInfos[index].capture->release();
            delete cameraInfos[index].capture;
            cameraInfos[index].capture = nullptr;
        }
        cameraInfos.removeAt(index);
    }
}

bool TeachingWidget::isValidCameraIndex(int index) const {
    QMutexLocker locker(&cameraInfosMutex);
    return (index >= 0 && index < cameraInfos.size());
}

// 연결된 모든 카메라의 UUID 목록 반환
QStringList TeachingWidget::getConnectedCameraUuids() const {
    QMutexLocker locker(&cameraInfosMutex);
    QStringList uuids;
    
    for (const CameraInfo& cameraInfo : cameraInfos) {
        if (cameraInfo.isConnected && !cameraInfo.uniqueId.isEmpty()) {
            uuids.append(cameraInfo.uniqueId);
        }
    }
    
    return uuids;
}

#ifdef USE_SPINNAKER
// Spinnaker SDK 초기화
bool TeachingWidget::initSpinnakerSDK()
{
    try {
        // Spinnaker 시스템 객체 생성 - 네임스페이스 추가
        m_spinSystem = Spinnaker::System::GetInstance();
        
        // 라이브러리 버전 출력 - 네임스페이스 추가
        const Spinnaker::LibraryVersion spinnakerLibraryVersion = m_spinSystem->GetLibraryVersion();
                 << spinnakerLibraryVersion.major << "."
                 << spinnakerLibraryVersion.minor << "."
                 << spinnakerLibraryVersion.type << "."
                 << spinnakerLibraryVersion.build;
        
        return true;
    }
    catch (Spinnaker::Exception& e) {
        return false;
    }
}

// Spinnaker SDK 해제
void TeachingWidget::releaseSpinnakerSDK()
{
    try {
        // 모든 카메라 참조 해제
        m_spinCameras.clear();
        
        if (m_spinCamList.GetSize() > 0) {
            m_spinCamList.Clear();
        }
        
        // 시스템 인스턴스 해제
        if (m_spinSystem) {
            m_spinSystem->ReleaseInstance();
            m_spinSystem = nullptr;
        }
        
    }
    catch (Spinnaker::Exception& e) {
    }
}

bool TeachingWidget::connectSpinnakerCamera(int index, CameraInfo& info)
{
    try {
        // 디버그 로그 추가
        
        if (index >= static_cast<int>(m_spinCamList.GetSize())) {
            return false;
        }
        
        // 카메라 선택
        Spinnaker::CameraPtr camera = m_spinCamList.GetByIndex(index);
        if (!camera.IsValid()) {
            return false;
        }
        
        // 카메라 세부 정보 로깅 추가
        try {
            Spinnaker::GenApi::INodeMap& nodeMapTLDevice = camera->GetTLDeviceNodeMap();
            
            Spinnaker::GenApi::CStringPtr ptrDeviceVendorName = nodeMapTLDevice.GetNode("DeviceVendorName");
            if (Spinnaker::GenApi::IsReadable(ptrDeviceVendorName)) {
            }
            
            Spinnaker::GenApi::CStringPtr ptrDeviceModelName = nodeMapTLDevice.GetNode("DeviceModelName");
            if (Spinnaker::GenApi::IsReadable(ptrDeviceModelName)) {
            }
            
            Spinnaker::GenApi::CStringPtr ptrDeviceVersion = nodeMapTLDevice.GetNode("DeviceVersion");
            if (Spinnaker::GenApi::IsReadable(ptrDeviceVersion)) {
            }
        }
        catch (Spinnaker::Exception& e) {
        }
        
        // 카메라가 이미 초기화되었는지 확인
        if (camera->IsInitialized()) {
            try {
                if (camera->IsStreaming()) {
                    camera->EndAcquisition();
                }
            }
            catch (Spinnaker::Exception& e) {
            }
            
            try {
                camera->DeInit();
            }
            catch (Spinnaker::Exception& e) {
                return false;
            }
        }
        
        // 초기화 시도 횟수 추가
        const int maxRetries = 3;
        bool initSuccess = false;
        
        for (int retry = 0; retry < maxRetries && !initSuccess; retry++) {
            try {
                camera->Init();
                initSuccess = true;
            }
            catch (Spinnaker::Exception& e) {
                
                if (retry < maxRetries - 1) {
                    // 잠시 대기
                    QThread::msleep(500);
                } else {
                    return false;
                }
            }
        }
        
        if (!initSuccess) {
            return false;
        }
        
        // 카메라 정보 가져오기
        try {
            Spinnaker::GenApi::INodeMap& nodeMapTLDevice = camera->GetTLDeviceNodeMap();
            
            // 시리얼 번호
            Spinnaker::GenApi::CStringPtr ptrDeviceSerialNumber = nodeMapTLDevice.GetNode("DeviceSerialNumber");
            if (Spinnaker::GenApi::IsReadable(ptrDeviceSerialNumber)) {
                info.serialNumber = QString::fromStdString(ptrDeviceSerialNumber->GetValue().c_str());
            }
            
            // 모델 이름
            Spinnaker::GenApi::CStringPtr ptrDeviceModelName = nodeMapTLDevice.GetNode("DeviceModelName");
            if (Spinnaker::GenApi::IsReadable(ptrDeviceModelName)) {
                info.name = QString::fromStdString(ptrDeviceModelName->GetValue().c_str());
            }
            
            // 벤더 이름
            Spinnaker::GenApi::CStringPtr ptrDeviceVendorName = nodeMapTLDevice.GetNode("DeviceVendorName");
            if (Spinnaker::GenApi::IsReadable(ptrDeviceVendorName)) {
                info.vendorId = QString::fromStdString(ptrDeviceVendorName->GetValue().c_str());
            }
        }
        catch (Spinnaker::Exception& e) {
            // 정보를 가져오지 못했더라도 계속 진행
        }
        
        // 고유 ID 생성
        info.uniqueId = "SPINNAKER_" + info.serialNumber;
        if (info.uniqueId.isEmpty()) {
            info.uniqueId = QString("SPINNAKER_%1").arg(index);
        }
        
        // 카메라 저장
        m_spinCameras.push_back(camera);
        
        // 카메라 설정 구성
        try {
            // 버퍼 핸들링 모드 설정 (최신 이미지만 유지)
            Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
            Spinnaker::GenApi::CEnumerationPtr ptrBufferHandlingMode = nodeMap.GetNode("StreamBufferHandlingMode");
            if (Spinnaker::GenApi::IsReadable(ptrBufferHandlingMode) && 
                Spinnaker::GenApi::IsWritable(ptrBufferHandlingMode)) {
                
                Spinnaker::GenApi::CEnumEntryPtr ptrNewestOnly = ptrBufferHandlingMode->GetEntryByName("NewestOnly");
                if (Spinnaker::GenApi::IsReadable(ptrNewestOnly)) {
                    ptrBufferHandlingMode->SetIntValue(ptrNewestOnly->GetValue());
                }
            }
            
            // StreamBufferCountMode 설정
            Spinnaker::GenApi::CEnumerationPtr ptrBufferCountMode = nodeMap.GetNode("StreamBufferCountMode");
            if (Spinnaker::GenApi::IsReadable(ptrBufferCountMode) && 
                Spinnaker::GenApi::IsWritable(ptrBufferCountMode)) {
                
                Spinnaker::GenApi::CEnumEntryPtr ptrManual = ptrBufferCountMode->GetEntryByName("Manual");
                if (Spinnaker::GenApi::IsReadable(ptrManual)) {
                    ptrBufferCountMode->SetIntValue(ptrManual->GetValue());
                    
                    // StreamBufferCount 설정 (작은 값으로)
                    Spinnaker::GenApi::CIntegerPtr ptrBufferCount = nodeMap.GetNode("StreamBufferCount");
                    if (Spinnaker::GenApi::IsReadable(ptrBufferCount) && 
                        Spinnaker::GenApi::IsWritable(ptrBufferCount)) {
                        ptrBufferCount->SetValue(3); // 버퍼 크기를 3으로 설정
                    }
                }
            }
            
            // 트리거 모드 끄기
            Spinnaker::GenApi::CEnumerationPtr ptrTriggerMode = nodeMap.GetNode("TriggerMode");
            if (Spinnaker::GenApi::IsWritable(ptrTriggerMode)) {
                Spinnaker::GenApi::CEnumEntryPtr ptrTriggerModeOff = ptrTriggerMode->GetEntryByName("Off");
                if (Spinnaker::GenApi::IsReadable(ptrTriggerModeOff)) {
                    ptrTriggerMode->SetIntValue(ptrTriggerModeOff->GetValue());
                }
            }
            
            // 연속 획득 모드 설정
            Spinnaker::GenApi::CEnumerationPtr ptrAcquisitionMode = nodeMap.GetNode("AcquisitionMode");
            if (Spinnaker::GenApi::IsReadable(ptrAcquisitionMode) && 
                Spinnaker::GenApi::IsWritable(ptrAcquisitionMode)) {
                
                Spinnaker::GenApi::CEnumEntryPtr ptrAcquisitionModeContinuous = ptrAcquisitionMode->GetEntryByName("Continuous");
                if (Spinnaker::GenApi::IsReadable(ptrAcquisitionModeContinuous)) {
                    ptrAcquisitionMode->SetIntValue(ptrAcquisitionModeContinuous->GetValue());
                }
            }
            
            // 노출 설정 (자동)
            Spinnaker::GenApi::CEnumerationPtr ptrExposureAuto = nodeMap.GetNode("ExposureAuto");
            if (Spinnaker::GenApi::IsWritable(ptrExposureAuto)) {
                try {
                    Spinnaker::GenApi::CEnumEntryPtr ptrExposureAutoContinuous = ptrExposureAuto->GetEntryByName("Continuous");
                    if (Spinnaker::GenApi::IsReadable(ptrExposureAutoContinuous)) {
                        ptrExposureAuto->SetIntValue(ptrExposureAutoContinuous->GetValue());
                    }
                }
                catch (Spinnaker::Exception& e) {
                }
            }
            
            // 프레임 레이트 설정 (가능한 경우)
            try {
                Spinnaker::GenApi::CBooleanPtr ptrFrameRateEnable = nodeMap.GetNode("AcquisitionFrameRateEnable");
                if (Spinnaker::GenApi::IsWritable(ptrFrameRateEnable)) {
                    ptrFrameRateEnable->SetValue(true);

                    Spinnaker::GenApi::CFloatPtr ptrFrameRate = nodeMap.GetNode("AcquisitionFrameRate");
                    if (Spinnaker::GenApi::IsWritable(ptrFrameRate)) {
                        // 최대 프레임 레이트 확인
                        double maxFrameRate = ptrFrameRate->GetMax();
                        double targetFrameRate = qMin(maxFrameRate, 30.0); // 30fps 제한
                        
                        ptrFrameRate->SetValue(targetFrameRate);
                    }
                }
            }
            catch (Spinnaker::Exception& e) {
            }
        }
        catch (Spinnaker::Exception& e) {
            // 설정 오류가 있더라도 계속 진행
        }
        
        // 획득 시작
        try {
            camera->BeginAcquisition();
        }
        catch (Spinnaker::Exception& e) {
            return false;
        }
        
        // 버퍼 클리어 - 오래된 프레임 제거
        try {
            // 버퍼에 쌓인 이미지 버리기
            uint64_t bufferedImages = camera->GetNumImagesInUse();
            if (bufferedImages > 0) {
                for (uint64_t i = 0; i < bufferedImages; i++) {
                    Spinnaker::ImagePtr oldImage = camera->GetNextImage(100);
                    if (oldImage) {
                        oldImage->Release();
                    }
                }
            }
        }
        catch (Spinnaker::Exception& e) {
        }
        
        // 연결 상태 설정
        info.isConnected = true;
        
        // 카메라 연결 시 시뮬레이션 모드 해제
        if (simulationMode) {
            simulationMode = false;
            currentSimulationImage = cv::Mat(); // 시뮬레이션 이미지 초기화
            updateUIForSimulationMode(false);  // UI 업데이트
            
            // 카메라뷰의 시뮬레이션 상태도 초기화
            if (cameraView) {
                cameraView->setSimulationCameraName("");
            }
        }
        
        return true;
    }
    catch (Spinnaker::Exception& e) {
        return false;
    }
}

cv::Mat TeachingWidget::grabFrameFromSpinnakerCamera(Spinnaker::CameraPtr& camera)
{
    cv::Mat cvImage;
    try {
        // 카메라가 초기화되었는지 확인
        if (!camera || !camera->IsInitialized()) {
            return cvImage;
        }
        
        // 카메라가 스트리밍 중인지 확인
        if (!camera->IsStreaming()) {
            try {
                camera->BeginAcquisition();
            } catch (Spinnaker::Exception& e) {
                return cvImage;
            }
        }
        
        // 버퍼 완전 비우기: 더 이상 이미지가 없을 때까지 반복
        while (true) {
            try {
                Spinnaker::ImagePtr oldImage = camera->GetNextImage(1); // 1ms 타임아웃
                if (!oldImage || oldImage->IsIncomplete()) break;
                oldImage->Release();
            } catch (...) {
                break;
            }
        }
        
        // 새 이미지 획득 시도 - 타임아웃을 보다 길게 설정 (100ms)
        Spinnaker::ImagePtr spinImage = camera->GetNextImage(100);
        
        // 완전한 이미지인지 확인
        if (!spinImage || spinImage->IsIncomplete()) {
            if (spinImage) {
                spinImage->Release();
            } else {
            }
            return cvImage;
        }
        
        // 이미지 크기 및 데이터 가져오기
        size_t width = spinImage->GetWidth();
        size_t height = spinImage->GetHeight();
        
        // 이미지 변환은 픽셀 형식에 따라 다름
        Spinnaker::PixelFormatEnums pixelFormat = spinImage->GetPixelFormat();
        
        if (pixelFormat == Spinnaker::PixelFormat_Mono8) {
            // 흑백 이미지 처리
            unsigned char* buffer = static_cast<unsigned char*>(spinImage->GetData());
            cvImage = cv::Mat(height, width, CV_8UC1, buffer).clone();
        } else {
            // 컬러 이미지 변환 (RGB8 형식으로)
            try {
                // 이미지 처리기를 사용하여 RGB8로 변환
                Spinnaker::ImageProcessor processor;
                processor.SetColorProcessing(Spinnaker::SPINNAKER_COLOR_PROCESSING_ALGORITHM_HQ_LINEAR);
                Spinnaker::ImagePtr convertedImage = processor.Convert(spinImage, Spinnaker::PixelFormat_RGB8);
                
                if (convertedImage && !convertedImage->IsIncomplete()) {
                    unsigned char* buffer = static_cast<unsigned char*>(convertedImage->GetData());
                    cvImage = cv::Mat(height, width, CV_8UC3, buffer).clone();
                } else {
                }
            } catch (Spinnaker::Exception& e) {
            }
        }
        
        // 이미지 메모리 해제
        spinImage->Release();
        
        return cvImage;
    }
    catch (Spinnaker::Exception& e) {
        return cvImage;
    }
}
#endif

TeachingWidget::~TeachingWidget() {
    #ifdef USE_SPINNAKER
        // Spinnaker SDK 정리
        releaseSpinnakerSDK();
    #endif

    // **멀티 카메라 스레드 정리**
    for (CameraGrabberThread* thread : cameraThreads) {
        if (thread && thread->isRunning()) {
            thread->stopGrabbing();
            thread->wait();
            delete thread;
        }
    }
    cameraThreads.clear();
    
    // UI 업데이트 스레드 정리
    if (uiUpdateThread) {
        uiUpdateThread->stopUpdating();
        uiUpdateThread->wait();
        delete uiUpdateThread;
        uiUpdateThread = nullptr;
    }
    
    // **멀티 카메라 자원 해제**
    for (int i = 0; i < getCameraInfosCount(); i++) {
        if (getCameraInfo(i).capture) {
            getCameraInfo(i).capture->release();
            removeCameraInfo(i);
        }
    }

    if (filterDialog) {
        delete filterDialog;
        filterDialog = nullptr;
    }
    
    // SimulationDialog 정리 시 부모가 닫히고 있음을 알림
    if (simulationDialog) {
        simulationDialog->setParentClosing(true);
        delete simulationDialog;
        simulationDialog = nullptr;
    }
}

QColor TeachingWidget::getNextColor() {
    // 색상 배열에서 순환하며 색상 선택
    QColor color = patternColors[nextColorIndex];
    nextColorIndex = (nextColorIndex + 1) % patternColors.size();
    return color;
}

void TeachingWidget::addFilter() {
    QTreeWidgetItem* selectedItem = patternTree->currentItem();
    if (!selectedItem) {
        UIColors::showWarning(this, "패턴 미선택", "필터를 추가할 패턴을 먼저 선택해주세요.");
        return;
    }
    
    // 필터 아이템이 선택되었을 경우 부모 패턴 아이템으로 변경
    QVariant filterIndexVar = selectedItem->data(0, Qt::UserRole + 1);
    if (filterIndexVar.isValid()) {
        if (selectedItem->parent()) {
            selectedItem = selectedItem->parent();
        }
    }
    
    QString idStr = selectedItem->data(0, Qt::UserRole).toString();
    QUuid patternId = QUuid(idStr);
    if (patternId.isNull()) {
        QMessageBox::warning(this, "패턴 정보 오류", "패턴 정보가 유효하지 않습니다.");
        return;
    }
    
    
    // 필터 대화상자 설정
    filterDialog->setPatternId(patternId);
    
    // 기존 연결 해제
    filterDialog->disconnect(SIGNAL(accepted()));
    
    // 필터 대화상자가 완료되면 트리 아이템 업데이트
    connect(filterDialog, &QDialog::accepted, this, [this, patternId]() {
        
        // 트리 아이템 업데이트
        updatePatternTree();
        
        // 카메라 뷰 업데이트
        updateCameraFrame();
        
        // 모든 패턴의 템플릿 이미지 갱신
        updateAllPatternTemplateImages();
    });
    
    filterDialog->show();
}

void TeachingWidget::addPattern() {
    // 시뮬레이션 모드 상태 디버깅
    if (!currentSimulationImage.empty()) {
    }
    
    // 현재 그려진 사각형이 있는지 먼저 확인 (엔터키로 호출된 경우)
    QRect currentRect = cameraView->getCurrentRect();
    bool hasDrawnRect = (!currentRect.isNull() && currentRect.width() >= 10 && currentRect.height() >= 10);
    
    // 선택된 아이템 확인
    QTreeWidgetItem* selectedItem = patternTree->currentItem();
    
    // 선택된 아이템이 필터인지 확인 (UserRole + 1에 필터 인덱스가 저장됨)
    QVariant filterIndexVar;
    if (selectedItem) {
        filterIndexVar = selectedItem->data(0, Qt::UserRole + 1);
        
        // 필터 아이템이 선택되었을 경우 부모 패턴 아이템으로 변경
        if (filterIndexVar.isValid()) {
            if (selectedItem->parent()) {
                selectedItem = selectedItem->parent();
            }
        }
    }
    
    // 그려진 사각형이 있으면 무조건 새 패턴 생성 (필터 추가 방지)
    if (hasDrawnRect) {
        // 패턴 이름 입력 받기
        bool ok;
        QString patternName = QInputDialog::getText(this, "패턴 이름", 
                                            "패턴 이름을 입력하세요 (비우면 자동 생성):", 
                                            QLineEdit::Normal, "", &ok);
        
        if (!ok) return; // 취소 버튼 누름
        
        // 이름이 비었으면 자동 생성
        if (patternName.isEmpty()) {
            const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
            QString prefix;
            
            switch (currentPatternType) {
                case PatternType::ROI: prefix = "R_"; break;
                case PatternType::FID: prefix = "F_"; break;
                case PatternType::INS: prefix = "I_"; break;
                case PatternType::FIL: prefix = "FL_"; break;
            }
            
            patternName = prefix;
            for (int i = 0; i < 5; ++i) {
                patternName += chars.at(QRandomGenerator::global()->bounded(chars.length()));
            }
        }
        
        // 패턴 정보 생성
        PatternInfo pattern;
        pattern.rect = currentRect;
        pattern.name = patternName;
        pattern.type = currentPatternType;
        
        // 시뮬레이션 모드인지 확인하여 카메라 UUID 설정
        if (cameraView && !cameraView->getSimulationCameraName().isEmpty()) {
            // 시뮬레이션 모드: 시뮬레이션 카메라 이름을 UUID로 사용
            pattern.cameraUuid = cameraView->getSimulationCameraName();
        } else {
            // 일반 모드: 실제 카메라 UUID 사용
            pattern.cameraUuid = getCameraInfo(cameraIndex).uniqueId;
        }
        
        // 타입별 색상 설정 (UIColors 클래스 사용)
        switch (currentPatternType) {
            case PatternType::ROI: pattern.color = UIColors::ROI_COLOR; break;
            case PatternType::FID: pattern.color = UIColors::FIDUCIAL_COLOR; break;
            case PatternType::INS: pattern.color = UIColors::INSPECTION_COLOR; break;
            case PatternType::FIL: pattern.color = UIColors::FILTER_COLOR; break;
        }
        
        // 패턴 타입별 기본값 설정
        if (currentPatternType == PatternType::ROI) {
            pattern.includeAllCamera = false;
        } 
        else if (currentPatternType == PatternType::FID) {
            pattern.matchThreshold = 0.8;
            pattern.useRotation = false;
            pattern.minAngle = -5.0;
            pattern.maxAngle = 5.0;
            pattern.angleStep = 1.0;
            pattern.fidMatchMethod = 0;
            pattern.runInspection = true;
            
            // 템플릿 이미지 추출
            cv::Mat sourceImage;
            bool hasSourceImage = false;
            
            // 시뮬레이션 모드인지 확인
            if (simulationMode && !currentSimulationImage.empty()) {
                // 시뮬레이션 모드: 현재 시뮬레이션 이미지 사용
                sourceImage = currentSimulationImage.clone();
                hasSourceImage = true;
            } else {
                // 일반 모드: cameraFrames 사용
                if (cameraIndex >= 0 && cameraIndex < static_cast<int>(cameraFrames.size()) && 
                    !cameraFrames[cameraIndex].empty()) {
                    sourceImage = cameraFrames[cameraIndex];
                    hasSourceImage = true;
                }
            }
            
            if (hasSourceImage) {
                cv::Rect rect(pattern.rect.x(), pattern.rect.y(), 
                             pattern.rect.width(), pattern.rect.height());
                
                if (rect.x >= 0 && rect.y >= 0 &&
                    rect.x + rect.width <= sourceImage.cols &&
                    rect.y + rect.height <= sourceImage.rows) {
                    
                    cv::Mat roi = sourceImage(rect).clone();
                    cv::cvtColor(roi, roi, cv::COLOR_BGR2RGB);
                    QImage img(roi.data, roi.cols, roi.rows, roi.step, QImage::Format_RGB888);
                    pattern.templateImage = img.copy();
                }
            }
        } 
        else if (currentPatternType == PatternType::INS) {
            pattern.passThreshold = 0.9;
            pattern.invertResult = false;
            pattern.inspectionMethod = 0;
            pattern.binaryThreshold = 128;
            pattern.compareMethod = 0;
            pattern.lowerThreshold = 0.5;
            pattern.upperThreshold = 1.0;
            pattern.ratioType = 0;
        }
        
        // 패턴 추가 및 ID 받기
        QUuid id = cameraView->addPattern(pattern);
        
        // CameraView에서 추가된 패턴 가져오기
        PatternInfo* addedPattern = cameraView->getPatternById(id);
        if (!addedPattern) {
            return;
        }
        
        // INS 패턴인 경우 템플릿 이미지를 필터가 적용된 상태로 업데이트
        if (currentPatternType == PatternType::INS) {
            updateInsTemplateImage(addedPattern, addedPattern->rect);
        }
        
        // 트리 아이템 생성
        QTreeWidgetItem* newItem = createPatternTreeItem(*addedPattern);
        
        // 최상위 항목으로 추가
        patternTree->addTopLevelItem(newItem);
        
        // 새로 추가한 항목 선택 및 표시
        patternTree->clearSelection();
        newItem->setSelected(true);
        patternTree->scrollToItem(newItem);
        
        // 임시 사각형 지우기
        cameraView->clearCurrentRect();

        if (addedPattern) {
            cameraView->setSelectedPatternId(addedPattern->id);
        }
        
        return; // 새 패턴 생성 후 함수 종료
    }
    
    // 그려진 사각형이 없고 선택된 아이템이 있으면 필터 추가
    if (selectedItem) {
        QString idStr = selectedItem->data(0, Qt::UserRole).toString();
        QUuid patternId = QUuid(idStr);
        if (patternId.isNull()) {
           UIColors::showWarning(this, "패턴 정보 오류", "패턴 정보가 유효하지 않습니다.");
            return;
        }
        
        
        // 필터 대화상자 설정
        filterDialog->setPatternId(patternId);
        
        // 기존 연결 해제
        filterDialog->disconnect(SIGNAL(accepted()));
        
        // 필터 대화상자가 완료되면 트리 아이템 업데이트
        connect(filterDialog, &QDialog::accepted, this, [this, patternId]() {
            
            // 트리 아이템 업데이트
            updatePatternTree();
            
            // 카메라 뷰 업데이트
            updateCameraFrame();
        });
        
        filterDialog->show();
    } else {
        // 선택된 아이템도 없고 그려진 사각형도 없으면 안내 메시지
        if (!selectedItem && !hasDrawnRect) {
            UIColors::showWarning(this, "패턴 없음", "먼저 카메라 화면에 사각형 패턴을 그리거나 패턴을 선택해주세요.");
        }
    }
}

void TeachingWidget::removePattern() {
    QTreeWidgetItem* selectedItem = patternTree->currentItem();
    if (!selectedItem) {
        UIColors::showInformation(this, "선택 필요", "삭제할 항목을 먼저 목록에서 선택하세요.");
        return;
    }
    
    QVariant filterIndexVar = selectedItem->data(0, Qt::UserRole + 1);
    
    if (filterIndexVar.isValid()) {
        // 필터 삭제 로직
        QString idStr = selectedItem->data(0, Qt::UserRole).toString();
        QUuid patternId = QUuid(idStr);
        int filterIndex = filterIndexVar.toInt();
        
        QMessageBox::StandardButton reply = UIColors::showQuestion(this, "패턴 삭제",
                "선택한 패턴을 삭제하시겠습니까?", 
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            
        if (reply == QMessageBox::Yes) {
            cameraView->removePatternFilter(patternId, filterIndex);
            
            // 트리 업데이트 - 전체 트리 재구성으로 안전하게 처리
            updatePatternTree();
            
            // 필터 삭제 후 즉시 카메라 프레임 업데이트
            updateCameraFrame();
            updateAllPatternTemplateImages();
            
            cameraView->update();
        }
    } else {
        // 패턴 삭제 로직
        QUuid patternId = getPatternIdFromItem(selectedItem);
        if (!patternId.isNull()) {
            QMessageBox::StandardButton reply = UIColors::showQuestion(this, "패턴 삭제",
                "선택한 패턴을 삭제하시겠습니까?", 
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
                
            if (reply == QMessageBox::Yes) {
                cameraView->removePattern(patternId);
                
                // 트리 업데이트 - 전체 트리 재구성으로 안전하게 처리
                updatePatternTree();
                
                // 프로퍼티 패널 초기화
                if (propertyStackWidget) {
                    propertyStackWidget->setCurrentIndex(0);
                }
            }
        }
    }
}

QColor TeachingWidget::getButtonColorForPatternType(PatternType type) {
    return UIColors::getPatternColor(type);
}

void TeachingWidget::onBackButtonClicked() {
    // **1. 멀티 카메라 스레드 중지**
    for (CameraGrabberThread* thread : cameraThreads) {
        if (thread && thread->isRunning()) {
            thread->stopGrabbing();
            thread->wait();
            delete thread;
        }
    }
    cameraThreads.clear();
    
    // **2. UI 업데이트 스레드 중지**
    if (uiUpdateThread) {
        uiUpdateThread->stopUpdating();
        uiUpdateThread->wait();
    }
    
#ifdef USE_SPINNAKER
    // **3. Spinnaker 카메라 정리**
    if (m_useSpinnaker) {
        try {
            for (auto& camera : m_spinCameras) {
                if (camera && camera->IsStreaming()) {
                    camera->EndAcquisition();
                }
                if (camera && camera->IsInitialized()) {
                    camera->DeInit();
                }
            }
            m_spinCameras.clear();
            
            if (m_spinCamList.GetSize() > 0) {
                m_spinCamList.Clear();
            }
        }
        catch (Spinnaker::Exception& e) {
        }
    }
#endif
    
    // **4. OpenCV 카메라 자원 해제**
    int cameraCount = getCameraInfosCount();
    for (int i = cameraCount - 1; i >= 0; i--) {  // 역순으로 삭제
        CameraInfo info = getCameraInfo(i);
        if (info.capture && !info.uniqueId.startsWith("SPINNAKER_")) {
            info.capture->release();
            delete info.capture;
            info.capture = nullptr;
        }
        info.isConnected = false;
        setCameraInfo(i, info);
    }
    clearCameraInfos();  // 마지막에 전체 클리어
    cameraIndex = -1;
    
    // **6. 이전 화면으로 돌아가기**
    emit goBack();
}

void TeachingWidget::updateUIElements() {
    // 카메라 뷰가 유효한지 확인
    if (!cameraView) return;
    
    // 스케일링 정보 업데이트
    if (cameraIndex >= 0 && cameraIndex < static_cast<int>(cameraFrames.size()) && 
        !cameraFrames[cameraIndex].empty()) {
        
        QSize origSize(cameraFrames[cameraIndex].cols, cameraFrames[cameraIndex].rows);
        QSize viewSize = cameraView->size();
        
        if (origSize.width() > 0 && origSize.height() > 0 && 
            viewSize.width() > 0 && viewSize.height() > 0) {
            double newScaleX = static_cast<double>(viewSize.width()) / origSize.width();
            double newScaleY = static_cast<double>(viewSize.height()) / origSize.height();
            
            if (cameraView->hasValidScaling()) {
                // 이전과 스케일이 동일하면 리턴 (오차 범위 고려)
                if (cameraView->isSameScaling(newScaleX, newScaleY)) {
                    // 지속적인 스케일링 계산이 필요없는 경우 UI만 업데이트
                    cameraView->update();
                }
            } else {
                cameraView->setScaling(newScaleX, newScaleY);
            }
        }
    }
    
    // UI 업데이트 - 패턴 및 사각형 그리기
    cameraView->update();
    
    // 미리보기 UI 업데이트
    updatePreviewUI();
}

void TeachingWidget::setupCalibrationTools() {
    // 기존에 생성된 액션에 시그널-슬롯 연결만 수행
    if (calibrateAction) {
        // 기존 연결 제거 (중복 연결 방지)
        disconnect(calibrateAction, &QAction::triggered, this, &TeachingWidget::startCalibration);
        // 새로 연결
        connect(calibrateAction, &QAction::triggered, this, &TeachingWidget::startCalibration);
    } else {
    }

    // CameraView에서 캘리브레이션 관련 시그널 연결
    connect(cameraView, &CameraView::calibrationRectDrawn, this, [this](const QRect& rect) {
        // 사용자에게 실제 길이 입력 요청
        bool ok;
        double realLength = QInputDialog::getDouble(this, TR("REAL_LENGTH_INPUT_TITLE"),
            TR("REAL_LENGTH_INPUT_MSG"), 50.0, 1.0, 10000.0, 1, &ok); 

        if (ok) {
            finishCalibration(rect, realLength);
        } else {
            // 취소 시 캘리브레이션 모드 종료
            cameraView->setCalibrationMode(false);
        }
    });
    
    // 일반 사각형 그리기 이벤트에 물리적 길이 표시 추가
    connect(cameraView, &CameraView::rectDrawn, this, [this](const QRect& rect) {
        const CalibrationInfo& calibInfo = cameraView->getCalibrationInfo();
        if (calibInfo.isCalibrated) {
            // 물리적 길이 계산 및 표시
            double widthMm = cameraView->calculatePhysicalLength(rect.width());
            double heightMm = cameraView->calculatePhysicalLength(rect.height());
            
            cameraView->setMeasurementInfo(QString("%1 × %2 mm")
                                         .arg(widthMm, 0, 'f', 1)
                                         .arg(heightMm, 0, 'f', 1));
        }
    });
}

void TeachingWidget::startCalibration() {
    // 카메라가 연결되었는지 확인
    if (cameraIndex < 0 || cameraIndex >= getCameraInfosCount() || !getCameraInfo(cameraIndex).isConnected) {
        UIColors::showWarning(this, TR("LENGTH_CALIBRATION"), 
                             TR("NO_CAMERA_CONNECTED"));
        return;
    }
    
    // 현재 모드 저장
    CameraView::EditMode savedMode = cameraView->getEditMode();
    
    // 현재 카메라 정보 표시
    QString currentCameraName = getCameraInfo(cameraIndex).name;
    QString currentCameraUuid = getCameraInfo(cameraIndex).uniqueId;
    
    // 사용자에게 안내 메시지 표시
    UIColors::showInformation(this, TR("LENGTH_CALIBRATION"),
        QString("%1\n\n%2: %3\n%4: %5")
            .arg(TR("CALIBRATION_INSTRUCTION"))
            .arg(TR("CURRENT_CAMERA"))
            .arg(currentCameraName)
            .arg(TR("CAMERA_ID"))
            .arg(currentCameraUuid));
    
    // 캘리브레이션 모드로 전환
    cameraView->setCalibrationMode(true);
}

void TeachingWidget::disableAllUIElements() {
    // **메인 기능 버튼들 비활성화**
    if (runStopButton) runStopButton->setEnabled(false);
    
    // **패턴 관련 버튼들 비활성화** 
    if (addPatternButton) addPatternButton->setEnabled(false);
    if (removeButton) removeButton->setEnabled(false);
    if (addFilterButton) addFilterButton->setEnabled(false);
    
    // **ROI/FID/INS 패턴 타입 버튼들 비활성화**
    if (roiButton) roiButton->setEnabled(false);
    if (fidButton) fidButton->setEnabled(false);
    if (insButton) insButton->setEnabled(false);
    
    // **토글 버튼들 비활성화 (CAM 버튼 제외)**
    if (modeToggleButton) modeToggleButton->setEnabled(false);
    
    // **레시피 관련 버튼들 비활성화**
    if (saveRecipeButton) saveRecipeButton->setEnabled(false);
    
    // **findChild로 버튼 찾아서 비활성화 (이름 기반)**
    QPushButton* modeToggleBtn = findChild<QPushButton*>("modeToggleButton");
    if (modeToggleBtn) modeToggleBtn->setEnabled(false);
    
    QPushButton* saveBtn = findChild<QPushButton*>("saveRecipeButton");
    if (saveBtn) saveBtn->setEnabled(false);
    
    QPushButton* addBtn = findChild<QPushButton*>("addPatternButton");
    if (addBtn) addBtn->setEnabled(false);
    
    QPushButton* removeBtn = findChild<QPushButton*>("removeButton");
    if (removeBtn) removeBtn->setEnabled(false);
    
    QPushButton* filterBtn = findChild<QPushButton*>("addFilterButton");
    if (filterBtn) filterBtn->setEnabled(false);
    
    // **메뉴 항목들은 모두 기본 활성화 유지**
    if (menuBar) {
        QList<QAction*> actions = menuBar->actions();
        for (QAction* action : actions) {
            // 모든 메뉴를 기본 활성화 상태로 유지
            action->setEnabled(true);
            if (action->menu()) {
                action->menu()->setEnabled(true);
                // 모든 하위 액션들도 활성화
                QList<QAction*> subActions = action->menu()->actions();
                for (QAction* subAction : subActions) {
                    subAction->setEnabled(true);
                }
            }
        }
    }
    
    // **패턴 트리 비활성화**
    if (patternTree) {
        patternTree->setEnabled(false);
        patternTree->clear();
    }
    
    // **프로퍼티 패널 비활성화**
    if (propertyStackWidget) {
        propertyStackWidget->setEnabled(false);
    }
    
    // **카메라 뷰를 "연결 없음" 상태로 설정**
    if (cameraView) {
        cameraView->setEnabled(false);
        // 빈 픽스맵(null)으로 설정하여 "연결 없음" 텍스트가 표시되도록 함
        QPixmap emptyPixmap;  // null 픽스맵
        cameraView->setBackgroundPixmap(emptyPixmap);
        cameraView->update();
    }
    
    // **미리보기 레이블들 비활성화**
    for (int i = 0; i < cameraPreviewLabels.size(); i++) {
        if (cameraPreviewLabels[i]) {
            cameraPreviewLabels[i]->setEnabled(false);
            cameraPreviewLabels[i]->clear();
            cameraPreviewLabels[i]->setText(TR("NO_CONNECTION"));
            cameraPreviewLabels[i]->setStyleSheet("background-color: #333333; color: #666666;");
        }
    }
    
}

void TeachingWidget::enableAllUIElements() {
    
    // **메인 기능 버튼들 활성화**
    if (runStopButton) runStopButton->setEnabled(true);
    
    // **패턴 관련 버튼들 활성화**
    if (addPatternButton) addPatternButton->setEnabled(true);
    if (removeButton) removeButton->setEnabled(true);
    if (addFilterButton) addFilterButton->setEnabled(true);
    
    // **ROI/FID/INS 패턴 타입 버튼들 활성화**
    if (roiButton) roiButton->setEnabled(true);
    if (fidButton) fidButton->setEnabled(true);
    if (insButton) insButton->setEnabled(true);
    
    // **토글 버튼들 활성화**
    if (modeToggleButton) modeToggleButton->setEnabled(true);
    
    // **레시피 관련 버튼들 활성화**
    if (saveRecipeButton) saveRecipeButton->setEnabled(true);
    
    // **findChild로 버튼 찾아서 활성화 (이름 기반)**
    QPushButton* modeToggleBtn = findChild<QPushButton*>("modeToggleButton");
    if (modeToggleBtn) modeToggleBtn->setEnabled(true);
    
    QPushButton* saveBtn = findChild<QPushButton*>("saveRecipeButton");
    if (saveBtn) saveBtn->setEnabled(true);
    
    QPushButton* addBtn = findChild<QPushButton*>("addPatternButton");
    if (addBtn) addBtn->setEnabled(true);
    
    QPushButton* removeBtn = findChild<QPushButton*>("removeButton");
    if (removeBtn) removeBtn->setEnabled(true);
    
    QPushButton* filterBtn = findChild<QPushButton*>("addFilterButton");
    if (filterBtn) filterBtn->setEnabled(true);
    
    // **메뉴 항목들 활성화**
    if (menuBar) {
        QList<QAction*> actions = menuBar->actions();
        for (QAction* action : actions) {
            action->setEnabled(true);
            if (action->menu()) {
                action->menu()->setEnabled(true);
                // 하위 메뉴 항목들도 활성화
                QList<QAction*> subActions = action->menu()->actions();
                for (QAction* subAction : subActions) {
                    subAction->setEnabled(true);
                }
            }
        }
    }
    
    // **패턴 트리 활성화**
    if (patternTree) {
        patternTree->setEnabled(true);
    }
    
    // **프로퍼티 패널 활성화**
    if (propertyStackWidget) {
        propertyStackWidget->setEnabled(true);
    }
    
    // **카메라 뷰 활성화**
    if (cameraView) {
        cameraView->setEnabled(true);
    }
    
    // **미리보기 레이블들 활성화**
    for (int i = 0; i < cameraPreviewLabels.size(); i++) {
        if (cameraPreviewLabels[i]) {
            cameraPreviewLabels[i]->setEnabled(true);
            cameraPreviewLabels[i]->setStyleSheet("background-color: black; color: white;");
        }
    }
    
}

InspectionResult TeachingWidget::runSingleInspection(int specificCameraIndex) {
    InspectionResult result;
    
    try {
        // **1. 카메라 인덱스 유효성 검사**
        if (specificCameraIndex < 0 || specificCameraIndex >= getCameraInfosCount()) {
            return result;
        }

        // **2. 필요시 카메라 전환**
        if (specificCameraIndex != cameraIndex) {
            CameraInfo targetCameraInfo = getCameraInfo(specificCameraIndex);
            switchToCamera(targetCameraInfo.uniqueId);
            QApplication::processEvents();
        }

        // **3. 멤버 변수 runStopButton 직접 사용**
        if (!runStopButton) {
            return result;
        }

        bool wasInInspectionMode = runStopButton->isChecked();
        
        // **4. 라이브 모드였다면 RUN 버튼 클릭 (검사 시작)**
        if (!wasInInspectionMode) {
            runStopButton->click();
            QApplication::processEvents();
        }

        // **5. 검사 실행**
        cv::Mat inspectionFrame;
        
        // 시뮬레이션 모드 확인
        if (simulationMode && !currentSimulationImage.empty()) {
            // 시뮬레이션 모드: 현재 시뮬레이션 이미지 사용
            inspectionFrame = currentSimulationImage.clone();
            printf("[TeachingWidget] runSingleInspection - 시뮬레이션 모드로 검사\n");
            fflush(stdout);
        } else if (cameraView && specificCameraIndex < static_cast<int>(cameraFrames.size()) && 
                   !cameraFrames[specificCameraIndex].empty()) {
            // 실제 카메라 모드: 해당 카메라 프레임 사용
            inspectionFrame = cameraFrames[specificCameraIndex].clone();
            printf("[TeachingWidget] runSingleInspection - 카메라 모드로 검사\n");
            fflush(stdout);
        }
        
        if (!inspectionFrame.empty() && cameraView) {
            // 현재 카메라의 활성 패턴들 가져오기
            QList<PatternInfo> cameraPatterns;
            QString currentCameraUuid;
            
            if (simulationMode) {
                // 시뮬레이션 모드에서는 현재 카메라의 UUID 사용
                if (cameraIndex >= 0 && cameraIndex < cameraInfos.size()) {
                    currentCameraUuid = cameraInfos[cameraIndex].uniqueId;
                }
            } else {
                // 실제 카메라 모드
                currentCameraUuid = getCameraInfo(specificCameraIndex).uniqueId;
            }
            
            const QList<PatternInfo>& allPatterns = cameraView->getPatterns();
            
            for (const PatternInfo& pattern : allPatterns) {
                if (pattern.enabled && pattern.cameraUuid == currentCameraUuid) {
                    cameraPatterns.append(pattern);
                }
            }

            if (!cameraPatterns.isEmpty()) {
                // 직접 검사 수행
                InsProcessor processor;
                result = processor.performInspection(inspectionFrame, cameraPatterns);
                
                // **UI 업데이트 (메인 카메라인 경우 또는 시뮬레이션 모드)**
                if (specificCameraIndex == cameraIndex || simulationMode) {
                    updateMainCameraUI(result, inspectionFrame);
                }
            }
        }

        return result;

    } catch (...) {
        return result;
    }
}

void TeachingWidget::stopSingleInspection() {
    try {
        // **1. RUN 버튼을 STOP 상태로 변경**
        if (runStopButton && runStopButton->isChecked()) {
            runStopButton->blockSignals(true);
            runStopButton->setChecked(false);
            runStopButton->setText("RUN");
            runStopButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));
            runStopButton->blockSignals(false);
        }
        
        // **2. 검사 모드 해제**
        if (cameraView) {
            cameraView->setInspectionMode(false);
        }
        
        // **3. UI 업데이트 스레드 재개**
        if (uiUpdateThread) {
            if (uiUpdateThread->isRunning()) {
                uiUpdateThread->setPaused(false);
            } else if (uiUpdateThread->isFinished()) {
                uiUpdateThread->start(QThread::NormalPriority);
            }
        }
        
        // **4. UI 이벤트 처리**
        QApplication::processEvents();
        
        // **5. 화면 갱신**
        if (cameraView) {
            cameraView->update();
        }

    } catch (...) {
        // 예외 발생 시에도 최소한의 정리
        if (runStopButton && runStopButton->isChecked()) {
            runStopButton->blockSignals(true);
            runStopButton->setChecked(false);
            runStopButton->setText("RUN");
            runStopButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));
            runStopButton->blockSignals(false);
        }
        
        if (cameraView) {
            cameraView->setInspectionMode(false);
        }
    }
}

// **private 헬퍼 함수 추가**
void TeachingWidget::updateMainCameraUI(const InspectionResult& result, const cv::Mat& frameForInspection) {
    // **RUN 버튼 상태 업데이트**
    if (runStopButton && !runStopButton->isChecked()) {
        runStopButton->blockSignals(true);
        runStopButton->setChecked(true);
        runStopButton->setText("STOP");
        runStopButton->setStyleSheet(UIColors::toggleButtonStyle(UIColors::BTN_REMOVE_COLOR, QColor("#FF5722"), true));
        runStopButton->blockSignals(false);
    }
    
    // **검사 모드 설정**
    if (cameraView) {
        cameraView->setInspectionMode(true);
        cameraView->updateInspectionResult(result.isPassed, result);
        
        // **원본 이미지를 배경으로 설정**
        QImage originalImage = InsProcessor::matToQImage(frameForInspection);
        if (!originalImage.isNull()) {
            QPixmap pixmap = QPixmap::fromImage(originalImage);
            cameraView->setBackgroundPixmap(pixmap);
        }
        
        cameraView->update();
    }
    
    // **로그 뷰어 표시**
    if (logViewer) {
        logViewer->show();
    }
}

void TeachingWidget::onSimulationModeToggled() {
    simulationMode = !simulationMode;
    
    if (simulationMode) {
        // 1. 기존 레시피 백업 및 언로드
        backupCurrentRecipe();
        clearCurrentRecipe();
        
        // 2. 시뮬레이션 레시피 로드 또는 생성
        loadSimulationRecipe();
        
        // 3. 카메라 중지
        stopCamera();
        
        // 4. 시뮬레이션 다이얼로그 생성 및 표시
        if (!simulationDialog) {
            simulationDialog = new SimulationDialog(this);
            
            // 시그널 연결
            connect(simulationDialog, &SimulationDialog::imageSelected, 
                    this, &TeachingWidget::onSimulationImageSelected);
            connect(simulationDialog, &SimulationDialog::recipeNameChanged,
                    this, &TeachingWidget::onSimulationProjectNameChanged);
            connect(simulationDialog, &SimulationDialog::recipeSelected,
                    this, &TeachingWidget::onSimulationProjectSelected);
        }
        
        simulationDialog->show();
        simulationDialog->raise();
        simulationDialog->activateWindow();
        
        // UI 상태 업데이트
        updateUIForSimulationMode(true);
        
    } else {
        // 1. 시뮬레이션 다이얼로그 숨기기
        if (simulationDialog) {
            simulationDialog->hide();
        }
        
        // 2. 시뮬레이션 카메라 이름 초기화
        if (cameraView) {
            cameraView->setSimulationCameraName(""); // 빈 문자열로 초기화
            cameraView->update();
        }
        
        // UI 상태 업데이트
        updateUIForSimulationMode(false);
    }
}

void TeachingWidget::showSimulationDialog() {
    // 라이브 모드가 켜져 있으면 자동으로 끄기
    if (startCameraButton && startCameraButton->isChecked()) {
        startCameraButton->setChecked(false);
        stopCamera();
        qDebug() << "시뮬레이션 모드 진입을 위해 라이브 모드를 종료했습니다.";
    }
    
    // 시뮬레이션 다이얼로그 생성 및 표시
    if (!simulationDialog) {
        simulationDialog = new SimulationDialog(this);
        
        // 시그널 연결
        connect(simulationDialog, &SimulationDialog::imageSelected, 
                this, &TeachingWidget::onSimulationImageSelected);
        connect(simulationDialog, &SimulationDialog::recipeNameChanged,
                this, &TeachingWidget::onSimulationProjectNameChanged);
        connect(simulationDialog, &SimulationDialog::recipeSelected,
                this, &TeachingWidget::onSimulationProjectSelected);
        
        // 다이얼로그가 닫힐 때 시뮬레이션 모드 비활성화
        connect(simulationDialog, &QDialog::rejected, this, [this]() {
            simulationMode = false;
            qDebug() << "시뮬레이션 모드가 비활성화되었습니다.";
        });
        connect(simulationDialog, &QDialog::accepted, this, [this]() {
            simulationMode = false;
            qDebug() << "시뮬레이션 모드가 비활성화되었습니다.";
        });
    }
    
    // 시뮬레이션 모드 활성화
    simulationMode = true;
    qDebug() << "시뮬레이션 모드가 활성화되었습니다.";
    
    simulationDialog->show();
    simulationDialog->raise();
    simulationDialog->activateWindow();
    
    // 현재 레시피가 있으면 시뮬레이션 다이얼로그에 로드
    if (!currentRecipeName.isEmpty()) {
        simulationDialog->loadRecipeImages(currentRecipeName);
    }
}

// 시뮬레이션 다이얼로그에서 이미지가 선택되었을 때
void TeachingWidget::onSimulationImageSelected(const cv::Mat& image, const QString& imagePath, const QString& projectName) {
    if (!image.empty()) {
        // 현재 시뮬레이션 모드 상태 저장
        bool wasInSimulationMode = simulationMode;
        
        // 시뮬레이션 모드 활성화
        simulationMode = true;
        
        // 현재 시뮬레이션 이미지 저장
        currentSimulationImage = image.clone();
        
        // 시뮬레이션 모드임을 명확히 표시
        if (cameraView) {
            // 패턴은 이미 레시피 로드 시에 로딩되었으므로 재로딩하지 않음
            // 단지 시뮬레이션 이미지만 표시
            
            // OpenCV Mat을 QImage로 변환
            QImage qImage;
            if (image.channels() == 3) {
                cv::Mat rgbImage;
                cv::cvtColor(image, rgbImage, cv::COLOR_BGR2RGB);
                qImage = QImage(rgbImage.data, rgbImage.cols, rgbImage.rows, rgbImage.step, QImage::Format_RGB888);
            } else {
                qImage = QImage(image.data, image.cols, image.rows, image.step, QImage::Format_Grayscale8);
            }
            
            // QPixmap으로 변환하여 CameraView에 설정
            QPixmap pixmap = QPixmap::fromImage(qImage);
            cameraView->setBackgroundPixmap(pixmap);
            
            // 마우스 이벤트와 줌/팬 기능 강제 활성화
            cameraView->setEnabled(true);
            cameraView->setMouseTracking(true);
            cameraView->setFocusPolicy(Qt::StrongFocus);
            
            // 현재 선택된 패턴 버튼에 따라 적절한 Edit 모드 설정
            if (patternButtonGroup && patternButtonGroup->checkedButton()) {
                cameraView->setEditMode(CameraView::EditMode::Draw);
            } else {
                cameraView->setEditMode(CameraView::EditMode::Move);
            }
            
            cameraView->setFocus(); // 포커스 설정
            cameraView->setAttribute(Qt::WA_AcceptTouchEvents, true);
            
            // 강제 업데이트
            cameraView->update();
            cameraView->repaint();
            cameraView->show(); // 위젯 표시 강제
        }
        
        // 시뮬레이션 카메라 정보로 UI 업데이트
        updateCameraInfoForSimulation(imagePath);
        
        // 시뮬레이션 모드에서는 이미 selectCameraTeachingImage에서 설정된 카메라 UUID를 유지
        // projectName(레시피 이름)으로 카메라 UUID를 덮어쓰지 않음
        qDebug() << QString("시뮬레이션 이미지 선택됨, 현재 카메라 UUID 유지: %1").arg(cameraView ? cameraView->getCurrentCameraUuid() : "없음");
        
        // 패턴 편집 기능들 활성화
        enablePatternEditingFeatures();
        
        // 티칭 이미지 변경은 단순히 배경 이미지만 교체하는 것이므로 패턴 트리 업데이트 불필요
        // updatePatternTree(); // 제거: 패턴 목록은 그대로 유지
        
        // 상태바에 시뮬레이션 정보 표시
        QFileInfo fileInfo(imagePath);
        QString statusMessage = QString("시뮬레이션 이미지: %1 (%2x%3) | 마우스휠:줌, Ctrl+드래그:이동")
            .arg(fileInfo.fileName())
            .arg(image.cols)
            .arg(image.rows);
    }
}

void TeachingWidget::onSimulationProjectNameChanged(const QString& projectName) {
    if (simulationMode && cameraView) {
        
        if (projectName.isEmpty()) {
            // 빈 프로젝트 이름이면 초기화
            cameraView->setSimulationCameraName("");
            cameraView->setCurrentCameraUuid("");
            
            // 카메라 뷰 이미지 초기화 (연결 없음 상태로)
            cameraView->setBackgroundPixmap(QPixmap());
            cameraView->clear(); // QLabel의 텍스트/이미지 지우기
            cameraView->setText(TR("NO_CONNECTION")); // 기본 텍스트 설정
            
            // 패턴들 모두 제거 (CameraView에서 관리)
            cameraView->clearPatterns();
            
            // UI 초기화
            updatePatternTree();
            cameraView->update();
            
        } else {
            // projectName을 그대로 사용 (이미 SIM_ 접두어가 포함되어 있음)
            QString cameraDisplayName = projectName;
            
            // CameraView에 카메라 이름 표시 업데이트
            cameraView->setSimulationCameraName(cameraDisplayName);
            
            // 현재 카메라 UUID도 동일한 이름으로 설정 (패턴 추가 시 일치하도록)
            cameraView->setCurrentCameraUuid(cameraDisplayName);
            
            
            // UI 업데이트
            cameraView->update();
        }
    }
}

void TeachingWidget::onSimulationProjectSelected(const QString& projectName) {
    if (!simulationMode || !cameraView) {
        return;
    }
    
    qDebug() << QString("시뮬레이션 프로젝트 선택됨: %1").arg(projectName);
    
    // 현재 레시피 이름 설정 (Save 버튼으로 저장할 때 사용)
    currentRecipeName = projectName;
    hasUnsavedChanges = false;
    qDebug() << QString("시뮬레이션 모드에서 현재 레시피 이름 설정: %1").arg(currentRecipeName);
    
    // 레시피에서 해당 프로젝트의 패턴들 로드
    loadSimulationRecipePatterns(projectName);
    
    // 패턴 트리 업데이트 (카메라 UUID는 selectCameraTeachingImage에서 설정됨)
    updatePatternTree();
    cameraView->update();
    
    // AI 모델 존재 여부 체크 및 미리 로딩
    if (aiTrainer) {
        // 현재 로딩된 레시피와 다른 경우 이전 모델 정리
        QString currentLoadedRecipe = getCurrentRecipeName();
        if (!currentLoadedRecipe.isEmpty() && currentLoadedRecipe != projectName) {
            qDebug() << "[TeachingWidget] Unloading previous model for recipe:" << currentLoadedRecipe;
            aiTrainer->unloadModel(currentLoadedRecipe);
        }
        
        // 모델 파일 존재 여부 확인
        QString appBase = QDir::cleanPath(QCoreApplication::applicationDirPath());
        QString candidate1 = QDir::cleanPath(appBase + "/models/" + projectName + "/model.ckpt");
        QString candidate2 = QDir::cleanPath(QDir::currentPath() + "/models/" + projectName + "/model.ckpt");
        bool modelExists = QFile::exists(candidate1) || QFile::exists(candidate2);
        
        if (modelExists) {
            qDebug() << "[TeachingWidget] AI model found for recipe:" << projectName << "- starting pre-load";
            // 상태바나 로그에 로딩 시작 표시
            qDebug() << "[TeachingWidget] AI 모델 로딩 시작:" << projectName;
            
            // 비동기로 모델 로딩 (UI 블로킹 방지)
            QTimer::singleShot(100, [this, projectName]() {
                bool success = aiTrainer->loadModel(projectName);
                if (success) {
                    qDebug() << "[TeachingWidget] AI 모델 로딩 완료:" << projectName;
                } else {
                    qWarning() << "[TeachingWidget] AI 모델 로딩 실패:" << projectName;
                }
            });
        }
    }
}

void TeachingWidget::loadSimulationRecipePatterns(const QString& projectName) {
    qDebug() << QString("시뮬레이션 레시피 패턴 로드: %1").arg(projectName);
    
    // 선택된 프로젝트 이름으로 레시피 로드
    if (!projectName.isEmpty()) {
        // onRecipeSelected 함수를 사용하여 해당 레시피 로드
        onRecipeSelected(projectName);
        
        qDebug() << QString("시뮬레이션 레시피 '%1' 로드 완료").arg(projectName);
    }
}

void TeachingWidget::backupCurrentRecipe() {
    // 현재 레시피를 백업 변수에 저장
    backupRecipeData.clear();
    
    // 현재 레시피 이름 백업
    QString currentRecipeName = getCurrentRecipeName();
    if (!currentRecipeName.isEmpty()) {
        backupRecipeData["recipeName"] = currentRecipeName;
    }
}

void TeachingWidget::clearCurrentRecipe() {
    // 현재 레시피 데이터 초기화
    // 기존 함수들 사용 (있다면)
    if (patternTree) {
        patternTree->clear();
    }
    
    // 카메라 정보 초기화
    cameraInfos.clear();
}

void TeachingWidget::loadSimulationRecipe() {
    // 먼저 현재 상태를 백업
    backupCurrentRecipe();
    
    // 기존 레시피 데이터 초기화
    clearCurrentRecipe();
    
    // 시뮬레이션 전용 카메라 정보 생성
    CameraInfo simCameraInfo;
    simCameraInfo.name = "SIM_CAM (준비중...)";
    simCameraInfo.index = -1; // 시뮬레이션용
    simCameraInfo.uniqueId = "SIM_CAM";
    cameraInfos.clear();
    cameraInfos.append(simCameraInfo);
    
    // 사용 가능한 개별 레시피 파일이 있는지 확인
    RecipeManager recipeManager;
    QStringList availableRecipes = recipeManager.getAvailableRecipes();
    
    if (!availableRecipes.isEmpty()) {
        // 첫 번째 사용 가능한 레시피 로드
        onRecipeSelected(availableRecipes.first());
    } else {
        // 사용 가능한 레시피가 없으면 새로 생성
        createNewSimulationRecipe();
    }
}

void TeachingWidget::createNewSimulationRecipe() {
    // 새로운 시뮬레이션 레시피 생성 (빈 상태로 시작)
    clearCurrentRecipe();
    
    // 시뮬레이션용 카메라 정보 설정
    CameraInfo simCameraInfo;
    simCameraInfo.name = "SIM_CAM";
    simCameraInfo.index = -1; // 시뮬레이션용
    simCameraInfo.videoDeviceIndex = -1; // 시뮬레이션용
    cameraInfos.append(simCameraInfo);
}

void TeachingWidget::saveSimulationRecipe() {
    // 시뮬레이션 레시피를 개별 파일로 저장
    if (currentRecipeName.isEmpty()) {
        // 새 레시피 생성 (자동 타임스탬프 이름)
        newRecipe();
    } else {
        // 기존 레시피 업데이트 - 연결된 카메라 정보와 함께 저장
        // 기존 레시피 업데이트 - 기존 saveRecipe 함수 사용
        QString recipeFileName = QString("recipes/%1/%1.xml").arg(currentRecipeName);
        QMap<QString, CalibrationInfo> calibrationMap;
        QStringList simulationImagePaths;
        recipeManager->saveRecipe(recipeFileName, cameraInfos, cameraIndex, calibrationMap, cameraView, simulationImagePaths);
    }
    
    // 현재 카메라 이름을 임시로 SIM_CAM으로 변경
    QString originalCameraName;
    if (!cameraInfos.isEmpty()) {
        originalCameraName = cameraInfos[0].name;
        cameraInfos[0].name = "SIM_CAM";
    }
    
    // 원래 카메라 이름 복원
    if (!cameraInfos.isEmpty() && !originalCameraName.isEmpty()) {
        cameraInfos[0].name = originalCameraName;
    }
}

void TeachingWidget::restoreBackupRecipe() {
    // 백업된 레시피가 있으면 복원
    if (backupRecipeData.contains("recipeName")) {
        QString recipeName = backupRecipeData["recipeName"].toString();
        if (!recipeName.isEmpty()) {
            // 백업된 레시피 로드
            QString recipeFile = recipeName + "_recipe.xml";
            QFile file(recipeFile);
            if (file.exists()) {
                bool success = loadRecipe(recipeFile);
                if (success) {
                } else {
                }
            }
        }
    } else {
        // 백업된 레시피가 없으면 빈 레시피로 시작
        clearCurrentRecipe();
    }
    
    // 백업 데이터 정리
    backupRecipeData.clear();
}

void TeachingWidget::updateUIForSimulationMode(bool isSimulation) {
    // 윈도우 타이틀 업데이트
    QString title = isSimulation ? "MV - 시뮬레이션 모드" : "MV - 라이브 모드";
    setWindowTitle(title);
    
    if (isSimulation) {
        // 시뮬레이션 모드에서는 패턴 편집 기능들을 활성화
        enablePatternEditingFeatures();
    }
}

QString TeachingWidget::getCurrentRecipeName() const {
    // 더 신뢰성 있는 레시피 이름 소스 순서:
    // 1) 백업된 레시피 데이터 (backupRecipeData)
    // 2) 시뮬레이션 다이얼로그에 설정된 레시피명 (simulationDialog)
    // 3) cameraInfos[0].name
    if (backupRecipeData.contains("recipeName")) {
        QString rn = backupRecipeData.value("recipeName").toString();
        if (!rn.isEmpty()) {
            qDebug() << "getCurrentRecipeName: using backupRecipeData.recipeName=" << rn;
            return rn;
        }
    }

    // simulationDialog가 존재하면 그쪽의 현재 레시피명을 우선 조회
    if (simulationDialog) {
        QString simName = simulationDialog->getCurrentRecipeName();
        if (!simName.isEmpty()) {
            return simName;
        }
    }

    // 마지막으로 cameraInfos[0].name 사용
    if (!cameraInfos.isEmpty()) {
        qDebug() << "getCurrentRecipeName: using cameraInfos[0].name=" << cameraInfos[0].name;
        return cameraInfos[0].name;
    }

    qDebug() << "getCurrentRecipeName: no recipe name available";
    return QString(); // 빈 문자열 반환
}

void TeachingWidget::updateCameraInfoForSimulation(const QString& imagePath) {
    // 시뮬레이션 모드에서 카메라 정보를 업데이트
    QFileInfo fileInfo(imagePath);
    
    // 임시로 카메라 정보를 시뮬레이션용으로 변경
    if (!cameraInfos.isEmpty()) {
        cameraInfos[0].name = QString("SIM_CAM (%1)").arg(fileInfo.fileName());
        cameraInfos[0].index = -1; // 시뮬레이션 표시
    }
}

void TeachingWidget::updateCameraInfoForDisconnected() {
    if (cameraView) {
        cameraView->setCurrentCameraUuid("");
    }
}

void TeachingWidget::enablePatternEditingFeatures() {
    // 패턴 편집 관련 모든 버튼들을 활성화
    
    // ROI, FID, INS 버튼들
    if (roiButton) roiButton->setEnabled(true);
    if (fidButton) fidButton->setEnabled(true);
    if (insButton) insButton->setEnabled(true);
    
    // Draw/Move 토글 버튼 활성화
    if (modeToggleButton) modeToggleButton->setEnabled(true);
    
    // RUN 버튼 활성화 (시뮬레이션 모드에서도 테스트 가능)
    if (runStopButton) runStopButton->setEnabled(true);
    
    // 패턴 관리 버튼들 (objectName으로 찾기)
    QPushButton* saveBtn = findChild<QPushButton*>("saveRecipeButton");
    if (saveBtn) saveBtn->setEnabled(true);
    
    QPushButton* addBtn = findChild<QPushButton*>("addPatternButton");
    if (addBtn) addBtn->setEnabled(true);
    
    QPushButton* filterBtn = findChild<QPushButton*>("addFilterButton");
    if (filterBtn) filterBtn->setEnabled(true);
    
    QPushButton* removeBtn = findChild<QPushButton*>("removeButton");
    if (removeBtn) removeBtn->setEnabled(true);
    
    // 시뮬레이션 모드에서는 모든 메뉴도 활성화
    if (cameraSettingsAction) cameraSettingsAction->setEnabled(true);
    if (languageSettingsAction) languageSettingsAction->setEnabled(true);
    if (calibrateAction) calibrateAction->setEnabled(true);
    
    // CameraView 활성화 및 패턴 그리기 모드 설정
    if (cameraView) {
        cameraView->setEnabled(true);
        cameraView->setMouseTracking(true);
        cameraView->setFocusPolicy(Qt::StrongFocus);
        cameraView->setAttribute(Qt::WA_AcceptTouchEvents, true);
        
        // 현재 선택된 패턴 버튼에 따라 Edit 모드 설정
        if (roiButton && roiButton->isChecked()) {
            cameraView->setEditMode(CameraView::EditMode::Draw);
        } else if (fidButton && fidButton->isChecked()) {
            cameraView->setEditMode(CameraView::EditMode::Draw);
        } else if (insButton && insButton->isChecked()) {
            cameraView->setEditMode(CameraView::EditMode::Draw);
        }
        
        cameraView->update();
    }
    
    // 패턴 트리와 관련 위젯들
    if (patternTree) {
        patternTree->setEnabled(true);
    }
    
    // 프로퍼티 패널 활성화
    if (propertyStackWidget) {
        propertyStackWidget->setEnabled(true);
    }
    
    if (filterPropertyContainer) {
        filterPropertyContainer->setEnabled(true);
    }
    
    // 프로퍼티 패널 내 모든 위젯들 활성화
    QList<QSpinBox*> spinBoxes = findChildren<QSpinBox*>();
    for (QSpinBox* spinBox : spinBoxes) {
        spinBox->setEnabled(true);
    }
    
    QList<QDoubleSpinBox*> doubleSpinBoxes = findChildren<QDoubleSpinBox*>();
    for (QDoubleSpinBox* doubleSpinBox : doubleSpinBoxes) {
        doubleSpinBox->setEnabled(true);
    }
    
    QList<QCheckBox*> checkBoxes = findChildren<QCheckBox*>();
    for (QCheckBox* checkBox : checkBoxes) {
        checkBox->setEnabled(true);
    }
    
    QList<QComboBox*> comboBoxes = findChildren<QComboBox*>();
    for (QComboBox* comboBox : comboBoxes) {
        comboBox->setEnabled(true);
    }
    
    // 필터 관련 위젯들도 활성화
    enableFilterWidgets();
}

void TeachingWidget::enableFilterWidgets() {
    // 필터 관련 위젯들을 활성화
    // 이는 시뮬레이션 모드에서 필터 기능을 사용할 수 있게 함
}

void TeachingWidget::onPatternTreeDropCompleted() {
    qDebug() << "=== 패턴 드래그 앤 드롭 완료 ===";
    
    // 현재 트리 구조를 분석하여 부모-자식 관계 변화 감지
    QMap<QUuid, QUuid> newParentRelations;  // 자식ID -> 부모ID
    
    // 최상위 아이템들 확인
    for (int i = 0; i < patternTree->topLevelItemCount(); i++) {
        QTreeWidgetItem* topItem = patternTree->topLevelItem(i);
        QString topIdStr = topItem->data(0, Qt::UserRole).toString();
        QUuid topId = QUuid(topIdStr);
        
        // 자식 아이템들 확인
        for (int j = 0; j < topItem->childCount(); j++) {
            QTreeWidgetItem* childItem = topItem->child(j);
            QString childIdStr = childItem->data(0, Qt::UserRole).toString();
            QUuid childId = QUuid(childIdStr);
            
            // 필터가 아닌 패턴인 경우만 처리
            if (!childItem->data(0, Qt::UserRole + 1).isValid()) {
                newParentRelations[childId] = topId;
            }
        }
    }
    
    // 실제 패턴 데이터에 부모-자식 관계 적용
    bool hasChanges = false;
    for (auto it = newParentRelations.begin(); it != newParentRelations.end(); ++it) {
        QUuid childId = it.key();
        QUuid parentId = it.value();
        
        PatternInfo* childPattern = cameraView->getPatternById(childId);
        PatternInfo* parentPattern = cameraView->getPatternById(parentId);
        
        if (childPattern && parentPattern) {
            // INS가 FID 하위로 가는 경우만 허용
            if (childPattern->type == PatternType::INS && 
                parentPattern->type == PatternType::FID) {
                
                if (childPattern->parentId != parentId) {
                    qDebug() << "패턴 그룹화:" << childPattern->name << "→" << parentPattern->name;
                    childPattern->parentId = parentId;
                    cameraView->updatePatternById(childId, *childPattern);
                    hasChanges = true;
                }
            }
        }
    }
}

PatternInfo* TeachingWidget::findPatternById(const QUuid& patternId) {
    if (!cameraView) return nullptr;
    
    const auto& patterns = cameraView->getPatterns();
    for (auto it = patterns.begin(); it != patterns.end(); ++it) {
        if (it->id == patternId) {
            return const_cast<PatternInfo*>(&(*it));
        }
    }
    
    return nullptr;
}

// 각도 정규화 함수 (-180° ~ +180° 범위로 변환)
double TeachingWidget::normalizeAngle(double angle) {
    // 각도를 0 ~ 360 범위로 먼저 정규화
    while (angle < 0) angle += 360.0;
    while (angle >= 360.0) angle -= 360.0;
    
    // -180 ~ +180 범위로 변환
    if (angle > 180.0) {
        angle -= 360.0;
    }
    
    return angle;
}

// === 레시피 관리 함수들 구현 ===

void TeachingWidget::newRecipe() {
    // 저장되지 않은 변경사항 확인
    if (hasUnsavedChanges) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, 
            "새 레시피", 
            "저장되지 않은 변경사항이 있습니다. 새 레시피를 생성하시겠습니까?",
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        
        if (reply == QMessageBox::Cancel) {
            return;
        } else if (reply == QMessageBox::Yes) {
            saveRecipe();
        }
    }
    
    // 새 레시피 이름 입력받기
    bool ok;
    QString recipeName = QInputDialog::getText(this,
        "새 레시피 생성",
        "레시피 이름을 입력하세요:\n(비어있으면 자동으로 생성됩니다)",
        QLineEdit::Normal,
        "",
        &ok);
    
    if (!ok) {
        return; // 사용자가 취소
    }
    
    // 이름이 비어있으면 자동 생성 (년월일시간초밀리초)
    if (recipeName.trimmed().isEmpty()) {
        QDateTime now = QDateTime::currentDateTime();
        recipeName = now.toString("yyyyMMdd_HHmmss_zzz");
    } else {
        recipeName = recipeName.trimmed();
    }
    
    // 중복 이름 확인
    QStringList existingRecipes = recipeManager->getAvailableRecipes();
    if (existingRecipes.contains(recipeName)) {
        QMessageBox::StandardButton reply = QMessageBox::question(this,
            "레시피 이름 중복",
            QString("'%1' 레시피가 이미 존재합니다. 덮어쓰시겠습니까?").arg(recipeName),
            QMessageBox::Yes | QMessageBox::No);
        
        if (reply != QMessageBox::Yes) {
            return;
        }
    }
    
    // 기존 패턴들 클리어
    if (cameraView) {
        cameraView->clearPatterns();
    }
    if (patternTree) {
        patternTree->clear();
    }
    
    // 새 레시피 상태로 설정
    currentRecipeName = recipeName;
    hasUnsavedChanges = false;
    
    // 새 레시피 파일 즉시 저장 (빈 상태로라도 파일이 있어야 목록에 표시됨) - 기존 saveRecipe 함수 사용
    QString recipeFileName = QString("recipes/%1/%1.xml").arg(recipeName);
    QMap<QString, CalibrationInfo> calibrationMap;
    QStringList simulationImagePaths;
    if (recipeManager->saveRecipe(recipeFileName, cameraInfos, cameraIndex, calibrationMap, cameraView, simulationImagePaths)) {
        // 저장 성공
        
        // 라이브 모드에서 현재 프레임을 레시피 폴더에 저장 (첫 번째 이미지)
        if (!simulationMode) {
            saveCurrentFrameToRecipe();
        }
    } else {
        QMessageBox::warning(this, "저장 실패", 
            QString("새 레시피 파일 생성에 실패했습니다:\n%1").arg(recipeManager->getLastError()));
    }
    
    // 시뮬레이션 다이얼로그가 열려있으면 초기화
    if (simulationDialog) {
        simulationDialog->clearForNewRecipe(); // 새 레시피용 초기화
        // 새 레시피 생성 시 패턴 트리도 클리어
        updatePatternTree();
    }
    
    UIColors::showInformation(this, "새 레시피", 
        QString("새 레시피 '%1'가 생성되었습니다.").arg(recipeName));
}

void TeachingWidget::saveRecipeAs() {
    bool ok;
    QString recipeName = QInputDialog::getText(this,
        "레시피 저장",
        "레시피 이름을 입력하세요:",
        QLineEdit::Normal,
        currentRecipeName,
        &ok);
    
    if (ok && !recipeName.isEmpty()) {
        RecipeManager manager;
        
        // 같은 이름의 레시피가 있는지 확인
        QStringList existingRecipes = manager.getAvailableRecipes();
        if (existingRecipes.contains(recipeName)) {
            QMessageBox::StandardButton reply = QMessageBox::question(this,
                "레시피 저장",
                QString("'%1' 레시피가 이미 존재합니다. 덮어쓰시겠습니까?").arg(recipeName),
                QMessageBox::Yes | QMessageBox::No);
            
            if (reply != QMessageBox::Yes) {
                return;
            }
        }
        
        // 기존 saveRecipe 함수 사용
        QString recipeFileName = QString("recipes/%1/%1.xml").arg(recipeName);
        QMap<QString, CalibrationInfo> calibrationMap;
        QStringList simulationImagePaths;
        if (manager.saveRecipe(recipeFileName, cameraInfos, cameraIndex, calibrationMap, cameraView, simulationImagePaths)) {
            currentRecipeName = recipeName;
            hasUnsavedChanges = false;
            
            // 라이브 모드에서 현재 프레임을 레시피 폴더에 저장
            if (!simulationMode) {
                saveCurrentFrameToRecipe();
            }
            
            QMessageBox::information(this, "레시피 저장", 
                QString("'%1' 레시피가 성공적으로 저장되었습니다.").arg(recipeName));
        } else {
            QMessageBox::critical(this, "레시피 저장 실패", 
                QString("레시피 저장에 실패했습니다:\n%1").arg(manager.getLastError()));
        }
    }
}

QString TeachingWidget::getCurrentSimulationCameraUuid() {
    if (!simulationDialog) {
        qDebug() << "시뮬레이션 다이얼로그가 없습니다.";
        return QString();
    }
    
    // SimulationDialog에서 현재 선택된 카메라 UUID 가져오기
    QString cameraUuid = simulationDialog->getSelectedCameraUuid();
    if (cameraUuid.isEmpty()) {
        qDebug() << "시뮬레이션에서 선택된 카메라가 없습니다.";
    }
    
    return cameraUuid;
}

void TeachingWidget::saveSimulationTeachingImage() {
    if (currentRecipeName.isEmpty()) {
        return;
    }
    
    // 현재 시뮬레이션 카메라 UUID 가져오기
    QString currentCameraUuid = getCurrentSimulationCameraUuid();
    if (currentCameraUuid.isEmpty()) {
        qDebug() << "시뮬레이션 카메라 UUID를 가져올 수 없습니다.";
        return;
    }
    
    // 레시피 이미지 폴더 경로
    QString recipeImagesDir = QString("recipes/%1/teach").arg(currentRecipeName);
    QDir dir(recipeImagesDir);
    if (!dir.exists()) {
        qDebug() << "레시피 이미지 폴더가 존재하지 않습니다:" << recipeImagesDir;
        return;
    }
    
    // 현재 표시된 이미지 파일 찾기 (temp_로 시작하는 최신 파일)
    QStringList filters;
    filters << "temp_*.jpg" << "temp_*.png" << "temp_*.bmp" << "temp_*.jpeg";
    QFileInfoList tempFiles = dir.entryInfoList(filters, QDir::Files, QDir::Time);
    
    QString sourceImagePath;
    if (!tempFiles.isEmpty()) {
        // 가장 최신 임시 파일 사용
        sourceImagePath = tempFiles.first().absoluteFilePath();
        qDebug() << "찾은 최신 임시 파일:" << sourceImagePath;
    } else {
        // 임시 파일이 없으면 현재 CameraView에서 이미지 가져오기
        QPixmap currentPixmap = cameraView->getBackgroundPixmap();
        if (currentPixmap.isNull()) {
            qDebug() << "표시할 이미지가 없습니다.";
            return;
        }
        
        // QPixmap을 cv::Mat으로 변환
        QImage qImg = currentPixmap.toImage();
        cv::Mat currentImage = cv::Mat(qImg.height(), qImg.width(), CV_8UC4, (void*)qImg.constBits(), qImg.bytesPerLine());
        cv::cvtColor(currentImage, currentImage, cv::COLOR_BGRA2BGR);
        
        // 임시로 저장
        sourceImagePath = QString("%1/temp_current.jpg").arg(recipeImagesDir);
        if (!cv::imwrite(sourceImagePath.toStdString(), currentImage)) {
            qDebug() << "현재 이미지 임시 저장 실패:" << sourceImagePath;
            return;
        }
    }
    
    // 해당 카메라의 기존 이미지들 삭제 (1장만 유지하기 위해)
    QStringList existingFilters;
    existingFilters << QString("%1.jpg").arg(currentCameraUuid) 
                   << QString("%1.png").arg(currentCameraUuid)
                   << QString("%1.bmp").arg(currentCameraUuid) 
                   << QString("%1.jpeg").arg(currentCameraUuid);
    QStringList existingFiles = dir.entryList(existingFilters, QDir::Files);
    for (const QString& file : existingFiles) {
        QString filePath = dir.absoluteFilePath(file);
        if (QFile::remove(filePath)) {
            qDebug() << QString("기존 카메라 %1 이미지 삭제: %2").arg(currentCameraUuid).arg(filePath);
        }
    }
    
    // 카메라 UUID로 파일명 생성하여 복사
    QString targetFileName = QString("%1.jpg").arg(currentCameraUuid);
    QString targetPath = dir.absoluteFilePath(targetFileName);
    
    // 기존 대상 파일이 있으면 삭제 (QFile::copy는 덮어쓰기를 하지 않음)
    if (QFile::exists(targetPath)) {
        if (QFile::remove(targetPath)) {
            qDebug() << QString("기존 대상 파일 삭제: %1").arg(targetPath);
        } else {
            qDebug() << QString("기존 대상 파일 삭제 실패: %1").arg(targetPath);
        }
    }
    
    if (QFile::copy(sourceImagePath, targetPath)) {
        qDebug() << QString("시뮬레이션 이미지를 카메라명으로 저장 성공: %1 -> %2").arg(sourceImagePath).arg(targetPath);
        
        // 파일이 제대로 생성되었는지 확인
        if (QFile::exists(targetPath)) {
            qDebug() << QString("카메라 %1의 티칭 이미지 파일 생성 확인: %2").arg(currentCameraUuid).arg(targetPath);
        } else {
            qDebug() << QString("경고: 카메라 %1의 티칭 이미지 파일이 생성되지 않았습니다: %2").arg(currentCameraUuid).arg(targetPath);
        }
    } else {
        qDebug() << QString("시뮬레이션 이미지 저장 실패: %1 -> %2").arg(sourceImagePath).arg(targetPath);
        
        // 실패 원인 분석
        if (!QFile::exists(sourceImagePath)) {
            qDebug() << QString("소스 파일이 존재하지 않습니다: %1").arg(sourceImagePath);
        }
        if (QFile::exists(targetPath)) {
            qDebug() << QString("대상 파일이 이미 존재합니다: %1").arg(targetPath);
        }
    }
    
    // 모든 임시 파일들 삭제 (temp_로 시작하는 파일들)
    QStringList allTempFiles = dir.entryList(filters, QDir::Files);
    for (const QString& tempFile : allTempFiles) {
        QString tempPath = dir.absoluteFilePath(tempFile);
        if (QFile::remove(tempPath)) {
            qDebug() << QString("임시 파일 삭제: %1").arg(tempPath);
        }
    }
    
    // 시뮬레이션 다이얼로그의 이미지 목록 갱신
    if (simulationDialog) {
        simulationDialog->loadRecipeImages(currentRecipeName);
        qDebug() << QString("시뮬레이션 다이얼로그 이미지 목록 갱신 완료: %1").arg(currentRecipeName);
    }
}

void TeachingWidget::saveCurrentFrameToRecipe() {
    if (currentRecipeName.isEmpty()) {
        return;
    }
    
    // 현재 프레임 가져오기
    cv::Mat currentFrame = getCurrentFrame();
    if (currentFrame.empty()) {
        qDebug() << "현재 프레임이 비어있어서 저장할 수 없습니다.";
        return;
    }
    
    // 현재 카메라 UUID 가져오기
    QString cameraUuid;
    if (isValidCameraIndex(cameraIndex)) {
        cameraUuid = getCameraInfo(cameraIndex).uniqueId;
    }
    if (cameraUuid.isEmpty()) {
        cameraUuid = "unknown";
    }
    
    // 레시피 이미지 폴더 경로 생성
    QString recipeImagesDir = QString("recipes/%1/teach").arg(currentRecipeName);
    QDir dir;
    if (!dir.mkpath(recipeImagesDir)) {
        qDebug() << QString("레시피 이미지 폴더 생성 실패: %1").arg(recipeImagesDir);
        return;
    }
    
    // 해당 카메라의 기존 이미지들 삭제 (1장만 유지하기 위해)
    QStringList filters;
    filters << QString("%1.jpg").arg(cameraUuid) << QString("%1.png").arg(cameraUuid) 
            << QString("%1.bmp").arg(cameraUuid) << QString("%1.jpeg").arg(cameraUuid);
    QStringList existingFiles = dir.entryList(filters, QDir::Files);
    for (const QString& file : existingFiles) {
        QString filePath = QString("%1/%2").arg(recipeImagesDir).arg(file);
        if (QFile::remove(filePath)) {
            qDebug() << QString("기존 카메라 %1 이미지 삭제: %2").arg(cameraUuid).arg(filePath);
        }
    }
    
    // 카메라 UUID로만 파일명 생성 (타임스탬프 제거)
    QString fileName = QString("%1.jpg").arg(cameraUuid);
    QString fullPath = QString("%1/%2").arg(recipeImagesDir, fileName);
    
    // OpenCV Mat을 이미지 파일로 저장
    if (cv::imwrite(fullPath.toStdString(), currentFrame)) {
        qDebug() << QString("현재 프레임을 레시피에 저장했습니다: %1").arg(fullPath);
    } else {
        qDebug() << QString("현재 프레임 저장 실패: %1").arg(fullPath);
    }
}

void TeachingWidget::manageRecipes() {
    RecipeManager manager;
    QStringList availableRecipes = manager.getAvailableRecipes();
    
    QDialog dialog(this);
    dialog.setWindowTitle("레시피 관리");
    dialog.setMinimumSize(400, 300);
    
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    
    // 레시피 목록
    QLabel* label = new QLabel("저장된 레시피 목록:");
    layout->addWidget(label);
    
    QListWidget* recipeList = new QListWidget(&dialog);
    recipeList->addItems(availableRecipes);
    layout->addWidget(recipeList);
    
    // 버튼들
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    QPushButton* loadButton = new QPushButton("불러오기");
    QPushButton* deleteButton = new QPushButton("삭제");
    QPushButton* renameButton = new QPushButton("이름 변경");
    QPushButton* closeButton = new QPushButton("닫기");
    
    buttonLayout->addWidget(loadButton);
    buttonLayout->addWidget(deleteButton);
    buttonLayout->addWidget(renameButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    
    layout->addLayout(buttonLayout);
    
    // 버튼 활성화 상태 관리
    auto updateButtonState = [&]() {
        bool hasSelection = recipeList->currentItem() != nullptr;
        loadButton->setEnabled(hasSelection);
        deleteButton->setEnabled(hasSelection);
        renameButton->setEnabled(hasSelection);
    };
    
    connect(recipeList, &QListWidget::itemSelectionChanged, updateButtonState);
    updateButtonState();
    
    // 버튼 이벤트 연결
    connect(loadButton, &QPushButton::clicked, [&]() {
        QListWidgetItem* item = recipeList->currentItem();
        if (item) {
            QString recipeName = item->text();
            dialog.accept();
            onRecipeSelected(recipeName);
        }
    });
    
    connect(deleteButton, &QPushButton::clicked, [&]() {
        QListWidgetItem* item = recipeList->currentItem();
        if (item) {
            QString recipeName = item->text();
            QMessageBox::StandardButton reply = QMessageBox::question(&dialog,
                "레시피 삭제",
                QString("'%1' 레시피를 삭제하시겠습니까?").arg(recipeName),
                QMessageBox::Yes | QMessageBox::No);
            
            if (reply == QMessageBox::Yes) {
                if (manager.deleteRecipe(recipeName)) {
                    delete item;
                    
                    // 현재 삭제된 레시피가 로드되어 있다면 티칭위젯 초기화
                    if (currentRecipeName == recipeName) {
                        // 패턴들 모두 삭제
                        if (cameraView) {
                            cameraView->clearPatterns();
                        }
                        // 패턴 트리 업데이트
                        updatePatternTree();
                        // 현재 레시피 이름 초기화
                        currentRecipeName.clear();
                    }
                    
                    // 시뮬레이션 다이얼로그가 열려있다면 업데이트
                    if (simulationDialog) {
                        simulationDialog->refreshRecipeList();
                    }
                    
                    QMessageBox::information(&dialog, "레시피 삭제", 
                        QString("'%1' 레시피가 삭제되었습니다.").arg(recipeName));
                } else {
                    QMessageBox::critical(&dialog, "레시피 삭제 실패", 
                        QString("레시피 삭제에 실패했습니다:\n%1").arg(manager.getLastError()));
                }
            }
        }
    });
    
    connect(renameButton, &QPushButton::clicked, [&]() {
        QListWidgetItem* item = recipeList->currentItem();
        if (item) {
            QString oldName = item->text();
            bool ok;
            QString newName = QInputDialog::getText(&dialog,
                "레시피 이름 변경",
                "새 레시피 이름을 입력하세요:",
                QLineEdit::Normal,
                oldName,
                &ok);
            
            if (ok && !newName.isEmpty() && newName != oldName) {
                if (manager.renameRecipe(oldName, newName)) {
                    item->setText(newName);
                    
                    // 현재 로드된 레시피가 변경된 레시피라면 이름 업데이트
                    if (currentRecipeName == oldName) {
                        currentRecipeName = newName;
                    }
                    
                    // 시뮬레이션 다이얼로그가 열려있다면 업데이트
                    if (simulationDialog) {
                        simulationDialog->refreshRecipeList();
                    }
                    
                    QMessageBox::information(&dialog, "레시피 이름 변경", 
                        QString("'%1'에서 '%2'로 이름이 변경되었습니다.").arg(oldName, newName));
                } else {
                    QMessageBox::critical(&dialog, "레시피 이름 변경 실패", 
                        QString("레시피 이름 변경에 실패했습니다:\n%1").arg(manager.getLastError()));
                }
            }
        }
    });
    
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    
    dialog.exec();
}

void TeachingWidget::onRecipeSelected(const QString& recipeName) {
    // 저장되지 않은 변경사항 확인
    if (hasUnsavedChanges) {
        QMessageBox::StandardButton reply = QMessageBox::question(this,
            "레시피 불러오기",
            "저장되지 않은 변경사항이 있습니다. 레시피를 불러오시겠습니까?",
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        
        if (reply == QMessageBox::Cancel) {
            return;
        } else if (reply == QMessageBox::Yes) {
            saveRecipe();
        }
    }
    
    RecipeManager manager;
    
    // 시뮬레이션 모드에서는 loadRecipeByName 사용
    if (simulationMode) {
        // 레시피에서 카메라 정보 먼저 읽기
        QStringList recipeCameraUuids = manager.getRecipeCameraUuids(recipeName);
        qDebug() << QString("시뮬레이션 모드 - 레시피 '%1'의 카메라 목록: %2").arg(recipeName).arg(recipeCameraUuids.join(", "));
        
        QVector<PatternInfo> patterns;
        if (manager.loadRecipeByName(recipeName, patterns)) {
            // 기존 패턴들 클리어
            if (cameraView) {
                cameraView->clearPatterns();
            }
            if (patternTree) {
                patternTree->clear();
            }
            
            // 새 패턴들 추가
            for (const PatternInfo& pattern : patterns) {
                if (cameraView) {
                    cameraView->addPattern(pattern);
                }
            }
            
            currentRecipeName = recipeName;
            hasUnsavedChanges = false;
            
            // 최근 사용한 레시피를 ConfigManager에 저장
            ConfigManager::instance()->setLastRecipePath(recipeName);
            ConfigManager::instance()->saveConfig();
            qDebug() << QString("시뮬레이션 모드 - 최근 레시피 로드: %1").arg(recipeName);
            
            // 패턴 동기화 및 트리 업데이트
            syncPatternsFromCameraView();
            updatePatternTree();
            
            // 시뮬레이션 다이얼로그가 열려있으면 레시피 이미지들과 카메라 목록 업데이트
            if (simulationDialog) {
                simulationDialog->updateCameraList(recipeName);
                simulationDialog->loadRecipeImages(recipeName);
            }
            
            QMessageBox::information(this, "레시피 불러오기", 
                QString("'%1' 레시피가 성공적으로 불러와졌습니다.\n카메라: %2개").arg(recipeName).arg(recipeCameraUuids.size()));
        } else {
            QMessageBox::critical(this, "레시피 불러오기 실패", 
                QString("레시피 불러오기에 실패했습니다:\n%1").arg(manager.getLastError()));
        }
    } else {
        // 일반 모드에서는 기존 loadRecipe 사용
        QString recipeFileName = QDir(manager.getRecipesDirectory()).absoluteFilePath(QString("%1/%1.xml").arg(recipeName));
        QMap<QString, CalibrationInfo> calibrationMap;
        if (manager.loadRecipe(recipeFileName, cameraInfos, calibrationMap, cameraView, patternTree)) {
            currentRecipeName = recipeName;
            hasUnsavedChanges = false;
            
            // 최근 사용한 레시피를 ConfigManager에 저장
            ConfigManager::instance()->setLastRecipePath(recipeName);
            ConfigManager::instance()->saveConfig();
            qDebug() << QString("최근 레시피 로드: %1").arg(recipeName);
            
            // 패턴 동기화 및 트리 업데이트
            syncPatternsFromCameraView();
            updatePatternTree();
            
            // 시뮬레이션 다이얼로그가 열려있으면 레시피 이미지들과 카메라 목록 업데이트
            if (simulationDialog) {
                simulationDialog->updateCameraList(recipeName);
                simulationDialog->loadRecipeImages(recipeName);
            }
            
            QMessageBox::information(this, "레시피 불러오기", 
                QString("'%1' 레시피가 성공적으로 불러와졌습니다.").arg(recipeName));
        } else {
            QMessageBox::critical(this, "레시피 불러오기 실패", 
                QString("레시피 불러오기에 실패했습니다:\n%1").arg(manager.getLastError()));
        }
    }
}

void TeachingWidget::selectCameraTeachingImage(const QString& cameraUuid)
{
    if (cameraUuid.isEmpty()) return;
    
    // 시뮬레이션 모드 체크
    if (simulationMode) {
        qDebug() << QString("시뮬레이션 모드 - 카메라 UUID '%1' 선택").arg(cameraUuid);
        
        // 시뮬레이션 모드에서는 카메라 뷰에 직접 설정
        if (cameraView) {
            cameraView->setCurrentCameraUuid(cameraUuid);
            cameraView->setSimulationCameraName(cameraUuid);  // simulationCameraName도 함께 설정
            
            // 현재 카메라 UUID가 설정되었는지 확인
            QString currentUuid = cameraView->getCurrentCameraUuid();
            QString simCameraName = cameraView->getSimulationCameraName();
            qDebug() << QString("시뮬레이션 모드 - 설정된 카메라 UUID: '%1', simulationCameraName: '%2'").arg(currentUuid).arg(simCameraName);
            
            // 패턴들을 해당 카메라 뷰로 다시 그리기
            cameraView->update();
        }
        
        // 패턴 트리 업데이트
        updatePatternTree();
        
        // 선택된 카메라에 실제로 몇 개의 패턴이 있는지 확인
        const QList<PatternInfo>& allPatterns = cameraView->getPatterns();
        int patternCount = 0;
        for (const PatternInfo& pattern : allPatterns) {
            if (pattern.cameraUuid == cameraUuid) {
                patternCount++;
            }
        }
        qDebug() << QString("시뮬레이션 모드 - 카메라 '%1' 선택완료, 해당 카메라 패턴 수: %2개").arg(cameraUuid).arg(patternCount);
        
        qDebug() << QString("시뮬레이션 모드 - 카메라 선택 완료, 패턴 트리 업데이트됨");
        return;
    }
    
    // 일반 모드: 해당 카메라 UUID와 일치하는 카메라 인덱스 찾기
    int targetCameraIndex = -1;
    for (int i = 0; i < cameraInfos.size(); ++i) {
        if (cameraInfos[i].uniqueId == cameraUuid) {
            targetCameraIndex = i;
            break;
        }
    }
    
    if (targetCameraIndex >= 0 && targetCameraIndex != cameraIndex) {
        // 카메라 인덱스 변경
        cameraIndex = targetCameraIndex;
        
        // UI 업데이트
        updateCameraFrame();
        updatePatternTree();
        
        // 카메라 뷰에 현재 카메라 UUID 설정
        if (cameraView) {
            cameraView->setCurrentCameraUuid(cameraUuid);
        }
        
        qDebug() << QString("카메라 티칭 이미지 선택됨: %1 (인덱스: %2)")
                    .arg(cameraUuid).arg(targetCameraIndex);
    }
}