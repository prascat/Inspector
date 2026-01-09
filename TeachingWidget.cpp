#include "TeachingWidget.h"
#include "ImageProcessor.h"
#include "FilterDialog.h"

#include "LanguageSettingsDialog.h"
#include "SerialSettingsDialog.h"
#include "ClientDialog.h"
#include "SerialCommunication.h"
#include "LanguageManager.h"
#include "RecipeManager.h"
#include "ConfigManager.h"
#include "CustomMessageBox.h"
#include "CustomFileDialog.h"
#include "TestDialog.h"
#include <QTimer>
#include <QProgressDialog>
#include <QProcess>
#include <QStorageInfo>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDomDocument>
#include <QDomElement>
#include <QDateTime>
#include <QInputDialog>
#include <QMessageBox>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLocale>
#include <QTcpSocket>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrent>
#include <chrono>
#include <thread>

cv::Mat TeachingWidget::getCurrentFrame() const
{
    // **camOff 모드 처리 - cameraFrames[cameraIndex] 사용**
    if (camOff && cameraIndex >= 0 && cameraIndex < 4 &&
        !cameraFrames[cameraIndex].empty())
    {
        return cameraFrames[cameraIndex].clone();
    }

    // **메인 카메라의 프레임 반환**
    if (cameraIndex >= 0 && cameraIndex < 4 &&
        !cameraFrames[cameraIndex].empty())
    {
        return cameraFrames[cameraIndex].clone();
    }
    return cv::Mat(); // 빈 프레임 반환
}

cv::Mat TeachingWidget::getCurrentFilteredFrame() const
{
    cv::Mat sourceFrame;

    // CAM OFF 모드에서는 currentDisplayFrameIndex 사용, CAM ON 모드에서는 cameraIndex 사용
    int frameIndex = camOff ? currentDisplayFrameIndex : cameraIndex;

    if (frameIndex >= 0 && frameIndex < static_cast<int>(4) &&
        !cameraFrames[frameIndex].empty())
    {
        sourceFrame = cameraFrames[frameIndex].clone();
    }

    if (!sourceFrame.empty())
    {
        // 선택된 필터가 있는 경우에만 적용
        if (!selectedPatternId.isNull() && selectedFilterIndex >= 0)
        {
            // 패턴 찾기
            QList<PatternInfo> allPatterns = cameraView->getPatterns();

            for (const auto &pattern : allPatterns)
            {
                if (pattern.id == selectedPatternId && selectedFilterIndex < pattern.filters.size())
                {
                    const FilterInfo &filter = pattern.filters[selectedFilterIndex];

                    // 회전이 있는 경우: 회전된 사각형 영역에만 필터 적용
                    if (std::abs(pattern.angle) > 0.1)
                    {
                        cv::Point2f center(pattern.rect.x() + pattern.rect.width() / 2.0f,
                                           pattern.rect.y() + pattern.rect.height() / 2.0f);

                        // 1. 회전된 사각형 마스크 생성
                        cv::Mat mask = cv::Mat::zeros(sourceFrame.size(), CV_8UC1);
                        cv::Size2f patternSize(pattern.rect.width(), pattern.rect.height());

                        cv::Point2f vertices[4];
                        cv::RotatedRect rotatedRect(center, patternSize, pattern.angle);
                        rotatedRect.points(vertices);

                        std::vector<cv::Point> points;
                        for (int i = 0; i < 4; i++)
                        {
                            points.push_back(cv::Point(static_cast<int>(std::round(vertices[i].x)),
                                                       static_cast<int>(std::round(vertices[i].y))));
                        }
                        cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{points}, cv::Scalar(255));

                        // 2. 마스크 영역만 복사
                        cv::Mat maskedImage = cv::Mat::zeros(sourceFrame.size(), sourceFrame.type());
                        sourceFrame.copyTo(maskedImage, mask);

                        // 3. 확장된 ROI 계산
                        double width = pattern.rect.width();
                        double height = pattern.rect.height();

                        int rotatedWidth, rotatedHeight;
                        calculateRotatedBoundingBox(width, height, pattern.angle, rotatedWidth, rotatedHeight);

                        int maxSize = std::max(rotatedWidth, rotatedHeight);
                        int halfSize = maxSize / 2;

                        cv::Rect expandedRoi(
                            qBound(0, static_cast<int>(center.x) - halfSize, sourceFrame.cols - 1),
                            qBound(0, static_cast<int>(center.y) - halfSize, sourceFrame.rows - 1),
                            qBound(1, maxSize, sourceFrame.cols - (static_cast<int>(center.x) - halfSize)),
                            qBound(1, maxSize, sourceFrame.rows - (static_cast<int>(center.y) - halfSize)));

                        // 4. 확장된 영역에 필터 적용
                        if (expandedRoi.width > 0 && expandedRoi.height > 0 &&
                            expandedRoi.x + expandedRoi.width <= maskedImage.cols &&
                            expandedRoi.y + expandedRoi.height <= maskedImage.rows)
                        {

                            cv::Mat roiMat = maskedImage(expandedRoi);
                            ImageProcessor processor;
                            cv::Mat filteredRoi;
                            processor.applyFilter(roiMat, filteredRoi, filter);
                            if (!filteredRoi.empty())
                            {
                                filteredRoi.copyTo(roiMat);
                            }
                        }

                        // 5. 마스크 영역만 필터 적용된 결과로 교체 (나머지는 원본 유지)
                        maskedImage.copyTo(sourceFrame, mask);
                    }
                    else
                    {
                        // 회전 없는 경우: rect 영역만 필터 적용
                        cv::Rect roi(
                            qBound(0, static_cast<int>(pattern.rect.x()), sourceFrame.cols - 1),
                            qBound(0, static_cast<int>(pattern.rect.y()), sourceFrame.rows - 1),
                            qBound(1, static_cast<int>(pattern.rect.width()), sourceFrame.cols - static_cast<int>(pattern.rect.x())),
                            qBound(1, static_cast<int>(pattern.rect.height()), sourceFrame.rows - static_cast<int>(pattern.rect.y())));

                        if (roi.width > 0 && roi.height > 0 &&
                            roi.x + roi.width <= sourceFrame.cols && roi.y + roi.height <= sourceFrame.rows)
                        {

                            cv::Mat roiMat = sourceFrame(roi);
                            ImageProcessor processor;
                            cv::Mat filteredRoi;
                            processor.applyFilter(roiMat, filteredRoi, filter);
                            if (!filteredRoi.empty())
                            {
                                filteredRoi.copyTo(roiMat);
                            }
                        }
                    }
                    break;
                }
            }
        }

        return sourceFrame;
    }

    return cv::Mat();
}

CameraGrabberThread::CameraGrabberThread(QObject *parent)
    : QThread(parent), m_cameraIndex(-1), m_stopped(false), m_paused(false) // m_camera 제거
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
            msleep(10);
            continue;
        }

        // **LIVE/INSPECT 모드 변경 감지 및 UserSet 동적 변경**
        TeachingWidget *parent = qobject_cast<TeachingWidget *>(this->parent());

#ifdef USE_SPINNAKER
        // ★ 카메라가 중지되는 중이면 카메라 접근하지 않음
        if (parent && !parent->camOff && parent->m_useSpinnaker && m_cameraIndex >= 0 &&
            m_cameraIndex < static_cast<int>(parent->m_spinCameras.size()))
        {
            try {
                auto spinCamera = parent->m_spinCameras[m_cameraIndex];
                if (spinCamera && spinCamera->IsValid() && spinCamera->IsInitialized())
                {
                    // CameraGrabberThread에서는 UserSet을 자동으로 변경하지 않음
                    // 버튼 클릭(onCameraModeToggled)으로만 변경
                    // 여기서는 현재 설정을 유지하기만 함
                }
            }
            catch (...) {
                // 카메라 접근 실패 무시
            }
        }
#endif

        cv::Mat frame;
        bool grabbed = false;
        bool isTriggerMode = false; // ★ 추가: 트리거 모드 구분

        // **부모 위젯에서 카메라 객체에 직접 접근**
        if (parent && m_cameraIndex >= 0)
        {
            if (parent->isValidCameraIndex(m_cameraIndex))
            {
                CameraInfo info = parent->getCameraInfo(m_cameraIndex);

                // Spinnaker 카메라 처리
                if (info.uniqueId.startsWith("SPINNAKER_"))
                {
#ifdef USE_SPINNAKER
                    // ★ 카메라가 중지되는 중이면 프레임 획득하지 않음
                    if (!parent->camOff && parent->m_useSpinnaker && m_cameraIndex < static_cast<int>(parent->m_spinCameras.size()))
                    {
                        auto spinCamera = parent->m_spinCameras[m_cameraIndex];
                        
                        // ★ 추가 안전 검사: IsValid() 체크 추가
                        if (spinCamera && spinCamera->IsValid() && spinCamera->IsInitialized())
                        {

                            // **트리거 모드 확인**
                            try
                            {
                                Spinnaker::GenApi::INodeMap &nodeMap = spinCamera->GetNodeMap();
                                Spinnaker::GenApi::CEnumerationPtr ptrTriggerMode = nodeMap.GetNode("TriggerMode");
                                Spinnaker::GenApi::CEnumerationPtr ptrAcquisitionMode = nodeMap.GetNode("AcquisitionMode");

                                if (Spinnaker::GenApi::IsReadable(ptrTriggerMode) &&
                                    Spinnaker::GenApi::IsReadable(ptrAcquisitionMode))
                                {

                                    QString triggerModeStr = QString::fromStdString(ptrTriggerMode->GetCurrentEntry()->GetSymbolic().c_str());
                                    QString acqModeStr = QString::fromStdString(ptrAcquisitionMode->GetCurrentEntry()->GetSymbolic().c_str());

                                    // **트리거 모드: 새 트리거 신호 대기**
                                    if (triggerModeStr == "On" && acqModeStr == "SingleFrame")
                                    {
                                        isTriggerMode = true; // ★ 트리거 모드 표시

                                        bool wasStreaming = spinCamera->IsStreaming();
                                        if (!wasStreaming)
                                        {
                                            spinCamera->BeginAcquisition();
                                        }

                                        // 트리거 신호 대기: CameraSettingsDialog와 동일한 방식
                                        Spinnaker::ImagePtr spinImage = nullptr;
                                        try
                                        {
                                            // ★ 1ms 타임아웃 (CameraSettingsDialog와 동일)
                                            spinImage = spinCamera->GetNextImage(1);
                                        }
                                        catch (...)
                                        {
                                            // 타임아웃 예외는 무시
                                            spinImage = nullptr;
                                        }

                                        if (spinImage && !spinImage->IsIncomplete())
                                        {
                                            // ✓ 새로운 트리거 신호로 프레임 획득
                                            try
                                            {
                                                Spinnaker::ImageProcessor processor;
                                                processor.SetColorProcessing(Spinnaker::SPINNAKER_COLOR_PROCESSING_ALGORITHM_DIRECTIONAL_FILTER);
                                                Spinnaker::ImagePtr convertedImage = processor.Convert(spinImage, Spinnaker::PixelFormat_BGR8);

                                                if (convertedImage && !convertedImage->IsIncomplete())
                                                {
                                                    unsigned char *buffer = static_cast<unsigned char *>(convertedImage->GetData());
                                                    size_t width = convertedImage->GetWidth();
                                                    size_t height = convertedImage->GetHeight();
                                                    frame = cv::Mat(height, width, CV_8UC3, buffer).clone();
                                                    grabbed = !frame.empty(); // ✓ 새 트리거 프레임만 검사

                                                    // **트리거 신호 수신 - 검사 자동 시작**
                                                    if (!frame.empty())
                                                    {
                                                        int hwTotal = ++parent->totalHardwareTriggersReceived;
                                                        int hwCam = ++parent->hardwareTriggersPerCamera[m_cameraIndex];
                                                        int serverCount = parent->totalTriggersReceived.load();
                                                        int hwCam0 = parent->hardwareTriggersPerCamera[0].load();
                                                        int hwCam1 = parent->hardwareTriggersPerCamera[1].load();
                                                        
                                                        // ★ 실제 프레임 인덱스를 읽어서 해당 카운트 증가
                                                        int currentFrameIdx = parent->nextFrameIndex[m_cameraIndex].load();
                                                        if (currentFrameIdx >= 0 && currentFrameIdx < 4) {
                                                            parent->serialFrameCount[currentFrameIdx]++;
                                                        }
                                                        
                                                        int f0 = parent->serialFrameCount[0].load();
                                                        int f1 = parent->serialFrameCount[1].load();
                                                        int f2 = parent->serialFrameCount[2].load();
                                                        int f3 = parent->serialFrameCount[3].load();
                                                        
                                                        qDebug().noquote() << QString("[카메라 HW 트리거] 서버:%1(0:%2, 1:%3, 2:%4, 3:%5) | HW전체:%6 (CAM0:%7 CAM1:%8)")
                                                                    .arg(serverCount)
                                                                    .arg(f0).arg(f1).arg(f2).arg(f3)
                                                                    .arg(hwTotal)
                                                                    .arg(hwCam0)
                                                                    .arg(hwCam1);
                                                        emit triggerSignalReceived(frame, m_cameraIndex);
                                                    }
                                                }
                                            }
                                            catch (...)
                                            {
                                                // 변환 실패
                                            }

                                            spinImage->Release();

                                            // ★ CameraSettingsDialog와 동일: SingleFrame 모드에서 다음 트리거 대기를 위해 acquisition 재시작
                                            try
                                            {
                                                if (spinCamera->IsStreaming())
                                                {
                                                    spinCamera->EndAcquisition();
                                                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                                                }
                                                spinCamera->BeginAcquisition();
                                            }
                                            catch (...)
                                            {
                                                // 무시
                                            }
                                        }
                                        else
                                        {
                                            // 타임아웃 또는 불완전한 이미지 - 정상 (트리거 아직 안 받음)
                                            if (spinImage)
                                            {
                                                spinImage->Release();
                                            }

                                            // ★ Streaming 상태 확인 및 필요시 재개
                                            try
                                            {
                                                if (!spinCamera->IsStreaming())
                                                {
                                                    spinCamera->BeginAcquisition();
                                                }
                                            }
                                            catch (...)
                                            {
                                                // 무시
                                            }

                                            grabbed = false;
                                        }
                                    }
                                    else
                                    {
                                        // **LIVE 모드: 계속 프레임 요청**
                                        isTriggerMode = false;
                                        frame = parent->grabFrameFromSpinnakerCamera(spinCamera);
                                        grabbed = !frame.empty();
                                    }
                                }
                                else
                                {
                                    // 모드 확인 실패 → LIVE 모드로 간주
                                    frame = parent->grabFrameFromSpinnakerCamera(spinCamera);
                                    grabbed = !frame.empty();
                                }
                            }
                            catch (Spinnaker::Exception &e)
                            {
                                // 무시
                            }

                            // CAM ON 모드에서는 연속 촬영만 수행 (자동 검사 없음)
                            // 트리거 기반 자동 검사는 별도 기능으로 분리
                        }
                    }
#endif
                }
                // OpenCV 카메라 처리
                // OpenCV capture 제거됨 (Spinnaker SDK만 사용)
                else
                {
                    grabbed = false;
                }
            }
        }

        // ★ 중요: 트리거 모드에서는 frameGrabbed 신호를 발생시키지 않음 (triggerSignalReceived만 사용)
        if (grabbed && !frame.empty() && !isTriggerMode)
        {
            // **라이브 모드에서도 프레임 인덱스 사용 (트리거와 동일)**
            if (m_cameraIndex >= 0 && m_cameraIndex < 2)
            {
                cv::Mat frameCopy = frame.clone();
                int baseFrameIndex = m_cameraIndex * 2;
                
                {
                    QMutexLocker locker(&parent->cameraFramesMutex);
                    // 카메라 0 → 프레임 0, 1 / 카메라 1 → 프레임 2, 3
                    parent->cameraFrames[baseFrameIndex] = frameCopy.clone();     // STRIP
                    parent->cameraFrames[baseFrameIndex + 1] = frameCopy.clone(); // CRIMP
                }
                
                // 미리보기 업데이트 (레시피 없어도 표시)
                QMetaObject::invokeMethod(parent, [parent, baseFrameIndex, frameCopy]() {
                    if (parent->previewOverlayLabels[baseFrameIndex]) {
                        parent->updateSinglePreviewWithFrame(baseFrameIndex, frameCopy);
                    }
                    if (parent->previewOverlayLabels[baseFrameIndex + 1]) {
                        parent->updateSinglePreviewWithFrame(baseFrameIndex + 1, frameCopy);
                    }
                }, Qt::QueuedConnection);
            }

            emit frameGrabbed(frame, m_cameraIndex);
        }

        // LIVE 모드에서는 딜레이 없이 최대한 빠르게
        // 카메라 자체 프레임 레이트가 속도를 제한함
    }
}

// UIUpdateThread 구현
UIUpdateThread::UIUpdateThread(QObject *parent)
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
            msleep(10);
            continue;
        }

        emit updateUI(); // UI 업데이트 시그널 발생

        msleep(CAMERA_INTERVAL);
    }
}

class QObjectEventFilter : public QObject
{
public:
    using FilterFunction = std::function<bool(QObject *, QEvent *)>;

    QObjectEventFilter(FilterFunction filter) : filter(filter) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        return filter(obj, event);
    }

private:
    FilterFunction filter;
};

TeachingWidget::TeachingWidget(int cameraIndex, const QString &cameraStatus, QWidget *parent)
    : QWidget(parent), cameraIndex(cameraIndex), cameraStatus(cameraStatus)
#ifdef USE_SPINNAKER
      ,
      m_useSpinnaker(false)
#endif
{
    // 로딩 프로그레스 다이얼로그 표시
    CustomMessageBox* loadingDialog = CustomMessageBox::showLoading(nullptr, "KM Inspector");
    loadingDialog->updateProgress(5, "언어 시스템 초기화 중...");
    
    // 언어 시스템을 가장 먼저 초기화
    initializeLanguageSystem();

    // cv::Mat 타입을 메타타입으로 등록 (시그널/슬롯에서 사용 가능)
    qRegisterMetaType<cv::Mat>("cv::Mat");

    loadingDialog->updateProgress(10, "카메라 SDK 초기화 중...");

#ifdef USE_SPINNAKER
    // Spinnaker SDK 초기화 시도
    m_useSpinnaker = initSpinnakerSDK();
    if (m_useSpinnaker)
    {
    }
    else
    {
    }
#endif

    loadingDialog->updateProgress(20, "설정 로딩 중...");
    
    // 기본 초기화 및 설정
    initBasicSettings();

    loadingDialog->updateProgress(30, "레시피 관리자 초기화 중...");
    
    // 레시피 관리자 초기화
    recipeManager = new RecipeManager();

    loadingDialog->updateProgress(40, "UI 레이아웃 생성 중...");
    
    // 레이아웃 구성
    QVBoxLayout *mainLayout = createMainLayout();
    QHBoxLayout *contentLayout = createContentLayout();
    mainLayout->addLayout(contentLayout);

    // 왼쪽 패널 (카메라 뷰 및 컨트롤) 설정 - 전체 화면 사용
    QVBoxLayout *cameraLayout = createCameraLayout();
    contentLayout->addLayout(cameraLayout, 1);

    loadingDialog->updateProgress(50, "로그 시스템 초기화 중...");
    
    // 로그 오버레이 생성 (화면 하단)
    setupLogOverlay();

    // 오른쪽 패널 오버레이 생성
    setupRightPanelOverlay();

    loadingDialog->updateProgress(60, "패턴 테이블 설정 중...");
    
    // 패턴 테이블 설정
    setupPatternTree();

    // 프로퍼티 패널 생성
    createPropertyPanels();

    // 카메라 포인터 초기화
    // camera = nullptr;

    // 필터 다이얼로그 초기화
    filterDialog = new FilterDialog(cameraView, -1, this);

    loadingDialog->updateProgress(70, "이벤트 연결 중...");
    
    // 이벤트 연결
    connectEvents();

    // InsProcessor 로그를 오버레이로 연결
    connect(insProcessor, &InsProcessor::logMessage, this, &TeachingWidget::receiveLogMessage);

    uiUpdateThread = new UIUpdateThread(this);

    // UI 업데이트 이벤트 연결
    connect(uiUpdateThread, &UIUpdateThread::updateUI,
            this, &TeachingWidget::updateUIElements, Qt::QueuedConnection);

    // 언어 변경 시그널 연결 (즉시 처리)
    connect(LanguageManager::instance(), &LanguageManager::languageChanged,
            this, &TeachingWidget::updateUITexts, Qt::DirectConnection);

    loadingDialog->updateProgress(80, "레시피 로드 준비 중...");
    
    // 프로그램 시작 시 최근 레시피 자동 로드 (CAM ON/OFF 모두)
    bool recipeLoaded = false;
    QString lastRecipePath = ConfigManager::instance()->getLastRecipePath();
    if (!lastRecipePath.isEmpty())
    {
        // 레시피 경로에서 레시피 이름 추출
        QString recipeName = QFileInfo(lastRecipePath).baseName();
        
        // 레시피 파일 존재 여부 확인
        RecipeManager checkManager;
        QString recipeFilePath = QDir(checkManager.getRecipesDirectory()).absoluteFilePath(QString("%1/%1.xml").arg(recipeName));
        
        if (QFile::exists(recipeFilePath)) {
            loadingDialog->updateProgress(85, "레시피 로딩 중...");
            qDebug() << "[TeachingWidget] 프로그램 시작 시 레시피 자동 로드:" << recipeName << "camOff:" << camOff;
            // 레시피 선택
            onRecipeSelected(recipeName);
            recipeLoaded = true;
            
            // 레시피 로드 완료 확인
            if (cameraView) {
                QList<PatternInfo> patterns = cameraView->getPatterns();
                qDebug() << "[TeachingWidget] 레시피 로드 완료 - 패턴 수:" << patterns.size();
            }
            
            // 4분할 뷰 강제 업데이트
            if (cameraView && cameraView->getQuadViewMode())
            {
                cameraView->setQuadFrames(cameraFrames);
                cameraView->viewport()->update();
                cameraView->repaint();
            }
            updatePreviewFrames();
        }
    }
    
    loadingDialog->updateProgress(95, "UI 준비 중...");
    
    // 로딩 다이얼로그 닫기
    loadingDialog->finishLoading();

    // 전체화면 모드 초기화
    isFullScreenMode = true;                       // 시작할 때 최대화 모드
    windowedGeometry = QRect(100, 100, 1200, 700); // 기본 윈도우 크기

    // Ctrl+F로 전체화면 토글 단축키 설정 (Ubuntu F11 충돌 회피)
    fullscreenShortcut = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(fullscreenShortcut, &QShortcut::activated, this, &TeachingWidget::toggleFullScreenMode);

    // UI 텍스트 초기 갱신
    QTimer::singleShot(100, this, &TeachingWidget::updateUITexts);

    // 초기 UI 상태 설정 (TEACH OFF)
    QTimer::singleShot(50, this, [this]() {
        if (cameraView)
            cameraView->setQuadViewMode(true);
        if (rightPanelOverlay)
            rightPanelOverlay->hide();
        for (int i = 0; i < 4; i++)
        {
            if (previewOverlayLabels[i])
                previewOverlayLabels[i]->hide();
        }
        if (logTextEdit && logTextEdit->parentWidget())
            logTextEdit->parentWidget()->hide();
        
        // TEACH OFF 버튼 빼고 모든 버튼 숨김
        if (modeToggleButton) modeToggleButton->hide();
        if (startCameraButton) startCameraButton->hide();
        if (runStopButton) runStopButton->hide();
        if (saveRecipeButton) saveRecipeButton->hide();
        if (addPatternButton) addPatternButton->hide();
        if (addFilterButton) addFilterButton->hide();
        if (removeButton) removeButton->hide();
        if (roiButton) roiButton->hide();
        if (fidButton) fidButton->hide();
        if (insButton) insButton->hide();
    });

    // ClientDialog 초기화 (자동 연결 처리)
    QTimer::singleShot(1500, this, [this]() {
        ClientDialog::instance()->initialize();
        
        // 프레임 인덱스 수신 시그널 연결
        connect(ClientDialog::instance(), &ClientDialog::frameIndexReceived,
                this, &TeachingWidget::onFrameIndexReceived);
    });

    // 시리얼 통신 자동 연결 (저장된 설정 확인)
    QTimer::singleShot(500, this, [this]() {
        ConfigManager* config = ConfigManager::instance();
        bool autoConnect = config->getSerialAutoConnect();
        
        if (autoConnect && serialCommunication) {
            qDebug() << "[TeachingWidget] 시리얼 통신 자동 연결 시도 시작";
            QString savedPort = config->getSerialPort();
            int savedBaudRate = config->getSerialBaudRate();
            
            if (!savedPort.isEmpty() && savedPort != "사용 가능한 포트 없음") {
                // 표시 이름에서 실제 포트 이름 추출 (괄호 앞 부분)
                QString actualPort = savedPort.split(" (").first();
                qDebug() << "[TeachingWidget] 저장된 설정으로 연결 시도:" << actualPort << "@" << savedBaudRate;
                
                // 직접 연결 시도
                if (serialCommunication->connectToPort(actualPort, savedBaudRate)) {
                    qDebug() << "[TeachingWidget] 시리얼 통신 자동 연결 성공!";
                } else {
                    qDebug() << "[TeachingWidget] 시리얼 통신 자동 연결 실패 - 수동으로 연결해주세요";
                }
            } else {
                qDebug() << "[TeachingWidget] 저장된 시리얼 포트 설정이 없습니다";
            }
        } else {
            qDebug() << "[TeachingWidget] 시리얼 자동 연결이 비활성화되어 있습니다";
        }
    });

    // 프로그램 시작 시 카메라 자동 연결 체크
    if (ConfigManager::instance()->getCameraAutoConnect())
    {
        qDebug() << "[TeachingWidget] 카메라 자동 연결 설정 활성화 - CAM ON 실행 예약";
        QTimer::singleShot(2000, this, [this]()
                           {
            qDebug() << "[TeachingWidget] 카메라 자동 연결 실행";
            startCamera(); });
    }
}

void TeachingWidget::initializeLanguageSystem()
{
    // ConfigManager에서 설정 로드
    ConfigManager::instance()->loadConfig();

    // 언어 파일 경로 찾기
    QStringList possiblePaths = {
        QCoreApplication::applicationDirPath() + "/" + QString(LANGUAGE_FILE),
        QString(LANGUAGE_FILE),
        QString("build/") + QString(LANGUAGE_FILE)};

    QString languageFile;
    for (const QString &path : possiblePaths)
    {
        if (QFile::exists(path))
        {
            languageFile = path;
            break;
        }
    }

    // 언어 파일 로드
    if (!languageFile.isEmpty())
    {
        LanguageManager::instance()->loadLanguage(languageFile);
        // ConfigManager에서 저장된 언어 설정 사용
        QString savedLanguage = ConfigManager::instance()->getLanguage();
        LanguageManager::instance()->setCurrentLanguage(savedLanguage);
    }

    // ConfigManager의 언어 변경 시그널 연결
    // 주의: 중복 연결을 방지하기 위해 먼저 disconnect
    disconnect(ConfigManager::instance(), &ConfigManager::languageChanged,
               this, nullptr);
    connect(ConfigManager::instance(), &ConfigManager::languageChanged,
            this, [this](const QString &newLanguage)
            {
                LanguageManager::instance()->setCurrentLanguage(newLanguage);
            });
}

void TeachingWidget::deleRecipe()
{
    // 현재 카메라 정보 확인
    if (cameraInfos.isEmpty() || cameraIndex < 0 || cameraIndex >= cameraInfos.size())
    {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Warning);
        msgBox.setTitle("레시피 삭제 오류");
        msgBox.setMessage("연결된 카메라가 없습니다.");
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    // 삭제 확인 메시지 표시
    QString cameraName = cameraInfos[cameraIndex].name;
    QString message = QString("현재 카메라(%1)의 모든 패턴과 레시피가 삭제됩니다.\n계속하시겠습니까?").arg(cameraName);

    CustomMessageBox msgBox(this);
    msgBox.setIcon(CustomMessageBox::Question);
    msgBox.setTitle("레시피 삭제 확인");
    msgBox.setMessage(message);
    msgBox.setButtons(QMessageBox::Yes | QMessageBox::No);
    QMessageBox::StandardButton reply = static_cast<QMessageBox::StandardButton>(msgBox.exec());

    if (reply != QMessageBox::Yes)
    {
        return; // 사용자가 취소함
    }

    // 패턴 트리 비우기
    patternTree->clear();

    // 현재 프레임(frameIndex)에 해당하는 모든 패턴 찾기
    QList<QUuid> patternsToRemove;
    int currentFrameIndex = currentDisplayFrameIndex;

    const QList<PatternInfo> &allPatterns = cameraView->getPatterns();
    for (const PatternInfo &pattern : allPatterns)
    {
        if (pattern.frameIndex == currentFrameIndex)
        {
            patternsToRemove.append(pattern.id);
        }
    }

    // 패턴 삭제 (CameraView에서)
    for (const QUuid &id : patternsToRemove)
    {
        cameraView->removePattern(id);
    }

    // 속성 패널 초기화
    if (propertyStackWidget)
    {
        propertyStackWidget->setCurrentIndex(0);
    }

    // **현재 카메라의 패턴만 삭제했으므로 레시피 파일 전체를 삭제하지 않음**
    // 대신 수정된 레시피를 다시 저장
    saveRecipe();

    // 삭제 완료 메시지
    CustomMessageBox msgBoxInfo(this);
    msgBoxInfo.setIcon(CustomMessageBox::Information);
    msgBoxInfo.setTitle("레시피 삭제 완료");
    msgBoxInfo.setMessage(QString("현재 카메라(%1)의 모든 패턴이 삭제되었습니다.\n레시피 파일이 업데이트되었습니다.").arg(cameraName));
    msgBoxInfo.setButtons(QMessageBox::Ok);
    msgBoxInfo.exec();

    // 메인 화면 갱신 - 패턴 없이 현재 프레임 다시 표시
    if (!cameraFrames[currentFrameIndex].empty())
    {
        QImage qImage = InsProcessor::matToQImage(cameraFrames[currentFrameIndex]);
        QPixmap pixmap = QPixmap::fromImage(qImage);
        cameraView->setBackgroundPixmap(pixmap);
    }
    
    // 4분할 미리보기 갱신 - 패턴 오버레이 제거
    for (int i = 0; i < 4; i++)
    {
        if (!cameraFrames[i].empty() && previewOverlayLabels[i])
        {
            QImage qImage = InsProcessor::matToQImage(cameraFrames[i]);
            QPixmap pixmap = QPixmap::fromImage(qImage);
            previewOverlayLabels[i]->setPixmap(pixmap.scaled(
                previewOverlayLabels[i]->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
    
    // 카메라 뷰 업데이트
    cameraView->update();
}

void TeachingWidget::openRecipe(bool autoMode)
{
    QStringList availableRecipes = recipeManager->getAvailableRecipes();

    if (availableRecipes.isEmpty())
    {
        if (!autoMode)
        {
            CustomMessageBox msgBox(this);
            msgBox.setIcon(CustomMessageBox::Information);
            msgBox.setTitle("레시피 없음");
            msgBox.setMessage("사용 가능한 레시피가 없습니다.");
            msgBox.setButtons(QMessageBox::Ok);
            msgBox.exec();
        }
        else
        {
        }
        return;
    }

    QString selectedRecipe;

    if (autoMode)
    {
        // 자동 모드: 최근 레시피 또는 첫 번째 레시피 선택
        QString lastRecipePath = ConfigManager::instance()->getLastRecipePath();

        if (!lastRecipePath.isEmpty() && availableRecipes.contains(lastRecipePath))
        {
            selectedRecipe = lastRecipePath;
        }
        else
        {
            selectedRecipe = availableRecipes.first();
        }
    }
    else
    {
        // 수동 모드: 레시피 관리 다이얼로그 열기 (다이얼로그에서 onRecipeSelected 호출됨)

        manageRecipes(); // 다이얼로그에서 레시피 선택 및 로드 처리
        return;
    }

    // 자동 모드에서만 직접 onRecipeSelected 호출
    if (autoMode)
    {
        // 레시피 파일 존재 여부 확인
        RecipeManager checkManager;
        QString recipeFilePath = QDir(checkManager.getRecipesDirectory()).absoluteFilePath(QString("%1/%1.xml").arg(selectedRecipe));

        if (QFile::exists(recipeFilePath))
        {

            onRecipeSelected(selectedRecipe);
        }
        else
        {
        }
    }
}

void TeachingWidget::initBasicSettings()
{
    insProcessor = new InsProcessor(this);

    // 시리얼 통신 객체 초기화
    serialCommunication = new SerialCommunication(this);
    serialCommunication->setTeachingWidget(this);  // TeachingWidget 포인터 설정
    
    // 시리얼 통신 연결 상태 변경 신호 연결
    connect(serialCommunication, &SerialCommunication::connectionStatusChanged, this, [this](bool connected) {
        if (serialStatusLabel) {
            if (connected) {
                // 연결된 포트 이름 가져오기
                QString portName = ConfigManager::instance()->getSerialPort();
                // "ttyTHS1 (Jetson 내장 UART)" 형식에서 "ttyTHS1"만 추출
                if (portName.contains(" (")) {
                    portName = portName.left(portName.indexOf(" ("));
                }
                serialStatusLabel->setText(QString("📡 시리얼: 연결됨 (%1)").arg(portName));
                serialStatusLabel->setStyleSheet(
                    "QLabel {"
                    "  background-color: rgba(0, 100, 0, 180);"
                    "  color: white;"
                    "  border: 1px solid #555;"
                    "  padding-left: 8px;"
                    "  font-size: 12px;"
                    "}");
            } else {
                serialStatusLabel->setText("📡 시리얼: 미연결");
                serialStatusLabel->setStyleSheet(
                    "QLabel {"
                    "  background-color: rgba(0, 0, 0, 180);"
                    "  color: white;"
                    "  border: 1px solid #555;"
                    "  padding-left: 8px;"
                    "  font-size: 12px;"
                    "}");
            }
        }
    });

    // camOff 모드 초기 설정
    camOff = true;
    cameraIndex = 0;

    // cameraFrames 고정 배열 초기화 (std::array<cv::Mat, 4>로 선언됨)
    frameUpdatedFlags.fill(false);
    lastUsedFrameIndex = -1;  // 마지막 사용 프레임 인덱스 초기화
    
    // ★ 카메라별 다음 프레임 인덱스 초기화
    nextFrameIndex[0] = -1;
    nextFrameIndex[1] = -1;
    totalTriggersReceived = 0;
    totalInspectionsExecuted = 0;

    // 8개 카메라 미리보기를 고려하여 크기 확장
    setMinimumSize(1280, 800);
    patternColors << QColor("#FF5252") << QColor("#448AFF") << QColor("#4CAF50")
                  << QColor("#FFC107") << QColor("#9C27B0") << QColor("#00BCD4")
                  << QColor("#FF9800") << QColor("#607D8B") << QColor("#E91E63");
    setFocusPolicy(Qt::StrongFocus);
    
    // SEG 모델 로드 (CRIMP BARREL 검사용)
    initYoloModel();
}

void TeachingWidget::initYoloModel()
{
    // 실행 파일 경로 기준으로 weights 폴더 찾기
    QString appDir = QCoreApplication::applicationDirPath();
    QString modelPath = appDir + "/weights/best.xml";
    
    // 모델 파일 존재 확인
    QFileInfo modelFile(modelPath);
    if (!modelFile.exists()) {
        qDebug() << "[SEG] 모델 파일을 찾을 수 없음:" << modelPath;
        qDebug() << "[SEG] CRIMP BARREL 검사가 비활성화됩니다.";
        return;
    }
    
    qDebug() << "[SEG] YOLO 모델 비활성화됨 (OpenVINO 제거)";
    // CRIMP BARREL 검사는 더 이상 지원되지 않음
}

QVBoxLayout *TeachingWidget::createMainLayout()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setSpacing(5);

    // 메뉴바 생성
    menuBar = new QMenuBar(this);

    // 파일 메뉴
    fileMenu = menuBar->addMenu(TR("FILE_MENU"));

    // 이미지 저장 액션 추가
    saveImageAction = fileMenu->addAction(TR("SAVE_IMAGE"));
    QAction *loadImageAction = fileMenu->addAction("이미지 추가");
    fileMenu->addSeparator();

    // 종료 액션
    exitAction = fileMenu->addAction(TR("EXIT"));

    // 이미지 추가 액션 연결
    connect(loadImageAction, &QAction::triggered, this, &TeachingWidget::loadTeachingImage);

    // === 레시피 메뉴 추가 ===
    recipeMenu = menuBar->addMenu("레시피");
    recipeMenu->setEnabled(true);

    // 레시피 액션들 생성
    QAction *newRecipeAction = recipeMenu->addAction("새 레시피");
    QAction *saveRecipeAsAction = recipeMenu->addAction("다른 이름으로 저장");
    QAction *saveCurrentRecipeAction = recipeMenu->addAction("현재 레시피 저장");
    recipeMenu->addSeparator();
    QAction *closeRecipeAction = recipeMenu->addAction("현재 레시피 닫기");
    recipeMenu->addSeparator();
    QAction *manageRecipesAction = recipeMenu->addAction("레시피 관리");

    // 레시피 액션들 연결
    connect(newRecipeAction, &QAction::triggered, this, &TeachingWidget::newRecipe);
    connect(saveRecipeAsAction, &QAction::triggered, this, &TeachingWidget::saveRecipeAs);
    connect(saveCurrentRecipeAction, &QAction::triggered, this, &TeachingWidget::saveRecipe);
    connect(closeRecipeAction, &QAction::triggered, this, &TeachingWidget::clearAllRecipeData);
    connect(manageRecipesAction, &QAction::triggered, this, &TeachingWidget::manageRecipes);

    // 테스트 메뉴 추가
    QMenu *testMenu = menuBar->addMenu("테스트");
    testMenu->setEnabled(true);
    testDialogAction = testMenu->addAction("테스트");
    testDialogAction->setEnabled(true);

    // 설정 메뉴
    settingsMenu = menuBar->addMenu(TR("SETTINGS_MENU"));
    settingsMenu->setEnabled(true);

    serialSettingsAction = settingsMenu->addAction(TR("SERIAL_SETTINGS"));
    serialSettingsAction->setEnabled(true);

    serverSettingsAction = settingsMenu->addAction(TR("SERVER_SETTINGS"));
    serverSettingsAction->setEnabled(true);

    languageSettingsAction = settingsMenu->addAction(TR("LANGUAGE_SETTINGS"));
    languageSettingsAction->setEnabled(true);

    modelManagementAction = settingsMenu->addAction("모델 관리");
    modelManagementAction->setEnabled(true);

    // 도움말 메뉴
    helpMenu = menuBar->addMenu(TR("HELP_MENU"));
    helpMenu->setEnabled(true);

    // macOS에서 시스템 메뉴로 인식되지 않도록 설정
    helpMenu->menuAction()->setMenuRole(QAction::NoRole);

    aboutAction = helpMenu->addAction(TR("ABOUT"));
    aboutAction->setEnabled(true);

    // About 액션도 시스템 About으로 인식되지 않도록 설정
    aboutAction->setMenuRole(QAction::NoRole);
    aboutAction->setEnabled(true); // 기본 활성화

    // 메뉴 액션 연결
    connect(saveImageAction, &QAction::triggered, this, &TeachingWidget::saveCurrentImage);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    connect(serverSettingsAction, &QAction::triggered, this, &TeachingWidget::showServerSettings);
    connect(languageSettingsAction, &QAction::triggered, this, &TeachingWidget::openLanguageSettings);
    connect(serialSettingsAction, &QAction::triggered, this, &TeachingWidget::showSerialSettings);
    connect(aboutAction, &QAction::triggered, this, &TeachingWidget::showAboutDialog);
    connect(modelManagementAction, &QAction::triggered, this, &TeachingWidget::showModelManagement);
    connect(testDialogAction, &QAction::triggered, this, &TeachingWidget::showTestDialog);

    // 메뉴바 추가
    layout->setMenuBar(menuBar);
    // 헤더 부분 - 제목과 버튼들
    QHBoxLayout *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(5, 5, 5, 5);
    headerLayout->setSpacing(20);

    // 버튼 폰트 설정
    QFont buttonFont = QFont("Arial", 14, QFont::Bold);

    // 버튼 설정 헬퍼 함수
    auto setupHeaderButton = [&buttonFont](QPushButton *button)
    {
        button->setFont(buttonFont);
    };

    // 1. ROI/FID/INS 패턴 타입 버튼들 - 첫 번째 그룹
    QHBoxLayout *patternTypeLayout = new QHBoxLayout();
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

    // 스타일시트 적용 - 오버레이 스타일
    roiButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::ROI_COLOR, UIColors::ROI_COLOR, roiButton->isChecked()));
    fidButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::FIDUCIAL_COLOR, UIColors::FIDUCIAL_COLOR, fidButton->isChecked()));
    insButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::INSPECTION_COLOR, UIColors::INSPECTION_COLOR, insButton->isChecked()));

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
    QHBoxLayout *toggleButtonLayout = new QHBoxLayout();
    toggleButtonLayout->setSpacing(10);
    toggleButtonLayout->setContentsMargins(0, 0, 0, 0);

    // DRAW/MOVE 모드 토글 버튼
    modeToggleButton = new QPushButton("DRAW", this);
    modeToggleButton->setObjectName("modeToggleButton");
    modeToggleButton->setCheckable(true);
    modeToggleButton->setChecked(true); // 기본값 DRAW 모드
    setupHeaderButton(modeToggleButton);
    modeToggleButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_MOVE_COLOR, UIColors::BTN_DRAW_COLOR, true));

    // TEACH ON/OFF 모드 토글 버튼
    teachModeButton = new QPushButton("TEACH OFF", this);
    teachModeButton->setObjectName("teachModeButton");
    teachModeButton->setCheckable(true);
    teachModeButton->setChecked(false); // 기본값 TEACH OFF
    setupHeaderButton(teachModeButton);
    teachModeButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_TEACH_OFF_COLOR, UIColors::BTN_TEACH_ON_COLOR, false));

    // CAM START/STOP 버튼
    startCameraButton = new QPushButton("CAM OFF", this);
    startCameraButton->setCheckable(true);
    setupHeaderButton(startCameraButton);
    startCameraButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_CAM_OFF_COLOR, UIColors::BTN_CAM_ON_COLOR, false));

    // RUN 버튼 - 일반 푸시 버튼으로 변경
    runStopButton = new QPushButton("RUN", this);
    runStopButton->setObjectName("runStopButton");
    runStopButton->setCheckable(true); // 토글 버튼으로 변경
    setupHeaderButton(runStopButton);
    runStopButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));

    // 토글 버튼 레이아웃에 추가
    toggleButtonLayout->addWidget(modeToggleButton);
    toggleButtonLayout->addWidget(teachModeButton);
    toggleButtonLayout->addWidget(startCameraButton);
    toggleButtonLayout->addWidget(runStopButton);

    // 3. 액션 버튼 그룹 (SAVE, 패턴추가, 패턴삭제, 필터추가) - 세 번째 그룹
    QHBoxLayout *actionButtonLayout = new QHBoxLayout();
    actionButtonLayout->setSpacing(10);
    actionButtonLayout->setContentsMargins(0, 0, 0, 0);

    // SAVE 버튼
    saveRecipeButton = new QPushButton("SAVE", this);
    saveRecipeButton->setObjectName("saveRecipeButton");
    setupHeaderButton(saveRecipeButton);
    saveRecipeButton->setStyleSheet(UIColors::overlayButtonStyle(UIColors::BTN_SAVE_COLOR));

    // 패턴 추가 버튼
    addPatternButton = new QPushButton("ADD", this);
    addPatternButton->setObjectName("addPatternButton");
    setupHeaderButton(addPatternButton);
    addPatternButton->setStyleSheet(UIColors::overlayButtonStyle(UIColors::BTN_ADD_COLOR));

    // 필터 추가 버튼
    addFilterButton = new QPushButton("FILTER", this);
    addFilterButton->setObjectName("addFilterButton");
    setupHeaderButton(addFilterButton);
    addFilterButton->setStyleSheet(UIColors::overlayButtonStyle(UIColors::BTN_FILTER_COLOR));

    // 패턴 삭제 버튼
    removeButton = new QPushButton("DELETE", this);
    removeButton->setObjectName("removeButton");
    removeButton->setEnabled(false);
    setupHeaderButton(removeButton);
    removeButton->setStyleSheet(UIColors::overlayButtonStyle(UIColors::BTN_REMOVE_COLOR));

    if (!removeButton->isEnabled())
    {
        removeButton->setStyleSheet(UIColors::overlayButtonStyle(UIColors::BTN_REMOVE_COLOR));
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
    connect(teachModeButton, &QPushButton::toggled, this, &TeachingWidget::onTeachModeToggled);
    connect(addPatternButton, &QPushButton::clicked, this, &TeachingWidget::addPattern);
    connect(removeButton, &QPushButton::clicked, this, &TeachingWidget::removePattern);
    connect(addFilterButton, &QPushButton::clicked, this, &TeachingWidget::addFilter);

    // 초기 상태 설정 - CAM OFF 상태이므로 편집 버튼들 비활성화
    if (saveRecipeButton)
        saveRecipeButton->setEnabled(false);
    if (addFilterButton)
        addFilterButton->setEnabled(false);
    if (removeButton)
        removeButton->setEnabled(false);

    // 헤더 레이아웃을 메인 레이아웃에 추가하지 않음 (오버레이로 이동)
    // layout->addLayout(headerLayout);

    // 구분선 제거
    // layout->addSpacing(15);
    // QFrame* line = new QFrame(this);
    // line->setFrameShape(QFrame::HLine);
    // line->setFrameShadow(QFrame::Sunken);
    // line->setMinimumHeight(2);
    // layout->addWidget(line);
    // layout->addSpacing(10);

    // contentLayout 추가 (이 부분은 나중에 외부에서 처리)
    // 임시로 반환
    return layout;
}

QHBoxLayout *TeachingWidget::createContentLayout()
{
    QHBoxLayout *layout = new QHBoxLayout();
    layout->setSpacing(5); // 간격 줄이기
    return layout;
}

QVBoxLayout *TeachingWidget::createCameraLayout()
{
    QVBoxLayout *cameraLayout = new QVBoxLayout();
    cameraLayout->setSpacing(0);
    cameraLayout->setContentsMargins(0, 0, 0, 0);

    // 1. 카메라 뷰를 담을 컨테이너 위젯 생성
    QWidget *cameraContainer = new QWidget(this);
    cameraContainer->setObjectName("cameraContainer");
    cameraContainer->setStyleSheet("background-color: black;");
    cameraContainer->setMinimumSize(640, 480);

    // 2. 카메라 뷰 초기화
    cameraView = new CameraView(cameraContainer);
    cameraView->setGeometry(0, 0, 640, 480);
    cameraView->setTeachingWidget(this);  // TeachingWidget 포인터 설정
    cameraView->setTeachOff(true);  // TEACH OFF 상태 초기화

    // 3. 버튼 오버레이 위젯 생성
    QWidget *buttonOverlay = new QWidget(cameraContainer);
    buttonOverlay->setObjectName("buttonOverlay");
    buttonOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    buttonOverlay->setStyleSheet("background-color: transparent;");
    buttonOverlay->setGeometry(0, 0, 640, 60);
    buttonOverlay->raise(); // 맨 위로 올리기

    // 4. 버튼 오버레이 레이아웃
    QHBoxLayout *overlayLayout = new QHBoxLayout(buttonOverlay);
    overlayLayout->setContentsMargins(10, 10, 10, 10);
    overlayLayout->setSpacing(10);

    // 중앙 정렬을 위한 stretch 추가
    overlayLayout->addStretch(1);

    // 5. 패턴 타입 버튼들 추가
    overlayLayout->addWidget(roiButton);
    overlayLayout->addWidget(fidButton);
    overlayLayout->addWidget(insButton);
    overlayLayout->addSpacing(10);

    // 6. 토글 버튼들 추가
    overlayLayout->addWidget(modeToggleButton);
    overlayLayout->addWidget(teachModeButton);
    overlayLayout->addWidget(startCameraButton);
    overlayLayout->addWidget(runStopButton);
    overlayLayout->addSpacing(10);

    // 7. 액션 버튼들 추가
    overlayLayout->addWidget(saveRecipeButton);
    QPushButton *addPatternButton = findChild<QPushButton *>("addPatternButton");
    if (addPatternButton)
        overlayLayout->addWidget(addPatternButton);
    overlayLayout->addWidget(addFilterButton);
    overlayLayout->addWidget(removeButton);

    overlayLayout->addStretch(1);

    // 8. 컨테이너의 resizeEvent에서 위젯 크기 조정
    cameraContainer->installEventFilter(this);

    cameraLayout->addWidget(cameraContainer);

    // 9. 패턴 타입 버튼 초기화
    setupPatternTypeButtons(nullptr);

    // 10. 메인 화면 오른쪽 상단에 미리보기 오버레이 추가
    setupPreviewOverlay();

    return cameraLayout;
}

void TeachingWidget::setupButton(QPushButton *button)
{
    button->setMinimumSize(40, 40);
    button->setMaximumSize(80, 40);
    button->setIconSize(QSize(20, 20));
}

void TeachingWidget::setupPatternTypeButtons(QVBoxLayout *cameraLayout)
{
    if (cameraView)
    {
        cameraView->setEditMode(CameraView::EditMode::Draw);  // 기본 모드: DRAW
        cameraView->setCurrentDrawColor(UIColors::ROI_COLOR); // 초기값: ROI (노란색)
    }

    // 초기 상태: TEACH OFF이므로 티칭 버튼들 비활성화
    setTeachingButtonsEnabled(false);
}

void TeachingWidget::connectButtonEvents(QPushButton *modeToggleButton, QPushButton *saveRecipeButton,
                                         QPushButton *startCameraButton, QPushButton *runStopButton)
{
    connect(modeToggleButton, &QPushButton::toggled, this, [this, modeToggleButton](bool checked)
            {
        if (cameraView) {
            CameraView::EditMode newMode = checked ? CameraView::EditMode::Draw : CameraView::EditMode::Move;
            cameraView->setEditMode(newMode);
            
            // 버튼 텍스트 및 스타일 업데이트
                if (checked) {
                // DRAW 모드
                modeToggleButton->setText(TR("DRAW"));
                // 오렌지색(DRAW)과 블루바이올렛(MOVE) 색상 사용 - DRAW 모드에서는 오렌지색이 적용됨
                modeToggleButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_MOVE_COLOR, UIColors::BTN_DRAW_COLOR, true));
            } else {
                // MOVE 모드
                modeToggleButton->setText(TR("MOVE"));
                // 오렌지색(DRAW)과 블루바이올렛(MOVE) 색상 사용 - MOVE 모드에서는 블루바이올렛이 적용됨
                modeToggleButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_MOVE_COLOR, UIColors::BTN_DRAW_COLOR, false));
            }
        } });

    connect(runStopButton, &QPushButton::toggled, this, [this](bool checked)
            {
        QPushButton* btn = qobject_cast<QPushButton*>(sender());
        if (btn) {
            if (checked) {
                // **RUN 버튼 눌림 - 검사 모드로 전환**
                
                // 1. 기본 안전성 검사
                if (!cameraView || !insProcessor) {
                    btn->blockSignals(true);
                    btn->setChecked(false);
                    btn->blockSignals(false);
                    CustomMessageBox msgBox(this);
    msgBox.setIcon(CustomMessageBox::Warning);
    msgBox.setTitle("오류");
    msgBox.setMessage("시스템이 초기화되지 않았습니다.");
    msgBox.setButtons(QMessageBox::Ok);
    msgBox.exec();
                    return;
                }

                if (camOff) {
                    // 시뮬레이션 모드: 현재 표시된 프레임 확인
                    if (!cameraView || currentDisplayFrameIndex < 0 || currentDisplayFrameIndex >= static_cast<int>(4) || 
                        cameraFrames[currentDisplayFrameIndex].empty()) {
                        btn->blockSignals(true);
                        btn->setChecked(false);
                        btn->blockSignals(false);
                        qDebug() << "[RUN] 현재 표시된 프레임이 없음 (frameIndex=" << currentDisplayFrameIndex << ")";
                        return;
                    }
                } else {
                    // 실제 카메라 모드: 카메라 프레임 확인
                    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(4) || 
                        cameraFrames[cameraIndex].empty()) {
                        btn->blockSignals(true);
                        btn->setChecked(false);
                        btn->blockSignals(false);
                        qDebug() << "[RUN] 카메라 영상이 없어 검사를 건너뜁니다";
                        return;
                    }
                }
                
                // 3. 패턴 확인 (camOn/camOff 동일 처리)
                QList<PatternInfo> patterns = cameraView->getPatterns();
                bool hasEnabledPatterns = false;
                
                // 현재 카메라 UUID와 시리얼 넘버 구하기
                QString targetUuid;
                QString targetSerial;
                if (isValidCameraIndex(cameraIndex)) {
                    targetUuid = getCameraInfo(cameraIndex).uniqueId;
                    targetSerial = getCameraInfo(cameraIndex).serialNumber;
                }
                
                // 패턴 확인 (로그 최소화)
                for (const PatternInfo& pattern : patterns) {
                    // 프레임 인덱스 매칭 확인
                    if (pattern.frameIndex != currentDisplayFrameIndex) {
                        continue;
                    }
                    
                    // frameIndex 기반 매칭 (0,1,2,3)
                    bool isMatch = (pattern.frameIndex == currentDisplayFrameIndex);
                    
                    if (pattern.enabled && isMatch) {
                        hasEnabledPatterns = true;
                        break;
                    }
                }
                
                if (!hasEnabledPatterns) {
                    btn->blockSignals(true);
                    btn->setChecked(false);
                    btn->blockSignals(false);
                    qDebug() << "[RUN] 활성화된 패턴이 없어 검사를 건너뜁니다";
                    return;
                }
                
                QApplication::processEvents();
                
                // **4. 패턴 원본 정보 백업 (검사 중지 시 복원용)**
                originalPatternBackup.clear();
                for (const PatternInfo& pattern : patterns) {
                    originalPatternBackup[pattern.id] = pattern;
                }

                
                // **5. 검사 모드 활성화**
                if (cameraView) {
                    // 검사 실행 전에 CameraView의 프레임 인덱스를 현재 표시 프레임으로 설정
                    cameraView->setCurrentFrameIndex(currentDisplayFrameIndex);
                    cameraView->setInspectionMode(true);
                }
                
                // **7. 검사 실행 - 현재 프레임 또는 시뮬레이션 이미지로**
                try {
                    cv::Mat inspectionFrame;
                    int inspectionCameraIndex;
                    
                    if (camOff) {                
                        // 시뮬레이션 모드: 현재 표시된 프레임 사용
                        {
                            QMutexLocker locker(&cameraFramesMutex);
                            if (currentDisplayFrameIndex < 0 || currentDisplayFrameIndex >= static_cast<int>(4) || 
                                cameraFrames[currentDisplayFrameIndex].empty()) {
                                btn->blockSignals(true);
                                btn->setChecked(false);
                                btn->blockSignals(false);
                                return;
                            }
                            inspectionFrame = cameraFrames[currentDisplayFrameIndex].clone();
                        }
                        // frameIndex를 그대로 사용 (0,1,2,3)
                        inspectionCameraIndex = currentDisplayFrameIndex;
                    } else {
                        // **실제 카메라 모드: 현재 표시된 프레임 사용**
                        // 1. 먼저 cameraFrames에 저장된 프레임이 있는지 확인 (트리거 신호로 저장된 프레임)
                        
                        QMutexLocker locker(&cameraFramesMutex);
                        if (currentDisplayFrameIndex >= 0 && currentDisplayFrameIndex < static_cast<int>(4) && 
                            !cameraFrames[currentDisplayFrameIndex].empty()) {
                            inspectionFrame = cameraFrames[currentDisplayFrameIndex].clone();
                            // frameIndex를 그대로 사용 (0,1,2,3)
                            inspectionCameraIndex = currentDisplayFrameIndex;
                        } 
                        // 2. 저장된 프레임이 없으면 Spinnaker에서 직접 획득 시도
                        else if (m_useSpinnaker && cameraIndex >= 0 && cameraIndex < static_cast<int>(m_spinCameras.size())) {
                            inspectionFrame = grabFrameFromSpinnakerCamera(m_spinCameras[cameraIndex]);
                            
                            if (inspectionFrame.empty()) {
                                btn->blockSignals(true);
                                btn->setChecked(false);
                                btn->blockSignals(false);
                                
                                return;
                            }
                            
                            // BGR 형식으로 저장
                            if (inspectionFrame.channels() == 3) {
                                cv::cvtColor(inspectionFrame, inspectionFrame, cv::COLOR_RGB2BGR);
                            }
                            
                            inspectionCameraIndex = cameraIndex;
                            
                        } else {
                            btn->blockSignals(true);
                            btn->setChecked(false);
                            btn->blockSignals(false);
                            return;
                        }
                    }
                    
                    // 비동기 검사 실행
                    cv::Mat frameCopy = inspectionFrame.clone();
                    int frameIdx = currentDisplayFrameIndex;
                    int camIdx = inspectionCameraIndex;
                    
                    QFuture<void> future = QtConcurrent::run([this, frameCopy, camIdx, frameIdx]() {
                        // 검사 실행 (UI 업데이트 없이) - frameIdx 전달
                        runInspect(frameCopy, camIdx, false, frameIdx);
                        
                        // 결과를 메인 스레드로 전달하여 UI 업데이트
                        QMetaObject::invokeMethod(this, [this, frameIdx]() {
                            if (cameraView) {
                                cameraView->update();
                            }
                        }, Qt::QueuedConnection);
                    });
                    
                    // **8. 버튼 상태 업데이트**
                    btn->setText(TR("STOP"));
                    btn->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_REMOVE_COLOR, QColor("#FF5722"), true));
                    
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
                    CustomMessageBox msgBox(this);
    msgBox.setIcon(CustomMessageBox::Critical);
    msgBox.setTitle("검사 오류");
    msgBox.setMessage("검사 실행 중 알 수 없는 오류가 발생했습니다.");
    msgBox.setButtons(QMessageBox::Ok);
    msgBox.exec();
                    return;
                }
                
            } else {
                // **STOP 버튼 눌림 - 라이브 모드로 복귀**
            
                try {
                    resumeToLiveMode();
                    
                    // 버튼 상태 복원
                    btn->setText(TR("RUN"));
                    btn->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));
                    
                    
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
        } });

    // 저장 버튼 이벤트
    connect(saveRecipeButton, &QPushButton::clicked, this, &TeachingWidget::saveRecipe);

    // 카메라 시작/정지 토글 이벤트
    connect(startCameraButton, &QPushButton::toggled, this, [this](bool checked)
            {
        if (checked) {
            // 카메라 시작 (CAM ON)
            startCamera();
            
            // TEACH 모드에서만 편집 버튼들 활성화
            if (this->saveRecipeButton) this->saveRecipeButton->setEnabled(true);
            if (this->addFilterButton) this->addFilterButton->setEnabled(true); 
        } else {
            // 카메라 중지 (CAM OFF) 
            stopCamera();
            
            // 편집 버튼들 비활성화
            if (this->saveRecipeButton) this->saveRecipeButton->setEnabled(false);
            if (this->addFilterButton) this->addFilterButton->setEnabled(false);
        } });

    // 패턴 타입 버튼 그룹 이벤트
    connect(patternButtonGroup, &QButtonGroup::idClicked, this, [this, modeToggleButton = modeToggleButton](int id)
            {
        currentPatternType = static_cast<PatternType>(id);
        
        // 버튼 스타일 업데이트
        roiButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::ROI_COLOR, UIColors::ROI_COLOR, roiButton->isChecked()));
        fidButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::FIDUCIAL_COLOR, UIColors::FIDUCIAL_COLOR, fidButton->isChecked()));
        insButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::INSPECTION_COLOR, UIColors::INSPECTION_COLOR, insButton->isChecked()));
        
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
        
        // 패턴 버튼이 클릭되면 CameraView를 Draw 모드로 전환 (단, 현재 Move 모드가 아닐 때만)
        if (modeToggleButton->isChecked()) {
            cameraView->setEditMode(CameraView::EditMode::Draw);
        } });

    // 레시피 불러오기 버튼 이벤트 연결
    if (loadRecipeAction)
    {
        connect(loadRecipeAction, &QAction::triggered, this, [this]()
                {
            QString fileName = QFileDialog::getOpenFileName(this, 
                "레시피 불러오기", 
                "", 
                "레시피 파일 (*.config);;모든 파일 (*)");
                
            if (!fileName.isEmpty()) {
                loadRecipe(fileName);
            } });
    }
}

void TeachingWidget::updateFilterParam(const QUuid &patternId, int filterIndex, const QString &paramName, int value)
{
    PatternInfo *pattern = cameraView->getPatternById(patternId);
    if (!pattern || filterIndex < 0 || filterIndex >= pattern->filters.size())
    {
        return;
    }

    // 이전 값과 비교 (변경되었는지 확인)
    int oldValue = pattern->filters[filterIndex].params.value(paramName, -1);
    if (oldValue == value)
    {
        return; // 변경 없으면 종료
    }

    // 필터 파라미터 업데이트
    pattern->filters[filterIndex].params[paramName] = value;

    // 컨투어 필터 특별 처리
    if (pattern->filters[filterIndex].type == FILTER_CONTOUR)
    {
        // 필터가 적용된 현재 프레임 가져오기
        cv::Mat filteredFrame = getCurrentFilteredFrame();
        if (!filteredFrame.empty())
        {
            // ROI 영역 추출
            cv::Rect roi(pattern->rect.x(), pattern->rect.y(),
                         pattern->rect.width(), pattern->rect.height());

            if (roi.x >= 0 && roi.y >= 0 &&
                roi.x + roi.width <= filteredFrame.cols &&
                roi.y + roi.height <= filteredFrame.rows)
            {

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
                for (QVector<QPoint> &contour : contours)
                {
                    for (QPoint &pt : contour)
                    {
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

    // ★ 필터 파라미터 변경 시 필터 선택 상태라면 필터 적용 결과 다시 보여주기
    if (!selectedPatternId.isNull() && selectedFilterIndex >= 0 && selectedPatternId == patternId && selectedFilterIndex == filterIndex)
    {
        // 필터 선택 상태이므로 필터 적용된 결과를 다시 렌더링
        PatternInfo *parentPattern = cameraView->getPatternById(patternId);
        if (parentPattern && filterIndex < parentPattern->filters.size())
        {
            const FilterInfo &filter = parentPattern->filters[filterIndex];
            
            int frameIndex = camOff ? currentDisplayFrameIndex : cameraIndex;
            if (frameIndex >= 0 && frameIndex < 4 && !cameraFrames[frameIndex].empty())
            {
                cv::Mat sourceFrame = cameraFrames[frameIndex].clone();
                
                // 회전이 있는 경우: 회전된 사각형 영역에만 필터 적용
                if (std::abs(parentPattern->angle) > 0.1)
                {
                    cv::Point2f center(parentPattern->rect.x() + parentPattern->rect.width() / 2.0f,
                                      parentPattern->rect.y() + parentPattern->rect.height() / 2.0f);

                    cv::Mat mask = cv::Mat::zeros(sourceFrame.size(), CV_8UC1);
                    cv::Size2f patternSize(parentPattern->rect.width(), parentPattern->rect.height());

                    cv::Point2f vertices[4];
                    cv::RotatedRect rotatedRect(center, patternSize, parentPattern->angle);
                    rotatedRect.points(vertices);

                    std::vector<cv::Point> points;
                    for (int i = 0; i < 4; i++)
                    {
                        points.push_back(cv::Point(static_cast<int>(std::round(vertices[i].x)),
                                                  static_cast<int>(std::round(vertices[i].y))));
                    }
                    cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{points}, cv::Scalar(255));

                    cv::Mat maskedImage = cv::Mat::zeros(sourceFrame.size(), sourceFrame.type());
                    sourceFrame.copyTo(maskedImage, mask);

                    double width = parentPattern->rect.width();
                    double height = parentPattern->rect.height();

                    int rotatedWidth, rotatedHeight;
                    calculateRotatedBoundingBox(width, height, parentPattern->angle, rotatedWidth, rotatedHeight);

                    int maxSize = std::max(rotatedWidth, rotatedHeight);
                    int halfSize = maxSize / 2;

                    cv::Rect expandedRoi(
                        qBound(0, static_cast<int>(center.x) - halfSize, sourceFrame.cols - 1),
                        qBound(0, static_cast<int>(center.y) - halfSize, sourceFrame.rows - 1),
                        qBound(1, maxSize, sourceFrame.cols - (static_cast<int>(center.x) - halfSize)),
                        qBound(1, maxSize, sourceFrame.rows - (static_cast<int>(center.y) - halfSize)));

                    if (expandedRoi.width > 0 && expandedRoi.height > 0 &&
                        expandedRoi.x + expandedRoi.width <= maskedImage.cols &&
                        expandedRoi.y + expandedRoi.height <= maskedImage.rows)
                    {
                        cv::Mat roiMat = maskedImage(expandedRoi);
                        ImageProcessor processor;
                        cv::Mat filteredRoi;
                        processor.applyFilter(roiMat, filteredRoi, filter);
                        if (!filteredRoi.empty())
                        {
                            filteredRoi.copyTo(roiMat);
                        }
                    }

                    maskedImage.copyTo(sourceFrame, mask);
                }
                else
                {
                    cv::Rect roi(
                        qBound(0, static_cast<int>(parentPattern->rect.x()), sourceFrame.cols - 1),
                        qBound(0, static_cast<int>(parentPattern->rect.y()), sourceFrame.rows - 1),
                        qBound(1, static_cast<int>(parentPattern->rect.width()), sourceFrame.cols - static_cast<int>(parentPattern->rect.x())),
                        qBound(1, static_cast<int>(parentPattern->rect.height()), sourceFrame.rows - static_cast<int>(parentPattern->rect.y())));

                    if (roi.width > 0 && roi.height > 0)
                    {
                        cv::Mat roiMat = sourceFrame(roi);
                        ImageProcessor processor;
                        cv::Mat filteredRoi;
                        processor.applyFilter(roiMat, filteredRoi, filter);
                        if (!filteredRoi.empty())
                        {
                            filteredRoi.copyTo(roiMat);
                        }
                    }
                }
                
                cv::Mat rgbFrame;
                cv::cvtColor(sourceFrame, rgbFrame, cv::COLOR_BGR2RGB);
                QImage image(rgbFrame.data, rgbFrame.cols, rgbFrame.rows,
                            rgbFrame.step, QImage::Format_RGB888);
                QPixmap pixmap = QPixmap::fromImage(image.copy());
                
                cameraView->setBackgroundPixmap(pixmap);
                cameraView->viewport()->update();
            }
        }
    }
    else
    {
        // 실시간 필터 적용을 위한 화면 업데이트 (필터 선택 상태가 아닐 때)
        updateCameraFrame();
    }

    // 모든 패턴의 템플릿 이미지 실시간 갱신 (필터 변경으로 인한 영향을 고려)
    updateAllPatternTemplateImages();

    // 필터 조정 완료
    setFilterAdjusting(false);

    // 필터 상태 텍스트 업데이트 (트리 아이템)
    QTreeWidgetItem *selectedItem = patternTree->currentItem();
    if (selectedItem)
    {
        selectedItem->setText(2, getFilterParamSummary(pattern->filters[filterIndex]));
    }
}

void TeachingWidget::setupLogOverlay()
{
    if (!cameraView)
        return;

    // ConfigManager에서 설정 로드
    QRect savedGeometry = ConfigManager::instance()->getLogPanelGeometry();
    bool savedCollapsed = ConfigManager::instance()->getLogPanelCollapsed();

    // 로그 오버레이 위젯 생성 - cameraView에 붙임
    logOverlayWidget = new QWidget(cameraView);
    
    // 저장된 크기가 있으면 적용, 없으면 기본값 (위치는 나중에 설정)
    if (savedGeometry.isValid() && savedGeometry.width() > 0 && savedGeometry.height() > 0) {
        logOverlayWidget->resize(savedGeometry.width(), savedGeometry.height());
    } else {
        logOverlayWidget->resize(800, 144);
    }
    logOverlayWidget->setStyleSheet(
        "QWidget {"
        "  background-color: rgba(0, 0, 0, 180);"
        "  border: 2px solid rgba(100, 100, 100, 150);"
        "}");
    logOverlayWidget->installEventFilter(this);
    
    // 마우스 추적 활성화 (드래그/리사이즈용)
    logOverlayWidget->setMouseTracking(true);
    logOverlayWidget->setAttribute(Qt::WA_Hover, true);

    QVBoxLayout *logLayout = new QVBoxLayout(logOverlayWidget);
    logLayout->setContentsMargins(5, 5, 5, 5);
    logLayout->setSpacing(2);

    // 로그 텍스트 표시 (최근 5줄만)
    logTextEdit = new QTextEdit(logOverlayWidget);
    logTextEdit->setReadOnly(true);
    logTextEdit->setStyleSheet(
        "QTextEdit {"
        "  background-color: transparent;"
        "  color: white;"
        "  border: none;"
        "  font-family: 'Courier New';"
        "  font-size: 12px;"
        "}"
        "QMenu {"
        "  background-color: white;"
        "  color: black;"
        "  border: 1px solid #c0c0c0;"
        "}"
        "QMenu::item:selected {"
        "  background-color: #e0e0e0;"
        "}");
    logTextEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    logTextEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    // 로그창 우클릭 메뉴 (Copy, Select All, Clear)
    logTextEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(logTextEdit, &QTextEdit::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu contextMenu(this);
        
        QAction *copyAction = contextMenu.addAction("Copy");
        connect(copyAction, &QAction::triggered, this, [this]() {
            logTextEdit->copy();
        });
        
        QAction *selectAllAction = contextMenu.addAction("Select All");
        connect(selectAllAction, &QAction::triggered, this, [this]() {
            logTextEdit->selectAll();
        });
        
        contextMenu.addSeparator();
        
        QAction *clearAction = contextMenu.addAction("Clear");
        connect(clearAction, &QAction::triggered, this, [this]() {
            logMessages.clear();
            logTextEdit->clear();
        });
        
        contextMenu.exec(logTextEdit->mapToGlobal(pos));
    });
    
    logLayout->addWidget(logTextEdit);

    // 드래그 및 리사이즈를 위한 변수 초기화
    logDragging = false;
    logResizing = false;
    
    // 오버레이 표시
    logOverlayWidget->show();
    logOverlayWidget->raise();

    // 초기 위치 설정 (저장된 위치가 있으면 적용, 없으면 기본 위치)
    // cameraView가 완전히 초기화될 때까지 대기
    QTimer::singleShot(500, this, [this, savedGeometry]() {
        if (savedGeometry.isValid() && savedGeometry.width() > 0 && savedGeometry.height() > 0) {
            // cameraView 기준 상대 좌표로 복원
            logOverlayWidget->setGeometry(savedGeometry);
        } else {
            updateLogOverlayPosition();
        }
    });
}

void TeachingWidget::setupStatusPanel()
{
    if (!cameraView)
        return;

    // 서버 연결 상태 레이블
    serverStatusLabel = new QLabel(cameraView);
    serverStatusLabel->setFixedSize(240, 30);
    serverStatusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    serverStatusLabel->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(0, 0, 0, 180);"
        "  color: white;"
        "  border: 1px solid #555;"
        "  padding-left: 8px;"
        "  font-size: 12px;"
        "}");
    serverStatusLabel->setText("🌐 서버: 미연결");
    serverStatusLabel->raise();

    // 시리얼 통신 상태 레이블
    serialStatusLabel = new QLabel(cameraView);
    serialStatusLabel->setFixedSize(240, 30);
    serialStatusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    serialStatusLabel->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(0, 0, 0, 180);"
        "  color: white;"
        "  border: 1px solid #555;"
        "  padding-left: 8px;"
        "  font-size: 12px;"
        "}");
    serialStatusLabel->setText("📡 시리얼: 미연결");
    serialStatusLabel->raise();

    // 디스크 용량 레이블
    diskSpaceLabel = new QLabel(cameraView);
    diskSpaceLabel->setFixedSize(240, 30);
    diskSpaceLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    diskSpaceLabel->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(0, 0, 0, 180);"
        "  color: white;"
        "  border: 1px solid #555;"
        "  padding-left: 8px;"
        "  font-size: 12px;"
        "}");
    diskSpaceLabel->setText("💾 디스크: 계산 중...");
    diskSpaceLabel->raise();

    // 픽셀 정보 레이블
    pixelInfoLabel = new QLabel(cameraView);
    pixelInfoLabel->setFixedSize(240, 30);
    pixelInfoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    pixelInfoLabel->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(0, 0, 0, 180);"
        "  color: white;"
        "  border: 1px solid #555;"
        "  padding-left: 8px;"
        "  font-size: 12px;"
        "}");
    pixelInfoLabel->setText("🖱️ 픽셀: (0,0) RGB(0,0,0)");
    pixelInfoLabel->raise();

    // 초기 위치 설정
    updateStatusPanelPosition();

    // 상태 업데이트 타이머 (1초마다)
    statusUpdateTimer = new QTimer(this);
    connect(statusUpdateTimer, &QTimer::timeout, this, &TeachingWidget::updateStatusPanel);
    statusUpdateTimer->start(1000);

    // 초기 상태 업데이트
    updateStatusPanel();
}

void TeachingWidget::setupPreviewOverlay()
{
    if (!cameraView)
        return;

    // 메인 화면 오른쪽에 4개 미리보기 레이블 생성 (세로 배치)
    const int previewWidth = 240;
    const int previewHeight = 180;
    const QStringList labels = {"STAGE 1 - STRIP", "STAGE 1 - CRIMP", "STAGE 2 - STRIP", "STAGE 2 - CRIMP"};
    
    int rightMargin = 10;
    int topMargin = 70;
    int spacing = 5;
    int previewX = cameraView->width() - previewWidth - rightMargin;
    int previewY = topMargin;
    
    for (int i = 0; i < 4; i++)
    {
        previewOverlayLabels[i] = new QLabel(cameraView);
        previewOverlayLabels[i]->setFixedSize(previewWidth, previewHeight);
        previewOverlayLabels[i]->setAlignment(Qt::AlignTop | Qt::AlignLeft);  // 왼쪽 상단 정렬
        previewOverlayLabels[i]->setText("  " + labels[i]);  // 초기 텍스트 설정 (여백 추가)
        // rgba 대신 rgb 사용 (파싱 오류 방지)
        previewOverlayLabels[i]->setStyleSheet(
            "QLabel {"
            "  background-color: rgb(0, 0, 0);"
            "  color: white;"
            "  border: 2px solid #555;"
            "  font-size: 14px;"
            "  font-weight: bold;"
            "}");
        previewOverlayLabels[i]->setWindowOpacity(0.8);
        previewOverlayLabels[i]->setCursor(Qt::PointingHandCursor);
        previewOverlayLabels[i]->move(previewX, previewY);  // 초기 위치 설정
        previewOverlayLabels[i]->raise();
        previewOverlayLabels[i]->installEventFilter(this);
        
        previewY += previewHeight + spacing;
    }
    
    // 하위 호환성을 위해 첫 번째를 기존 포인터로도 참조
    previewOverlayLabel = previewOverlayLabels[0];

    // cameraView 크기 변경 시 미리보기 위치 재조정
    cameraView->installEventFilter(this);

    // 상태 표시 패널 생성 (미리보기 아래)
    setupStatusPanel();
}

void TeachingWidget::setupRightPanelOverlay()
{
    // ConfigManager에서 설정 로드
    QRect savedGeometry = ConfigManager::instance()->getPropertyPanelGeometry();
    bool savedCollapsed = ConfigManager::instance()->getPropertyPanelCollapsed();
    int savedExpandedHeight = ConfigManager::instance()->getPropertyPanelExpandedHeight();
    
    // 오른쪽 패널 오버레이 위젯 생성
    rightPanelOverlay = new QWidget(this);

    // 반투명 배경 적용
    rightPanelOverlay->setAutoFillBackground(true);

    // 팔레트로 반투명 배경 설정
    QPalette palette = rightPanelOverlay->palette();
    palette.setColor(QPalette::Window, QColor(30, 30, 30, 200));
    rightPanelOverlay->setPalette(palette);

    rightPanelOverlay->setStyleSheet(
        "QWidget#rightPanelOverlay {"
        "  background-color: rgba(30, 30, 30, 200);"
        "  border: 2px solid rgba(100, 100, 100, 150);"
        "}"
        "QLineEdit {"
        "  background-color: rgba(50, 50, 50, 180);"
        "  color: white;"
        "  border: 1px solid rgba(100, 100, 100, 150);"
        "  padding: 2px;"
        "}"
        "QSpinBox, QDoubleSpinBox {"
        "  background-color: rgba(50, 50, 50, 180);"
        "  color: white;"
        "  border: 1px solid rgba(100, 100, 100, 150);"
        "  padding: 2px;"
        "}"
        "QSpinBox::up-button, QDoubleSpinBox::up-button {"
        "  background-color: rgba(70, 70, 70, 180);"
        "  border: 1px solid rgba(100, 100, 100, 150);"
        "}"
        "QSpinBox::down-button, QDoubleSpinBox::down-button {"
        "  background-color: rgba(70, 70, 70, 180);"
        "  border: 1px solid rgba(100, 100, 100, 150);"
        "}"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {"
        "  image: none;"
        "  border-left: 4px solid transparent;"
        "  border-right: 4px solid transparent;"
        "  border-bottom: 5px solid white;"
        "  width: 0px;"
        "  height: 0px;"
        "}"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {"
        "  image: none;"
        "  border-left: 4px solid transparent;"
        "  border-right: 4px solid transparent;"
        "  border-top: 5px solid white;"
        "  width: 0px;"
        "  height: 0px;"
        "}"
        "QComboBox {"
        "  background-color: rgba(50, 50, 50, 180);"
        "  color: white;"
        "  border: 1px solid rgba(100, 100, 100, 150);"
        "  padding: 2px;"
        "}"
        "QComboBox::drop-down {"
        "  border: 1px solid rgba(100, 100, 100, 150);"
        "}"
        "QComboBox::down-arrow {"
        "  image: none;"
        "  border-left: 4px solid transparent;"
        "  border-right: 4px solid transparent;"
        "  border-top: 5px solid white;"
        "  width: 0px;"
        "  height: 0px;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background-color: rgba(50, 50, 50, 230);"
        "  color: white;"
        "  selection-background-color: rgba(0, 120, 215, 180);"
        "  selection-color: white;"
        "}"
        "QCheckBox {"
        "  color: white;"
        "}"
        "QSlider::groove:horizontal {"
        "  background: rgba(100, 100, 100, 150);"
        "  height: 6px;"
        "}"
        "QSlider::handle:horizontal {"
        "  background: rgba(0, 120, 215, 200);"
        "  width: 14px;"
        "}"
        "QTreeWidget, QTextEdit {"
        "  background-color: transparent;"
        "  color: white;"
        "}"
        "QPushButton#collapseButton {"
        "  background-color: rgba(70, 70, 70, 200);"
        "  color: white;"
        "  border: 1px solid rgba(100, 100, 100, 150);"
        "  padding: 2px 5px;"
        "  font-weight: bold;"
        "}"
        "QPushButton#collapseButton:hover {"
        "  background-color: rgba(90, 90, 90, 220);"
        "}");
    rightPanelOverlay->setObjectName("rightPanelOverlay");

    // 메인 레이아웃
    QVBoxLayout *mainLayout = new QVBoxLayout(rightPanelOverlay);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(0);

    // 상단 헤더 (접기/펼치기 버튼)
    QHBoxLayout *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(5, 5, 5, 5);

    QLabel *titleLabel = new QLabel("Properties");
    titleLabel->setStyleSheet("color: white; font-weight: bold; font-size: 12px;");

    rightPanelCollapseButton = new QPushButton("▼");
    rightPanelCollapseButton->setObjectName("collapseButton");
    rightPanelCollapseButton->setFixedSize(24, 24);
    rightPanelCollapseButton->setToolTip("접기/펼치기");

    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(rightPanelCollapseButton);

    mainLayout->addLayout(headerLayout);

    // 컨텐츠 영역 (접기 가능)
    rightPanelContent = new QWidget();
    rightPanelLayout = new QVBoxLayout(rightPanelContent);
    rightPanelLayout->setContentsMargins(5, 5, 5, 5);
    rightPanelLayout->setSpacing(5);

    mainLayout->addWidget(rightPanelContent);

    // 접기 버튼 연결
    connect(rightPanelCollapseButton, &QPushButton::clicked, this, [this]()
            {
        rightPanelCollapsed = !rightPanelCollapsed;
        rightPanelContent->setVisible(!rightPanelCollapsed);
        rightPanelCollapseButton->setText(rightPanelCollapsed ? "▲" : "▼");
        
        if (rightPanelCollapsed) {
            // 접을 때: 현재 높이 저장 후 최소 높이로
            rightPanelExpandedHeight = rightPanelOverlay->height();
            rightPanelOverlay->setFixedHeight(40);
        } else {
            // 펼칠 때: 저장된 높이로 복원
            rightPanelOverlay->setMinimumHeight(200);
            rightPanelOverlay->setMaximumHeight(QWIDGETSIZE_MAX);
            rightPanelOverlay->resize(rightPanelOverlay->width(), rightPanelExpandedHeight);
        }
        
        // ConfigManager에 접힘 상태 저장
        ConfigManager::instance()->setPropertyPanelCollapsed(rightPanelCollapsed);
        ConfigManager::instance()->setPropertyPanelExpandedHeight(rightPanelExpandedHeight);
    });

    // 초기 크기 설정 - 저장된 설정이 있으면 적용
    rightPanelOverlay->setMinimumWidth(250);
    if (savedGeometry.isValid() && savedGeometry.width() > 0) {
        rightPanelOverlay->resize(savedGeometry.width(), savedExpandedHeight > 0 ? savedExpandedHeight : 600);
        rightPanelOverlay->move(savedGeometry.x(), savedGeometry.y());
        rightPanelExpandedHeight = savedExpandedHeight > 0 ? savedExpandedHeight : 600;
    } else {
        rightPanelOverlay->resize(400, 600);
        rightPanelExpandedHeight = 600;
    }
    
    // 저장된 접힘 상태 적용
    rightPanelCollapsed = savedCollapsed;
    rightPanelContent->setVisible(!rightPanelCollapsed);
    rightPanelCollapseButton->setText(rightPanelCollapsed ? "▲" : "▼");
    if (rightPanelCollapsed) {
        rightPanelOverlay->setFixedHeight(40);
    }

    rightPanelOverlay->raise();
    rightPanelOverlay->show();

    // 드래그 및 리사이즈를 위한 이벤트 필터 설치
    rightPanelOverlay->installEventFilter(this);
    rightPanelDragPos = QPoint();
    rightPanelDragging = false;
    rightPanelResizing = false;
    rightPanelResizeEdge = ResizeEdge::None;

    // 마우스 추적 활성화 (리사이즈 커서 변경용)
    rightPanelOverlay->setMouseTracking(true);
    rightPanelOverlay->setAttribute(Qt::WA_Hover, true);

    // 초기 위치 설정 (저장된 위치가 없을 때만)
    if (!savedGeometry.isValid() || savedGeometry.width() <= 0) {
        QTimer::singleShot(100, this, &TeachingWidget::updateLogOverlayPosition);
    }
}

QVBoxLayout *TeachingWidget::createRightPanel()
{
    // 더미 레이아웃 반환 (기존 코드 호환성)
    QVBoxLayout *layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    return layout;
}

void TeachingWidget::setupPatternTree()
{
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

    // 패턴 트리 스타일 설정 (어두운 배경, 흰색 글자)
    patternTree->setStyleSheet(
        "QTreeWidget { "
        "   background-color: rgb(50, 50, 50); "
        "   color: white; "
        "   alternate-background-color: rgb(60, 60, 60); "
        "} "
        "QTreeWidget::item { "
        "   color: white; "
        "   background-color: transparent; "
        "} "
        "QTreeWidget::item:selected { "
        "   background-color: rgba(0, 120, 215, 150); "
        "   color: white; "
        "} "
        "QTreeWidget::item:hover { "
        "   background-color: rgba(255, 255, 255, 30); "
        "} "
        "QHeaderView::section { "
        "   background-color: rgb(40, 40, 40); "
        "   color: white; "
        "   border: 1px solid rgb(80, 80, 80); "
        "   padding: 4px; "
        "}");

    // 헤더 텍스트 중앙 정렬 설정
    QHeaderView *header = patternTree->header();
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

QPushButton *TeachingWidget::createActionButton(const QString &text, const QString &color, const QFont &font)
{
    QPushButton *button = new QPushButton(text, this);
    button->setMinimumHeight(40);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setFont(font);

    QString hoverColor = color;
    hoverColor.replace("#", "#"); // 이 부분은 원하는 hover 색상으로 변경 가능

    button->setStyleSheet(
        "QPushButton { "
        "   background-color: " +
        color + "; "
                "   color: white; "
                "   border: 1px solid #a0a0a0; "
                "   padding: 8px; "
                "}"
                "QPushButton:hover { background-color: " +
        hoverColor + "; }"
                     "QPushButton:disabled { background-color: #BDBDBD; color: white; }");

    return button;
}

void TeachingWidget::connectEvents()
{
    connect(LanguageManager::instance(), &LanguageManager::languageChanged,
            this, &TeachingWidget::updateUITexts);

    // FID 템플릿 이미지 갱신 필요 시그널 연결
    connect(cameraView, &CameraView::fidTemplateUpdateRequired, this,
            [this](const QUuid &patternId)
            {
                // 레시피 로드 중에는 템플릿 업데이트 스킵
                if (isLoadingRecipe) {
                    // 레시피 로드 중 스킵 (로그 제거)
                    return;
                }
                // 현재 표시된 프레임으로 템플릿 이미지 갱신
                qDebug() << "[FID템플릿업데이트] patternId:" << patternId << "currentDisplayFrameIndex:" << currentDisplayFrameIndex;
                if (currentDisplayFrameIndex >= 0 && currentDisplayFrameIndex < static_cast<int>(4) &&
                    !cameraFrames[currentDisplayFrameIndex].empty())
                {
                    PatternInfo *pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::FID)
                    {
                        qDebug() << "[FID템플릿업데이트] 템플릿 이미지 갱신 시작";
                        // 필터 적용된 이미지로 템플릿 갱신
                        updateFidTemplateImage(pattern, pattern->rect);
                        qDebug() << "[FID템플릿업데이트] 완료 - templateImage.isNull():" << pattern->templateImage.isNull();
                    } else {
                        qDebug() << "[FID템플릿업데이트] 패턴을 찾을 수 없거나 타입이 FID가 아님";
                    }
                } else {
                    qDebug() << "[FID템플릿업데이트] 프레임이 없거나 비어있음";
                }
            });

    // INS 템플릿 이미지 갱신 시그널 연결 추가
    connect(cameraView, &CameraView::insTemplateUpdateRequired, this,
            [this](const QUuid &patternId)
            {
                // 레시피 로드 중에는 템플릿 업데이트 스킵
                if (isLoadingRecipe) {
                    // 레시피 로드 중 스킵 (로그 제거)
                    return;
                }
                PatternInfo *pattern = cameraView->getPatternById(patternId);
                if (pattern && pattern->type == PatternType::INS)
                {
                    // 패턴의 frameIndex에 해당하는 이미지 사용
                    int frameIdx = pattern->frameIndex;
                    if (frameIdx >= 0 && frameIdx < static_cast<int>(4) &&
                        !cameraFrames[frameIdx].empty())
                    {
                        // 필터 적용된 이미지로 템플릿 갱신
                        updateInsTemplateImage(pattern, pattern->rect);
                    }
                }
            });

    connect(cameraView, &CameraView::requestRemovePattern, this, &TeachingWidget::removePattern);
    connect(cameraView, &CameraView::requestAddFilter, this, [this](const QUuid &patternId)
            {
        // 필터 다이얼로그 설정
        if (filterDialog) {
            PatternInfo* pattern = cameraView->getPatternById(patternId);
            if (pattern) {
                // 필터 다이얼로그 설정 및 표시
                filterDialog->setPatternId(patternId);
                filterDialog->exec();
            }
        } });
    connect(cameraView, &CameraView::enterKeyPressed, this, &TeachingWidget::addPattern);

    connect(cameraView, &CameraView::pixelInfoChanged, this, [this](int x, int y, int r, int g, int b)
            {
        if (pixelInfoLabel) {
            // 밝기 계산 (텍스트 색상 결정용)
            int brightness = (r * 299 + g * 587 + b * 114) / 1000;
            QString textColor = brightness > 128 ? "black" : "white";
            
            // RGB 값을 각각 색상으로 표현 (HTML 서식 사용)
            QString coloredText = QString("🖱️ (%1,%2) <span style='color: #ff0000;'>R</span>(<span style='color: #ff0000;'>%3</span>) "
                                         "<span style='color: #00ff00;'>G</span>(<span style='color: #00ff00;'>%4</span>) "
                                         "<span style='color: #5555ff;'>B</span>(<span style='color: #5555ff;'>%5</span>)")
                .arg(x).arg(y).arg(r).arg(g).arg(b);
            
            pixelInfoLabel->setText(coloredText);
            pixelInfoLabel->setTextFormat(Qt::RichText);  // HTML 서식 활성화
            pixelInfoLabel->setStyleSheet(QString(
                "QLabel {"
                "  background-color: rgb(%1, %2, %3);"
                "  color: %4;"
                "  border: 1px solid #555;"
                "  padding-left: 8px;"
                "  font-size: 12px;"
                "}"
            ).arg(r).arg(g).arg(b).arg(textColor));
        } });

    connect(cameraView, &CameraView::patternSelected, this, [this](const QUuid &id)
            {
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
    } });

    connect(cameraView, &CameraView::patternRectChanged, this, [this](const QUuid &id, const QRect &rect)
            {
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
        } });

    connect(cameraView, &CameraView::patternsGrouped, this, [this]()
            {
        // 패턴 그룹화/해제 이후 트리 업데이트
        updatePatternTree(); });

    // 패턴 각도 변경 시 프로퍼티 패널 실시간 업데이트
    connect(cameraView, &CameraView::patternAngleChanged, this, [this](const QUuid &id, double angle)
            {
        // 검사 모드에서는 템플릿 업데이트하지 않음 (티칭 패턴 보존)
        if (cameraView->getInspectionMode()) {
            return;
        }
        
        // 각도를 -180° ~ +180° 범위로 정규화
        angle = normalizeAngle(angle);
        
        PatternInfo* pattern = cameraView->getPatternById(id);
        if (!pattern) return;
        
        // 기존 각도 저장 (그룹화된 패턴들 회전 계산용)
        double oldAngle = pattern->angle;
        double angleDelta = angle - oldAngle;
        
        // 정규화된 각도로 패턴 업데이트
        pattern->angle = angle;
        
        cameraView->updatePatternById(id, *pattern);
        
        // INS 패턴의 경우 회전 시 템플릿 이미지 재생성
        if (pattern->type == PatternType::INS) {
            updateInsTemplateImage(pattern, pattern->rect);
        }
        // ★ FID 패턴의 경우 회전 시 템플릿 이미지 재생성 (마스크 포함)
        else if (pattern->type == PatternType::FID) {
            updateFidTemplateImage(pattern, pattern->rect);
        }
        
        // FID 패턴인 경우, 그룹화된 INS 패턴들도 함께 회전
        if (pattern->type == PatternType::FID && std::abs(angleDelta) > 0.01) {
            QPointF fidCenter(pattern->rect.x() + pattern->rect.width()/2, 
                             pattern->rect.y() + pattern->rect.height()/2);
            
            // 해당 FID를 부모로 하는 모든 INS 패턴들 찾기
            QList<PatternInfo> allPatterns = cameraView->getPatterns();
            for (int i = 0; i < allPatterns.size(); i++) {
                PatternInfo& insPattern = allPatterns[i];
                if (insPattern.parentId == id && insPattern.type == PatternType::INS) {
                    // INS 패턴 중심점 계산
                    QPointF insCenter(insPattern.rect.x() + insPattern.rect.width()/2,
                                     insPattern.rect.y() + insPattern.rect.height()/2);
                    
                    // FID 중심을 기준으로 INS 패턴을 회전
                    double angleRad = angleDelta * M_PI / 180.0;
                    double dx = insCenter.x() - fidCenter.x();
                    double dy = insCenter.y() - fidCenter.y();
                    
                    // 회전 변환
                    double newDx = dx * cos(angleRad) - dy * sin(angleRad);
                    double newDy = dx * sin(angleRad) + dy * cos(angleRad);
                    
                    // 새로운 중심점 계산
                    QPointF newInsCenter(fidCenter.x() + newDx, fidCenter.y() + newDy);
                    
                    // INS 패턴 위치 업데이트
                    insPattern.rect.setX(newInsCenter.x() - insPattern.rect.width()/2);
                    insPattern.rect.setY(newInsCenter.y() - insPattern.rect.height()/2);
                    
                    // INS 패턴 각도도 함께 회전
                    insPattern.angle = normalizeAngle(insPattern.angle + angleDelta);
                    
                    cameraView->updatePatternById(insPattern.id, insPattern);
                }
            }
        }
        
        QTreeWidgetItem* currentItem = patternTree->currentItem();
        if (currentItem && getPatternIdFromItem(currentItem) == id) {
            // 현재 선택된 패턴의 각도가 변경된 경우 프로퍼티 패널 업데이트
            if (angleEdit) {
                angleEdit->blockSignals(true);
                angleEdit->setText(QString::number(angle, 'f', 2));
                angleEdit->blockSignals(false);
            }
            
            // INS 패턴의 경우 회전에 따른 STRIP 매개변수 UI 업데이트
            if (pattern->type == PatternType::INS) {
                int patternWidth = pattern->rect.width();
                int patternHeight = pattern->rect.height();
                
                // REAR 두께 측정 위젯들 회전 후 크기 제한 업데이트
                if (insStripRearThicknessWidthSlider) {
                    insStripRearThicknessWidthSlider->blockSignals(true);
                    insStripRearThicknessWidthSlider->setMaximum(patternWidth / 2);
                    // 현재 값이 새로운 최대값을 초과하면 조정
                    if (insStripRearThicknessWidthSlider->value() > patternWidth / 2) {
                        insStripRearThicknessWidthSlider->setValue(patternWidth / 2);
                        pattern->stripRearThicknessBoxWidth = patternWidth / 2;
                    }
                    insStripRearThicknessWidthSlider->blockSignals(false);
                }
                
                if (insStripRearThicknessHeightSlider) {
                    insStripRearThicknessHeightSlider->blockSignals(true);
                    insStripRearThicknessHeightSlider->setMaximum(patternHeight);
                    // 현재 값이 새로운 최대값을 초과하면 조정
                    if (insStripRearThicknessHeightSlider->value() > patternHeight) {
                        insStripRearThicknessHeightSlider->setValue(patternHeight);
                        pattern->stripRearThicknessBoxHeight = patternHeight;
                    }
                    insStripRearThicknessHeightSlider->blockSignals(false);
                }
                
                // FRONT 두께 측정 위젯들도 같은 방식으로 업데이트
                if (insStripThicknessWidthSlider) {
                    insStripThicknessWidthSlider->blockSignals(true);
                    insStripThicknessWidthSlider->setMaximum(patternWidth / 2);
                    if (insStripThicknessWidthSlider->value() > patternWidth / 2) {
                        insStripThicknessWidthSlider->setValue(patternWidth / 2);
                        pattern->stripThicknessBoxWidth = patternWidth / 2;
                    }
                    insStripThicknessWidthSlider->blockSignals(false);
                }
                
                if (insStripThicknessHeightSlider) {
                    insStripThicknessHeightSlider->blockSignals(true);
                    insStripThicknessHeightSlider->setMaximum(patternHeight);
                    if (insStripThicknessHeightSlider->value() > patternHeight) {
                        insStripThicknessHeightSlider->setValue(patternHeight);
                        pattern->stripThicknessBoxHeight = patternHeight;
                    }
                    insStripThicknessHeightSlider->blockSignals(false);
                }
                
                // EDGE 검사 위젯들도 패턴 크기에 맞춰 업데이트
                if (insEdgeOffsetXSlider) {
                    insEdgeOffsetXSlider->blockSignals(true);
                    insEdgeOffsetXSlider->setMaximum(patternWidth);
                    if (insEdgeOffsetXSlider->value() > patternWidth) {
                        insEdgeOffsetXSlider->setValue(patternWidth);
                        pattern->edgeOffsetX = patternWidth;
                        insEdgeOffsetXValueLabel->setText(QString("%1px").arg(patternWidth));
                    }
                    insEdgeOffsetXSlider->blockSignals(false);
                }
                
                // EDGE Width 슬라이더 업데이트 (blockSignals 적용)
                if (insEdgeWidthSlider) {
                    insEdgeWidthSlider->blockSignals(true);
                    // 값 변경 없이 repaint 강제
                    insEdgeWidthSlider->update();
                    insEdgeWidthSlider->repaint();
                    insEdgeWidthSlider->blockSignals(false);
                }
                
                // EDGE Height 슬라이더 업데이트 (blockSignals 적용)
                if (insEdgeHeightSlider) {
                    insEdgeHeightSlider->blockSignals(true);
                    // 값 변경 없이 repaint 강제
                    insEdgeHeightSlider->update();
                    insEdgeHeightSlider->repaint();
                    insEdgeHeightSlider->blockSignals(false);
                }
                

                
                // 패턴 업데이트 후 CameraView에 반영
                cameraView->updatePatternById(id, *pattern);
            }
        }
        
        // FID 패턴의 각도 변경 시 템플릿 이미지는 업데이트하지 않음
        // (원본 템플릿 유지, 검사 시 회전 매칭으로 처리)
        if (pattern->type == PatternType::INS) {
            updateInsTemplateImage(pattern, pattern->rect);
        } });

    // CameraView 빈 공간 클릭 시 검사 결과 필터 해제
    connect(cameraView, &CameraView::selectedInspectionPatternCleared, this, [this]()
            {
        qDebug().noquote() << "[TeachingWidget] selectedInspectionPatternCleared 시그널 받음 - patternTree 선택 해제";
        patternTree->setCurrentItem(nullptr);
        patternTree->clearSelection(); });
}

bool TeachingWidget::findAndUpdatePatternName(QTreeWidgetItem *parentItem, const QUuid &patternId, const QString &newName)
{
    if (!parentItem)
        return false;

    // 모든 자식 아이템 검색
    for (int i = 0; i < parentItem->childCount(); i++)
    {
        QTreeWidgetItem *childItem = parentItem->child(i);
        QString idStr = childItem->data(0, Qt::UserRole).toString();
        if (idStr == patternId.toString())
        {
            childItem->setText(0, newName);
            return true;
        }

        // 재귀적으로 자식의 자식 검색
        if (findAndUpdatePatternName(childItem, patternId, newName))
        {
            return true;
        }
    }

    return false;
}

bool TeachingWidget::findAndUpdatePatternEnabledState(QTreeWidgetItem *parentItem, const QUuid &patternId, bool enabled)
{
    if (!parentItem)
        return false;

    // 모든 자식 아이템 검색
    for (int i = 0; i < parentItem->childCount(); i++)
    {
        QTreeWidgetItem *childItem = parentItem->child(i);
        QString idStr = childItem->data(0, Qt::UserRole).toString();
        if (idStr == patternId.toString())
        {
            childItem->setDisabled(!enabled);
            return true;
        }

        // 재귀적으로 자식의 자식 검색
        if (findAndUpdatePatternEnabledState(childItem, patternId, enabled))
        {
            return true;
        }
    }

    return false;
}

void TeachingWidget::updatePropertySpinBoxes(const QRect &rect)
{
    // 읽기 전용 라벨로 변경
    QLabel *xValueLabel = findChild<QLabel *>("patternXValue");
    if (xValueLabel)
    {
        xValueLabel->setText(QString::number(rect.x()));
    }

    QLabel *yValueLabel = findChild<QLabel *>("patternYValue");
    if (yValueLabel)
    {
        yValueLabel->setText(QString::number(rect.y()));
    }

    QLabel *wValueLabel = findChild<QLabel *>("patternWValue");
    if (wValueLabel)
    {
        wValueLabel->setText(QString::number(rect.width()));
    }

    QLabel *hValueLabel = findChild<QLabel *>("patternHValue");
    if (hValueLabel)
    {
        hValueLabel->setText(QString::number(rect.height()));
    }
    // FID 패턴인 경우 템플릿 이미지 업데이트
    QTreeWidgetItem *selectedItem = patternTree->currentItem();
    if (selectedItem)
    {
        QUuid patternId = getPatternIdFromItem(selectedItem);
        if (!patternId.isNull())
        {
            PatternInfo *pattern = cameraView->getPatternById(patternId);
            if (pattern)
            {
                // 각도 정보 업데이트
                if (angleEdit)
                {
                    angleEdit->blockSignals(true);
                    angleEdit->setText(QString::number(pattern->angle, 'f', 1));
                    angleEdit->blockSignals(false);
                }

                // FID 패턴인 경우 템플릿 이미지 업데이트
                if (pattern->type == PatternType::FID)
                {
                    updateFidTemplateImage(pattern, rect);
                }
            }
        }
    }
}

void TeachingWidget::onPatternTableDropEvent(const QModelIndex &parent, int start, int end, const QModelIndex &destination, int row)
{

    // 드롭된 아이템이 필터인지 확인
    QTreeWidgetItem *item = nullptr;
    QTreeWidgetItem *targetItem = nullptr;

    // 부모가 유효한 경우 (자식 아이템이 이동된 경우)
    if (parent.isValid())
    {
        // QModelIndex를 대신하는 방법
        QTreeWidgetItem *parentItem = nullptr;
        if (parent.parent().isValid())
        {
            // 2단계 이상의 깊이인 경우
            int grandParentRow = parent.parent().row();
            QTreeWidgetItem *grandParentItem = patternTree->topLevelItem(grandParentRow);
            if (grandParentItem)
            {
                parentItem = grandParentItem->child(parent.row());
            }
        }
        else
        {
            // 1단계 깊이인 경우
            parentItem = patternTree->topLevelItem(parent.row());
        }

        if (parentItem && start < parentItem->childCount())
        {
            item = parentItem->child(start);
        }
    }
    else
    {
        // 최상위 아이템인 경우
        if (start < patternTree->topLevelItemCount())
        {
            item = patternTree->topLevelItem(start);
        }
    }

    // 드롭 대상이 유효한 경우
    if (destination.isValid())
    {
        // QModelIndex를 대신하는 방법
        if (destination.parent().isValid())
        {
            // 2단계 이상의 깊이인 경우
            int parentRow = destination.parent().row();
            QTreeWidgetItem *parentItem = patternTree->topLevelItem(parentRow);
            if (parentItem)
            {
                targetItem = parentItem->child(destination.row());
            }
        }
        else
        {
            // 1단계 깊이인 경우
            targetItem = patternTree->topLevelItem(destination.row());
        }
    }
    else if (row >= 0 && row < patternTree->topLevelItemCount())
    {
        targetItem = patternTree->topLevelItem(row);
    }

    // item 또는 targetItem이 null인 경우
    if (!item)
    {
        return;
    }

    // 드래그된 아이템이 필터인지 패턴인지 확인
    QVariant filterIndexVar = item->data(0, Qt::UserRole + 1);
    QVariant patternIdVar = item->data(0, Qt::UserRole);

    // 1. 필터 이동 처리
    if (filterIndexVar.isValid())
    {
        // 타겟 아이템이 필터인지 확인 (필터를 필터 하위로 넣는 것 방지)
        if (targetItem && targetItem->data(0, Qt::UserRole + 1).isValid())
        {
            updatePatternTree(); // 원래 상태로 복원
            return;
        }

        // 부모 항목이 같은지 확인 (같은 패턴 내에서만 이동 가능)
        QTreeWidgetItem *sourceParent = item->parent();
        QTreeWidgetItem *destParent = targetItem ? targetItem->parent() : nullptr;

        if (sourceParent != destParent)
        {
            // 다른 패턴으로 이동 시도하면 원래 위치로 복원
            updatePatternTree();
            return;
        }
    }
    // 2. 패턴 이동 처리 (패턴을 다른 패턴의 하위로)
    else if (patternIdVar.isValid() && targetItem)
    {
        QUuid sourcePatternId = QUuid(patternIdVar.toString());
        QVariant targetPatternIdVar = targetItem->data(0, Qt::UserRole);

        if (targetPatternIdVar.isValid())
        {
            QUuid targetPatternId = QUuid(targetPatternIdVar.toString());

            PatternInfo *sourcePattern = cameraView->getPatternById(sourcePatternId);
            PatternInfo *targetPattern = cameraView->getPatternById(targetPatternId);

            if (sourcePattern && targetPattern)
            {
                // INS 패턴을 FID 패턴 하위로 이동하는 경우만 허용
                if (sourcePattern->type == PatternType::INS && targetPattern->type == PatternType::FID)
                {

                    // 기존 부모에서 제거
                    if (!sourcePattern->parentId.isNull())
                    {
                        PatternInfo *oldParent = cameraView->getPatternById(sourcePattern->parentId);
                        if (oldParent)
                        {
                            oldParent->childIds.removeAll(sourcePatternId);
                            cameraView->updatePatternById(oldParent->id, *oldParent);
                        }
                    }

                    // 부모-자식 관계 설정
                    sourcePattern->parentId = targetPatternId;

                    // 대상 패턴의 childIds에 추가

                    for (int i = 0; i < targetPattern->childIds.size(); i++)
                    {
                    }

                    bool alreadyContains = targetPattern->childIds.contains(sourcePatternId);

                    if (!alreadyContains)
                    {

                        targetPattern->childIds.append(sourcePatternId);

                        bool targetUpdateResult = cameraView->updatePatternById(targetPatternId, *targetPattern);

                        // 업데이트 후 다시 확인
                        PatternInfo *verifyTarget = cameraView->getPatternById(targetPatternId);
                        if (verifyTarget)
                        {
                        }
                    }
                    else
                    {
                    }

                    // 대상 패턴의 childIds 확인
                    PatternInfo *updatedTargetPattern = cameraView->getPatternById(targetPatternId);
                    if (updatedTargetPattern)
                    {

                        for (const QUuid &childId : updatedTargetPattern->childIds)
                        {
                        }
                    }

                    // CameraView에 패턴 업데이트 알리기
                    bool updateResult = cameraView->updatePatternById(sourcePatternId, *sourcePattern);

                    // 업데이트 후 다시 확인
                    PatternInfo *updatedPattern = cameraView->getPatternById(sourcePatternId);
                    if (updatedPattern)
                    {
                    }

                    // 시뮬레이션 모드에서는 즉시 저장하여 데이터 지속성 보장
                    if (camOff)
                    {

                        saveRecipe();
                    }

                    // 패턴 트리 업데이트
                    updatePatternTree();

                    // 업데이트 후 최종 확인
                    PatternInfo *finalTargetPattern = cameraView->getPatternById(targetPatternId);
                    if (finalTargetPattern)
                    {

                        for (const QUuid &childId : finalTargetPattern->childIds)
                        {
                        }
                    }

                    // 카메라 뷰 업데이트
                    cameraView->update();

                    return;
                }
                // 그룹화 해제 (INS를 최상위로 이동)
                else if (sourcePattern->type == PatternType::INS && !targetItem->parent())
                {

                    // 기존 부모에서 제거
                    if (!sourcePattern->parentId.isNull())
                    {
                        PatternInfo *oldParent = cameraView->getPatternById(sourcePattern->parentId);
                        if (oldParent)
                        {
                            oldParent->childIds.removeAll(sourcePatternId);
                            cameraView->updatePatternById(oldParent->id, *oldParent);
                        }
                    }

                    sourcePattern->parentId = QUuid();

                    // CameraView에 패턴 업데이트 알리기
                    bool updateResult = cameraView->updatePatternById(sourcePatternId, *sourcePattern);

                    // 업데이트 후 다시 확인
                    PatternInfo *updatedPattern = cameraView->getPatternById(sourcePatternId);
                    if (updatedPattern)
                    {
                    }

                    // 시뮬레이션 모드에서는 즉시 저장하여 데이터 지속성 보장
                    if (camOff)
                    {

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
    else
    {
        return;
    }

    // 필터 이동 처리
    if (filterIndexVar.isValid())
    {
        QTreeWidgetItem *sourceParent = item->parent();

        if (sourceParent)
        {
            // 같은 패턴 내에서 필터 순서 변경
            QString patternIdStr = sourceParent->data(0, Qt::UserRole).toString();
            QUuid patternId = QUuid(patternIdStr);
            if (patternId.isNull())
            {
                return;
            }

            PatternInfo *pattern = cameraView->getPatternById(patternId);
            if (!pattern)
            {
                return;
            }

            // 필터 인덱스 가져오기
            int filterIdx = filterIndexVar.toInt();
            int newIdx = destination.isValid() ? destination.row() : row;

            // 같은 부모 내에서 위치 조정 (패턴 안에서의 상대적 위치)
            if (newIdx > filterIdx)
                newIdx--;

            // 실제 필터 순서 변경
            if (filterIdx >= 0 && filterIdx < pattern->filters.size() &&
                newIdx >= 0 && newIdx < pattern->filters.size() && filterIdx != newIdx)
            {

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

QUuid TeachingWidget::getPatternIdFromItem(QTreeWidgetItem *item)
{
    if (!item)
        return QUuid();
    return QUuid(item->data(0, Qt::UserRole).toString());
}

void TeachingWidget::updatePatternTree()
{

    // 현재 선택된 패턴 ID 저장
    QUuid selectedId = cameraView->getSelectedPatternId();

    // 트리 위젯 초기화
    patternTree->clear();

    // 컬럼 헤더 설정
    patternTree->setHeaderLabels(QStringList() << TR("PATTERN_NAME") << TR("PATTERN_TYPE") << TR("PATTERN_STATUS"));

    // 모든 패턴 가져오기
    const QList<PatternInfo> &allPatterns = cameraView->getPatterns();

    // 현재 카메라의 패턴만 필터링
    QList<PatternInfo> currentCameraPatterns;

    // 패턴 필터링: 현재 프레임의 패턴만 추가
    for (const PatternInfo &pattern : allPatterns)
    {
        // 현재 표시된 프레임과 일치하는 패턴만 표시
        if (pattern.frameIndex != currentDisplayFrameIndex)
        {
            continue;
        }

        // frameIndex로 이미 필터링되었으므로 추가 매칭 불필요

        // 현재 프레임의 패턴만 표시
        currentCameraPatterns.append(pattern);
    }

    // 패턴 ID에 대한 트리 아이템 맵핑 저장 (부모-자식 관계 구성 시 사용)
    QMap<QUuid, QTreeWidgetItem *> itemMap;

    // 1. 모든 최상위 패턴 먼저 추가 (부모가 없는 패턴)
    int addedPatterns = 0;

    for (const PatternInfo &pattern : currentCameraPatterns)
    {
        // 부모가 없는 패턴만 최상위 항목으로 추가
        if (pattern.parentId.isNull())
        {
            QTreeWidgetItem *item = createPatternTreeItem(pattern);
            if (item)
            {
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
    for (int pass = 0; pass < 3; pass++)
    {
        bool addedInThisPass = false;

        for (const PatternInfo &pattern : currentCameraPatterns)
        {
            // 부모가 있는 패턴만 처리 (아직 itemMap에 없는 것만)
            if (!pattern.parentId.isNull() && !itemMap.contains(pattern.id))
            {
                QTreeWidgetItem *parentItem = itemMap.value(pattern.parentId);
                if (parentItem)
                {
                    QTreeWidgetItem *childItem = createPatternTreeItem(pattern);
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
        if (!addedInThisPass)
        {
            break;
        }
    }

    // 모든 최상위 항목 확장
    patternTree->expandAll();

    // 이전에 선택된 패턴 다시 선택
    if (!selectedId.isNull())
    {
        for (int i = 0; i < patternTree->topLevelItemCount(); i++)
        {
            if (selectItemById(patternTree->topLevelItem(i), selectedId))
            {
                break;
            }
        }
    }
}

// 필터 파라미터 요약 문자열 생성 함수
QString TeachingWidget::getFilterParamSummary(const FilterInfo &filter)
{
    QString summary;

    switch (filter.type)
    {
    case FILTER_THRESHOLD:
    {
        int type = filter.params.value("thresholdType", 0);
        int threshold = filter.params.value("threshold", 128);

        if (type == THRESH_ADAPTIVE_MEAN || type == THRESH_ADAPTIVE_GAUSSIAN)
        {
            int blockSize = filter.params.value("blockSize", 7);
            int C = filter.params.value("C", 5);
            summary = QString("적응형, 블록:%1, C:%2").arg(blockSize).arg(C);
        }
        else
        {
            summary = QString("임계값:%1").arg(threshold);
        }
        break;
    }
    case FILTER_BLUR:
    {
        int kernelSize = filter.params.value("kernelSize", 3);
        summary = QString("커널:%1×%1").arg(kernelSize);
        break;
    }
    case FILTER_CANNY:
    {
        int threshold1 = filter.params.value("threshold1", 100);
        int threshold2 = filter.params.value("threshold2", 200);
        summary = QString("하한:%1, 상한:%2").arg(threshold1).arg(threshold2);
        break;
    }
    case FILTER_SOBEL:
    {
        int kernelSize = filter.params.value("sobelKernelSize", 3);
        summary = QString("커널:%1×%1").arg(kernelSize);
        break;
    }
    case FILTER_LAPLACIAN:
    {
        int kernelSize = filter.params.value("laplacianKernelSize", 3);
        summary = QString("커널:%1×%1").arg(kernelSize);
        break;
    }
    case FILTER_SHARPEN:
    {
        int strength = filter.params.value("sharpenStrength", 3);
        summary = QString("강도:%1").arg(strength);
        break;
    }
    case FILTER_BRIGHTNESS:
    {
        int brightness = filter.params.value("brightness", 0);
        summary = QString("값:%1").arg(brightness);
        break;
    }
    case FILTER_CONTRAST:
    {
        int contrast = filter.params.value("contrast", 0);
        summary = QString("값:%1").arg(contrast);
        break;
    }
    case FILTER_CONTOUR:
    {
        int threshold = filter.params.value("threshold", 128);
        int minArea = filter.params.value("minArea", 100);
        summary = QString("임계값:%1, 최소면적:%2").arg(threshold).arg(minArea);
        break;
    }
    case FILTER_REFLECTION_CHROMATICITY:
    {
        int threshold = filter.params.value("reflectionThreshold", 200);
        int radius = filter.params.value("inpaintRadius", 3);
        summary = QString("임계값:%1, 반경:%2").arg(threshold).arg(radius);
        break;
    }
    case FILTER_REFLECTION_INPAINTING:
    {
        int threshold = filter.params.value("reflectionThreshold", 200);
        int radius = filter.params.value("inpaintRadius", 5);
        int method = filter.params.value("inpaintMethod", 0);
        QString methodName = (method == 0) ? "TELEA" : "NS";
        summary = QString("임계값:%1, 반경:%2, 방법:%3").arg(threshold).arg(radius).arg(methodName);
        break;
    }
    default:
        summary = "기본 설정";
        break;
    }

    return summary;
}

void TeachingWidget::connectItemChangedEvent()
{
    connect(patternTree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column)
            {
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
                        cameraIndex >= 0 && cameraIndex < static_cast<int>(4) && 
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
                            cameraIndex >= 0 && cameraIndex < static_cast<int>(4) && 
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
                        cameraIndex >= 0 && cameraIndex < static_cast<int>(4) && 
                        !cameraFrames[cameraIndex].empty()) {
                        updateFidTemplateImage(pattern, pattern->rect);
                    }
                    // INS 패턴이면 템플릿 이미지 업데이트 - **여기도 수정됨**
                    if (pattern->type == PatternType::INS && 
                        cameraIndex >= 0 && cameraIndex < static_cast<int>(4) && 
                        !cameraFrames[cameraIndex].empty()) {
                        updateInsTemplateImage(pattern, pattern->rect);
                    }
                    
                    cameraView->update();
                }
            }
        } });
}

// 필터 타입 이름을 번역된 텍스트로 반환하는 함수 추가
QString TeachingWidget::getFilterTypeName(int filterType)
{
    switch (filterType)
    {
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
    case FILTER_REFLECTION_CHROMATICITY:
        return "반사 제거 (Chromaticity)";
    case FILTER_REFLECTION_INPAINTING:
        return "반사 제거 (Inpainting)";
    default:
        return TR("UNKNOWN_FILTER");
    }
}

void TeachingWidget::addFiltersToTreeItem(QTreeWidgetItem *parentItem, const PatternInfo &pattern)
{
    if (pattern.filters.isEmpty())
    {
        return;
    }

    // 각 필터를 자식 항목으로 추가
    for (int i = 0; i < pattern.filters.size(); i++)
    {
        const FilterInfo &filter = pattern.filters[i];

        // 필터 이름/유형 획득
        QString filterName = getFilterTypeName(filter.type);

        // 필터 파라미터 요약 생성
        QString paramSummary = getFilterParamSummary(filter);

        // 필터를 위한 트리 아이템 생성
        QTreeWidgetItem *filterItem = new QTreeWidgetItem();

        // 필터 이름은 0번 열에
        filterItem->setText(0, filterName);

        // 필터 타입 정보는 1번 열에
        filterItem->setText(1, TR("FIL"));

        // 파라미터 요약은 2번 열에 (활성/비활성 상태 포함)
        QString statusText = filter.enabled ? QString("%1 (%2)").arg(TR("ACTIVE")).arg(paramSummary)
                                            : QString("%1").arg(TR("INACTIVE"));
        filterItem->setText(2, statusText);

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
void TeachingWidget::onPatternSelected(QTreeWidgetItem *current, QTreeWidgetItem *previous)
{

    // 삭제 버튼 활성화 상태 관리 - 함수 시작 부분에 추가
    QPushButton *removeButton = findChild<QPushButton *>("removeButton");
    if (removeButton)
    {
        removeButton->setEnabled(current != nullptr);
    }

    if (!current)
    {
        if (propertyStackWidget)
            propertyStackWidget->setCurrentIndex(0);
        // 선택 해제 시 카메라뷰에서 검사 결과 필터링 해제
        if (cameraView)
        {
            cameraView->clearSelectedInspectionPattern();
        }
        // 선택된 필터 정보 초기화
        selectedPatternId = QUuid();
        selectedFilterIndex = -1;

        // 선택 해제 시 원본 화면으로 복원
        updateCameraFrame();
        return;
    }

    // 선택된 트리 아이템에서 패턴 ID 가져오기
    QString idStr = current->data(0, Qt::UserRole).toString();
    QUuid patternId = QUuid(idStr);

    // 카메라뷰에 선택된 패턴 전달 (검사 결과 필터링용)
    if (cameraView)
    {
        cameraView->setSelectedInspectionPatternId(patternId);
    }

    // 필터 아이템인지 확인
    QVariant filterIndexVar = current->data(0, Qt::UserRole + 1);
    bool isFilterItem = filterIndexVar.isValid();

    if (isFilterItem)
    {
        // 필터 아이템이 선택된 경우
        int filterIndex = filterIndexVar.toInt();

        // 부모 패턴 찾기 (필터는 항상 패턴의 자식)
        QTreeWidgetItem *parentItem = current->parent();
        if (parentItem)
        {
            QString parentIdStr = parentItem->data(0, Qt::UserRole).toString();
            QUuid parentId = QUuid(parentIdStr);
            PatternInfo *parentPattern = cameraView->getPatternById(parentId);

            // 선택된 필터 정보 저장
            selectedPatternId = parentId;
            selectedFilterIndex = filterIndex;

            if (parentPattern && filterIndex >= 0 && filterIndex < parentPattern->filters.size())
            {

                // 패널 전환 전에 확인

                // 필터 프로퍼티 패널 업데이트 - 직접 인덱스 설정
                propertyStackWidget->setCurrentIndex(2);

                // 필터 내용 업데이트 - 별도 함수 호출 대신 직접 코드 삽입
                if (!filterPropertyContainer)
                {
                    return;
                }

                // 기존 필터 위젯 모두 제거
                QLayout *containerLayout = filterPropertyContainer->layout();
                if (containerLayout)
                {
                    QLayoutItem *item;
                    while ((item = containerLayout->takeAt(0)) != nullptr)
                    {
                        if (item->widget())
                        {
                            item->widget()->deleteLater();
                        }
                        delete item;
                    }
                }

                // 필터 정보 라벨 생성
                const FilterInfo &filter = parentPattern->filters[filterIndex];

                // 필터 프로퍼티 위젯 생성 및 추가
                FilterPropertyWidget *filterPropWidget = new FilterPropertyWidget(filter.type, filterPropertyContainer);
                filterPropWidget->setObjectName("filterPropertyWidget");
                filterPropWidget->setParams(filter.params);
                filterPropWidget->setEnabled(filter.enabled);
                containerLayout->addWidget(filterPropWidget);

                connect(filterPropWidget, &FilterPropertyWidget::paramChanged,
                        [this, parentId, filterIndex](const QString &paramName, int value)
                        {
                            updateFilterParam(parentId, filterIndex, paramName, value);
                        });

                connect(filterPropWidget, &FilterPropertyWidget::enableStateChanged,
                        [this, parentId, filterIndex](bool enabled)
                        {
                            cameraView->setPatternFilterEnabled(parentId, filterIndex, enabled);

                            QTreeWidgetItem *selectedItem = patternTree->currentItem();
                            if (selectedItem)
                            {
                                selectedItem->setCheckState(0, enabled ? Qt::Checked : Qt::Unchecked);
                            }
                        });

                // ★ 필터 선택 시 해당 영역에 필터 적용해서 보여주기
                // cameraView의 패턴 오버레이 그리기 비활성화
                if (cameraView)
                {
                    cameraView->clearSelectedInspectionPattern();
                    cameraView->setSelectedPatternId(QUuid());
                }
                
                int frameIndex = camOff ? currentDisplayFrameIndex : cameraIndex;
                if (frameIndex >= 0 && frameIndex < 4 && !cameraFrames[frameIndex].empty())
                {
                    cv::Mat sourceFrame = cameraFrames[frameIndex].clone();
                    
                    // 회전이 있는 경우: 회전된 사각형 영역에만 필터 적용
                    if (std::abs(parentPattern->angle) > 0.1)
                    {
                        cv::Point2f center(parentPattern->rect.x() + parentPattern->rect.width() / 2.0f,
                                          parentPattern->rect.y() + parentPattern->rect.height() / 2.0f);

                        // 1. 회전된 사각형 마스크 생성
                        cv::Mat mask = cv::Mat::zeros(sourceFrame.size(), CV_8UC1);
                        cv::Size2f patternSize(parentPattern->rect.width(), parentPattern->rect.height());

                        cv::Point2f vertices[4];
                        cv::RotatedRect rotatedRect(center, patternSize, parentPattern->angle);
                        rotatedRect.points(vertices);

                        std::vector<cv::Point> points;
                        for (int i = 0; i < 4; i++)
                        {
                            points.push_back(cv::Point(static_cast<int>(std::round(vertices[i].x)),
                                                      static_cast<int>(std::round(vertices[i].y))));
                        }
                        cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{points}, cv::Scalar(255));

                        // 2. 마스크 영역만 복사
                        cv::Mat maskedImage = cv::Mat::zeros(sourceFrame.size(), sourceFrame.type());
                        sourceFrame.copyTo(maskedImage, mask);

                        // 3. 확장된 ROI 계산
                        double width = parentPattern->rect.width();
                        double height = parentPattern->rect.height();

                        int rotatedWidth, rotatedHeight;
                        calculateRotatedBoundingBox(width, height, parentPattern->angle, rotatedWidth, rotatedHeight);

                        int maxSize = std::max(rotatedWidth, rotatedHeight);
                        int halfSize = maxSize / 2;

                        cv::Rect expandedRoi(
                            qBound(0, static_cast<int>(center.x) - halfSize, sourceFrame.cols - 1),
                            qBound(0, static_cast<int>(center.y) - halfSize, sourceFrame.rows - 1),
                            qBound(1, maxSize, sourceFrame.cols - (static_cast<int>(center.x) - halfSize)),
                            qBound(1, maxSize, sourceFrame.rows - (static_cast<int>(center.y) - halfSize)));

                        // 4. 확장된 영역에 필터 적용
                        if (expandedRoi.width > 0 && expandedRoi.height > 0 &&
                            expandedRoi.x + expandedRoi.width <= maskedImage.cols &&
                            expandedRoi.y + expandedRoi.height <= maskedImage.rows)
                        {
                            cv::Mat roiMat = maskedImage(expandedRoi);
                            ImageProcessor processor;
                            cv::Mat filteredRoi;
                            processor.applyFilter(roiMat, filteredRoi, filter);
                            if (!filteredRoi.empty())
                            {
                                filteredRoi.copyTo(roiMat);
                            }
                        }

                        // 5. 마스크 영역만 필터 적용된 결과로 교체 (나머지는 원본 유지)
                        maskedImage.copyTo(sourceFrame, mask);
                    }
                    else
                    {
                        // 회전 없는 경우: rect 영역만 필터 적용
                        cv::Rect roi(
                            qBound(0, static_cast<int>(parentPattern->rect.x()), sourceFrame.cols - 1),
                            qBound(0, static_cast<int>(parentPattern->rect.y()), sourceFrame.rows - 1),
                            qBound(1, static_cast<int>(parentPattern->rect.width()), sourceFrame.cols - static_cast<int>(parentPattern->rect.x())),
                            qBound(1, static_cast<int>(parentPattern->rect.height()), sourceFrame.rows - static_cast<int>(parentPattern->rect.y())));

                        if (roi.width > 0 && roi.height > 0)
                        {
                            cv::Mat roiMat = sourceFrame(roi);
                            ImageProcessor processor;
                            cv::Mat filteredRoi;
                            processor.applyFilter(roiMat, filteredRoi, filter);
                            if (!filteredRoi.empty())
                            {
                                filteredRoi.copyTo(roiMat);
                            }
                        }
                    }
                    
                    // RGB 변환 및 UI 업데이트
                    cv::Mat rgbFrame;
                    cv::cvtColor(sourceFrame, rgbFrame, cv::COLOR_BGR2RGB);
                    QImage image(rgbFrame.data, rgbFrame.cols, rgbFrame.rows,
                                rgbFrame.step, QImage::Format_RGB888);
                    QPixmap pixmap = QPixmap::fromImage(image.copy());
                    
                    cameraView->setBackgroundPixmap(pixmap);
                    cameraView->viewport()->update();
                }

                return;
            }
        }
    }

    // 일반 패턴 아이템이 선택된 경우 (기존 코드 유지)
    PatternInfo *pattern = cameraView->getPatternById(patternId);
    updatePropertyPanel(pattern, nullptr, QUuid(), -1);

    // 선택된 필터 정보 초기화 (패턴만 선택된 경우)
    selectedPatternId = QUuid();
    selectedFilterIndex = -1;

    // 패턴 선택 시 원본 화면 보여주기
    int frameIndex = camOff ? currentDisplayFrameIndex : cameraIndex;
    if (frameIndex >= 0 && frameIndex < 4 && !cameraFrames[frameIndex].empty())
    {
        cv::Mat sourceFrame = cameraFrames[frameIndex].clone();
        
        // RGB 변환 및 UI 업데이트
        cv::Mat rgbFrame;
        cv::cvtColor(sourceFrame, rgbFrame, cv::COLOR_BGR2RGB);
        QImage image(rgbFrame.data, rgbFrame.cols, rgbFrame.rows,
                    rgbFrame.step, QImage::Format_RGB888);
        QPixmap pixmap = QPixmap::fromImage(image.copy());
        
        cameraView->setBackgroundPixmap(pixmap);
        cameraView->viewport()->update();
    }

    if (pattern)
    {
        cameraView->setSelectedPatternId(pattern->id);
    }
}

void TeachingWidget::createPropertyPanels()
{
    // 1. 프로퍼티 패널을 담을 스택 위젯 생성
    propertyStackWidget = new QStackedWidget(this);
    rightPanelLayout->addWidget(propertyStackWidget);

    // 2. 빈 상태를 위한 기본 패널
    QWidget *emptyPanel = new QWidget(propertyStackWidget);
    QVBoxLayout *emptyLayout = new QVBoxLayout(emptyPanel);
    emptyPanelLabel = new QLabel("패턴을 선택하면 속성이 표시됩니다", emptyPanel);
    emptyPanelLabel->setAlignment(Qt::AlignCenter);
    emptyPanelLabel->setStyleSheet("color: gray; font-style: italic;");
    emptyLayout->addWidget(emptyPanelLabel);
    propertyStackWidget->addWidget(emptyPanel);

    // 3. 패턴 속성 패널
    QWidget *patternPanel = new QWidget(propertyStackWidget);
    QVBoxLayout *patternContentLayout = new QVBoxLayout(patternPanel);
    patternContentLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea *scrollArea = new QScrollArea(patternPanel);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setStyleSheet(
        "QScrollArea { background-color: rgb(50, 50, 50); border: none; } "
        "QScrollBar:vertical { background: rgb(40, 40, 40); width: 12px; } "
        "QScrollBar::handle:vertical { background: rgb(80, 80, 80); min-height: 20px; border-radius: 6px; } "
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; } "
        "QScrollBar:horizontal { background: rgb(40, 40, 40); height: 12px; } "
        "QScrollBar::handle:horizontal { background: rgb(80, 80, 80); min-width: 20px; border-radius: 6px; } "
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; } ");

    QWidget *scrollContent = new QWidget();
    scrollContent->setStyleSheet(
        "QWidget { background-color: rgb(50, 50, 50); color: white; } "
        "QLabel { color: white; background-color: transparent; } "
        "QLineEdit { background-color: rgb(60, 60, 60); color: white; border: 1px solid rgb(100, 100, 100); padding: 2px; } "
        "QSpinBox, QDoubleSpinBox { background-color: rgb(60, 60, 60); color: white; border: 1px solid rgb(100, 100, 100); } "
        "QComboBox { background-color: rgb(60, 60, 60); color: white; border: 1px solid rgb(100, 100, 100); padding: 2px; } "
        "QComboBox::drop-down { border: none; } "
        "QComboBox QAbstractItemView { background-color: rgb(60, 60, 60); color: white; selection-background-color: rgb(0, 120, 215); } "
        "QCheckBox { color: white; spacing: 5px; } "
        "QCheckBox::indicator { width: 16px; height: 16px; background-color: rgb(60, 60, 60); border: 1px solid rgb(100, 100, 100); } "
        "QCheckBox::indicator:checked { background-color: rgb(0, 120, 215); border: 1px solid rgb(0, 100, 200); } ");
    QVBoxLayout *mainContentLayout = new QVBoxLayout(scrollContent);
    mainContentLayout->setContentsMargins(5, 5, 5, 5);
    mainContentLayout->setSpacing(8);

    // === 공통 기본 정보 그룹 ===
    QGroupBox *basicInfoGroup = new QGroupBox("기본 정보", scrollContent);
    basicInfoGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; background-color: rgb(45, 45, 45); border: 1px solid rgb(80, 80, 80); border-radius: 4px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QFormLayout *basicInfoLayout = new QFormLayout(basicInfoGroup);
    basicInfoLayout->setVerticalSpacing(5);
    basicInfoLayout->setContentsMargins(10, 15, 10, 10);

    // 패턴 ID
    patternIdLabel = new QLabel("ID:", basicInfoGroup);
    patternIdLabel->setStyleSheet("color: white;");
    patternIdValue = new QLabel(basicInfoGroup);
    patternIdValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    patternIdValue->setStyleSheet("color: #ccc; font-family: 'Courier New'; background-color: transparent;");
    basicInfoLayout->addRow(patternIdLabel, patternIdValue);

    // 패턴 이름
    patternNameLabel = new QLabel("이름:", basicInfoGroup);
    patternNameLabel->setStyleSheet("color: white;");
    patternNameEdit = new QLineEdit(basicInfoGroup);
    patternNameEdit->setFixedHeight(24);
    basicInfoLayout->addRow(patternNameLabel, patternNameEdit);

    // 패턴 타입 (동적 색상 적용)
    patternTypeLabel = new QLabel("타입:", basicInfoGroup);
    patternTypeLabel->setStyleSheet("color: white;");
    patternTypeValue = new QLabel(basicInfoGroup);
    patternTypeValue->setAlignment(Qt::AlignCenter);
    patternTypeValue->setFixedHeight(24);
    patternTypeValue->setStyleSheet(
        "QLabel { "
        "  border: 1px solid #ccc; "
        "  padding: 2px 8px; "
        "  font-weight: bold; "
        "  color: white; "
        "}");
    basicInfoLayout->addRow(patternTypeLabel, patternTypeValue);

    // 카메라 번호
    patternCameraLabel = new QLabel("번호:", basicInfoGroup);
    patternCameraLabel->setStyleSheet("color: white;");
    patternCameraValue = new QLabel(basicInfoGroup);
    patternCameraValue->setStyleSheet("color: #ccc; background-color: transparent;");
    basicInfoLayout->addRow(patternCameraLabel, patternCameraValue);

    mainContentLayout->addWidget(basicInfoGroup);

    // === 위치 및 크기 그룹 ===
    QGroupBox *positionSizeGroup = new QGroupBox("위치 및 크기", scrollContent);
    positionSizeGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; background-color: rgb(45, 45, 45); border: 1px solid rgb(80, 80, 80); border-radius: 4px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QFormLayout *positionSizeLayout = new QFormLayout(positionSizeGroup);
    positionSizeLayout->setVerticalSpacing(5);
    positionSizeLayout->setContentsMargins(10, 15, 10, 10);

    // 좌표 설정
    positionLabel = new QLabel("좌표:", positionSizeGroup);
    positionLabel->setStyleSheet("color: white;");
    QWidget *posWidget = new QWidget(positionSizeGroup);
    QHBoxLayout *posLayout = new QHBoxLayout(posWidget);
    posLayout->setContentsMargins(0, 0, 0, 0);
    posLayout->setSpacing(8);

    QLabel *xLabel = new QLabel("X:", posWidget);
    xLabel->setFixedWidth(15);
    xLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    xLabel->setStyleSheet("color: white;");
    patternXSpin = new QSpinBox(posWidget);
    patternXSpin->setFixedHeight(24);
    patternXSpin->setRange(0, 9999);

    QLabel *yLabel = new QLabel("Y:", posWidget);
    yLabel->setStyleSheet("color: white;");
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
    sizeLabel->setStyleSheet("color: white;");
    QWidget *sizeWidget = new QWidget(positionSizeGroup);
    QHBoxLayout *sizeLayout = new QHBoxLayout(sizeWidget);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->setSpacing(8);

    QLabel *wLabel = new QLabel("W:", sizeWidget);
    wLabel->setFixedWidth(15);
    wLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    wLabel->setStyleSheet("color: white;");
    patternWSpin = new QSpinBox(sizeWidget);
    patternWSpin->setFixedHeight(24);
    patternWSpin->setRange(1, 9999);

    QLabel *hLabel = new QLabel("H:", sizeWidget);
    hLabel->setFixedWidth(15);
    hLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    hLabel->setStyleSheet("color: white;");
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
    angleLabel->setStyleSheet("color: white;");
    QWidget *angleWidget = new QWidget(positionSizeGroup);
    QHBoxLayout *angleLayout = new QHBoxLayout(angleWidget);
    angleLayout->setContentsMargins(0, 0, 0, 0);
    angleLayout->setSpacing(5);

    angleEdit = new QLineEdit(angleWidget);
    angleEdit->setFixedHeight(24);
    angleEdit->setText("0.0");
    angleEdit->setPlaceholderText("0.0");

    QLabel *degreeLabel = new QLabel("°", angleWidget);
    degreeLabel->setStyleSheet("color: white;");

    angleLayout->addWidget(angleEdit, 1);
    angleLayout->addWidget(degreeLabel);
    positionSizeLayout->addRow(angleLabel, angleWidget);

    mainContentLayout->addWidget(positionSizeGroup);

    // 패턴 타입별 특수 속성 스택
    specialPropStack = new QStackedWidget(scrollContent);
    mainContentLayout->addWidget(specialPropStack);

    // 1. ROI 속성 (전체 카메라 영역 포함 기능 제거됨)
    QWidget *roiPropWidget = new QWidget(specialPropStack);
    QVBoxLayout *roiLayout = new QVBoxLayout(roiPropWidget);
    roiLayout->setContentsMargins(0, 0, 0, 0);
    roiLayout->setSpacing(3);
    roiLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // ROI에 특별한 속성 없음
    specialPropStack->addWidget(roiPropWidget);

    // 2. FID 속성 - 그룹박스로 묶기
    QWidget *fidPropWidget = new QWidget(specialPropStack);
    QVBoxLayout *fidLayout = new QVBoxLayout(fidPropWidget);
    fidLayout->setContentsMargins(0, 0, 0, 0);
    fidLayout->setSpacing(8);

    // === 템플릿 이미지 그룹 ===
    QGroupBox *fidTemplateGroup = new QGroupBox("템플릿 이미지", fidPropWidget);
    fidTemplateGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; background-color: rgb(45, 45, 45); border: 1px solid rgb(80, 80, 80); border-radius: 4px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QVBoxLayout *fidTemplateLayout = new QVBoxLayout(fidTemplateGroup);
    fidTemplateLayout->setContentsMargins(10, 15, 10, 10);

    // 템플릿 이미지 (매칭용만)
    QVBoxLayout *fidMatchTemplateLayout = new QVBoxLayout();
    QLabel *fidMatchTemplateLabel = new QLabel("패턴 매칭용", fidTemplateGroup);
    fidMatchTemplateLabel->setStyleSheet("color: #888888; font-size: 9px;");
    fidMatchTemplateLabel->setAlignment(Qt::AlignCenter);
    
    fidTemplateImg = new QLabel(fidTemplateGroup);
    fidTemplateImg->setFixedSize(120, 90);
    fidTemplateImg->setAlignment(Qt::AlignCenter);
    fidTemplateImg->setStyleSheet(
        "background-color: rgb(60, 60, 60); "
        "border: 1px solid rgb(100, 100, 100); "
        "color: white;");
    fidTemplateImg->setText(TR("NO_IMAGE"));
    fidTemplateImg->setCursor(Qt::PointingHandCursor);
    fidTemplateImg->installEventFilter(this);
    
    fidMatchTemplateLayout->addWidget(fidMatchTemplateLabel);
    fidMatchTemplateLayout->addWidget(fidTemplateImg, 0, Qt::AlignCenter);
    fidTemplateLayout->addLayout(fidMatchTemplateLayout);
    fidLayout->addWidget(fidTemplateGroup);

    // === 패턴 매칭 설정 그룹 ===
    QGroupBox *fidMatchGroup = new QGroupBox("패턴 매칭 활성화", fidPropWidget);
    fidMatchGroup->setCheckable(true);
    fidMatchGroup->setChecked(true);
    fidMatchGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; background-color: rgb(45, 45, 45); border: 1px solid rgb(80, 80, 80); border-radius: 4px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
        "QGroupBox::indicator { width: 13px; height: 13px; }"
        "QGroupBox::indicator:unchecked { background-color: rgb(60, 60, 60); border: 1px solid rgb(100, 100, 100); }"
        "QGroupBox::indicator:checked { background-color: #4CAF50; border: 1px solid #45a049; }");
    fidMatchCheckBox = fidMatchGroup;
    QFormLayout *fidMatchLayout = new QFormLayout(fidMatchGroup);
    fidMatchLayout->setContentsMargins(10, 15, 10, 10);
    fidMatchLayout->setVerticalSpacing(5);
    fidMatchLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fidMatchLayout->setFormAlignment(Qt::AlignCenter);

    // 매칭 방법
    fidMatchMethodLabel = new QLabel("매칭 방법:", fidMatchGroup);
    fidMatchMethodCombo = new QComboBox(fidMatchGroup);
    fidMatchMethodCombo->addItem("Coefficient", 0);
    fidMatchMethodCombo->addItem("Correlation", 1);
    fidMatchLayout->addRow(fidMatchMethodLabel, fidMatchMethodCombo);

    // 매칭 임계값
    fidMatchThreshLabel = new QLabel("매칭 임계값:", fidMatchGroup);
    fidMatchThreshSpin = new QDoubleSpinBox(fidMatchGroup);
    fidMatchThreshSpin->setFixedHeight(22);
    fidMatchThreshSpin->setRange(10.0, 100.0);
    fidMatchThreshSpin->setSingleStep(5.0);
    fidMatchThreshSpin->setValue(75.0);
    fidMatchThreshSpin->setSuffix("%");
    fidMatchLayout->addRow(fidMatchThreshLabel, fidMatchThreshSpin);

    // 회전 허용
    QHBoxLayout *fidRotationLayout = new QHBoxLayout();
    fidRotationCheck = new QCheckBox("회전 허용", fidMatchGroup);
    fidRotationLayout->addWidget(fidRotationCheck);
    fidRotationLayout->addStretch();
    fidMatchLayout->addRow("", fidRotationLayout);

    // 회전 각도 범위
    fidAngleLabel = new QLabel("회전 각도 범위:", fidMatchGroup);
    QWidget *fidAngleWidget = new QWidget(fidMatchGroup);
    QHBoxLayout *fidAngleLayout = new QHBoxLayout(fidAngleWidget);
    fidAngleLayout->setContentsMargins(0, 0, 0, 0);
    fidAngleLayout->setSpacing(5);
    fidMinAngleSpin = new QDoubleSpinBox(fidAngleWidget);
    fidMinAngleSpin->setFixedHeight(22);
    fidMinAngleSpin->setRange(-15, 0);
    fidMinAngleSpin->setSingleStep(1);
    fidMinAngleSpin->setValue(-5);
    fidMinAngleSpin->setSuffix("°");
    fidToLabel = new QLabel("~", fidAngleWidget);
    fidMaxAngleSpin = new QDoubleSpinBox(fidAngleWidget);
    fidMaxAngleSpin->setFixedHeight(22);
    fidMaxAngleSpin->setRange(0, 15);
    fidMaxAngleSpin->setSingleStep(1);
    fidMaxAngleSpin->setValue(5);
    fidMaxAngleSpin->setSuffix("°");
    fidAngleLayout->addWidget(fidMinAngleSpin);
    fidAngleLayout->addWidget(fidToLabel);
    fidAngleLayout->addWidget(fidMaxAngleSpin);
    fidAngleLayout->addStretch();
    fidMatchLayout->addRow(fidAngleLabel, fidAngleWidget);

    // 각도 스텝
    fidStepLabel = new QLabel("각도 스텝:", fidMatchGroup);
    fidStepSpin = new QDoubleSpinBox(fidMatchGroup);
    fidStepSpin->setFixedHeight(22);
    fidStepSpin->setRange(0.1, 10);
    fidStepSpin->setSingleStep(0.5);
    fidStepSpin->setValue(1.0);
    fidStepSpin->setSuffix("°");
    fidMatchLayout->addRow(fidStepLabel, fidStepSpin);

    // 그룹을 메인 레이아웃에 추가
    fidLayout->addWidget(fidMatchGroup);
    fidLayout->addStretch();

    specialPropStack->addWidget(fidPropWidget);

    // 3. INS 속성 패널 생성 (카테고리별 그룹화)
    QWidget *insPropWidget = new QWidget(specialPropStack);
    QVBoxLayout *insMainLayout = new QVBoxLayout(insPropWidget);
    insMainLayout->setContentsMargins(0, 0, 0, 0);
    insMainLayout->setSpacing(8);

    // === 기본 검사 설정 그룹 ===
    QGroupBox *basicInspectionGroup = new QGroupBox("기본 검사 설정", insPropWidget);
    basicInspectionGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; background-color: rgb(45, 45, 45); border: 1px solid rgb(80, 80, 80); border-radius: 4px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QFormLayout *basicInspectionLayout = new QFormLayout(basicInspectionGroup);
    basicInspectionLayout->setVerticalSpacing(5);
    basicInspectionLayout->setContentsMargins(10, 15, 10, 10);
    basicInspectionLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    basicInspectionLayout->setFormAlignment(Qt::AlignCenter);

    // 검사 방법
    insMethodLabel = new QLabel("검사 방법:", basicInspectionGroup);
    insMethodCombo = new QComboBox(basicInspectionGroup);
    insMethodCombo->addItem(InspectionMethod::getName(InspectionMethod::DIFF));
    insMethodCombo->addItem(InspectionMethod::getName(InspectionMethod::STRIP));
    insMethodCombo->addItem(InspectionMethod::getName(InspectionMethod::CRIMP));
    insMethodCombo->addItem(InspectionMethod::getName(InspectionMethod::SSIM));
    insMethodCombo->addItem(InspectionMethod::getName(InspectionMethod::ANOMALY));
    insMethodCombo->setCurrentIndex(0); // 기본값을 DIFF로 설정
    basicInspectionLayout->addRow(insMethodLabel, insMethodCombo);

    // 합격 임계값
    insPassThreshLabel = new QLabel("합격 임계값:", basicInspectionGroup);
    insPassThreshSpin = new QDoubleSpinBox(basicInspectionGroup);
    insPassThreshSpin->setFixedHeight(22);
    insPassThreshSpin->setRange(10.0, 100.0);
    insPassThreshSpin->setSingleStep(5.0);
    insPassThreshSpin->setValue(90.0);
    insPassThreshSpin->setSuffix("%");
    basicInspectionLayout->addRow(insPassThreshLabel, insPassThreshSpin);

    // ANOMALY 히트맵 임계값 슬라이더
    QWidget* sliderContainer = new QWidget(basicInspectionGroup);
    QHBoxLayout* sliderLayout = new QHBoxLayout(sliderContainer);
    sliderLayout->setContentsMargins(0, 0, 0, 0);
    sliderLayout->setSpacing(10);
    
    insPassThreshSlider = new QSlider(Qt::Horizontal, sliderContainer);
    insPassThreshSlider->setRange(0, 100);
    insPassThreshSlider->setValue(90);
    insPassThreshSlider->setFixedHeight(22);
    
    insPassThreshValue = new QLabel("90%", sliderContainer);
    insPassThreshValue->setFixedWidth(40);
    insPassThreshValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    
    sliderLayout->addWidget(insPassThreshSlider);
    sliderLayout->addWidget(insPassThreshValue);
    
    basicInspectionLayout->addRow((QLabel*)nullptr, sliderContainer);
    
    // 초기 표시 상태 (ANOMALY가 아니면 슬라이더 숨김)
    sliderContainer->setVisible(false);

    // ===== SSIM 검사 설정 위젯 =====
    ssimSettingsWidget = new QWidget(basicInspectionGroup);
    QVBoxLayout* ssimLayout = new QVBoxLayout(ssimSettingsWidget);
    ssimLayout->setContentsMargins(0, 5, 0, 5);
    ssimLayout->setSpacing(5);
    
    // SSIM NG 임계값 라벨
    QHBoxLayout* ssimLabelLayout = new QHBoxLayout();
    ssimNgThreshLabel = new QLabel("차이 NG 임계값:", ssimSettingsWidget);
    ssimNgThreshValue = new QLabel("30%", ssimSettingsWidget);
    ssimNgThreshValue->setFixedWidth(40);
    ssimNgThreshValue->setAlignment(Qt::AlignRight);
    ssimLabelLayout->addWidget(ssimNgThreshLabel);
    ssimLabelLayout->addStretch();
    ssimLabelLayout->addWidget(ssimNgThreshValue);
    ssimLayout->addLayout(ssimLabelLayout);
    
    // 컬러바 + 슬라이더
    QWidget* colorBarWidget = new QWidget(ssimSettingsWidget);
    colorBarWidget->setFixedHeight(30);
    QHBoxLayout* colorBarLayout = new QHBoxLayout(colorBarWidget);
    colorBarLayout->setContentsMargins(0, 0, 0, 0);
    colorBarLayout->setSpacing(5);
    
    QLabel* sameLabel = new QLabel("Same", colorBarWidget);
    sameLabel->setStyleSheet("color: #4444FF; font-size: 10px;");
    
    // 컬러바 (그라데이션 배경)
    ssimColorBar = new QLabel(colorBarWidget);
    ssimColorBar->setFixedHeight(20);
    ssimColorBar->setMinimumWidth(150);
    ssimColorBar->setStyleSheet(
        "background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
        "stop:0 #0000FF, stop:0.5 #00FF00, stop:1 #FF0000);"
        "border: 1px solid #666666; border-radius: 2px;");
    
    QLabel* diffLabel = new QLabel("Diff", colorBarWidget);
    diffLabel->setStyleSheet("color: #FF4444; font-size: 10px;");
    
    colorBarLayout->addWidget(sameLabel);
    colorBarLayout->addWidget(ssimColorBar, 1);
    colorBarLayout->addWidget(diffLabel);
    ssimLayout->addWidget(colorBarWidget);
    
    // 슬라이더
    ssimNgThreshSlider = new QSlider(Qt::Horizontal, ssimSettingsWidget);
    ssimNgThreshSlider->setRange(5, 95);
    ssimNgThreshSlider->setValue(30);
    ssimNgThreshSlider->setTickPosition(QSlider::TicksBelow);
    ssimNgThreshSlider->setTickInterval(10);
    ssimLayout->addWidget(ssimNgThreshSlider);
    
    // 설명 라벨
    QLabel* ssimDescLabel = new QLabel("차이 NG 임계값 (이 값 이상 차이나는 픽셀을 NG로 판정)", ssimSettingsWidget);
    ssimDescLabel->setStyleSheet("color: #888888; font-size: 9px;");
    ssimLayout->addWidget(ssimDescLabel);
    
    // 허용 NG 비율 레이블 + 값
    QHBoxLayout* allowedLayout = new QHBoxLayout();
    QLabel* allowedLabel = new QLabel("허용 NG 비율:", ssimSettingsWidget);
    allowedLabel->setStyleSheet("color: white;");
    allowedNgRatioValue = new QLabel("20%", ssimSettingsWidget);
    allowedNgRatioValue->setStyleSheet("color: #00FF00; font-weight: bold;");
    allowedLayout->addWidget(allowedLabel);
    allowedLayout->addWidget(allowedNgRatioValue);
    allowedLayout->addStretch();
    ssimLayout->addLayout(allowedLayout);
    
    // 허용 NG 비율 슬라이더
    allowedNgRatioSlider = new QSlider(Qt::Horizontal, ssimSettingsWidget);
    allowedNgRatioSlider->setRange(1, 50);
    allowedNgRatioSlider->setValue(20);
    allowedNgRatioSlider->setTickPosition(QSlider::TicksBelow);
    allowedNgRatioSlider->setTickInterval(5);
    ssimLayout->addWidget(allowedNgRatioSlider);
    
    // 허용 비율 설명 라벨
    QLabel* allowedDescLabel = new QLabel("(NG 픽셀이 이 값 이하면 합격)", ssimSettingsWidget);
    allowedDescLabel->setStyleSheet("color: #888888; font-size: 9px;");
    ssimLayout->addWidget(allowedDescLabel);
    
    basicInspectionLayout->addRow("", ssimSettingsWidget);
    ssimSettingsWidget->setVisible(false);  // 초기에는 숨김 (SSIM 선택 시만 표시)

    // ===== ANOMALY 검사 설정 위젯 =====
    anomalySettingsWidget = new QWidget(basicInspectionGroup);
    QFormLayout* anomalyLayout = new QFormLayout(anomalySettingsWidget);
    anomalyLayout->setContentsMargins(0, 5, 0, 5);
    anomalyLayout->setSpacing(5);
    
    anomalyMinBlobSizeSpin = new QSpinBox(anomalySettingsWidget);
    anomalyMinBlobSizeSpin->setRange(1, 10000);
    anomalyMinBlobSizeSpin->setValue(10);
    anomalyLayout->addRow("최소 불량 크기 (px):", anomalyMinBlobSizeSpin);
    
    // 최소 불량 너비/높이
    anomalyMinDefectWidthSpin = new QSpinBox(anomalySettingsWidget);
    anomalyMinDefectWidthSpin->setRange(1, 1000);
    anomalyMinDefectWidthSpin->setValue(5);
    anomalyLayout->addRow("최소 불량 W (px):", anomalyMinDefectWidthSpin);
    
    anomalyMinDefectHeightSpin = new QSpinBox(anomalySettingsWidget);
    anomalyMinDefectHeightSpin->setRange(1, 1000);
    anomalyMinDefectHeightSpin->setValue(5);
    anomalyLayout->addRow("최소 불량 H (px):", anomalyMinDefectHeightSpin);
    
    // Train 버튼 추가
    anomalyTrainButton = new QPushButton("Train", anomalySettingsWidget);
    anomalyTrainButton->setStyleSheet(
        "QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 5px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #45a049; }"
        "QPushButton:pressed { background-color: #3d8b40; }");
    anomalyLayout->addRow("", anomalyTrainButton);
    
    basicInspectionLayout->addRow("", anomalySettingsWidget);
    anomalySettingsWidget->setVisible(false);  // 초기에는 숨김 (ANOMALY 선택 시만 표시)

    insMainLayout->addWidget(basicInspectionGroup);

    // === 템플릿 이미지 그룹 ===
    QGroupBox *templateGroup = new QGroupBox("템플릿 이미지", insPropWidget);
    templateGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; background-color: rgb(45, 45, 45); border: 1px solid rgb(80, 80, 80); border-radius: 4px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QVBoxLayout *templateLayout = new QVBoxLayout(templateGroup);
    templateLayout->setContentsMargins(10, 15, 10, 10);

    // 2개 이미지를 가로로 배치
    QHBoxLayout *templateImagesLayout = new QHBoxLayout();
    templateImagesLayout->setSpacing(10);

    // 왼쪽: 매칭용 이미지 (matchTemplate)
    QVBoxLayout *matchTemplateLayout = new QVBoxLayout();
    QLabel *matchTemplateLabel = new QLabel("패턴 매칭용", templateGroup);
    matchTemplateLabel->setStyleSheet("color: #888888; font-size: 9px;");
    matchTemplateLabel->setAlignment(Qt::AlignCenter);
    
    insMatchTemplateImg = new QLabel(templateGroup);
    insMatchTemplateImg->setFixedSize(100, 75);
    insMatchTemplateImg->setAlignment(Qt::AlignCenter);
    insMatchTemplateImg->setStyleSheet(
        "background-color: rgb(60, 60, 60); "
        "border: 1px solid rgb(100, 100, 100); "
        "color: white;");
    insMatchTemplateImg->setText("매칭용");
    
    matchTemplateLayout->addWidget(matchTemplateLabel);
    matchTemplateLayout->addWidget(insMatchTemplateImg);
    templateImagesLayout->addLayout(matchTemplateLayout);

    // 오른쪽: 검사용 템플릿 이미지
    QVBoxLayout *inspectTemplateLayout = new QVBoxLayout();
    QLabel *inspectTemplateLabel = new QLabel("검사용", templateGroup);
    inspectTemplateLabel->setStyleSheet("color: #888888; font-size: 9px;");
    inspectTemplateLabel->setAlignment(Qt::AlignCenter);
    
    insTemplateImg = new QLabel(templateGroup);
    insTemplateImg->setFixedSize(100, 75);
    insTemplateImg->setAlignment(Qt::AlignCenter);
    insTemplateImg->setStyleSheet(
        "background-color: rgb(60, 60, 60); "
        "border: 1px solid rgb(100, 100, 100); "
        "color: white;");
    insTemplateImg->setText("검사용");
    insTemplateImg->setCursor(Qt::PointingHandCursor);
    insTemplateImg->installEventFilter(this);
    
    inspectTemplateLayout->addWidget(inspectTemplateLabel);
    inspectTemplateLayout->addWidget(insTemplateImg);
    templateImagesLayout->addLayout(inspectTemplateLayout);

    templateLayout->addLayout(templateImagesLayout);
    insMainLayout->addWidget(templateGroup);

    // === 패턴 매칭 (Fine Alignment) 그룹 ===
    insPatternMatchGroup = new QGroupBox("패턴 매칭 활성화", insPropWidget);
    insPatternMatchGroup->setCheckable(true);
    insPatternMatchGroup->setChecked(false);
    insPatternMatchGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; background-color: rgb(45, 45, 45); border: 1px solid rgb(80, 80, 80); border-radius: 4px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
        "QGroupBox::indicator { width: 13px; height: 13px; }"
        "QGroupBox::indicator:unchecked { background-color: rgb(60, 60, 60); border: 1px solid rgb(100, 100, 100); }"
        "QGroupBox::indicator:checked { background-color: #4CAF50; border: 1px solid #45a049; }");
    QFormLayout *patternMatchLayout = new QFormLayout(insPatternMatchGroup);
    patternMatchLayout->setVerticalSpacing(5);
    patternMatchLayout->setContentsMargins(10, 15, 10, 10);
    patternMatchLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    patternMatchLayout->setFormAlignment(Qt::AlignCenter);

    // 매칭 방법
    insPatternMatchMethodLabel = new QLabel("매칭 방법:", insPatternMatchGroup);
    insPatternMatchMethodCombo = new QComboBox(insPatternMatchGroup);
    insPatternMatchMethodCombo->addItem("Coefficient", 0);
    insPatternMatchMethodCombo->addItem("Correlation", 1);
    patternMatchLayout->addRow(insPatternMatchMethodLabel, insPatternMatchMethodCombo);

    // 패턴 매칭 임계값
    insPatternMatchThreshLabel = new QLabel("매칭 임계값:", insPatternMatchGroup);
    insPatternMatchThreshSpin = new QDoubleSpinBox(insPatternMatchGroup);
    insPatternMatchThreshSpin->setFixedHeight(22);
    insPatternMatchThreshSpin->setRange(10.0, 100.0);
    insPatternMatchThreshSpin->setSingleStep(5.0);
    insPatternMatchThreshSpin->setValue(80.0);
    insPatternMatchThreshSpin->setSuffix("%");
    patternMatchLayout->addRow(insPatternMatchThreshLabel, insPatternMatchThreshSpin);

    // 회전 사용 체크박스
    insPatternMatchRotationCheck = new QCheckBox("회전 허용", insPatternMatchGroup);
    insPatternMatchRotationCheck->setStyleSheet("color: white;");
    patternMatchLayout->addRow("", insPatternMatchRotationCheck);

    // 최소 각도
    insPatternMatchMinAngleLabel = new QLabel("최소 각도:", insPatternMatchGroup);
    insPatternMatchMinAngleSpin = new QDoubleSpinBox(insPatternMatchGroup);
    insPatternMatchMinAngleSpin->setFixedHeight(22);
    insPatternMatchMinAngleSpin->setRange(-180.0, 180.0);
    insPatternMatchMinAngleSpin->setSingleStep(1.0);
    insPatternMatchMinAngleSpin->setValue(-3.0);
    insPatternMatchMinAngleSpin->setSuffix("°");
    insPatternMatchMinAngleSpin->setEnabled(false);
    patternMatchLayout->addRow(insPatternMatchMinAngleLabel, insPatternMatchMinAngleSpin);

    // 최대 각도
    insPatternMatchMaxAngleLabel = new QLabel("최대 각도:", insPatternMatchGroup);
    insPatternMatchMaxAngleSpin = new QDoubleSpinBox(insPatternMatchGroup);
    insPatternMatchMaxAngleSpin->setFixedHeight(22);
    insPatternMatchMaxAngleSpin->setRange(-180.0, 180.0);
    insPatternMatchMaxAngleSpin->setSingleStep(1.0);
    insPatternMatchMaxAngleSpin->setValue(3.0);
    insPatternMatchMaxAngleSpin->setSuffix("°");
    insPatternMatchMaxAngleSpin->setEnabled(false);
    patternMatchLayout->addRow(insPatternMatchMaxAngleLabel, insPatternMatchMaxAngleSpin);

    // 각도 스텝
    insPatternMatchStepLabel = new QLabel("각도 스텝:", insPatternMatchGroup);
    insPatternMatchStepSpin = new QDoubleSpinBox(insPatternMatchGroup);
    insPatternMatchStepSpin->setFixedHeight(22);
    insPatternMatchStepSpin->setRange(0.1, 10.0);
    insPatternMatchStepSpin->setSingleStep(0.5);
    insPatternMatchStepSpin->setValue(1.0);
    insPatternMatchStepSpin->setSuffix("°");
    insPatternMatchStepSpin->setEnabled(false);
    patternMatchLayout->addRow(insPatternMatchStepLabel, insPatternMatchStepSpin);

    // 설명 라벨
    QLabel *patternMatchDesc = new QLabel("FID 기반 대략 위치 → 패턴 매칭으로 정확한 위치/각도 찾기", insPatternMatchGroup);
    patternMatchDesc->setStyleSheet("color: #888888; font-size: 9px;");
    patternMatchDesc->setWordWrap(true);
    patternMatchLayout->addRow("", patternMatchDesc);

    insMainLayout->addWidget(insPatternMatchGroup);

    // === STRIP 검사 공통 파라미터 그룹 ===
    insStripPanel = new QGroupBox("STRIP 검사 공통 파라미터", insPropWidget);
    insStripPanel->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; background-color: rgb(45, 45, 45); border: 1px solid rgb(80, 80, 80); border-radius: 4px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QFormLayout *insStripLayout = new QFormLayout(insStripPanel);
    insStripLayout->setVerticalSpacing(5);
    insStripLayout->setContentsMargins(10, 15, 10, 10);
    insStripLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    insStripLayout->setFormAlignment(Qt::AlignCenter);

    // 형태학적 커널 크기
    insStripKernelLabel = new QLabel("형태학적 커널:", insStripPanel);
    insStripKernelSpin = new QSpinBox(insStripPanel);
    insStripKernelSpin->setRange(3, 15);
    insStripKernelSpin->setSingleStep(2); // 홀수만
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

    // Gradient 계산 범위 - 슬라이더로 변경
    QWidget *gradientRangeWidget = new QWidget(insStripPanel);
    QVBoxLayout *gradientRangeLayout = new QVBoxLayout(gradientRangeWidget);
    gradientRangeLayout->setContentsMargins(0, 0, 0, 0);
    gradientRangeLayout->setSpacing(3);

    // 시작 지점 슬라이더
    QWidget *startWidget = new QWidget(gradientRangeWidget);
    QHBoxLayout *startLayout = new QHBoxLayout(startWidget);
    startLayout->setContentsMargins(0, 0, 0, 0);
    startLayout->setSpacing(5);

    insStripStartLabel = new QLabel("시작:", startWidget);
    insStripStartSlider = new QSlider(Qt::Horizontal, startWidget);
    insStripStartSlider->setRange(0, 50);
    insStripStartSlider->setValue(20);
    insStripStartSlider->setStyleSheet(UIColors::sliderStyle());
    insStripStartValueLabel = new QLabel("20%", startWidget);
    insStripStartValueLabel->setMinimumWidth(30);

    startLayout->addWidget(insStripStartLabel);
    startLayout->addWidget(insStripStartSlider);
    startLayout->addWidget(insStripStartValueLabel);

    // 끝 지점 슬라이더
    QWidget *endWidget = new QWidget(gradientRangeWidget);
    QHBoxLayout *endLayout = new QHBoxLayout(endWidget);
    endLayout->setContentsMargins(0, 0, 0, 0);
    endLayout->setSpacing(5);

    insStripEndLabel = new QLabel("끝:", endWidget);
    insStripEndSlider = new QSlider(Qt::Horizontal, endWidget);
    insStripEndSlider->setRange(50, 100);
    insStripEndSlider->setValue(85);
    insStripEndSlider->setStyleSheet(UIColors::sliderStyle());
    insStripEndValueLabel = new QLabel("85%", endWidget);
    insStripEndValueLabel->setMinimumWidth(30);

    endLayout->addWidget(insStripEndLabel);
    endLayout->addWidget(insStripEndSlider);
    endLayout->addWidget(insStripEndValueLabel);

    gradientRangeLayout->addWidget(startWidget);
    gradientRangeLayout->addWidget(endWidget);

    insStripLayout->addRow("Gradient 범위:", gradientRangeWidget);

    // 최소 데이터 포인트
    insStripMinPointsLabel = new QLabel("최소 포인트:", insStripPanel);
    insStripMinPointsSpin = new QSpinBox(insStripPanel);
    insStripMinPointsSpin->setRange(3, 20);
    insStripMinPointsSpin->setValue(5);
    insStripLayout->addRow(insStripMinPointsLabel, insStripMinPointsSpin);

    insMainLayout->addWidget(insStripPanel);

    // === STRIP 길이 검사 그룹 ===
    insStripLengthGroup = new QGroupBox("STRIP 길이 검사", insPropWidget);
    insStripLengthGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; background-color: rgb(45, 45, 45); border: 1px solid rgb(80, 80, 80); border-radius: 4px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QFormLayout *stripLengthLayout = new QFormLayout(insStripLengthGroup);
    stripLengthLayout->setVerticalSpacing(5);
    stripLengthLayout->setContentsMargins(10, 15, 10, 10);
    stripLengthLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    stripLengthLayout->setFormAlignment(Qt::AlignCenter);

    // STRIP 길이검사 활성화 체크박스는 필수이므로 제거됨

    // STRIP 길이검사 범위 설정
    insStripLengthMinLabel = new QLabel("최소 길이:", insStripLengthGroup);
    insStripLengthMinEdit = new QLineEdit(insStripLengthGroup);
    insStripLengthMinEdit->setText("5.70");
    insStripLengthMinEdit->setValidator(new QDoubleValidator(0.0, 9999.0, 2, insStripLengthMinEdit));
    stripLengthLayout->addRow(insStripLengthMinLabel, insStripLengthMinEdit);

    insStripLengthMaxLabel = new QLabel("최대 길이:", insStripLengthGroup);
    insStripLengthMaxEdit = new QLineEdit(insStripLengthGroup);
    insStripLengthMaxEdit->setText("6.00");
    insStripLengthMaxEdit->setValidator(new QDoubleValidator(0.0, 9999.0, 2, insStripLengthMaxEdit));
    stripLengthLayout->addRow(insStripLengthMaxLabel, insStripLengthMaxEdit);

    // STRIP 길이 수치 변환 설정 (스핀박스 + 갱신 버튼)
    insStripLengthConversionLabel = new QLabel("수치 변환 (mm):", insStripLengthGroup);

    QWidget *conversionWidget = new QWidget(insStripLengthGroup);
    QHBoxLayout *conversionLayout = new QHBoxLayout(conversionWidget);
    conversionLayout->setContentsMargins(0, 0, 0, 0);
    conversionLayout->setSpacing(5);

    insStripLengthConversionEdit = new QLineEdit("6.0", insStripLengthGroup);
    insStripLengthConversionEdit->setMaximumWidth(80);
    conversionLayout->addWidget(insStripLengthConversionEdit);

    insStripLengthRefreshButton = new QPushButton("갱신", insStripLengthGroup);
    insStripLengthRefreshButton->setMaximumWidth(80);
    insStripLengthRefreshButton->setEnabled(true);
    insStripLengthRefreshButton->setFocusPolicy(Qt::StrongFocus);
    conversionLayout->addWidget(insStripLengthRefreshButton);

    stripLengthLayout->addRow(insStripLengthConversionLabel, conversionWidget);

    // 측정값 결과 라벨 (별도 행)
    insStripLengthMeasuredLabel = new QLabel("측정값: - mm", insStripLengthGroup);
    insStripLengthMeasuredLabel->setStyleSheet("QLabel { color: #00AAFF; font-weight: bold; }");
    stripLengthLayout->addRow("", insStripLengthMeasuredLabel);

    insMainLayout->addWidget(insStripLengthGroup);

    // === FRONT 두께 검사 그룹 ===
    insStripFrontGroup = new QGroupBox("FRONT 두께 검사 활성화", insPropWidget);
    insStripFrontGroup->setCheckable(true);
    insStripFrontGroup->setChecked(true);
    insStripFrontGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; background-color: rgb(45, 45, 45); border: 1px solid rgb(80, 80, 80); border-radius: 4px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
        "QGroupBox::indicator { width: 13px; height: 13px; }"
        "QGroupBox::indicator:unchecked { background-color: rgba(50, 50, 50, 180); border: 1px solid rgba(100, 100, 100, 150); }"
        "QGroupBox::indicator:checked { background-color: #4CAF50; border: 1px solid #45a049; }");
    insStripFrontEnabledCheck = insStripFrontGroup; // GroupBox 자체를 체크박스로 사용
    QFormLayout *stripFrontLayout = new QFormLayout(insStripFrontGroup);
    stripFrontLayout->setVerticalSpacing(5);
    stripFrontLayout->setContentsMargins(10, 15, 10, 10);
    stripFrontLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    stripFrontLayout->setFormAlignment(Qt::AlignCenter);

    // STRIP 두께 측정 관련 컨트롤들 - 슬라이더 + SpinBox 조합

    // 측정박스 너비 슬라이더
    QWidget *thicknessWidthWidget = new QWidget(insStripFrontGroup);
    QHBoxLayout *thicknessWidthLayout = new QHBoxLayout(thicknessWidthWidget);
    thicknessWidthLayout->setContentsMargins(0, 0, 0, 0);
    thicknessWidthLayout->setSpacing(5);

    insStripThicknessWidthLabel = new QLabel("너비:", thicknessWidthWidget);
    insStripThicknessWidthSlider = new QSlider(Qt::Horizontal, thicknessWidthWidget);
    insStripThicknessWidthSlider->setRange(10, 200);
    insStripThicknessWidthSlider->setValue(50);
    insStripThicknessWidthSlider->setStyleSheet(UIColors::sliderStyle());
    insStripThicknessWidthValueLabel = new QLabel("50mm", thicknessWidthWidget);
    insStripThicknessWidthValueLabel->setMinimumWidth(40);

    thicknessWidthLayout->addWidget(insStripThicknessWidthLabel);
    thicknessWidthLayout->addWidget(insStripThicknessWidthSlider);
    thicknessWidthLayout->addWidget(insStripThicknessWidthValueLabel);

    // 측정박스 높이 슬라이더
    QWidget *thicknessHeightWidget = new QWidget(insStripFrontGroup);
    QHBoxLayout *thicknessHeightLayout = new QHBoxLayout(thicknessHeightWidget);
    thicknessHeightLayout->setContentsMargins(0, 0, 0, 0);
    thicknessHeightLayout->setSpacing(5);

    insStripThicknessHeightLabel = new QLabel("높이:", thicknessHeightWidget);
    insStripThicknessHeightSlider = new QSlider(Qt::Horizontal, thicknessHeightWidget);
    insStripThicknessHeightSlider->setRange(10, 100);
    insStripThicknessHeightSlider->setValue(30);
    insStripThicknessHeightSlider->setStyleSheet(UIColors::sliderStyle());
    insStripThicknessHeightValueLabel = new QLabel("30mm", thicknessHeightWidget);
    insStripThicknessHeightValueLabel->setMinimumWidth(40);

    thicknessHeightLayout->addWidget(insStripThicknessHeightLabel);
    thicknessHeightLayout->addWidget(insStripThicknessHeightSlider);
    thicknessHeightLayout->addWidget(insStripThicknessHeightValueLabel);

    // 두께 범위 위젯
    QWidget *thicknessRangeWidget = new QWidget(insStripFrontGroup);
    QVBoxLayout *thicknessRangeLayout = new QVBoxLayout(thicknessRangeWidget);
    thicknessRangeLayout->setContentsMargins(0, 0, 0, 0);
    thicknessRangeLayout->setSpacing(3);
    thicknessRangeLayout->addWidget(thicknessWidthWidget);
    thicknessRangeLayout->addWidget(thicknessHeightWidget);

    // 최소/최대 두께 LineEdit
    insStripThicknessMinLabel = new QLabel("최소 두께:", insStripFrontGroup);
    insStripThicknessMinEdit = new QLineEdit(insStripFrontGroup);
    insStripThicknessMinEdit->setText("10");
    insStripThicknessMinEdit->setValidator(new QDoubleValidator(0.0, 9999.0, 2, insStripThicknessMinEdit));

    insStripThicknessMaxLabel = new QLabel("최대 두께:", insStripFrontGroup);
    insStripThicknessMaxEdit = new QLineEdit(insStripFrontGroup);
    insStripThicknessMaxEdit->setText("100");
    insStripThicknessMaxEdit->setValidator(new QDoubleValidator(0.0, 9999.0, 2, insStripThicknessMaxEdit));

    stripFrontLayout->addRow("FRONT 두께 범위:", thicknessRangeWidget);
    stripFrontLayout->addRow(insStripThicknessMinLabel, insStripThicknessMinEdit);
    stripFrontLayout->addRow(insStripThicknessMaxLabel, insStripThicknessMaxEdit);

    insMainLayout->addWidget(insStripFrontGroup);

    // === REAR 두께 검사 그룹 ===
    insStripRearGroup = new QGroupBox("REAR 두께 검사 활성화", insPropWidget);
    insStripRearGroup->setCheckable(true);
    insStripRearGroup->setChecked(true);
    insStripRearGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; background-color: rgb(45, 45, 45); border: 1px solid rgb(80, 80, 80); border-radius: 4px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
        "QGroupBox::indicator { width: 13px; height: 13px; }"
        "QGroupBox::indicator:unchecked { background-color: rgba(50, 50, 50, 180); border: 1px solid rgba(100, 100, 100, 150); }"
        "QGroupBox::indicator:checked { background-color: #4CAF50; border: 1px solid #45a049; }");
    insStripRearEnabledCheck = insStripRearGroup; // GroupBox 자체를 체크박스로 사용
    QFormLayout *stripRearLayout = new QFormLayout(insStripRearGroup);
    stripRearLayout->setVerticalSpacing(5);
    stripRearLayout->setContentsMargins(10, 15, 10, 10);
    stripRearLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    stripRearLayout->setFormAlignment(Qt::AlignCenter);

    // REAR 두께 측정 박스 크기 설정
    insStripRearThicknessWidthLabel = new QLabel("너비:", insStripRearGroup);
    insStripRearThicknessWidthSlider = new QSlider(Qt::Horizontal, insStripRearGroup);
    insStripRearThicknessWidthSlider->setRange(10, 200);
    insStripRearThicknessWidthSlider->setValue(50);
    insStripRearThicknessWidthSlider->setStyleSheet(UIColors::sliderStyle());
    insStripRearThicknessWidthValueLabel = new QLabel("50", insStripRearGroup);
    insStripRearThicknessWidthValueLabel->setMinimumWidth(40);

    QWidget *rearThicknessWidthWidget = new QWidget(insStripRearGroup);
    QHBoxLayout *rearThicknessWidthLayout = new QHBoxLayout(rearThicknessWidthWidget);
    rearThicknessWidthLayout->setContentsMargins(0, 0, 0, 0);
    rearThicknessWidthLayout->setSpacing(5);

    rearThicknessWidthLayout->addWidget(insStripRearThicknessWidthLabel);
    rearThicknessWidthLayout->addWidget(insStripRearThicknessWidthSlider);
    rearThicknessWidthLayout->addWidget(insStripRearThicknessWidthValueLabel);

    insStripRearThicknessHeightLabel = new QLabel("높이:", insStripRearGroup);
    insStripRearThicknessHeightSlider = new QSlider(Qt::Horizontal, insStripRearGroup);
    insStripRearThicknessHeightSlider->setRange(10, 100);
    insStripRearThicknessHeightSlider->setValue(30);
    insStripRearThicknessHeightSlider->setStyleSheet(UIColors::sliderStyle());
    insStripRearThicknessHeightValueLabel = new QLabel("30", insStripRearGroup);
    insStripRearThicknessHeightValueLabel->setMinimumWidth(40);

    QWidget *rearThicknessHeightWidget = new QWidget(insStripRearGroup);
    QHBoxLayout *rearThicknessHeightLayout = new QHBoxLayout(rearThicknessHeightWidget);
    rearThicknessHeightLayout->setContentsMargins(0, 0, 0, 0);
    rearThicknessHeightLayout->setSpacing(5);

    rearThicknessHeightLayout->addWidget(insStripRearThicknessHeightLabel);
    rearThicknessHeightLayout->addWidget(insStripRearThicknessHeightSlider);
    rearThicknessHeightLayout->addWidget(insStripRearThicknessHeightValueLabel);

    // REAR 최소/최대 두께 SpinBox
    insStripRearThicknessMinLabel = new QLabel("REAR 최소 두께:", insStripRearGroup);
    insStripRearThicknessMinEdit = new QLineEdit(insStripRearGroup);
    insStripRearThicknessMinEdit->setText("10");
    insStripRearThicknessMinEdit->setValidator(new QDoubleValidator(0.0, 9999.0, 2, insStripRearThicknessMinEdit));

    insStripRearThicknessMaxLabel = new QLabel("REAR 최대 두께:", insStripRearGroup);
    insStripRearThicknessMaxEdit = new QLineEdit(insStripRearGroup);
    insStripRearThicknessMaxEdit->setText("100");
    insStripRearThicknessMaxEdit->setValidator(new QDoubleValidator(0.0, 9999.0, 2, insStripRearThicknessMaxEdit));

    // REAR 두께 범위 위젯
    QWidget *rearThicknessRangeWidget = new QWidget(insStripRearGroup);
    QVBoxLayout *rearThicknessRangeLayout = new QVBoxLayout(rearThicknessRangeWidget);
    rearThicknessRangeLayout->setContentsMargins(0, 0, 0, 0);
    rearThicknessRangeLayout->setSpacing(3);

    rearThicknessRangeLayout->addWidget(rearThicknessWidthWidget);
    rearThicknessRangeLayout->addWidget(rearThicknessHeightWidget);

    stripRearLayout->addRow("REAR 두께 범위:", rearThicknessRangeWidget);
    stripRearLayout->addRow(insStripRearThicknessMinLabel, insStripRearThicknessMinEdit);
    stripRearLayout->addRow(insStripRearThicknessMaxLabel, insStripRearThicknessMaxEdit);

    insMainLayout->addWidget(insStripRearGroup);

    // === EDGE 검사 그룹 ===
    insEdgeGroup = new QGroupBox("EDGE 검사 활성화", insPropWidget);
    insEdgeGroup->setCheckable(true);
    insEdgeGroup->setChecked(true);
    insEdgeGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; background-color: rgb(45, 45, 45); border: 1px solid rgb(80, 80, 80); border-radius: 4px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
        "QGroupBox::indicator { width: 13px; height: 13px; }"
        "QGroupBox::indicator:unchecked { background-color: rgba(50, 50, 50, 180); border: 1px solid rgba(100, 100, 100, 150); }"
        "QGroupBox::indicator:checked { background-color: #4CAF50; border: 1px solid #45a049; }");
    insEdgeEnabledCheck = insEdgeGroup; // GroupBox 자체를 체크박스로 사용
    QFormLayout *edgeLayout = new QFormLayout(insEdgeGroup);
    edgeLayout->setVerticalSpacing(5);
    edgeLayout->setContentsMargins(10, 15, 10, 10);
    edgeLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    edgeLayout->setFormAlignment(Qt::AlignCenter);

    insEdgeOffsetXLabel = new QLabel("패턴 왼쪽 오프셋:", insEdgeGroup);
    insEdgeOffsetXSlider = new QSlider(Qt::Horizontal, insEdgeGroup);
    insEdgeOffsetXSlider->setRange(1, 500); // 임시값, 패턴 선택시 동적 조정
    insEdgeOffsetXSlider->setValue(10);
    insEdgeOffsetXSlider->setStyleSheet(UIColors::sliderStyle());
    insEdgeOffsetXValueLabel = new QLabel("10", insEdgeGroup);

    QWidget *edgeOffsetWidget = new QWidget(insEdgeGroup);
    QHBoxLayout *edgeOffsetLayout = new QHBoxLayout(edgeOffsetWidget);
    edgeOffsetLayout->setContentsMargins(0, 0, 0, 0);
    edgeOffsetLayout->addWidget(insEdgeOffsetXSlider);
    edgeOffsetLayout->addWidget(insEdgeOffsetXValueLabel);

    insEdgeWidthLabel = new QLabel("너비:", insEdgeGroup);
    insEdgeWidthSlider = new QSlider(Qt::Horizontal, insEdgeGroup);
    insEdgeWidthSlider->setRange(10, 300); // 최대값 300으로 고정
    insEdgeWidthSlider->setValue(50);
    insEdgeWidthSlider->setStyleSheet(UIColors::sliderStyle());
    insEdgeWidthValueLabel = new QLabel("50", insEdgeGroup);

    QWidget *edgeWidthWidget = new QWidget(insEdgeGroup);
    QHBoxLayout *edgeWidthLayout = new QHBoxLayout(edgeWidthWidget);
    edgeWidthLayout->setContentsMargins(0, 0, 0, 0);
    edgeWidthLayout->addWidget(insEdgeWidthLabel);
    edgeWidthLayout->addWidget(insEdgeWidthSlider);
    edgeWidthLayout->addWidget(insEdgeWidthValueLabel);

    insEdgeHeightLabel = new QLabel("높이:", insEdgeGroup);
    insEdgeHeightSlider = new QSlider(Qt::Horizontal, insEdgeGroup);
    insEdgeHeightSlider->setRange(20, 300); // 최대값 300으로 고정
    insEdgeHeightSlider->setValue(100);
    insEdgeHeightSlider->setStyleSheet(UIColors::sliderStyle());
    insEdgeHeightValueLabel = new QLabel("100", insEdgeGroup);

    QWidget *edgeHeightWidget = new QWidget(insEdgeGroup);
    QHBoxLayout *edgeHeightLayout = new QHBoxLayout(edgeHeightWidget);
    edgeHeightLayout->setContentsMargins(0, 0, 0, 0);
    edgeHeightLayout->addWidget(insEdgeHeightLabel);
    edgeHeightLayout->addWidget(insEdgeHeightSlider);
    edgeHeightLayout->addWidget(insEdgeHeightValueLabel);

    // insEdgeThresholdLabel과 insEdgeThresholdSpin 제거됨 (통계적 방법 사용)

    insEdgeMaxIrregularitiesLabel = new QLabel("허용 최대 불량 개수:", insEdgeGroup);
    insEdgeMaxIrregularitiesSpin = new QSpinBox(insEdgeGroup);
    insEdgeMaxIrregularitiesSpin->setRange(1, 20);
    insEdgeMaxIrregularitiesSpin->setValue(5);
    insEdgeMaxIrregularitiesSpin->setSuffix(" 개");

    insEdgeDistanceMaxLabel = new QLabel("평균선 최대 거리:", insEdgeGroup);
    insEdgeDistanceMaxEdit = new QLineEdit(insEdgeGroup);
    insEdgeDistanceMaxEdit->setValidator(new QDoubleValidator(0.0, 9999.0, 2, insEdgeDistanceMaxEdit));
    insEdgeDistanceMaxEdit->setText("10.00");

    insEdgeStartPercentLabel = new QLabel("시작 제외 비율:", insEdgeGroup);
    insEdgeStartPercentSpin = new QSpinBox(insEdgeGroup);
    insEdgeStartPercentSpin->setRange(1, 50);
    insEdgeStartPercentSpin->setValue(10);
    insEdgeStartPercentSpin->setSuffix(" %");

    insEdgeEndPercentLabel = new QLabel("끝 제외 비율:", insEdgeGroup);
    insEdgeEndPercentSpin = new QSpinBox(insEdgeGroup);
    insEdgeEndPercentSpin->setRange(1, 50);
    insEdgeEndPercentSpin->setValue(10);
    insEdgeEndPercentSpin->setSuffix(" %");

    // EDGE 위젯들을 레이아웃에 추가
    QWidget *edgeRangeWidget = new QWidget(insEdgeGroup);
    QVBoxLayout *edgeRangeLayout = new QVBoxLayout(edgeRangeWidget);
    edgeRangeLayout->setContentsMargins(0, 0, 0, 0);
    edgeRangeLayout->setSpacing(3);

    edgeRangeLayout->addWidget(edgeWidthWidget);
    edgeRangeLayout->addWidget(edgeHeightWidget);

    edgeLayout->addRow(insEdgeOffsetXLabel, edgeOffsetWidget);
    edgeLayout->addRow("EDGE 박스 크기:", edgeRangeWidget);
    edgeLayout->addRow(insEdgeMaxIrregularitiesLabel, insEdgeMaxIrregularitiesSpin);
    edgeLayout->addRow(insEdgeDistanceMaxLabel, insEdgeDistanceMaxEdit);
    edgeLayout->addRow(insEdgeStartPercentLabel, insEdgeStartPercentSpin);
    edgeLayout->addRow(insEdgeEndPercentLabel, insEdgeEndPercentSpin);

    insMainLayout->addWidget(insEdgeGroup);

    // === CRIMP 검사 파라미터 그룹 ===
    insCrimpPanel = new QGroupBox("CRIMP 검사", insPropWidget);
    insCrimpPanel->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; background-color: rgb(45, 45, 45); border: 1px solid rgb(80, 80, 80); border-radius: 4px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }");
    QVBoxLayout *insCrimpLayout = new QVBoxLayout(insCrimpPanel);
    insCrimpLayout->setSpacing(10);
    insCrimpLayout->setContentsMargins(10, 15, 10, 10);

    // === CRIMP BARREL 기준 왼쪽 스트리핑 길이 검사 ===
    insBarrelLeftStripGroup = new QGroupBox("BARREL 기준 왼쪽 스트리핑 길이 검사", insCrimpPanel);
    insBarrelLeftStripGroup->setCheckable(true);
    insBarrelLeftStripGroup->setChecked(true);
    insBarrelLeftStripGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; background-color: rgb(45, 45, 45); border: 1px solid rgb(80, 80, 80); border-radius: 4px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
        "QGroupBox::indicator { width: 13px; height: 13px; }"
        "QGroupBox::indicator:unchecked { background-color: rgba(50, 50, 50, 180); border: 1px solid rgba(100, 100, 100, 150); }"
        "QGroupBox::indicator:checked { background-color: #4CAF50; border: 1px solid #45a049; }");
    QFormLayout *leftStripLayout = new QFormLayout(insBarrelLeftStripGroup);
    leftStripLayout->setVerticalSpacing(5);
    leftStripLayout->setContentsMargins(10, 15, 10, 10);

    // 왼쪽: 오프셋
    QWidget *leftOffsetWidget = new QWidget();
    QHBoxLayout *leftOffsetLayout = new QHBoxLayout(leftOffsetWidget);
    leftOffsetLayout->setContentsMargins(0, 0, 0, 0);
    leftOffsetLayout->setSpacing(5);
    insBarrelLeftStripOffsetSlider = new QSlider(Qt::Horizontal, leftOffsetWidget);
    insBarrelLeftStripOffsetSlider->setRange(-250, 250);
    insBarrelLeftStripOffsetSlider->setValue(0);
    insBarrelLeftStripOffsetSlider->setStyleSheet(UIColors::sliderStyle());
    insBarrelLeftStripOffsetValueLabel = new QLabel("0px", leftOffsetWidget);
    insBarrelLeftStripOffsetValueLabel->setMinimumWidth(40);
    leftOffsetLayout->addWidget(insBarrelLeftStripOffsetSlider);
    leftOffsetLayout->addWidget(insBarrelLeftStripOffsetValueLabel);
    leftStripLayout->addRow("오프셋:", leftOffsetWidget);

    // 왼쪽: 너비
    QWidget *leftWidthWidget = new QWidget();
    QHBoxLayout *leftWidthLayout = new QHBoxLayout(leftWidthWidget);
    leftWidthLayout->setContentsMargins(0, 0, 0, 0);
    leftWidthLayout->setSpacing(5);
    insBarrelLeftStripWidthSlider = new QSlider(Qt::Horizontal, leftWidthWidget);
    insBarrelLeftStripWidthSlider->setRange(10, 500);
    insBarrelLeftStripWidthSlider->setValue(100);
    insBarrelLeftStripWidthSlider->setStyleSheet(UIColors::sliderStyle());
    insBarrelLeftStripWidthValueLabel = new QLabel("100px", leftWidthWidget);
    insBarrelLeftStripWidthValueLabel->setMinimumWidth(40);
    leftWidthLayout->addWidget(insBarrelLeftStripWidthSlider);
    leftWidthLayout->addWidget(insBarrelLeftStripWidthValueLabel);
    leftStripLayout->addRow("너비:", leftWidthWidget);

    // 왼쪽: 높이
    QWidget *leftHeightWidget = new QWidget();
    QHBoxLayout *leftHeightLayout = new QHBoxLayout(leftHeightWidget);
    leftHeightLayout->setContentsMargins(0, 0, 0, 0);
    leftHeightLayout->setSpacing(5);
    insBarrelLeftStripHeightSlider = new QSlider(Qt::Horizontal, leftHeightWidget);
    insBarrelLeftStripHeightSlider->setRange(10, 500);
    insBarrelLeftStripHeightSlider->setValue(100);
    insBarrelLeftStripHeightSlider->setStyleSheet(UIColors::sliderStyle());
    insBarrelLeftStripHeightValueLabel = new QLabel("100px", leftHeightWidget);
    insBarrelLeftStripHeightValueLabel->setMinimumWidth(40);
    leftHeightLayout->addWidget(insBarrelLeftStripHeightSlider);
    leftHeightLayout->addWidget(insBarrelLeftStripHeightValueLabel);
    leftStripLayout->addRow("높이:", leftHeightWidget);

    // 왼쪽: 최소 길이 (라인에디터)
    QWidget *leftMinWidget = new QWidget();
    QHBoxLayout *leftMinLayout = new QHBoxLayout(leftMinWidget);
    leftMinLayout->setContentsMargins(0, 0, 0, 0);
    leftMinLayout->setSpacing(5);
    insBarrelLeftStripMinEdit = new QLineEdit(leftMinWidget);
    insBarrelLeftStripMinEdit->setText("5.70");
    insBarrelLeftStripMinEdit->setValidator(new QDoubleValidator(0.0, 9999.0, 2, insBarrelLeftStripMinEdit));
    QLabel *leftMinUnitLabel = new QLabel("mm", leftMinWidget);
    leftMinUnitLabel->setMinimumWidth(30);
    leftMinLayout->addWidget(insBarrelLeftStripMinEdit);
    leftMinLayout->addWidget(leftMinUnitLabel);
    leftStripLayout->addRow("최소 길이:", leftMinWidget);

    // 왼쪽: 최대 길이 (라인에디터)
    QWidget *leftMaxWidget = new QWidget();
    QHBoxLayout *leftMaxLayout = new QHBoxLayout(leftMaxWidget);
    leftMaxLayout->setContentsMargins(0, 0, 0, 0);
    leftMaxLayout->setSpacing(5);
    insBarrelLeftStripMaxEdit = new QLineEdit(leftMaxWidget);
    insBarrelLeftStripMaxEdit->setText("6.00");
    insBarrelLeftStripMaxEdit->setValidator(new QDoubleValidator(0.0, 9999.0, 2, insBarrelLeftStripMaxEdit));
    QLabel *leftMaxUnitLabel = new QLabel("mm", leftMaxWidget);
    leftMaxUnitLabel->setMinimumWidth(30);
    leftMaxLayout->addWidget(insBarrelLeftStripMaxEdit);
    leftMaxLayout->addWidget(leftMaxUnitLabel);
    leftStripLayout->addRow("최대 길이:", leftMaxWidget);

    insCrimpLayout->addWidget(insBarrelLeftStripGroup);

    // === CRIMP BARREL 기준 오른쪽 스트리핑 길이 검사 ===
    insBarrelRightStripGroup = new QGroupBox("BARREL 기준 오른쪽 스트리핑 길이 검사", insCrimpPanel);
    insBarrelRightStripGroup->setCheckable(true);
    insBarrelRightStripGroup->setChecked(true);
    insBarrelRightStripGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: white; background-color: rgb(45, 45, 45); border: 1px solid rgb(80, 80, 80); border-radius: 4px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
        "QGroupBox::indicator { width: 13px; height: 13px; }"
        "QGroupBox::indicator:unchecked { background-color: rgba(50, 50, 50, 180); border: 1px solid rgba(100, 100, 100, 150); }"
        "QGroupBox::indicator:checked { background-color: #4CAF50; border: 1px solid #45a049; }");
    QFormLayout *rightStripLayout = new QFormLayout(insBarrelRightStripGroup);
    rightStripLayout->setVerticalSpacing(5);
    rightStripLayout->setContentsMargins(10, 15, 10, 10);

    // 오른쪽: 오프셋
    QWidget *rightOffsetWidget = new QWidget();
    QHBoxLayout *rightOffsetLayout = new QHBoxLayout(rightOffsetWidget);
    rightOffsetLayout->setContentsMargins(0, 0, 0, 0);
    rightOffsetLayout->setSpacing(5);
    insBarrelRightStripOffsetSlider = new QSlider(Qt::Horizontal, rightOffsetWidget);
    insBarrelRightStripOffsetSlider->setRange(-250, 250);
    insBarrelRightStripOffsetSlider->setValue(0);
    insBarrelRightStripOffsetSlider->setStyleSheet(UIColors::sliderStyle());
    insBarrelRightStripOffsetValueLabel = new QLabel("0px", rightOffsetWidget);
    insBarrelRightStripOffsetValueLabel->setMinimumWidth(40);
    rightOffsetLayout->addWidget(insBarrelRightStripOffsetSlider);
    rightOffsetLayout->addWidget(insBarrelRightStripOffsetValueLabel);
    rightStripLayout->addRow("오프셋:", rightOffsetWidget);

    // 오른쪽: 너비
    QWidget *rightWidthWidget = new QWidget();
    QHBoxLayout *rightWidthLayout = new QHBoxLayout(rightWidthWidget);
    rightWidthLayout->setContentsMargins(0, 0, 0, 0);
    rightWidthLayout->setSpacing(5);
    insBarrelRightStripWidthSlider = new QSlider(Qt::Horizontal, rightWidthWidget);
    insBarrelRightStripWidthSlider->setRange(10, 500);
    insBarrelRightStripWidthSlider->setValue(100);
    insBarrelRightStripWidthSlider->setStyleSheet(UIColors::sliderStyle());
    insBarrelRightStripWidthValueLabel = new QLabel("100px", rightWidthWidget);
    insBarrelRightStripWidthValueLabel->setMinimumWidth(40);
    rightWidthLayout->addWidget(insBarrelRightStripWidthSlider);
    rightWidthLayout->addWidget(insBarrelRightStripWidthValueLabel);
    rightStripLayout->addRow("너비:", rightWidthWidget);

    // 오른쪽: 높이
    QWidget *rightHeightWidget = new QWidget();
    QHBoxLayout *rightHeightLayout = new QHBoxLayout(rightHeightWidget);
    rightHeightLayout->setContentsMargins(0, 0, 0, 0);
    rightHeightLayout->setSpacing(5);
    insBarrelRightStripHeightSlider = new QSlider(Qt::Horizontal, rightHeightWidget);
    insBarrelRightStripHeightSlider->setRange(10, 500);
    insBarrelRightStripHeightSlider->setValue(100);
    insBarrelRightStripHeightSlider->setStyleSheet(UIColors::sliderStyle());
    insBarrelRightStripHeightValueLabel = new QLabel("100px", rightHeightWidget);
    insBarrelRightStripHeightValueLabel->setMinimumWidth(40);
    rightHeightLayout->addWidget(insBarrelRightStripHeightSlider);
    rightHeightLayout->addWidget(insBarrelRightStripHeightValueLabel);
    rightStripLayout->addRow("높이:", rightHeightWidget);

    // 오른쪽: 최소 길이 (라인에디터)
    QWidget *rightMinWidget = new QWidget();
    QHBoxLayout *rightMinLayout = new QHBoxLayout(rightMinWidget);
    rightMinLayout->setContentsMargins(0, 0, 0, 0);
    rightMinLayout->setSpacing(5);
    insBarrelRightStripMinEdit = new QLineEdit(rightMinWidget);
    insBarrelRightStripMinEdit->setText("5.70");
    insBarrelRightStripMinEdit->setValidator(new QDoubleValidator(0.0, 9999.0, 2, insBarrelRightStripMinEdit));
    QLabel *rightMinUnitLabel = new QLabel("mm", rightMinWidget);
    rightMinUnitLabel->setMinimumWidth(30);
    rightMinLayout->addWidget(insBarrelRightStripMinEdit);
    rightMinLayout->addWidget(rightMinUnitLabel);
    rightStripLayout->addRow("최소 길이:", rightMinWidget);

    // 오른쪽: 최대 길이 (라인에디터)
    QWidget *rightMaxWidget = new QWidget();
    QHBoxLayout *rightMaxLayout = new QHBoxLayout(rightMaxWidget);
    rightMaxLayout->setContentsMargins(0, 0, 0, 0);
    rightMaxLayout->setSpacing(5);
    insBarrelRightStripMaxEdit = new QLineEdit(rightMaxWidget);
    insBarrelRightStripMaxEdit->setText("6.00");
    insBarrelRightStripMaxEdit->setValidator(new QDoubleValidator(0.0, 9999.0, 2, insBarrelRightStripMaxEdit));
    QLabel *rightMaxUnitLabel = new QLabel("mm", rightMaxWidget);
    rightMaxUnitLabel->setMinimumWidth(30);
    rightMaxLayout->addWidget(insBarrelRightStripMaxEdit);
    rightMaxLayout->addWidget(rightMaxUnitLabel);
    rightStripLayout->addRow("최대 길이:", rightMaxWidget);

    insCrimpLayout->addWidget(insBarrelRightStripGroup);
    insMainLayout->addWidget(insCrimpPanel);

    // 여백 추가
    insMainLayout->addStretch();
    // 패널 초기 설정 - 검사 방법에 따라 표시
    insStripPanel->setVisible(false); // STRIP 패널도 처음에는 숨김
    insCrimpPanel->setVisible(false); // CRIMP 패널도 처음에는 숨김

    // 검사 방법에 따른 패널 표시 설정
    connect(insMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index)
            {
                insStripPanel->setVisible(index == InspectionMethod::STRIP); // STRIP
                insCrimpPanel->setVisible(index == InspectionMethod::CRIMP); // CRIMP
                if (ssimSettingsWidget)
                    ssimSettingsWidget->setVisible(index == InspectionMethod::SSIM); // SSIM
                if (anomalySettingsWidget)
                    anomalySettingsWidget->setVisible(index == InspectionMethod::ANOMALY); // ANOMALY
                
                // 합격 임계값: ANOMALY일 때는 슬라이더, 다른 방법(DIFF)일 때는 SpinBox
                bool isAnomaly = (index == InspectionMethod::ANOMALY);
                bool isDiff = (index == InspectionMethod::DIFF);
                
                // SpinBox는 DIFF에서만 표시
                if (insPassThreshSpin && insPassThreshLabel)
                {
                    insPassThreshSpin->setVisible(isDiff);
                    insPassThreshLabel->setVisible(isDiff || isAnomaly);
                    
                    // 라벨 텍스트 변경
                    if (isAnomaly)
                    {
                        insPassThreshLabel->setText("임계값(%):");
                    }
                    else
                    {
                        insPassThreshLabel->setText("합격 임계값(%):");
                    }
                }
                
                // 슬라이더는 ANOMALY에서만 표시
                if (insPassThreshSlider)
                {
                    insPassThreshSlider->parentWidget()->setVisible(isAnomaly);
                }
                
                // SSIM 선택 시 필터 실시간 업데이트
                if (index == InspectionMethod::SSIM)
                {
                    updateCameraFrame();
                }
            });
    
    // SSIM NG 임계값 슬라이더 연결
    connect(ssimNgThreshSlider, &QSlider::valueChanged, [this](int value) {
        ssimNgThreshValue->setText(QString("%1%").arg(value));
        
        // 선택된 패턴 찾기 (cameraView에서 현재 선택된 패턴 ID 가져오기)
        if (cameraView) {
            QUuid currentSelectedId = cameraView->getSelectedPatternId();
            if (!currentSelectedId.isNull()) {
                // getPatterns()가 복사본을 반환하므로 직접 수정 후 updatePattern 호출
                PatternInfo* patternPtr = cameraView->getPatternById(currentSelectedId);
                if (patternPtr && patternPtr->type == PatternType::INS) {
                    patternPtr->ssimNgThreshold = static_cast<double>(value);
                    
                    // 검사 결과가 있으면 히트맵 실시간 갱신
                    if (cameraView->hasLastInspectionResult()) {
                        cameraView->updateSSIMHeatmap(currentSelectedId, static_cast<double>(value));
                    }
                    
                    cameraView->viewport()->update();
                }
            }
        }
    });
    
    // 허용 NG 비율 슬라이더 연결
    connect(allowedNgRatioSlider, &QSlider::valueChanged, [this](int value) {
        allowedNgRatioValue->setText(QString("%1%").arg(value));
        
        // 선택된 패턴 찾기
        if (cameraView) {
            QUuid currentSelectedId = cameraView->getSelectedPatternId();
            if (!currentSelectedId.isNull()) {
                PatternInfo* patternPtr = cameraView->getPatternById(currentSelectedId);
                if (patternPtr && patternPtr->type == PatternType::INS) {
                    patternPtr->allowedNgRatio = static_cast<double>(value);
                    cameraView->viewport()->update();
                }
            }
        }
    });

    // ANOMALY 파라미터 연결
    connect(anomalyMinBlobSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), [this](int value) {
        if (cameraView) {
            QUuid currentSelectedId = cameraView->getSelectedPatternId();
            if (!currentSelectedId.isNull()) {
                PatternInfo* patternPtr = cameraView->getPatternById(currentSelectedId);
                if (patternPtr && patternPtr->type == PatternType::INS) {
                    patternPtr->anomalyMinBlobSize = value;
                    cameraView->viewport()->update();
                }
            }
        }
    });
    
    connect(anomalyMinDefectWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), [this](int value) {
        if (cameraView) {
            QUuid currentSelectedId = cameraView->getSelectedPatternId();
            if (!currentSelectedId.isNull()) {
                PatternInfo* patternPtr = cameraView->getPatternById(currentSelectedId);
                if (patternPtr && patternPtr->type == PatternType::INS) {
                    patternPtr->anomalyMinDefectWidth = value;
                    cameraView->viewport()->update();
                }
            }
        }
    });
    
    connect(anomalyMinDefectHeightSpin, QOverload<int>::of(&QSpinBox::valueChanged), [this](int value) {
        if (cameraView) {
            QUuid currentSelectedId = cameraView->getSelectedPatternId();
            if (!currentSelectedId.isNull()) {
                PatternInfo* patternPtr = cameraView->getPatternById(currentSelectedId);
                if (patternPtr && patternPtr->type == PatternType::INS) {
                    patternPtr->anomalyMinDefectHeight = value;
                    cameraView->viewport()->update();
                }
            }
        }
    });
    
    // ANOMALY Train 버튼 연결
    connect(anomalyTrainButton, &QPushButton::clicked, [this]() {
        if (!cameraView) return;
        
        // 현재 모드의 모든 ANOMALY 패턴 수집
        QVector<PatternInfo*> anomalyPatterns;
        QVector<PatternInfo*> allPatternsVec;
        const QList<PatternInfo>& allPatterns = cameraView->getPatterns();
        
        for (const PatternInfo& pattern : allPatterns) {
            PatternInfo* patPtr = const_cast<PatternInfo*>(&pattern);
            allPatternsVec.append(patPtr);  // 모든 패턴 추가 (FID 포함)
            
            if (pattern.type == PatternType::INS && 
                pattern.inspectionMethod == InspectionMethod::ANOMALY) {
                qDebug() << "[모델관리] ANOMALY 패턴 발견:" << pattern.name 
                         << "frameIndex:" << pattern.frameIndex;
                anomalyPatterns.append(patPtr);
            }
        }
        
        qDebug() << "[모델관리] 전체 패턴:" << allPatterns.size() 
                 << "ANOMALY 패턴:" << anomalyPatterns.size();
        
        if (anomalyPatterns.isEmpty()) {
            QMessageBox::warning(this, "경고", "ANOMALY 검사 방법을 사용하는 INS 패턴이 없습니다.");
            return;
        }
        
        // TrainDialog 표시
        TrainDialog* trainDialog = new TrainDialog(this);
        activeTrainDialog = trainDialog;  // 활성 다이얼로그 저장
        trainDialog->setAllPatterns(allPatternsVec);  // 모든 패턴 설정 (FID 찾기용)
        trainDialog->setAnomalyPatterns(anomalyPatterns);
        trainDialog->setCurrentRecipeName(currentRecipeName);  // 현재 레시피명 전달
        
        // Train 요청 시그널 연결
        connect(trainDialog, &TrainDialog::trainRequested, this, [this](const QString& patternName) {
            trainAnomalyPattern(patternName);
        });
        
        // 학습 완료 시그널 연결 - 모델 리로딩
        connect(trainDialog, &TrainDialog::trainingFinished, this, [this](bool success) {
            if (success) {
                qDebug() << "[TRAIN] 학습 완료됨. 모델 리로딩 시작...";
                // 기존 모델 해제
                ImageProcessor::releasePatchCoreTensorRT();
                qDebug() << "[TRAIN] 모델 리로딩 완료. 새로 학습된 모델을 사용할 수 있습니다.";
            }
        });
        
        // 다이얼로그 닫힐 때 activeTrainDialog 초기화
        connect(trainDialog, &QWidget::destroyed, this, [this]() {
            activeTrainDialog = nullptr;
        });
        
        trainDialog->show();
        trainDialog->setAttribute(Qt::WA_DeleteOnClose);
    });

    // 패턴 매칭 활성화 체크박스 연결
    if (insPatternMatchGroup) {
        connect(insPatternMatchGroup, &QGroupBox::toggled, [this](bool checked) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->patternMatchEnabled = checked;
                        
                        // 활성화 시 matchTemplate 이미지 업데이트
                        if (checked) {
                            updateInsMatchTemplate(pattern);
                        }
                        
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }

    // 패턴 매칭 방법 연결
    if (insPatternMatchMethodCombo) {
        connect(insPatternMatchMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                [this](int index) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->patternMatchMethod = index;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }

    // 패턴 매칭 임계값 연결
    if (insPatternMatchThreshSpin) {
        connect(insPatternMatchThreshSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                [this](double value) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->patternMatchThreshold = value;
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }

    // 패턴 매칭 회전 체크박스 연결
    if (insPatternMatchRotationCheck) {
        connect(insPatternMatchRotationCheck, &QCheckBox::toggled, [this](bool checked) {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->useRotation = checked;
                        
                        // 회전 관련 위젯 활성화/비활성화
                        if (insPatternMatchMinAngleSpin) insPatternMatchMinAngleSpin->setEnabled(checked);
                        if (insPatternMatchMaxAngleSpin) insPatternMatchMaxAngleSpin->setEnabled(checked);
                        if (insPatternMatchStepSpin) insPatternMatchStepSpin->setEnabled(checked);
                        
                        cameraView->updatePatternById(patternId, *pattern);
                    }
                }
            }
        });
    }

    // 패턴 매칭 최소 각도 연결
    if (insPatternMatchMinAngleSpin) {
        connect(insPatternMatchMinAngleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
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

    // 패턴 매칭 최대 각도 연결
    if (insPatternMatchMaxAngleSpin) {
        connect(insPatternMatchMaxAngleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
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

    // 패턴 매칭 각도 스텝 연결
    if (insPatternMatchStepSpin) {
        connect(insPatternMatchStepSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
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

    // 특수 속성 스택에 INS 패널 추가
    specialPropStack->addWidget(insPropWidget);

    // 스크롤 영역에 컨텐츠 설정
    scrollArea->setWidget(scrollContent);
    patternContentLayout->addWidget(scrollArea);
    propertyStackWidget->addWidget(patternPanel);

    // 4. 필터 속성 패널을 위한 컨테이너 추가
    QWidget *filterPanelContainer = new QWidget(propertyStackWidget);
    QVBoxLayout *filterContainerLayout = new QVBoxLayout(filterPanelContainer);
    filterContainerLayout->setContentsMargins(0, 0, 0, 0);

    // 필터 설명 레이블
    filterDescLabel = new QLabel("필터 설정", filterPanelContainer);
    filterDescLabel->setStyleSheet("font-weight: bold; color: white; font-size: 11pt; margin-top: 4px; margin-bottom: 1px;");
    filterContainerLayout->addWidget(filterDescLabel);

    // 스크롤 영역 추가
    QScrollArea *filterScrollArea = new QScrollArea(filterPanelContainer);
    filterScrollArea->setWidgetResizable(true);
    filterScrollArea->setFrameShape(QFrame::NoFrame);

    // 필터 스크롤 영역 스타일 설정 (투명 배경)
    filterScrollArea->setStyleSheet("QScrollArea { background-color: transparent; }");

    // 필터 위젯이 여기에 추가됨
    filterPropertyContainer = new QWidget(filterScrollArea);
    filterPropertyContainer->setStyleSheet("QWidget { background-color: transparent; color: white; }");
    QVBoxLayout *filterLayout = new QVBoxLayout(filterPropertyContainer);
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

void TeachingWidget::showImageViewerDialog(const QImage &image, const QString &title)
{
    // 타이틀리스 대화상자 생성
    QDialog *imageDialog = new QDialog(this);
    imageDialog->setWindowTitle(title);
    imageDialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    imageDialog->setMinimumSize(400, 400);
    imageDialog->resize(600, 500);

    QVBoxLayout *layout = new QVBoxLayout(imageDialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 스케일 표시용 레이블 (미리 생성)
    QLabel *scaleLabel = new QLabel("Scale: 100%", imageDialog);

    // 이미지 표시용 레이블 (스크롤 없이 직접 표시)
    class ZoomableImageLabel : public QLabel
    {
    public:
        ZoomableImageLabel(QLabel *scaleLabel, QWidget *parent = nullptr)
            : QLabel(parent),
              scale(1.0),
              isDragging(false),
              originalPixmap(),
              scaleLabel(scaleLabel)
        {
            setAlignment(Qt::AlignCenter);
            setCursor(Qt::OpenHandCursor);

            // 초기 포커스 정책 설정
            setFocusPolicy(Qt::StrongFocus);
            setMouseTracking(true);
        }

        void setOriginalPixmap(const QPixmap &pixmap)
        {
            originalPixmap = pixmap;
            if (!originalPixmap.isNull())
            {
                // 초기 설정: 최소 충분한 크기로 설정
                setMinimumSize(originalPixmap.width(), originalPixmap.height());
                updatePixmap();
            }
        }

        void setScale(double newScale)
        {
            scale = qBound(0.1, newScale, 10.0); // 최소 0.1x, 최대 10x
            updatePixmap();

            // 스케일 정보 표시 - 직접 scaleLabel 업데이트
            if (scaleLabel)
            {
                scaleLabel->setText(QString("Scale: %1%").arg(qRound(scale * 100)));
            }

            // 크기 변경 - 스크롤바 제대로 표시되도록
            if (!originalPixmap.isNull())
            {
                int newWidth = qRound(originalPixmap.width() * scale);
                int newHeight = qRound(originalPixmap.height() * scale);
                setMinimumSize(newWidth, newHeight);
            }

            // 프로퍼티 업데이트 (버튼 이벤트에서 사용)
            setProperty("scale", scale);
        }

        double getScale() const
        {
            return scale;
        }

        void fitToView(const QSize &viewSize)
        {
            if (originalPixmap.isNull())
                return;

            double widthScale = (double)viewSize.width() / originalPixmap.width();
            double heightScale = (double)viewSize.height() / originalPixmap.height();
            double fitScale = qMin(widthScale, heightScale) * 0.95; // 약간의 여백

            setScale(fitScale);
            scrollOffset = QPoint(0, 0); // 스크롤 위치 초기화
            updatePixmap();
        }

    protected:
        void wheelEvent(QWheelEvent *event) override
        {
            int delta = event->angleDelta().y();
            double factor = (delta > 0) ? 1.1 : 0.9;
            setScale(scale * factor);
            event->accept();
        }

        void mousePressEvent(QMouseEvent *event) override
        {
            if (event->button() == Qt::LeftButton)
            {
                isDragging = true;
                lastDragPos = event->pos();
                setCursor(Qt::ClosedHandCursor);
            }
            QLabel::mousePressEvent(event);
        }

        void mouseMoveEvent(QMouseEvent *event) override
        {
            if (isDragging)
            {
                QPoint delta = event->pos() - lastDragPos;
                scrollOffset += delta;
                lastDragPos = event->pos();
                updatePixmap();
            }
            QLabel::mouseMoveEvent(event);
        }

        void mouseReleaseEvent(QMouseEvent *event) override
        {
            if (event->button() == Qt::LeftButton)
            {
                isDragging = false;
                setCursor(Qt::OpenHandCursor);
            }
            QLabel::mouseReleaseEvent(event);
        }

        void resizeEvent(QResizeEvent *event) override
        {
            QLabel::resizeEvent(event);
            if (!originalPixmap.isNull())
            {
                updatePixmap();
            }
        }

    private:
        void updatePixmap()
        {
            if (originalPixmap.isNull())
                return;

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
        QLabel *scaleLabel; // 스케일 표시용 레이블 참조
    };

    // 확대/축소 가능한 레이블 생성
    ZoomableImageLabel *imageLabel = new ZoomableImageLabel(scaleLabel, imageDialog);
    imageLabel->setOriginalPixmap(QPixmap::fromImage(image));

    // 버튼 레이아웃
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(10, 5, 10, 5);

    // 확대/축소 버튼
    QPushButton *zoomInButton = new QPushButton("+", imageDialog);
    QPushButton *zoomOutButton = new QPushButton("-", imageDialog);
    QPushButton *resetButton = new QPushButton("원본 크기", imageDialog);
    QPushButton *fitButton = new QPushButton("화면에 맞춤", imageDialog);
    QPushButton *closeButton = new QPushButton("닫기", imageDialog);

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
    layout->addWidget(imageLabel, 1);
    layout->addLayout(buttonLayout);

    // 버튼 이벤트 연결
    connect(zoomInButton, &QPushButton::clicked, [imageLabel]()
            { imageLabel->setScale(imageLabel->getScale() * 1.2); });

    connect(zoomOutButton, &QPushButton::clicked, [imageLabel]()
            { imageLabel->setScale(imageLabel->getScale() / 1.2); });

    connect(resetButton, &QPushButton::clicked, [imageLabel]()
            { imageLabel->setScale(1.0); });

    connect(fitButton, &QPushButton::clicked, [imageLabel, imageDialog]()
            { imageLabel->fitToView(imageDialog->size()); });

    connect(closeButton, &QPushButton::clicked, imageDialog, &QDialog::accept);

    // 초기 스케일 정보 저장
    imageLabel->setProperty("scale", 1.0);

    // 도움말 추가
    QLabel *helpLabel = new QLabel("마우스 휠: 확대/축소 | 드래그: 이동", imageDialog);
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

void TeachingWidget::updateFidTemplateImage(const QUuid &patternId)
{
    PatternInfo *pattern = cameraView->getPatternById(patternId);
    if (pattern && pattern->type == PatternType::FID)
    {
        updateFidTemplateImage(pattern, pattern->rect);
    }
}

void TeachingWidget::updateInsTemplateImage(const QUuid &patternId)
{
    PatternInfo *pattern = cameraView->getPatternById(patternId);
    if (pattern && pattern->type == PatternType::INS)
    {
        updateInsTemplateImage(pattern, pattern->rect);
    }
}

void TeachingWidget::updateInsTemplateImage(PatternInfo *pattern, const QRectF &newRect)
{
    if (!pattern || pattern->type != PatternType::INS)
    {
        return;
    }

    // **검사 모드일 때는 템플릿 이미지 갱신 금지**
    if (cameraView && cameraView->getInspectionMode())
    {
        return;
    }

    // 패턴의 frameIndex 사용
    int frameIndex = pattern->frameIndex;

    // cameraFrames 유효성 검사
    if (frameIndex < 0 || frameIndex >= static_cast<int>(4))
    {
        qDebug() << "[updateInsTemplateImage] 유효하지 않은 frameIndex:" << frameIndex;
        return;
    }

    cv::Mat sourceFrame;

    // 시뮬레이션 모드와 일반 모드 모두 cameraFrames 사용
    {
        QMutexLocker locker(&cameraFramesMutex);
        if (cameraFrames[frameIndex].empty())
        {
            return;
        }

        try
        {
            sourceFrame = cameraFrames[frameIndex].clone();
        }
        catch (...)
        {
            return;
        }
    }

    if (sourceFrame.empty())
    {
        return;
    }

    // 1. 전체 프레임 복사 (원본 이미지 사용 - 필터 적용 안함)
    cv::Mat originalFrame = sourceFrame.clone();

    // 2. INS 템플릿 이미지는 원본에서 생성 (필터 적용하지 않음)

    // 3. INS 템플릿 이미지: 회전 시 전체 영역을 템플릿으로 저장 (필터 적용 후 저장)
    cv::Mat roiMat;

    // 패턴이 회전되어 있는지 확인
    if (std::abs(pattern->angle) > 0.1)
    {
        // 회전된 경우: 회전 각도에 따른 실제 bounding box 크기 계산
        cv::Point2f center(newRect.x() + newRect.width() / 2.0f, newRect.y() + newRect.height() / 2.0f);

        double width = newRect.width();
        double height = newRect.height();

        // 회전된 사각형의 실제 bounding box 크기 계산
        int bboxWidth, bboxHeight;
        calculateRotatedBoundingBox(width, height, pattern->angle, bboxWidth, bboxHeight);

        // ROI 영역 계산 (중심점 기준)
        cv::Rect bboxRoi(
            static_cast<int>(center.x - bboxWidth / 2.0),
            static_cast<int>(center.y - bboxHeight / 2.0),
            bboxWidth,
            bboxHeight);

        // 이미지 경계와 교집합 구하기
        cv::Rect imageBounds(0, 0, originalFrame.cols, originalFrame.rows);
        cv::Rect validRoi = bboxRoi & imageBounds;

        if (validRoi.width > 0 && validRoi.height > 0)
        {
            // bounding box 크기의 결과 이미지 생성 (검은색 배경)
            cv::Mat templateRegion = cv::Mat::zeros(bboxHeight, bboxWidth, originalFrame.type());

            // 유효한 영역만 복사
            int offsetX = validRoi.x - bboxRoi.x;
            int offsetY = validRoi.y - bboxRoi.y;

            cv::Mat validImage = originalFrame(validRoi);
            cv::Rect resultRect(offsetX, offsetY, validRoi.width, validRoi.height);
            validImage.copyTo(templateRegion(resultRect));

            // ===== DIFF/STRIP/SSIM 검사: 전체 영역에 필터 먼저 적용 =====
            if ((pattern->inspectionMethod == InspectionMethod::DIFF ||
                 pattern->inspectionMethod == InspectionMethod::STRIP ||
                 pattern->inspectionMethod == InspectionMethod::SSIM) &&
                !pattern->filters.isEmpty())
            {
                cv::Mat processedRegion = templateRegion.clone();
                ImageProcessor processor;
                for (const FilterInfo &filter : pattern->filters)
                {
                    if (filter.enabled)
                    {
                        cv::Mat tempFiltered;
                        processor.applyFilter(processedRegion, tempFiltered, filter);
                        if (!tempFiltered.empty())
                        {
                            processedRegion = tempFiltered.clone();
                        }
                    }
                }
                templateRegion = processedRegion;
            }

            // 회전 시에는 전체 bounding box 영역을 템플릿으로 저장
            roiMat = templateRegion.clone();
        }
        else
        {
            return;
        }
    }
    else
    {
        // 회전 없는 경우: INS 영역만 직접 추출
        cv::Rect roi(
            static_cast<int>(newRect.x()),
            static_cast<int>(newRect.y()),
            static_cast<int>(newRect.width()),
            static_cast<int>(newRect.height()));

        // 이미지 경계와 교집합 구하기
        cv::Rect imageBounds(0, 0, originalFrame.cols, originalFrame.rows);
        cv::Rect validRoi = roi & imageBounds;

        if (validRoi.width > 0 && validRoi.height > 0)
        {
            cv::Mat insRegion = originalFrame(validRoi).clone();

            // ===== DIFF/STRIP/SSIM 검사: 필터 적용 =====
            if ((pattern->inspectionMethod == InspectionMethod::DIFF ||
                 pattern->inspectionMethod == InspectionMethod::STRIP ||
                 pattern->inspectionMethod == InspectionMethod::SSIM) &&
                !pattern->filters.isEmpty())
            {
                qDebug() << QString("템플릿(회전 없음): INS 영역(%1x%2)에 %3개 필터 순차 적용")
                                .arg(insRegion.cols)
                                .arg(insRegion.rows)
                                .arg(pattern->filters.size());

                cv::Mat processedRegion = insRegion.clone();
                ImageProcessor processor;
                for (const FilterInfo &filter : pattern->filters)
                {
                    if (filter.enabled)
                    {
                        cv::Mat tempFiltered;
                        processor.applyFilter(processedRegion, tempFiltered, filter);
                        if (!tempFiltered.empty())
                        {
                            processedRegion = tempFiltered.clone();
                        }
                    }
                }
                insRegion = processedRegion;
            }

            roiMat = insRegion;
        }
        else
        {
            return;
        }
    }

    if (roiMat.empty())
    {
        return;
    }

    // **템플릿 이미지는 필터를 적용하지 않고 원본 그대로 저장**
    // DIFF 검사는 이미 위에서 전체 영역에 필터 적용 완료

    // BGR -> RGB 변환 (QImage 생성용)
    if (roiMat.channels() == 3)
    {
        cv::cvtColor(roiMat, roiMat, cv::COLOR_BGR2RGB);
    }

    // QImage로 변환
    QImage qimg;
    if (roiMat.isContinuous())
    {
        qimg = QImage(roiMat.data, roiMat.cols, roiMat.rows, roiMat.step, QImage::Format_RGB888);
    }
    else
    {
        qimg = QImage(roiMat.cols, roiMat.rows, QImage::Format_RGB888);
        for (int y = 0; y < roiMat.rows; y++)
        {
            memcpy(qimg.scanLine(y), roiMat.ptr<uchar>(y), roiMat.cols * 3);
        }
    }

    // 패턴의 템플릿 이미지 업데이트
    pattern->templateImage = qimg.copy();

    // UI 업데이트
    QImage *currentTemplateImage = &pattern->templateImage;

    if (insTemplateImg)
    {
        if (!currentTemplateImage->isNull())
        {
            QPixmap pixmap = QPixmap::fromImage(*currentTemplateImage);
            if (!pixmap.isNull())
            {
                insTemplateImg->setPixmap(pixmap.scaled(
                    insTemplateImg->width(), insTemplateImg->height(), Qt::KeepAspectRatio));
                insTemplateImg->setText("");
            }
            else
            {
                insTemplateImg->setText(TR("IMAGE_CONVERSION_FAILED"));
            }
        }
        else
        {
            insTemplateImg->setPixmap(QPixmap());
            insTemplateImg->setText(TR("NO_IMAGE"));
        }
    }

    // 패턴 매칭용 템플릿 이미지도 함께 업데이트
    updateInsMatchTemplate(pattern);
}

void TeachingWidget::updateFidTemplateImage(PatternInfo *pattern, const QRectF &newRect)
{
    if (!pattern || pattern->type != PatternType::FID)
    {
        return;
    }

    // **검사 모드일 때는 템플릿 이미지 갱신 금지**
    if (cameraView && cameraView->getInspectionMode())
    {
        return;
    }

    // 현재 표시된 프레임 사용
    int frameIndex = currentDisplayFrameIndex;

    cv::Mat sourceFrame;

    // cameraFrames 유효성 검사 및 프레임 가져오기
    if (frameIndex >= 0 && frameIndex < static_cast<int>(4) &&
        !cameraFrames[frameIndex].empty())
    {
        sourceFrame = cameraFrames[frameIndex].clone();
    }
    else
    {
        return;
    }

    cv::Mat roiMat;
    cv::Mat maskMat;  // 마스크 생성용

    // 패턴이 회전되어 있는지 확인
    if (std::abs(pattern->angle) > 0.1)
    {
        // ★ 회전된 경우: 마스킹된 이미지 생성 (INS와 동일)
        cv::Point2f center(newRect.x() + newRect.width() / 2.0f,
                          newRect.y() + newRect.height() / 2.0f);

        double width = newRect.width();
        double height = newRect.height();

        // 회전된 사각형의 bounding box 크기 계산
        int bboxWidth, bboxHeight;
        calculateRotatedBoundingBox(width, height, pattern->angle, bboxWidth, bboxHeight);

        // ROI 영역 계산 (중심점 기준)
        cv::Rect bboxRoi(
            static_cast<int>(center.x - bboxWidth / 2.0),
            static_cast<int>(center.y - bboxHeight / 2.0),
            bboxWidth,
            bboxHeight);

        // 이미지 경계와 교집합 구하기
        cv::Rect imageBounds(0, 0, sourceFrame.cols, sourceFrame.rows);
        cv::Rect validRoi = bboxRoi & imageBounds;

        if (validRoi.width > 0 && validRoi.height > 0)
        {
            // bounding box 크기의 결과 이미지 생성 (검은색 배경)
            cv::Mat templateRegion = cv::Mat::zeros(bboxHeight, bboxWidth, sourceFrame.type());
            cv::Mat maskRegion = cv::Mat::zeros(bboxHeight, bboxWidth, CV_8UC1);

            // 유효한 영역만 복사
            int offsetX = validRoi.x - bboxRoi.x;
            int offsetY = validRoi.y - bboxRoi.y;

            cv::Mat validImage = sourceFrame(validRoi);
            cv::Rect resultRect(offsetX, offsetY, validRoi.width, validRoi.height);
            validImage.copyTo(templateRegion(resultRect));

            // ★ 회전된 사각형 영역을 마스크로 생성
            cv::Point2f rectCenter(bboxWidth / 2.0f, bboxHeight / 2.0f);
            cv::RotatedRect rotatedRect(rectCenter, cv::Size2f(width, height), pattern->angle);
            
            cv::Point2f vertices[4];
            rotatedRect.points(vertices);
            std::vector<cv::Point> contour;
            for (int i = 0; i < 4; i++) {
                contour.push_back(cv::Point(static_cast<int>(vertices[i].x), static_cast<int>(vertices[i].y)));
            }
            cv::fillConvexPoly(maskRegion, contour, cv::Scalar(255));

            // ★ 마스크를 이미지에 적용 (배경을 검은색으로)
            cv::Mat maskedImage;
            templateRegion.copyTo(maskedImage, maskRegion);

            roiMat = maskedImage.clone();
            maskMat = maskRegion.clone();
        }
        else
        {
            return;
        }
    }
    else
    {
        // 회전 없는 경우: 원본 사각형 영역만 추출
        cv::Rect roi(
            static_cast<int>(newRect.x()),
            static_cast<int>(newRect.y()),
            static_cast<int>(newRect.width()),
            static_cast<int>(newRect.height()));

        cv::Rect imageBounds(0, 0, sourceFrame.cols, sourceFrame.rows);
        cv::Rect validRoi = roi & imageBounds;

        if (validRoi.width > 0 && validRoi.height > 0)
        {
            roiMat = sourceFrame(validRoi).clone();
            maskMat = cv::Mat(validRoi.height, validRoi.width, CV_8UC1, cv::Scalar(255));
        }
        else
        {
            return;
        }
    }

    if (roiMat.empty())
    {
        return;
    }

    // FID는 필터를 사용하지 않음 (원본 이미지로만 매칭)

    // BGR -> RGB 변환
    cv::cvtColor(roiMat, roiMat, cv::COLOR_BGR2RGB);

    // QImage로 변환 (RGB888)
    QImage qimg(roiMat.data, roiMat.cols, roiMat.rows, roiMat.step, QImage::Format_RGB888);

    // 패턴의 템플릿 이미지 업데이트
    pattern->templateImage = qimg.copy();
    
    // ★ matchTemplate 저장 (RGB32 포맷으로 변환 - OpenCV 매칭용)
    pattern->matchTemplate = qimg.convertToFormat(QImage::Format_RGB32);
    
    // ★ 마스크 이미지도 저장 (matchTemplateMask 사용)
    if (!maskMat.empty())
    {
        QImage maskImg(maskMat.data, maskMat.cols, maskMat.rows, maskMat.step, QImage::Format_Grayscale8);
        pattern->matchTemplateMask = maskImg.copy();
    }
    else
    {
        pattern->matchTemplateMask = QImage();
    }

    // UI 업데이트
    if (fidTemplateImg)
    {
        fidTemplateImg->setPixmap(QPixmap::fromImage(pattern->templateImage.scaled(
            fidTemplateImg->width(), fidTemplateImg->height(), Qt::KeepAspectRatio)));
    }
}

void TeachingWidget::updateInsMatchTemplate(PatternInfo *pattern)
{
    if (!pattern || pattern->type != PatternType::INS)
    {
        return;
    }

    // 검사 모드일 때는 템플릿 이미지 갱신 금지
    if (cameraView && cameraView->getInspectionMode())
    {
        return;
    }

    // 현재 표시된 프레임 사용
    int frameIndex = currentDisplayFrameIndex;

    cv::Mat sourceFrame;

    // cameraFrames 유효성 검사 및 프레임 가져오기
    if (frameIndex >= 0 && frameIndex < static_cast<int>(4) &&
        !cameraFrames[frameIndex].empty())
    {
        sourceFrame = cameraFrames[frameIndex].clone();
    }
    else
    {
        return;
    }

    cv::Mat roiMat;
    cv::Mat maskMat;  // 마스크 생성용

    // 패턴이 회전되어 있는지 확인
    if (std::abs(pattern->angle) > 0.1)
    {
        // ★ 회전된 경우: 마스킹된 이미지 생성
        cv::Point2f center(pattern->rect.x() + pattern->rect.width() / 2.0f,
                          pattern->rect.y() + pattern->rect.height() / 2.0f);

        double width = pattern->rect.width();
        double height = pattern->rect.height();

        // 회전된 사각형의 bounding box 크기 계산
        int bboxWidth, bboxHeight;
        calculateRotatedBoundingBox(width, height, pattern->angle, bboxWidth, bboxHeight);

        // ROI 영역 계산 (중심점 기준)
        cv::Rect bboxRoi(
            static_cast<int>(center.x - bboxWidth / 2.0),
            static_cast<int>(center.y - bboxHeight / 2.0),
            bboxWidth,
            bboxHeight);

        // 이미지 경계와 교집합 구하기
        cv::Rect imageBounds(0, 0, sourceFrame.cols, sourceFrame.rows);
        cv::Rect validRoi = bboxRoi & imageBounds;

        if (validRoi.width > 0 && validRoi.height > 0)
        {
            // bounding box 크기의 결과 이미지 생성 (검은색 배경)
            cv::Mat templateRegion = cv::Mat::zeros(bboxHeight, bboxWidth, sourceFrame.type());
            cv::Mat maskRegion = cv::Mat::zeros(bboxHeight, bboxWidth, CV_8UC1);

            // 유효한 영역만 복사
            int offsetX = validRoi.x - bboxRoi.x;
            int offsetY = validRoi.y - bboxRoi.y;

            cv::Mat validImage = sourceFrame(validRoi);
            cv::Rect resultRect(offsetX, offsetY, validRoi.width, validRoi.height);
            validImage.copyTo(templateRegion(resultRect));

            // ★ 회전된 사각형 영역을 마스크로 생성
            cv::Point2f rectCenter(bboxWidth / 2.0f, bboxHeight / 2.0f);
            cv::RotatedRect rotatedRect(rectCenter, cv::Size2f(width, height), pattern->angle);
            
            cv::Point2f vertices[4];
            rotatedRect.points(vertices);
            std::vector<cv::Point> contour;
            for (int i = 0; i < 4; i++) {
                contour.push_back(cv::Point(static_cast<int>(vertices[i].x), static_cast<int>(vertices[i].y)));
            }
            cv::fillConvexPoly(maskRegion, contour, cv::Scalar(255));

            // ★ 마스크를 이미지에 적용 (배경을 검은색으로)
            cv::Mat maskedImage;
            templateRegion.copyTo(maskedImage, maskRegion);

            roiMat = maskedImage.clone();
            maskMat = maskRegion.clone();
        }
        else
        {
            return;
        }
    }
    else
    {
        // 회전 없는 경우: 원본 사각형 영역만 추출
        cv::Rect roi(
            static_cast<int>(pattern->rect.x()),
            static_cast<int>(pattern->rect.y()),
            static_cast<int>(pattern->rect.width()),
            static_cast<int>(pattern->rect.height()));

        cv::Rect imageBounds(0, 0, sourceFrame.cols, sourceFrame.rows);
        cv::Rect validRoi = roi & imageBounds;

        if (validRoi.width > 0 && validRoi.height > 0)
        {
            roiMat = sourceFrame(validRoi).clone();
            maskMat = cv::Mat(validRoi.height, validRoi.width, CV_8UC1, cv::Scalar(255));
        }
        else
        {
            return;
        }
    }

    if (roiMat.empty())
    {
        return;
    }

    // BGR -> RGB 변환
    cv::cvtColor(roiMat, roiMat, cv::COLOR_BGR2RGB);

    // QImage로 변환
    QImage qimg(roiMat.data, roiMat.cols, roiMat.rows, roiMat.step, QImage::Format_RGB888);
    QImage maskImg(maskMat.data, maskMat.cols, maskMat.rows, maskMat.step, QImage::Format_Grayscale8);

    // 패턴의 matchTemplate 이미지 + 마스크 업데이트
    pattern->matchTemplate = qimg.copy();
    pattern->matchTemplateMask = maskImg.copy();

    if (pattern->matchTemplateMask.isNull()) {
        qDebug() << QString("INS 패턴 '%1' matchTemplate 업데이트: %2x%3, 포맷=%4, 마스크=NULL, 각도=%5°")
                        .arg(pattern->name)
                        .arg(pattern->matchTemplate.width())
                        .arg(pattern->matchTemplate.height())
                        .arg(pattern->matchTemplate.format())
                        .arg(pattern->angle, 0, 'f', 2);
    } else {
        qDebug() << QString("INS 패턴 '%1' matchTemplate 업데이트: %2x%3, 포맷=%4, 마스크=%5x%6, 각도=%7°")
                        .arg(pattern->name)
                        .arg(pattern->matchTemplate.width())
                        .arg(pattern->matchTemplate.height())
                        .arg(pattern->matchTemplate.format())
                        .arg(pattern->matchTemplateMask.width())
                        .arg(pattern->matchTemplateMask.height())
                        .arg(pattern->angle, 0, 'f', 2);
    }

    // UI 업데이트 (현재 선택된 패턴인 경우에만)
    if (insMatchTemplateImg)
    {
        QTreeWidgetItem *currentItem = patternTree ? patternTree->currentItem() : nullptr;
        if (currentItem)
        {
            QUuid selectedPatternId = getPatternIdFromItem(currentItem);
            if (selectedPatternId == pattern->id)
            {
                if (!pattern->matchTemplate.isNull())
                {
                    QPixmap pixmap = QPixmap::fromImage(pattern->matchTemplate);
                    if (!pixmap.isNull())
                    {
                        insMatchTemplateImg->setPixmap(pixmap.scaled(
                            insMatchTemplateImg->width(), insMatchTemplateImg->height(), Qt::KeepAspectRatio));
                        insMatchTemplateImg->setText("");
                    }
                    else
                    {
                        insMatchTemplateImg->setPixmap(QPixmap());
                        insMatchTemplateImg->setText("변환\n실패");
                    }
                }
                else
                {
                    insMatchTemplateImg->setPixmap(QPixmap());
                    insMatchTemplateImg->setText("매칭용");
                }
            }
        }
    }
}

void TeachingWidget::calculateRotatedBoundingBox(double width, double height, double angle, int& bboxWidth, int& bboxHeight)
{
    double angleRad = std::abs(angle) * M_PI / 180.0;
    bboxWidth = static_cast<int>(std::abs(width * std::cos(angleRad)) + std::abs(height * std::sin(angleRad)));
    bboxHeight = static_cast<int>(std::abs(width * std::sin(angleRad)) + std::abs(height * std::cos(angleRad)));
}

cv::Mat TeachingWidget::extractRotatedRegion(const cv::Mat &image, const QRectF &rect, double angle)
{
    if (image.empty() || rect.width() <= 0 || rect.height() <= 0)
    {
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
    for (int i = 0; i < 4; i++)
    {
        double dx = corners[i].x - centerX;
        double dy = corners[i].y - centerY;

        rotatedCorners[i].x = centerX + dx * cosA - dy * sinA;
        rotatedCorners[i].y = centerY + dx * sinA + dy * cosA;
    }

    // 회전된 꼭짓점들의 바운딩 박스 계산
    float minX = rotatedCorners[0].x, maxX = rotatedCorners[0].x;
    float minY = rotatedCorners[0].y, maxY = rotatedCorners[0].y;

    for (int i = 1; i < 4; i++)
    {
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

    if (boundingWidth <= 0 || boundingHeight <= 0)
    {
        return cv::Mat(static_cast<int>(rect.height()), static_cast<int>(rect.width()),
                       image.type(), cv::Scalar(255, 255, 255));
    }

    // 바운딩 박스 크기의 결과 이미지 생성 (흰색으로 초기화)
    cv::Mat result(boundingHeight, boundingWidth, image.type(), cv::Scalar(255, 255, 255));

    // 회전된 사각형 영역의 마스크 생성
    cv::Mat mask = cv::Mat::zeros(boundingHeight, boundingWidth, CV_8UC1);

    // 바운딩 박스 좌표계로 변환된 회전된 꼭짓점들
    std::vector<cv::Point> maskCorners(4);
    for (int i = 0; i < 4; i++)
    {
        maskCorners[i].x = static_cast<int>(rotatedCorners[i].x - boundingX);
        maskCorners[i].y = static_cast<int>(rotatedCorners[i].y - boundingY);
    }

    // 마스크에 회전된 사각형 그리기
    cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{maskCorners}, cv::Scalar(255));

    // 바운딩 박스 영역의 원본 이미지 복사
    cv::Rect boundingRect(boundingX, boundingY, boundingWidth, boundingHeight);
    cv::Mat boundingRegion = image(boundingRect);

    // 마스크를 사용해서 회전된 영역만 복사
    for (int y = 0; y < boundingHeight; y++)
    {
        for (int x = 0; x < boundingWidth; x++)
        {
            if (mask.at<uchar>(y, x) > 0)
            {
                if (image.channels() == 3)
                {
                    result.at<cv::Vec3b>(y, x) = boundingRegion.at<cv::Vec3b>(y, x);
                }
                else
                {
                    result.at<uchar>(y, x) = boundingRegion.at<uchar>(y, x);
                }
            }
        }
    }

    return result;
}

void TeachingWidget::updatePatternFilters(int patternIndex)
{
    updatePatternTree(); // 간단히 트리 전체 업데이트로 대체
}

// 프로퍼티 패널의 이벤트 연결을 처리하는 함수
void TeachingWidget::connectPropertyPanelEvents()
{
    // 이름 변경 이벤트
    if (patternNameEdit)
    {
        connect(patternNameEdit, &QLineEdit::textChanged, [this](const QString &text)
                {
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
            } });
    }

    // includeAllCameraCheck 제거됨

    // FID 패턴 매칭 방법 콤보박스
    if (fidMatchMethodCombo)
    {
        connect(fidMatchMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                [this](int index)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::FID)
                            {
                                pattern->fidMatchMethod = index;
                                cameraView->updatePatternById(patternId, *pattern);
                            }
                        }
                    }
                });
    }

    // FID 매칭 검사 활성화 체크박스
    if (fidMatchCheckBox)
    {
        connect(fidMatchCheckBox, &QGroupBox::toggled, [this](bool checked)
                {
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
            } });
    }

    // FID 패턴 임계값 이벤트
    if (fidMatchThreshSpin)
    {
        connect(fidMatchThreshSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                [this](double value)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::FID)
                            {
                                pattern->matchThreshold = value; // 100% 단위 그대로 저장
                                cameraView->updatePatternById(patternId, *pattern);
                            }
                        }
                    }
                });
    }

    // FID 회전 사용 체크박스
    if (fidRotationCheck)
    {
        connect(fidRotationCheck, &QCheckBox::toggled, [this](bool checked)
                {
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
            } });
    }

    // FID 최소 각도 설정
    if (fidMinAngleSpin)
    {
        connect(fidMinAngleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                [this](double value)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::FID)
                            {
                                pattern->minAngle = value;
                                cameraView->updatePatternById(patternId, *pattern);
                            }
                        }
                    }
                });
    }

    // FID 최대 각도 설정
    if (fidMaxAngleSpin)
    {
        connect(fidMaxAngleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                [this](double value)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::FID)
                            {
                                pattern->maxAngle = value;
                                cameraView->updatePatternById(patternId, *pattern);
                            }
                        }
                    }
                });
    }

    // FID 각도 스텝 설정
    if (fidStepSpin)
    {
        connect(fidStepSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                [this](double value)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::FID)
                            {
                                pattern->angleStep = value;
                                cameraView->updatePatternById(patternId, *pattern);
                            }
                        }
                    }
                });
    }

    // INS 합격 임계값 설정
    if (insPassThreshSpin)
    {
        connect(insPassThreshSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                [this](double value)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->passThreshold = value; // 100% 단위 그대로 저장
                                cameraView->updatePatternById(patternId, *pattern);
                            }
                        }
                    }
                });
    }

    // ANOMALY 히트맵 임계값 슬라이더
    if (insPassThreshSlider)
    {
        connect(insPassThreshSlider, &QSlider::valueChanged,
                [this](int value)
                {
                    if (insPassThreshValue)
                    {
                        insPassThreshValue->setText(QString("%1%").arg(value));
                    }
                    
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->passThreshold = static_cast<double>(value);
                                cameraView->updatePatternById(patternId, *pattern);
                                
                                // 검사 결과가 있으면 히트맵 실시간 갱신
                                if (cameraView->hasLastInspectionResult())
                                {
                                    cameraView->updateAnomalyHeatmap(patternId, static_cast<double>(value));
                                }
                            }
                        }
                    }
                });
    }

    // INS 검사 방법 콤보박스
    if (insMethodCombo)
    {
        connect(insMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                [this](int index)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->inspectionMethod = index;

                                // STRIP 검사 패널 및 그룹들 표시 설정
                                bool isStripMethod = (index == InspectionMethod::STRIP);
                                if (insStripPanel)
                                {
                                    insStripPanel->setVisible(isStripMethod);
                                }
                                if (insStripLengthGroup)
                                    insStripLengthGroup->setVisible(isStripMethod);
                                if (insStripFrontGroup)
                                    insStripFrontGroup->setVisible(isStripMethod);
                                if (insStripRearGroup)
                                    insStripRearGroup->setVisible(isStripMethod);
                                if (insEdgeGroup)
                                    insEdgeGroup->setVisible(isStripMethod);

                                // STRIP과 SSIM 검사에서는 검사 임계값과 결과 반전 옵션 필요 없음 (ANOMALY는 표시)
                                if (insPassThreshSpin && insPassThreshLabel)
                                {
                                    bool isAnomaly = (index == InspectionMethod::ANOMALY);
                                    bool isDiff = (index == InspectionMethod::DIFF);
                                    
                                    // SpinBox는 DIFF에서만 표시
                                    insPassThreshSpin->setVisible(isDiff);
                                    insPassThreshLabel->setVisible(isDiff || isAnomaly);
                                    
                                    // 라벨 텍스트 변경
                                    if (isAnomaly)
                                    {
                                        insPassThreshLabel->setText("임계값(%):");
                                    }
                                    else
                                    {
                                        insPassThreshLabel->setText("합격 임계값(%):");
                                    }
                                }
                                
                                // 슬라이더는 ANOMALY에서만 표시
                                if (insPassThreshSlider)
                                {
                                    insPassThreshSlider->parentWidget()->setVisible(index == InspectionMethod::ANOMALY);
                                }

                                // SSIM 설정 위젯 표시/숨김
                                if (ssimSettingsWidget)
                                {
                                    ssimSettingsWidget->setVisible(index == InspectionMethod::SSIM);
                                }

                                // ANOMALY 설정 위젯 표시/숨김
                                if (anomalySettingsWidget)
                                {
                                    anomalySettingsWidget->setVisible(index == InspectionMethod::ANOMALY);
                                }

                                // 패턴 매칭 패널 표시 설정
                                if (insPatternMatchPanel)
                                {
                                    insPatternMatchPanel->setVisible(index == InspectionMethod::DIFF && pattern->runInspection);
                                }

                                cameraView->updatePatternById(patternId, *pattern);
                                
                                // SSIM 선택 시 필터 실시간 업데이트
                                if (index == InspectionMethod::SSIM)
                                {
                                    updateCameraFrame();
                                }
                            }
                        }
                    }
                });
    }

    // INS 회전 체크박스
    if (insRotationCheck)
    {
        connect(insRotationCheck, &QCheckBox::toggled, [this](bool checked)
                {
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
            } });
    }

    // INS 최소 회전 각도
    if (insMinAngleSpin)
    {
        connect(insMinAngleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                [this](double value)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->minAngle = value;
                                cameraView->updatePatternById(patternId, *pattern);
                            }
                        }
                    }
                });
    }

    // INS 최대 회전 각도
    if (insMaxAngleSpin)
    {
        connect(insMaxAngleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                [this](double value)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->maxAngle = value;
                                cameraView->updatePatternById(patternId, *pattern);
                            }
                        }
                    }
                });
    }

    // INS 회전 간격
    if (insAngleStepSpin)
    {
        connect(insAngleStepSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                [this](double value)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->angleStep = value;
                                cameraView->updatePatternById(patternId, *pattern);
                            }
                        }
                    }
                });
    }

    // 위치 및 크기 변경 연결
    auto connectPatternSpinBox = [this](QSpinBox *spinBox, std::function<void(int)> updateFunc)
    {
        if (spinBox)
        {
            connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), [this, updateFunc](int value)
                    {
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
                } });
        }
    };

    connectPatternSpinBox(patternXSpin, [this](int value)
                          {
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
        } });

    connectPatternSpinBox(patternYSpin, [this](int value)
                          {
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
        } });

    connectPatternSpinBox(patternWSpin, [this](int value)
                          {
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
        } });

    connectPatternSpinBox(patternHSpin, [this](int value)
                          {
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
        } });

    // 이진화 검사 관련 연결
    // 이진화 임계값

    // === STRIP 검사 파라미터 이벤트 연결 ===

    // 컨투어 마진
    // 형태학적 커널 크기
    if (insStripKernelSpin)
    {
        connect(insStripKernelSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                [this](int value)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                // 홀수로 강제 조정
                                if (value % 2 == 0)
                                    value++;
                                pattern->stripMorphKernelSize = value;
                                cameraView->updatePatternById(patternId, *pattern);
                            }
                        }
                    }
                });
    }

    // Gradient 임계값
    if (insStripGradThreshSpin)
    {
        connect(insStripGradThreshSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                [this](double value)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->stripGradientThreshold = static_cast<float>(value);
                                cameraView->updatePatternById(patternId, *pattern);
                            }
                        }
                    }
                });
    }

    // Gradient 시작 지점 슬라이더
    if (insStripStartSlider)
    {
        connect(insStripStartSlider, &QSlider::valueChanged,
                [this](int value)
                {
                    // 값 표시 레이블 업데이트
                    if (insStripStartValueLabel)
                    {
                        insStripStartValueLabel->setText(QString("%1%").arg(value));
                    }

                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->stripGradientStartPercent = value;
                                cameraView->updatePatternById(patternId, *pattern);
                                // 카메라뷰 다시 그리기 (점선 위치 업데이트)
                                cameraView->update();
                            }
                        }
                    }
                });
    }

    // Gradient 끝 지점 슬라이더
    if (insStripEndSlider)
    {
        connect(insStripEndSlider, &QSlider::valueChanged,
                [this](int value)
                {
                    // 값 표시 레이블 업데이트
                    if (insStripEndValueLabel)
                    {
                        insStripEndValueLabel->setText(QString("%1%").arg(value));
                    }

                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->stripGradientEndPercent = value;
                                cameraView->updatePatternById(patternId, *pattern);
                                // 카메라뷰 다시 그리기 (점선 위치 업데이트)
                                cameraView->update();
                            }
                        }
                    }
                });
    }

    // 최소 데이터 포인트
    if (insStripMinPointsSpin)
    {
        connect(insStripMinPointsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                [this](int value)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->stripMinDataPoints = value;
                                cameraView->updatePatternById(patternId, *pattern);
                            }
                        }
                    }
                });
    }

    // 두께 측정 박스 너비
    if (insStripThicknessWidthSlider)
    {
        connect(insStripThicknessWidthSlider, &QSlider::valueChanged,
                [this](int value)
                {
                    // 값 표시 레이블 업데이트
                    if (insStripThicknessWidthValueLabel)
                    {
                        insStripThicknessWidthValueLabel->setText(QString("%1px").arg(value));
                    }

                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->stripThicknessBoxWidth = value;
                                cameraView->updatePatternById(patternId, *pattern);
                                cameraView->update();
                            }
                        }
                    }
                });
    }

    // 두께 측정 박스 높이
    if (insStripThicknessHeightSlider)
    {
        connect(insStripThicknessHeightSlider, &QSlider::valueChanged,
                [this](int value)
                {
                    // 값 표시 레이블 업데이트
                    if (insStripThicknessHeightValueLabel)
                    {
                        insStripThicknessHeightValueLabel->setText(QString("%1px").arg(value));
                    }

                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->stripThicknessBoxHeight = value;
                                cameraView->updatePatternById(patternId, *pattern);
                                cameraView->update();
                            }
                        }
                    }
                });
    }

    // 최소 두께
    if (insStripThicknessMinEdit)
    {
        connect(insStripThicknessMinEdit, &QLineEdit::textChanged,
                [this](const QString &text)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                bool ok;
                                double value = text.toDouble(&ok);
                                if (ok)
                                {
                                    pattern->stripThicknessMin = value;
                                    cameraView->updatePatternById(patternId, *pattern);
                                    cameraView->update();
                                }
                            }
                        }
                    }
                });
    }

    // 최대 두께
    if (insStripThicknessMaxEdit)
    {
        connect(insStripThicknessMaxEdit, &QLineEdit::textChanged,
                [this](const QString &text)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                bool ok;
                                double value = text.toDouble(&ok);
                                if (ok)
                                {
                                    pattern->stripThicknessMax = value;
                                    cameraView->updatePatternById(patternId, *pattern);
                                    cameraView->update();
                                }
                            }
                        }
                    }
                });
    }

    // REAR 두께 측정 박스 너비
    if (insStripRearThicknessWidthSlider)
    {
        connect(insStripRearThicknessWidthSlider, &QSlider::valueChanged,
                [this](int value)
                {
                    // 값 표시 레이블 업데이트
                    if (insStripRearThicknessWidthValueLabel)
                    {
                        insStripRearThicknessWidthValueLabel->setText(QString("%1px").arg(value));
                    }

                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->stripRearThicknessBoxWidth = value;
                                cameraView->updatePatternById(patternId, *pattern);
                                cameraView->update();
                            }
                        }
                    }
                });
    }

    // REAR 두께 측정 박스 높이
    if (insStripRearThicknessHeightSlider)
    {
        connect(insStripRearThicknessHeightSlider, &QSlider::valueChanged,
                [this](int value)
                {
                    // 값 표시 레이블 업데이트
                    if (insStripRearThicknessHeightValueLabel)
                    {
                        insStripRearThicknessHeightValueLabel->setText(QString("%1px").arg(value));
                    }

                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->stripRearThicknessBoxHeight = value;
                                cameraView->updatePatternById(patternId, *pattern);
                                cameraView->update();
                            }
                        }
                    }
                });
    }

    // REAR 최소 두께
    if (insStripRearThicknessMinEdit)
    {
        connect(insStripRearThicknessMinEdit, &QLineEdit::textChanged,
                [this](const QString &text)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                bool ok;
                                double value = text.toDouble(&ok);
                                if (ok)
                                {
                                    pattern->stripRearThicknessMin = value;
                                    cameraView->updatePatternById(patternId, *pattern);
                                    cameraView->update();
                                }
                            }
                        }
                    }
                });
    }

    // REAR 최대 두께
    if (insStripRearThicknessMaxEdit)
    {
        connect(insStripRearThicknessMaxEdit, &QLineEdit::textChanged,
                [this](const QString &text)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                bool ok;
                                double value = text.toDouble(&ok);
                                if (ok)
                                {
                                    pattern->stripRearThicknessMax = value;
                                    cameraView->updatePatternById(patternId, *pattern);
                                    cameraView->update();
                                }
                            }
                        }
                    }
                });
    }

    // STRIP 길이검사 활성화
    if (insStripLengthEnabledCheck)
    {
        connect(insStripLengthEnabledCheck, &QCheckBox::toggled, [this](bool enabled)
                {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->stripLengthEnabled = enabled;
                        
                        // 길이검사 관련 위젯들 활성화/비활성화
                        if (insStripLengthMinEdit) insStripLengthMinEdit->setEnabled(enabled);
                        if (insStripLengthMaxEdit) insStripLengthMaxEdit->setEnabled(enabled);
                        if (insStripLengthConversionEdit) insStripLengthConversionEdit->setEnabled(enabled);
                        
                        cameraView->updatePatternById(patternId, *pattern);
                        cameraView->update();
                    }
                }
            } });

        // 길이검사 최소값 변경 이벤트
        connect(insStripLengthMinEdit, &QLineEdit::textChanged, [this](const QString &text)
                {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        bool ok;
                        double value = text.toDouble(&ok);
                        if (ok) {
                            pattern->stripLengthMin = value;
                            cameraView->updatePatternById(patternId, *pattern);
                            cameraView->update();
                            qDebug() << "[UI 변경] stripLengthMin 갱신:" << pattern->name << "->" << value;
                        }
                    }
                }
            } });

        // 길이검사 최대값 변경 이벤트
        connect(insStripLengthMaxEdit, &QLineEdit::textChanged, [this](const QString &text)
                {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        bool ok;
                        double value = text.toDouble(&ok);
                        if (ok) {
                            pattern->stripLengthMax = value;
                            cameraView->updatePatternById(patternId, *pattern);
                            cameraView->update();
                            qDebug() << "[UI 변경] stripLengthMax 갱신:" << pattern->name << "->" << value;
                        }
                    }
                }
            } });
    }

    // 길이검사 수치 변환 변경 이벤트
    if (insStripLengthConversionEdit)
    {
        connect(insStripLengthConversionEdit, &QLineEdit::textChanged, [this](const QString &text)
                {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        bool ok;
                        double value = text.toDouble(&ok);
                        if (ok) {
                            pattern->stripLengthConversionMm = value;
                            cameraView->updatePatternById(patternId, *pattern);
                        }
                    }
                }
            } });
    }

    // 길이 측정값 갱신 버튼
    if (insStripLengthRefreshButton)
    {
        connect(insStripLengthRefreshButton, &QPushButton::clicked, [this]()
                {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        // 마지막 검사 결과에서 측정된 길이 가져오기
                        const InspectionResult& result = cameraView->getLastInspectionResult();
                        if (result.stripMeasuredLengthPx.contains(patternId)) {
                            double pixelLength = result.stripMeasuredLengthPx[patternId];
                            bool ok;
                            double mmLength = insStripLengthConversionEdit->text().toDouble(&ok);
                            if (!ok || mmLength <= 0) return;
                            
                            pattern->stripLengthConversionMm = mmLength;
                            pattern->stripLengthCalibrationPx = pixelLength;
                            pattern->stripLengthCalibrated = true;
                            
                            cameraView->updatePatternById(patternId, *pattern);
                            
                            double conversionRatio = pixelLength / mmLength;
                            
                            if (insStripLengthMeasuredLabel) {
                                insStripLengthMeasuredLabel->setText(
                                    QString("측정값: %1 px (%2 px/mm)").arg(pixelLength, 0, 'f', 1).arg(conversionRatio, 0, 'f', 2)
                                );
                            }
                            
                            insStripLengthRefreshButton->setStyleSheet(
                                "QPushButton { background-color: #4CAF50; color: white; font-weight: bold; }"
                                "QPushButton:hover { background-color: #45a049; }"
                            );
                            
                            cameraView->update();
                            update();
                            
                        } else {
                            if (insStripLengthMeasuredLabel) {
                                insStripLengthMeasuredLabel->setText("측정값: 검사 필요");
                            }
                        }
                    }
                }
            } });
    }

    // FRONT 두께 검사 활성화
    if (insStripFrontEnabledCheck)
    {
        connect(insStripFrontEnabledCheck, &QGroupBox::toggled, [this](bool enabled)
                {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->stripFrontEnabled = enabled;
                        
                        // FRONT 관련 위젯들 활성화/비활성화
                        if (insStripThicknessWidthSlider) insStripThicknessWidthSlider->setEnabled(enabled);
                        if (insStripThicknessHeightSlider) insStripThicknessHeightSlider->setEnabled(enabled);
                        if (insStripThicknessMinEdit) insStripThicknessMinEdit->setEnabled(enabled);
                        if (insStripThicknessMaxEdit) insStripThicknessMaxEdit->setEnabled(enabled);
                        
                        cameraView->updatePatternById(patternId, *pattern);
                        cameraView->update();
                    }
                }
            } });
    }

    // REAR 두께 검사 활성화
    if (insStripRearEnabledCheck)
    {
        connect(insStripRearEnabledCheck, &QGroupBox::toggled, [this](bool enabled)
                {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->stripRearEnabled = enabled;
                        
                        // REAR 관련 위젯들 활성화/비활성화
                        if (insStripRearThicknessWidthSlider) insStripRearThicknessWidthSlider->setEnabled(enabled);
                        if (insStripRearThicknessHeightSlider) insStripRearThicknessHeightSlider->setEnabled(enabled);
                        if (insStripRearThicknessMinEdit) insStripRearThicknessMinEdit->setEnabled(enabled);
                        if (insStripRearThicknessMaxEdit) insStripRearThicknessMaxEdit->setEnabled(enabled);
                        
                        cameraView->updatePatternById(patternId, *pattern);
                        cameraView->update();
                    }
                }
            } });
    }

    // EDGE 검사 활성화
    if (insEdgeEnabledCheck)
    {
        connect(insEdgeEnabledCheck, &QGroupBox::toggled, [this](bool enabled)
                {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->edgeEnabled = enabled;
                        cameraView->updatePatternById(patternId, *pattern);
                        cameraView->update();
                    }
                }
            } });
    }

    // EDGE 오프셋 X
    if (insEdgeOffsetXSlider)
    {
        connect(insEdgeOffsetXSlider, &QSlider::valueChanged,
                [this](int value)
                {
                    insEdgeOffsetXValueLabel->setText(QString("%1px").arg(value));

                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->edgeOffsetX = value;
                                cameraView->updatePatternById(patternId, *pattern);
                                cameraView->update();
                            }
                        }
                    }
                });
    }

    // EDGE 박스 너비
    if (insEdgeWidthSlider)
    {

        connect(insEdgeWidthSlider, &QSlider::valueChanged,
                this, [this](int value)
                {
            // 값 표시 레이블 업데이트
            if (insEdgeWidthValueLabel) {
                insEdgeWidthValueLabel->setText(QString("%1px").arg(value));
            }
            
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->stripEdgeBoxWidth = value;
                        // 메인 카메라뷰만 간단히 갱신 (패턴 전체 업데이트 불필요)
                        cameraView->update();
                    }
                }
            } });
    }

    // EDGE 박스 높이
    if (insEdgeHeightSlider)
    {

        connect(insEdgeHeightSlider, &QSlider::valueChanged,
                this, [this](int value)
                {
            // 값 표시 레이블 업데이트
            if (insEdgeHeightValueLabel) {
                insEdgeHeightValueLabel->setText(QString("%1px").arg(value));
            }
            
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->stripEdgeBoxHeight = value;
                        // 메인 카메라뷰만 간단히 갱신 (패턴 전체 업데이트 불필요)
                        cameraView->update();
                    }
                }
            } });
    }

    // EDGE 불규칙성 임계값
    // insEdgeThresholdSpin 연결 코드 제거됨 (통계적 방법 사용)

    // EDGE 최대 불량 개수
    if (insEdgeMaxIrregularitiesSpin)
    {
        connect(insEdgeMaxIrregularitiesSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                [this](int value)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->edgeMaxOutliers = value;
                                cameraView->updatePatternById(patternId, *pattern);
                                cameraView->update();
                            }
                        }
                    }
                });
    }

    // EDGE 평균선 최대 거리
    if (insEdgeDistanceMaxEdit)
    {
        connect(insEdgeDistanceMaxEdit, &QLineEdit::textChanged,
                [this](const QString &text)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                bool ok;
                                double value = QLocale::c().toDouble(text, &ok);

                                if (ok)
                                {
                                    pattern->edgeDistanceMax = value;

                                    cameraView->updatePatternById(patternId, *pattern);
                                    cameraView->update();
                                }
                                else
                                {
                                }
                            }
                        }
                    }
                });
    }

    // EDGE 시작 제외 퍼센트
    if (insEdgeStartPercentSpin)
    {
        connect(insEdgeStartPercentSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                [this](int value)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->edgeStartPercent = value;
                                cameraView->updatePatternById(patternId, *pattern);
                                cameraView->update();
                            }
                        }
                    }
                });
    }

    // EDGE 끝 제외 퍼센트
    if (insEdgeEndPercentSpin)
    {
        connect(insEdgeEndPercentSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                [this](int value)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QUuid patternId = getPatternIdFromItem(selectedItem);
                        if (!patternId.isNull())
                        {
                            PatternInfo *pattern = cameraView->getPatternById(patternId);
                            if (pattern && pattern->type == PatternType::INS)
                            {
                                pattern->edgeEndPercent = value;
                                cameraView->updatePatternById(patternId, *pattern);
                                cameraView->update();
                            }
                        }
                    }
                });
    }

    // === 왼쪽 스트리핑 길이 검사 신호 연결 ===
    if (insBarrelLeftStripGroup)
    {
        connect(insBarrelLeftStripGroup, &QGroupBox::toggled, [this](bool checked)
                {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->barrelLeftStripEnabled = checked;
                        cameraView->updatePatternById(patternId, *pattern);
                        cameraView->update();
                    }
                }
            } });
    }

    if (insBarrelLeftStripOffsetSlider && insBarrelLeftStripOffsetValueLabel)
    {
        connect(insBarrelLeftStripOffsetSlider, &QSlider::valueChanged, [this](int value)
                {
            insBarrelLeftStripOffsetValueLabel->setText(QString::number(value) + "px");
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->barrelLeftStripOffsetX = value;
                        cameraView->updatePatternById(patternId, *pattern);
                        cameraView->update();
                    }
                }
            } });
    }

    if (insBarrelLeftStripWidthSlider && insBarrelLeftStripWidthValueLabel)
    {
        connect(insBarrelLeftStripWidthSlider, &QSlider::valueChanged, [this](int value)
                {
            insBarrelLeftStripWidthValueLabel->setText(QString::number(value) + "px");
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->barrelLeftStripBoxWidth = value;
                        cameraView->updatePatternById(patternId, *pattern);
                        cameraView->update();
                    }
                }
            } });
    }

    if (insBarrelLeftStripHeightSlider && insBarrelLeftStripHeightValueLabel)
    {
        connect(insBarrelLeftStripHeightSlider, &QSlider::valueChanged, [this](int value)
                {
            insBarrelLeftStripHeightValueLabel->setText(QString::number(value) + "px");
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->barrelLeftStripBoxHeight = value;
                        cameraView->updatePatternById(patternId, *pattern);
                        cameraView->update();
                    }
                }
            } });
    }

    if (insBarrelLeftStripMinEdit)
    {
        connect(insBarrelLeftStripMinEdit, &QLineEdit::textChanged, [this](const QString &text)
                {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        bool ok;
                        double value = text.toDouble(&ok);
                        if (ok) {
                            pattern->barrelLeftStripLengthMin = value;
                            cameraView->updatePatternById(patternId, *pattern);
                            cameraView->update();
                        }
                    }
                }
            } });
    }

    if (insBarrelLeftStripMaxEdit)
    {
        connect(insBarrelLeftStripMaxEdit, &QLineEdit::textChanged, [this](const QString &text)
                {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        bool ok;
                        double value = text.toDouble(&ok);
                        if (ok) {
                            pattern->barrelLeftStripLengthMax = value;
                            cameraView->updatePatternById(patternId, *pattern);
                            cameraView->update();
                        }
                    }
                }
            } });
    }

    // === 오른쪽 스트리핑 길이 검사 신호 연결 ===
    if (insBarrelRightStripGroup)
    {
        connect(insBarrelRightStripGroup, &QGroupBox::toggled, [this](bool checked)
                {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->barrelRightStripEnabled = checked;
                        cameraView->updatePatternById(patternId, *pattern);
                        cameraView->update();
                    }
                }
            } });
    }

    if (insBarrelRightStripOffsetSlider && insBarrelRightStripOffsetValueLabel)
    {
        connect(insBarrelRightStripOffsetSlider, &QSlider::valueChanged, [this](int value)
                {
            insBarrelRightStripOffsetValueLabel->setText(QString::number(value) + "px");
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->barrelRightStripOffsetX = value;
                        cameraView->updatePatternById(patternId, *pattern);
                        cameraView->update();
                    }
                }
            } });
    }

    if (insBarrelRightStripWidthSlider && insBarrelRightStripWidthValueLabel)
    {
        connect(insBarrelRightStripWidthSlider, &QSlider::valueChanged, [this](int value)
                {
            insBarrelRightStripWidthValueLabel->setText(QString::number(value) + "px");
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->barrelRightStripBoxWidth = value;
                        cameraView->updatePatternById(patternId, *pattern);
                        cameraView->update();
                    }
                }
            } });
    }

    if (insBarrelRightStripHeightSlider && insBarrelRightStripHeightValueLabel)
    {
        connect(insBarrelRightStripHeightSlider, &QSlider::valueChanged, [this](int value)
                {
            insBarrelRightStripHeightValueLabel->setText(QString::number(value) + "px");
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        pattern->barrelRightStripBoxHeight = value;
                        cameraView->updatePatternById(patternId, *pattern);
                        cameraView->update();
                    }
                }
            } });
    }

    if (insBarrelRightStripMinEdit)
    {
        connect(insBarrelRightStripMinEdit, &QLineEdit::textChanged, [this](const QString &text)
                {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        bool ok;
                        double value = text.toDouble(&ok);
                        if (ok) {
                            pattern->barrelRightStripLengthMin = value;
                            cameraView->updatePatternById(patternId, *pattern);
                            cameraView->update();
                        }
                    }
                }
            } });
    }

    if (insBarrelRightStripMaxEdit)
    {
        connect(insBarrelRightStripMaxEdit, &QLineEdit::textChanged, [this](const QString &text)
                {
            QTreeWidgetItem* selectedItem = patternTree->currentItem();
            if (selectedItem) {
                QUuid patternId = getPatternIdFromItem(selectedItem);
                if (!patternId.isNull()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::INS) {
                        bool ok;
                        double value = text.toDouble(&ok);
                        if (ok) {
                            pattern->barrelRightStripLengthMax = value;
                            cameraView->updatePatternById(patternId, *pattern);
                            cameraView->update();
                        }
                    }
                }
            } });
    }

    // 패턴 각도 텍스트박스
    if (angleEdit)
    {
        connect(angleEdit, &QLineEdit::textChanged, [this](const QString &text)
                {
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
            } });
    }
}

void TeachingWidget::updatePropertyPanel(PatternInfo *pattern, const FilterInfo *filter, const QUuid &patternId, int filterIndex)
{
    // 필터가 제공된 경우 필터 속성 패널 표시
    if (filter)
    {
        propertyStackWidget->setCurrentIndex(2);

        if (!filterPropertyContainer)
        {
            return;
        }

        // 기존 필터 위젯 모두 제거
        QLayout *containerLayout = filterPropertyContainer->layout();
        if (containerLayout)
        {
            QLayoutItem *item;
            while ((item = containerLayout->takeAt(0)) != nullptr)
            {
                if (item->widget())
                {
                    item->widget()->deleteLater();
                }
                delete item;
            }
        }

        // 필터 타입에 맞는 FilterPropertyWidget 생성
        FilterPropertyWidget *filterPropWidget = new FilterPropertyWidget(filter->type, filterPropertyContainer);

        // 필터 정보로 속성 설정
        filterPropWidget->setParams(filter->params);
        filterPropWidget->setEnabled(filter->enabled);

        // 레이아웃에 추가
        containerLayout->addWidget(filterPropWidget);

        // 공간 추가
        QWidget *spacer = new QWidget(filterPropertyContainer);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        containerLayout->addWidget(spacer);

        // 파라미터 변경 이벤트 연결
        connect(filterPropWidget, &FilterPropertyWidget::paramChanged,
                [this, patternId, filterIndex](const QString &paramName, int value)
                {
                    updateFilterParam(patternId, filterIndex, paramName, value);
                });

        // 필터 활성화 상태 변경 이벤트 연결
        connect(filterPropWidget, &FilterPropertyWidget::enableStateChanged,
                [this, patternId, filterIndex](bool enabled)
                {
                    // 필터 활성화 상태 변경
                    cameraView->setPatternFilterEnabled(patternId, filterIndex, enabled);

                    // 체크박스 상태 업데이트 (트리 아이템)
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        selectedItem->setCheckState(0, enabled ? Qt::Checked : Qt::Unchecked);
                    }
                });

        return; // 필터 프로퍼티 패널을 표시했으므로 여기서 함수 종료
    }

    // 패턴 없으면 빈 패널 표시
    if (!pattern)
    {
        propertyStackWidget->setCurrentIndex(0);
        return;
    }

    // 패턴 타입에 따른 프로퍼티 패널 (기존 코드와 동일)
    if (propertyStackWidget)
    {
        // 패턴 패널로 전환
        propertyStackWidget->setCurrentIndex(1);

        // 기본 정보 설정
        if (patternIdValue)
        {
            // ID 필드에는 패턴 ID 표시 (UUID)
            patternIdValue->setText(pattern->id.toString());
        }

        if (patternNameEdit)
        {
            // 이름 필드에는 패턴 이름 표시
            patternNameEdit->setText(pattern->name);
        }

        if (patternCameraValue)
        {
            // 카메라 번호 표시 (무조건 0, 1, 2, 3으로 표시)
            QString cameraNumber = "알 수 없음";
            if (pattern->frameIndex >= 0 && pattern->frameIndex < 4)
            {
                cameraNumber = QString::number(pattern->frameIndex);
            }
            patternCameraValue->setText(cameraNumber);
        }

        if (patternTypeValue)
        {
            QString typeText;
            QColor typeColor;

            switch (pattern->type)
            {
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
            
            // 프레임 인덱스에 맞는 레이블 추가
            QStringList frameLabels = {"STAGE 1 - STRIP", "STAGE 1 - CRIMP", "STAGE 2 - STRIP", "STAGE 2 - CRIMP"};
            if (pattern->frameIndex >= 0 && pattern->frameIndex < 4) {
                typeText += " | " + frameLabels[pattern->frameIndex];
            }

            patternTypeValue->setText(typeText);
            patternTypeValue->setStyleSheet(QString("background-color: %1; color: %2; border-radius: 3px; padding: 2px 5px;")
                                                .arg(typeColor.name())
                                                .arg(UIColors::getTextColor(typeColor).name()));
        }

        // 위치 정보 업데이트
        if (patternXSpin)
        {
            patternXSpin->blockSignals(true);
            patternXSpin->setValue(pattern->rect.x());
            patternXSpin->blockSignals(false);
        }

        if (patternYSpin)
        {
            patternYSpin->blockSignals(true);
            patternYSpin->setValue(pattern->rect.y());
            patternYSpin->blockSignals(false);
        }

        if (patternWSpin)
        {
            patternWSpin->blockSignals(true);
            patternWSpin->setValue(pattern->rect.width());
            patternWSpin->blockSignals(false);
        }

        if (patternHSpin)
        {
            patternHSpin->blockSignals(true);
            patternHSpin->setValue(pattern->rect.height());
            patternHSpin->blockSignals(false);
        }

        // 각도 정보 업데이트
        if (angleEdit)
        {
            angleEdit->blockSignals(true);
            angleEdit->setText(QString::number(pattern->angle, 'f', 1));
            angleEdit->blockSignals(false);
        }

        // 패턴 타입별 특수 속성 설정
        if (specialPropStack)
        {
            switch (pattern->type)
            {
            case PatternType::ROI:
            {
                specialPropStack->setCurrentIndex(0);
                // includeAllCameraCheck 제거됨
                break;
            }
            case PatternType::FID:
            {
                specialPropStack->setCurrentIndex(1);

                // FID 속성 업데이트
                if (fidMatchMethodCombo)
                {
                    fidMatchMethodCombo->setCurrentIndex(pattern->fidMatchMethod);
                }

                if (fidMatchCheckBox)
                {
                    fidMatchCheckBox->setChecked(pattern->runInspection);
                }
                if (fidMatchThreshSpin)
                {
                    fidMatchThreshSpin->setValue(pattern->matchThreshold); // 100% 단위 그대로 표시
                }

                if (fidRotationCheck)
                {
                    fidRotationCheck->setChecked(pattern->useRotation);
                }

                if (fidMinAngleSpin)
                {
                    fidMinAngleSpin->setValue(pattern->minAngle);
                }

                if (fidMaxAngleSpin)
                {
                    fidMaxAngleSpin->setValue(pattern->maxAngle);
                }

                if (fidStepSpin)
                {
                    fidStepSpin->setValue(pattern->angleStep);
                }

                // 템플릿 이미지 업데이트
                if (fidTemplateImg)
                {
                    if (!pattern->templateImage.isNull())
                    {
                        fidTemplateImg->setPixmap(QPixmap::fromImage(pattern->templateImage.scaled(
                            fidTemplateImg->width(), fidTemplateImg->height(), Qt::KeepAspectRatio)));
                        fidTemplateImg->setText(""); // 이미지가 있을 때는 텍스트 삭제
                    }
                    else
                    {
                        fidTemplateImg->setPixmap(QPixmap()); // 빈 픽스맵으로 설정
                        fidTemplateImg->setText(TR("NO_IMAGE"));
                    }
                }
                break;
            }
            case PatternType::INS:
            {
                specialPropStack->setCurrentIndex(2);

                // 검사 방법 콤보박스 설정
                if (insMethodCombo)
                {
                    insMethodCombo->blockSignals(true);
                    insMethodCombo->setCurrentIndex(pattern->inspectionMethod);
                    insMethodCombo->blockSignals(false);
                }
                
                // SSIM 설정 위젯 표시/숨김 및 값 설정
                if (ssimSettingsWidget)
                {
                    bool isSSIM = (pattern->inspectionMethod == InspectionMethod::SSIM);
                    ssimSettingsWidget->setVisible(isSSIM);
                    
                    if (isSSIM && ssimNgThreshSlider)
                    {
                        ssimNgThreshSlider->blockSignals(true);
                        ssimNgThreshSlider->setValue(static_cast<int>(pattern->ssimNgThreshold));
                        ssimNgThreshSlider->blockSignals(false);
                        
                        if (ssimNgThreshValue)
                            ssimNgThreshValue->setText(QString("%1%").arg(static_cast<int>(pattern->ssimNgThreshold)));
                        
                        // 허용 NG 비율 슬라이더도 업데이트
                        if (allowedNgRatioSlider)
                        {
                            allowedNgRatioSlider->blockSignals(true);
                            allowedNgRatioSlider->setValue(static_cast<int>(pattern->allowedNgRatio));
                            allowedNgRatioSlider->blockSignals(false);
                            
                            if (allowedNgRatioValue)
                                allowedNgRatioValue->setText(QString("%1%").arg(static_cast<int>(pattern->allowedNgRatio)));
                        }
                    }
                }

                // ANOMALY 설정 위젯 표시/숨김 및 값 설정
                if (anomalySettingsWidget)
                {
                    bool isANOMALY = (pattern->inspectionMethod == InspectionMethod::ANOMALY);
                    anomalySettingsWidget->setVisible(isANOMALY);
                    
                    if (isANOMALY)
                    {
                        if (anomalyMinBlobSizeSpin)
                        {
                            anomalyMinBlobSizeSpin->blockSignals(true);
                            anomalyMinBlobSizeSpin->setValue(pattern->anomalyMinBlobSize);
                            anomalyMinBlobSizeSpin->blockSignals(false);
                        }
                        
                        if (anomalyMinDefectWidthSpin)
                        {
                            anomalyMinDefectWidthSpin->blockSignals(true);
                            anomalyMinDefectWidthSpin->setValue(pattern->anomalyMinDefectWidth);
                            anomalyMinDefectWidthSpin->blockSignals(false);
                        }
                        
                        if (anomalyMinDefectHeightSpin)
                        {
                            anomalyMinDefectHeightSpin->blockSignals(true);
                            anomalyMinDefectHeightSpin->setValue(pattern->anomalyMinDefectHeight);
                            anomalyMinDefectHeightSpin->blockSignals(false);
                        }
                        
                        // Train 버튼 상태 업데이트 (모델 파일 존재 여부 확인)
                        if (anomalyTrainButton)
                        {
                            QString modelPath = QCoreApplication::applicationDirPath() + QString("/weights/%1/%1.xml").arg(pattern->name);
                            QFileInfo modelFile(modelPath);
                            
                            if (modelFile.exists())
                            {
                                // 모델 파일이 있으면 "Trained" (빨간색)
                                anomalyTrainButton->setText("Trained");
                                anomalyTrainButton->setStyleSheet(
                                    "QPushButton { background-color: #f44336; color: white; font-weight: bold; padding: 5px; border-radius: 3px; }"
                                    "QPushButton:hover { background-color: #da190b; }"
                                    "QPushButton:pressed { background-color: #c0180a; }");
                            }
                            else
                            {
                                // 모델 파일이 없으면 "Train" (녹색)
                                anomalyTrainButton->setText("Train");
                                anomalyTrainButton->setStyleSheet(
                                    "QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 5px; border-radius: 3px; }"
                                    "QPushButton:hover { background-color: #45a049; }"
                                    "QPushButton:pressed { background-color: #3d8b40; }");
                            }
                        }
                    }
                }

                // 패턴 매칭 그룹 표시/숨김 및 값 설정
                if (insPatternMatchGroup)
                {
                    // FID 자식이 있는 INS 패턴만 표시
                    bool hasFidParent = false;
                    if (!pattern->parentId.isNull())
                    {
                        PatternInfo* parent = cameraView->getPatternById(pattern->parentId);
                        if (parent && parent->type == PatternType::FID)
                        {
                            hasFidParent = true;
                        }
                    }
                    
                    insPatternMatchGroup->setVisible(hasFidParent);
                    
                    if (hasFidParent)
                    {
                        insPatternMatchGroup->blockSignals(true);
                        insPatternMatchGroup->setChecked(pattern->patternMatchEnabled);
                        insPatternMatchGroup->blockSignals(false);
                        
                        if (insPatternMatchMethodCombo)
                        {
                            insPatternMatchMethodCombo->blockSignals(true);
                            insPatternMatchMethodCombo->setCurrentIndex(pattern->patternMatchMethod);
                            insPatternMatchMethodCombo->blockSignals(false);
                        }
                        
                        if (insPatternMatchThreshSpin)
                        {
                            insPatternMatchThreshSpin->blockSignals(true);
                            insPatternMatchThreshSpin->setValue(pattern->patternMatchThreshold);
                            insPatternMatchThreshSpin->blockSignals(false);
                        }
                        
                        if (insPatternMatchRotationCheck)
                        {
                            insPatternMatchRotationCheck->blockSignals(true);
                            insPatternMatchRotationCheck->setChecked(pattern->useRotation);
                            insPatternMatchRotationCheck->blockSignals(false);
                        }
                        
                        if (insPatternMatchMinAngleSpin)
                        {
                            insPatternMatchMinAngleSpin->blockSignals(true);
                            insPatternMatchMinAngleSpin->setValue(pattern->minAngle);
                            insPatternMatchMinAngleSpin->setEnabled(pattern->useRotation);
                            insPatternMatchMinAngleSpin->blockSignals(false);
                        }
                        
                        if (insPatternMatchMaxAngleSpin)
                        {
                            insPatternMatchMaxAngleSpin->blockSignals(true);
                            insPatternMatchMaxAngleSpin->setValue(pattern->maxAngle);
                            insPatternMatchMaxAngleSpin->setEnabled(pattern->useRotation);
                            insPatternMatchMaxAngleSpin->blockSignals(false);
                        }
                        
                        if (insPatternMatchStepSpin)
                        {
                            insPatternMatchStepSpin->blockSignals(true);
                            insPatternMatchStepSpin->setValue(pattern->angleStep);
                            insPatternMatchStepSpin->setEnabled(pattern->useRotation);
                            insPatternMatchStepSpin->blockSignals(false);
                        }
                    }
                }
                
                if (insRotationCheck)
                {
                    insRotationCheck->setChecked(pattern->useRotation);
                }

                if (insMinAngleSpin)
                {
                    insMinAngleSpin->setValue(pattern->minAngle);
                }

                if (insMaxAngleSpin)
                {
                    insMaxAngleSpin->setValue(pattern->maxAngle);
                }

                if (insAngleStepSpin)
                {
                    insAngleStepSpin->setValue(pattern->angleStep);
                }

                if (insPassThreshSpin)
                {
                    bool isAnomaly = (pattern->inspectionMethod == InspectionMethod::ANOMALY);
                    bool isDiff = (pattern->inspectionMethod == InspectionMethod::DIFF);
                    
                    // SpinBox는 DIFF에서만 표시
                    insPassThreshSpin->setVisible(isDiff);
                    insPassThreshLabel->setVisible(isDiff || isAnomaly);
                    
                    // 라벨 텍스트 변경
                    if (isAnomaly)
                    {
                        insPassThreshLabel->setText("임계값(%):");
                    }
                    else
                    {
                        insPassThreshLabel->setText("합격 임계값(%):");
                    }
                    
                    if (isDiff)
                    {
                        insPassThreshSpin->setValue(pattern->passThreshold);
                    }
                }
                
                // 슬라이더 값 설정 (ANOMALY)
                if (insPassThreshSlider)
                {
                    bool isAnomaly = (pattern->inspectionMethod == InspectionMethod::ANOMALY);
                    insPassThreshSlider->parentWidget()->setVisible(isAnomaly);
                    
                    if (isAnomaly)
                    {
                        insPassThreshSlider->setValue(static_cast<int>(pattern->passThreshold));
                        if (insPassThreshValue)
                        {
                            insPassThreshValue->setText(QString("%1%").arg(static_cast<int>(pattern->passThreshold)));
                        }
                    }
                }

                // STRIP 패널 표시 설정
                if (insStripPanel)
                {
                    insStripPanel->setVisible(pattern->inspectionMethod == InspectionMethod::STRIP);
                }

                // CRIMP 패널 표시 설정
                if (insCrimpPanel)
                {
                    insCrimpPanel->setVisible(pattern->inspectionMethod == InspectionMethod::CRIMP);
                }

                // STRIP 검사 그룹들 표시 설정 (STRIP 검사 방법일 때만 보임)
                bool isStripMethod = (pattern->inspectionMethod == InspectionMethod::STRIP);
                if (insStripLengthGroup)
                    insStripLengthGroup->setVisible(isStripMethod);
                if (insStripFrontGroup)
                    insStripFrontGroup->setVisible(isStripMethod);
                if (insStripRearGroup)
                    insStripRearGroup->setVisible(isStripMethod);
                if (insEdgeGroup)
                    insEdgeGroup->setVisible(isStripMethod);

                // STRIP 파라미터 로드
                if (insStripKernelSpin)
                {
                    insStripKernelSpin->blockSignals(true);
                    insStripKernelSpin->setValue(pattern->stripMorphKernelSize);
                    insStripKernelSpin->blockSignals(false);
                }

                if (insStripGradThreshSpin)
                {
                    insStripGradThreshSpin->blockSignals(true);
                    insStripGradThreshSpin->setValue(pattern->stripGradientThreshold);
                    insStripGradThreshSpin->blockSignals(false);
                }

                if (insStripStartSlider)
                {
                    insStripStartSlider->blockSignals(true);
                    insStripStartSlider->setValue(pattern->stripGradientStartPercent);
                    insStripStartSlider->blockSignals(false);

                    // 값 표시 레이블 업데이트
                    if (insStripStartValueLabel)
                    {
                        insStripStartValueLabel->setText(QString("%1%").arg(pattern->stripGradientStartPercent));
                    }
                }

                if (insStripEndSlider)
                {
                    insStripEndSlider->blockSignals(true);
                    insStripEndSlider->setValue(pattern->stripGradientEndPercent);
                    insStripEndSlider->blockSignals(false);

                    // 값 표시 레이블 업데이트
                    if (insStripEndValueLabel)
                    {
                        insStripEndValueLabel->setText(QString("%1%").arg(pattern->stripGradientEndPercent));
                    }
                }

                if (insStripMinPointsSpin)
                {
                    insStripMinPointsSpin->blockSignals(true);
                    insStripMinPointsSpin->setValue(pattern->stripMinDataPoints);
                    insStripMinPointsSpin->blockSignals(false);
                }

                // STRIP 두께 측정 관련 컨트롤 업데이트
                if (insStripThicknessWidthSlider)
                {
                    // 패턴의 실제 너비 계산
                    float patternWidth = abs(pattern->rect.width());

                    insStripThicknessWidthSlider->blockSignals(true);
                    // 너비 슬라이더 최대값을 패턴 너비의 절반으로 설정
                    insStripThicknessWidthSlider->setMaximum(patternWidth / 2);
                    insStripThicknessWidthSlider->setValue(pattern->stripThicknessBoxWidth);
                    insStripThicknessWidthSlider->blockSignals(false);

                    if (insStripThicknessWidthValueLabel)
                    {
                        insStripThicknessWidthValueLabel->setText(QString("%1px").arg(pattern->stripThicknessBoxWidth));
                    }
                }

                if (insStripThicknessHeightSlider)
                {
                    // 패턴의 실제 높이 계산
                    float patternHeight = abs(pattern->rect.height());

                    insStripThicknessHeightSlider->blockSignals(true);
                    // 높이 슬라이더 최대값을 패턴 높이 전체로 설정
                    insStripThicknessHeightSlider->setMaximum(patternHeight);
                    insStripThicknessHeightSlider->setValue(pattern->stripThicknessBoxHeight);
                    insStripThicknessHeightSlider->blockSignals(false);

                    if (insStripThicknessHeightValueLabel)
                    {
                        insStripThicknessHeightValueLabel->setText(QString("%1px").arg(pattern->stripThicknessBoxHeight));
                    }
                }

                if (insStripThicknessMinEdit)
                {
                    insStripThicknessMinEdit->blockSignals(true);
                    insStripThicknessMinEdit->setText(QString::number(pattern->stripThicknessMin, 'f', 2));
                    insStripThicknessMinEdit->blockSignals(false);
                }

                if (insStripThicknessMaxEdit)
                {
                    insStripThicknessMaxEdit->blockSignals(true);
                    insStripThicknessMaxEdit->setText(QString::number(pattern->stripThicknessMax, 'f', 2));
                    insStripThicknessMaxEdit->blockSignals(false);
                }

                // REAR 두께 측정 위젯들 업데이트
                if (insStripRearThicknessWidthSlider)
                {
                    // 패턴의 실제 너비 계산
                    float patternWidth = abs(pattern->rect.width());

                    insStripRearThicknessWidthSlider->blockSignals(true);
                    // REAR 너비 슬라이더 최대값을 패턴 너비의 절반으로 설정
                    insStripRearThicknessWidthSlider->setMaximum(patternWidth / 2);
                    insStripRearThicknessWidthSlider->setValue(pattern->stripRearThicknessBoxWidth);
                    insStripRearThicknessWidthSlider->blockSignals(false);

                    if (insStripRearThicknessWidthValueLabel)
                    {
                        insStripRearThicknessWidthValueLabel->setText(QString("%1px").arg(pattern->stripRearThicknessBoxWidth));
                    }
                }

                if (insStripRearThicknessHeightSlider)
                {
                    // 패턴의 실제 높이 계산
                    float patternHeight = abs(pattern->rect.height());

                    insStripRearThicknessHeightSlider->blockSignals(true);
                    // REAR 높이 슬라이더 최대값을 패턴 높이 전체로 설정
                    insStripRearThicknessHeightSlider->setMaximum(patternHeight);
                    insStripRearThicknessHeightSlider->setValue(pattern->stripRearThicknessBoxHeight);
                    insStripRearThicknessHeightSlider->blockSignals(false);

                    if (insStripRearThicknessHeightValueLabel)
                    {
                        insStripRearThicknessHeightValueLabel->setText(QString("%1px").arg(pattern->stripRearThicknessBoxHeight));
                    }
                }

                if (insStripRearThicknessMinEdit)
                {
                    insStripRearThicknessMinEdit->blockSignals(true);
                    insStripRearThicknessMinEdit->setText(QString::number(pattern->stripRearThicknessMin, 'f', 2));
                    insStripRearThicknessMinEdit->blockSignals(false);
                }

                if (insStripRearThicknessMaxEdit)
                {
                    insStripRearThicknessMaxEdit->blockSignals(true);
                    insStripRearThicknessMaxEdit->setText(QString::number(pattern->stripRearThicknessMax, 'f', 2));
                    insStripRearThicknessMaxEdit->blockSignals(false);
                }

                // STRIP 길이검사 활성화 상태 업데이트
                if (insStripLengthEnabledCheck)
                {
                    insStripLengthEnabledCheck->blockSignals(true);
                    insStripLengthEnabledCheck->setChecked(pattern->stripLengthEnabled);
                    insStripLengthEnabledCheck->blockSignals(false);

                    // 길이검사 관련 위젯들 활성화/비활성화
                    if (insStripLengthMinEdit)
                        insStripLengthMinEdit->setEnabled(pattern->stripLengthEnabled);
                    if (insStripLengthMaxEdit)
                        insStripLengthMaxEdit->setEnabled(pattern->stripLengthEnabled);
                }

                // 길이검사 범위 값들 업데이트
                if (insStripLengthMinEdit)
                {
                    insStripLengthMinEdit->blockSignals(true);
                    insStripLengthMinEdit->setText(QString::number(pattern->stripLengthMin, 'f', 2));
                    insStripLengthMinEdit->blockSignals(false);
                }

                if (insStripLengthMaxEdit)
                {
                    insStripLengthMaxEdit->blockSignals(true);
                    insStripLengthMaxEdit->setText(QString::number(pattern->stripLengthMax, 'f', 2));
                    insStripLengthMaxEdit->blockSignals(false);
                }

                if (insStripLengthConversionEdit)
                {
                    insStripLengthConversionEdit->blockSignals(true);
                    insStripLengthConversionEdit->setText(QString::number(pattern->stripLengthConversionMm, 'f', 3));
                    insStripLengthConversionEdit->blockSignals(false);
                }

                // 캘리브레이션 정보 표시
                if (insStripLengthMeasuredLabel)
                {
                    if (pattern->stripLengthCalibrated && pattern->stripLengthCalibrationPx > 0)
                    {
                        double conversionRatio = pattern->stripLengthCalibrationPx / pattern->stripLengthConversionMm;
                        insStripLengthMeasuredLabel->setText(
                            QString("측정값: %1 px (%2 px/mm)")
                                .arg(pattern->stripLengthCalibrationPx, 0, 'f', 1)
                                .arg(conversionRatio, 0, 'f', 2));
                    }
                    else
                    {
                        insStripLengthMeasuredLabel->setText("측정값: - mm");
                    }
                }

                // 캘리브레이션 완료 여부에 따라 갱신 버튼 색상 변경
                if (insStripLengthRefreshButton)
                {
                    insStripLengthRefreshButton->setEnabled(true);

                    if (pattern->stripLengthCalibrated && pattern->stripLengthCalibrationPx > 0)
                    {
                        // 캘리브레이션 완료: 녹색
                        insStripLengthRefreshButton->setStyleSheet(
                            "QPushButton { background-color: #4CAF50; color: white; font-weight: bold; }"
                            "QPushButton:hover { background-color: #45a049; }");
                    }
                    else
                    {
                        // 캘리브레이션 미완료: 기본 색상
                        insStripLengthRefreshButton->setStyleSheet("");
                    }
                }

                // FRONT 두께 검사 활성화 상태 업데이트
                if (insStripFrontEnabledCheck)
                {
                    insStripFrontEnabledCheck->blockSignals(true);
                    insStripFrontEnabledCheck->setChecked(pattern->stripFrontEnabled);
                    insStripFrontEnabledCheck->blockSignals(false);

                    // FRONT 관련 위젯들 활성화/비활성화
                    if (insStripThicknessWidthSlider)
                        insStripThicknessWidthSlider->setEnabled(pattern->stripFrontEnabled);
                    if (insStripThicknessHeightSlider)
                        insStripThicknessHeightSlider->setEnabled(pattern->stripFrontEnabled);
                    if (insStripThicknessMinEdit)
                        insStripThicknessMinEdit->setEnabled(pattern->stripFrontEnabled);
                    if (insStripThicknessMaxEdit)
                        insStripThicknessMaxEdit->setEnabled(pattern->stripFrontEnabled);
                }

                // REAR 두께 검사 활성화 상태 업데이트
                if (insStripRearEnabledCheck)
                {
                    insStripRearEnabledCheck->blockSignals(true);
                    insStripRearEnabledCheck->setChecked(pattern->stripRearEnabled);
                    insStripRearEnabledCheck->blockSignals(false);

                    // REAR 관련 위젯들 활성화/비활성화
                    if (insStripRearThicknessWidthSlider)
                        insStripRearThicknessWidthSlider->setEnabled(pattern->stripRearEnabled);
                    if (insStripRearThicknessHeightSlider)
                        insStripRearThicknessHeightSlider->setEnabled(pattern->stripRearEnabled);
                    if (insStripRearThicknessMinEdit)
                        insStripRearThicknessMinEdit->setEnabled(pattern->stripRearEnabled);
                    if (insStripRearThicknessMaxEdit)
                        insStripRearThicknessMaxEdit->setEnabled(pattern->stripRearEnabled);
                }

                // EDGE 검사 UI 업데이트
                if (insEdgeEnabledCheck)
                {
                    insEdgeEnabledCheck->blockSignals(true);
                    insEdgeEnabledCheck->setChecked(pattern->edgeEnabled);
                    insEdgeEnabledCheck->blockSignals(false);
                }

                if (insEdgeOffsetXSlider)
                {
                    insEdgeOffsetXSlider->blockSignals(true);
                    insEdgeOffsetXSlider->setValue(pattern->edgeOffsetX);
                    insEdgeOffsetXValueLabel->setText(QString("%1px").arg(pattern->edgeOffsetX));
                    insEdgeOffsetXSlider->blockSignals(false);
                }

                if (insEdgeWidthSlider)
                {
                    insEdgeWidthSlider->blockSignals(true);
                    insEdgeWidthSlider->setValue(pattern->stripEdgeBoxWidth);
                    insEdgeWidthValueLabel->setText(QString("%1px").arg(pattern->stripEdgeBoxWidth));
                    insEdgeWidthSlider->blockSignals(false);
                }

                if (insEdgeHeightSlider)
                {
                    insEdgeHeightSlider->blockSignals(true);
                    insEdgeHeightSlider->setValue(pattern->stripEdgeBoxHeight);
                    insEdgeHeightValueLabel->setText(QString("%1px").arg(pattern->stripEdgeBoxHeight));
                    insEdgeHeightSlider->blockSignals(false);
                }

                // insEdgeThresholdSpin 업데이트 코드 제거됨 (통계적 방법 사용)

                if (insEdgeMaxIrregularitiesSpin)
                {
                    insEdgeMaxIrregularitiesSpin->blockSignals(true);
                    insEdgeMaxIrregularitiesSpin->setValue(pattern->edgeMaxOutliers);
                    insEdgeMaxIrregularitiesSpin->blockSignals(false);
                }

                if (insEdgeDistanceMaxEdit)
                {
                    insEdgeDistanceMaxEdit->blockSignals(true);
                    insEdgeDistanceMaxEdit->setText(QString::number(pattern->edgeDistanceMax, 'f', 2));
                    insEdgeDistanceMaxEdit->blockSignals(false);
                }

                if (insEdgeStartPercentSpin)
                {
                    insEdgeStartPercentSpin->blockSignals(true);
                    insEdgeStartPercentSpin->setValue(pattern->edgeStartPercent);
                    insEdgeStartPercentSpin->blockSignals(false);
                }

                if (insEdgeEndPercentSpin)
                {
                    insEdgeEndPercentSpin->blockSignals(true);
                    insEdgeEndPercentSpin->setValue(pattern->edgeEndPercent);
                    insEdgeEndPercentSpin->blockSignals(false);
                }

                // BARREL 기준 왼쪽 스트리핑 길이 검사 파라미터 로드
                if (insBarrelLeftStripGroup)
                {
                    insBarrelLeftStripGroup->blockSignals(true);
                    insBarrelLeftStripGroup->setChecked(pattern->barrelLeftStripEnabled);
                    insBarrelLeftStripGroup->blockSignals(false);
                }

                if (insBarrelLeftStripOffsetSlider && insBarrelLeftStripOffsetValueLabel)
                {
                    int maxOffsetX = pattern->rect.width();
                    insBarrelLeftStripOffsetSlider->blockSignals(true);
                    insBarrelLeftStripOffsetSlider->setRange(-250, qMax(250, maxOffsetX));
                    int offsetValue = qMin(pattern->barrelLeftStripOffsetX, maxOffsetX);
                    insBarrelLeftStripOffsetSlider->setValue(offsetValue);
                    insBarrelLeftStripOffsetValueLabel->setText(QString::number(offsetValue) + "px");
                    insBarrelLeftStripOffsetSlider->blockSignals(false);
                }

                if (insBarrelLeftStripWidthSlider && insBarrelLeftStripWidthValueLabel)
                {
                    int maxWidth = pattern->rect.width();
                    insBarrelLeftStripWidthSlider->blockSignals(true);
                    insBarrelLeftStripWidthSlider->setRange(1, qMax(500, maxWidth));
                    int widthValue = qMin(pattern->barrelLeftStripBoxWidth, maxWidth);
                    insBarrelLeftStripWidthSlider->setValue(widthValue);
                    insBarrelLeftStripWidthValueLabel->setText(QString::number(widthValue) + "px");
                    insBarrelLeftStripWidthSlider->blockSignals(false);
                }

                if (insBarrelLeftStripHeightSlider && insBarrelLeftStripHeightValueLabel)
                {
                    int maxHeight = pattern->rect.height();
                    insBarrelLeftStripHeightSlider->blockSignals(true);
                    insBarrelLeftStripHeightSlider->setRange(1, qMax(500, maxHeight));
                    int heightValue = qMin(pattern->barrelLeftStripBoxHeight, maxHeight);
                    insBarrelLeftStripHeightSlider->setValue(heightValue);
                    insBarrelLeftStripHeightValueLabel->setText(QString::number(heightValue) + "px");
                    insBarrelLeftStripHeightSlider->blockSignals(false);
                }

                if (insBarrelLeftStripMinEdit)
                {
                    insBarrelLeftStripMinEdit->blockSignals(true);
                    insBarrelLeftStripMinEdit->setText(QString::number(pattern->barrelLeftStripLengthMin, 'f', 2));
                    insBarrelLeftStripMinEdit->blockSignals(false);
                }

                if (insBarrelLeftStripMaxEdit)
                {
                    insBarrelLeftStripMaxEdit->blockSignals(true);
                    insBarrelLeftStripMaxEdit->setText(QString::number(pattern->barrelLeftStripLengthMax, 'f', 2));
                    insBarrelLeftStripMaxEdit->blockSignals(false);
                }

                // BARREL 기준 오른쪽 스트리핑 길이 검사 파라미터 로드
                if (insBarrelRightStripGroup)
                {
                    insBarrelRightStripGroup->blockSignals(true);
                    insBarrelRightStripGroup->setChecked(pattern->barrelRightStripEnabled);
                    insBarrelRightStripGroup->blockSignals(false);
                }

                if (insBarrelRightStripOffsetSlider && insBarrelRightStripOffsetValueLabel)
                {
                    int maxOffsetX = pattern->rect.width();
                    insBarrelRightStripOffsetSlider->blockSignals(true);
                    insBarrelRightStripOffsetSlider->setRange(-250, qMax(250, maxOffsetX));
                    int offsetValue = qMin(pattern->barrelRightStripOffsetX, maxOffsetX);
                    insBarrelRightStripOffsetSlider->setValue(offsetValue);
                    insBarrelRightStripOffsetValueLabel->setText(QString::number(offsetValue) + "px");
                    insBarrelRightStripOffsetSlider->blockSignals(false);
                }

                if (insBarrelRightStripWidthSlider && insBarrelRightStripWidthValueLabel)
                {
                    int maxWidth = pattern->rect.width();
                    insBarrelRightStripWidthSlider->blockSignals(true);
                    insBarrelRightStripWidthSlider->setRange(1, qMax(500, maxWidth));
                    int widthValue = qMin(pattern->barrelRightStripBoxWidth, maxWidth);
                    insBarrelRightStripWidthSlider->setValue(widthValue);
                    insBarrelRightStripWidthValueLabel->setText(QString::number(widthValue) + "px");
                    insBarrelRightStripWidthSlider->blockSignals(false);
                }

                if (insBarrelRightStripHeightSlider && insBarrelRightStripHeightValueLabel)
                {
                    int maxHeight = pattern->rect.height();
                    insBarrelRightStripHeightSlider->blockSignals(true);
                    insBarrelRightStripHeightSlider->setRange(1, qMax(500, maxHeight));
                    int heightValue = qMin(pattern->barrelRightStripBoxHeight, maxHeight);
                    insBarrelRightStripHeightSlider->setValue(heightValue);
                    insBarrelRightStripHeightValueLabel->setText(QString::number(heightValue) + "px");
                    insBarrelRightStripHeightSlider->blockSignals(false);
                }

                if (insBarrelRightStripMinEdit)
                {
                    insBarrelRightStripMinEdit->blockSignals(true);
                    insBarrelRightStripMinEdit->setText(QString::number(pattern->barrelRightStripLengthMin, 'f', 2));
                    insBarrelRightStripMinEdit->blockSignals(false);
                }

                if (insBarrelRightStripMaxEdit)
                {
                    insBarrelRightStripMaxEdit->blockSignals(true);
                    insBarrelRightStripMaxEdit->setText(QString::number(pattern->barrelRightStripLengthMax, 'f', 2));
                    insBarrelRightStripMaxEdit->blockSignals(false);
                }

                // INS 패턴의 템플릿 이미지 업데이트
                // 1. 매칭용 템플릿 이미지 (matchTemplate)
                if (insMatchTemplateImg)
                {
                    if (!pattern->matchTemplate.isNull())
                    {
                        QPixmap pixmap = QPixmap::fromImage(pattern->matchTemplate);
                        if (!pixmap.isNull())
                        {
                            insMatchTemplateImg->setPixmap(pixmap.scaled(
                                insMatchTemplateImg->width(), insMatchTemplateImg->height(), Qt::KeepAspectRatio));
                            insMatchTemplateImg->setText("");
                        }
                        else
                        {
                            insMatchTemplateImg->setPixmap(QPixmap());
                            insMatchTemplateImg->setText("변환\n실패");
                        }
                    }
                    else
                    {
                        insMatchTemplateImg->setPixmap(QPixmap());
                        insMatchTemplateImg->setText("매칭용");
                    }
                }

                // 2. 검사용 템플릿 이미지 (templateImage)
                if (insTemplateImg)
                {
                    if (!pattern->templateImage.isNull())
                    {
                        QPixmap pixmap = QPixmap::fromImage(pattern->templateImage);
                        if (!pixmap.isNull())
                        {
                            insTemplateImg->setPixmap(pixmap.scaled(
                                insTemplateImg->width(), insTemplateImg->height(), Qt::KeepAspectRatio));
                            insTemplateImg->setText("");
                        }
                        else
                        {
                            insTemplateImg->setPixmap(QPixmap());
                            insTemplateImg->setText(TR("IMAGE_CONVERSION_FAILED"));
                        }
                    }
                    else
                    {
                        insTemplateImg->setPixmap(QPixmap());
                        insTemplateImg->setText(TR("NO_IMAGE"));
                    }
                }
                break;
            }
            case PatternType::FIL:
            {
                // 필터 타입은 특별한 패널이 없음, 기본 패널 표시
                specialPropStack->setCurrentIndex(0);
                break;
            }
            default:
            {
                // 알 수 없는 패턴 타입
                specialPropStack->setCurrentIndex(0); // 기본 패널 표시
                break;
            }
            }
        }
    }
}

void TeachingWidget::detectCameras()
{
    // **프로그레스 다이얼로그 생성**
    QProgressDialog *progressDialog = new QProgressDialog("카메라 검색 중...", "취소", 0, 100, this);
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    progressDialog->setStyleSheet(
        "QProgressDialog { background-color: #1e1e1e; color: #ffffff; }"
        "QWidget { background-color: #1e1e1e; color: #ffffff; }"
        "QPushButton { background-color: #2d2d2d; color: #ffffff; border: 1px solid #3d3d3d; padding: 5px; min-width: 80px; }"
        "QPushButton:hover { background-color: #3d3d3d; }"
        "QProgressBar { border: 1px solid #3d3d3d; background-color: #252525; color: #ffffff; text-align: center; }"
        "QProgressBar::chunk { background-color: #0d47a1; }"
        "QLabel { color: #ffffff; }"
    );
    progressDialog->setMinimumDuration(0); // 즉시 표시
    progressDialog->setValue(0);
    progressDialog->show();
    QApplication::processEvents(); // UI 즉시 업데이트

    // 실제 연결된 카메라 수 카운트
    int connectedCameras = 0;

    // 카메라 정보 초기화
    progressDialog->setLabelText("기존 카메라 정보 정리 중...");
    progressDialog->setValue(5);
    QApplication::processEvents();

    int cameraCount = getCameraInfosCount();
    // OpenCV capture 제거됨 (Spinnaker SDK만 사용)
    clearCameraInfos(); // 전체 클리어

#ifdef USE_SPINNAKER
    printf("[detectCameras] USE_SPINNAKER 정의됨, m_useSpinnaker=%d, m_spinSystem=%p\n", m_useSpinnaker, m_spinSystem.get());
    fflush(stdout);
    // Spinnaker SDK 사용 가능한 경우
    if (m_useSpinnaker && m_spinSystem != nullptr)
    {
        // ★★★ System 유효성 재확인 (핫플러그 시 손상 가능)
        try
        {
            // GetCameras() 호출로 System 유효성 간접 검증
            Spinnaker::CameraList testList = m_spinSystem->GetCameras();
            testList.Clear();
        }
        catch (Spinnaker::Exception &e)
        {
            qDebug() << "[detectCameras] System 손상 감지:" << e.what() << "코드:" << e.GetError() << "- 재초기화 시도";
            m_spinSystem = nullptr;
            m_useSpinnaker = false;  // ★★★ Spinnaker 사용 비활성화
            qDebug() << "[detectCameras] Spinnaker 재초기화 실패 - 시뮬레이션 모드로 전환";
            progressDialog->deleteLater();
            return;
        }
        
        progressDialog->setLabelText("Spinnaker 카메라 검색 중...");
        progressDialog->setValue(10);
        QApplication::processEvents();

        try
        {
            // ★★★ 카메라 리소스 정리 (리소스 누수 방지)
            m_spinCameras.clear();
            if (m_spinCamList.GetSize() > 0)
            {
                m_spinCamList.Clear();
                QThread::msleep(100);
            }

            progressDialog->setValue(15);
            QApplication::processEvents();

            // 카메라 재검색
            m_spinCamList = m_spinSystem->GetCameras();
            unsigned int numCameras = m_spinCamList.GetSize();

            progressDialog->setLabelText(QString("Spinnaker 카메라 %1개 발견, 연결 중...").arg(numCameras));
            progressDialog->setValue(20);
            QApplication::processEvents();

            if (numCameras > 0)
            {

                // 각 카메라에 대해 처리
                for (unsigned int i = 0; i < numCameras; i++)
                {
                    if (progressDialog->wasCanceled())
                    {
                        progressDialog->deleteLater();
                        return;
                    }

                    progressDialog->setLabelText(QString("Spinnaker 카메라 %1/%2 연결 중...").arg(i + 1).arg(numCameras));
                    int progressValue = 20 + (i * 30 / numCameras); // 20-50%
                    progressDialog->setValue(progressValue);
                    QApplication::processEvents();

                    CameraInfo info;
                    info.index = i;

                    if (connectSpinnakerCamera(i, info))
                    {
                        // 성공적으로 연결된 카메라 추가
                        appendCameraInfo(info);
                        connectedCameras++;
                    }
                }

                // Spinnaker 카메라를 연결했으면 OpenCV 카메라 검색 건너뛰기
                if (connectedCameras > 0)
                {
                    // ★ 레시피 자동 로드 추가 (CAM ON 시)
                    QString lastRecipePath = ConfigManager::instance()->getLastRecipePath();
                    if (!lastRecipePath.isEmpty())
                    {
                        qDebug() << "[detectCameras] 카메라 연결 후 레시피 자동 로드:" << lastRecipePath;
                        QTimer::singleShot(100, this, [this, lastRecipePath]() {
                            onRecipeSelected(lastRecipePath);
                        });
                    }
                    
                    progressDialog->setValue(100);
                    progressDialog->deleteLater();
                    return;
                }
            }
            else
            {
                qDebug() << "[detectCameras] Spinnaker 카메라를 찾을 수 없습니다.";
            }
        }
        catch (Spinnaker::Exception &e)
        {
            qDebug() << "[detectCameras] Spinnaker 예외:" << e.what();
        }
        catch (...)
        {
            qDebug() << "[detectCameras] 알 수 없는 예외 발생";
        }
    }
    else
    {
        printf("[detectCameras] Spinnaker SDK 초기화 실패 - m_useSpinnaker=%d, m_spinSystem=%p\n", m_useSpinnaker, m_spinSystem.get());
        fflush(stdout);
        qDebug() << "[detectCameras] Spinnaker SDK가 초기화되지 않았습니다. 카메라 없이 시뮬레이션 모드로 동작합니다.";
    }

    // Spinnaker SDK가 활성화되어 있으면 OpenCV 카메라 검색 건너뛰기
    progressDialog->setValue(95);
    QApplication::processEvents();

    progressDialog->setValue(100);
    progressDialog->deleteLater();
    return;
#else
    printf("[detectCameras] USE_SPINNAKER가 정의되지 않음\n");
    fflush(stdout);
#endif

#ifdef __linux__
    // OpenCV 카메라 검색 제거 - Spinnaker SDK만 사용
    qDebug() << "[scanCamera] Linux OpenCV 카메라 검색 생략 (Spinnaker SDK만 사용)";
#else
    // OpenCV 카메라 검색 제거 - Spinnaker SDK만 사용
    qDebug() << "[scanCamera] OpenCV 카메라 검색 생략 (Spinnaker SDK만 사용)";
#endif

    // 미리보기 오버레이는 updatePreviewFrames에서 자동 업데이트됨
    progressDialog->setValue(95);
    QApplication::processEvents();

    // 완료
    progressDialog->setLabelText(QString("카메라 검색 완료 - %1개 카메라 발견").arg(connectedCameras));
    progressDialog->setValue(100);
    QApplication::processEvents();

    // 잠시 대기 후 다이얼로그 닫기
    QTimer::singleShot(500, progressDialog, &QProgressDialog::deleteLater);
}

void TeachingWidget::processGrabbedFrame(const cv::Mat &frame, int camIdx)
{
    // 프레임이 비어 있으면 무시
    if (frame.empty())
    {
        return;
    }

    if (camIdx >= MAX_CAMERAS / 2)  // MAX_CAMERAS=4이면 카메라는 최대 2대
        return;

    // TEACH OFF 상태에서만 cameraFrames 갱신 (TEACH ON 시 영상 정지)
    // 라이브 모드에서는 카메라 인덱스를 프레임 인덱스로 매핑
    if (!teachingEnabled)
    {
        // 라이브 모드: 카메라별로 STRIP 프레임에 저장 (0→0, 1→2)
        // 카메라 0 → 프레임 0 (STRIP)
        // 카메라 1 → 프레임 2 (STRIP)
        int frameIndex = camIdx * 2;  // 0→0, 1→2
        
        qDebug() << QString("[processGrabbedFrame 라이브] Cam%1 → Frame[%2] 저장")
                    .arg(camIdx).arg(frameIndex);
        
        if (frameIndex >= 0 && frameIndex < MAX_CAMERAS)
        {
            // 프레임 쓰기 (mutex로 보호)
            {
                QMutexLocker locker(&cameraFramesMutex);
                cameraFrames[frameIndex] = frame.clone();
                frameUpdatedFlags[frameIndex] = true;
            }
            
            // 해당 미리보기 즉시 업데이트 (메인 스레드에서 실행)
            QMetaObject::invokeMethod(this, [this, frameIndex]() {
                updateSinglePreview(frameIndex);
            }, Qt::QueuedConnection);
        }
    }
}

// 프레임 인덱스를 강제 지정하는 오버로드
void TeachingWidget::processGrabbedFrame(const cv::Mat &frame, int camIdx, int forceFrameIndex)
{
    // 프레임이 비어 있으면 무시
    if (frame.empty())
    {
        return;
    }

    if (camIdx >= MAX_CAMERAS / 2)  // MAX_CAMERAS=4이면 카메라는 최대 2대
        return;

    // std::array는 고정 크기이므로 resize 불필요

    // 트리거 모드에서는 TEACH 상태 관계없이 프레임 저장 (forceFrameIndex가 지정된 경우)
    // TEACH OFF 상태에서만 일반 프레임 갱신 (TEACH ON 시 영상 정지)
    if (!teachingEnabled || forceFrameIndex >= 0)
    {
        int frameIndex = forceFrameIndex;
        
        if (frameIndex >= 0 && frameIndex < MAX_CAMERAS)
        {
            qDebug() << QString("[processGrabbedFrame] Frame[%1] 저장 시작 (입력 frame: %2x%3)")
                        .arg(frameIndex).arg(frame.cols).arg(frame.rows);
            
            // 프레임 쓰기 (mutex로 보호)
            try {
                qDebug() << QString("[processGrabbedFrame] Frame[%1] clone 시작").arg(frameIndex);
                
                // 새 프레임 저장 (cv::Mat의 assignment operator가 자동으로 메모리 관리)
                {
                    QMutexLocker locker(&cameraFramesMutex);
                    cameraFrames[frameIndex] = frame.clone();
                    frameUpdatedFlags[frameIndex] = true;
                }
                
                qDebug() << QString("[processGrabbedFrame] Frame[%1] clone 완료").arg(frameIndex);
            } catch (const cv::Exception& e) {
                qDebug() << QString("[processGrabbedFrame] Frame[%1] OpenCV 예외: %2").arg(frameIndex).arg(e.what());
                return;
            } catch (const std::exception& e) {
                qDebug() << QString("[processGrabbedFrame] Frame[%1] 표준 예외: %2").arg(frameIndex).arg(e.what());
                return;
            } catch (...) {
                qDebug() << QString("[processGrabbedFrame] Frame[%1] 알 수 없는 예외").arg(frameIndex);
                return;
            }
            
            qDebug() << QString("[processGrabbedFrame] Frame[%1] 저장 완료 (size: %2x%3, teachingEnabled:%4)")
                        .arg(frameIndex).arg(frame.cols).arg(frame.rows).arg(teachingEnabled);
            
            qDebug() << QString("[processGrabbedFrame] Frame[%1] 함수 종료").arg(frameIndex);
        }
    }

    // 메인 카메라 처리
    if (camIdx == cameraIndex)
    {
        try
        {
            // ★ TEACH ON/OFF 관계없이 필터 적용 (필터 프리뷰를 위해)
            if (cameraView)
            {
                // 필터 적용
                cv::Mat filteredFrame = frame.clone();
                cameraView->applyFiltersToImage(filteredFrame);

                // RGB 변환
                cv::Mat displayFrame;
                cv::cvtColor(filteredFrame, displayFrame, cv::COLOR_BGR2RGB);
                
                // 메모리 안정성 보장
                if (!displayFrame.isContinuous()) {
                    displayFrame = displayFrame.clone();
                }

                // QImage로 변환 - deep copy로 메모리 안전성 보장
                QImage image(displayFrame.data, displayFrame.cols, displayFrame.rows,
                             static_cast<int>(displayFrame.step), QImage::Format_RGB888);
                QImage safeCopy = image.copy();
                QPixmap pixmap = QPixmap::fromImage(safeCopy);

                // UI 업데이트 - 메인 스레드에서 안전하게 실행
                QPointer<CameraView> safeView = cameraView;
                QMetaObject::invokeMethod(this, [safeView, pixmap]()
                                          {
                    if (safeView) {
                        safeView->setBackgroundPixmap(pixmap);
                        safeView->update();
                    } }, Qt::QueuedConnection);
            }
        }
        catch (const std::exception &e)
        {
        }
        return;
    }

    // **미리보기 카메라 처리** - 개별 프레임 업데이트로 변경 (4개 전체 갱신 제거)
    // updatePreviewFrames();  // ← 제거! (동시 접근 문제 발생)
}

void TeachingWidget::updateStatusPanel()
{
    if (!serverStatusLabel || !diskSpaceLabel)
        return;

    // 서버 연결 상태 업데이트 (ClientDialog에서 상태 읽기)
    ConfigManager *config = ConfigManager::instance();
    QString serverIp = config->getServerIp();
    int serverPort = config->getServerPort();

    if (ClientDialog::instance()->isServerConnected())
    {
        // 연결됨 - 녹색
        serverStatusLabel->setText(QString("🌐 서버: 연결됨 (%1:%2)").arg(serverIp).arg(serverPort));
        serverStatusLabel->setStyleSheet(
            "QLabel {"
            "  background-color: rgba(0, 100, 0, 180);"
            "  color: white;"
            "  border: 1px solid #555;"
            "  border-radius: 3px;"
            "  padding-left: 8px;"
            "  font-size: 12px;"
            "}");
    }
    else
    {
        // 미연결 - 회색
        serverStatusLabel->setText(QString("🌐 서버: 미연결 (%1:%2)").arg(serverIp).arg(serverPort));
        serverStatusLabel->setStyleSheet(
            "QLabel {"
            "  background-color: rgba(0, 0, 0, 180);"
            "  color: white;"
            "  border: 1px solid #555;"
            "  border-radius: 3px;"
            "  padding-left: 8px;"
            "  font-size: 12px;"
            "}");
    }

    // 디스크 용량 업데이트
    QStorageInfo storage = QStorageInfo::root();
    qint64 availableGB = storage.bytesAvailable() / (1024 * 1024 * 1024);
    qint64 totalGB = storage.bytesTotal() / (1024 * 1024 * 1024);
    int percent = totalGB > 0 ? (int)((storage.bytesAvailable() * 100) / storage.bytesTotal()) : 0;

    diskSpaceLabel->setText(QString("💾 디스크: %1GB / %2GB (%3% Available)")
                                .arg(availableGB)
                                .arg(totalGB)
                                .arg(percent));

    // 용량에 따라 색상 변경
    QString diskColor = "#4caf50"; // 녹색 (충분)
    if (percent < 10)
    {
        diskColor = "#f44336"; // 빨간색 (부족)
    }
    else if (percent < 20)
    {
        diskColor = "#ff9800"; // 주황색 (경고)
    }

    diskSpaceLabel->setStyleSheet(
        QString("QLabel {"
                "  background-color: rgba(0, 0, 0, 180);"
                "  color: %1;"
                "  border: 1px solid #555;"
                "  border-radius: 3px;"
                "  padding-left: 8px;"
                "  font-size: 12px;"
                "}")
            .arg(diskColor));
}

void TeachingWidget::updateStatusPanelPosition()
{
    if (!previewOverlayLabels[0] || !serverStatusLabel || !diskSpaceLabel)
        return;
    if (!cameraView)
        return;

    int rightMargin = 10;
    int topMargin = 70; // 버튼 오버레이 아래
    int spacing = 5;

    // 4개 미리보기 오버레이를 오른쪽에 세로로 배치
    int previewX = cameraView->width() - previewOverlayLabels[0]->width() - rightMargin;
    int previewY = topMargin;
    
    for (int i = 0; i < 4; i++)
    {
        if (previewOverlayLabels[i])
        {
            previewOverlayLabels[i]->move(previewX, previewY);
            previewY += previewOverlayLabels[i]->height() + spacing;
        }
    }

    // 상태 패널들을 미리보기 아래에 배치
    int statusX = previewX;
    int statusY = previewY;

    serverStatusLabel->move(statusX, statusY);
    statusY += serverStatusLabel->height() + spacing;
    
    if (serialStatusLabel) {
        serialStatusLabel->move(statusX, statusY);
        statusY += serialStatusLabel->height() + spacing;
    }
    
    diskSpaceLabel->move(statusX, statusY);
    statusY += diskSpaceLabel->height() + spacing;
    
    pixelInfoLabel->move(statusX, statusY);
}

void TeachingWidget::updateLogOverlayPosition()
{
    if (!logOverlayWidget || !cameraView)
        return;

    int bottomMargin = 10; // 하단 마진
    int rightMargin = 10;  // 오른쪽 마진

    // 화면 하단 오른쪽 끝에 배치 (미리보기 CAM2와 동일한 마진)
    int x = cameraView->width() - logOverlayWidget->width() - rightMargin;
    int y = cameraView->height() - logOverlayWidget->height() - bottomMargin;

    logOverlayWidget->move(x, y);

    // 오른쪽 패널 오버레이 위치 업데이트 (cameraView 기준, 버튼 아래)
    if (rightPanelOverlay && cameraView)
    {
        int leftMargin = 10;
        int topMargin = 70; // 버튼 오버레이(60px) + 여백(10px)

        // cameraView의 글로벌 좌표를 this 기준으로 변환
        QPoint cameraViewPos = cameraView->mapTo(this, QPoint(0, 0));

        // 위치만 업데이트 (사용자가 크기 조절했을 수 있으므로 크기는 건드리지 않음)
        rightPanelOverlay->move(
            cameraViewPos.x() + leftMargin, // cameraView 왼쪽 상단 기준
            cameraViewPos.y() + topMargin);
    }
}

void TeachingWidget::receiveLogMessage(const QString &message)
{
    // 메인 스레드가 아니면 QueuedConnection으로 재호출
    if (QThread::currentThread() != this->thread())
    {
        QMetaObject::invokeMethod(this, "receiveLogMessage",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, message));
        return;
    }

    if (!logTextEdit || !logOverlayWidget)
        return;

    // 현재 커서를 끝으로 이동
    QTextCursor cursor = logTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    logTextEdit->setTextCursor(cursor);

    // HTML 태그 파싱 및 처리
    QString processedMessage = message;
    QList<QPair<QString, QColor>> segments; // (텍스트, 색상) 쌍
    
    // HTML 태그 파싱
    if (message.contains("<font color=")) {
        QRegularExpression fontRegex("<font color='([^']+)'>([^<]*)</font>");
        QRegularExpressionMatchIterator it = fontRegex.globalMatch(message);
        
        int lastPos = 0;
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            
            // 태그 이전 텍스트 (기본 색상)
            if (match.capturedStart() > lastPos) {
                QString beforeText = message.mid(lastPos, match.capturedStart() - lastPos);
                segments.append(qMakePair(beforeText, QColor("#FFFFFF")));
            }
            
            // 태그 내부 텍스트 (지정 색상)
            QString colorStr = match.captured(1);
            QString text = match.captured(2);
            segments.append(qMakePair(text, QColor(colorStr)));
            
            lastPos = match.capturedEnd();
        }
        
        // 마지막 태그 이후 텍스트
        if (lastPos < message.length()) {
            QString afterText = message.mid(lastPos);
            segments.append(qMakePair(afterText, QColor("#FFFFFF")));
        }
    } else {
        // HTML 태그가 없으면 기존 로직 사용
        segments.append(qMakePair(message, QColor("#FFFFFF")));
    }

    // 텍스트 색상 결정 (HTML 태그가 없는 경우에만 적용)
    QTextCharFormat format;

    // [STRIP] 또는 [CRIMP] 검사 결과 로그 - PASS/NG/FAIL 색상
    if (message.contains("[STRIP]") || message.contains("[CRIMP]"))
    {
        if (message.contains("PASS"))
        {
            format.setForeground(QColor("#4CAF50")); // 초록색
            format.setFontWeight(QFont::Bold);
        }
        else if (message.contains("NG"))
        {
            format.setForeground(QColor("#f44336")); // 빨간색 (불량)
            format.setFontWeight(QFont::Bold);
        }
        else if (message.contains("FAIL"))
        {
            format.setForeground(QColor(255, 165, 0)); // 주황색 (검사 실패)
            format.setFontWeight(QFont::Bold);
        }
        else
        {
            format.setForeground(QColor("#2196F3")); // 파란색 (Trigger ON 등)
            format.setFontWeight(QFont::Bold);
        }
    }
    // "검사 시작", "검사 종료"
    else if (message.contains("검사 시작") || message.contains("검사 종료"))
    {
        format.setForeground(QColor("#2196F3")); // 파란색
        format.setFontWeight(QFont::Bold);
    }
    // "전체 검사 결과"
    else if (message.contains("전체 검사 결과:"))
    {
        if (message.contains("PASS"))
        {
            format.setForeground(QColor("#4CAF50")); // 초록색
            format.setFontWeight(QFont::Bold);
        }
        else if (message.contains("NG") || message.contains("FAIL"))
        {
            format.setForeground(QColor(255, 165, 0)); // 주황색
            format.setFontWeight(QFont::Bold);
        }
    }
    // INS 패턴 검사 결과 - PASS는 초록, NG는 빨간색, FAIL은 주황색
    else if ((message.contains("EDGE:") || message.contains("FRONT:") || message.contains("REAR:") || message.contains("STRIP LENGTH:")))
    {
        if (message.contains("PASS"))
        {
            format.setForeground(QColor("#4CAF50")); // 초록색
        }
        else if (message.contains("NG"))
        {
            format.setForeground(QColor("#f44336")); // 빨간색 (불량)
            format.setFontWeight(QFont::Bold);
        }
        else if (message.contains("FAIL"))
        {
            format.setForeground(QColor(255, 165, 0)); // 주황색 (검사 실패)
            format.setFontWeight(QFont::Bold);
        }
        else
        {
            format.setForeground(QColor("#8BCB8B")); // INS 색상 (연한 초록색)
        }
    }
    // FID/INS 패턴 - PASS는 기본색, NG는 빨간색, FAIL은 주황색
    else if (message.contains(": PASS [") || message.contains(": NG [") || message.contains(": FAIL ["))
    {
        // "F_u4E4Y: PASS [1.00/0.80]" 또는 "I_lc46A: NG" 형식
        if (message.contains(": NG"))
        {
            format.setForeground(QColor("#f44336")); // 빨간색 (불량)
            format.setFontWeight(QFont::Bold);
        }
        else if (message.contains(": FAIL ["))
        {
            format.setForeground(QColor(255, 165, 0)); // 주황색 (검사 실패)
            format.setFontWeight(QFont::Bold);
        }
        else
        {
            format.setForeground(QColor("#7094DB")); // FID 색상 (연한 파란색)
        }
    }
    else
    {
        format.setForeground(QColor("#FFFFFF")); // 기본 흰색
    }

    // HTML 태그가 있으면 segments로 출력, 없으면 기존 방식
    if (message.contains("<font color=")) {
        // HTML 태그가 있는 경우: segments 순회하며 색상 적용
        for (const auto& segment : segments) {
            QTextCharFormat segFormat;
            segFormat.setForeground(segment.second);
            cursor.insertText(segment.first, segFormat);
        }
    } else {
        // HTML 태그가 없는 경우: 기존 로직 (타임스탬프 분리)
        if (message.contains(" - ")) {
            int separatorIndex = message.indexOf(" - ");
            
            // 타임스탬프 부분 (회색)
            QTextCharFormat timestampFormat;
            timestampFormat.setForeground(QColor("#9E9E9E"));
            cursor.insertText(message.left(separatorIndex + 3), timestampFormat);

            // 메시지 부분 (위에서 결정된 색상)
            QString msg = message.mid(separatorIndex + 3);
            cursor.insertText(msg, format);
        } else {
            cursor.insertText(message, format);
        }
    }

    cursor.insertText("\n");
    logTextEdit->ensureCursorVisible();

    // TEACH ON 상태일 때만 오버레이 자동 표시
    if (teachingEnabled && !logOverlayWidget->isVisible())
    {
        logOverlayWidget->show();
        logOverlayWidget->raise();
    }
}

void TeachingWidget::updatePreviewFrames()
{
    if (!previewOverlayLabel)
        return;

    // 4개 미리보기 모두 업데이트
    const QStringList labels = {"STAGE 1 - STRIP", "STAGE 1 - CRIMP", "STAGE 2 - STRIP", "STAGE 2 - CRIMP"};
    
    for (int i = 0; i < 4; i++)
    {
        if (!previewOverlayLabels[i])
            continue;
        
        try
        {
            QPixmap pixmap;
            
            // 검사 결과가 있으면 검사 결과 pixmap 사용 (오버레이 포함)
            if (cameraView && cameraView->hasModeResult(i))
            {
                const QPixmap& resultPixmap = cameraView->getFramePixmap(i);
                if (!resultPixmap.isNull()) {
                    pixmap = resultPixmap;
                } else {
                    // pixmap이 null이면 원본 프레임 사용
                    if (i < static_cast<int>(4))
                    {
                        cv::Mat previewFrame;
                        {
                            QMutexLocker locker(&cameraFramesMutex);
                            if (!cameraFrames[i].empty()) {
                                previewFrame = cameraFrames[i].clone();
                            }
                        }
                        if (!previewFrame.empty()) {
                            cv::cvtColor(previewFrame, previewFrame, cv::COLOR_BGR2RGB);
                            QImage image(previewFrame.data, previewFrame.cols, previewFrame.rows,
                                         previewFrame.step, QImage::Format_RGB888);
                            pixmap = QPixmap::fromImage(image.copy());
                        }
                    }
                }
            }
            // 검사 결과 없으면 원본 프레임 사용
            else if (i < static_cast<int>(4))
            {
                cv::Mat previewFrame;
                {
                    QMutexLocker locker(&cameraFramesMutex);
                    if (cameraFrames[i].empty())
                        continue;
                    previewFrame = cameraFrames[i].clone();
                }
                
                // ★ cameraFrame에 직접 텍스트 오버레이 그리기
                QString text = labels[i];
                cv::Scalar textColor(255, 255, 255); // BGR - 흰색 (기본)
                
                // 검사 결과가 있으면 추가 표시
                if (cameraView && cameraView->hasModeResult(i))
                {
                    const InspectionResult& result = cameraView->getFrameResult(i);
                    if (result.isPassed) {
                        text += " (PASS)";
                        textColor = cv::Scalar(0, 255, 0); // BGR - 밝은 초록
                    } else {
                        text += " (NG)";
                        textColor = cv::Scalar(0, 0, 255); // BGR - 빨강
                    }
                }
                
                // 카메라별 획득 수 표시
                int frameCount = serialFrameCount[i].load();
                text += QString(" [%1]").arg(frameCount);
                
                // 반투명 검은 배경 사각형
                int baseline = 0;
                cv::Size textSize = cv::getTextSize(text.toStdString(), cv::FONT_HERSHEY_SIMPLEX, 0.8, 2, &baseline);
                cv::Rect bgRect(5, 5, textSize.width + 10, textSize.height + baseline + 10);
                
                // 배경 그리기 (검은색, 투명도 70%)
                cv::Mat overlay = previewFrame.clone();
                cv::rectangle(overlay, bgRect, cv::Scalar(0, 0, 0), -1);
                cv::addWeighted(overlay, 0.7, previewFrame, 0.3, 0, previewFrame);
                
                // 텍스트 그리기
                cv::putText(previewFrame, text.toStdString(), 
                           cv::Point(10, 10 + textSize.height), 
                           cv::FONT_HERSHEY_SIMPLEX, 0.8, textColor, 2);
                
                cv::cvtColor(previewFrame, previewFrame, cv::COLOR_BGR2RGB);
                
                // 메모리 안정성을 위해 연속 메모리 보장
                if (!previewFrame.isContinuous()) {
                    previewFrame = previewFrame.clone();
                }
                
                QImage image(previewFrame.data, previewFrame.cols, previewFrame.rows,
                             static_cast<int>(previewFrame.step), QImage::Format_RGB888);
                QImage safeCopy = image.copy();
                pixmap = QPixmap::fromImage(safeCopy);
            }
            else
            {
                // 프레임 없으면 검은 배경
                pixmap = QPixmap(previewOverlayLabels[i]->size());
                pixmap.fill(Qt::black);
            }

            // 레이블 크기에 맞춰 스케일링 (오버레이는 이미 cameraFrame에 그려짐)
            QSize labelSize = previewOverlayLabels[i]->size();
            if (labelSize.width() > 0 && labelSize.height() > 0)
            {
                QPixmap scaledPixmap = pixmap.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                previewOverlayLabels[i]->setPixmap(scaledPixmap);
            previewOverlayLabels[i]->setScaledContents(false);
            // rgba 대신 rgb 사용 (파싱 오류 방지)
            previewOverlayLabels[i]->setStyleSheet(
                "QLabel {"
                "  background-color: rgb(0, 0, 0);"
                "  border: 2px solid #555;"
                "  border-radius: 5px;"
                "}");
            previewOverlayLabels[i]->setWindowOpacity(0.8);
            }
        }
        catch (const std::exception &e)
        {
            previewOverlayLabels[i]->clear();
            previewOverlayLabels[i]->setText(labels[i] + "\n" + TR("PROCESSING_ERROR"));
        }
    }
    
    // 4분할 뷰 모드일 때 cameraView에 프레임 전달
    if (cameraView && cameraView->getQuadViewMode())
    {
        QMutexLocker locker(&cameraFramesMutex);
        cameraView->setQuadFrames(cameraFrames);
    }
}

// cameraFrames에 접근하지 않고 프레임 데이터를 직접 받아서 처리 (스레드 안전)
void TeachingWidget::updateSinglePreviewWithFrame(int frameIndex, const cv::Mat& previewFrame)
{
    if (frameIndex < 0 || frameIndex >= 4) {
        return;
    }
    
    // QPointer로 레이블 안전성 체크
    QPointer<QLabel> safeLabel = previewOverlayLabels[frameIndex];
    if (!safeLabel || previewFrame.empty()) {
        return;
    }
    
    // ★ 프레임 갱신 카운트 증가
    frameUpdateCount[frameIndex]++;
    
    const QStringList labels = {"STAGE 1 - STRIP", "STAGE 1 - CRIMP", "STAGE 2 - STRIP", "STAGE 2 - CRIMP"};
    
    try
    {
        // ★ BGR 프레임에 직접 텍스트 오버레이 그리기 (RGB 변환 전)
        QString text = labels[frameIndex];
        
        // 패턴(레시피)이 있는지 확인
        bool hasRecipe = cameraView && !cameraView->getPatterns().isEmpty();
        
        // 작업용 프레임 생성 (const 문제 해결)
        cv::Mat workFrame = previewFrame.clone();
        
        // 반투명 검은 배경 사각형
        int baseline = 0;
        cv::Size textSize = cv::getTextSize(text.toStdString(), cv::FONT_HERSHEY_SIMPLEX, 0.8, 2, &baseline);
        
        // 왼쪽 상단 배경 (STAGE 텍스트)
        cv::Rect bgRect(5, 5, textSize.width + 10, textSize.height + baseline + 10);
        
        // ★ 카운트 텍스트 추가 (오른쪽 상단)
        int currentCount = frameUpdateCount[frameIndex].load();
        QString countText = QString::number(currentCount);
        cv::Size countTextSize = cv::getTextSize(countText.toStdString(), cv::FONT_HERSHEY_SIMPLEX, 0.8, 2, &baseline);
        
        // 배경 그리기 (검은색, 투명도 70%)
        cv::Mat overlay = workFrame.clone();
        cv::rectangle(overlay, bgRect, cv::Scalar(0, 0, 0), -1);
        
        // 오른쪽 상단 배경 (카운트)
        cv::Rect countBgRect(workFrame.cols - countTextSize.width - 15, 5, 
                            countTextSize.width + 10, countTextSize.height + baseline + 10);
        cv::rectangle(overlay, countBgRect, cv::Scalar(0, 0, 0), -1);
        
        cv::addWeighted(overlay, 0.7, workFrame, 0.3, 0, workFrame);
        
        // 기본 텍스트 그리기 (항상 흰색)
        cv::putText(workFrame, text.toStdString(), 
                   cv::Point(10, 10 + textSize.height), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 255, 255), 2);
        
        // 검사 결과가 있고 레시피가 있으면 결과 텍스트 추가 (색상 있음)
        if (hasRecipe && cameraView && cameraView->hasModeResult(frameIndex))
        {
            const InspectionResult& result = cameraView->getFrameResult(frameIndex);
            QString resultText;
            cv::Scalar resultColor;
            
            if (result.isPassed) {
                resultText = " (PASS)";
                resultColor = cv::Scalar(0, 255, 0); // BGR - 밝은 초록
            } else {
                resultText = " (NG)";
                resultColor = cv::Scalar(0, 0, 255); // BGR - 빨강
            }
            
            // 결과 텍스트 위치 계산 (기본 텍스트 오른쪽에 추가)
            int resultX = 10 + textSize.width;
            cv::putText(workFrame, resultText.toStdString(), 
                       cv::Point(resultX, 10 + textSize.height), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.8, resultColor, 2);
        }
        
        // 카운트 텍스트 그리기 (오른쪽 상단)
        cv::putText(workFrame, countText.toStdString(), 
                   cv::Point(workFrame.cols - countTextSize.width - 10, 10 + countTextSize.height), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 255, 255), 2);
        
        // 프레임 변환 - 메모리 안전성을 위해 연속 메모리 보장
        cv::Mat rgbFrame;
        cv::cvtColor(workFrame, rgbFrame, cv::COLOR_BGR2RGB);
        
        // 메모리 정렬 보장을 위해 연속적인 메모리로 복사
        if (!rgbFrame.isContinuous()) {
            rgbFrame = rgbFrame.clone();
        }

        // QImage로 변환 - deep copy로 메모리 안전성 보장
        QImage image(rgbFrame.data, rgbFrame.cols, rgbFrame.rows,
                     static_cast<int>(rgbFrame.step), QImage::Format_RGB888);
        // 즉시 deep copy 수행하여 rgbFrame의 수명 문제 회피
        QImage safeCopy = image.copy();
        QPixmap pixmap = QPixmap::fromImage(safeCopy);
        
        // 레이블 크기에 맞춰 스케일링
        QSize labelSize = safeLabel->size();
        
        if (labelSize.width() > 0 && labelSize.height() > 0)
        {
            QPixmap scaledPixmap = pixmap.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            
            // 재확인 - 레이블이 여전히 유효한지
            safeLabel = previewOverlayLabels[frameIndex];
            if (!safeLabel) {
                return;
            }
            
            safeLabel->setPixmap(scaledPixmap);
            safeLabel->setScaledContents(false);
        }
    }
    catch (const std::exception &e)
    {
        qDebug() << QString("[updateSinglePreviewWithFrame] Frame[%1] 예외 발생: %2").arg(frameIndex).arg(e.what());
        QPointer<QLabel> safeLabel = previewOverlayLabels[frameIndex];
        if (safeLabel) {
            safeLabel->clear();
            safeLabel->setText(labels[frameIndex] + "\n" + TR("PROCESSING_ERROR"));
        }
    }
}

void TeachingWidget::updateSinglePreview(int frameIndex)
{
    qDebug() << QString("[updateSinglePreview] Frame[%1] 시작").arg(frameIndex);
    
    if (frameIndex < 0 || frameIndex >= 4) {
        qDebug() << QString("[updateSinglePreview] Frame[%1] 인덱스 범위 오류").arg(frameIndex);
        return;
    }
    
    if (frameIndex >= static_cast<int>(4)) {
        qDebug() << QString("[updateSinglePreview] Frame[%1] cameraFrames 크기 초과").arg(frameIndex);
        return;
    }
    
    qDebug() << QString("[updateSinglePreview] Frame[%1] 프레임 복사 시작").arg(frameIndex);
    
    // 프레임 복사
    cv::Mat previewFrame;
    if (cameraFrames[frameIndex].empty()) {
        qDebug() << QString("[updateSinglePreview] Frame[%1] cameraFrame is empty").arg(frameIndex);
        return;
    }
    previewFrame = cameraFrames[frameIndex].clone();
    
    qDebug() << QString("[updateSinglePreview] Frame[%1] 프레임 복사 완료 - updateSinglePreviewWithFrame 호출")
                .arg(frameIndex);
    
    // 실제 UI 업데이트는 별도 함수로
    updateSinglePreviewWithFrame(frameIndex, previewFrame);
}

void TeachingWidget::onFrameIndexReceived(int frameIndex)
{
    // TEACH ON/OFF 상태 관계없이 시리얼 트리거는 처리
    
    if (frameIndex < 0 || frameIndex > 3) {
        qWarning() << QString("[시리얼 메시지] 유효하지 않은 프레임 인덱스: %1").arg(frameIndex);
        return;
    }
    
    // ★ 프레임 인덱스 → 카메라 매핑 (0,1=카메라0 / 2,3=카메라1)
    int targetCameraIndex = (frameIndex <= 1) ? 0 : 1;
    
    totalTriggersReceived++;  // 총 시리얼 메시지 카운트
    
    // ★ "다음 트리거는 이 프레임으로 검사해라" 설정 (덮어쓰기)
    int prevIndex = nextFrameIndex[targetCameraIndex].exchange(frameIndex);
    
    if (prevIndex >= 0 && prevIndex != frameIndex) {
        qWarning().noquote() << QString("[시리얼 메시지] 카메라%1 프레임[%2]→[%3] 변경 (이전 값 미사용)")
                      .arg(targetCameraIndex).arg(prevIndex).arg(frameIndex);
    }
    
}


void TeachingWidget::onTriggerSignalReceived(const cv::Mat &frame, int triggerCameraIndex)
{
    auto triggerStartTime = std::chrono::high_resolution_clock::now();
    
    if (frame.empty() || triggerCameraIndex < 0)
    {
        qDebug() << "[onTriggerSignalReceived] 프레임 empty 또는 유효하지 않은 카메라 인덱스";
        return;
    }
    
    // **TrainDialog가 열려있으면 모든 처리 무시**
    if (activeTrainDialog && activeTrainDialog->isVisible()) {
        return;  // 캡처, 검사 모두 무시
    }

    // ★ 서버가 지정한 프레임 인덱스 읽기 (덮어쓰기 방식)
    int frameIdx = nextFrameIndex[triggerCameraIndex].load();
    
    if (frameIdx < 0 || frameIdx >= 4) {
        // 서버가 프레임 인덱스를 지정하지 않은 경우 - 검사 스킵
        qWarning() << QString("[Trigger] 서버 프레임 인덱스 없음 - 검사 스킵 (카메라%1) | CAM0:%2 CAM1:%3 | 통계 서버:%4 HW:%5 실행:%6")
                      .arg(triggerCameraIndex)
                      .arg(nextFrameIndex[0].load())
                      .arg(nextFrameIndex[1].load())
                      .arg(totalTriggersReceived.load())
                      .arg(totalHardwareTriggersReceived.load())
                      .arg(totalInspectionsExecuted.load());
        return;
    }
    
    totalInspectionsExecuted++;  // 검사 실행 카운트
    
    // ★ 트리거 사용 후 즉시 초기화 (같은 메시지로 중복 검사 방지)
    nextFrameIndex[triggerCameraIndex] = -1;
    
    // ★★★ 같은 프레임이 이미 처리 중이면 무시 (동시 접근 차단)
    bool expected = false;
    if (!frameProcessing[frameIdx].compare_exchange_strong(expected, true)) {
        qWarning() << QString("[Trigger] Frame[%1] 이미 처리 중 - 트리거 무시").arg(frameIdx);
        nextFrameIndex[triggerCameraIndex] = frameIdx;  // 롤백 (다음 트리거에서 사용 가능)
        return;
    }
    
    // 사용한 프레임 인덱스 기록
    lastUsedFrameIndex = frameIdx;
    
    // ★★★ 완전 독립 메모리 사용 - cameraFrames 접근 안 함
    cv::Mat frameForInspection = frame.clone();
    
    // **통합 로그 출력** - 제거됨

    // **4분할 화면 갱신은 검사 완료 후에만 수행 (processNextInspection 내부에서)**

    // **패턴이 없으면 프레임만 4분할에 표시 (검사 스킵)**
    if (!cameraView)
    {
        qDebug() << "[onTriggerSignalReceived] cameraView 없음 - 프레임 표시만 진행";
        triggerProcessing = false;
        return;
    }

    // ★★★ 하드웨어 트리거 들어오면 무조건 해당 프레임에 이미지 저장
    {
        QMutexLocker locker(&cameraFramesMutex);
        cameraFrames[frameIdx] = frameForInspection.clone();
        frameUpdatedFlags[frameIdx] = true;
    }
    
    // 메인 카메라뷰 무조건 업데이트
    if (cameraView)
    {
        QImage qImage(frameForInspection.data, frameForInspection.cols, frameForInspection.rows, 
                      frameForInspection.step, QImage::Format_BGR888);
        QPixmap pixmap = QPixmap::fromImage(qImage);
        
        QMetaObject::invokeMethod(cameraView, [this, pixmap]() {
            if (cameraView) {
                cameraView->setBackgroundPixmap(pixmap);
                cameraView->viewport()->update();
            }
        }, Qt::QueuedConnection);
    }
    
    // 4분할 화면에서 해당 프레임만 즉시 갱신
    if (cameraView && cameraView->getQuadViewMode())
    {
        QMutexLocker locker(&cameraFramesMutex);
        cameraView->setQuadFrames(cameraFrames);  // 배열 전체 전달하지만 frameIdx만 변경됨
        cameraView->viewport()->update();
    }
    
    // ★★★ TEACH ON 상태일 때 해당 프레임의 미리보기 무조건 업데이트 (레시피 유무와 무관)
    if (teachingEnabled && frameIdx >= 0 && frameIdx < 4 && previewOverlayLabels[frameIdx])
    {
        updateSinglePreviewWithFrame(frameIdx, frameForInspection);
    }
    
    // 해당 프레임의 패턴 리스트 사용 (미리 분리된 리스트)
    const QList<PatternInfo>& framePatterns = framePatternLists[frameIdx];
    
    if (framePatterns.isEmpty())
    {
        
        // ★ 프레임 처리 완료 플래그 해제
        frameProcessing[frameIdx] = false;
        triggerProcessing = false;
        return;
    }

    // **레시피 있음 → 독립 메모리로 검사**
    // ★ 검사 전에 currentDisplayFrameIndex 업데이트 (패턴 트리 동기화용)
    currentDisplayFrameIndex = frameIdx;
    
    // ★ CameraView의 currentFrameIndex도 업데이트 (패턴 렌더링용)
    if (cameraView) {
        cameraView->setCurrentFrameIndex(frameIdx);
    }
    
    bool passed = runInspect(frameForInspection, triggerCameraIndex, false, frameIdx);
    
    // ★★★ 검사 완료 후 4분할 화면에 결과 저장 (카운트 업데이트)
    if (cameraView && cameraView->getQuadViewMode()) {
        // framePatternLists에서 현재 프레임의 패턴 가져오기
        const QList<PatternInfo>& framePatterns = framePatternLists[frameIdx];
        QString cameraName = (triggerCameraIndex >= 0 && triggerCameraIndex < cameraInfos.size()) ? cameraInfos[triggerCameraIndex].serialNumber : "";
        
        // 검사 결과 재획득
        InspectionResult quadResult = insProcessor->performInspection(frameForInspection, framePatterns, cameraName);
        
        QImage qImage(frameForInspection.data, frameForInspection.cols, frameForInspection.rows, 
                      frameForInspection.step, QImage::Format_BGR888);
        QPixmap pixmap = QPixmap::fromImage(qImage);
        cameraView->saveInspectionResultForMode(frameIdx, quadResult, pixmap);
    }
    
    // ★ 검사 후 패턴 트리 업데이트 및 화면 갱신 (UI 스레드에서 실행)
    QMetaObject::invokeMethod(this, [this]() {
        updatePatternTree();
        if (cameraView) {
            cameraView->viewport()->update();
        }
    }, Qt::QueuedConnection);
    
    // ★★★ 검사 완료 후 cameraFrames에 저장 (shallow copy - 빠름)
    cameraFrames[frameIdx] = frameForInspection;  // assignment operator (참조 카운팅)
    frameUpdatedFlags[frameIdx] = true;
    
    // 트리거 처리 완료 시간 측정
    auto triggerEndTime = std::chrono::high_resolution_clock::now();
    auto triggerDuration = std::chrono::duration_cast<std::chrono::milliseconds>(triggerEndTime - triggerStartTime).count();
    qDebug().noquote() << QString("[Trigger] 전체 처리 시간: %1ms (Cam:%2 Frame[%3])").arg(triggerDuration).arg(triggerCameraIndex).arg(frameIdx);
    
    // ★★★ 시리얼 통신으로 검사 결과 전송 (4바이트: 0xFF frameIdx+1 result 0xEF)
    if (serialCommunication && serialCommunication->isConnected()) {
        serialCommunication->sendInspectionResult(frameIdx, passed);
    }
    
    // 프레임 처리 완료 플래그 해제
    frameProcessing[frameIdx] = false;
}

// processNextInspection 함수 제거됨 - 순차 처리로 변경

void TeachingWidget::startCamera()
{
    // ★ 이미 카메라가 실행 중이면 먼저 중지
    if (!camOff) {
        qDebug() << "[startCamera] 카메라가 이미 실행 중 - 먼저 중지합니다";
        stopCamera();
        QThread::msleep(800);  // ★★★ 딜레이 증가 (USB 하드웨어 리셋 대기)
        qDebug() << "[startCamera] stopCamera 완료 후 대기 완료";
    }

    // ★ CAM ON 상태로 변경
    camOff = false;
    
    // ★ 서버 프레임 인덱스 초기화
    nextFrameIndex[0] = -1;
    nextFrameIndex[1] = -1;
    lastUsedFrameIndex = -1;
    totalTriggersReceived = 0;
    totalInspectionsExecuted = 0;
    qDebug() << "[startCamera] 서버 프레임 인덱스 초기화 완료";

    // CameraView에 TEACH OFF 상태 전달
    if (cameraView)
    {
        cameraView->setTeachOff(false);
    }

    // ★ 모든 레시피 데이터 초기화 (공용 함수 사용)
    // clearAllRecipeData() 내부에 CAM ON 체크가 있으므로 직접 초기화
    for (auto& frame : cameraFrames) {
        frame.release();
    }
    if (cameraView)
    {
        cameraView->setBackgroundPixmap(QPixmap());
        cameraView->clearPatterns();
        cameraView->setSelectedPatternId(QUuid());
        cameraView->update();
    }
    if (patternTree)
    {
        patternTree->clear();
    }

    // 1-6. 프로퍼티 패널 초기화
    if (propertyStackWidget)
    {
        propertyStackWidget->setCurrentIndex(0); // 기본 패널로
    }

    // 2. CAM 버튼 상태 먼저 업데이트 (즉시 UI 반응)
    updateCameraButtonState(true);

    // 3. 카메라 정보 갱신
    detectCameras();

    // 4. 기존 스레드 중지 및 정리
    for (CameraGrabberThread *thread : cameraThreads)
    {
        if (thread && thread->isRunning())
        {
            thread->stopGrabbing();
            thread->wait();
            delete thread;
        }
    }
    cameraThreads.clear();

    if (uiUpdateThread && uiUpdateThread->isRunning())
    {
        uiUpdateThread->stopUpdating();
        uiUpdateThread->wait();
    }

    if (cameraInfos.isEmpty())
    {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Warning);
        msgBox.setTitle("카메라 오류");
        msgBox.setMessage("연결된 카메라가 없습니다.");
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
        
        // ★ 카메라가 없으면 camOff 상태로 복원
        camOff = true;
        
        // CameraView에 TEACH OFF 상태 전달
        if (cameraView)
        {
            cameraView->setTeachOff(true);
        }
        
        updateCameraButtonState(false); // 버튼 상태 업데이트
        return;
    }

    // 6. 메인 카메라 설정
    cameraIndex = 0;

    // 현재 카메라 UUID 및 이름 설정
    if (cameraView)
    {
        cameraView->setCurrentCameraUuid(cameraInfos[cameraIndex].uniqueId);
        // 카메라 이름을 숫자로만 설정 (0, 1, 2, 3)
        QString cameraName = QString::number(cameraIndex);
        cameraView->setCurrentCameraName(cameraName);
    }

    // 5. 미리보기 오버레이는 updatePreviewFrames에서 자동 업데이트됨

    // 7. 카메라 스레드 생성 및 시작
    for (int i = 0; i < cameraInfos.size(); i++)
    {
        if (cameraInfos[i].isConnected)
        {
            CameraGrabberThread *thread = new CameraGrabberThread(this);
            thread->setCameraIndex(i);
            // 오버로드 함수를 명시적으로 지정
            connect(thread, &CameraGrabberThread::frameGrabbed,
                    this, static_cast<void(TeachingWidget::*)(const cv::Mat&, int)>(&TeachingWidget::processGrabbedFrame), Qt::QueuedConnection);
            connect(thread, &CameraGrabberThread::triggerSignalReceived,
                    this, &TeachingWidget::onTriggerSignalReceived, Qt::DirectConnection);
            thread->start(QThread::NormalPriority);
            cameraThreads.append(thread);
        }
    }

    // 8. UI 업데이트 스레드 시작 (강제로 시작)
    if (uiUpdateThread)
    {
        if (!uiUpdateThread->isRunning())
        {
            uiUpdateThread->start(QThread::NormalPriority);
            QThread::msleep(100); // 스레드 시작 대기
        }
    }
    else
    {
        // UI 업데이트 스레드가 없으면 생성
        uiUpdateThread = new UIUpdateThread(this);
        uiUpdateThread->start(QThread::NormalPriority);
    }

    // 9. 카메라 연결 상태 확인
    bool cameraStarted = false;
    for (const auto &cameraInfo : cameraInfos)
    {
        if (cameraInfo.isConnected)
        {
            cameraStarted = true;
            break;
        }
    }

    // 10. 카메라가 연결된 경우 최근 레시피 자동 로드
    if (cameraStarted)
    {
        QString lastRecipePath = ConfigManager::instance()->getLastRecipePath();
        if (!lastRecipePath.isEmpty())
        {
            onRecipeSelected(lastRecipePath);
        }
    }

    // 11. 패턴 트리 업데이트
    updatePatternTree();
}

void TeachingWidget::updateCameraButtonState(bool isStarted)
{
    if (!startCameraButton)
        return;

    startCameraButton->blockSignals(true);

    if (isStarted)
    {
        // 카메라 시작됨 - 영상 스트리밍 중
        startCameraButton->setChecked(true);
        startCameraButton->setText(TR("CAM ON")); // 또는 "STREAMING"
        startCameraButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_CAM_OFF_COLOR, UIColors::BTN_CAM_ON_COLOR, true));
    }
    else
    {
        // 카메라 중지됨 - 영상 없음
        startCameraButton->setChecked(false);
        startCameraButton->setText(TR("CAM OFF")); // 또는 "NO VIDEO"
        startCameraButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_CAM_OFF_COLOR, UIColors::BTN_CAM_ON_COLOR, false));
    }

    startCameraButton->blockSignals(false);

    // UI 요소들 활성화/비활성화는 제거됨
}

void TeachingWidget::stopCamera()
{

    // ★ CAM OFF 상태로 변경
    camOff = true;
    
    // CameraView에 TEACH OFF 상태 전달
    if (cameraView)
    {
        cameraView->setTeachOff(true);
    }

    // ★ cameraFrames 초기화 - CAM ON에서 사용한 프레임 제거
    for (auto& frame : cameraFrames) {
        frame.release();
    }
    qDebug() << "[stopCamera] cameraFrames 초기화 완료";

    // UI 요소들 비활성화 제거됨

    // 1. 멀티 카메라 스레드 중지
    qDebug() << "[stopCamera] 카메라 스레드 중지 시작...";
    for (int i = cameraThreads.size() - 1; i >= 0; i--)
    {
        if (cameraThreads[i] && cameraThreads[i]->isRunning())
        {
            qDebug() << "[stopCamera] 스레드" << i << "중지 중...";
            cameraThreads[i]->stopGrabbing();
            
            // 최대 3초 대기
            if (!cameraThreads[i]->wait(3000))
            {
                qDebug() << "[stopCamera] 스레드" << i << "강제 종료";
                cameraThreads[i]->terminate();
                cameraThreads[i]->wait(1000);
            }
            
            delete cameraThreads[i];
            qDebug() << "[stopCamera] 스레드" << i << "중지 완료";
        }
    }
    cameraThreads.clear();
    QThread::msleep(100);  // 스레드 완전 종료 대기;

    // 2. UI 업데이트 스레드 중지
    if (uiUpdateThread && uiUpdateThread->isRunning())
    {
        uiUpdateThread->stopUpdating();
        uiUpdateThread->wait();
    }

#ifdef USE_SPINNAKER
    // 3. Spinnaker 카메라 정리
    qDebug() << "[stopCamera] Spinnaker 카메라 정리 시작...";
    if (m_useSpinnaker)
    {
        try
        {
            // 역순으로 카메라 정리
            for (int i = m_spinCameras.size() - 1; i >= 0; i--)
            {
                qDebug() << "[stopCamera] Spinnaker 카메라" << i << "정리 중...";
                
                if (!m_spinCameras[i])
                {
                    qDebug() << "[stopCamera] 카메라" << i << "이미 nullptr";
                    continue;
                }

                try
                {
                    // 현재 스트리밍 중이면 중지
                    if (m_spinCameras[i]->IsValid() && m_spinCameras[i]->IsStreaming())
                    {
                        qDebug() << "[stopCamera] 카메라" << i << "스트리밍 중지 중...";
                        m_spinCameras[i]->EndAcquisition();
                        QThread::msleep(100);
                        qDebug() << "[stopCamera] 카메라" << i << "스트리밍 중지 완료";
                    }
                }
                catch (Spinnaker::Exception &e)
                {
                    qDebug() << "[stopCamera] 카메라" << i << "EndAcquisition 실패:" << e.what();
                }

                try
                {
                    // 카메라 초기화 해제
                    if (m_spinCameras[i]->IsValid() && m_spinCameras[i]->IsInitialized())
                    {
                        qDebug() << "[stopCamera] 카메라" << i << "DeInit 중...";
                        m_spinCameras[i]->DeInit();
                        QThread::msleep(100);  // ★ 딜레이 증가 (하드웨어 리셋 대기)
                        qDebug() << "[stopCamera] 카메라" << i << "DeInit 완료";
                    }
                }
                catch (Spinnaker::Exception &e)
                {
                    qDebug() << "[stopCamera] 카메라" << i << "DeInit 실패:" << e.what();
                }

                // ★★★ [중요] DeInit 후 명시적으로 nullptr 설정 (참조 카운트 감소)
                qDebug() << "[stopCamera] 카메라" << i << "참조 해제 (nullptr)";
                m_spinCameras[i] = nullptr;
                QThread::msleep(50);  // ★ 포인터 소멸 대기
            }
            // ★★★ [중요] 벡터 clear로 모든 스마트 포인터 참조 해제
            m_spinCameras.clear();
            QThread::msleep(300);  // ★ 모든 포인터 소멸 대기 (증가)

            // ★★★ [중요] CameraList도 명시적으로 Clear (참조 카운트 정리)
            if (m_spinCamList.GetSize() > 0)
            {
                qDebug() << "[stopCamera] 카메라 리스트 Clear 중...";
                m_spinCamList.Clear();
                QThread::msleep(300);  // ★ Clear 완료 대기 (증가)
                qDebug() << "[stopCamera] 카메라 리스트 Clear 완료";
            }
            
            qDebug() << "[stopCamera] Spinnaker 카메라 정리 완료";
        }
        catch (Spinnaker::Exception &e)
        {
            qDebug() << "[stopCamera] Spinnaker 정리 중 예외:" << e.what();
        }
        catch (...)
        {
            qDebug() << "[stopCamera] Spinnaker 정리 중 알 수 없는 예외";
        }
    }
#endif

    // 4. OpenCV 카메라 자원 해제
    for (int i = 0; i < cameraInfos.size(); i++)
    {
        // OpenCV capture 제거됨 (Spinnaker SDK만 사용)
        cameraInfos[i].isConnected = false;
    }

    // 5. 4분할 미리보기 초기화
    for (int i = 0; i < 4; i++)
    {
        if (previewOverlayLabels[i])
        {
            previewOverlayLabels[i]->clear();
            previewOverlayLabels[i]->setText("");
        }
    }
    qDebug() << "[stopCamera] 4분할 미리보기 초기화 완료";

    // 6. 메인 카메라 뷰 초기화
    if (cameraView)
    {
        cameraView->setInspectionMode(false);

        // 카메라 이름 초기화 - "연결 없음" 표시하기 위해
        cameraView->setCurrentCameraName("");

        // **camOff 모드에서는 티칭 이미지(cameraFrames) 유지**
        if (!camOff)
        {
            for (auto& frame : cameraFrames) {
                frame.release();
            }
        }

        // 모든 패턴들 지우기
        cameraView->clearPatterns();
        
        // 검사 결과 초기화 (4분할 뷰 결과 포함)
        cameraView->clearInspectionResult();

        // 백그라운드 이미지도 지우기 - 초기화면(화면 없음) 보여주기
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
    if (runStopButton && runStopButton->isChecked())
    {
        runStopButton->blockSignals(true);
        runStopButton->setChecked(false);
        runStopButton->setText("RUN");
        runStopButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));
        runStopButton->blockSignals(false);
    }

    // 9. 카메라 정보 목록 비우기
    cameraInfos.clear();
    cameraIndex = -1;
}

void TeachingWidget::saveCurrentImage()
{
    // 현재 카메라 프레임 확인
    cv::Mat frameToSave;
    {
        QMutexLocker locker(&cameraFramesMutex);
        if (cameraIndex < 0 || cameraIndex >= static_cast<int>(4) ||
            cameraFrames[cameraIndex].empty())
        {
            CustomMessageBox(this, CustomMessageBox::Warning, TR("SAVE_IMAGE"),
                             "저장할 이미지가 없습니다.\n카메라를 시작하고 이미지를 캡처해주세요.")
                .exec();
            return;
        }
        // 현재 프레임 저장
        frameToSave = cameraFrames[cameraIndex].clone();
    }

    // 저장 경로 선택 다이얼로그 (CustomFileDialog 사용)
    QString defaultFileName = QString("image_%1.png")
                                  .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));

    QString filePath = CustomFileDialog::getSaveFileName(
        this,
        "이미지 저장",
        defaultFileName,
        "PNG 이미지 (*.png);;JPEG 이미지 (*.jpg *.jpeg);;BMP 이미지 (*.bmp);;모든 파일 (*.*)"
    );

    if (filePath.isEmpty())
    {
        return; // 사용자가 취소
    }

    try
    {
        // OpenCV는 BGR 형식이므로 그대로 저장
        if (cv::imwrite(filePath.toStdString(), frameToSave))
        {
            CustomMessageBox(this, CustomMessageBox::Information, TR("SAVE_IMAGE"),
                             QString("이미지가 저장되었습니다:\n%1").arg(filePath))
                .exec();
        }
        else
        {
            CustomMessageBox(this, CustomMessageBox::Critical, TR("SAVE_IMAGE"),
                             "이미지 저장에 실패했습니다.")
                .exec();
        }
    }
    catch (const std::exception &e)
    {
        CustomMessageBox(this, CustomMessageBox::Critical, TR("SAVE_IMAGE"),
                         QString("이미지 저장 중 오류 발생:\n%1").arg(e.what()))
            .exec();
    }
}

void TeachingWidget::updateUITexts()
{
    // **언어매니저 내부 번역 맵 확인**
    const auto &translations = LanguageManager::instance()->getAllTranslations();

    // 기본 UI 텍스트 업데이트
    if (roiButton)
        roiButton->setText(TR("ROI"));
    if (fidButton)
        fidButton->setText(TR("FID"));
    if (insButton)
        insButton->setText(TR("INS"));

    // 메뉴 텍스트 업데이트 및 활성화 상태 유지
    if (fileMenu)
        fileMenu->setTitle(TR("FILE_MENU"));
    if (settingsMenu)
    {
        settingsMenu->setTitle(TR("SETTINGS_MENU"));
        settingsMenu->setEnabled(true); // 활성화 상태 유지
    }
    if (helpMenu)
    {
        helpMenu->setTitle(TR("HELP_MENU"));
        helpMenu->setEnabled(true); // 활성화 상태 유지
    }

    // 액션 텍스트 업데이트 및 활성화 상태 유지
    if (exitAction)
        exitAction->setText(TR("EXIT"));

    if (serverSettingsAction)
    {
        serverSettingsAction->setText(TR("SERVER_SETTINGS"));
        serverSettingsAction->setEnabled(true); // 활성화 상태 유지
    }
    if (serialSettingsAction)
    {
        serialSettingsAction->setText(TR("SERIAL_SETTINGS"));
        serialSettingsAction->setEnabled(true); // 활성화 상태 유지
    }
    if (languageSettingsAction)
    {
        languageSettingsAction->setText(TR("LANGUAGE_SETTINGS"));
        languageSettingsAction->setEnabled(true); // 활성화 상태 유지
    }
    if (aboutAction)
    {
        aboutAction->setText(TR("ABOUT"));
        aboutAction->setEnabled(true); // 활성화 상태 유지
    }

    // **패턴 트리 헤더 업데이트**
    if (patternTree)
    {
        QStringList headers;
        headers << TR("PATTERN_NAME") << TR("PATTERN_TYPE") << TR("PATTERN_STATUS");
        patternTree->setHeaderLabels(headers);

        // 헤더 뷰 갱신
        QHeaderView *header = patternTree->header();
        header->update();
        header->repaint();

        // 기존 패턴들의 텍스트도 갱신
        updateTreeItemTexts(nullptr);
    }

    // 나머지 UI 텍스트들도 TR로 처리
    if (emptyPanelLabel)
        emptyPanelLabel->setText(TR("EMPTY_PANEL_MESSAGE"));
    if (basicInfoLabel)
        basicInfoLabel->setText(TR("BASIC_INFO"));
    if (patternIdLabel)
        patternIdLabel->setText(TR("PATTERN_ID"));
    if (patternNameLabel)
        patternNameLabel->setText(TR("PATTERN_NAME_LABEL"));
    if (patternTypeLabel)
        patternTypeLabel->setText(TR("PATTERN_TYPE_LABEL"));
    if (positionSizeLabel)
        positionSizeLabel->setText(TR("POSITION_SIZE"));
    if (positionLabel)
        positionLabel->setText(TR("POSITION"));
    if (sizeLabel)
        sizeLabel->setText(TR("SIZE"));

    // CameraView의 텍스트도 업데이트
    if (cameraView)
    {
        cameraView->updateUITexts();
    }

    // **강제로 모든 메뉴 활성화 (언어 변경 후에도 유지)**
    if (menuBar)
    {
        // 도움말 메뉴가 없으면 다시 생성
        if (!helpMenu)
        {
            helpMenu = menuBar->addMenu(TR("HELP_MENU"));
            helpMenu->setEnabled(true);
            helpMenu->menuAction()->setMenuRole(QAction::NoRole);

            if (!aboutAction)
            {
                aboutAction = helpMenu->addAction(TR("ABOUT"));
                aboutAction->setEnabled(true);
                aboutAction->setMenuRole(QAction::NoRole);
                connect(aboutAction, &QAction::triggered, this, &TeachingWidget::showAboutDialog);
            }
        }

        QList<QAction *> actions = menuBar->actions();
        for (QAction *action : actions)
        {
            action->setEnabled(true);
            if (action->menu())
            {
                action->menu()->setEnabled(true);
                QList<QAction *> subActions = action->menu()->actions();
                for (QAction *subAction : subActions)
                {
                    subAction->setEnabled(true);
                }
            }
        }
    }

    // 전체 위젯 강제 갱신 및 즉시 처리
    this->repaint();
    QApplication::processEvents(); // 즉시 화면 갱신

    // 모든 자식 위젯들도 강제 갱신
    QList<QWidget *> childWidgets = this->findChildren<QWidget *>();
    for (QWidget *child : childWidgets)
    {
        child->update();
    }
}

void TeachingWidget::updateTreeItemTexts(QTreeWidgetItem *item)
{
    // item이 null이면 모든 최상위 아이템부터 시작
    if (!item)
    {
        for (int i = 0; i < patternTree->topLevelItemCount(); i++)
        {
            updateTreeItemTexts(patternTree->topLevelItem(i));
        }
        return;
    }

    // 현재 아이템의 텍스트 갱신
    QString idStr = item->data(0, Qt::UserRole).toString();
    QVariant filterIndexVar = item->data(0, Qt::UserRole + 1);

    if (filterIndexVar.isValid())
    {
        // 필터 아이템인 경우
        int filterIndex = filterIndexVar.toInt();
        QUuid patternId = QUuid(idStr);
        PatternInfo *pattern = cameraView->getPatternById(patternId);

        if (pattern && filterIndex >= 0 && filterIndex < pattern->filters.size())
        {
            const FilterInfo &filter = pattern->filters[filterIndex];

            // 필터 이름을 번역된 텍스트로 변경
            QString filterName = getFilterTypeName(filter.type);
            item->setText(0, filterName);
            item->setText(1, TR("FILTER_TYPE_ABBREV")); // "FIL" 등

            // 상태 텍스트도 번역
            item->setText(2, filter.enabled ? TR("ACTIVE") : TR("INACTIVE"));
        }
    }
    else
    {
        // 패턴 아이템인 경우
        QUuid patternId = QUuid(idStr);
        PatternInfo *pattern = cameraView->getPatternById(patternId);

        if (pattern)
        {
            // 패턴 타입 텍스트 번역
            QString typeText;
            switch (pattern->type)
            {
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
    for (int i = 0; i < item->childCount(); i++)
    {
        updateTreeItemTexts(item->child(i));
    }
}

void TeachingWidget::setSerialCommunication(SerialCommunication *serialComm)
{
    serialCommunication = serialComm;
}

void TeachingWidget::showServerSettings()
{
    // 서버 설정 다이얼로그 표시
    ClientDialog::instance(this)->exec();
}

void TeachingWidget::showSerialSettings()
{
    // 시리얼 통신 객체가 없으면 에러
    if (!serialCommunication)
    {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Warning);
        msgBox.setTitle(TR("WARNING"));
        msgBox.setMessage("시리얼 통신이 초기화되지 않았습니다.");
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    // 시리얼 설정 다이얼로그가 없으면 생성
    if (!serialSettingsDialog)
    {
        serialSettingsDialog = new SerialSettingsDialog(serialCommunication, this);
    }

    // 다이얼로그 표시 전에 현재 연결 상태를 오버레이에 반영
    if (serialStatusLabel) {
        bool isConnected = serialCommunication->isConnected();
        if (isConnected) {
            QString portName = ConfigManager::instance()->getSerialPort();
            if (portName.contains(" (")) {
                portName = portName.left(portName.indexOf(" ("));
            }
            serialStatusLabel->setText(QString("📡 시리얼: 연결됨 (%1)").arg(portName));
            serialStatusLabel->setStyleSheet(
                "QLabel {"
                "  background-color: rgba(0, 100, 0, 180);"
                "  color: white;"
                "  border: 1px solid #555;"
                "  padding-left: 8px;"
                "  font-size: 12px;"
                "}");
        } else {
            serialStatusLabel->setText("📡 시리얼: 미연결");
            serialStatusLabel->setStyleSheet(
                "QLabel {"
                "  background-color: rgba(100, 0, 0, 180);"
                "  color: white;"
                "  border: 1px solid #555;"
                "  padding-left: 8px;"
                "  font-size: 12px;"
                "}");
        }
    }

    // 다이얼로그 표시
    serialSettingsDialog->exec();
}

void TeachingWidget::showModelManagement()
{
    // 레시피가 로드되지 않았으면 경고
    if (currentRecipeName.isEmpty()) {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Warning);
        msgBox.setTitle("경고");
        msgBox.setMessage("로드된 레시피가 없습니다.\n먼저 레시피를 로드하세요.");
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }
    
    // cameraView에서 모든 패턴 가져오기
    const QList<PatternInfo>& allPatterns = cameraView->getPatterns();
    
    // 전체 패턴과 ANOMALY 패턴 분리
    QVector<PatternInfo*> allPatternsVec;
    QVector<PatternInfo*> anomalyPatterns;
    for (int i = 0; i < allPatterns.size(); ++i) {
        // const_cast 사용하여 포인터로 변환 (TrainDialog에서 읽기만 함)
        PatternInfo* pattern = const_cast<PatternInfo*>(&allPatterns[i]);
        allPatternsVec.append(pattern);  // 모든 패턴 추가 (FID 포함)
        if (pattern && pattern->inspectionMethod == InspectionMethod::ANOMALY) {
            anomalyPatterns.append(pattern);
        }
    }
    
    // TrainDialog가 없으면 생성
    if (!activeTrainDialog)
    {
        activeTrainDialog = new TrainDialog(this);
        
        // 학습 요청 시그널 연결 (필요시)
        connect(activeTrainDialog, &TrainDialog::trainRequested, this, [this](const QString& patternName) {
            qDebug() << "[TeachingWidget] 학습 요청:" << patternName;
            // 여기서 실제 학습 로직 호출 가능
        });
        
        // 학습 완료 시그널 연결 - 모델 리로딩
        connect(activeTrainDialog, &TrainDialog::trainingFinished, this, [this](bool success) {
            if (success) {
                qDebug() << "[TRAIN] 학습 완료됨. 모델 리로딩 시작...";
                // 기존 모델 해제
                ImageProcessor::releasePatchCoreTensorRT();
                qDebug() << "[TRAIN] 모델 리로딩 완료. 새로 학습된 모델을 사용할 수 있습니다.";
                
                // results 폴더 정리 (학습 중 생성된 임시 파일들)
                QString resultsPath = QCoreApplication::applicationDirPath() + "/results";
                QDir resultsDir(resultsPath);
                if (resultsDir.exists()) {
                    qDebug() << "[TRAIN] results 폴더 정리 시작:" << resultsPath;
                    if (resultsDir.removeRecursively()) {
                        qDebug() << "[TRAIN] results 폴더 삭제 완료";
                    } else {
                        qWarning() << "[TRAIN] results 폴더 삭제 실패";
                    }
                }
            }
        });
    }
    
    // 현재 모드 전달 (0: STRIP, 1: CRIMP)
    int currentMode = 0; // 기본값
    activeTrainDialog->setAllPatterns(allPatternsVec);  // 모든 패턴 설정 (FID 찾기용)
    activeTrainDialog->setAnomalyPatterns(anomalyPatterns);
    activeTrainDialog->setCurrentRecipeName(currentRecipeName);  // 현재 레시피명 전달
    
    // 다이얼로그 표시
    activeTrainDialog->show();
    activeTrainDialog->raise();
    activeTrainDialog->activateWindow();
}

void TeachingWidget::showTestDialog()
{
    if (!testDialog) {
        testDialog = new TestDialog(this);
    }
    testDialog->show();
    testDialog->raise();
    testDialog->activateWindow();
}

void TeachingWidget::openLanguageSettings()
{
    LanguageSettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted)
    {
        // 언어가 변경된 경우 UI 텍스트 업데이트
        updateUITexts();
    }
}



void TeachingWidget::selectFilterForPreview(const QUuid &patternId, int filterIndex)
{
    // 필터 미리보기를 위한 선택 상태 설정
    selectedPatternId = patternId;
    selectedFilterIndex = filterIndex;
}

void TeachingWidget::updateCameraFrame()
{
    // **시뮬레이션 모드 처리**
    if (camOff)
    {
        // CAM OFF 모드에서는 currentDisplayFrameIndex를 직접 사용
        int frameIndex = currentDisplayFrameIndex;

        if (frameIndex < static_cast<int>(4) && !cameraFrames[frameIndex].empty())
        {
            cv::Mat currentFrame = cameraFrames[frameIndex];

            // ★ 모든 필터 적용 (선택된 필터만이 아닌 전체 필터 체인)
            cv::Mat filteredFrame = cameraFrames[frameIndex].clone();
            
            if (cameraView)
            {
                cameraView->applyFiltersToImage(filteredFrame);
            }

            // 필터링된 프레임이 없으면 원본 사용
            if (filteredFrame.empty())
            {
                filteredFrame = cameraFrames[frameIndex].clone();
            }

            // RGB 변환 및 UI 업데이트
            cv::Mat displayFrame;
            if (filteredFrame.channels() == 3)
            {
                cv::cvtColor(filteredFrame, displayFrame, cv::COLOR_BGR2RGB);
            }
            else
            {
                displayFrame = filteredFrame.clone();
            }

            QImage image;
            if (displayFrame.channels() == 3)
            {
                image = QImage(displayFrame.data, displayFrame.cols, displayFrame.rows,
                               displayFrame.step, QImage::Format_RGB888)
                            .copy();
            }
            else
            {
                image = QImage(displayFrame.data, displayFrame.cols, displayFrame.rows,
                               displayFrame.step, QImage::Format_Grayscale8)
                            .copy();
            }

            QPixmap pixmap = QPixmap::fromImage(image);

            QSize origSize(cameraFrames[frameIndex].cols, cameraFrames[frameIndex].rows);
            cameraView->setScalingInfo(origSize, cameraView->size());
            cameraView->setStatusInfo("SIM");
            cameraView->setBackgroundPixmap(pixmap);
            cameraView->update();
            return;
        }
    }

    // **메인 카메라 프레임 업데이트만 처리**
    if (cameraIndex >= 0 && cameraIndex < cameraInfos.size() &&
        cameraInfos[cameraIndex].isConnected)
    {

#ifdef USE_SPINNAKER
        // Spinnaker 카메라 확인
        if (m_useSpinnaker && cameraInfos[cameraIndex].uniqueId.startsWith("SPINNAKER_") &&
            cameraIndex < static_cast<int>(m_spinCameras.size()))
        {

            cv::Mat frame = grabFrameFromSpinnakerCamera(m_spinCameras[cameraIndex]);

            if (!frame.empty())
            {
                // **벡터에 저장**

                cv::Mat bgrFrame;
                cv::cvtColor(frame, bgrFrame, cv::COLOR_RGB2BGR);
                cameraFrames[cameraIndex] = bgrFrame.clone();

                // ★ 모든 필터 적용 (선택된 필터만이 아닌 전체 필터 체인)
                cv::Mat filteredFrame = cameraFrames[cameraIndex].clone();
                if (cameraView)
                {
                    cameraView->applyFiltersToImage(filteredFrame);
                }

                // 필터링된 프레임이 없으면 원본 사용
                if (filteredFrame.empty())
                {
                    filteredFrame = cameraFrames[cameraIndex].clone();
                }

                // RGB 변환 및 UI 업데이트
                cv::Mat displayFrame;
                cv::cvtColor(filteredFrame, displayFrame, cv::COLOR_BGR2RGB);

                QImage image(displayFrame.data, displayFrame.cols, displayFrame.rows,
                             displayFrame.step, QImage::Format_RGB888);
                QPixmap pixmap = QPixmap::fromImage(image.copy());

                QSize origSize(frame.cols, frame.rows);
                cameraView->setScalingInfo(origSize, cameraView->size());
                cameraView->setStatusInfo(QString("CAM%1").arg(cameraIndex + 1));

                cameraView->setBackgroundPixmap(pixmap);
            }
        }
#endif
        // OpenCV 카메라 제거됨 (Spinnaker SDK만 사용)
    }
}

bool TeachingWidget::eventFilter(QObject *watched, QEvent *event)
{
    // 카메라 컨테이너 리사이즈 이벤트 처리
    if (watched->objectName() == "cameraContainer" && event->type() == QEvent::Resize)
    {
        QWidget *container = qobject_cast<QWidget *>(watched);
        if (container && cameraView)
        {
            // CameraView 크기 조정
            cameraView->setGeometry(0, 0, container->width(), container->height());

            // 버튼 오버레이 크기 조정
            QWidget *buttonOverlay = container->findChild<QWidget *>("buttonOverlay");
            if (buttonOverlay)
            {
                buttonOverlay->setGeometry(0, 0, container->width(), 60);
            }
        }
    }

    // 오른쪽 패널 오버레이 드래그 및 리사이즈 처리
    if (watched == rightPanelOverlay)
    {
        QMouseEvent *mouseEvent = nullptr;
        QHoverEvent *hoverEvent = nullptr;

        if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::MouseButtonRelease)
        {
            mouseEvent = static_cast<QMouseEvent *>(event);
        }
        else if (event->type() == QEvent::HoverMove)
        {
            hoverEvent = static_cast<QHoverEvent *>(event);
        }

        if (event->type() == QEvent::MouseMove)
        {
            if (rightPanelResizing)
            {
                // 리사이즈 중
                QPoint globalDelta = mouseEvent->globalPosition().toPoint() - rightPanelDragPos;
                QRect geo = rightPanelOverlay->geometry();

                if (rightPanelResizeEdge == ResizeEdge::Right || rightPanelResizeEdge == ResizeEdge::BottomRight)
                {
                    int newWidth = geo.width() + globalDelta.x();
                    if (newWidth >= rightPanelOverlay->minimumWidth())
                    {
                        geo.setWidth(newWidth);
                    }
                }
                if (rightPanelResizeEdge == ResizeEdge::Bottom || rightPanelResizeEdge == ResizeEdge::BottomRight)
                {
                    int newHeight = geo.height() + globalDelta.y();
                    if (newHeight >= 200)
                    {
                        geo.setHeight(newHeight);
                    }
                }

                rightPanelOverlay->setGeometry(geo);
                rightPanelDragPos = mouseEvent->globalPosition().toPoint();
                return true;
            }
            else if (rightPanelDragging)
            {
                // 드래그 중
                rightPanelOverlay->setCursor(Qt::ClosedHandCursor);
                QPoint delta = mouseEvent->pos() - rightPanelDragPos;
                rightPanelOverlay->move(rightPanelOverlay->pos() + delta);
                return true;
            }
            else
            {
                // 드래그/리사이즈 중이 아닐 때만 커서 업데이트
                QPoint pos = mouseEvent->pos();
                int w = rightPanelOverlay->width();
                int h = rightPanelOverlay->height();
                int edgeMargin = 10;

                // 오른쪽과 하단 경계 체크 (리사이즈 가능 영역)
                bool atRight = (pos.x() >= w - edgeMargin);
                bool atBottom = (pos.y() >= h - edgeMargin);

                // 이전 상태 저장
                ResizeEdge previousEdge = rightPanelResizeEdge;

                if (atRight && atBottom)
                {
                    // 오른쪽 하단 모서리
                    rightPanelOverlay->setCursor(Qt::SizeFDiagCursor);
                    rightPanelResizeEdge = ResizeEdge::BottomRight;
                }
                else if (atRight)
                {
                    // 오른쪽 경계
                    rightPanelOverlay->setCursor(Qt::SizeHorCursor);
                    rightPanelResizeEdge = ResizeEdge::Right;
                }
                else if (atBottom)
                {
                    // 하단 경계
                    rightPanelOverlay->setCursor(Qt::SizeVerCursor);
                    rightPanelResizeEdge = ResizeEdge::Bottom;
                }
                else
                {
                    // 경계 밖(내부) - 항상 일반 포인터로 설정
                    rightPanelOverlay->setCursor(Qt::ArrowCursor);
                    rightPanelResizeEdge = ResizeEdge::None;
                }
            }
        }
        else if (event->type() == QEvent::MouseButtonPress)
        {
            if (mouseEvent->button() == Qt::LeftButton)
            {
                // 경계에 있으면 리사이즈
                if (rightPanelResizeEdge != ResizeEdge::None)
                {
                    rightPanelResizing = true;
                    rightPanelDragPos = mouseEvent->globalPosition().toPoint();
                    return true;
                }
                else
                {
                    // 경계가 아니면 드래그 가능 (자식 위젯이 아닌 경우)
                    QWidget *childWidget = rightPanelOverlay->childAt(mouseEvent->pos());
                    if (!childWidget)
                    {
                        // 빈 공간 클릭 - 드래그 시작
                        rightPanelDragging = true;
                        rightPanelDragPos = mouseEvent->pos();
                        rightPanelOverlay->setCursor(Qt::ClosedHandCursor);
                        return true;
                    }
                }
            }
        }
        else if (event->type() == QEvent::HoverMove)
        {
            // HoverMove 이벤트로 자식 위젯 위에서도 커서 업데이트
            if (!rightPanelDragging && !rightPanelResizing && hoverEvent)
            {
                QPoint pos = hoverEvent->position().toPoint();
                int w = rightPanelOverlay->width();
                int h = rightPanelOverlay->height();
                int edgeMargin = 10;

                bool atRight = (pos.x() >= w - edgeMargin);
                bool atBottom = (pos.y() >= h - edgeMargin);

                if (atRight && atBottom)
                {
                    rightPanelOverlay->setCursor(Qt::SizeFDiagCursor);
                    rightPanelResizeEdge = ResizeEdge::BottomRight;
                }
                else if (atRight)
                {
                    rightPanelOverlay->setCursor(Qt::SizeHorCursor);
                    rightPanelResizeEdge = ResizeEdge::Right;
                }
                else if (atBottom)
                {
                    rightPanelOverlay->setCursor(Qt::SizeVerCursor);
                    rightPanelResizeEdge = ResizeEdge::Bottom;
                }
                else
                {
                    rightPanelOverlay->setCursor(Qt::ArrowCursor);
                    rightPanelResizeEdge = ResizeEdge::None;
                }
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease)
        {
            if (mouseEvent->button() == Qt::LeftButton)
            {
                // 드래그/리사이즈가 끝났으면 ConfigManager에 저장
                if (rightPanelDragging || rightPanelResizing) {
                    QRect geometry = rightPanelOverlay->geometry();
                    ConfigManager::instance()->setPropertyPanelGeometry(geometry);
                    if (!rightPanelCollapsed) {
                        ConfigManager::instance()->setPropertyPanelExpandedHeight(rightPanelOverlay->height());
                    }
                }
                
                rightPanelDragging = false;
                rightPanelResizing = false;

                // 버튼 릴리즈 후 현재 마우스 위치로 상태 재평가
                QPoint globalPos = QCursor::pos();
                QPoint localPos = rightPanelOverlay->mapFromGlobal(globalPos);

                int w = rightPanelOverlay->width();
                int h = rightPanelOverlay->height();
                int edgeMargin = 10;

                bool atRight = (localPos.x() >= w - edgeMargin);
                bool atBottom = (localPos.y() >= h - edgeMargin);

                if (atRight && atBottom)
                {
                    rightPanelOverlay->setCursor(Qt::SizeFDiagCursor);
                    rightPanelResizeEdge = ResizeEdge::BottomRight;
                }
                else if (atRight)
                {
                    rightPanelOverlay->setCursor(Qt::SizeHorCursor);
                    rightPanelResizeEdge = ResizeEdge::Right;
                }
                else if (atBottom)
                {
                    rightPanelOverlay->setCursor(Qt::SizeVerCursor);
                    rightPanelResizeEdge = ResizeEdge::Bottom;
                }
                else
                {
                    rightPanelOverlay->setCursor(Qt::ArrowCursor);
                    rightPanelResizeEdge = ResizeEdge::None;
                }

                return true;
            }
        }
        else if (event->type() == QEvent::Leave)
        {
            // 오버레이 영역을 벗어나면 커서를 일반 포인터로 복원
            if (!rightPanelDragging && !rightPanelResizing)
            {
                rightPanelOverlay->setCursor(Qt::ArrowCursor);
                rightPanelResizeEdge = ResizeEdge::None;
            }
        }
    }

    if (event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {

            // **미리보기 오버레이 클릭 처리 - 해당 프레임을 메인 화면에 표시**
            for (int i = 0; i < 4; i++)
            {
                if (watched == previewOverlayLabels[i] && previewOverlayLabels[i])
                {
                    // 클릭된 미리보기의 프레임을 메인 화면에 표시
                    if (i < static_cast<int>(4) && !cameraFrames[i].empty())
                    {
                            cv::Mat displayImage = cameraFrames[i].clone();
                            cv::cvtColor(displayImage, displayImage, cv::COLOR_BGR2RGB);
                            QImage qImage(displayImage.data, displayImage.cols, displayImage.rows,
                                          displayImage.step, QImage::Format_RGB888);
                            QPixmap pixmap = QPixmap::fromImage(qImage.copy());
                            
                            // 현재 표시된 프레임 인덱스 업데이트
                            currentDisplayFrameIndex = i;
                            
                            // ★ cameraIndex는 frameIndex와 동일하게 설정 (0,1,2,3)
                            cameraIndex = i;
                            
                            if (cameraView)
                            {
                                // 드래그 중인 사각형 제거 (프레임 전환 시)
                                cameraView->clearCurrentRect();
                                
                                // 검사 모드 해제 (프레임 전환 시)
                                cameraView->setInspectionMode(false);
                                
                                // 프레임 인덱스와 카메라 UUID를 먼저 설정
                                cameraView->setCurrentFrameIndex(i);
                                
                                // 카메라 UUID 설정
                                if (i >= 0 && i < cameraInfos.size())
                                {
                                    QString frameCameraUuid = cameraInfos[i].uniqueId;
                                    cameraView->setCurrentCameraUuid(frameCameraUuid);
                                }
                                
                                // ★ 프레임 이미지를 직접 설정 (패턴 오버레이는 cameraView가 자동으로 그림)
                                cameraView->setBackgroundPixmap(pixmap);
                                cameraView->viewport()->update();
                            }
                            
                            // RUN 버튼 상태 초기화 (STOP -> RUN)
                            if (runStopButton && runStopButton->isChecked())
                            {
                                runStopButton->blockSignals(true);
                                runStopButton->setChecked(false);
                                runStopButton->setText("RUN");
                                runStopButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_ADD_COLOR, QColor("#4CAF50"), false));
                                runStopButton->blockSignals(false);
                            }
                            
                            // 패턴 트리 업데이트 (현재 프레임의 패턴만 표시)
                            updatePatternTree();
                        
                        // 미리보기 클릭 (로그 제거)
                    }
                    return true;
                }
            }

            // **템플릿 이미지 클릭 처리**
            if (watched == fidTemplateImg || watched == insTemplateImg)
            {
                QLabel *imageLabel = qobject_cast<QLabel *>(watched);
                if (imageLabel)
                {
                    QTreeWidgetItem *selectedItem = patternTree->currentItem();
                    if (selectedItem)
                    {
                        QString idStr = selectedItem->data(0, Qt::UserRole).toString();
                        QUuid patternId = QUuid(idStr);
                        PatternInfo *pattern = cameraView->getPatternById(patternId);

                        if (pattern && !pattern->templateImage.isNull())
                        {
                            QString title = QString("%1 템플릿 이미지").arg(pattern->name);
                            showImageViewerDialog(pattern->templateImage, title);
                        }
                    }
                    return true;
                }
            }
        }
    }

    // **cameraView 리사이즈 이벤트 - 상태 패널 위치 재조정**
    if (watched == cameraView && event->type() == QEvent::Resize)
    {
        updateStatusPanelPosition();
        // 로그창 위치는 사용자가 지정한 위치 유지 (강제 이동 제거)
        return QWidget::eventFilter(watched, event);
    }

    // 로그 오버레이 드래그 및 리사이즈 처리
    if (watched == logOverlayWidget && logOverlayWidget)
    {
        QMouseEvent *mouseEvent = nullptr;

        if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::MouseButtonRelease)
        {
            mouseEvent = static_cast<QMouseEvent *>(event);
        }

        if (event->type() == QEvent::MouseMove && mouseEvent)
        {
            if (logResizing)
            {
                // 리사이즈 중 - 상단 경계를 드래그하여 높이 조절
                int deltaY = mouseEvent->globalPosition().toPoint().y() - logResizeStartPos.y();
                int newHeight = logResizeStartHeight - deltaY; // 위로 드래그하면 높이 증가
                newHeight = qMax(80, qMin(newHeight, 500));    // 최소 80px, 최대 500px

                QPoint currentPos = logOverlayWidget->pos();
                int heightDiff = logOverlayWidget->height() - newHeight;

                logOverlayWidget->resize(logOverlayWidget->width(), newHeight);
                logOverlayWidget->move(currentPos.x(), currentPos.y() + heightDiff);
                return true;
            }
            else if (logDragging)
            {
                // 드래그 중 - 위치 이동
                logOverlayWidget->setCursor(Qt::ClosedHandCursor);
                QPoint delta = mouseEvent->globalPosition().toPoint() - logDragStartPos;
                logOverlayWidget->move(logOverlayWidget->pos() + delta);
                logDragStartPos = mouseEvent->globalPosition().toPoint();
                return true;
            }
            else
            {
                // 커서 모양 업데이트
                QPoint pos = mouseEvent->pos();
                int edgeMargin = 8;

                if (pos.y() <= edgeMargin)
                {
                    // 상단 경계 - 리사이즈 커서
                    logOverlayWidget->setCursor(Qt::SizeVerCursor);
                }
                else
                {
                    // 내부 - 이동 커서
                    logOverlayWidget->setCursor(Qt::SizeAllCursor);
                }
            }
        }
        else if (event->type() == QEvent::MouseButtonPress && mouseEvent)
        {
            if (mouseEvent->button() == Qt::LeftButton)
            {
                QPoint pos = mouseEvent->pos();
                int edgeMargin = 8;

                if (pos.y() <= edgeMargin)
                {
                    // 상단 경계 클릭 - 리사이즈 시작
                    logResizing = true;
                    logResizeStartPos = mouseEvent->globalPosition().toPoint();
                    logResizeStartHeight = logOverlayWidget->height();
                    return true;
                }
                else
                {
                    // 내부 클릭 - 드래그 시작
                    logDragging = true;
                    logDragStartPos = mouseEvent->globalPosition().toPoint();
                    logOverlayWidget->setCursor(Qt::ClosedHandCursor);
                    return true;
                }
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease && mouseEvent)
        {
            if (mouseEvent->button() == Qt::LeftButton)
            {
                // 드래그/리사이즈가 끝났으면 ConfigManager에 저장
                if (logDragging || logResizing) {
                    QRect geometry = logOverlayWidget->geometry();
                    ConfigManager::instance()->setLogPanelGeometry(geometry);
                }
                
                logDragging = false;
                logResizing = false;

                // 현재 마우스 위치에 따라 커서 재설정
                QPoint pos = mouseEvent->pos();
                int edgeMargin = 8;

                if (pos.y() <= edgeMargin)
                {
                    logOverlayWidget->setCursor(Qt::SizeVerCursor);
                }
                else
                {
                    logOverlayWidget->setCursor(Qt::SizeAllCursor);
                }
                return true;
            }
        }
        else if (event->type() == QEvent::Leave)
        {
            if (!logDragging && !logResizing)
            {
                logOverlayWidget->setCursor(Qt::ArrowCursor);
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void TeachingWidget::switchToCamera(const QString &cameraUuid)
{
    // 인자로 받은 UUID가 현재 카메라와 같은지 확인
    if (cameraIndex >= 0 && cameraIndex < cameraInfos.size() &&
        cameraUuid == cameraInfos[cameraIndex].uniqueId)
    {
        return;
    }

    // **RUN 버튼 상태 확인 - 검사 모드인지 체크**
    bool wasInInspectionMode = false;
    if (runStopButton && runStopButton->isChecked())
    {
        wasInInspectionMode = true;

        // **일단 라이브 모드로 전환**
        resumeToLiveMode();
    }

    // **검사 결과 및 UI 상태 정리**
    if (cameraView)
    {
        cameraView->setInspectionMode(false);
        cameraView->clearCurrentRect();
    }

    // **프로퍼티 패널 정리**
    if (propertyStackWidget)
    {
        propertyStackWidget->setCurrentIndex(0); // 빈 패널로 초기화
    }

    // **패턴 트리 선택 해제**
    if (patternTree)
    {
        patternTree->clearSelection();
    }

    // UUID로 카메라 인덱스 찾기
    int newCameraIndex = -1;
    int cameraCount = getCameraInfosCount();
    for (int i = 0; i < cameraCount; i++)
    {
        CameraInfo info = getCameraInfo(i);
        if (info.uniqueId == cameraUuid)
        {
            newCameraIndex = i;
            break;
        }
    }

    if (newCameraIndex < 0)
    {
        return;
    }

    // 새로운 메인 카메라 인덱스로 업데이트
    cameraIndex = newCameraIndex;

    // CameraView에 현재 카메라 UUID 설정
    if (cameraView)
    {
        cameraView->setCurrentCameraUuid(cameraUuid);
    }

    // 미리보기 오버레이는 updatePreviewFrames에서 자동 업데이트됨

    // **즉시 미리보기 업데이트**
    updatePreviewFrames();

    // **UI 갱신**
    updatePatternTree();

    // **화면 강제 갱신**
    if (cameraView)
    {
        // CAM OFF/ON 모두 현재 카메라 이미지 표시
        int frameIndex = getFrameIndex(cameraIndex);
        
        if (frameIndex >= 0 && frameIndex < static_cast<int>(4) &&
            !cameraFrames[frameIndex].empty())
        {
            cv::Mat currentFrame = cameraFrames[frameIndex];

            // OpenCV Mat을 QImage로 변환
            QImage qImage;
            if (currentFrame.channels() == 3)
            {
                cv::Mat rgbImage;
                cv::cvtColor(currentFrame, rgbImage, cv::COLOR_BGR2RGB);
                qImage = QImage(rgbImage.data, rgbImage.cols, rgbImage.rows, rgbImage.step, QImage::Format_RGB888).copy();
            }
            else
            {
                qImage = QImage(currentFrame.data, currentFrame.cols, currentFrame.rows, currentFrame.step, QImage::Format_Grayscale8).copy();
            }

            if (!qImage.isNull())
            {
                QPixmap pixmap = QPixmap::fromImage(qImage);
                cameraView->setBackgroundPixmap(pixmap);
            }
        }
        cameraView->update();
    }

    // **이벤트 처리**
    QApplication::processEvents();

    // **검사 모드였다면 다시 검사 모드로 전환**
    if (wasInInspectionMode)
    {
        // 잠깐 대기 후 검사 모드 재개 (UI 업데이트 시간 확보)
        QTimer::singleShot(200, this, [this]()
                           {
            if (runStopButton && !runStopButton->isChecked()) {
                
                // RUN 버튼을 다시 체크된 상태로 만들기
                runStopButton->blockSignals(true);
                runStopButton->setChecked(true);
                runStopButton->blockSignals(false);
                
                // RUN 버튼 이벤트 수동 트리거
                runStopButton->clicked(true);
            } });
    }
}

QTreeWidgetItem *TeachingWidget::createPatternTreeItem(const PatternInfo &pattern)
{
    QTreeWidgetItem *item = new QTreeWidgetItem();

    // 패턴 이름 - 카메라 UUID와 같으면 타입별 기본 이름 사용
    QString name = pattern.name;

    // 패턴 이름이 카메라 UUID 형태이거나 비어있으면 타입별 기본 이름 사용
    if (name.isEmpty() || name.startsWith("CV_") || name.contains("_0_0_"))
    {
        QString typePrefix;
        switch (pattern.type)
        {
        case PatternType::ROI:
            typePrefix = "ROI";
            break;
        case PatternType::FID:
            typePrefix = "FID";
            break;
        case PatternType::INS:
            typePrefix = "INS";
            break;
        case PatternType::FIL:
            typePrefix = "FIL";
            break;
        }
        name = QString("%1_%2").arg(typePrefix).arg(pattern.id.toString().left(8));
    }

    item->setText(0, name);

    // 패턴 타입
    QString typeText;
    switch (pattern.type)
    {
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
        typeText = TR("FIL");
        break;
    }
    item->setText(1, typeText);

    // FIL 패턴의 경우 첫 번째 필터 정보를 함께 표시
    QString statusText = pattern.enabled ? TR("ACTIVE") : TR("INACTIVE");
    if (pattern.type == PatternType::FIL && !pattern.filters.isEmpty())
    {
        const FilterInfo &firstFilter = pattern.filters[0];
        QString filterName = getFilterTypeName(firstFilter.type);
        QString paramSummary = getFilterParamSummary(firstFilter);
        statusText = QString("%1 %2").arg(filterName).arg(paramSummary);
        qDebug() << "[createPatternTreeItem FIL] 필터:" << filterName << "요약:" << paramSummary << "최종:" << statusText;
    }
    else if (pattern.type == PatternType::FIL)
    {
        qDebug() << "[createPatternTreeItem FIL] 필터 없음 - isEmpty:" << pattern.filters.isEmpty();
    }

    // 활성화 상태
    item->setText(2, statusText);

    // 패턴 ID 저장
    item->setData(0, Qt::UserRole, pattern.id.toString());

    // 활성화 체크박스 설정
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(0, pattern.enabled ? Qt::Checked : Qt::Unchecked);

    return item;
}

bool TeachingWidget::selectItemById(QTreeWidgetItem *item, const QUuid &id)
{
    if (!item)
        return false;

    // 현재 아이템의 ID 확인
    QString idStr = item->data(0, Qt::UserRole).toString();
    QUuid itemId = QUuid(idStr);

    if (itemId == id)
    {
        patternTree->setCurrentItem(item);
        patternTree->scrollToItem(item);
        item->setSelected(true);
        return true;
    }

    // 재귀적으로 자식 아이템 확인
    for (int i = 0; i < item->childCount(); i++)
    {
        if (selectItemById(item->child(i), id))
        {
            return true;
        }
    }

    return false;
}

QTreeWidgetItem *TeachingWidget::findItemById(QTreeWidgetItem *parent, const QUuid &id)
{
    if (!parent)
        return nullptr;

    // 현재 아이템과 ID 비교
    if (getPatternIdFromItem(parent) == id)
    {
        return parent;
    }

    // 모든 자식에서 찾기
    for (int i = 0; i < parent->childCount(); i++)
    {
        QTreeWidgetItem *found = findItemById(parent->child(i), id);
        if (found)
            return found;
    }

    return nullptr;
}

// 패턴 이름 가져오기 (ID로)
void TeachingWidget::updateCameraDetailInfo(CameraInfo &info)
{
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

    for (const QString &line : lines)
    {
        if (line.contains("Camera:") || line.contains("Cameras:") || line.contains("FaceTime"))
        {
            inCameraSection = true;
            cameraCount = -1; // 카메라 카운트 초기화
            continue;
        }

        // 카메라 섹션 내에서만 처리
        if (inCameraSection)
        {
            // 들여쓰기 레벨로 섹션 구분 (들여쓰기 없으면 새 섹션)
            if (!line.startsWith(" ") && !line.isEmpty())
            {
                inCameraSection = false;
                continue;
            }

            if (line.trimmed().startsWith("Camera"))
            {
                cameraCount++;
                // 현재 카메라 인덱스와 일치하는지 확인
                if (cameraCount == info.index)
                {
                    cameraName = line.trimmed();
                    if (cameraName.contains(":"))
                    {
                        cameraName = cameraName.section(':', 1).trimmed();
                    }
                    info.name = cameraName;
                }
            }

            // 현재 카메라에 대한 정보만 처리
            if (cameraCount == info.index)
            {
                if (line.contains("Unique ID:"))
                {
                    info.serialNumber = line.section(':', 1).trimmed();
                }

                if (line.contains("Product ID:"))
                {
                    info.productId = line.section(':', 1).trimmed();
                }

                if (line.contains("Vendor ID:"))
                {
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

    while (!xml.atEnd())
    {
        xml.readNext();

        if (xml.isStartElement())
        {
            if (xml.name() == "array")
            {
                QString arrayKey = xml.attributes().value("key").toString();
                if (arrayKey == "_items")
                {
                    inCameraArray = true;
                }
            }
            else if (inCameraArray && xml.name() == "dict")
            {
                cameraIndex++;
            }
            else if (inCameraArray && cameraIndex == info.index)
            {
                QString key = xml.attributes().value("key").toString();

                // 다음 텍스트 요소 읽기
                if (key == "_name" || key == "spcamera_unique-id" ||
                    key == "spcamera_model-id" || key == "spcamera_device-path")
                {
                    xml.readNext();
                    if (xml.isCharacters())
                    {
                        QString value = xml.text().toString();
                        if (key == "_name")
                        {
                            info.name = value;
                        }
                        else if (key == "spcamera_unique-id")
                        {
                            info.serialNumber = value;
                        }
                        else if (key == "spcamera_model-id")
                        {
                            info.productId = value;
                        }
                        else if (key == "spcamera_device-path")
                        {
                            info.locationId = value;
                        }
                    }
                }
            }
        }
        else if (xml.isEndElement())
        {
            if (inCameraArray && xml.name() == "array")
            {
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

    for (const QString &line : ioregLines)
    {
        // 새 USB 장치 시작
        if (line.contains("+-o"))
        {
            // 이전 장치가 카메라와 일치했다면 정보 저장
            if (foundMatchingDevice)
            {
                if (!currentName.isEmpty())
                    info.name = currentName;
                if (!currentVID.isEmpty())
                    info.vendorId = currentVID;
                if (!currentPID.isEmpty())
                    info.productId = currentPID;
                if (!currentSerial.isEmpty())
                    info.serialNumber = currentSerial;
                if (!currentLocation.isEmpty())
                    info.locationId = currentLocation;

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

        if (inUSBDevice)
        {
            // 장치 클래스가 카메라/비디오 관련인지 확인
            if (line.contains("bDeviceClass") && (line.contains("0e") || line.contains("0E") || line.contains("14")))
            {
                foundMatchingDevice = true;
            }

            // 장치 이름이 "FaceTime" 또는 "Camera"를 포함하는지 확인
            if (line.contains("USB Product Name") &&
                (line.contains("FaceTime", Qt::CaseInsensitive) ||
                 line.contains("Camera", Qt::CaseInsensitive) ||
                 line.contains("CAM", Qt::CaseInsensitive)))
            {
                foundMatchingDevice = true;
                currentName = line.section('"', 1, 1); // 따옴표 사이의 텍스트 추출
            }

            // 장치가 카메라 인터페이스 클래스를 가지는지 확인
            if (line.contains("bInterfaceClass") && (line.contains("0e") || line.contains("0E") || line.contains("14")))
            {
                foundMatchingDevice = true;
            }

            // 장치 정보 수집
            if (line.contains("idVendor"))
            {
                currentVID = line.section('=', 1).trimmed();
                currentVID = currentVID.section(' ', 0, 0); // 첫 번째 단어만 추출
            }

            if (line.contains("idProduct"))
            {
                currentPID = line.section('=', 1).trimmed();
                currentPID = currentPID.section(' ', 0, 0); // 첫 번째 단어만 추출
            }

            if (line.contains("USB Serial Number"))
            {
                currentSerial = line.section('"', 1, 1); // 따옴표 사이의 텍스트 추출
            }

            if (line.contains("locationID"))
            {
                currentLocation = line.section('=', 1).trimmed();
                currentLocation = currentLocation.section(' ', 0, 0); // 첫 번째 단어만 추출
            }
        }
    }

    // 4. 최후의 방법: 카메라 인덱스를 기반으로 생성된 고유 ID
    // (다른 방법으로 찾지 못한 경우 적어도 카메라 식별은 가능하게)
    if (info.serialNumber.isEmpty() && info.locationId.isEmpty())
    {
        // OpenCV의 카메라 프레임에서 직접 카메라 정보 추출 시도
        // OpenCV capture 제거됨 (Spinnaker SDK만 사용)
        if (false)
        {
            double deviceId = 0;
            double apiId = 0;
            double backend = 0;

            QString generatedId = QString("CV_%1_%2_%3_%4")
                                      .arg(info.index)
                                      .arg(deviceId)
                                      .arg(apiId)
                                      .arg(backend);

            info.serialNumber = generatedId;                         // 시리얼 번호로 사용
            info.locationId = QString("USB_CAM_%1").arg(info.index); // 위치 ID로 사용
        }
        else
        {
            // 카메라가 열려있지 않거나 접근할 수 없는 경우 - 인덱스만 사용
            info.serialNumber = QString("CAM_S%1").arg(info.index);
            info.locationId = QString("CAM_L%1").arg(info.index);
        }
    }

    // 최소한의 고유 식별자 보장
    if (info.uniqueId.isEmpty())
    {
        if (!info.serialNumber.isEmpty())
        {
            info.uniqueId = info.serialNumber;
        }
        else if (!info.locationId.isEmpty())
        {
            info.uniqueId = info.locationId;
        }
        else if (!info.vendorId.isEmpty() && !info.productId.isEmpty())
        {
            info.uniqueId = QString("VID_%1_PID_%2").arg(info.vendorId).arg(info.productId);
        }
        else
        {
            // 최후의 방법: 랜덤 문자와 함께 인덱스 사용
            const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            QString randStr;
            for (int i = 0; i < 6; i++)
            {
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
    QMap<QString, QString> deviceNameMap; // 디바이스 ID -> 이름 매핑
    QMap<QString, QString> devicePnpMap;  // 디바이스 ID -> PNP ID 매핑

    // CSV 출력에서 라인 읽기 (Node,Caption,Description,DeviceID,PNPDeviceID 형식)
    for (int i = 1; i < deviceLines.size(); i++)
    { // 첫 라인은 헤더이므로 건너뜀
        QString line = deviceLines[i].trimmed();
        if (line.isEmpty())
            continue;

        QStringList parts = line.split(",");
        if (parts.size() >= 5)
        {
            QString nodeName = parts[0];
            QString caption = parts[1];
            QString description = parts[2];
            QString deviceId = parts[3];
            QString pnpId = parts[4];

            // 카메라/웹캠 관련 디바이스 필터링
            if (caption.contains("camera", Qt::CaseInsensitive) ||
                caption.contains("webcam", Qt::CaseInsensitive) ||
                description.contains("camera", Qt::CaseInsensitive) ||
                description.contains("webcam", Qt::CaseInsensitive))
            {

                deviceNameMap[deviceId] = caption;
                devicePnpMap[deviceId] = pnpId;
            }
        }
    }

    // 3. 디바이스 세부 정보 추출
    if (info.index < deviceNameMap.size())
    {
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

        if (vidMatch.hasMatch())
        {
            info.vendorId = vidMatch.captured(1);
        }

        if (pidMatch.hasMatch())
        {
            info.productId = pidMatch.captured(1);
        }

        // 시리얼 번호 추출 (사용 가능한 경우)
        QRegularExpression serialRegex("\\\\\([^\\\\]+\\)$");
        QRegularExpressionMatch serialMatch = serialRegex.match(pnpId);
        if (serialMatch.hasMatch())
        {
            info.serialNumber = serialMatch.captured(1);
        }

        // 직접적인 디바이스 경로 저장
        info.locationId = deviceId;

        // 고유 ID 설정 (VID+PID+일부 디바이스 ID)
        if (!info.vendorId.isEmpty() && !info.productId.isEmpty())
        {
            info.uniqueId = QString("VID_%1_PID_%2").arg(info.vendorId).arg(info.productId);

            // 시리얼 번호가 있으면 추가
            if (!info.serialNumber.isEmpty())
            {
                info.uniqueId += "_" + info.serialNumber;
            }
            else
            {
                // 시리얼 번호가 없으면 디바이스 ID 일부를 추가
                info.uniqueId += "_" + deviceId.right(8).remove("{").remove("}").remove("-");
            }
        }
        else
        {
            // VID/PID를 추출할 수 없는 경우 인덱스 기반 ID 사용
            info.uniqueId = QString("WIN_CAM_%1").arg(info.index);
        }
    }
    else
    {
        // 인덱스에 해당하는 카메라가 없으면 기본값 사용
        info.name = QString("카메라 %1").arg(info.index + 1);
        info.uniqueId = QString("WIN_CAM_%1").arg(info.index);
    }

    // OpenCV capture 제거됨 (Spinnaker SDK만 사용)

    // 최소 고유 ID 보장
    if (info.uniqueId.isEmpty())
    {
        // 고유 ID 생성
        const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        QString randStr;
        for (int i = 0; i < 6; i++)
        {
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

    for (const QString &line : v4lLines)
    {
        if (line.isEmpty())
            continue;

        // 탭으로 시작하지 않는 줄은 카메라 이름
        if (!line.startsWith("\t"))
        {
            currentName = line.trimmed();
            if (currentName.endsWith(":"))
            {
                currentName = currentName.left(currentName.length() - 1).trimmed();
            }
        }
        // 탭으로 시작하는 줄은 장치 경로
        else if (!currentName.isEmpty())
        {
            QString devicePath = line.trimmed();
            if (devicePath.startsWith("/dev/video"))
            {
                cameraDevices.append(qMakePair(currentName, devicePath));
            }
        }
    }

    // 3. 인덱스에 해당하는 장치 정보 추출
    if (info.index < cameraDevices.size())
    {
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

        for (const QString &line : udevLines)
        {
            if (line.contains("idVendor"))
            {
                QRegularExpression re("idVendor==\"?([0-9a-fA-F]{4})\"?");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch())
                    idVendor = match.captured(1);
            }
            else if (line.contains("idProduct"))
            {
                QRegularExpression re("idProduct==\"?([0-9a-fA-F]{4})\"?");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch())
                    idProduct = match.captured(1);
            }
            else if (line.contains("serial"))
            {
                QRegularExpression re("serial==\"?([^\"]+)\"?");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch())
                    serial = match.captured(1);
            }
        }

        // 추출한 정보 저장
        info.vendorId = idVendor;
        info.productId = idProduct;
        info.serialNumber = serial;

        // 고유 ID 설정
        if (!idVendor.isEmpty() && !idProduct.isEmpty())
        {
            info.uniqueId = QString("VID_%1_PID_%2").arg(idVendor).arg(idProduct);

            // 시리얼 번호가 있으면 추가
            if (!serial.isEmpty())
            {
                info.uniqueId += "_" + serial;
            }
            else
            {
                // 장치 경로에서 번호만 추출
                QRegularExpression numRe("/dev/video(\\d+)");
                QRegularExpressionMatch numMatch = numRe.match(devicePath);
                if (numMatch.hasMatch())
                {
                    info.uniqueId += "_DEV" + numMatch.captured(1);
                }
            }
        }
        else
        {
            // VID/PID를 추출할 수 없는 경우 장치 경로 기반 ID 사용
            QRegularExpression numRe("/dev/video(\\d+)");
            QRegularExpressionMatch numMatch = numRe.match(devicePath);
            if (numMatch.hasMatch())
            {
                info.uniqueId = QString("LNX_VIDEO%1").arg(numMatch.captured(1));
            }
            else
            {
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

        if (driverMatch.hasMatch())
        {
        }

        if (busMatch.hasMatch())
        {
        }
    }
    else
    {
        // 인덱스에 해당하는 카메라가 없으면 기본값 사용
        info.name = QString("카메라 %1").arg(info.index + 1);
        info.uniqueId = QString("LNX_CAM_%1").arg(info.index);
    }

    // OpenCV capture 제거됨 (Spinnaker SDK만 사용)

    // 5. 최소 고유 ID 보장
    if (info.uniqueId.isEmpty())
    {
        const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        QString randStr;
        for (int i = 0; i < 6; i++)
        {
            randStr += chars.at(QRandomGenerator::global()->bounded(chars.length()));
        }
        info.uniqueId = QString("LNX_CAM_%1_%2").arg(info.index).arg(randStr);
    }

#endif
}

// 카메라 ID 및 이름 읽기 함수
QString TeachingWidget::getCameraName(int index)
{
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
    if (root.contains("SPCameraDataType"))
    {
        QJsonArray cameras = root["SPCameraDataType"].toArray();

        // 연결된 카메라 수 확인
        if (index < cameras.size())
        {
            QJsonObject camera = cameras[index].toObject();
            QString deviceName;
            QString deviceID;

            if (camera.contains("_name"))
            {
                deviceName = camera["_name"].toString();
            }

            // 2. 이제 USB 정보에서 해당 카메라의 장치 ID 찾기
            if (root.contains("SPUSBDataType"))
            {
                QJsonArray usbDevices = root["SPUSBDataType"].toArray();

                // 모든 USB 장치 순회
                for (const QJsonValue &usbDeviceValue : usbDevices)
                {
                    QJsonObject usbDevice = usbDeviceValue.toObject();

                    // USB 장치에 연결된 모든 항목 검색
                    if (usbDevice.contains("_items"))
                    {
                        QJsonArray items = usbDevice["_items"].toArray();

                        for (const QJsonValue &itemValue : items)
                        {
                            QJsonObject item = itemValue.toObject();

                            // 장치 이름이 카메라 이름과 일치하는지 확인
                            if (item.contains("_name") && item["_name"].toString() == deviceName)
                            {
                                // 장치 ID 찾기
                                if (item.contains("location_id"))
                                {
                                    deviceID = item["location_id"].toString();
                                }
                                else if (item.contains("serial_num"))
                                {
                                    deviceID = item["serial_num"].toString();
                                }
                                else if (item.contains("vendor_id"))
                                {
                                    deviceID = QString("VID_%1_PID_%2")
                                                   .arg(item["vendor_id"].toString())
                                                   .arg(item.contains("product_id") ? item["product_id"].toString() : "UNKNOWN");
                                }

                                if (!deviceID.isEmpty())
                                {
                                    return QString("%1 [%2]").arg(deviceName).arg(deviceID);
                                }
                            }
                        }
                    }
                }
            }

            // ID를 찾지 못했지만 이름은 있는 경우
            if (!deviceName.isEmpty())
            {
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

    for (const QString &line : lines)
    {
        if (line.trimmed().isEmpty() || line.startsWith("Node"))
            continue;

        QStringList parts = line.split(",");
        if (parts.size() >= 3)
        {
            QString deviceName = parts[2].trimmed();
            QString deviceId = parts[3].trimmed();

            // 웹캠/카메라 관련 키워드 포함 여부 확인
            if (deviceName.contains("webcam", Qt::CaseInsensitive) ||
                deviceName.contains("camera", Qt::CaseInsensitive) ||
                deviceName.contains("cam", Qt::CaseInsensitive))
            {
                cameraDevices.append(qMakePair(deviceName, deviceId));
            }
        }
    }

    // index에 해당하는 카메라 반환
    if (index < cameraDevices.size())
    {
        QString deviceId = cameraDevices[index].second;
        QString deviceName = cameraDevices[index].first;

        // 고유 ID 추출 (USB\VID_xxxx&PID_yyyy&MI_zz 형식에서)
        QString vid, pid;
        QRegularExpression reVid("VID_([0-9A-F]{4})", QRegularExpression::CaseInsensitiveOption);
        QRegularExpression rePid("PID_([0-9A-F]{4})", QRegularExpression::CaseInsensitiveOption);

        QRegularExpressionMatch vidMatch = reVid.match(deviceId);
        QRegularExpressionMatch pidMatch = rePid.match(deviceId);

        if (vidMatch.hasMatch())
            vid = vidMatch.captured(1);
        if (pidMatch.hasMatch())
            pid = pidMatch.captured(1);

        if (!vid.isEmpty() && !pid.isEmpty())
        {
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

    for (const QString &line : linesV4L)
    {
        if (line.isEmpty())
            continue;

        // 탭으로 시작하지 않으면 카메라 이름
        if (!line.startsWith("\t"))
        {
            currentName = line.trimmed();
            // 끝의 괄호와 콜론 제거
            if (currentName.endsWith(":"))
            {
                currentName = currentName.left(currentName.length() - 1);
            }
        }
        // 탭으로 시작하면 디바이스 경로
        else if (!currentName.isEmpty())
        {
            QString devicePath = line.trimmed();
            if (devicePath.startsWith("/dev/video"))
            {
                cameraDevices.append(qMakePair(currentName, devicePath));
            }
        }
    }

    // index에 해당하는 카메라 반환
    if (index < cameraDevices.size())
    {
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

        for (const QString &line : linesUSB)
        {
            if (line.contains("idVendor"))
            {
                QRegularExpression re("idVendor==\"?([0-9a-fA-F]{4})\"?");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch())
                    idVendor = match.captured(1);
            }
            else if (line.contains("idProduct"))
            {
                QRegularExpression re("idProduct==\"?([0-9a-fA-F]{4})\"?");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch())
                    idProduct = match.captured(1);
            }
            else if (line.contains("serial"))
            {
                QRegularExpression re("serial==\"?([^\"]+)\"?");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch())
                    serial = match.captured(1);
            }
        }

        // 고유 ID 정보 추가
        if (!idVendor.isEmpty() && !idProduct.isEmpty())
        {
            if (!serial.isEmpty())
            {
                return QString("%1 [%2]").arg(deviceName).arg(serial);
            }
            else
            {
                return QString("%1 [VID_%2_PID_%3]").arg(deviceName).arg(idVendor).arg(idProduct);
            }
        }

        return QString("%1 [%2]").arg(deviceName).arg(devicePath);
    }
#endif

    return cameraName;
}

bool TeachingWidget::runInspect(const cv::Mat &frame, int specificCameraIndex, bool updateMainView, int frameIndexForResult)
{
    if (frame.empty())
    {

        return false;
    }
    
    // frameIndexForResult이 지정되지 않으면 currentDisplayFrameIndex 사용
    int resultFrameIndex = (frameIndexForResult >= 0) ? frameIndexForResult : currentDisplayFrameIndex;

    //   // 로그 제거

    if (!cameraView || !insProcessor)
    {
        return false;
    }

    // ★★★ 검사 직전: 현재 선택된 패턴의 UI 프로퍼티 값들을 패턴에 강제 반영 ★★★
    QTreeWidgetItem *selectedItem = patternTree->currentItem();
    if (selectedItem)
    {
        QUuid patternId = getPatternIdFromItem(selectedItem);
        if (!patternId.isNull())
        {
            PatternInfo *pattern = cameraView->getPatternById(patternId);
            if (pattern && pattern->type == PatternType::INS && pattern->inspectionMethod == InspectionMethod::STRIP)
            {
                // UI 값을 패턴에 강제 반영
                if (insStripLengthMinEdit)
                {
                    bool ok;
                    double value = insStripLengthMinEdit->text().toDouble(&ok);
                    if (ok)
                    {
                        pattern->stripLengthMin = value;
                    }
                }
                if (insStripLengthMaxEdit)
                {
                    bool ok;
                    double value = insStripLengthMaxEdit->text().toDouble(&ok);
                    if (ok)
                    {
                        pattern->stripLengthMax = value;
                    }
                }
            }
        }
    }

    QList<PatternInfo> allPatterns = cameraView->getPatterns();  // 실시간으로 최신 패턴 가져오기
    QList<PatternInfo> cameraPatterns;

    // 현재 프레임의 패턴만 필터링
    QList<PatternInfo> framePatterns;
    for (const PatternInfo &pattern : allPatterns)
    {
        if (pattern.frameIndex == resultFrameIndex)
        {
            framePatterns.append(pattern);
        }
    }

    // frameIndex로 필터링된 패턴 중 활성화된 패턴만 선택
    for (const PatternInfo &pattern : framePatterns)
    {
        if (pattern.enabled)
        {
            cameraPatterns.append(pattern);
        }
    }

    // **레시피가 없으면 검사 패스**
    if (cameraPatterns.empty())
    {

        return true; // 검사 성공으로 처리 (패턴 없으므로 패스)
    }

    try
    {
        // 카메라 이름 가져오기 (resultFrameIndex 기반)
        int cameraIndexForName = (specificCameraIndex == -1) ? cameraIndex : specificCameraIndex;
        QString cameraName = (cameraIndexForName >= 0 && cameraIndexForName < cameraInfos.size()) ? cameraInfos[cameraIndexForName].serialNumber : "";
        InspectionResult result = insProcessor->performInspection(frame, cameraPatterns, cameraName);

        // **추가**: 검사 결과를 기반으로 패턴들을 FID 중심으로 그룹 회전
        if (!result.angles.isEmpty())
        {

            QList<PatternInfo> updatedPatterns = cameraView->getPatterns();

            // FID 패턴별로 처리
            for (auto it = result.angles.begin(); it != result.angles.end(); ++it)
            {
                QUuid fidId = it.key();
                double detectedAngle = it.value();
                // 해당 FID 패턴 찾기
                PatternInfo *fidPattern = nullptr;
                for (PatternInfo &pattern : updatedPatterns)
                {
                    if (pattern.id == fidId && pattern.type == PatternType::FID)
                    {
                        fidPattern = &pattern;
                        break;
                    }
                }

                if (!fidPattern)
                    continue;

                // FID의 원본 각도 (현재 패턴 기준)
                double originalFidAngle = fidPattern->angle;
                QPointF originalFidCenter = fidPattern->rect.center();

                // FID 매칭된 실제 위치
                QPointF detectedFidCenter = originalFidCenter;
                if (result.locations.contains(fidId))
                {
                    cv::Point loc = result.locations[fidId];
                    detectedFidCenter = QPointF(loc.x, loc.y);
                }

                // 각도 차이 계산 (검출 각도 - 원본 각도)
                double angleDiff = detectedAngle - originalFidAngle;

                // FID 패턴 업데이트 (위치와 각도)
                fidPattern->rect.moveCenter(detectedFidCenter);
                fidPattern->angle = detectedAngle;

                // 같은 그룹의 INS 패턴들을 FID 중심으로 회전 이동
                for (PatternInfo &pattern : updatedPatterns)
                {
                    if (pattern.type == PatternType::INS &&
                        pattern.parentId == fidId)
                    {

                        // INS의 원본 각도 (현재 패턴 기준)
                        double originalInsAngle = pattern.angle;

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

                        // INS 패턴 업데이트: 원본 각도 + FID 회전 차이
                        pattern.rect.moveCenter(newInsCenter);
                        pattern.angle = originalInsAngle + angleDiff;
                    }
                }
            }

            // 업데이트된 패턴들을 CameraView에 적용
            cameraView->getPatterns() = updatedPatterns;
        }

        // 검사 결과를 CameraView에 전달
        // 메인 스레드에서 호출해야 함 (QTimer 관련 문제 방지)
        if (updateMainView) {
            cameraView->updateInspectionResult(result.isPassed, result, resultFrameIndex);
        } else {
            // 비동기 호출 시에는 메인 스레드로 전달 (lambda capture 사용)
            QMetaObject::invokeMethod(this, [this, result, frameIdx = resultFrameIndex]() {
                if (cameraView) {
                    cameraView->updateInspectionResult(result.isPassed, result, frameIdx);
                }
            }, Qt::QueuedConnection);
        }

        // 배경은 원본 이미지만 설정 (검사 결과 오버레이 없이)
        QImage originalImage = InsProcessor::matToQImage(frame);
        if (!originalImage.isNull())
        {
            QPixmap pixmap = QPixmap::fromImage(originalImage);
            
            // ★ 검사 결과와 프레임 저장 (resultFrameIndex 사용)
            cameraView->saveCurrentResultForMode(resultFrameIndex, pixmap);
            
            // updateMainView가 true일 때만 메인 카메라뷰 업데이트 (RUN 버튼 모드)
            if (updateMainView)
            {
                cameraView->setBackgroundPixmap(pixmap);
            }

            // UI 업데이트를 메인 스레드로 이동 (QBasicTimer 경고 방지)
            QMetaObject::invokeMethod(this, [this, updateMainView]() {
                // updateMainView가 true일 때만 메인 카메라뷰 업데이트
                if (updateMainView && cameraView)
                {
                    cameraView->update();
                }
                // 미리보기 4개 모두 업데이트 (검사 결과 표시)
                updatePreviewFrames();
            }, Qt::QueuedConnection);
        }

        // **검사 완료 후 결과에 따라 이미지 저장 (카메라별 폴더 구분)**
        saveImageAsync(frame, result.isPassed, cameraIndex);

        // 메모리 정리: 검사 결과의 큰 이미지들 명시적 해제
        for (auto it = result.insProcessedImages.begin(); it != result.insProcessedImages.end(); ++it) {
            it.value().release();
        }
        result.insProcessedImages.clear();

        return result.isPassed;
    }
    catch (...)
    {
        return false;
    }
}

// 라이브 모드로 복귀하는 헬퍼 함수 - 버튼 상태 고려
void TeachingWidget::resumeToLiveMode()
{
    // **UI 스레드에서 실행되는지 확인**
    if (QThread::currentThread() != QApplication::instance()->thread())
    {
        // UI 스레드로 호출 예약 (람다 사용)
        QMetaObject::invokeMethod(this, [this]() {
            resumeToLiveMode();
        }, Qt::QueuedConnection);
        return;
    }

    // **중복 호출 방지: 이미 라이브 모드라면 리턴**
    static bool isResuming = false;
    if (isResuming)
    {
        return;
    }
    isResuming = true;

    try
    {
        // **1. RUN/STOP 버튼 상태 확인 및 강제로 STOP 상태로 만들기**
        if (runStopButton && runStopButton->isChecked())
        {
            // 버튼이 RUN 상태(검사 중)라면 STOP 상태로 변경
            runStopButton->blockSignals(true);
            runStopButton->setChecked(false);
            runStopButton->setText("RUN");
            runStopButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));
            runStopButton->blockSignals(false);
        }

        // **2. 카메라 모드 복원 (원래 camOff 상태 유지)**
        // camOff 상태는 검사 모드와 독립적으로 유지되어야 함
        // 검사 종료 시 원래 카메라 모드(camOn/camOff)로 복원

        // **2.5. camOn 상태에서만 카메라 시작**
        if (!camOff && startCameraButton && !startCameraButton->isChecked())
        {
            startCamera();
        }

        // **3. 검사 모드 해제**
        if (cameraView)
        {
            cameraView->setInspectionMode(false);
        }

        // **4. 패턴들을 원래 티칭 상태로 복원**
        if (!originalPatternBackup.isEmpty() && cameraView)
        {
            QList<PatternInfo> currentPatterns = cameraView->getPatterns();

            for (int i = 0; i < currentPatterns.size(); ++i)
            {
                QUuid patternId = currentPatterns[i].id;
                if (originalPatternBackup.contains(patternId))
                {
                    // 검사 중 변경된 각도와 위치를 원본으로 복원
                    PatternInfo &currentPattern = currentPatterns[i];
                    const PatternInfo &originalPattern = originalPatternBackup[patternId];

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
        if (uiUpdateThread)
        {
            if (uiUpdateThread->isRunning())
            {
                uiUpdateThread->setPaused(false);
            }
            else if (uiUpdateThread->isFinished())
            {
                uiUpdateThread->start(QThread::NormalPriority);
            }
        }

        // **cameraFrames는 유지 - camOff 모드에서는 티칭 이미지 보존, camOn 모드에서는 라이브 영상 유지**
        // 프레임을 비우지 않음

        // **6. UI 이벤트 처리**
        QApplication::processEvents();

        // **7. 강제로 화면 갱신 및 카메라 프레임 업데이트**
        if (cameraView)
        {
            cameraView->update();
            // 라이브 모드로 전환 시 카메라 프레임 강제 업데이트
            updateCameraFrame();
        }
    }
    catch (const std::exception &e)
    {
        // 최소한의 복구
        if (cameraView)
        {
            cameraView->setInspectionMode(false);
        }

        // 버튼 상태도 복구
        if (runStopButton && runStopButton->isChecked())
        {
            runStopButton->blockSignals(true);
            runStopButton->setChecked(false);
            runStopButton->setText("RUN");
            runStopButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));
            runStopButton->blockSignals(false);
        }
    }
    catch (...)
    {
        // 최소한의 복구
        if (cameraView)
        {
            cameraView->setInspectionMode(false);
        }

        // 버튼 상태도 복구
        if (runStopButton && runStopButton->isChecked())
        {
            runStopButton->blockSignals(true);
            runStopButton->setChecked(false);
            runStopButton->setText("RUN");
            runStopButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));
            runStopButton->blockSignals(false);
        }
    }

    // **플래그 해제**
    isResuming = false;
}

void TeachingWidget::switchToTestMode()
{
    // 로그 메시지 추가
    if (logTextEdit && logOverlayWidget)
    {
        receiveLogMessage("검사 모드로 전환되었습니다.");
        // TEACH ON 상태일 때만 로그창 자동 표시
        if (teachingEnabled)
        {
            logOverlayWidget->show();
            logOverlayWidget->raise();
        }
        // 로그창 위치는 사용자가 지정한 위치 유지 (강제 이동 제거)
    }

    cameraView->setInspectionMode(true);

    // 카메라가 열려있는지 확인
    cv::Mat testFrame;
    bool gotFrame = false;

#ifdef USE_SPINNAKER
    // Spinnaker 카메라 확인
    if (m_useSpinnaker && cameraIndex >= 0 && cameraIndex < cameraInfos.size() &&
        cameraInfos[cameraIndex].uniqueId.startsWith("SPINNAKER_"))
    {

        if (!m_spinCameras.empty() && cameraIndex < static_cast<int>(m_spinCameras.size()))
        {
            // Spinnaker 카메라에서 프레임 가져오기
            testFrame = grabFrameFromSpinnakerCamera(m_spinCameras[cameraIndex]);
            if (!testFrame.empty())
            {
                gotFrame = true;
                // Spinnaker는 RGB로 들어오므로 BGR로 변환
                cv::cvtColor(testFrame, testFrame, cv::COLOR_RGB2BGR);
            }
        }
    }
#endif
    // OpenCV 카메라 제거됨 (Spinnaker SDK만 사용)

    // 프레임을 가져왔거나 기존 프레임이 있는 경우 사용
    if (gotFrame)
    {
        // **벡터에 저장**
        cameraFrames[cameraIndex] = testFrame.clone();

        cv::Mat displayFrame;
        cv::cvtColor(cameraFrames[cameraIndex], displayFrame, cv::COLOR_BGR2RGB);

        QImage image(displayFrame.data, displayFrame.cols, displayFrame.rows,
                     displayFrame.step, QImage::Format_RGB888);
        QPixmap pixmap = QPixmap::fromImage(image);
        cameraView->setBackgroundPixmap(pixmap);
    }
    else if (cameraIndex >= 0 && cameraIndex < static_cast<int>(4) &&
             !cameraFrames[cameraIndex].empty())
    {
        // 기존 프레임 사용
        cv::Mat displayFrame;
        cv::cvtColor(cameraFrames[cameraIndex], displayFrame, cv::COLOR_BGR2RGB);

        QImage image(displayFrame.data, displayFrame.cols, displayFrame.rows,
                     displayFrame.step, QImage::Format_RGB888);
        QPixmap pixmap = QPixmap::fromImage(image);
        cameraView->setBackgroundPixmap(pixmap);
    }
}

void TeachingWidget::switchToRecipeMode()
{
    // 로그 메시지 추가
    if (logTextEdit)
    {
        receiveLogMessage("레시피 모드로 전환되었습니다.");
    }

    cameraView->setInspectionMode(false);

    if (uiUpdateThread && uiUpdateThread->isRunning())
    {
        uiUpdateThread->setPaused(false);
    }

    // --- 실시간 필터 적용: 카메라뷰에 필터 적용된 이미지를 표시 ---
    if (cameraIndex >= 0 && cameraIndex < static_cast<int>(4) &&
        !cameraFrames[cameraIndex].empty())
    {

        cv::Mat filteredFrame = cameraFrames[cameraIndex].clone();
        cameraView->applyFiltersToImage(filteredFrame);
        cv::Mat rgbFrame;
        cv::cvtColor(filteredFrame, rgbFrame, cv::COLOR_BGR2RGB);
        QImage image(rgbFrame.data, rgbFrame.cols, rgbFrame.rows, rgbFrame.step, QImage::Format_RGB888);
        QPixmap pixmap = QPixmap::fromImage(image);
        cameraView->setBackgroundPixmap(pixmap);
    }
}

void TeachingWidget::updateAllPatternTemplateImages()
{
    if (!cameraView)
    {
        return;
    }

    // 현재 이미지 가져오기 (패턴이 그려지기 전의 원본 이미지)
    cv::Mat currentImage;
    if (camOff)
    {
        if (cameraIndex < 0 || cameraIndex >= static_cast<int>(4) ||
            cameraFrames[cameraIndex].empty())
        {
            return;
        }
        currentImage = cameraFrames[cameraIndex].clone();
    }
    else
    {
        // CameraView의 backgroundPixmap에서 원본 이미지 가져오기
        if (cameraView)
        {
            QPixmap bgPixmap = cameraView->getBackgroundPixmap();
            if (!bgPixmap.isNull())
            {
                QImage qimg = bgPixmap.toImage().convertToFormat(QImage::Format_RGB888);
                cv::Mat tempMat(qimg.height(), qimg.width(), CV_8UC3, (void *)qimg.constBits(), qimg.bytesPerLine());
                cv::cvtColor(tempMat, currentImage, cv::COLOR_RGB2BGR);
            }
            else
            {
                currentImage = getCurrentFrame();
            }
        }
        else
        {
            currentImage = getCurrentFrame();
        }
        if (currentImage.empty())
        {
            return;
        }
    }

    // 모든 패턴 가져오기
    QList<PatternInfo> patterns = cameraView->getPatterns();
    for (int i = 0; i < patterns.size(); i++)
    {
        PatternInfo pattern = patterns[i];
        // FID와 INS 패턴만 템플릿 이미지가 필요함
        if (pattern.type == PatternType::FID || pattern.type == PatternType::INS)
        {
            // 필터 변경 시에는 템플릿을 다시 생성해야 하므로 스킵하지 않음

            // 패턴 포인터 가져오기
            PatternInfo *patternPtr = cameraView->getPatternById(pattern.id);
            if (!patternPtr)
            {
                continue;
            }

            // rect가 유효한지 확인
            if (patternPtr->rect.width() <= 0 || patternPtr->rect.height() <= 0)
            {
                continue;
            }

            try
            {
                // FID 패턴은 updateFidTemplateImage 사용
                if (pattern.type == PatternType::FID)
                {
                    updateFidTemplateImage(patternPtr, patternPtr->rect);
                }
                // INS 패턴은 updateInsTemplateImage 사용
                else if (pattern.type == PatternType::INS)
                {
                    updateInsTemplateImage(patternPtr, patternPtr->rect);
                }
            }
            catch (const std::exception &e)
            {
            }
            catch (...)
            {
            }
        }
    }

    cameraView->update(); // 화면 갱신

    // 필터 설정 중이 아닐 때만 프로퍼티 패널의 템플릿 이미지도 업데이트
    if (!isFilterAdjusting)
    {
        QTreeWidgetItem *currentItem = patternTree->currentItem();
        if (currentItem)
        {
            QUuid selectedPatternId = getPatternIdFromItem(currentItem);
            PatternInfo *selectedPattern = cameraView->getPatternById(selectedPatternId);
            if (selectedPattern && (selectedPattern->type == PatternType::FID || selectedPattern->type == PatternType::INS))
            {
                updatePropertyPanel(selectedPattern, nullptr, selectedPatternId, -1);
            }
        }
    }
}

int TeachingWidget::getFrameIndex(int cameraIndex) const
{
    return cameraIndex;
}

void TeachingWidget::setNextFrameIndex(int cameraNumber, int frameIndex)
{
    // TrainDialog가 열려있으면 검사 요청 무시
    if (activeTrainDialog && activeTrainDialog->isVisible()) {
        qDebug() << QString("[시리얼/서버] TrainDialog 활성화 중 - 검사 요청 무시 (카메라%1, 프레임%2)")
            .arg(cameraNumber).arg(frameIndex);
        return;
    }
    
    if (cameraNumber >= 0 && cameraNumber < 2 && frameIndex >= 0 && frameIndex < 4) {
        int oldValue = nextFrameIndex[cameraNumber].load();
        nextFrameIndex[cameraNumber].store(frameIndex);
        
        qDebug() << QString("[시리얼 명령] 카메라%1의 nextFrameIndex = %2 설정됨")
            .arg(cameraNumber).arg(frameIndex);
    }
}

void TeachingWidget::saveRecipe()
{

    // 현재 레시피 이름이 있으면 개별 파일로 저장, 없으면 사용자에게 물어봄
    if (currentRecipeName.isEmpty())
    {

        // 사용자에게 새 레시피 생성 여부 묻기
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Question);
        msgBox.setTitle("새 레시피 생성");
        msgBox.setMessage("현재 열린 레시피가 없습니다.\n새로운 레시피를 생성하시겠습니까?");
        msgBox.setButtons(QMessageBox::Yes | QMessageBox::No);

        if (msgBox.exec() == QMessageBox::Yes)
        {
            // 자동으로 타임스탬프 이름 생성
            QDateTime now = QDateTime::currentDateTime();
            currentRecipeName = now.toString("yyyyMMdd_HHmmss_zzz");
        }
        else
        {

            return; // 저장 취소
        }
    }

    // cameraInfos 검증
    if (cameraInfos.isEmpty())
    {

        CustomMessageBox(this, CustomMessageBox::Critical, "레시피 저장 실패",
                         "카메라 정보가 없습니다. 먼저 이미지를 추가하거나 카메라를 연결하세요.")
            .exec();
        return;
    }

    // ★ CAM ON 상태일 때: 모든 실제 카메라 정보 유지 (2대의 카메라 모두 저장)
    if (!camOff && cameraView)
    {
        qDebug() << "[saveRecipe] CAM ON - 연결된 카메라" << cameraInfos.size() << "대의 정보 유지";
        
        // 각 카메라의 정보 출력
        for (int i = 0; i < cameraInfos.size(); i++)
        {
            qDebug() << QString("[saveRecipe] 카메라 %1: %2 (UUID: %3)")
                        .arg(i)
                        .arg(cameraInfos[i].name)
                        .arg(cameraInfos[i].uniqueId);
        }
        
        // 패턴들의 cameraUuid는 이미 올바르게 설정되어 있으므로 변경하지 않음
        qDebug() << "[saveRecipe] ✓ 총" << cameraView->getPatterns().size() << "개 패턴 저장";
    }

    for (int i = 0; i < cameraInfos.size(); i++)
    {
    }

    // 현재 편집 모드 저장 (저장 후 복원하기 위해)
    CameraView::EditMode currentMode = cameraView->getEditMode();
    bool currentModeToggleState = modeToggleButton->isChecked();

    // 개별 레시피 파일로 저장
    RecipeManager manager;

    // 레시피 파일 경로 생성
    QString recipeFileName = QDir(manager.getRecipesDirectory()).absoluteFilePath(QString("%1/%1.xml").arg(currentRecipeName));

    // 빈 시뮬레이션 이미지 패스와 빈 캘리브레이션 맵 (필요시 나중에 추가)
    QStringList simulationImagePaths;
    QMap<QString, CalibrationInfo> calibrationMap;

    // ★ cameraFrames에 이미지가 있는 모든 프레임을 카메라로 추가
    QVector<CameraInfo> saveCameraInfos = cameraInfos;
    
    // 프레임 인덱스 = 카메라 인덱스 (1:1 매핑)
    for (int frameIdx = 0; frameIdx < static_cast<int>(cameraFrames.size()); frameIdx++)
    {
        if (!cameraFrames[frameIdx].empty())
        {
            // 이 프레임(카메라)이 cameraInfos에 있는지 확인
            bool cameraExists = false;
            for (const auto& cam : saveCameraInfos)
            {
                if (cam.index == frameIdx)
                {
                    cameraExists = true;
                    break;
                }
            }
            
            // 없으면 가상 카메라 정보 추가
            if (!cameraExists)
            {
                CameraInfo virtualCam;
                virtualCam.index = frameIdx;
                virtualCam.name = QString("Camera_%1").arg(frameIdx);
                virtualCam.uniqueId = QString("CAM_%1_UUID").arg(frameIdx);
                virtualCam.serialNumber = QString("SERIAL_%1").arg(frameIdx);
                virtualCam.locationId = QString("VIRTUAL_%1").arg(frameIdx);
                virtualCam.imageIndex = frameIdx;
                virtualCam.isConnected = false;
                
                saveCameraInfos.append(virtualCam);
                qDebug() << QString("[saveRecipe] ✓ 프레임 %1에 이미지가 있어서 카메라 %1 자동 추가")
                            .arg(frameIdx);
            }
        }
    }
    
    // 저장 전 cameraInfos 크기 확인
    qDebug() << "[saveRecipe] 저장할 cameraInfos 개수:" << saveCameraInfos.size();
    for (int i = 0; i < saveCameraInfos.size(); i++)
    {
        qDebug() << QString("[saveRecipe]   카메라 %1: %2 (UUID: %3, index: %4)")
                    .arg(i)
                    .arg(saveCameraInfos[i].name)
                    .arg(saveCameraInfos[i].uniqueId)
                    .arg(saveCameraInfos[i].index);
    }

    // 기존 saveRecipe 함수 사용 (TeachingWidget 포인터 전달)
    if (manager.saveRecipe(recipeFileName, saveCameraInfos, cameraIndex, calibrationMap, cameraView, simulationImagePaths, -1, QStringList(), this))
    {

        hasUnsavedChanges = false;

        // 최근 사용한 레시피를 ConfigManager에 저장
        ConfigManager::instance()->setLastRecipePath(currentRecipeName);
        ConfigManager::instance()->saveConfig();

        // 윈도우 타이틀 업데이트
        setWindowTitle(QString("KM Inspector - %1").arg(currentRecipeName));

        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Information);
        msgBox.setTitle("레시피 저장");
        msgBox.setMessage(QString("'%1' 레시피가 성공적으로 저장되었습니다.").arg(currentRecipeName));
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
    }
    else
    {

        CustomMessageBox msgBoxCritical(this);
        msgBoxCritical.setIcon(CustomMessageBox::Critical);
        msgBoxCritical.setTitle("레시피 저장 실패");
        msgBoxCritical.setMessage(QString("레시피 저장에 실패했습니다:\n%1").arg(manager.getLastError()));
        msgBoxCritical.setButtons(QMessageBox::Ok);
        msgBoxCritical.exec();
    }

    // 저장 전 모드 복원
    cameraView->setEditMode(currentMode);
    modeToggleButton->setChecked(currentModeToggleState);

    // 버튼 텍스트와 스타일도 복원
    if (currentMode == CameraView::EditMode::Draw)
    {
        modeToggleButton->setText("DRAW");
        modeToggleButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_MOVE_COLOR, UIColors::BTN_DRAW_COLOR, true));
    }
    else
    {
        modeToggleButton->setText("MOVE");
        modeToggleButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_DRAW_COLOR, UIColors::BTN_MOVE_COLOR, false));
    }
}

bool TeachingWidget::loadRecipe(const QString &fileName, bool showMessageBox)
{
    if (fileName.isEmpty())
    {
        // 파일명이 없으면 사용 가능한 첫 번째 레시피 로드
        RecipeManager recipeManager;
        QStringList availableRecipes = recipeManager.getAvailableRecipes();
        if (availableRecipes.isEmpty())
        {
            if (showMessageBox)
            {
                CustomMessageBox(this, CustomMessageBox::Warning, tr("Warning"), tr("No recipes available")).exec();
            }
            return false;
        }
        onRecipeSelected(availableRecipes.first());
        return true;
    }

    // 더 이상 직접 파일 로드를 지원하지 않음 - 개별 레시피 시스템만 사용
    if (showMessageBox)
    {
        qWarning() << "직접 파일 로드는 지원되지 않습니다. 레시피 관리 시스템을 사용하세요.";
    }
    return false;
}

bool TeachingWidget::hasLoadedRecipe() const
{
    // 레시피가 로드된 경우 패턴이 하나 이상 있어야 함
    return !cameraView->getPatterns().isEmpty();
}

QVector<CameraInfo> TeachingWidget::getCameraInfos() const
{
    return cameraInfos;
}

CameraInfo TeachingWidget::getCameraInfo(int index) const
{
    if (index >= 0 && index < cameraInfos.size())
    {
        return cameraInfos[index];
    }
    return CameraInfo();
}

bool TeachingWidget::setCameraInfo(int index, const CameraInfo &info)
{
    if (index >= 0 && index < cameraInfos.size())
    {
        cameraInfos[index] = info;
        return true;
    }
    return false;
}

int TeachingWidget::getCameraInfosCount() const
{
    return cameraInfos.size();
}

void TeachingWidget::clearCameraInfos()
{
    cameraInfos.clear();

    // 레시피 초기화 시 타이틀도 초기화
    setWindowTitle("KM Inspector");
}

void TeachingWidget::appendCameraInfo(const CameraInfo &info)
{
    cameraInfos.append(info);
}

void TeachingWidget::removeCameraInfo(int index)
{
    if (index >= 0 && index < cameraInfos.size())
    {
        cameraInfos.removeAt(index);
    }
}

bool TeachingWidget::isValidCameraIndex(int index) const
{
    return (index >= 0 && index < cameraInfos.size());
}

// 연결된 모든 카메라의 UUID 목록 반환
QStringList TeachingWidget::getConnectedCameraUuids() const
{
    QStringList uuids;

    for (const CameraInfo &cameraInfo : cameraInfos)
    {
        if (cameraInfo.isConnected && !cameraInfo.uniqueId.isEmpty())
        {
            uuids.append(cameraInfo.uniqueId);
        }
    }

    return uuids;
}

#ifdef USE_SPINNAKER
// Spinnaker SDK 초기화
bool TeachingWidget::initSpinnakerSDK()
{
    try
    {
        // 기존 인스턴스가 있으면 유효성 검증
        if (m_spinSystem != nullptr)
        {
            try {
                // System 유효성 검증 시도
                Spinnaker::CameraList testList = m_spinSystem->GetCameras();
                testList.Clear();
                qDebug() << "[initSpinnakerSDK] ✓ 기존 Spinnaker System 인스턴스 재사용 (유효성 확인 완료)";
                m_spinCameras.clear();
                return true;
            } catch (Spinnaker::Exception &e) {
                qDebug() << "[initSpinnakerSDK] 기존 System 손상 감지:" << e.what() << "- 강제 재생성";
                // 손상된 인스턴스 정리 시도
                m_spinCameras.clear();
                m_spinCamList.Clear();
                
                // ReleaseInstance 호출하여 완전히 정리
                try {
                    if (m_spinSystem) {
                        m_spinSystem->ReleaseInstance();
                    }
                    m_spinSystem = nullptr;
                    QThread::msleep(500); // 시스템 정리 대기
                } catch (...) {
                    qDebug() << "[initSpinnakerSDK] ReleaseInstance 실패 (무시)";
                    m_spinSystem = nullptr;
                }
            }
        }
        
        // System 인스턴스 생성
        qDebug() << "[initSpinnakerSDK] Spinnaker System 인스턴스 생성...";
        m_spinSystem = Spinnaker::System::GetInstance();
        
        if (!m_spinSystem)
        {
            qDebug() << "[initSpinnakerSDK] Spinnaker System 인스턴스가 nullptr입니다.";
            return false;
        }

        // 라이브러리 버전 출력 - 네임스페이스 추가
        const Spinnaker::LibraryVersion spinnakerLibraryVersion = m_spinSystem->GetLibraryVersion();
        qDebug() << "[initSpinnakerSDK] Spinnaker SDK 초기화 성공 - 버전:" 
                 << spinnakerLibraryVersion.major << "." 
                 << spinnakerLibraryVersion.minor << "." 
                 << spinnakerLibraryVersion.type << "." 
                 << spinnakerLibraryVersion.build;

        return true;
    }
    catch (Spinnaker::Exception &e)
    {
        qDebug() << "[initSpinnakerSDK] Spinnaker 예외:" << e.what() 
                 << "코드:" << e.GetError();
        qDebug() << "[initSpinnakerSDK] Spinnaker 카메라 없이 시뮬레이션 모드로 동작합니다.";
        m_spinSystem = nullptr;
        m_useSpinnaker = false;  // ★★★ Spinnaker 사용 비활성화
        return false;
    }
    catch (std::exception &e)
    {
        qDebug() << "[initSpinnakerSDK] 표준 예외:" << e.what();
        qDebug() << "[initSpinnakerSDK] Spinnaker 카메라 없이 시뮬레이션 모드로 동작합니다.";
        m_spinSystem = nullptr;
        m_useSpinnaker = false;  // ★★★ Spinnaker 사용 비활성화
        return false;
    }
    catch (...)
    {
        qDebug() << "[initSpinnakerSDK] 알 수 없는 예외 발생";
        qDebug() << "[initSpinnakerSDK] Spinnaker 카메라 없이 시뮬레이션 모드로 동작합니다.";
        m_spinSystem = nullptr;
        m_useSpinnaker = false;  // ★★★ Spinnaker 사용 비활성화
        return false;
    }
}

// Spinnaker SDK 해제
void TeachingWidget::releaseSpinnakerSDK()
{
    qDebug() << "[releaseSpinnakerSDK] Spinnaker 리소스 정리 시작...";
    
    try
    {
        // 1. 모든 카메라의 acquisition 중지 및 DeInit (역순으로 처리)
        for (int i = m_spinCameras.size() - 1; i >= 0; i--)
        {
            qDebug() << "[releaseSpinnakerSDK] 카메라" << i << "정리 시작";
            
            try
            {
                // ★★★ 강화된 NULL 포인터 검사
                if (!m_spinCameras[i])
                {
                    qDebug() << "[releaseSpinnakerSDK] 카메라" << i << "포인터가 NULL입니다. 건너뜀";
                    continue;
                }
                
                // ★★★ IsValid() 체크 전에 포인터 유효성 재확인
                bool isValid = false;
                try {
                    isValid = m_spinCameras[i]->IsValid();
                } catch (...) {
                    qDebug() << "[releaseSpinnakerSDK] 카메라" << i << "IsValid() 호출 실패";
                    continue;
                }
                
                if (isValid)
                {
                    // Acquisition 중지 (스트리밍 종료)
                    try
                    {
                        if (m_spinCameras[i]->IsStreaming())
                        {
                            qDebug() << "[releaseSpinnakerSDK] 카메라" << i << "스트리밍 중지 중...";
                            m_spinCameras[i]->EndAcquisition();
                            QThread::msleep(50);  // 스트리밍 완전 종료 대기
                            qDebug() << "[releaseSpinnakerSDK] 카메라" << i << "스트리밍 중지 완료";
                        }
                    }
                    catch (Spinnaker::Exception &e)
                    {
                        qDebug() << "[releaseSpinnakerSDK] 카메라" << i << "EndAcquisition 실패:" << e.what() << "코드:" << e.GetError();
                    }
                    catch (...)
                    {
                        qDebug() << "[releaseSpinnakerSDK] 카메라" << i << "EndAcquisition 중 알 수 없는 예외";
                    }
                    
                    // 충분한 대기 시간
                    QThread::msleep(100);
                    
                    // DeInit
                    try
                    {
                        bool isInitialized = false;
                        try {
                            isInitialized = m_spinCameras[i]->IsInitialized();
                        } catch (...) {
                            qDebug() << "[releaseSpinnakerSDK] 카메라" << i << "IsInitialized() 호출 실패";
                        }
                        
                        if (isInitialized)
                        {
                            qDebug() << "[releaseSpinnakerSDK] 카메라" << i << "DeInit 중...";
                            m_spinCameras[i]->DeInit();
                            QThread::msleep(50);  // DeInit 완료 대기
                            qDebug() << "[releaseSpinnakerSDK] 카메라" << i << "DeInit 완료";
                        }
                    }
                    catch (Spinnaker::Exception &e)
                    {
                        qDebug() << "[releaseSpinnakerSDK] 카메라" << i << "DeInit 실패:" << e.what() << "코드:" << e.GetError();
                    }
                    catch (...)
                    {
                        qDebug() << "[releaseSpinnakerSDK] 카메라" << i << "DeInit 중 알 수 없는 예외";
                    }
                }
            }
            catch (Spinnaker::Exception &e)
            {
                qDebug() << "[releaseSpinnakerSDK] 카메라" << i << "정리 중 예외:" << e.what() << "코드:" << e.GetError();
            }
            catch (...)
            {
                qDebug() << "[releaseSpinnakerSDK] 카메라" << i << "정리 중 알 수 없는 예외";
            }
        }
        
        // 2. 카메라 참조 해제
        qDebug() << "[releaseSpinnakerSDK] 카메라 참조 해제 중...";
        // ★★★ [중요] 명시적으로 각 포인터 nullptr 설정 후 clear
        for (int i = 0; i < (int)m_spinCameras.size(); i++)
        {
            m_spinCameras[i] = nullptr;
        }
        m_spinCameras.clear();
        QThread::msleep(300);  // ★ 카메라 참조 완전 해제 대기 (증가)

        // 3. 카메라 리스트 정리
        try
        {
            if (m_spinCamList.GetSize() > 0)
            {
                qDebug() << "[releaseSpinnakerSDK] 카메라 리스트 정리 중...";
                m_spinCamList.Clear();
                QThread::msleep(300);  // ★ 리스트 정리 대기 (증가)
                qDebug() << "[releaseSpinnakerSDK] 카메라 리스트 정리 완료";
            }
        }
        catch (Spinnaker::Exception &e)
        {
            qDebug() << "[releaseSpinnakerSDK] 카메라 리스트 정리 실패:" << e.what();
        }

        // 4. 시스템 인스턴스 해제 - ★★★ 프로그램 종료 시에만 호출
        // ★★★ [중요] ReleaseInstance는 프로그램 완전 종료 시에만 호출
        // 껐다 켰다 반복 시에는 호출하지 않음 (System은 싱글톤으로 유지)
        qDebug() << "[releaseSpinnakerSDK] System 인스턴스는 유지됨 (ReleaseInstance 생략)";
        // m_spinSystem은 nullptr로 설정하지 않음 - 재사용
        
        qDebug() << "[releaseSpinnakerSDK] 정리 완료";
    }
    catch (Spinnaker::Exception &e)
    {
        qDebug() << "[releaseSpinnakerSDK] 전체 정리 중 예외:" << e.what() << "코드:" << e.GetError();
    }
    catch (...)
    {
        qDebug() << "[releaseSpinnakerSDK] 전체 정리 중 알 수 없는 예외";
    }
}

bool TeachingWidget::connectSpinnakerCamera(int index, CameraInfo &info)
{
    try
    {
        // ★★★ 안전성 체크 추가
        if (!m_spinSystem)
        {
            qDebug() << "[connectSpinnakerCamera] System이 nullptr입니다";
            return false;
        }

        if (index >= static_cast<int>(m_spinCamList.GetSize()))
        {
            qDebug() << "[connectSpinnakerCamera] 인덱스 범위 초과:" << index << "/" << m_spinCamList.GetSize();
            return false;
        }

        // 카메라 선택
        Spinnaker::CameraPtr camera = m_spinCamList.GetByIndex(index);
        if (!camera || !camera.IsValid())
        {
            qDebug() << "[connectSpinnakerCamera] 카메라" << index << "가 nullptr이거나 유효하지 않음";
            return false;
        }

        // 카메라 세부 정보 로깅 추가
        try
        {
            Spinnaker::GenApi::INodeMap &nodeMapTLDevice = camera->GetTLDeviceNodeMap();

            Spinnaker::GenApi::CStringPtr ptrDeviceVendorName = nodeMapTLDevice.GetNode("DeviceVendorName");
            if (Spinnaker::GenApi::IsReadable(ptrDeviceVendorName))
            {
            }

            Spinnaker::GenApi::CStringPtr ptrDeviceModelName = nodeMapTLDevice.GetNode("DeviceModelName");
            if (Spinnaker::GenApi::IsReadable(ptrDeviceModelName))
            {
            }

            Spinnaker::GenApi::CStringPtr ptrDeviceVersion = nodeMapTLDevice.GetNode("DeviceVersion");
            if (Spinnaker::GenApi::IsReadable(ptrDeviceVersion))
            {
            }
        }
        catch (Spinnaker::Exception &e)
        {
        }

        // 카메라가 이미 초기화되었는지 확인
        if (camera->IsInitialized())
        {
            try
            {
                if (camera->IsStreaming())
                {
                    camera->EndAcquisition();
                }
            }
            catch (Spinnaker::Exception &e)
            {
            }

            try
            {
                camera->DeInit();
            }
            catch (Spinnaker::Exception &e)
            {
                return false;
            }
        }

        // 초기화 시도 횟수 추가
        const int maxRetries = 3;
        bool initSuccess = false;

        for (int retry = 0; retry < maxRetries && !initSuccess; retry++)
        {
            try
            {
                camera->Init();
                initSuccess = true;
            }
            catch (Spinnaker::Exception &e)
            {

                if (retry < maxRetries - 1)
                {
                    // 잠시 대기
                    QThread::msleep(500);
                }
                else
                {
                    return false;
                }
            }
        }

        if (!initSuccess)
        {
            return false;
        }

        // 카메라 정보 가져오기
        try
        {
            Spinnaker::GenApi::INodeMap &nodeMapTLDevice = camera->GetTLDeviceNodeMap();

            // 시리얼 번호
            Spinnaker::GenApi::CStringPtr ptrDeviceSerialNumber = nodeMapTLDevice.GetNode("DeviceSerialNumber");
            if (Spinnaker::GenApi::IsReadable(ptrDeviceSerialNumber))
            {
                info.serialNumber = QString::fromStdString(ptrDeviceSerialNumber->GetValue().c_str());
            }

            // 모델 이름
            Spinnaker::GenApi::CStringPtr ptrDeviceModelName = nodeMapTLDevice.GetNode("DeviceModelName");
            if (Spinnaker::GenApi::IsReadable(ptrDeviceModelName))
            {
                info.name = QString::fromStdString(ptrDeviceModelName->GetValue().c_str());
            }

            // 벤더 이름
            Spinnaker::GenApi::CStringPtr ptrDeviceVendorName = nodeMapTLDevice.GetNode("DeviceVendorName");
            if (Spinnaker::GenApi::IsReadable(ptrDeviceVendorName))
            {
                info.vendorId = QString::fromStdString(ptrDeviceVendorName->GetValue().c_str());
            }
        }
        catch (Spinnaker::Exception &e)
        {
            // 정보를 가져오지 못했더라도 계속 진행
        }

        // 고유 ID 생성
        info.uniqueId = "SPINNAKER_" + info.serialNumber;
        if (info.uniqueId.isEmpty())
        {
            info.uniqueId = QString("SPINNAKER_%1").arg(index);
        }

        // 카메라 저장
        m_spinCameras.push_back(camera);

        // 카메라 설정 구성
        try
        {
            // 버퍼 핸들링 모드 설정 (최신 이미지만 유지)
            Spinnaker::GenApi::INodeMap &nodeMap = camera->GetNodeMap();
            Spinnaker::GenApi::CEnumerationPtr ptrBufferHandlingMode = nodeMap.GetNode("StreamBufferHandlingMode");
            if (Spinnaker::GenApi::IsReadable(ptrBufferHandlingMode) &&
                Spinnaker::GenApi::IsWritable(ptrBufferHandlingMode))
            {

                Spinnaker::GenApi::CEnumEntryPtr ptrNewestOnly = ptrBufferHandlingMode->GetEntryByName("NewestOnly");
                if (Spinnaker::GenApi::IsReadable(ptrNewestOnly))
                {
                    ptrBufferHandlingMode->SetIntValue(ptrNewestOnly->GetValue());
                }
            }

            // StreamBufferCountMode 설정
            Spinnaker::GenApi::CEnumerationPtr ptrBufferCountMode = nodeMap.GetNode("StreamBufferCountMode");
            if (Spinnaker::GenApi::IsReadable(ptrBufferCountMode) &&
                Spinnaker::GenApi::IsWritable(ptrBufferCountMode))
            {

                Spinnaker::GenApi::CEnumEntryPtr ptrManual = ptrBufferCountMode->GetEntryByName("Manual");
                if (Spinnaker::GenApi::IsReadable(ptrManual))
                {
                    ptrBufferCountMode->SetIntValue(ptrManual->GetValue());

                    // StreamBufferCount 설정 (작은 값으로)
                    Spinnaker::GenApi::CIntegerPtr ptrBufferCount = nodeMap.GetNode("StreamBufferCount");
                    if (Spinnaker::GenApi::IsReadable(ptrBufferCount) &&
                        Spinnaker::GenApi::IsWritable(ptrBufferCount))
                    {
                        ptrBufferCount->SetValue(3); // 버퍼 크기를 3으로 설정
                    }
                }
            }

            // 트리거 모드 설정 - 사용자 설정을 존중 (자동으로 Off로 변경하지 않음)
            // 참고: 카메라 설정 다이얼로그에서 트리거 모드를 설정할 수 있음

            std::cout << "카메라 연결 완료 - 트리거 모드는 현재 설정 유지" << std::endl;

            // AcquisitionMode 설정 전 트리거 소스 확인
            Spinnaker::GenApi::CEnumerationPtr ptrTriggerSourceCheck = nodeMap.GetNode("TriggerSource");
            if (Spinnaker::GenApi::IsAvailable(ptrTriggerSourceCheck) && Spinnaker::GenApi::IsReadable(ptrTriggerSourceCheck))
            {
                QString triggerSourceBeforeAcq = QString::fromStdString(ptrTriggerSourceCheck->GetCurrentEntry()->GetSymbolic().c_str());
                std::cout << "AcquisitionMode 설정 전 트리거 소스: " << triggerSourceBeforeAcq.toStdString() << std::endl;
            }

            // **중요**: 트리거 모드에서는 AcquisitionMode를 변경하지 않음
            // UserSet에서 설정된 AcquisitionMode를 그대로 유지
            // - 트리거 모드일 때: SingleFrame (각 트리거마다 한 장씩)
            // - 자유 실행 모드일 때: Continuous (연속 촬영)

            // 현재 설정 상태만 확인하고 로그 출력
            Spinnaker::GenApi::CEnumerationPtr ptrTriggerMode = nodeMap.GetNode("TriggerMode");
            Spinnaker::GenApi::CEnumerationPtr ptrAcquisitionMode = nodeMap.GetNode("AcquisitionMode");

            if (Spinnaker::GenApi::IsReadable(ptrTriggerMode) &&
                Spinnaker::GenApi::IsReadable(ptrAcquisitionMode))
            {

                QString triggerModeStr = QString::fromStdString(ptrTriggerMode->GetCurrentEntry()->GetSymbolic().c_str());
                QString acqModeStr = QString::fromStdString(ptrAcquisitionMode->GetCurrentEntry()->GetSymbolic().c_str());

                std::cout << "✓ 현재 설정 유지 - TriggerMode: " << triggerModeStr.toStdString()
                          << ", AcquisitionMode: " << acqModeStr.toStdString() << std::endl;
            }

            // AcquisitionMode 설정 후 트리거 소스 확인
            if (Spinnaker::GenApi::IsAvailable(ptrTriggerSourceCheck) && Spinnaker::GenApi::IsReadable(ptrTriggerSourceCheck))
            {
                QString triggerSourceAfterAcq = QString::fromStdString(ptrTriggerSourceCheck->GetCurrentEntry()->GetSymbolic().c_str());
                std::cout << "AcquisitionMode 설정 후 트리거 소스: " << triggerSourceAfterAcq.toStdString() << std::endl;
            }

            // 노출 설정 (자동) - 주석처리: 사용자 설정 유지를 위해
            // Spinnaker::GenApi::CEnumerationPtr ptrExposureAuto = nodeMap.GetNode("ExposureAuto");
            // if (Spinnaker::GenApi::IsWritable(ptrExposureAuto)) {
            //     try {
            //         Spinnaker::GenApi::CEnumEntryPtr ptrExposureAutoContinuous = ptrExposureAuto->GetEntryByName("Continuous");
            //         if (Spinnaker::GenApi::IsReadable(ptrExposureAutoContinuous)) {
            //             ptrExposureAuto->SetIntValue(ptrExposureAutoContinuous->GetValue());
            //         }
            //     }
            //     catch (Spinnaker::Exception& e) {
            //     }
            // }

            // 프레임 레이트 설정 (가능한 경우)
            try
            {
                Spinnaker::GenApi::CBooleanPtr ptrFrameRateEnable = nodeMap.GetNode("AcquisitionFrameRateEnable");
                if (Spinnaker::GenApi::IsWritable(ptrFrameRateEnable))
                {
                    ptrFrameRateEnable->SetValue(true);

                    Spinnaker::GenApi::CFloatPtr ptrFrameRate = nodeMap.GetNode("AcquisitionFrameRate");
                    if (Spinnaker::GenApi::IsWritable(ptrFrameRate))
                    {
                        // 최대 프레임 레이트 확인
                        double maxFrameRate = ptrFrameRate->GetMax();
                        double targetFrameRate = qMin(maxFrameRate, 30.0); // 30fps 제한

                        ptrFrameRate->SetValue(targetFrameRate);
                    }
                }
            }
            catch (Spinnaker::Exception &e)
            {
            }

            // 화이트 밸런스 및 색상 설정은 UserSet에서 로드된 설정을 그대로 사용
            // (자동으로 변경하지 않음)
        }
        catch (Spinnaker::Exception &e)
        {
            // 설정 오류가 있더라도 계속 진행
        }

        // 획득 시작
        try
        {
            camera->BeginAcquisition();
        }
        catch (Spinnaker::Exception &e)
        {
            return false;
        }

        // 버퍼 클리어 - 오래된 프레임 제거
        try
        {
            // 버퍼에 쌓인 이미지 버리기
            uint64_t bufferedImages = camera->GetNumImagesInUse();
            if (bufferedImages > 0)
            {
                for (uint64_t i = 0; i < bufferedImages; i++)
                {
                    Spinnaker::ImagePtr oldImage = camera->GetNextImage(1);
                    if (oldImage)
                    {
                        oldImage->Release();
                    }
                }
            }
        }
        catch (Spinnaker::Exception &e)
        {
        }

        // 연결 상태 설정
        info.isConnected = true;

        // OpenCV capture 제거됨 (Spinnaker SDK만 사용)

        return true;
    }
    catch (Spinnaker::Exception &e)
    {
        return false;
    }
}

cv::Mat TeachingWidget::grabFrameFromSpinnakerCamera(Spinnaker::CameraPtr &camera)
{
    cv::Mat cvImage;
    try
    {
        // ★ 카메라 유효성 및 초기화 상태 확인 (IsValid() 추가)
        if (!camera || !camera->IsValid() || !camera->IsInitialized())
        {
            return cvImage;
        }

        // 카메라가 스트리밍 중인지 확인
        if (!camera->IsStreaming())
        {
            try
            {
                camera->BeginAcquisition();
            }
            catch (Spinnaker::Exception &e)
            {
                return cvImage;
            }
        }

        // 버퍼 클리어 - 최신 프레임만 획득 (오래된 프레임 모두 버림)
        try
        {
            // 버퍼에 쌓인 모든 이미지를 제거하고 최신 것만 남기기
            Spinnaker::ImagePtr latestImage = nullptr;
            while (true)
            {
                try
                {
                    Spinnaker::ImagePtr tempImage = camera->GetNextImage(1); // 1ms 타임아웃
                    if (!tempImage)
                    {
                        break; // 버퍼 비움
                    }
                    if (latestImage)
                    {
                        latestImage->Release(); // 이전 이미지 버림
                    }
                    latestImage = tempImage; // 현재 이미지를 최신으로 저장
                }
                catch (...)
                {
                    break; // 타임아웃 또는 에러 → 버퍼가 비어있음
                }
            }
            // 버퍼 클리어 후 latestImage 반환
            if (latestImage)
            {
                if (!latestImage->IsIncomplete())
                {
                    // 성공적으로 최신 프레임 획득
                    size_t width = latestImage->GetWidth();
                    size_t height = latestImage->GetHeight();

                    try
                    {
                        Spinnaker::ImageProcessor processor;
                        processor.SetColorProcessing(Spinnaker::SPINNAKER_COLOR_PROCESSING_ALGORITHM_DIRECTIONAL_FILTER);
                        Spinnaker::ImagePtr convertedImage = processor.Convert(latestImage, Spinnaker::PixelFormat_BGR8);

                        if (convertedImage && !convertedImage->IsIncomplete())
                        {
                            unsigned char *buffer = static_cast<unsigned char *>(convertedImage->GetData());
                            cvImage = cv::Mat(height, width, CV_8UC3, buffer).clone();
                            latestImage->Release();
                            return cvImage; // 최신 프레임 반환
                        }
                    }
                    catch (...)
                    {
                        // 변환 실패
                    }
                }
                latestImage->Release();
            }
        }
        catch (...)
        {
            // 버퍼 클리어 실패시 무시하고 계속
        }

        // 이 코드는 버퍼가 비었을 때만 도달
        // 트리거 모드: 트리거 신호 대기 (긴 타임아웃 허용)
        // 라이브 모드: 다음 프레임 도착 대기
        int timeout = 100; // 버퍼가 비었을 때 다음 프레임 대기용 (100ms)

        Spinnaker::ImagePtr spinImage = camera->GetNextImage(timeout);

        // 완전한 이미지인지 확인
        if (!spinImage || spinImage->IsIncomplete())
        {
            if (spinImage)
            {
                spinImage->Release();
            }
            else
            {
                // 트리거 모드에서 타임아웃은 정상 - 트리거 대기 중
                try
                {
                    Spinnaker::GenApi::INodeMap &nodeMap = camera->GetNodeMap();
                    Spinnaker::GenApi::CEnumerationPtr ptrTriggerMode = nodeMap.GetNode("TriggerMode");
                    Spinnaker::GenApi::CEnumerationPtr ptrAcquisitionMode = nodeMap.GetNode("AcquisitionMode");

                    if (Spinnaker::GenApi::IsReadable(ptrTriggerMode))
                    {
                        QString triggerModeStr = QString::fromStdString(ptrTriggerMode->GetCurrentEntry()->GetSymbolic().c_str());
                        if (triggerModeStr == "On")
                        {
                            // 트리거 모드: acquisition 상태 확인 및 재시작
                            if (!camera->IsStreaming())
                            {
                                if (Spinnaker::GenApi::IsReadable(ptrAcquisitionMode))
                                {
                                    QString acqModeStr = QString::fromStdString(ptrAcquisitionMode->GetCurrentEntry()->GetSymbolic().c_str());
                                    if (acqModeStr == "SingleFrame")
                                    {
                                        camera->BeginAcquisition();
                                    }
                                }
                            }
                            // 트리거 모드에서 타임아웃은 정상 상황 - 에러 아님
                            return cvImage;
                        }
                    }
                }
                catch (Spinnaker::Exception &e)
                {
                    // 무시
                }
            }
            return cvImage;
        }

        // 이미지 크기 및 데이터 가져오기
        size_t width = spinImage->GetWidth();
        size_t height = spinImage->GetHeight();

        // 이미지 변환은 픽셀 형식에 따라 다름
        Spinnaker::PixelFormatEnums pixelFormat = spinImage->GetPixelFormat();

        if (pixelFormat == Spinnaker::PixelFormat_Mono8)
        {
            // 흑백 이미지 처리
            unsigned char *buffer = static_cast<unsigned char *>(spinImage->GetData());
            cvImage = cv::Mat(height, width, CV_8UC1, buffer).clone();
        }
        else
        {
            // 컬러 이미지 변환 (BGR8 형식으로 - OpenCV는 BGR 순서 사용)
            try
            {
                // 이미지 처리기를 사용하여 BGR8로 변환
                Spinnaker::ImageProcessor processor;
                processor.SetColorProcessing(Spinnaker::SPINNAKER_COLOR_PROCESSING_ALGORITHM_DIRECTIONAL_FILTER);
                Spinnaker::ImagePtr convertedImage = processor.Convert(spinImage, Spinnaker::PixelFormat_BGR8);

                if (convertedImage && !convertedImage->IsIncomplete())
                {
                    unsigned char *buffer = static_cast<unsigned char *>(convertedImage->GetData());
                    cvImage = cv::Mat(height, width, CV_8UC3, buffer).clone();
                }
                else
                {
                }
            }
            catch (Spinnaker::Exception &e)
            {
            }
        }

        // 이미지 메모리 해제
        spinImage->Release();

        // **핵심**: SingleFrame 모드에서 이미지 획득 후 즉시 다음 acquisition 시작
        try
        {
            Spinnaker::GenApi::INodeMap &nodeMap = camera->GetNodeMap();
            Spinnaker::GenApi::CEnumerationPtr ptrTriggerMode = nodeMap.GetNode("TriggerMode");
            Spinnaker::GenApi::CEnumerationPtr ptrAcquisitionMode = nodeMap.GetNode("AcquisitionMode");

            if (Spinnaker::GenApi::IsReadable(ptrTriggerMode) &&
                Spinnaker::GenApi::IsReadable(ptrAcquisitionMode))
            {

                QString triggerModeStr = QString::fromStdString(ptrTriggerMode->GetCurrentEntry()->GetSymbolic().c_str());
                QString acqModeStr = QString::fromStdString(ptrAcquisitionMode->GetCurrentEntry()->GetSymbolic().c_str());

                // 트리거 + SingleFrame: 이미지 획득 후 즉시 다음 트리거를 위해 acquisition 재시작
                if (triggerModeStr == "On" && acqModeStr == "SingleFrame")
                {
                    // SingleFrame 모드에서는 이미지 획득 후 항상 acquisition이 정지되므로
                    // IsStreaming() 상태와 관계없이 강제로 재시작
                    try
                    {
                        if (camera->IsStreaming())
                        {
                            camera->EndAcquisition(); // 혹시 남아있는 acquisition 종료
                        }
                        camera->BeginAcquisition();
                    }
                    catch (Spinnaker::Exception &e)
                    {
                    }
                }
            }
        }
        catch (Spinnaker::Exception &e)
        {
            // 무시
        }

        // ===== 라이브 영상을 카메라 설정 다이얼로그에 전달 =====


        return cvImage;
    }
    catch (Spinnaker::Exception &e)
    {
        return cvImage;
    }
}
#endif

TeachingWidget::~TeachingWidget()
{
    qDebug() << "[~TeachingWidget] 소멸자 시작";
    
    // 0. CameraView에 포인터 해제 알림 (제일 먼저!)
    if (cameraView) {
        cameraView->setTeachingWidget(nullptr);
    }
    
    // 0-1. 비동기 작업(QtConcurrent::run) 정리
    // 글로벌 스레드 풀의 대기 중인 작업 제거 및 실행 중인 작업 완료 대기
    qDebug() << "[~TeachingWidget] 비동기 작업 정리 시작";
    QThreadPool::globalInstance()->clear();
    QThreadPool::globalInstance()->waitForDone();
    qDebug() << "[~TeachingWidget] 비동기 작업 정리 완료";
    
    // 0-2. cameraFrames 명시적 해제 (OpenCV Mat 소멸자 mutex 문제 방지)
    for (auto& frame : cameraFrames) {
        if (!frame.empty()) {
            frame.release();
        }
    }
    for (auto& frame : cameraFrames) {
        frame.release();
    }
    
    // 1. ClientDialog reconnect 스레드 중지 (Spinnaker SDK 정리 전에 필수)
    qDebug() << "[~TeachingWidget] ClientDialog reconnect 스레드 중지 시작";
    if (ClientDialog::instance()) {
        ClientDialog::instance()->stopReconnectThread();
    }
    qDebug() << "[~TeachingWidget] ClientDialog reconnect 스레드 중지 완료";
    
    // 1. 먼저 모든 스레드 중지 (카메라 사용 중지)
    qDebug() << "[~TeachingWidget] 카메라 스레드 중지 시작";
    for (CameraGrabberThread *thread : cameraThreads)
    {
        if (thread && thread->isRunning())
        {
            thread->stopGrabbing();
            thread->wait(1000);  // 최대 1초 대기
            delete thread;
        }
    }
    cameraThreads.clear();
    qDebug() << "[~TeachingWidget] 카메라 스레드 중지 완료";

    // 2. UI 업데이트 스레드 정리
    if (uiUpdateThread)
    {
        uiUpdateThread->stopUpdating();
        uiUpdateThread->wait(1000);
        delete uiUpdateThread;
        uiUpdateThread = nullptr;
    }
    qDebug() << "[~TeachingWidget] UI 스레드 중지 완료";

    // 3. 카메라 정보 정리 (Spinnaker SDK만 사용)
    qDebug() << "[~TeachingWidget] 카메라 정보 정리 시작";
    cameraInfos.clear();
    qDebug() << "[~TeachingWidget] 카메라 정보 정리 완료";

#ifdef USE_SPINNAKER
    // 5. Spinnaker SDK 정리 (안전하게 해제)
    qDebug() << "[~TeachingWidget] Spinnaker SDK 해제 시작";
    
    if (m_useSpinnaker) {
        releaseSpinnakerSDK();
    } else {
        // Spinnaker 미사용 시에도 참조 정리
        m_spinCameras.clear();
        m_spinSystem = nullptr;
    }
    
    qDebug() << "[~TeachingWidget] Spinnaker SDK 해제 완료";
#endif

    // 6. 타이머 정리 (먼저 정리)
    qDebug() << "[~TeachingWidget] 타이머 정리 시작";
    if (statusUpdateTimer)
    {
        statusUpdateTimer->stop();
        disconnect(statusUpdateTimer, nullptr, this, nullptr);
        delete statusUpdateTimer;
        statusUpdateTimer = nullptr;
    }
    qDebug() << "[~TeachingWidget] 타이머 정리 완료";

    // 7. InsProcessor 연결 해제
    qDebug() << "[~TeachingWidget] InsProcessor 연결 해제 시작";
    if (insProcessor)
    {
        disconnect(insProcessor, nullptr, this, nullptr);
    }
    qDebug() << "[~TeachingWidget] InsProcessor 연결 해제 완료";

    // 8. FilterDialog 정리
    qDebug() << "[~TeachingWidget] FilterDialog 정리 시작";
    if (filterDialog)
    {
        delete filterDialog;
        filterDialog = nullptr;
    }
    qDebug() << "[~TeachingWidget] FilterDialog 정리 완료";

    // 9. TestDialog 정리
    qDebug() << "[~TeachingWidget] TestDialog 정리 시작";
    if (testDialog)
    {
        delete testDialog;
        testDialog = nullptr;
    }
    qDebug() << "[~TeachingWidget] TestDialog 정리 완료";

    // 10. SEG 모델 해제 (YOLO 제거됨)
    qDebug() << "[~TeachingWidget] SEG 모델 제거됨 (YOLO 비활성화)";

    // 11. ANOMALY 모델 해제
    qDebug() << "[~TeachingWidget] ANOMALY 모델 해제 시작";
    if (ImageProcessor::isTensorRTPatchCoreLoaded()) {
        ImageProcessor::releasePatchCoreTensorRT();
    }
    qDebug() << "[~TeachingWidget] ANOMALY 모델 해제 완료";
    
    // 12. OpenCV 전역 리소스 해제 (생략 - mutex 문제 가능성)
    // cv::destroyAllWindows();
    
    qDebug() << "[~TeachingWidget] 소멸자 완료";
    
    // 강제로 표준 출력 플러시 (로그가 확실히 출력되도록)
    fflush(stdout);
    fflush(stderr);
}

QColor TeachingWidget::getNextColor()
{
    // 색상 배열에서 순환하며 색상 선택
    QColor color = patternColors[nextColorIndex];
    nextColorIndex = (nextColorIndex + 1) % patternColors.size();
    return color;
}

void TeachingWidget::addFilter()
{
    QTreeWidgetItem *selectedItem = patternTree->currentItem();
    if (!selectedItem)
    {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Warning);
        msgBox.setTitle("패턴 미선택");
        msgBox.setMessage("필터를 추가할 패턴을 먼저 선택해주세요.");
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    // 필터 아이템이 선택되었을 경우 부모 패턴 아이템으로 변경
    QVariant filterIndexVar = selectedItem->data(0, Qt::UserRole + 1);
    if (filterIndexVar.isValid())
    {
        if (selectedItem->parent())
        {
            selectedItem = selectedItem->parent();
        }
    }

    QString idStr = selectedItem->data(0, Qt::UserRole).toString();
    QUuid patternId = QUuid(idStr);
    if (patternId.isNull())
    {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Warning);
        msgBox.setTitle("패턴 정보 오류");
        msgBox.setMessage("패턴 정보가 유효하지 않습니다.");
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    // 필터 대화상자 설정
    filterDialog->setPatternId(patternId);

    // 필터 대화상자 실행
    filterDialog->exec();

    // 필터 대화상자가 종료되면 트리 아이템 업데이트
    updatePatternTree();
    updateCameraFrame();
    updateAllPatternTemplateImages();
}

void TeachingWidget::addPattern()
{

    // 티칭 모드가 비활성화되어 있으면 패턴 추가 금지
    if (!teachingEnabled)
    {

        return;
    }

    // 시뮬레이션 모드 상태 디버깅 - cameraFrames 체크
    if (cameraIndex >= 0 && cameraIndex < static_cast<int>(4) &&
        !cameraFrames[cameraIndex].empty())
    {
    }

    // 현재 그려진 사각형이 있는지 먼저 확인 (엔터키로 호출된 경우)
    QRect currentRect = cameraView->getCurrentRect();
    bool hasDrawnRect = (!currentRect.isNull() && currentRect.width() >= 10 && currentRect.height() >= 10);

    // 선택된 아이템 확인
    QTreeWidgetItem *selectedItem = patternTree->currentItem();

    // 선택된 아이템이 필터인지 확인 (UserRole + 1에 필터 인덱스가 저장됨)
    QVariant filterIndexVar;
    if (selectedItem)
    {
        filterIndexVar = selectedItem->data(0, Qt::UserRole + 1);

        // 필터 아이템이 선택되었을 경우 부모 패턴 아이템으로 변경
        if (filterIndexVar.isValid())
        {
            if (selectedItem->parent())
            {
                selectedItem = selectedItem->parent();
            }
        }
    }

    // 그려진 사각형이 있으면 무조건 새 패턴 생성 (필터 추가 방지)
    if (hasDrawnRect)
    {

        // 패턴 이름 입력 받기
        CustomMessageBox msgBox(this);
        msgBox.setTitle("패턴 이름");
        msgBox.setMessage("패턴 이름을 입력하세요 (비우면 자동 생성):");
        msgBox.setInputField(true, "");
        msgBox.setButtons(QMessageBox::Ok | QMessageBox::Cancel);

        int result = msgBox.exec();

        if (result != QMessageBox::Ok)
        {

            return; // 취소 버튼 누름
        }

        QString patternName = msgBox.getInputText();

        // 이름이 비었으면 자동 생성
        if (patternName.isEmpty())
        {
            const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
            QString prefix;

            switch (currentPatternType)
            {
            case PatternType::ROI:
                prefix = "R_";
                break;
            case PatternType::FID:
                prefix = "F_";
                break;
            case PatternType::INS:
                prefix = "I_";
                break;
            case PatternType::FIL:
                prefix = "FL_";
                break;
            }

            patternName = prefix;
            for (int i = 0; i < 5; ++i)
            {
                patternName += chars.at(QRandomGenerator::global()->bounded(chars.length()));
            }
        }

        // 패턴 정보 생성
        PatternInfo pattern;
        pattern.rect = currentRect;
        pattern.name = patternName;
        pattern.type = currentPatternType;

        // frameIndex 설정 - 현재 표시된 프레임 인덱스 사용 (0,1,2,3)
        int viewFrameIndex = cameraView ? cameraView->getCurrentFrameIndex() : currentDisplayFrameIndex;
        pattern.frameIndex = viewFrameIndex;
        
        // 카메라 UUID는 cameraInfos에서 프레임 순서대로 가져오기
        if (viewFrameIndex >= 0 && viewFrameIndex < cameraInfos.size())
        {
            pattern.cameraUuid = cameraInfos[viewFrameIndex].uniqueId;
        }
        else
        {
            pattern.cameraUuid = QString("FRAME_%1").arg(viewFrameIndex);
        }
        
        qDebug() << "[TeachingWidget::addPattern] frameIndex:" << pattern.frameIndex;

        // currentCameraUuid가 비어있으면 자동 설정
        if (cameraView && cameraView->getCurrentCameraUuid().isEmpty())
        {
            cameraView->setCurrentCameraUuid(pattern.cameraUuid);
        }

        // 타입별 색상 설정 (UIColors 클래스 사용)
        switch (currentPatternType)
        {
        case PatternType::ROI:
            pattern.color = UIColors::ROI_COLOR;
            break;
        case PatternType::FID:
            pattern.color = UIColors::FIDUCIAL_COLOR;
            break;
        case PatternType::INS:
            pattern.color = UIColors::INSPECTION_COLOR;
            break;
        case PatternType::FIL:
            pattern.color = UIColors::FILTER_COLOR;
            break;
        }

        // 패턴 타입별 기본값 설정
        if (currentPatternType == PatternType::ROI)
        {
            // includeAllCamera 제거됨
        }
        else if (currentPatternType == PatternType::FID)
        {
            pattern.matchThreshold = 75.0;
            pattern.useRotation = false;
            pattern.minAngle = -5.0;
            pattern.maxAngle = 5.0;
            pattern.angleStep = 1.0;
            pattern.fidMatchMethod = 0;
            pattern.runInspection = true;

            // 템플릿 이미지 추출
            cv::Mat sourceImage;
            bool hasSourceImage = false;

            // frameIndex 계산
            int frameIndex = getFrameIndex(cameraIndex);
            
            qDebug() << "[addPattern] 템플릿 추출 - cameraIndex:" << cameraIndex 
                     << "frameIndex:" << frameIndex
                     << "4:" << 4;

            // cameraFrames[frameIndex] 사용
            if (frameIndex >= 0 && frameIndex < static_cast<int>(4) &&
                !cameraFrames[frameIndex].empty())
            {
                sourceImage = cameraFrames[frameIndex].clone();
                hasSourceImage = true;
                qDebug() << "[addPattern] 템플릿 이미지 획득 성공 - 크기:" << sourceImage.cols << "x" << sourceImage.rows;
            }
            else
            {
                qDebug().noquote() << QString("[addPattern] 템플릿 이미지 없음 - frameIndex: %1").arg(frameIndex);
            }

            if (hasSourceImage)
            {
                cv::Rect rect(pattern.rect.x(), pattern.rect.y(),
                              pattern.rect.width(), pattern.rect.height());

                if (rect.x >= 0 && rect.y >= 0 &&
                    rect.x + rect.width <= sourceImage.cols &&
                    rect.y + rect.height <= sourceImage.rows)
                {

                    cv::Mat roi = sourceImage(rect).clone();
                    cv::cvtColor(roi, roi, cv::COLOR_BGR2RGB);
                    QImage img(roi.data, roi.cols, roi.rows, roi.step, QImage::Format_RGB888);
                    pattern.templateImage = img.copy();
                }
            }
        }
        else if (currentPatternType == PatternType::INS)
        {
            pattern.passThreshold = 90.0; // 90%
            pattern.inspectionMethod = 0; // DIFF 방법

            // EDGE 검사 관련 기본값 설정
            pattern.edgeEnabled = true;
            pattern.edgeOffsetX = 90;
            pattern.stripEdgeBoxWidth = 150;
            pattern.stripEdgeBoxHeight = 150;
            pattern.edgeMaxOutliers = 5;
            pattern.edgeDistanceMax = 10;
            pattern.edgeStartPercent = 10;
            pattern.edgeEndPercent = 10;
        }

        // 패턴 추가 및 ID 받기
        QUuid id = cameraView->addPattern(pattern);

        // CameraView에서 추가된 패턴 가져오기
        PatternInfo *addedPattern = cameraView->getPatternById(id);
        if (!addedPattern)
        {
            return;
        }

        // ★★★ framePatternLists에도 패턴 추가 (검사 시 사용) ★★★
        int frameIdx = addedPattern->frameIndex;
        if (frameIdx >= 0 && frameIdx < 4)
        {
            framePatternLists[frameIdx].append(*addedPattern);
            qDebug().noquote() << QString("[addPattern] Frame[%1]에 패턴 추가 - 총 패턴 수: %2")
                        .arg(frameIdx).arg(framePatternLists[frameIdx].size());
        }

        // INS 패턴인 경우 템플릿 이미지를 필터가 적용된 상태로 업데이트
        if (currentPatternType == PatternType::INS)
        {
            updateInsTemplateImage(addedPattern, addedPattern->rect);
        }

        // 트리 아이템 생성
        QTreeWidgetItem *newItem = createPatternTreeItem(*addedPattern);

        // 최상위 항목으로 추가
        patternTree->addTopLevelItem(newItem);

        // 새로 추가한 항목 선택 및 표시
        patternTree->clearSelection();
        newItem->setSelected(true);
        patternTree->scrollToItem(newItem);

        // 임시 사각형 지우기
        cameraView->clearCurrentRect();

        if (addedPattern)
        {
            cameraView->setSelectedPatternId(addedPattern->id);
        }

        return; // 새 패턴 생성 후 함수 종료
    }

    // 그려진 사각형이 없고 선택된 아이템이 있으면 필터 추가
    if (selectedItem)
    {
        QString idStr = selectedItem->data(0, Qt::UserRole).toString();
        QUuid patternId = QUuid(idStr);
        if (patternId.isNull())
        {
            CustomMessageBox msgBox(this);
            msgBox.setIcon(CustomMessageBox::Warning);
            msgBox.setTitle("패턴 정보 오류");
            msgBox.setMessage("패턴 정보가 유효하지 않습니다.");
            msgBox.setButtons(QMessageBox::Ok);
            msgBox.exec();
            return;
        }

        // 필터 대화상자 설정
        filterDialog->setPatternId(patternId);

        // 필터 대화상자 실행
        filterDialog->exec();

        // 필터 대화상자가 종료되면 트리 아이템 업데이트
        updatePatternTree();
        updateCameraFrame();
    }
    else
    {
        // 선택된 아이템도 없고 그려진 사각형도 없으면 안내 메시지
        if (!selectedItem && !hasDrawnRect)
        {
            CustomMessageBox msgBox(this);
            msgBox.setIcon(CustomMessageBox::Warning);
            msgBox.setTitle("패턴 없음");
            msgBox.setMessage("먼저 카메라 화면에 사각형 패턴을 그리거나 패턴을 선택해주세요.");
            msgBox.setButtons(QMessageBox::Ok);
            msgBox.exec();
        }
    }
}

void TeachingWidget::removePattern()
{
    QTreeWidgetItem *selectedItem = patternTree->currentItem();
    if (!selectedItem)
    {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Information);
        msgBox.setTitle("선택 필요");
        msgBox.setMessage("삭제할 항목을 먼저 목록에서 선택하세요.");
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    QVariant filterIndexVar = selectedItem->data(0, Qt::UserRole + 1);

    if (filterIndexVar.isValid())
    {
        // 필터 삭제 로직
        QString idStr = selectedItem->data(0, Qt::UserRole).toString();
        QUuid patternId = QUuid(idStr);
        int filterIndex = filterIndexVar.toInt();

        CustomMessageBox msgBoxQuestion(this);
        msgBoxQuestion.setIcon(CustomMessageBox::Question);
        msgBoxQuestion.setTitle("패턴 삭제");
        msgBoxQuestion.setMessage("선택한 패턴을 삭제하시겠습니까?");
        msgBoxQuestion.setButtons(QMessageBox::Yes | QMessageBox::No);
        QMessageBox::StandardButton reply = static_cast<QMessageBox::StandardButton>(msgBoxQuestion.exec());

        if (reply == QMessageBox::Yes)
        {
            cameraView->removePatternFilter(patternId, filterIndex);

            // 트리 업데이트 - 전체 트리 재구성으로 안전하게 처리
            updatePatternTree();

            // 필터 삭제 후 즉시 카메라 프레임 업데이트
            updateCameraFrame();
            updateAllPatternTemplateImages();

            cameraView->update();
        }
    }
    else
    {
        // 패턴 삭제 로직
        QUuid patternId = getPatternIdFromItem(selectedItem);
        if (!patternId.isNull())
        {
            CustomMessageBox msgBoxQuestion2(this);
            msgBoxQuestion2.setIcon(CustomMessageBox::Question);
            msgBoxQuestion2.setTitle("패턴 삭제");
            msgBoxQuestion2.setMessage("선택한 패턴을 삭제하시겠습니까?");
            msgBoxQuestion2.setButtons(QMessageBox::Yes | QMessageBox::No);
            QMessageBox::StandardButton reply = static_cast<QMessageBox::StandardButton>(msgBoxQuestion2.exec());

            if (reply == QMessageBox::Yes)
            {
                // ANOMALY 패턴이면 가중치 폴더도 삭제
                PatternInfo* pattern = cameraView->getPatternById(patternId);
                if (pattern && pattern->type == PatternType::INS && 
                    pattern->inspectionMethod == InspectionMethod::ANOMALY) {
                    AnomalyWeightUtils::removeWeightFolder(pattern->name);
                }
                
                cameraView->removePattern(patternId);

                // 트리 업데이트 - 전체 트리 재구성으로 안전하게 처리
                updatePatternTree();

                // 프로퍼티 패널 초기화
                if (propertyStackWidget)
                {
                    propertyStackWidget->setCurrentIndex(0);
                }
            }
        }
    }
}

QColor TeachingWidget::getButtonColorForPatternType(PatternType type)
{
    return UIColors::getPatternColor(type);
}

void TeachingWidget::onBackButtonClicked()
{
    // **1. 멀티 카메라 스레드 중지**
    for (CameraGrabberThread *thread : cameraThreads)
    {
        if (thread && thread->isRunning())
        {
            thread->stopGrabbing();
            thread->wait();
            delete thread;
        }
    }
    cameraThreads.clear();

    // **2. UI 업데이트 스레드 중지**
    if (uiUpdateThread)
    {
        uiUpdateThread->stopUpdating();
        uiUpdateThread->wait();
    }

#ifdef USE_SPINNAKER
    // **3. Spinnaker 카메라 정리**
    if (m_useSpinnaker)
    {
        try
        {
            for (auto &camera : m_spinCameras)
            {
                if (camera && camera->IsStreaming())
                {
                    camera->EndAcquisition();
                }
                if (camera && camera->IsInitialized())
                {
                    camera->DeInit();
                }
            }
            m_spinCameras.clear();

            if (m_spinCamList.GetSize() > 0)
            {
                m_spinCamList.Clear();
            }
        }
        catch (Spinnaker::Exception &e)
        {
        }
    }
#endif

    // OpenCV 카메라 제거됨 (Spinnaker SDK만 사용)
    clearCameraInfos(); // 전체 클리어
    cameraIndex = -1;

    // **6. 이전 화면으로 돌아가기**
    emit goBack();
}

void TeachingWidget::updateUIElements()
{
    // 카메라 뷰가 유효한지 확인
    if (!cameraView)
        return;

    // 스케일링 정보 업데이트
    if (cameraIndex >= 0 && cameraIndex < static_cast<int>(4) &&
        !cameraFrames[cameraIndex].empty())
    {

        QSize origSize(cameraFrames[cameraIndex].cols, cameraFrames[cameraIndex].rows);
        QSize viewSize = cameraView->size();

        if (origSize.width() > 0 && origSize.height() > 0 &&
            viewSize.width() > 0 && viewSize.height() > 0)
        {
            double newScaleX = static_cast<double>(viewSize.width()) / origSize.width();
            double newScaleY = static_cast<double>(viewSize.height()) / origSize.height();

            if (cameraView->hasValidScaling())
            {
                // 이전과 스케일이 동일하면 리턴 (오차 범위 고려)
                if (cameraView->isSameScaling(newScaleX, newScaleY))
                {
                    // 지속적인 스케일링 계산이 필요없는 경우 UI만 업데이트
                    cameraView->update();
                }
            }
            else
            {
                cameraView->setScaling(newScaleX, newScaleY);
            }
        }
    }

    // UI 업데이트 - 패턴 및 사각형 그리기
    cameraView->update();
}

InspectionResult TeachingWidget::runSingleInspection(int specificCameraIndex)
{
    InspectionResult result;

    try
    {
        // 검사 수행
        // (LIVE/INSPECT 모드 구분 제거 - 항상 검사 수행)

        // **1. 카메라 인덱스 유효성 검사**
        if (specificCameraIndex < 0 || specificCameraIndex >= getCameraInfosCount())
        {
            return result;
        }

        // **2. 필요시 카메라 전환**
        if (specificCameraIndex != cameraIndex)
        {
            CameraInfo targetCameraInfo = getCameraInfo(specificCameraIndex);
            switchToCamera(targetCameraInfo.uniqueId);
            QApplication::processEvents();
        }

        // **3. 멤버 변수 runStopButton 직접 사용**
        if (!runStopButton)
        {
            return result;
        }

        bool wasInInspectionMode = runStopButton->isChecked();

        // **4. 라이브 모드였다면 RUN 버튼 클릭 (검사 시작)**
        if (!wasInInspectionMode)
        {
            runStopButton->click();
            QApplication::processEvents();
        }

        // **5. 검사 실행**
        cv::Mat inspectionFrame;

        // camOff 모드에서는 항상 cameraFrames[0] 사용, camOn 모드에서는 specificCameraIndex 사용
        int frameIndex = camOff ? 0 : specificCameraIndex;

        if (cameraView && frameIndex >= 0 && frameIndex < static_cast<int>(4) &&
            !cameraFrames[frameIndex].empty())
        {
            inspectionFrame = cameraFrames[frameIndex].clone();
        }

        if (!inspectionFrame.empty() && cameraView)
        {
            // 현재 프레임의 활성 패턴들 가져오기 (frameIndex 기반)
            QList<PatternInfo> cameraPatterns;
            int targetFrameIndex = frameIndex;

            const QList<PatternInfo> &allPatterns = cameraView->getPatterns();

            for (const PatternInfo &pattern : allPatterns)
            {
                if (pattern.enabled && pattern.frameIndex == targetFrameIndex)
                {
                    cameraPatterns.append(pattern);
                }
            }

            if (!cameraPatterns.isEmpty())
            {
                // 직접 검사 수행
                InsProcessor processor;
                QString cameraName = (specificCameraIndex >= 0 && specificCameraIndex < cameraInfos.size()) ? cameraInfos[specificCameraIndex].serialNumber : "";
                result = processor.performInspection(inspectionFrame, cameraPatterns, cameraName);

                // **UI 업데이트 (메인 카메라인 경우 또는 시뮬레이션 모드)**
                if (specificCameraIndex == cameraIndex || camOff)
                {
                    updateMainCameraUI(result, inspectionFrame);
                }
            }
        }

        return result;
    }
    catch (...)
    {
        return result;
    }
}

void TeachingWidget::stopSingleInspection()
{
    try
    {
        // **1. RUN 버튼을 STOP 상태로 변경**
        if (runStopButton && runStopButton->isChecked())
        {
            runStopButton->blockSignals(true);
            runStopButton->setChecked(false);
            runStopButton->setText("RUN");
            runStopButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));
            runStopButton->blockSignals(false);
        }

        // **2. 검사 모드 해제**
        if (cameraView)
        {
            cameraView->setInspectionMode(false);
        }

        // **3. UI 업데이트 스레드 재개**
        if (uiUpdateThread)
        {
            if (uiUpdateThread->isRunning())
            {
                uiUpdateThread->setPaused(false);
            }
            else if (uiUpdateThread->isFinished())
            {
                uiUpdateThread->start(QThread::NormalPriority);
            }
        }

        // **4. UI 이벤트 처리**
        QApplication::processEvents();

        // **5. 화면 갱신**
        if (cameraView)
        {
            cameraView->update();
        }
    }
    catch (...)
    {
        // 예외 발생 시에도 최소한의 정리
        if (runStopButton && runStopButton->isChecked())
        {
            runStopButton->blockSignals(true);
            runStopButton->setChecked(false);
            runStopButton->setText("RUN");
            runStopButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_RUN_OFF_COLOR, UIColors::BTN_RUN_ON_COLOR, false));
            runStopButton->blockSignals(false);
        }

        if (cameraView)
        {
            cameraView->setInspectionMode(false);
        }
    }
}

// **private 헬퍼 함수 추가**
void TeachingWidget::updateMainCameraUI(const InspectionResult &result, const cv::Mat &frameForInspection)
{
    // **RUN 버튼 상태 업데이트**
    if (runStopButton && !runStopButton->isChecked())
    {
        runStopButton->blockSignals(true);
        runStopButton->setChecked(true);
        runStopButton->setText("STOP");
        runStopButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_REMOVE_COLOR, QColor("#FF5722"), true));
        runStopButton->blockSignals(false);
    }

    // **검사 모드 설정**
    if (cameraView)
    {
        cameraView->setInspectionMode(true);
        cameraView->updateInspectionResult(result.isPassed, result, currentDisplayFrameIndex);

        // **원본 이미지를 배경으로 설정**
        QImage originalImage = InsProcessor::matToQImage(frameForInspection);
        if (!originalImage.isNull())
        {
            QPixmap pixmap = QPixmap::fromImage(originalImage);
            cameraView->setBackgroundPixmap(pixmap);
        }

        cameraView->update();
    }
}

void TeachingWidget::onCamModeToggled()
{
    camOff = !camOff;
    
    // CameraView에 TEACH OFF 상태 전달
    if (cameraView)
    {
        cameraView->setTeachOff(camOff);
    }

    if (camOff)
    {
        // camOn -> camOff (라이브 모드 -> 레시피 모드) 전환

        // 카메라 중지
        stopCamera();

        // 라이브 모드 데이터 초기화
        // cameraInfos는 유지 (레시피에서 재사용될 수 있음)

        // 패턴 리스트 초기화
        if (cameraView)
        {
            cameraView->clearPatterns();
            cameraView->clearCurrentRect();
            // camOff 모드에서는 티칭 이미지가 있을 수 있으므로 배경 이미지를 초기화하지 않음
            // cameraView->setBackgroundPixmap(QPixmap()); // 배경 이미지 초기화
        }

        // 패턴 트리 초기화
        if (patternTree)
        {
            patternTree->clear();
        }

        // cameraFrames 초기화 후 현재 레시피가 있으면 티칭 이미지 다시 로드
        QString currentRecipe = getCurrentRecipeName();

        if (!currentRecipe.isEmpty())
        {

            // 레시피를 다시 로드하여 티칭 이미지 가져오기
            onRecipeSelected(currentRecipe);
        }
        else
        {
            // 레시피가 없으면 cameraFrames 초기화
            for (auto& frame : cameraFrames) {
                frame.release();
            }
        }
    }
    else
    {
        // camOff -> camOn (레시피 모드 -> 라이브 모드) 전환

        // 카메라 재연결 시도
        detectCameras();
    }
}

// 시뮬레이션 다이얼로그에서 이미지가 선택되었을 때
void TeachingWidget::onSimulationImageSelected(const cv::Mat &image, const QString &imagePath, const QString &projectName)
{
    if (!image.empty())
    {
        // 현재 시뮬레이션 모드 상태 저장
        bool wasInSimulationMode = camOff;

        // 시뮬레이션 모드 활성화
        camOff = true;
        
        // CameraView에 TEACH OFF 상태 전달
        if (cameraView)
        {
            cameraView->setTeachOff(true);
        }

        // 현재 시뮬레이션 이미지를 cameraFrames에 저장
        if (cameraIndex >= 0)
        {
            // cameraFrames 크기가 충분한지 확인
            cameraFrames[cameraIndex] = image.clone();
        }

        // 시뮬레이션 모드임을 명확히 표시
        if (cameraView)
        {

            // 패턴은 이미 레시피 로드 시에 로딩되었으므로 재로딩하지 않음
            // 단지 시뮬레이션 이미지만 표시

            // OpenCV Mat을 QImage로 변환
            QImage qImage;
            if (image.channels() == 3)
            {
                cv::Mat rgbImage;
                cv::cvtColor(image, rgbImage, cv::COLOR_BGR2RGB);
                qImage = QImage(rgbImage.data, rgbImage.cols, rgbImage.rows, rgbImage.step, QImage::Format_RGB888);
            }
            else
            {
                qImage = QImage(image.data, image.cols, image.rows, image.step, QImage::Format_Grayscale8);
            }

            if (qImage.isNull())
            {

                return;
            }

            // QPixmap으로 변환하여 CameraView에 설정
            QPixmap pixmap = QPixmap::fromImage(qImage);
            if (pixmap.isNull())
            {

                return;
            }

            cameraView->setBackgroundPixmap(pixmap);

            // 마우스 이벤트와 줌/팬 기능 강제 활성화
            cameraView->setEnabled(true);
            cameraView->setMouseTracking(true);
            cameraView->setFocusPolicy(Qt::StrongFocus);

            // 현재 선택된 패턴 버튼에 따라 적절한 Edit 모드 설정
            if (patternButtonGroup && patternButtonGroup->checkedButton())
            {
                cameraView->setEditMode(CameraView::EditMode::Draw);
            }
            else
            {
                cameraView->setEditMode(CameraView::EditMode::Move);
            }

            cameraView->setFocus(); // 포커스 설정
            cameraView->setAttribute(Qt::WA_AcceptTouchEvents, true);

            // 강제 업데이트

            cameraView->update();
            cameraView->repaint();
            cameraView->show(); // 위젯 표시 강제
        }
        else
        {
        }

        // 시뮬레이션 카메라 정보로 UI 업데이트
        updateCameraInfoForSimulation(imagePath);

        // camOff 모드 이미지 설정 완료

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

void TeachingWidget::onSimulationProjectNameChanged(const QString &projectName)
{
    if (camOff && cameraView)
    {

        if (projectName.isEmpty())
        {
            // 빈 프로젝트 이름이면 초기화
            cameraView->setCurrentCameraUuid("");

            // 카메라 뷰 이미지 초기화 (연결 없음 상태로)
            cameraView->setBackgroundPixmap(QPixmap());

            // 패턴들 모두 제거 (CameraView에서 관리)
            cameraView->clearPatterns();

            // UI 초기화
            updatePatternTree();
            cameraView->update();
        }
        else
        {
            // projectName을 그대로 사용 (이미 SIM_ 접두어가 포함되어 있음)
            QString cameraDisplayName = projectName;

            // 현재 카메라 UUID도 동일한 이름으로 설정 (패턴 추가 시 일치하도록)
            cameraView->setCurrentCameraUuid(cameraDisplayName);

            // UI 업데이트
            cameraView->update();
        }
    }
}

void TeachingWidget::onSimulationProjectSelected(const QString &projectName)
{
    if (!camOff || !cameraView)
    {
        return;
    }

    // 현재 레시피 이름 설정 (Save 버튼으로 저장할 때 사용)
    currentRecipeName = projectName;
    hasUnsavedChanges = false;

    // 레시피에서 해당 프로젝트의 패턴들 로드 (일반 레시피 로드 방식 사용)
    onRecipeSelected(projectName);

    // 패턴 트리 업데이트 (카메라 UUID는 selectCameraTeachingImage에서 설정됨)
    updatePatternTree();
    cameraView->update();

    // AI 모델 관련 코드 제거됨 (AITrainer 사용 중단)
}

QString TeachingWidget::getCurrentRecipeName() const
{
    // 더 신뢰성 있는 레시피 이름 소스 순서:
    // 1) 백업된 레시피 데이터 (backupRecipeData)
    // 2) cameraInfos[0].name
    if (backupRecipeData.contains("recipeName"))
    {
        QString rn = backupRecipeData.value("recipeName").toString();
        if (!rn.isEmpty())
        {

            return rn;
        }
    }

    // 마지막으로 cameraInfos[0].name 사용
    if (!cameraInfos.isEmpty())
    {

        return cameraInfos[0].name;
    }

    return QString(); // 빈 문자열 반환
}

void TeachingWidget::updateCameraInfoForSimulation(const QString &imagePath)
{
    // 시뮬레이션 모드에서 카메라 정보를 업데이트
    QFileInfo fileInfo(imagePath);

    // 임시로 카메라 정보를 시뮬레이션용으로 변경
    if (!cameraInfos.isEmpty())
    {
        cameraInfos[0].name = QString("SIM_CAM (%1)").arg(fileInfo.fileName());
        cameraInfos[0].index = -1; // 시뮬레이션 표시
    }
}

void TeachingWidget::updateCameraInfoForDisconnected()
{
    if (cameraView)
    {
        cameraView->setCurrentCameraUuid("");
    }
}

void TeachingWidget::enablePatternEditingFeatures()
{
    // 패턴 편집 관련 모든 버튼들을 활성화

    // ROI, FID, INS 버튼들
    if (roiButton)
        roiButton->setEnabled(true);
    if (fidButton)
        fidButton->setEnabled(true);
    if (insButton)
        insButton->setEnabled(true);

    // Draw/Move 토글 버튼 활성화
    if (modeToggleButton)
        modeToggleButton->setEnabled(true);

    // RUN 버튼 활성화 (시뮬레이션 모드에서도 테스트 가능)
    if (runStopButton)
        runStopButton->setEnabled(true);

    // 패턴 관리 버튼들 (objectName으로 찾기)
    QPushButton *saveBtn = findChild<QPushButton *>("saveRecipeButton");
    if (saveBtn)
        saveBtn->setEnabled(true);

    QPushButton *addBtn = findChild<QPushButton *>("addPatternButton");
    if (addBtn)
        addBtn->setEnabled(true);

    QPushButton *filterBtn = findChild<QPushButton *>("addFilterButton");
    if (filterBtn)
        filterBtn->setEnabled(true);

    QPushButton *removeBtn = findChild<QPushButton *>("removeButton");
    if (removeBtn)
        removeBtn->setEnabled(true);

    // 시뮬레이션 모드에서는 모든 메뉴도 활성화

    if (languageSettingsAction)
        languageSettingsAction->setEnabled(true);

    // CameraView 활성화 및 패턴 그리기 모드 설정
    if (cameraView)
    {
        cameraView->setEnabled(true);
        cameraView->setMouseTracking(true);
        cameraView->setFocusPolicy(Qt::StrongFocus);
        cameraView->setAttribute(Qt::WA_AcceptTouchEvents, true);

        // 현재 선택된 패턴 버튼에 따라 Edit 모드 설정 (단, 현재 Move 모드가 아닐 때만)
        if (modeToggleButton && modeToggleButton->isChecked())
        {
            if (roiButton && roiButton->isChecked())
            {
                cameraView->setEditMode(CameraView::EditMode::Draw);
            }
            else if (fidButton && fidButton->isChecked())
            {
                cameraView->setEditMode(CameraView::EditMode::Draw);
            }
            else if (insButton && insButton->isChecked())
            {
                cameraView->setEditMode(CameraView::EditMode::Draw);
            }
        }

        cameraView->update();
    }

    // 패턴 트리와 관련 위젯들
    if (patternTree)
    {
        patternTree->setEnabled(true);
    }

    // 프로퍼티 패널 활성화
    if (propertyStackWidget)
    {
        propertyStackWidget->setEnabled(true);
    }

    if (filterPropertyContainer)
    {
        filterPropertyContainer->setEnabled(true);
    }

    // 프로퍼티 패널 내 모든 위젯들 활성화
    QList<QSpinBox *> spinBoxes = findChildren<QSpinBox *>();
    for (QSpinBox *spinBox : spinBoxes)
    {
        spinBox->setEnabled(true);
    }

    QList<QDoubleSpinBox *> doubleSpinBoxes = findChildren<QDoubleSpinBox *>();
    for (QDoubleSpinBox *doubleSpinBox : doubleSpinBoxes)
    {
        doubleSpinBox->setEnabled(true);
    }

    QList<QCheckBox *> checkBoxes = findChildren<QCheckBox *>();
    for (QCheckBox *checkBox : checkBoxes)
    {
        checkBox->setEnabled(true);
    }

    QList<QComboBox *> comboBoxes = findChildren<QComboBox *>();
    for (QComboBox *comboBox : comboBoxes)
    {
        comboBox->setEnabled(true);
    }

    // 필터 관련 위젯들도 활성화
    enableFilterWidgets();
}

void TeachingWidget::enableFilterWidgets()
{
    // 필터 관련 위젯들을 활성화
    // 이는 시뮬레이션 모드에서 필터 기능을 사용할 수 있게 함
}

void TeachingWidget::onPatternTreeDropCompleted()
{

    // 현재 트리 구조를 분석하여 부모-자식 관계 변화 감지
    QMap<QUuid, QUuid> newParentRelations; // 자식ID -> 부모ID

    // 최상위 아이템들 확인
    for (int i = 0; i < patternTree->topLevelItemCount(); i++)
    {
        QTreeWidgetItem *topItem = patternTree->topLevelItem(i);
        QString topIdStr = topItem->data(0, Qt::UserRole).toString();
        QUuid topId = QUuid(topIdStr);

        // 자식 아이템들 확인
        for (int j = 0; j < topItem->childCount(); j++)
        {
            QTreeWidgetItem *childItem = topItem->child(j);
            QString childIdStr = childItem->data(0, Qt::UserRole).toString();
            QUuid childId = QUuid(childIdStr);

            // 필터가 아닌 패턴인 경우만 처리
            if (!childItem->data(0, Qt::UserRole + 1).isValid())
            {
                newParentRelations[childId] = topId;
            }
        }
    }

    // 실제 패턴 데이터에 부모-자식 관계 적용
    bool hasChanges = false;
    for (auto it = newParentRelations.begin(); it != newParentRelations.end(); ++it)
    {
        QUuid childId = it.key();
        QUuid parentId = it.value();

        PatternInfo *childPattern = cameraView->getPatternById(childId);
        PatternInfo *parentPattern = cameraView->getPatternById(parentId);

        if (childPattern && parentPattern)
        {
            // INS가 FID 하위로 가는 경우만 허용
            if (childPattern->type == PatternType::INS &&
                parentPattern->type == PatternType::FID)
            {

                if (childPattern->parentId != parentId)
                {

                    childPattern->parentId = parentId;
                    cameraView->updatePatternById(childId, *childPattern);
                    hasChanges = true;
                }
            }
        }
    }
}

PatternInfo *TeachingWidget::findPatternById(const QUuid &patternId)
{
    if (!cameraView)
        return nullptr;

    const auto &patterns = cameraView->getPatterns();
    for (auto it = patterns.begin(); it != patterns.end(); ++it)
    {
        if (it->id == patternId)
        {
            return const_cast<PatternInfo *>(&(*it));
        }
    }

    return nullptr;
}

// 각도 정규화 함수 (-180° ~ +180° 범위로 변환)
double TeachingWidget::normalizeAngle(double angle)
{
    // 각도를 0 ~ 360 범위로 먼저 정규화
    while (angle < 0)
        angle += 360.0;
    while (angle >= 360.0)
        angle -= 360.0;

    // -180 ~ +180 범위로 변환
    if (angle > 180.0)
    {
        angle -= 360.0;
    }

    return angle;
}

// === 레시피 관리 함수들 구현 ===

void TeachingWidget::newRecipe()
{

    // 저장되지 않은 변경사항 확인
    if (hasUnsavedChanges)
    {

        CustomMessageBox msgBox(this, CustomMessageBox::Question, "새 레시피",
                                "저장되지 않은 변경사항이 있습니다. 새 레시피를 생성하시겠습니까?");
        msgBox.setButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        int reply = msgBox.exec();

        if (reply == QMessageBox::Cancel)
        {

            return;
        }
        else if (reply == QMessageBox::Yes)
        {

            saveRecipe();
        }
    }

    // **첫 번째: 새 레시피 이름 입력받기**

    CustomMessageBox nameBox(this);
    nameBox.setTitle("새 레시피 생성");
    nameBox.setMessage("레시피 이름을 입력하세요:\n(비어있으면 자동으로 생성됩니다)");
    nameBox.setInputField(true, "");
    nameBox.setButtons(QMessageBox::Ok | QMessageBox::Cancel);

    int nameResult = nameBox.exec();

    if (nameResult != QDialog::Accepted)
    {

        return; // 사용자가 취소
    }
    QString recipeName = nameBox.getInputText();

    // 이름이 비어있으면 자동 생성 (년월일시간초밀리초)
    if (recipeName.trimmed().isEmpty())
    {
        QDateTime now = QDateTime::currentDateTime();
        recipeName = now.toString("yyyyMMdd_HHmmss_zzz");
    }
    else
    {
        recipeName = recipeName.trimmed();
    }

    // 중복 이름 확인
    QStringList existingRecipes = recipeManager->getAvailableRecipes();
    if (existingRecipes.contains(recipeName))
    {
        CustomMessageBox msgBox(this, CustomMessageBox::Question, "레시피 이름 중복",
                                QString("'%1' 레시피가 이미 존재합니다. 덮어쓰시겠습니까?").arg(recipeName));
        msgBox.setButtons(QMessageBox::Yes | QMessageBox::No);
        int reply = msgBox.exec();

        if (reply != QMessageBox::Yes)
        {
            return;
        }
    }

    // **두 번째 선택: "이미지 찾기" vs "레시피로 읽기"**

    CustomMessageBox msgBox(this);
    msgBox.setTitle("새 레시피 생성");
    msgBox.setMessage("영상을 어디서 가져오시겠습니까?");
    msgBox.setButtons(QMessageBox::NoButton); // 기본 버튼 없음

    // 커스텀 버튼 생성
    QPushButton *imageButton = new QPushButton("이미지 찾기");
    QPushButton *recipeButton = new QPushButton("레시피로 읽기");
    QPushButton *cancelButton = new QPushButton("취소");

    // 버튼을 대화상자 레이아웃에 추가
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(imageButton);
    buttonLayout->addWidget(recipeButton);
    buttonLayout->addWidget(cancelButton);

    QVBoxLayout *mainLayout = qobject_cast<QVBoxLayout *>(msgBox.layout());
    if (mainLayout)
    {
        mainLayout->addLayout(buttonLayout);
    }

    bool useImage = false;
    bool useRecipe = false;
    QPushButton *clickedBtn = nullptr;

    connect(imageButton, &QPushButton::clicked, [&]()
            {
        clickedBtn = imageButton;
        msgBox.accept(); });
    connect(recipeButton, &QPushButton::clicked, [&]()
            {
        clickedBtn = recipeButton;
        msgBox.accept(); });
    connect(cancelButton, &QPushButton::clicked, [&]()
            {
        clickedBtn = cancelButton;
        msgBox.reject(); });

    int result = msgBox.exec();

    if (clickedBtn == imageButton)
    {

        useImage = true;
    }
    else if (clickedBtn == recipeButton)
    {

        useRecipe = true;
    }
    else
    {

        return; // 취소
    }

    // **세 번째: 이미지 찾기 또는 레시피로 읽기**
    if (useImage)
    {
        // 이미지 파일 선택
        QString imageFile = QFileDialog::getOpenFileName(this,
                                                         "티칭용 이미지 선택",
                                                         "",
                                                         "이미지 파일 (*.jpg *.jpeg *.png *.bmp *.tiff *.tif)");

        if (imageFile.isEmpty())
        {
            CustomMessageBox(this, CustomMessageBox::Information, "알림",
                             "이미지가 선택되지 않았습니다.")
                .exec();
            return;
        }

        // 선택한 이미지를 카메라뷰에 로드
        QPixmap pixmap(imageFile);
        if (pixmap.isNull() || !cameraView)
        {
            CustomMessageBox(this, CustomMessageBox::Warning, "이미지 로드 실패",
                             "선택한 이미지를 로드할 수 없습니다.")
                .exec();
            return;
        }

        cameraView->setBackgroundImage(pixmap);

        // cameraFrames에도 이미지 설정 (티칭 시 템플릿 추출을 위해 필요)
        cv::Mat loadedImage;
        QImage qImage = pixmap.toImage();
        if (qImage.format() != QImage::Format_RGB888)
        {
            qImage = qImage.convertToFormat(QImage::Format_RGB888);
        }
        loadedImage = cv::Mat(qImage.height(), qImage.width(), CV_8UC3,
                              (void *)qImage.constBits(), qImage.bytesPerLine())
                          .clone();
        cv::cvtColor(loadedImage, loadedImage, cv::COLOR_RGB2BGR);

        // cameraFrames[cameraIndex]에 저장
        if (4 <= static_cast<size_t>(cameraIndex))
        {
        }
        cameraFrames[cameraIndex] = loadedImage.clone();

        // 생성 날짜를 카메라 이름으로 설정 (레시피 이름과 동일)
        QString cameraName = recipeName; // 레시피 이름(타임스탬프)을 카메라 이름으로 사용
        cameraView->setCurrentCameraName(cameraName);
        cameraView->setCurrentCameraUuid(cameraName); // UUID도 같은 이름으로 설정

        // 가상 카메라 정보 생성 (레시피 저장을 위해 필요)
        CameraInfo virtualCamera;
        virtualCamera.name = cameraName;
        virtualCamera.uniqueId = cameraName;
        virtualCamera.index = 0;
        // videoDeviceIndex 제거됨
        virtualCamera.isConnected = true; // 시뮬레이션 모드에서는 연결된 것으로 표시
        virtualCamera.serialNumber = "0";

        // cameraInfos 초기화 및 설정
        cameraInfos.clear();
        cameraInfos.append(virtualCamera);
        cameraIndex = 0;
    }
    else if (useRecipe)
    {
        // 기존 레시피 목록 표시
        QStringList availableRecipes = recipeManager->getAvailableRecipes();

        if (availableRecipes.isEmpty())
        {
            CustomMessageBox(this, CustomMessageBox::Information, "레시피 없음", "사용 가능한 레시피가 없습니다.").exec();
            return;
        }

        // 레시피 선택 대화상자
        bool ok = false;
        QString selectedRecipe = QInputDialog::getItem(this,
                                                       "기존 레시피 선택",
                                                       "영상을 불러올 레시피를 선택하세요:",
                                                       availableRecipes,
                                                       0,
                                                       false,
                                                       &ok);

        if (!ok || selectedRecipe.isEmpty())
        {
            return; // 사용자가 취소
        }

        // 선택한 레시피에서 메인 카메라 이미지 로드
        cv::Mat mainCameraImage;
        QString cameraName;
        if (!recipeManager->loadMainCameraImage(selectedRecipe, mainCameraImage, cameraName))
        {
            CustomMessageBox(this, CustomMessageBox::Warning, "이미지 로드 실패",
                             QString("레시피 '%1'에서 이미지를 불러올 수 없습니다.\n오류: %2")
                                 .arg(selectedRecipe)
                                 .arg(recipeManager->getLastError()))
                .exec();
            return;
        }

        if (mainCameraImage.empty())
        {
            CustomMessageBox(this, CustomMessageBox::Warning, "이미지 없음",
                             QString("레시피 '%1'에서 이미지를 찾을 수 없습니다.").arg(selectedRecipe))
                .exec();
            return;
        }

        // cv::Mat을 QPixmap으로 변환해서 표시
        cv::Mat displayImage;
        cv::cvtColor(mainCameraImage, displayImage, cv::COLOR_BGR2RGB);
        QImage qImage(displayImage.data, displayImage.cols, displayImage.rows,
                      displayImage.step, QImage::Format_RGB888);
        QPixmap pixmap = QPixmap::fromImage(qImage);

        if (cameraView)
        {
            cameraView->setBackgroundImage(pixmap);
        }

        // cameraFrames에 설정
        if (4 <= static_cast<size_t>(cameraIndex))
        {
        }
        cameraFrames[cameraIndex] = mainCameraImage.clone();

        // 카메라 정보 설정
        if (cameraView)
        {
            cameraView->setCurrentCameraName(cameraName);
            cameraView->setCurrentCameraUuid(cameraName);
        }

        // 가상 카메라 정보 생성
        if (cameraInfos.empty() || cameraIndex >= cameraInfos.size())
        {
            CameraInfo virtualCamera;
            virtualCamera.name = cameraName;
            virtualCamera.uniqueId = cameraName;
            virtualCamera.index = 0;
            virtualCamera.isConnected = false;

            if (cameraInfos.empty())
            {
                cameraInfos.append(virtualCamera);
            }
            else
            {
                cameraInfos[cameraIndex] = virtualCamera;
            }
        }
    }

    // 기존 패턴들 클리어
    if (cameraView)
    {
        cameraView->clearPatterns();
    }
    if (patternTree)
    {
        patternTree->clear();
    }

    // 새 레시피 상태로 설정
    currentRecipeName = recipeName;
    hasUnsavedChanges = true; // 사용자가 명시적으로 저장할 때까지 대기

    // 윈도우 타이틀 업데이트
    setWindowTitle(QString("KM Inspector - %1").arg(recipeName));
}

void TeachingWidget::loadTeachingImage()
{
    // 이미지 파일 선택 (CustomFileDialog 사용)
    QString imageFile = CustomFileDialog::getOpenFileName(
        this,
        "티칭용 이미지 선택",
        "",
        "이미지 파일 (*.jpg *.jpeg *.png *.bmp *.tiff *.tif)"
    );

    if (imageFile.isEmpty())
    {
        return;
    }

    // 확인 대화상자
    CustomMessageBox confirmBox(this, CustomMessageBox::Question, "이미지 교체 확인",
                                "티칭 이미지로 바꾸시겠습니까?");
    confirmBox.setButtons(QMessageBox::Yes | QMessageBox::No);

    int reply = confirmBox.exec();
    if (reply != QMessageBox::Yes)
    {
        return;
    }

    // 이미지 로드
    QPixmap pixmap(imageFile);
    if (pixmap.isNull() || !cameraView)
    {
        CustomMessageBox(this, CustomMessageBox::Warning, "이미지 로드 실패",
                         "선택한 이미지를 로드할 수 없습니다.")
            .exec();
        return;
    }

    // cv::Mat으로 변환
    cv::Mat loadedImage;
    QImage qImage = pixmap.toImage();
    if (qImage.format() != QImage::Format_RGB888)
    {
        qImage = qImage.convertToFormat(QImage::Format_RGB888);
    }
    loadedImage = cv::Mat(qImage.height(), qImage.width(), CV_8UC3,
                          (void *)qImage.constBits(), qImage.bytesPerLine())
                      .clone();
    cv::cvtColor(loadedImage, loadedImage, cv::COLOR_RGB2BGR);

    // cameraFrames에 저장
    if (4 <= static_cast<size_t>(cameraIndex))
    {
    }
    cameraFrames[cameraIndex] = loadedImage.clone();

    // 카메라 정보가 없으면 기본 카메라 정보 생성
    if (cameraInfos.isEmpty())
    {
        // 이미지 파일명에서 확장자를 제외한 부분을 카메라 이름으로 사용
        QFileInfo fileInfo(imageFile);
        QString cameraName = fileInfo.baseName(); // 확장자를 제외한 파일명

        CameraInfo defaultCamera;
        defaultCamera.name = cameraName;
        defaultCamera.uniqueId = QUuid::createUuid().toString();
        cameraInfos.append(defaultCamera);
        cameraIndex = 0;
    }

    // 화면에 표시
    cameraView->setBackgroundImage(pixmap);
    
    // 이미지가 바뀌었으므로 검사 결과 초기화
    cameraView->clearInspectionResult();

    // 변경사항 플래그 설정
    hasUnsavedChanges = true;
}

void TeachingWidget::saveRecipeAs()
{
    CustomMessageBox msgBox(this);
    msgBox.setTitle("레시피 저장");
    msgBox.setMessage("레시피 이름을 입력하세요:");
    msgBox.setInputField(true, currentRecipeName);
    msgBox.setButtons(QMessageBox::Ok | QMessageBox::Cancel);

    if (msgBox.exec() != QMessageBox::Ok)
    {
        return;
    }
    QString recipeName = msgBox.getInputText();

    if (!recipeName.isEmpty())
    {
        RecipeManager manager;

        // 같은 이름의 레시피가 있는지 확인
        QStringList existingRecipes = manager.getAvailableRecipes();
        if (existingRecipes.contains(recipeName))
        {
            CustomMessageBox msgBox(this, CustomMessageBox::Question, "레시피 저장",
                                    QString("'%1' 레시피가 이미 존재합니다. 덮어쓰시겠습니까?").arg(recipeName));
            msgBox.setButtons(QMessageBox::Yes | QMessageBox::No);
            int reply = msgBox.exec();

            if (reply != QMessageBox::Yes)
            {
                return;
            }
        }

        // 기존 saveRecipe 함수 사용
        QString recipeFileName = QString("recipes/%1/%1.xml").arg(recipeName);
        QMap<QString, CalibrationInfo> calibrationMap;
        QStringList simulationImagePaths;
        if (manager.saveRecipe(recipeFileName, cameraInfos, cameraIndex, calibrationMap, cameraView, simulationImagePaths))
        {
            currentRecipeName = recipeName;
            hasUnsavedChanges = false;

            // 티칭 이미지는 XML에 base64로 저장됨

            CustomMessageBox(this, CustomMessageBox::Information, "레시피 저장",
                             QString("'%1' 레시피가 성공적으로 저장되었습니다.").arg(recipeName))
                .exec();
        }
        else
        {
            CustomMessageBox(this, CustomMessageBox::Critical, "레시피 저장 실패",
                             QString("레시피 저장에 실패했습니다:\n%1").arg(manager.getLastError()))
                .exec();
        }
    }
}

// 레시피 관리 함수
void TeachingWidget::clearAllRecipeData()
{
    qDebug() << "[clearAllRecipeData] 레시피 데이터 초기화 시작";

    // 1. cameraFrames 초기화 (CAM ON 상태에서도 허용)
    for (auto& frame : cameraFrames) {
        frame.release();
    }
    qDebug() << "[clearAllRecipeData] cameraFrames 초기화";

    // 2. 뷰포트 클리어 (배경 이미지 및 패턴 제거)
    if (cameraView)
    {
        cameraView->setBackgroundPixmap(QPixmap());
        cameraView->clearPatterns();
        cameraView->setSelectedPatternId(QUuid());
        cameraView->clearInspectionResult();  // 검사 결과 초기화 추가
        cameraView->update();
        qDebug() << "[clearAllRecipeData] 뷰포트 클리어 및 검사 결과 초기화";
    }
    
    // CAM ON 상태면 새 카메라 영상 프레임으로 갱신
    if (!camOff)
    {
        qDebug() << "[clearAllRecipeData] CAM ON 상태 - 새 프레임 요청";
        // 카메라 스레드에서 새 프레임을 받아올 때까지 대기
        // 프레임은 자동으로 grabber thread에서 업데이트됨
    }

    // 3. 패턴 트리 초기화
    if (patternTree)
    {
        patternTree->clear();
        qDebug() << "[clearAllRecipeData] 패턴 트리 초기화";
    }

    // 4. 프로퍼티 패널 초기화
    if (propertyStackWidget)
    {
        propertyStackWidget->setCurrentIndex(0);
    }

    // 5. 4개 프레임 미리보기 초기화
    for (int i = 0; i < 4; i++)
    {
        if (previewOverlayLabels[i])
        {
            previewOverlayLabels[i]->clear();
            previewOverlayLabels[i]->setText("");
        }
    }
    qDebug() << "[clearAllRecipeData] 프레임 미리보기 초기화";

    // 6. 마지막 레시피 경로 초기화
    ConfigManager::instance()->setLastRecipePath("");

    qDebug() << "[clearAllRecipeData] 완료";
}

void TeachingWidget::manageRecipes()
{
    RecipeManager manager;
    QStringList availableRecipes = manager.getAvailableRecipes();

    QDialog dialog(this);
    dialog.setWindowTitle("레시피 관리");
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setMinimumSize(400, 300);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    // 레시피 목록
    QLabel *label = new QLabel("저장된 레시피 목록:");
    layout->addWidget(label);

    QListWidget *recipeList = new QListWidget(&dialog);
    recipeList->addItems(availableRecipes);
    layout->addWidget(recipeList);

    // 버튼들
    QHBoxLayout *buttonLayout = new QHBoxLayout();

    QPushButton *loadButton = new QPushButton("불러오기");
    QPushButton *copyButton = new QPushButton("복사");
    QPushButton *deleteButton = new QPushButton("삭제");
    QPushButton *renameButton = new QPushButton("이름 변경");
    QPushButton *closeButton = new QPushButton("닫기");

    buttonLayout->addWidget(loadButton);
    buttonLayout->addWidget(copyButton);
    buttonLayout->addWidget(deleteButton);
    buttonLayout->addWidget(renameButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);

    layout->addLayout(buttonLayout);

    // 버튼 활성화 상태 관리
    auto updateButtonState = [&]()
    {
        bool hasSelection = recipeList->currentItem() != nullptr;
        loadButton->setEnabled(hasSelection);
        copyButton->setEnabled(hasSelection);
        deleteButton->setEnabled(hasSelection);
        renameButton->setEnabled(hasSelection);
    };

    connect(recipeList, &QListWidget::itemSelectionChanged, updateButtonState);
    updateButtonState();

    // 버튼 이벤트 연결
    connect(loadButton, &QPushButton::clicked, [&]()
            {
        QListWidgetItem* item = recipeList->currentItem();
        if (item) {
            QString recipeName = item->text();
            dialog.accept();
            onRecipeSelected(recipeName);
        } });

    connect(deleteButton, &QPushButton::clicked, [&]()
            {
        QListWidgetItem* item = recipeList->currentItem();
        if (item) {
            QString recipeName = item->text();
            CustomMessageBox msgBox(&dialog, CustomMessageBox::Question, "레시피 삭제",
                QString("'%1' 레시피를 삭제하시겠습니까?").arg(recipeName));
            msgBox.setButtons(QMessageBox::Yes | QMessageBox::No);
            int reply = msgBox.exec();
            
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
                        
                        // 메인 화면과 4분할 미리보기 갱신
                        if (!camOff) {
                            // CAM ON: 현재 카메라 프레임으로 갱신
                            if (cameraIndex >= 0 && cameraIndex < 4 && !cameraFrames[cameraIndex].empty()) {
                                QImage qImage = InsProcessor::matToQImage(cameraFrames[cameraIndex]);
                                QPixmap pixmap = QPixmap::fromImage(qImage);
                                cameraView->setBackgroundPixmap(pixmap);
                            }
                        } else {
                            // CAM OFF: 배경 제거
                            cameraView->setBackgroundPixmap(QPixmap());
                        }
                        
                        // 4분할 미리보기 갱신
                        for (int i = 0; i < 4; i++) {
                            if (previewOverlayLabels[i]) {
                                if (!cameraFrames[i].empty()) {
                                    QImage qImage = InsProcessor::matToQImage(cameraFrames[i]);
                                    QPixmap pixmap = QPixmap::fromImage(qImage);
                                    previewOverlayLabels[i]->setPixmap(pixmap.scaled(
                                        previewOverlayLabels[i]->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                                } else {
                                    previewOverlayLabels[i]->clear();
                                }
                            }
                        }
                        
                        cameraView->update();
                    }
                    
                    CustomMessageBox(&dialog, CustomMessageBox::Information, "레시피 삭제",
                        QString("'%1' 레시피가 삭제되었습니다.").arg(recipeName)).exec();
                } else {
                    CustomMessageBox(&dialog, CustomMessageBox::Critical, "레시피 삭제 실패",
                        QString("레시피 삭제에 실패했습니다:\n%1").arg(manager.getLastError())).exec();
                }
            }
        } });

    connect(renameButton, &QPushButton::clicked, [&]()
            {
        QListWidgetItem* item = recipeList->currentItem();
        if (item) {
            QString oldName = item->text();
            CustomMessageBox msgBox(&dialog);
            msgBox.setTitle("레시피 이름 변경");
            msgBox.setMessage("새 레시피 이름을 입력하세요:");
            msgBox.setInputField(true, oldName);
            msgBox.setButtons(QMessageBox::Ok | QMessageBox::Cancel);
            
            if (msgBox.exec() == QMessageBox::Ok) {
                QString newName = msgBox.getInputText();
                if (!newName.isEmpty() && newName != oldName) {
                    if (manager.renameRecipe(oldName, newName)) {
                        item->setText(newName);
                        
                        // 현재 로드된 레시피가 변경된 레시피라면 이름 업데이트
                        if (currentRecipeName == oldName) {
                            currentRecipeName = newName;
                        }
                        
                        CustomMessageBox(&dialog, CustomMessageBox::Information, "레시피 이름 변경",
                            QString("'%1'에서 '%2'로 이름이 변경되었습니다.").arg(oldName, newName)).exec();
                    } else {
                        CustomMessageBox(&dialog, CustomMessageBox::Critical, "레시피 이름 변경 실패",
                            QString("레시피 이름 변경에 실패했습니다:\n%1").arg(manager.getLastError())).exec();
                    }
                }
            }
        } });

    connect(copyButton, &QPushButton::clicked, [&]()
            {
        QListWidgetItem* item = recipeList->currentItem();
        if (item) {
            QString sourceName = item->text();
            
            // 레시피의 카메라 이름 가져오기
            QString recipeCameraName = manager.getRecipeCameraName(sourceName);
            
            // 현재 카메라 이름 가져오기
            QString currentCameraName;
            if (!cameraInfos.isEmpty()) {
                currentCameraName = cameraInfos[0].name;
            }
            
            QString targetCameraName;
            bool needsCameraChange = false;
            
            // 레시피 카메라와 현재 카메라가 다른지 확인
            if (!recipeCameraName.isEmpty() && !currentCameraName.isEmpty() && 
                recipeCameraName != currentCameraName) {
                
                CustomMessageBox confirmBox(&dialog, CustomMessageBox::Question, "카메라 이름 변경",
                    QString("레시피의 카메라 이름: %1\n현재 카메라 이름: %2\n\n"
                            "현재 카메라에 맞게 레시피를 복사하시겠습니까?")
                    .arg(recipeCameraName, currentCameraName));
                confirmBox.setButtons(QMessageBox::Yes | QMessageBox::No);
                
                if (confirmBox.exec() == QMessageBox::Yes) {
                    targetCameraName = currentCameraName;
                    needsCameraChange = true;
                }
            }
            
            // 새 레시피 이름 입력
            CustomMessageBox msgBox(&dialog);
            msgBox.setTitle("레시피 복사");
            msgBox.setMessage("복사할 레시피 이름을 입력하세요:");
            msgBox.setInputField(true, sourceName + "_복사");
            msgBox.setButtons(QMessageBox::Ok | QMessageBox::Cancel);
            
            if (msgBox.exec() == QMessageBox::Ok) {
                QString newName = msgBox.getInputText();
                if (!newName.isEmpty() && newName != sourceName) {
                    if (manager.copyRecipe(sourceName, newName, needsCameraChange ? targetCameraName : QString())) {
                        recipeList->addItem(newName);
                        
                        QString message = QString("'%1'에서 '%2'로 복사되었습니다.").arg(sourceName, newName);
                        if (needsCameraChange) {
                            message += QString("\n카메라 이름이 '%1'(으)로 변경되었습니다.").arg(targetCameraName);
                        }
                        
                        CustomMessageBox(&dialog, CustomMessageBox::Information, "레시피 복사", message).exec();
                    } else {
                        CustomMessageBox(&dialog, CustomMessageBox::Critical, "레시피 복사 실패",
                            QString("레시피 복사에 실패했습니다:\n%1").arg(manager.getLastError())).exec();
                    }
                }
            }
        } });

    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    // 중앙 배치
    QRect parentRect = frameGeometry();
    int x = parentRect.x() + (parentRect.width() - dialog.width()) / 2;
    int y = parentRect.y() + (parentRect.height() - dialog.height()) / 2;
    int titleBarHeight = frameGeometry().height() - geometry().height();
    y -= titleBarHeight / 2;
    dialog.move(x, y);

    dialog.exec();
}

void TeachingWidget::onRecipeSelected(const QString &recipeName)
{
    // 레시피 로드 시작 - 템플릿 자동 업데이트 방지
    isLoadingRecipe = true;
    
    // 검사 결과 초기화 (레시피만 로드한 초기 상태)
    if (cameraView) {
        cameraView->clearInspectionResult();
        cameraView->clearModeResults();
    }
    
    // 저장되지 않은 변경사항 확인
    if (hasUnsavedChanges)
    {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Question);
        msgBox.setTitle("레시피 불러오기");
        msgBox.setMessage("저장되지 않은 변경사항이 있습니다. 레시피를 불러오시겠습니까?");
        msgBox.setButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

        QMessageBox::StandardButton reply = static_cast<QMessageBox::StandardButton>(msgBox.exec());

        if (reply == QMessageBox::Cancel)
        {
            return;
        }
        else if (reply == QMessageBox::Yes)
        {
            saveRecipe();
        }
    }

    RecipeManager manager;

    // ★ CAM ON 상태에서 레시피 로드 시 스레드 일시정지 (cameraInfos 변경 방지)
    bool wasThreadsPaused = false;
    if (!camOff)
    {
        if (uiUpdateThread)
        {
            uiUpdateThread->setPaused(true);
        }
        for (CameraGrabberThread *thread : cameraThreads)
        {
            if (thread)
            {
                thread->setPaused(true);
            }
        }
        wasThreadsPaused = true;
        QThread::msleep(100); // 스레드가 완전히 일시정지될 때까지 대기
    }

    // 레시피 파일 경로 설정
    QString recipeFileName = QDir(manager.getRecipesDirectory()).absoluteFilePath(QString("%1/%1.xml").arg(recipeName));
    QMap<QString, CalibrationInfo> calibrationMap;

    // 레시피에서 카메라 정보 먼저 읽기 (camOn/camOff 공통)
    QStringList recipeCameraUuids = manager.getRecipeCameraUuids(recipeName);

    // **camOff 상태는 사용자가 CAM 버튼으로 제어하므로 자동 전환하지 않음**

    // camOff 모드에서는 cameraInfos를 비워서 레시피에서 새로 생성하도록 함
    // camOn 모드에서는 기존 cameraInfos 유지 (카메라 연결 상태 유지)
    if (camOff)
    {

        cameraInfos.clear();
    }
    else
    {
        // camOn 모드에서는 기존 cameraInfos 유지
    }

    // 티칭 이미지 콜백 함수 정의 (camOn/camOff 공통)
    auto teachingImageCallback = [this](const QStringList &imagePaths)
    {
        // ★ CAM ON/OFF 모두 티칭 이미지를 cameraFrames에 로드 (CAM ON에서는 임시로, 라이브 프레임이 들어오면 덮어써짐)
        
        // **카메라 ON/OFF 모두 티칭 이미지를 cameraFrames에 로드**

        int imageIndex = 0;
        for (const QString &imagePath : imagePaths)
        {

            // base64 더미 경로인 경우 특별 처리 (이미 cameraFrames에 로드됨)
            if (imagePath.startsWith("base64_image_"))
            {
                imageIndex++;
                continue;
            }

            // 실제 파일 경로인 경우 기존 로직 사용
            if (QFile::exists(imagePath))
            {
                cv::Mat teachingImage = cv::imread(imagePath.toStdString());
                if (!teachingImage.empty())
                {
                    // imageIndex를 cameraIndex로 직접 사용
                    int camIdx = imageIndex;
                    
                    // cameraFrames 배열 크기 확장
                    cameraFrames[camIdx] = teachingImage.clone();
                }
            }
            imageIndex++; // 실패해도 인덱스는 증가
        }

        // 모든 이미지 로드 완료 후 UI 업데이트 (camOn/camOff 공통)

        if (cameraIndex >= 0 && cameraIndex < static_cast<int>(4))
        {
        }

        // **카메라 ON 상태에서는 updateCameraFrame() 호출 금지 - 패턴만 로드**
        if (!camOff)
        {
            // 카메라 ON 상태 - 패턴만 로드됨
        }
        else if (cameraIndex >= 0 && cameraIndex < static_cast<int>(4) &&
                 !cameraFrames[cameraIndex].empty())
        {
            qDebug() << "[onRecipeSelected] camOff 상태에서 updateCameraFrame 호출 - cameraIndex:" << cameraIndex 
                     << "cameraFrames.size:" << 4;
            updateCameraFrame();
        }
        else
        {
            // 조건 불충족 시 디버그 출력
            qDebug() << "[onRecipeSelected] updateCameraFrame 호출 안됨!"
                     << "cameraIndex:" << cameraIndex
                     << "cameraFrames.size:" << 4
                     << "camOff:" << camOff;
            if (cameraIndex >= 0 && cameraIndex < static_cast<int>(4))
            {
                qDebug() << "  cameraFrames[cameraIndex].empty():" << cameraFrames[cameraIndex].empty();
            }
            
            // ★ 수정: cameraFrames[0]이 비어있지 않으면 updateCameraFrame 호출
            if (camOff && !cameraFrames.empty() && !cameraFrames[0].empty())
            {
                cameraIndex = 0;
                qDebug() << "[onRecipeSelected] cameraIndex=0으로 설정 후 updateCameraFrame 호출";
                updateCameraFrame();
            }
        }

        // 프리뷰 화면들도 업데이트
        updatePreviewFrames();
        
        // 4분할 뷰에 프레임 설정 (영상이 있으면 무조건 표시)
        if (cameraView && cameraView->getQuadViewMode())
        {
            cameraView->setQuadFrames(cameraFrames);
            cameraView->viewport()->update();
            cameraView->repaint();
        }
    };

    // ★ CAM ON 상태에서는 레시피 로드 전에 기존 패턴 제거 (중복 방지)
    if (!camOff && cameraView)
    {
        cameraView->clearPatterns();
        if (patternTree)
        {
            patternTree->clear();
        }
    }

    if (manager.loadRecipe(recipeFileName, cameraInfos, calibrationMap, cameraView, patternTree, teachingImageCallback, this))
    {
        currentRecipeName = recipeName;
        hasUnsavedChanges = false;

        // 윈도우 타이틀 업데이트
        setWindowTitle(QString("KM Inspector - %1").arg(recipeName));

        // 최근 사용한 레시피를 ConfigManager에 저장
        ConfigManager::instance()->setLastRecipePath(recipeName);
        ConfigManager::instance()->saveConfig();
        
        // weights 동기화: 레시피에 없는 패턴의 weights 폴더 삭제
        QString weightsDir = QCoreApplication::applicationDirPath() + "/recipes/" + recipeName + "/weights";
        QDir weightsDirObj(weightsDir);
        if (weightsDirObj.exists()) {
            // 현재 레시피의 ANOMALY 패턴 이름 목록
            QSet<QString> anomalyPatternNames;
            for (const PatternInfo& pattern : cameraView->getPatterns()) {
                if (pattern.type == PatternType::INS && 
                    pattern.inspectionMethod == InspectionMethod::ANOMALY) {
                    anomalyPatternNames.insert(pattern.name);
                }
            }
            
            // weights 폴더 내 서브폴더 검사
            QStringList weightsFolders = weightsDirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& folderName : weightsFolders) {
                if (!anomalyPatternNames.contains(folderName)) {
                    // 레시피에 없는 패턴의 weights 폴더 삭제
                    QString folderPath = weightsDir + "/" + folderName;
                    QDir(folderPath).removeRecursively();
                    qDebug() << "[RECIPE] 사용되지 않는 weights 삭제됨:" << folderName;
                }
            }
        }

        // **STRIP/CRIMP 이미지는 이미 loadRecipe에서 로드되었으므로 추가 로드 불필요**
        // loadMainCameraImage는 첫 번째 TeachingImage만 읽어서 현재 모드를 무시하므로 제거

        // TODO: 현재 연결된 카메라의 패턴만 필터링하는 기능 추가 예정

        // 첫 번째로 비어있지 않은 프레임 찾기
        int firstValidFrameIndex = -1;
        for (int i = 0; i < static_cast<int>(cameraFrames.size()); ++i)
        {
            if (!cameraFrames[i].empty())
            {
                firstValidFrameIndex = i;
                break;
            }
        }

        if (firstValidFrameIndex >= 0)
        {
            currentDisplayFrameIndex = firstValidFrameIndex;
            
            // 해당 프레임의 카메라 UUID 설정
            if (firstValidFrameIndex < cameraInfos.size() && cameraView)
            {
                QString firstCameraUuid = cameraInfos[firstValidFrameIndex].uniqueId;
                
                cv::Mat displayImage;
                cv::cvtColor(cameraFrames[firstValidFrameIndex], displayImage, cv::COLOR_BGR2RGB);
                QImage qImage(displayImage.data, displayImage.cols, displayImage.rows,
                              displayImage.step, QImage::Format_RGB888);
                QPixmap pixmap = QPixmap::fromImage(qImage.copy());
                cameraView->setBackgroundImage(pixmap);
                cameraView->setCurrentFrameIndex(firstValidFrameIndex);
                cameraView->setCurrentCameraUuid(firstCameraUuid);
                
                qDebug() << "[onRecipeSelected] 첫 번째 유효한 프레임 표시 - frameIndex:" << firstValidFrameIndex 
                         << "cameraUuid:" << firstCameraUuid;
            }
            // CAM OFF 상태에서만 updateCameraFrame 호출 (CAM ON에서는 배경 이미지 유지)
            if (camOff) {
                updateCameraFrame();
            }
            updatePreviewFrames();
        }
        else
        {
            currentDisplayFrameIndex = 0;
            qDebug() << "[onRecipeSelected] 유효한 cameraFrames가 없음";
        }

        // 패턴 동기화 및 트리 업데이트
        updatePatternTree();
        
        // 프레임별 패턴 리스트 미리 분리 (스레드 안전성 및 성능 향상)
        QList<PatternInfo> allPatterns = cameraView->getPatterns();
        for (int i = 0; i < 4; i++)
        {
            framePatternLists[i].clear();
            for (const auto &pattern : allPatterns)
            {
                if (pattern.frameIndex == i)
                {
                    framePatternLists[i].append(pattern);
                }
            }
        }
        
        // 레시피 로드 완료 - 템플릿 자동 업데이트 재활성화
        isLoadingRecipe = false;
        
        // Anomaly 모델 워밍업 (첫 검사 속도 향상)
        if (insProcessor) {
            insProcessor->warmupAnomalyModels(allPatterns, recipeName);
        }

        if (!cameraInfos.isEmpty())
        {
            // 레시피에서 카메라 UUID 목록 가져오기
            QStringList recipeCameraUuids = manager.getRecipeCameraUuids(recipeName);
            QString firstCameraUuid;

            if (!recipeCameraUuids.isEmpty())
            {
                // 레시피의 첫 번째 카메라 UUID 사용
                firstCameraUuid = recipeCameraUuids.first();
            }
            else
            {
                // 레시피에 카메라 정보가 없으면 cameraInfos에서 가져오기
                firstCameraUuid = cameraInfos[0].uniqueId;
            }

            switchToCamera(firstCameraUuid);
            cameraIndex = 0;

            if (cameraView)
            {
                cameraView->setCurrentCameraUuid(firstCameraUuid);
                cameraView->update();

                // 디버그: 현재 CameraView 상태 확인

                // 강제 repaint
                cameraView->repaint();
                QApplication::processEvents();
            }

            // 이미 위에서 정의된 recipeCameraUuids 사용
            if (!recipeCameraUuids.isEmpty())
            {
                QString firstCameraUuid = recipeCameraUuids.first();

                // cameraFrames 상태 디버그 출력

                for (int i = 0; i < static_cast<int>(4); i++)
                {
                    if (!cameraFrames[i].empty())
                    {
                    }
                    else
                    {
                    }
                }

                if (cameraFrames.empty())
                {
                }
                else if (4 > 0 && cameraFrames[0].empty())
                {
                }

                // 첫 번째 카메라로 전환 (프리뷰도 자동 할당됨)
                switchToCamera(firstCameraUuid);
                cameraIndex = 0;

                // camOff 모드에서 첫 번째 카메라의 티칭 이미지를 메인 카메라뷰에 표시
                if (!cameraFrames.empty() && !cameraFrames[0].empty() && cameraView)
                {
                    cv::Mat firstCameraImage = cameraFrames[0];

                    // OpenCV Mat을 QImage로 변환
                    QImage qImage;
                    if (firstCameraImage.channels() == 3)
                    {
                        cv::Mat rgbImage;
                        cv::cvtColor(firstCameraImage, rgbImage, cv::COLOR_BGR2RGB);
                        qImage = QImage(rgbImage.data, rgbImage.cols, rgbImage.rows, rgbImage.step, QImage::Format_RGB888);
                    }
                    else
                    {
                        qImage = QImage(firstCameraImage.data, firstCameraImage.cols, firstCameraImage.rows, firstCameraImage.step, QImage::Format_Grayscale8);
                    }

                    if (!qImage.isNull())
                    {
                        QPixmap pixmap = QPixmap::fromImage(qImage);
                        cameraView->setBackgroundPixmap(pixmap);
                        cameraView->update();
                    }
                }
                updateCameraFrame();
            }
        }

        // cameraInfos 요약 정보 출력

        // ★ ANOMALY 패턴이 있으면 PatchCore 모델 미리 로딩
        {
            const QList<PatternInfo> patterns = cameraView->getPatterns();
            for (const PatternInfo &pattern : patterns)
            {
                if (pattern.type == PatternType::INS && 
                    pattern.inspectionMethod == InspectionMethod::ANOMALY)
                {
                    QString appDir = QCoreApplication::applicationDirPath();
                    // 패턴 이름 기반으로 모델 경로 구성: weights/{패턴명}/{패턴명}.xml
                    QString fullModelPath = appDir + "/weights/" + pattern.name + "/" + pattern.name + ".xml";
                    
                    if (QFile::exists(fullModelPath))
                    {
                        ImageProcessor::initPatchCoreTensorRT(fullModelPath, "CPU");
                        // break 제거 - 모든 ANOMALY 패턴의 모델을 로드
                    }
                }
            }
        }

        // 4분할 화면 업데이트 (영상이 있으면 무조건 표시)
        if (cameraView && cameraView->getQuadViewMode())
        {
            cameraView->setQuadFrames(cameraFrames);
            cameraView->viewport()->update();
            cameraView->repaint();
            QApplication::processEvents();
        }
        
        // ★ CAM ON 상태였으면 스레드 재개
        if (wasThreadsPaused)
        {
            // 배경 이미지 확실히 유지 (스레드 재개 전)
            if (!cameraFrames.empty() && !cameraFrames[0].empty() && cameraView)
            {
                cv::Mat displayImage;
                cv::cvtColor(cameraFrames[0], displayImage, cv::COLOR_BGR2RGB);
                QImage qImage(displayImage.data, displayImage.cols, displayImage.rows,
                              displayImage.step, QImage::Format_RGB888);
                QPixmap pixmap = QPixmap::fromImage(qImage.copy());
                cameraView->setBackgroundImage(pixmap);
                cameraView->repaint();
                QApplication::processEvents();
            }
            
            // 스레드 재개 (트리거 모드에서는 트리거 신호가 올 때까지 프레임이 들어오지 않음)
            for (CameraGrabberThread *thread : cameraThreads)
            {
                if (thread)
                {
                    thread->setPaused(false);
                }
            }
            if (uiUpdateThread)
            {
                uiUpdateThread->setPaused(false);
            }
        }
    }
    else
    {
        QString errorMsg = manager.getLastError();
        
        // 레시피가 존재하지 않는 경우에는 메시지 박스를 표시하지 않음 (자동 로드 시)
        if (!errorMsg.contains("존재하지 않습니다") && !errorMsg.contains("does not exist"))
        {
            CustomMessageBox(this, CustomMessageBox::Critical, "레시피 불러오기 실패",
                             QString("레시피 불러오기에 실패했습니다:\n%1").arg(errorMsg))
                .exec();
        }
        else
        {
        }

        // ★ 레시피 로드 실패해도 스레드 재개
        if (wasThreadsPaused)
        {
            for (CameraGrabberThread *thread : cameraThreads)
            {
                if (thread)
                {
                    thread->setPaused(false);
                }
            }
            if (uiUpdateThread)
            {
                uiUpdateThread->setPaused(false);
            }
        }
    }
}

// TEACH 모드 토글 핸들러
void TeachingWidget::onTeachModeToggled(bool checked)
{
    teachingEnabled = checked;

    if (checked)
    {
        teachModeButton->setText("TEACH ON");
        teachModeButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_TEACH_OFF_COLOR, UIColors::BTN_TEACH_ON_COLOR, true));

        // TEACH ON 상태일 때 Save 버튼 활성화
        if (saveRecipeButton)
            saveRecipeButton->setEnabled(true);
        
        // 단일 뷰 모드로 전환
        if (cameraView)
            cameraView->setQuadViewMode(false);
        
        // UI 요소들 표시
        if (rightPanelOverlay)
            rightPanelOverlay->show();
        for (int i = 0; i < 4; i++)
        {
            if (previewOverlayLabels[i])
                previewOverlayLabels[i]->show();
        }
        if (logTextEdit)
            logTextEdit->parentWidget()->show();
        
        // 모든 버튼들 표시
        if (modeToggleButton) modeToggleButton->show();
        if (startCameraButton) startCameraButton->show();
        if (runStopButton) runStopButton->show();
        if (saveRecipeButton) saveRecipeButton->show();
        if (addPatternButton) addPatternButton->show();
        if (addFilterButton) addFilterButton->show();
        if (removeButton) removeButton->show();
        if (roiButton) roiButton->show();
        if (fidButton) fidButton->show();
        if (insButton) insButton->show();
        
        // TEACH ON 전환 시 미리보기 4개 갱신
        updatePreviewFrames();
    }
    else
    {
        teachModeButton->setText("TEACH OFF");
        teachModeButton->setStyleSheet(UIColors::overlayToggleButtonStyle(UIColors::BTN_TEACH_OFF_COLOR, UIColors::BTN_TEACH_ON_COLOR, false));

        // TEACH OFF 상태일 때 Save 버튼 비활성화
        if (saveRecipeButton)
            saveRecipeButton->setEnabled(false);
        
        // 4분할 뷰 모드로 전환
        if (cameraView)
            cameraView->setQuadViewMode(true);
        
        // UI 요소들 숨김
        if (rightPanelOverlay)
            rightPanelOverlay->hide();
        for (int i = 0; i < 4; i++)
        {
            if (previewOverlayLabels[i])
                previewOverlayLabels[i]->hide();
        }
        if (logTextEdit)
            logTextEdit->parentWidget()->hide();
        
        // TEACH OFF 버튼 빼고 모든 버튼 숨김
        if (modeToggleButton) modeToggleButton->hide();
        if (startCameraButton) startCameraButton->hide();
        if (runStopButton) runStopButton->hide();
        if (saveRecipeButton) saveRecipeButton->hide();
        if (addPatternButton) addPatternButton->hide();
        if (addFilterButton) addFilterButton->hide();
        if (removeButton) removeButton->hide();
        if (roiButton) roiButton->hide();
        if (fidButton) fidButton->hide();
        if (insButton) insButton->hide();
        
        // TEACH OFF 전환 시 미리보기 4개 갱신 (카메라별 획득 수 표시)
        updatePreviewFrames();
    }

    // 티칭 관련 버튼들 활성화/비활성화
    setTeachingButtonsEnabled(checked);
}

// 티칭 관련 버튼들 활성화/비활성화
void TeachingWidget::setTeachingButtonsEnabled(bool enabled)
{
    // 패턴 타입 버튼들
    if (roiButton)
        roiButton->setEnabled(enabled);
    if (fidButton)
        fidButton->setEnabled(enabled);
    if (insButton)
        insButton->setEnabled(enabled);

    // 편집 모드 버튼
    if (modeToggleButton)
        modeToggleButton->setEnabled(enabled);

    // 패턴 추가/삭제 버튼들
    if (addPatternButton)
        addPatternButton->setEnabled(enabled);
    if (removeButton)
        removeButton->setEnabled(enabled);
    if (addFilterButton)
        addFilterButton->setEnabled(enabled);

    // CameraView의 편집 모드 설정
    if (cameraView)
    {
        if (enabled)
        {
            // TEACH ON: 현재 모드에 따라 편집 모드 설정
            CameraView::EditMode currentMode = modeToggleButton && modeToggleButton->isChecked() ? CameraView::EditMode::Draw : CameraView::EditMode::Move;
            cameraView->setEditMode(currentMode);
        }
        else
        {
            // TEACH OFF: View 모드로 설정 (모든 편집 기능 차단)
            cameraView->setEditMode(CameraView::EditMode::View);
        }
    }
}

void TeachingWidget::toggleFullScreenMode()
{
    if (isFullScreenMode)
    {
        // 전체화면 -> 윈도우 모드 (타이틀바 유지)
        showNormal();
        setGeometry(windowedGeometry);
        isFullScreenMode = false;
    }
    else
    {
        // 윈도우 모드 -> 전체화면 (타이틀바 유지)
        windowedGeometry = geometry();
        showMaximized();
        isFullScreenMode = true;
    }
}

// 비동기 이미지 저장 함수
void TeachingWidget::saveImageAsync(const cv::Mat &frame, bool isPassed, int cameraIndex)
{
    if (frame.empty())
    {
        return;
    }

    // 이미지 복사 (비동기 작업에서 안전하게 사용)
    cv::Mat frameCopy = frame.clone();

    // QRunnable 람다로 비동기 작업 생성
    QThreadPool::globalInstance()->start([frameCopy, isPassed, cameraIndex]()
    {
        // 현재 날짜와 시간으로 폴더/파일명 생성
        QDateTime now = QDateTime::currentDateTime();
        QString dateFolder = now.toString("yyyyMMdd");  // 20260108
        QString timestamp = now.toString("yyyyMMdd_HHmmss_zzz");  // 20260108_150530_123
        
        // 저장 경로 설정: data/20260108/0/ (카메라 인덱스별 폴더)
        QString basePath = QString("../deploy/data/%1/%2").arg(dateFolder).arg(cameraIndex);
        
        // 디렉토리 생성 (없으면)
        QDir dir;
        if (!dir.exists(basePath)) {
            dir.mkpath(basePath);
        }
        
        // 파일 경로 생성: data/20260108/0/20260108_150530_123.png
        QString filePath = basePath + "/" + timestamp + ".png";
        
        // 이미지 저장
        std::vector<int> compression_params;
        compression_params.push_back(cv::IMWRITE_PNG_COMPRESSION);
        compression_params.push_back(3); // 압축 레벨 (0-9, 3은 중간)
        
        cv::imwrite(filePath.toStdString(), frameCopy, compression_params);
    });
}

void TeachingWidget::trainAnomalyPattern(const QString& patternName)
{
    if (!cameraView) return;
    
    // 기존 학습 폴더 삭제
    AnomalyWeightUtils::removeWeightFolder(patternName);
    
    // 패턴 이름으로 패턴 찾기
    PatternInfo* patternPtr = nullptr;
    const QList<PatternInfo>& allPatterns = cameraView->getPatterns();
    
    for (const PatternInfo& pattern : allPatterns) {
        if (pattern.type == PatternType::INS && 
            pattern.inspectionMethod == InspectionMethod::ANOMALY &&
            pattern.name == patternName) {
            patternPtr = const_cast<PatternInfo*>(&pattern);
            break;
        }
    }
    
    if (!patternPtr) {
        QMessageBox::warning(this, "경고", QString("패턴을 찾을 수 없습니다: %1").arg(patternName));
        return;
    }    
    // 패턴 정보 복사 (람다 캡처용)
    PatternInfo pattern = *patternPtr;
    
    // 부모 FID 패턴 정보 가져오기
    PatternInfo* parentFidPattern = nullptr;
    if (!pattern.parentId.isNull()) {
        parentFidPattern = cameraView->getPatternById(pattern.parentId);
    }
    
    // FID 템플릿 및 마스크 복사 (람다 캡처용)
    cv::Mat fidTemplate, fidMask;
    double fidTeachingAngle = 0.0;
    QPointF fidTeachingCenter;
    QPointF insTeachingCenter = pattern.rect.center();
    bool useFidMatching = false;
    
    if (parentFidPattern && parentFidPattern->type == PatternType::FID && 
        !parentFidPattern->matchTemplate.isNull()) {
        // QImage를 cv::Mat으로 변환
        QImage tempImg = parentFidPattern->matchTemplate.convertToFormat(QImage::Format_RGB888);
        fidTemplate = cv::Mat(tempImg.height(), tempImg.width(), CV_8UC3,
                              const_cast<uchar*>(tempImg.bits()), tempImg.bytesPerLine()).clone();
        cv::cvtColor(fidTemplate, fidTemplate, cv::COLOR_RGB2BGR);
        
        // 마스크가 있으면 변환
        if (!parentFidPattern->matchTemplateMask.isNull()) {
            QImage maskImg = parentFidPattern->matchTemplateMask.convertToFormat(QImage::Format_Grayscale8);
            fidMask = cv::Mat(maskImg.height(), maskImg.width(), CV_8UC1,
                              const_cast<uchar*>(maskImg.bits()), maskImg.bytesPerLine()).clone();
        }
        
        fidTeachingAngle = parentFidPattern->angle;
        fidTeachingCenter = parentFidPattern->rect.center();
        useFidMatching = true;
        
        qDebug() << "[ANOMALY TRAIN] FID 매칭 사용 - 부모 FID:" << parentFidPattern->name;
    } else {
        qDebug() << "[ANOMALY TRAIN] FID 매칭 없이 고정 좌표 사용";
    }
    
    // 폴더 선택 다이얼로그
    QString folderPath = CustomFileDialog::getExistingDirectory(this, "양품 이미지 폴더 선택", "");
    if (folderPath.isEmpty()) return;
    
    // 이미지 파일 목록 가져오기
    QDir dir(folderPath);
    QStringList filters;
    filters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp";
    QFileInfoList imageFiles = dir.entryInfoList(filters, QDir::Files);
    
    if (imageFiles.isEmpty()) {
        QMessageBox::warning(this, "경고", "폴더에 이미지 파일이 없습니다.");
        return;
    }
    
    // 임시 폴더 생성 (ROI 크롭 이미지 저장용)
    QString tempDir = QDir::temp().filePath(QString("anomaly_train_%1").arg(pattern.id.toString()));
    QString goodDir = tempDir + "/good";
    QDir().mkpath(goodDir);
    
    // ROI 크기 (고정)
    int roiW = static_cast<int>(pattern.rect.width());
    int roiH = static_cast<int>(pattern.rect.height());
    
    qDebug() << "[ANOMALY TRAIN] 학습 시작 - 패턴:" << pattern.name 
             << "ROI:" << roiW << "x" << roiH 
             << "이미지 수:" << imageFiles.size()
             << "FID 매칭:" << (useFidMatching ? "사용" : "미사용");
    
    // 학습 시작 시간 기록 (ROI 추출부터)
    QElapsedTimer *trainingTimer = new QElapsedTimer();
    trainingTimer->start();
    
    // 진행 다이얼로그
    QProgressDialog progress("Extracting ROI...", "Cancel", 0, imageFiles.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    progress.setStyleSheet(
        "QProgressDialog { background-color: #1e1e1e; color: #ffffff; }"
        "QWidget { background-color: #1e1e1e; color: #ffffff; }"
        "QPushButton { background-color: #2d2d2d; color: #ffffff; border: 1px solid #3d3d3d; padding: 5px; min-width: 80px; }"
        "QPushButton:hover { background-color: #3d3d3d; }"
        "QProgressBar { border: 1px solid #3d3d3d; background-color: #252525; color: #ffffff; text-align: center; }"
        "QProgressBar::chunk { background-color: #0d47a1; }"
        "QLabel { color: #ffffff; }"
    );
    
    int croppedCount = 0;
    int fidMatchFailCount = 0;
    
    for (int i = 0; i < imageFiles.size(); ++i) {
        // 경과 시간 계산
        qint64 elapsedMs = trainingTimer->elapsed();
        int elapsedSec = elapsedMs / 1000;
        int minutes = elapsedSec / 60;
        int seconds = elapsedSec % 60;
        QString timeStr = QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
        
        progress.setLabelText(QString("Extracting ROI... %1 / %2 [%3]")
            .arg(i + 1).arg(imageFiles.size()).arg(timeStr));
        progress.setValue(i);
        if (progress.wasCanceled()) break;
        
        QString imagePath = imageFiles[i].absoluteFilePath();
        cv::Mat image = cv::imread(imagePath.toStdString());
        
        if (image.empty()) {
            qWarning() << "[ANOMALY TRAIN] 이미지 로드 실패:" << imagePath;
            continue;
        }
        
        // ROI 좌표 계산 (FID 매칭 적용)
        int roiX, roiY;
        
        if (useFidMatching && !fidTemplate.empty()) {
            // FID 템플릿 매칭 수행
            cv::Mat result;
            int matchMethod = cv::TM_CCOEFF_NORMED;
            
            if (!fidMask.empty()) {
                cv::matchTemplate(image, fidTemplate, result, matchMethod, fidMask);
            } else {
                cv::matchTemplate(image, fidTemplate, result, matchMethod);
            }
            
            double minVal, maxVal;
            cv::Point minLoc, maxLoc;
            cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
            
            // 매칭 임계값 확인 (70% 이상)
            if (maxVal < 0.7) {
                fidMatchFailCount++;
                qWarning() << "[ANOMALY TRAIN] FID 매칭 실패 (score:" << maxVal << "):" << imagePath;
                continue;
            }
            
            // FID 매칭 위치에서 INS ROI 위치 계산
            // 검출된 FID 중심 = maxLoc + (템플릿 크기 / 2)
            double fidMatchCenterX = maxLoc.x + fidTemplate.cols / 2.0;
            double fidMatchCenterY = maxLoc.y + fidTemplate.rows / 2.0;
            
            // 티칭 시 FID 중심 -> INS 중심 상대 벡터
            double relativeX = insTeachingCenter.x() - fidTeachingCenter.x();
            double relativeY = insTeachingCenter.y() - fidTeachingCenter.y();
            
            // 새 INS 중심 = 검출된 FID 중심 + 상대 벡터
            double newInsCenterX = fidMatchCenterX + relativeX;
            double newInsCenterY = fidMatchCenterY + relativeY;
            
            // ROI 좌표 계산 (중심 기준)
            roiX = static_cast<int>(newInsCenterX - roiW / 2.0);
            roiY = static_cast<int>(newInsCenterY - roiH / 2.0);
        } else {
            // FID 매칭 없이 고정 좌표 사용
            roiX = static_cast<int>(pattern.rect.x());
            roiY = static_cast<int>(pattern.rect.y());
        }
        
        // ROI 범위 체크
        if (roiX < 0 || roiY < 0 || roiX + roiW > image.cols || roiY + roiH > image.rows) {
            qWarning() << "[ANOMALY TRAIN] ROI 범위 초과:" << imagePath 
                       << "ROI:(" << roiX << "," << roiY << "," << roiW << "," << roiH << ")";
            continue;
        }
        
        // ROI 크롭
        cv::Rect roiRect(roiX, roiY, roiW, roiH);
        cv::Mat croppedImage = image(roiRect).clone();
        
        // 저장
        QString outputPath = QString("%1/%2.png").arg(goodDir).arg(i, 4, 10, QChar('0'));
        cv::imwrite(outputPath.toStdString(), croppedImage);
        croppedCount++;
    }
    
    progress.setValue(imageFiles.size());
    
    if (croppedCount == 0) {
        QMessageBox::warning(this, "경고", "유효한 이미지가 없습니다.");
        QDir(tempDir).removeRecursively();
        delete trainingTimer;
        return;
    }
    
    qDebug() << "[ANOMALY TRAIN] ROI 크롭 완료:" << croppedCount << "개"
             << "(FID 매칭 실패:" << fidMatchFailCount << "개)";
    
    // 학습 실행 - 레시피별 weights 폴더
    QString recipesDir = QCoreApplication::applicationDirPath() + "/recipes";
    QString recipeDir = currentRecipeName.isEmpty() ? "default" : currentRecipeName;
    QString weightsBaseDir = recipesDir + "/" + recipeDir + "/weights";
    QString outputDir = weightsBaseDir + "/" + pattern.name;
    QDir().mkpath(outputDir);
    
    qDebug() << "[ANOMALY TRAIN] 학습 시작:";
    
    // 학습 진행 다이얼로그
    QProgressDialog *trainProgress = new QProgressDialog("Training model...", "Cancel", 0, 0, this);
    trainProgress->setWindowModality(Qt::WindowModal);
    trainProgress->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    trainProgress->setStyleSheet(
        "QProgressDialog { background-color: #1e1e1e; color: #ffffff; }"
        "QWidget { background-color: #1e1e1e; color: #ffffff; }"
        "QPushButton { background-color: #2d2d2d; color: #ffffff; border: 1px solid #3d3d3d; padding: 5px; min-width: 80px; }"
        "QPushButton:hover { background-color: #3d3d3d; }"
        "QProgressBar { border: 1px solid #3d3d3d; background-color: #252525; color: #ffffff; text-align: center; }"
        "QProgressBar::chunk { background-color: #0d47a1; }"
        "QLabel { color: #ffffff; }"
    );
    trainProgress->setMinimumDuration(0);
    trainProgress->setValue(0);
    trainProgress->setAutoClose(false);
    trainProgress->setAutoReset(false);
    
    // 새 프로세스 생성
    QProcess *process = new QProcess(this);
    process->setWorkingDirectory(QCoreApplication::applicationDirPath() + "/..");
    process->setProcessChannelMode(QProcess::MergedChannels);  // stdout + stderr 합침
    
    // 실시간 출력 읽기 및 진행률 표시
    connect(process, &QProcess::readyReadStandardOutput, [process, trainProgress, trainingTimer]() {
        QString output = process->readAllStandardOutput();
        QStringList lines = output.split('\n', Qt::SkipEmptyParts);
        
        // 경과 시간 계산
        qint64 elapsedMs = trainingTimer->elapsed();
        int elapsedSec = elapsedMs / 1000;
        int minutes = elapsedSec / 60;
        int seconds = elapsedSec % 60;
        QString timeStr = QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
        
        for (const QString& line : lines) {
            qDebug() << "[ANOMALY TRAIN]" << line;
            
            // \r로 시작하는 진행률 표시 (tqdm)
            if (line.startsWith("\r")) {
                QString cleanLine = line.mid(1).trimmed();
                
                // "Selecting Coreset Indices.:  99%|█████████▉| 5077/5120"
                if (cleanLine.contains("Selecting Coreset") || cleanLine.contains("Selecting")) {
                    QRegularExpression re(R"((\d+)/(\d+))");
                    QRegularExpressionMatch match = re.match(cleanLine);
                    if (match.hasMatch()) {
                        int current = match.captured(1).toInt();
                        int total = match.captured(2).toInt();
                        trainProgress->setLabelText(QString("Sampling... %1 / %2 (%3%) [%4]")
                            .arg(current).arg(total).arg(current * 100 / total).arg(timeStr));
                    }
                }
                // "Extracting features:  50%|█████     | 1/2"
                else if (cleanLine.contains("Extracting features") || cleanLine.contains("Extracting")) {
                    QRegularExpression re(R"((\d+)/(\d+))");
                    QRegularExpressionMatch match = re.match(cleanLine);
                    if (match.hasMatch()) {
                        int current = match.captured(1).toInt();
                        int total = match.captured(2).toInt();
                        trainProgress->setLabelText(QString("Extracting... %1 / %2 (%3%) [%4]")
                            .arg(current).arg(total).arg(current * 100 / total).arg(timeStr));
                    }
                }
                continue;
            }
            
            // 일반 메시지 파싱
            if (line.contains("Coreset Indices")) {
                QRegularExpression re(R"(Coreset Indices (\d+)/(\d+))");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch()) {
                    int current = match.captured(1).toInt();
                    int total = match.captured(2).toInt();
                    trainProgress->setLabelText(QString("Building... %1 / %2 (%3%) [%4]")
                        .arg(current).arg(total).arg(current * 100 / total).arg(timeStr));
                }
            }
            else if (line.contains("Converting to OpenVINO", Qt::CaseInsensitive)) {
                trainProgress->setLabelText(QString("Converting model... [%1]").arg(timeStr));
            }
            else if (line.contains("Building coreset", Qt::CaseInsensitive)) {
                trainProgress->setLabelText(QString("Building started... [%1]").arg(timeStr));
            }
            else if (line.contains("Computing normalization", Qt::CaseInsensitive)) {
                trainProgress->setLabelText(QString("Computing normalization... [%1]").arg(timeStr));
            }
            else if (line.contains("Exporting model", Qt::CaseInsensitive)) {
                trainProgress->setLabelText(QString("Exporting model... [%1]").arg(timeStr));
            }
            else if (line.contains("Starting training", Qt::CaseInsensitive)) {
                trainProgress->setLabelText(QString("Training started... [%1]").arg(timeStr));
            }
        }
    });
    
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, process, tempDir, pattern, outputDir, trainProgress, trainingTimer](int exitCode, QProcess::ExitStatus exitStatus) {
        
        // 총 소요 시간 계산
        qint64 totalMs = trainingTimer->elapsed();
        int totalSec = totalMs / 1000;
        int minutes = totalSec / 60;
        int seconds = totalSec % 60;
        QString totalTimeStr = QString("%1분 %2초").arg(minutes).arg(seconds);
        
        delete trainingTimer;  // 타이머 메모리 해제
        
        QString output = process->readAllStandardOutput();
        QString error = process->readAllStandardError();
        
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            // 기존 모델 해제 (새로 학습된 모델 강제 재로드를 위해)
            ImageProcessor::releasePatchCoreTensorRT();
            
            // 학습된 모델을 즉시 메모리에 적재 (정규화 통계도 자동 로드됨)
            QString fullModelPath = QCoreApplication::applicationDirPath() + QString("/weights/%1/%1.xml").arg(pattern.name);
            
            qDebug() << "[ANOMALY TRAIN] Training completed in" << totalTimeStr << "- Loading model:" << fullModelPath;
            
            if (ImageProcessor::initPatchCoreTensorRT(fullModelPath)) {
                qDebug() << "[ANOMALY TRAIN] Model loaded successfully!";
                CustomMessageBox msgBox(this, CustomMessageBox::Information, "Training Complete",
                    QString("Model training completed and loaded.\nPattern: %1\nPath: %2\nTime: %3")
                        .arg(pattern.name)
                        .arg(outputDir)
                        .arg(totalTimeStr));
                msgBox.exec();
            } else {
                qWarning() << "[ANOMALY TRAIN] Model load failed";
                CustomMessageBox msgBox(this, CustomMessageBox::Warning, "Training Complete",
                    QString("Training completed but model load failed.\nPattern: %1\nPath: %2\nTime: %3\n\nPlease reload the recipe.")
                        .arg(pattern.name)
                        .arg(outputDir)
                        .arg(totalTimeStr));
                msgBox.exec();
            }
            
            // Train 버튼을 Trained로 업데이트
            if (anomalyTrainButton) {
                anomalyTrainButton->setText("Trained");
                anomalyTrainButton->setStyleSheet(
                    "QPushButton { background-color: #f44336; color: white; font-weight: bold; padding: 5px; border-radius: 3px; }"
                    "QPushButton:hover { background-color: #da190b; }"
                    "QPushButton:pressed { background-color: #c0180a; }");
            }
        } else {
            qDebug() << "[ANOMALY TRAIN] Docker stdout:" << output;
            qDebug() << "[ANOMALY TRAIN] Docker stderr:" << error;
            CustomMessageBox msgBox(this, CustomMessageBox::Critical, "Training Failed",
                QString("Docker training failed (exit code: %1)\n\nError:\n%2")
                    .arg(exitCode)
                    .arg(error.isEmpty() ? output : error));
            msgBox.exec();
            qDebug() << "[ANOMALY TRAIN] Training failed:" << exitCode;
        }
        
        // 임시 폴더 삭제
        QDir(tempDir).removeRecursively();
        
        // 프로그레스 다이얼로그 정리
        if (trainProgress) {
            trainProgress->close();
            trainProgress->deleteLater();
        }
        
        // 프로세스 정리 (모든 시그널 연결 해제 후 삭제)
        process->disconnect();
        process->deleteLater();
    });
    
    // 취소 버튼 연결
    connect(trainProgress, &QProgressDialog::canceled, [process, trainProgress]() {
        if (process && process->state() == QProcess::Running) {
            process->disconnect();
            process->kill();
            process->waitForFinished(3000);
            process->deleteLater();
            QMessageBox::information(trainProgress, "취소됨", "학습이 취소되었습니다.");
        }
    });
    
    // TODO: 학습 스크립트 실행 로직 추가 필요
    // process->start(scriptPath, args);
    QMessageBox::warning(this, "미구현", "이 기능은 아직 구현되지 않았습니다.");
    trainProgress->close();
    trainProgress->deleteLater();
    delete trainingTimer;
    QDir(tempDir).removeRecursively();
    return;
    
    trainProgress->show();
    
    if (!process->waitForStarted()) {
        QString errorMsg = process->errorString();
        qDebug() << "[ANOMALY TRAIN] 실행 실패:" << errorMsg;
        
        // 프로그레스 다이얼로그 정리
        if (trainProgress) {
            trainProgress->close();
            trainProgress->deleteLater();
        }
        
        QMessageBox::critical(this, "오류", QString("학습 스크립트 실행 실패\n%1").arg(errorMsg));
        QDir(tempDir).removeRecursively();
        
        // 프로세스 정리
        process->disconnect();
        process->deleteLater();
        delete trainingTimer;
    }
}

// === 테스트 다이얼로그용 공용 메서드 ===
void TeachingWidget::setCameraFrame(int index, const cv::Mat& frame)
{
    if (frame.empty()) return;
    
    // cameraFrames 크기 확장
    if (index >= static_cast<int>(4)) {
    }
    
    cameraFrames[index] = frame.clone();
    
    // 화면 업데이트
    if (index == cameraIndex) {
        updateCameraFrame();
    }
}

InspectionResult TeachingWidget::runInspection()
{
    InspectionResult result;
    result.isPassed = false;
    
    if (!insProcessor) {
        qWarning() << "[runInspection] insProcessor가 없습니다.";
        return result;
    }
    
    if (!cameraView) {
        qWarning() << "[runInspection] cameraView가 없습니다.";
        return result;
    }
    
    // 현재 프레임 가져오기
    cv::Mat frame = getCurrentFrame();
    if (frame.empty()) {
        qWarning() << "[runInspection] 검사할 프레임이 없습니다.";
        return result;
    }
    
    // CameraView에서 패턴 리스트 가져오기
    const QList<PatternInfo>& patterns = cameraView->getPatterns();
    
    // 카메라 이름 가져오기 (시리얼 번호 사용)
    QString cameraName = (cameraIndex >= 0 && cameraIndex < cameraInfos.size()) ? cameraInfos[cameraIndex].serialNumber : "";
    
    // InsProcessor로 검사 실행
    result = insProcessor->performInspection(frame, patterns, cameraName);
    
    return result;
}

QString TeachingWidget::getPatternName(const QUuid& patternId) const
{
    if (!cameraView) {
        return "Unknown";
    }
    
    const QList<PatternInfo>& patterns = cameraView->getPatterns();
    for (const PatternInfo& pattern : patterns) {
        if (pattern.id == patternId) {
            return pattern.name;
        }
    }
    
    return "Unknown";
}

void TeachingWidget::triggerRunButton()
{
    if (runStopButton) {
        runStopButton->click();
    }
}
