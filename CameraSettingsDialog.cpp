#include "CameraSettingsDialog.h"
#include "CustomMessageBox.h"
#include <QDebug>
#include <chrono>
#include <thread>

CameraSettingsDialog::CameraSettingsDialog(QWidget* parent)
    : QDialog(parent)
    , currentCameraIndex(-1)
    , m_configManager(ConfigManager::instance())
    , isTriggerTesting(false)
    , liveImageThreadRunning(false)
    , triggerTestThreadRunning(false)
    , liveImageThread(nullptr)
    , triggerTestThread(nullptr)
    , triggerDetectionCount(0)
    , lastExposureCount(0)
{
    setWindowTitle("카메라 UserSet 관리");
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setMinimumSize(700, 450);
    
    setStyleSheet(
        "QDialog {"
        "    background-color: rgba(30, 30, 30, 240);"
        "    border: 2px solid rgba(100, 100, 100, 200);"
        "}"
        "QGroupBox {"
        "    color: white;"
        "    background-color: transparent;"
        "    border: 1px solid rgba(100, 100, 100, 150);"
        "    margin-top: 10px;"
        "    padding-top: 10px;"
        "}"
        "QGroupBox::title {"
        "    color: white;"
        "    subcontrol-origin: margin;"
        "    left: 10px;"
        "    padding: 0 5px;"
        "}"
        "QLabel {"
        "    color: white;"
        "    background-color: transparent;"
        "}"
        "QComboBox {"
        "    background-color: rgba(50, 50, 50, 180);"
        "    color: white;"
        "    border: 1px solid rgba(100, 100, 100, 150);"
        "    padding: 5px;"
        "}"
        "QComboBox::drop-down {"
        "    border: none;"
        "    width: 20px;"
        "}"
        "QComboBox::down-arrow {"
        "    image: none;"
        "    border-left: 5px solid transparent;"
        "    border-right: 5px solid transparent;"
        "    border-top: 5px solid white;"
        "    width: 0;"
        "    height: 0;"
        "    margin-right: 5px;"
        "}"
        "QComboBox QAbstractItemView {"
        "    background-color: rgba(50, 50, 50, 240);"
        "    color: white;"
        "    selection-background-color: rgba(70, 70, 70, 200);"
        "}"
        "QSpinBox, QDoubleSpinBox {"
        "    background-color: rgba(50, 50, 50, 180);"
        "    color: white;"
        "    border: 1px solid rgba(100, 100, 100, 150);"
        "    padding: 3px;"
        "}"
        "QSpinBox::up-button, QDoubleSpinBox::up-button {"
        "    border: none;"
        "    width: 16px;"
        "}"
        "QSpinBox::down-button, QDoubleSpinBox::down-button {"
        "    border: none;"
        "    width: 16px;"
        "}"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {"
        "    image: none;"
        "    border-left: 4px solid transparent;"
        "    border-right: 4px solid transparent;"
        "    border-bottom: 4px solid white;"
        "    width: 0;"
        "    height: 0;"
        "}"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {"
        "    image: none;"
        "    border-left: 4px solid transparent;"
        "    border-right: 4px solid transparent;"
        "    border-top: 4px solid white;"
        "    width: 0;"
        "    height: 0;"
        "}"
        "QPushButton {"
        "    background-color: rgba(70, 70, 70, 200);"
        "    color: white;"
        "    border: 1px solid rgba(100, 100, 100, 150);"
        "    padding: 8px 16px;"
        "    font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "    background-color: rgba(90, 90, 90, 220);"
        "}"
        "QPushButton:pressed {"
        "    background-color: rgba(60, 60, 60, 220);"
        "}"
    );
    
    setupUI();
}

CameraSettingsDialog::~CameraSettingsDialog() {
    qDebug() << "[CameraSettingsDialog] Destructor - Cleaning up resources";
    
    // 스레드 정지
    liveImageThreadRunning = false;
    triggerTestThreadRunning = false;
    isTriggerTesting = false;
    
    // 스레드 종료 대기
    if (liveImageThread) {
        if (liveImageThread->joinable()) {
            liveImageThread->join();
        }
        delete liveImageThread;
        liveImageThread = nullptr;
    }
    
    if (triggerTestThread) {
        if (triggerTestThread->joinable()) {
            triggerTestThread->join();
        }
        delete triggerTestThread;
        triggerTestThread = nullptr;
    }
    
#ifdef USE_SPINNAKER
    // 카메라 리소스 안전하게 해제
    for (auto& camera : m_spinCameras) {
        if (camera && camera->IsValid()) {
            try {
                if (camera->IsStreaming()) {
                    camera->EndAcquisition();
                }
                if (camera->IsInitialized()) {
                    camera->DeInit();
                }
            } catch (...) {
                // 소멸자에서는 예외 무시
            }
        }
    }
    m_spinCameras.clear();
#endif
    
    qDebug() << "[CameraSettingsDialog] Destructor completed";
}

int CameraSettingsDialog::exec() {
    if (parentWidget()) {
        QWidget* topWindow = parentWidget()->window();
        QRect parentRect = topWindow->frameGeometry();
        
        int x = parentRect.x() + (parentRect.width() - width()) / 2;
        int y = parentRect.y() + (parentRect.height() - height()) / 2;
        
        int titleBarHeight = topWindow->frameGeometry().height() - topWindow->geometry().height();
        y -= titleBarHeight / 2;
        
        move(x, y);
    }
    
    return QDialog::exec();
}

void CameraSettingsDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    
    qDebug() << "[CameraSettingsDialog] showEvent triggered";
    
#ifdef USE_SPINNAKER
    // 현재 UserSet 상태에 맞게 버튼 업데이트
    updateCurrentUserSetLabel();
    
    // 라이브 이미지 스레드 시작
    if (!liveImageThread || !liveImageThreadRunning) {
        liveImageThreadRunning = true;
        if (liveImageThread) {
            liveImageThread->join();
            delete liveImageThread;
        }
        liveImageThread = new std::thread([this]() {
            // Create and configure processor once to avoid per-frame overhead
            Spinnaker::ImageProcessor processor;
            try {
                processor.SetColorProcessing(Spinnaker::SPINNAKER_COLOR_PROCESSING_ALGORITHM_DIRECTIONAL_FILTER);
            } catch (...) {
                // ignore if processor doesn't support algorithm
            }
            
            // **★ 중요: 라이브 스레드 시작 직후 버퍼 완전히 비우기 ★**
            bool initialFlushDone = false;

            while (liveImageThreadRunning) {
                if (isTriggerTesting) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                int selectedCameraIndex = getSelectedCameraIndex();
                if (selectedCameraIndex < 0 || selectedCameraIndex >= static_cast<int>(m_spinCameras.size())) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                Spinnaker::CameraPtr camera = m_spinCameras[selectedCameraIndex];
                if (!camera) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                // quick check of UserSet, but don't throw on missing node
                try {
                    Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
                    auto pUserSetSelector = nodeMap.GetNode("UserSetSelector");
                    if (!pUserSetSelector || !Spinnaker::GenApi::IsReadable(pUserSetSelector)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        continue;
                    }

                    Spinnaker::GenApi::CEnumerationPtr userSetPtr(pUserSetSelector);
                    auto entry = userSetPtr->GetCurrentEntry();
                    QString currentUserSet = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
                    if (currentUserSet != "UserSet0") {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        initialFlushDone = false;  // 다른 UserSet이면 플러시 플래그 리셋
                        continue;
                    }
                } catch (...) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }

                try {
                    // Ensure streaming
                    try {
                        if (!camera->IsStreaming()) {
                            camera->BeginAcquisition();
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));  // 스트리밍 시작 대기
                        }
                    } catch (...) {
                        // ignore
                    }
                    
                    // **★ UserSet0 진입 직후 한 번만 버퍼 완전히 비우기 ★**
                    if (!initialFlushDone) {
                        qDebug() << "[LiveThread] 초기 버퍼 플러시 시작";
                        int flushedCount = 0;
                        try {
                            while (flushedCount < 50) {  // 최대 50개 프레임 제거
                                try {
                                    Spinnaker::ImagePtr oldImage = camera->GetNextImage(1);
                                    if (oldImage) {
                                        oldImage->Release();
                                        flushedCount++;
                                    } else {
                                        break;
                                    }
                                } catch (...) {
                                    break;
                                }
                            }
                            qDebug() << "[LiveThread] ✓ 초기 버퍼 플러시 완료 (" << flushedCount << "개 프레임 제거)";
                        } catch (...) {
                            qDebug() << "[LiveThread] 초기 버퍼 플러시 중 예외";
                        }
                        initialFlushDone = true;
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // 안정화 대기
                    }

                    // **★ 중요: 버퍼의 모든 오래된 프레임 완전히 제거, 최신 프레임만 획득 ★**
                    Spinnaker::ImagePtr pImage = nullptr;
                    Spinnaker::ImagePtr latestImage = nullptr;
                    
                    try {
                        // 버퍼에 쌓인 모든 프레임을 완전히 비우기 (무제한)
                        int flushedCount = 0;
                        while (true) {
                            try {
                                Spinnaker::ImagePtr tempImage = camera->GetNextImage(1);  // 1ms timeout
                                if (tempImage) {
                                    if (latestImage) {
                                        latestImage->Release();  // 이전 프레임 즉시 해제
                                    }
                                    latestImage = tempImage;  // 최신 프레임 유지
                                    flushedCount++;
                                } else {
                                    break;  // 더 이상 프레임 없음
                                }
                            } catch (...) {
                                break;  // 타임아웃 = 버퍼 비움 완료
                            }
                        }
                        
                        pImage = latestImage;  // 가장 최신 프레임만 사용
                    } catch (...) {
                        pImage = nullptr;
                    }

                    if (!pImage) {
                        // no frame available
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        continue;
                    }

                    if (pImage->IsIncomplete()) {
                        pImage->Release();
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        continue;
                    }

                    // Convert once using the prepared processor
                    Spinnaker::ImagePtr convertedImage;
                    try {
                        convertedImage = processor.Convert(pImage, Spinnaker::PixelFormat_BGR8);
                    } catch (...) {
                        convertedImage = pImage; // fallback: try using original
                    }

                    if (convertedImage && !convertedImage->IsIncomplete()) {
                        unsigned char* buffer = static_cast<unsigned char*>(convertedImage->GetData());
                        size_t width = convertedImage->GetWidth();
                        size_t height = convertedImage->GetHeight();

                        // copy/rescale on our thread
                        cv::Mat cvImage(height, width, CV_8UC3, buffer);
                        cv::Mat resized;
                        cv::resize(cvImage, resized, cv::Size(280, 210));
                        cv::Mat rgbImage;
                        cv::cvtColor(resized, rgbImage, cv::COLOR_BGR2RGB);

                        QImage qImage(rgbImage.data, rgbImage.cols, rgbImage.rows,
                                      static_cast<int>(rgbImage.step), QImage::Format_RGB888);
                        QPixmap pixmap = QPixmap::fromImage(qImage);

                        QMetaObject::invokeMethod(this, [this, pixmap]() {
                            if (triggerImageLabel) triggerImageLabel->setPixmap(pixmap);
                        }, Qt::QueuedConnection);
                    }

                    // release images
                    try { if (convertedImage && convertedImage != pImage) convertedImage->Release(); } catch(...){}
                    try { if (pImage) pImage->Release(); } catch(...){}

                } catch (const std::exception& e) {
                    qDebug() << "[LiveImage] Error:" << e.what();
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
        });
    }
    
    qDebug() << "[CameraSettingsDialog] Live image thread started";
#endif
}

void CameraSettingsDialog::closeEvent(QCloseEvent* event) {
    qDebug() << "[CameraSettingsDialog] closeEvent - Stopping threads and cleaning up cameras";
    
    // 1. 스레드 정지 플래그 먼저 설정 (모든 스레드가 loop을 빠져나가게 함)
    liveImageThreadRunning = false;
    triggerTestThreadRunning = false;
    isTriggerTesting = false;
    qDebug() << "[CameraSettingsDialog] Thread flags set to false";
    
    // 2. 스레드가 loop을 빠져나올 충분한 시간 확보 (최대 1초)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // 3. 라이브 영상 스레드 정리
    if (liveImageThread) {
        try {
            if (liveImageThread->joinable()) {
                qDebug() << "[CameraSettingsDialog] Joining live image thread...";
                liveImageThread->join();
                qDebug() << "[CameraSettingsDialog] Live image thread joined";
            }
            delete liveImageThread;
            liveImageThread = nullptr;
        } catch (const std::exception& e) {
            qWarning() << "[CameraSettingsDialog] Error in live thread cleanup:" << e.what();
        }
    }
    
    // 4. 트리거 테스트 스레드 정리
    if (triggerTestThread) {
        try {
            if (triggerTestThread->joinable()) {
                qDebug() << "[CameraSettingsDialog] Joining trigger test thread...";
                triggerTestThread->join();
                qDebug() << "[CameraSettingsDialog] Trigger test thread joined";
            }
            delete triggerTestThread;
            triggerTestThread = nullptr;
        } catch (const std::exception& e) {
            qWarning() << "[CameraSettingsDialog] Error in trigger thread cleanup:" << e.what();
        }
    }
    
#ifdef USE_SPINNAKER
    // 5. 카메라 리소스 정리 - 역순으로 안전하게 해제
    try {
        qDebug() << "[CameraSettingsDialog] Cleaning up" << m_spinCameras.size() << "cameras";
        
        // 역순으로 카메라 정리 (마지막 카메라부터)
        for (int i = static_cast<int>(m_spinCameras.size()) - 1; i >= 0; --i) {
            auto& camera = m_spinCameras[i];
            if (camera && camera->IsValid()) {
                try {
                    // Acquisition 중지
                    if (camera->IsStreaming()) {
                        qDebug() << "[CameraSettingsDialog] Stopping camera" << i << "acquisition";
                        camera->EndAcquisition();
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    
                    // DeInit - 카메라 리소스 해제
                    if (camera->IsInitialized()) {
                        qDebug() << "[CameraSettingsDialog] DeInitializing camera" << i;
                        camera->DeInit();
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    
                    qDebug() << "[CameraSettingsDialog] Camera" << i << "cleaned up successfully";
                } catch (const Spinnaker::Exception& e) {
                    qWarning() << "[CameraSettingsDialog] Spinnaker exception for camera" << i << ":" << e.what();
                } catch (const std::exception& e) {
                    qWarning() << "[CameraSettingsDialog] Exception for camera" << i << ":" << e.what();
                }
            }
            
            // 개별 카메라 참조 해제
            camera = nullptr;
        }
        
        // 6. 모든 카메라 참조 해제
        qDebug() << "[CameraSettingsDialog] Clearing all camera references";
        m_spinCameras.clear();
        
        // 7. 참조 완전히 해제되도록 대기
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        
        qDebug() << "[CameraSettingsDialog] Camera cleanup completed";
    } catch (const std::exception& e) {
        qWarning() << "[CameraSettingsDialog] Error in camera cleanup:" << e.what();
    }
#endif
    
    qDebug() << "[CameraSettingsDialog] All cleanup completed";
    QDialog::closeEvent(event);
}

void CameraSettingsDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // 카메라 선택 그룹
    QGroupBox* cameraGroup = new QGroupBox("카메라 선택", this);
    QVBoxLayout* cameraLayout = new QVBoxLayout(cameraGroup);
    
    QHBoxLayout* cameraSelectLayout = new QHBoxLayout;
    cameraSelectLayout->addWidget(new QLabel("카메라:", this));
    
    cameraCombo = new QComboBox(this);
    cameraCombo->addItem("카메라를 검색 중...");
    connect(cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            [this](int index) { 
                currentCameraIndex = index;
#ifdef USE_SPINNAKER
                updateCurrentUserSetLabel();
#endif
            });
    cameraSelectLayout->addWidget(cameraCombo);
    
    // 전단/후단 교체 버튼 추가
    QPushButton* swapCameraBtn = new QPushButton("전단↔후단 교체", this);
    swapCameraBtn->setMinimumWidth(180);
    swapCameraBtn->setMaximumWidth(220);
    connect(swapCameraBtn, &QPushButton::clicked, [this]() {
#ifdef USE_SPINNAKER
        if (m_spinCameras.size() >= 2) {
            // 카메라 위치 교체
            std::swap(m_spinCameras[0], m_spinCameras[1]);
            setSpinnakerCameras(m_spinCameras);
            emit camerasSwapped();  // 시그널 발생
            CustomMessageBox(this, CustomMessageBox::Information, "카메라 교체", "전단과 후단 카메라가 교체되었습니다.").exec();
        }
#endif
    });
    cameraSelectLayout->addWidget(swapCameraBtn);
    cameraSelectLayout->addStretch();
    
    cameraLayout->addLayout(cameraSelectLayout);
    
    mainLayout->addWidget(cameraGroup);
    
    // UserSet 설정 그룹 추가
    setupUserSetSettings();
    mainLayout->addWidget(userSetGroup);
    
    // 트리거 테스트 그룹 추가
    setupTriggerTestUI();
    mainLayout->addWidget(triggerTestGroup);
    
    // 버튼들
    QHBoxLayout* buttonLayout = new QHBoxLayout;
    QPushButton* closeBtn = new QPushButton("닫기", this);
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeBtn);
    
    mainLayout->addLayout(buttonLayout);
    
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

int CameraSettingsDialog::getSelectedCameraIndex() const {
    return currentCameraIndex;
}

#ifdef USE_SPINNAKER
void CameraSettingsDialog::setSpinnakerCameras(const std::vector<Spinnaker::CameraPtr>& cameras) {
    m_spinCameras = cameras;
    
    cameraCombo->clear();
    
    if (cameras.empty()) {
        cameraCombo->addItem("카메라가 검색되지 않음");
        return;
    }
    
    for (size_t i = 0; i < cameras.size(); ++i) {
        try {
            auto camera = cameras[i];
            if (camera && camera->IsInitialized()) {
                Spinnaker::GenApi::INodeMap& nodeMap = camera->GetTLDeviceNodeMap();
                Spinnaker::GenApi::CStringPtr ptrDeviceSerialNumber = nodeMap.GetNode("DeviceSerialNumber");
                Spinnaker::GenApi::CStringPtr ptrDeviceModelName = nodeMap.GetNode("DeviceModelName");
                
                QString serialNumber = "Unknown";
                QString modelName = "Unknown";
                
                if (IsReadable(ptrDeviceSerialNumber)) {
                    serialNumber = QString::fromLocal8Bit(ptrDeviceSerialNumber->GetValue().c_str());
                }
                if (IsReadable(ptrDeviceModelName)) {
                    modelName = QString::fromLocal8Bit(ptrDeviceModelName->GetValue().c_str());
                }
                
                // 카메라 2대 고정: 0번=전단, 1번=후단
                QString cameraName = (i == 0) ? "전단" : "후단";
                cameraCombo->addItem(QString("%1 (%2 - %3)").arg(cameraName, modelName, serialNumber));
            } else {
                QString cameraName = (i == 0) ? "전단" : "후단";
                cameraCombo->addItem(QString("%1: 초기화되지 않음").arg(cameraName));
            }
        } catch (const std::exception& e) {
            QString cameraName = (i == 0) ? "전단" : "후단";
            cameraCombo->addItem(QString("%1: 오류").arg(cameraName));
        }
    }
    
    currentCameraIndex = 0;
}
#endif

void CameraSettingsDialog::setupUserSetSettings() {
    userSetGroup = new QGroupBox("UserSet 설정", this);
    QVBoxLayout* userSetLayout = new QVBoxLayout(userSetGroup);
    
    // UserSet 로드 버튼
    QHBoxLayout* buttonLayout = new QHBoxLayout;
    
    loadUserSet0Btn = new QPushButton("LIVE 모드 (UserSet0)", this);
    loadUserSet0Btn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 10px; }");
    buttonLayout->addWidget(loadUserSet0Btn);
    
    loadUserSet1Btn = new QPushButton("TRIGGER 모드 (UserSet1)", this);
    loadUserSet1Btn->setStyleSheet("QPushButton { background-color: #FF9800; color: white; font-weight: bold; padding: 10px; }");
    buttonLayout->addWidget(loadUserSet1Btn);
    
    userSetLayout->addLayout(buttonLayout);
    
    // 현재 UserSet 상태
    QHBoxLayout* statusLayout = new QHBoxLayout;
    statusLayout->addWidget(new QLabel("현재 설정:", this));
    currentUserSetLabel = new QLabel("카메라 선택 필요", this);
    currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: white; }");
    statusLayout->addWidget(currentUserSetLabel);
    statusLayout->addStretch();
    
    userSetLayout->addLayout(statusLayout);
    
    // 카메라 자동 연결 체크박스
    cameraAutoConnectCheckBox = new QCheckBox("프로그램 시작 시 자동으로 카메라 연결 (CAM ON)", this);
    cameraAutoConnectCheckBox->setStyleSheet("QCheckBox { color: white; font-size: 12px; }");
    cameraAutoConnectCheckBox->setChecked(m_configManager->getCameraAutoConnect());
    userSetLayout->addWidget(cameraAutoConnectCheckBox);
    
    // 체크박스 상태 변경 시그널 연결
    connect(cameraAutoConnectCheckBox, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) {
        bool enabled = (state == Qt::Checked);
        m_configManager->setCameraAutoConnect(enabled);
        qDebug() << "[CameraSettingsDialog] 카메라 자동 연결 설정:" << enabled;
    });
    
    // 시그널 연결
    connect(loadUserSet0Btn, &QPushButton::clicked, this, &CameraSettingsDialog::onLoadUserSet0);
    connect(loadUserSet1Btn, &QPushButton::clicked, this, &CameraSettingsDialog::onLoadUserSet1);
}

void CameraSettingsDialog::setupTriggerTestUI() {
    triggerTestGroup = new QGroupBox("트리거 테스트", this);
    QVBoxLayout* mainLayout = new QVBoxLayout(triggerTestGroup);
    
    // 메인 가로 레이아웃: 좌측(컨트롤) + 우측(이미지)
    QHBoxLayout* contentLayout = new QHBoxLayout;
    
    // ===== 좌측: 컨트롤 영역 =====
    QVBoxLayout* leftLayout = new QVBoxLayout;
    
    // 상단: 토글 버튼 + 상태 표시기
    QHBoxLayout* headerLayout = new QHBoxLayout;
    
    // 토글 버튼
    triggerToggleBtn = new QPushButton("트리거 테스트 시작", this);
    triggerToggleBtn->setStyleSheet(
        "QPushButton { "
        "    background-color: #2196F3; "
        "    color: white; "
        "    font-weight: bold; "
        "    padding: 12px; "
        "    border-radius: 5px; "
        "    font-size: 12px; "
        "} "
        "QPushButton:hover { background-color: #1976D2; } "
        "QPushButton:pressed { background-color: #1565C0; }"
    );
    triggerToggleBtn->setFixedHeight(50);
    triggerToggleBtn->setMinimumWidth(150);
    headerLayout->addWidget(triggerToggleBtn);
    
    // 상태 표시기 (초록색 사각형)
    triggerIndicatorLabel = new QLabel(this);
    triggerIndicatorLabel->setFixedSize(60, 50);
    triggerIndicatorLabel->setStyleSheet(
        "QLabel { "
        "    background-color: rgba(60, 60, 60, 180); "
        "    border: 2px solid rgba(100, 100, 100, 150); "
        "    border-radius: 5px; "
        "    font-weight: bold; "
        "    color: white; "
        "    font-size: 11px; "
        "    qproperty-alignment: AlignCenter; "
        "}"
    );
    triggerIndicatorLabel->setText("Ready!");
    headerLayout->addWidget(triggerIndicatorLabel);
    
    // 상태 텍스트 레이블 (고정 높이)
    triggerStatusLabel = new QLabel("대기 중", this);
    triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: white; font-size: 13px; }");
    triggerStatusLabel->setFixedHeight(50);
    triggerStatusLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    headerLayout->addWidget(triggerStatusLabel);
    
    headerLayout->addStretch();
    
    leftLayout->addLayout(headerLayout);
    
    // 트리거 정보 표시 영역
    QGridLayout* infoLayout = new QGridLayout;
    
    // 라인 정보
    infoLayout->addWidget(new QLabel("트리거 라인:", this), 0, 0);
    triggerLineLabel = new QLabel("-", this);
    triggerLineLabel->setStyleSheet("QLabel { font-weight: bold; color: white; }");
    infoLayout->addWidget(triggerLineLabel, 0, 1);
    
    // 엣지 정보
    infoLayout->addWidget(new QLabel("트리거 엣지:", this), 1, 0);
    triggerEdgeLabel = new QLabel("-", this);
    triggerEdgeLabel->setStyleSheet("QLabel { font-weight: bold; color: white; }");
    infoLayout->addWidget(triggerEdgeLabel, 1, 1);
    
    // 활성화 정보
    infoLayout->addWidget(new QLabel("트리거 활성화:", this), 2, 0);
    triggerActivationLabel = new QLabel("-", this);
    triggerActivationLabel->setStyleSheet("QLabel { font-weight: bold; color: white; }");
    infoLayout->addWidget(triggerActivationLabel, 2, 1);
    
    // 감지 횟수
    infoLayout->addWidget(new QLabel("감지 횟수:", this), 3, 0);
    triggerCountLabel = new QLabel("0", this);
    triggerCountLabel->setStyleSheet("QLabel { font-weight: bold; color: white; font-size: 14px; }");
    infoLayout->addWidget(triggerCountLabel, 3, 1);
    
    infoLayout->setColumnStretch(1, 1);
    leftLayout->addLayout(infoLayout);
    leftLayout->addStretch();
    
    // ===== 우측: 이미지 표시 영역 =====
    QVBoxLayout* rightLayout = new QVBoxLayout;
    rightLayout->addWidget(new QLabel("라이브/트리거 이미지:", this));
    
    triggerImageLabel = new QLabel(this);
    triggerImageLabel->setFixedSize(280, 210);
    triggerImageLabel->setStyleSheet(
        "QLabel { "
        "    background-color: rgba(26, 26, 26, 200); "
        "    border: 2px solid rgba(100, 100, 100, 150); "
        "    border-radius: 5px; "
        "    qproperty-alignment: AlignCenter; "
        "    color: white; "
        "    font-size: 11px; "
        "}"
    );
    triggerImageLabel->setText("영상 대기 중...");
    rightLayout->addWidget(triggerImageLabel);
    rightLayout->addStretch();
    
    // 메인 레이아웃에 좌우 레이아웃 추가
    contentLayout->addLayout(leftLayout, 1);
    contentLayout->addLayout(rightLayout, 0);
    
    mainLayout->addLayout(contentLayout);
    
    // 시그널 연결 (토글)
    connect(triggerToggleBtn, &QPushButton::clicked, this, [this]() {
        if (isTriggerTesting) {
            onStopTriggerTest();
        } else {
            onStartTriggerTest();
        }
    });
}

#ifdef USE_SPINNAKER
void CameraSettingsDialog::updateCurrentUserSetLabel() {
    int selectedCameraIndex = getSelectedCameraIndex();
    if (selectedCameraIndex < 0 || selectedCameraIndex >= static_cast<int>(m_spinCameras.size())) {
        currentUserSetLabel->setText("카메라 선택 필요");
        currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #FF9800; }");
        
        // 버튼 스타일 초기화
        loadUserSet0Btn->setStyleSheet("QPushButton { background-color: #999; color: white; font-weight: bold; padding: 10px; }");
        loadUserSet1Btn->setStyleSheet("QPushButton { background-color: #999; color: white; font-weight: bold; padding: 10px; }");
        return;
    }

    try {
        Spinnaker::CameraPtr camera = m_spinCameras[selectedCameraIndex];
        Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
        
        // UserSetSelector 노드 가져오기
        Spinnaker::GenApi::CEnumerationPtr ptrUserSetSelector = nodeMap.GetNode("UserSetSelector");
        if (!IsAvailable(ptrUserSetSelector) || !IsReadable(ptrUserSetSelector)) {
            currentUserSetLabel->setText("조회 불가");
            currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #f44336; }");
            
            // 버튼 스타일 초기화
            loadUserSet0Btn->setStyleSheet("QPushButton { background-color: #999; color: white; font-weight: bold; padding: 10px; }");
            loadUserSet1Btn->setStyleSheet("QPushButton { background-color: #999; color: white; font-weight: bold; padding: 10px; }");
            return;
        }
        
        // 현재 UserSet 항목 가져오기
        auto entry = ptrUserSetSelector->GetCurrentEntry();
        if (IsReadable(entry)) {
            QString currentUserSet = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
            QString modeName;
            
            if (currentUserSet == "UserSet0") {
                modeName = "LIVE 모드";
                // UserSet0 적용 중 - 초록색
                currentUserSetLabel->setText(QString("%1 (%2)").arg(currentUserSet).arg(modeName));
                currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #4CAF50; }");
                loadUserSet0Btn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 10px; }");
                loadUserSet1Btn->setStyleSheet("QPushButton { background-color: #f44336; color: white; font-weight: bold; padding: 10px; }");
            } else if (currentUserSet == "UserSet1") {
                modeName = "TRIGGER 모드";
                // UserSet1 적용 중 - 초록색
                currentUserSetLabel->setText(QString("%1 (%2)").arg(currentUserSet).arg(modeName));
                currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #FF9800; }");
                loadUserSet0Btn->setStyleSheet("QPushButton { background-color: #f44336; color: white; font-weight: bold; padding: 10px; }");
                loadUserSet1Btn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 10px; }");
            } else {
                modeName = "기타";
                currentUserSetLabel->setText(QString("%1 (%2)").arg(currentUserSet).arg(modeName));
                currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #666; }");
                loadUserSet0Btn->setStyleSheet("QPushButton { background-color: #f44336; color: white; font-weight: bold; padding: 10px; }");
                loadUserSet1Btn->setStyleSheet("QPushButton { background-color: #f44336; color: white; font-weight: bold; padding: 10px; }");
            }
        } else {
            currentUserSetLabel->setText("조회 불가");
            currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #f44336; }");
            
            // 버튼 스타일 초기화
            loadUserSet0Btn->setStyleSheet("QPushButton { background-color: #999; color: white; font-weight: bold; padding: 10px; }");
            loadUserSet1Btn->setStyleSheet("QPushButton { background-color: #999; color: white; font-weight: bold; padding: 10px; }");
        }
    } catch (const std::exception& e) {
        currentUserSetLabel->setText("오류");
        currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #f44336; }");
        
        // 버튼 스타일 초기화
        loadUserSet0Btn->setStyleSheet("QPushButton { background-color: #999; color: white; font-weight: bold; padding: 10px; }");
        loadUserSet1Btn->setStyleSheet("QPushButton { background-color: #999; color: white; font-weight: bold; padding: 10px; }");
        qDebug() << "Error updating UserSet label:" << e.what();
    }
}
#endif

#ifdef USE_SPINNAKER
void CameraSettingsDialog::loadUserSet(const QString& userSetName, const QString& modeName) {
    int selectedCameraIndex = getSelectedCameraIndex();
    if (selectedCameraIndex < 0 || selectedCameraIndex >= static_cast<int>(m_spinCameras.size())) {
        QMessageBox::warning(this, "경고", "유효한 카메라를 선택해주세요.");
        return;
    }

    // **중요**: 라이브 이미지 스레드 임시 중지 (UserSet 로드 중 충돌 방지)
    bool wasLiveThreadRunning = liveImageThreadRunning;
    liveImageThreadRunning = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // 스레드가 멈출 시간 확보

    try {
        Spinnaker::CameraPtr camera = m_spinCameras[selectedCameraIndex];
        Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
        
        // **현재 UserSet 확인** - 이미 로드된 UserSet과 같으면 스킵
        try {
            Spinnaker::GenApi::CEnumerationPtr pUserSetSelector = nodeMap.GetNode("UserSetSelector");
            if (pUserSetSelector && Spinnaker::GenApi::IsReadable(pUserSetSelector)) {
                auto currentEntry = pUserSetSelector->GetCurrentEntry();
                if (currentEntry) {
                    QString currentUserSet = QString::fromLocal8Bit(currentEntry->GetSymbolic().c_str());
                    if (currentUserSet == userSetName) {
                        qDebug() << "[UserSet] " << userSetName << " 이미 로드됨 - 스킵";
                        CustomMessageBox(this, CustomMessageBox::Information, "정보", 
                                       QString("%1 (%2)은 이미 로드되어 있습니다.").arg(modeName, userSetName)).exec();
                        // 라이브 스레드 복구
                        if (wasLiveThreadRunning) {
                            liveImageThreadRunning = true;
                        }
                        return;
                    }
                }
            }
        } catch (...) {
            qDebug() << "[UserSet] 현재 UserSet 확인 실패";
        }
        
        // 카메라 스트리밍 상태 확인 및 정지 (UserSet1 전환 시에만)
        // UserSet0 (라이브 모드)는 연속 스트리밍이 필요하므로 유지
        bool wasStreaming = false;
        if (userSetName == "UserSet1" && camera->IsStreaming()) {
            wasStreaming = true;
            qDebug() << "[UserSet] UserSet1으로 전환 - EndAcquisition 시작";
            try {
                camera->EndAcquisition();
            } catch (const std::exception& e) {
                qDebug() << "[UserSet] EndAcquisition 예외:" << e.what();
            }
            
            // **중요**: EndAcquisition 후 충분한 시간 필요
            // Spinnaker SDK에서 카메라 상태 변경 후 다음 작업 전 필요한 대기시간
            qDebug() << "[UserSet] 1초 초기 대기 시작";
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            
            // 실제로 스트리밍이 중지되었는지 확인
            int retries = 0;
            while (camera->IsStreaming() && retries < 30) {  // 최대 3초 추가 대기
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                retries++;
            }
            
            if (camera->IsStreaming()) {
                qWarning() << "[UserSet] ⚠️ 경고: " << (1000 + retries * 100) << "ms 후에도 여전히 streaming 중!";
                // 여전히 streaming 중이면 한 번 더 시도
                qDebug() << "[UserSet] 추가 500ms 대기";
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            } else {
                qDebug() << "[UserSet] ✓ 성공: " << (1000 + retries * 100) << "ms 후 streaming 중지됨";
            }
        } else if (userSetName == "UserSet0") {
            qDebug() << "[UserSet] UserSet0 (라이브 모드) - 스트리밍 유지";
            wasStreaming = camera->IsStreaming();
        } else {
            qDebug() << "[UserSet] 카메라 streaming 중 아님 - 즉시 진행";
            wasStreaming = camera->IsStreaming();
        }
        
        // UserSetSelector 노드 가져오기
        Spinnaker::GenApi::CEnumerationPtr ptrUserSetSelector = nodeMap.GetNode("UserSetSelector");
        if (!IsAvailable(ptrUserSetSelector) || !IsWritable(ptrUserSetSelector)) {
            throw std::runtime_error("UserSetSelector 노드를 사용할 수 없습니다.");
        }
        
        // 해당 UserSet 선택
        Spinnaker::GenApi::CEnumEntryPtr ptrUserSet = ptrUserSetSelector->GetEntryByName(userSetName.toStdString().c_str());
        if (!IsAvailable(ptrUserSet) || !IsReadable(ptrUserSet)) {
            throw std::runtime_error(QString("%1을 사용할 수 없습니다.").arg(userSetName).toStdString());
        }
        ptrUserSetSelector->SetIntValue(ptrUserSet->GetValue());
        
        // UserSetLoad 노드 시도
        Spinnaker::GenApi::CCommandPtr ptrUserSetLoad = nodeMap.GetNode("UserSetLoad");
        bool userSetLoadExecuted = false;
        
        // **★ 중요: UserSetLoad 실행 전 스트리밍 완전 정지 확인 ★**
        if (camera->IsStreaming()) {
            qDebug() << "[UserSet] 스트리밍 중 감지 - EndAcquisition 실행";
            try {
                camera->EndAcquisition();
            } catch (const std::exception& e) {
                qDebug() << "[UserSet] EndAcquisition 예외:" << e.what();
            }
            
            // 스트리밍 완전 정지까지 대기 (최대 2초)
            int waitCount = 0;
            while (camera->IsStreaming() && waitCount < 20) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                waitCount++;
            }
            
            if (camera->IsStreaming()) {
                qWarning() << "[UserSet] ⚠️ 경고: " << (waitCount * 100) << "ms 후에도 스트리밍 중!";
            } else {
                qDebug() << "[UserSet] ✓ 스트리밍 정지 완료 (" << (waitCount * 100) << "ms)";
            }
            
            // 추가 안정화 대기
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        
        // **UserSet0 전환 시 사전 작업**: TriggerMode 미리 끄기
        if (userSetName == "UserSet0") {
            try {
                auto pTriggerMode = nodeMap.GetNode("TriggerMode");
                if (pTriggerMode && Spinnaker::GenApi::IsWritable(pTriggerMode)) {
                    Spinnaker::GenApi::CEnumerationPtr triggerModePtr(pTriggerMode);
                    auto offEntry = triggerModePtr->GetEntryByName("Off");
                    if (offEntry) {
                        triggerModePtr->SetIntValue(offEntry->GetValue());
                        qDebug() << "[UserSet] UserSet0 로드 전 TriggerMode Off로 설정";
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));  // 충분히 대기
                    }
                }
            } catch (const std::exception& e) {
                qDebug() << "[UserSet] UserSet0 전 TriggerMode 설정 실패 (무시 가능):" << e.what();
            }
        }

        if (ptrUserSetLoad) {
            try {
                // UserSet 로드 실행
                ptrUserSetLoad->Execute();
                userSetLoadExecuted = true;
                qDebug() << "[UserSet] UserSetLoad executed successfully";
            } catch (const std::exception& e) {
                qWarning() << "[UserSet] UserSetLoad execution failed:" << e.what();
                // 폴백: 쓰기 불가 또는 액세스 예외가 나면 스트리밍을 일시 중지하고 재시도
                try {
                    if (camera->IsStreaming()) {
                        qDebug() << "[UserSet] UserSetLoad 실패 - 스트리밍 일시중지 후 재시도";
                        try {
                            camera->EndAcquisition();
                        } catch (const std::exception& ex) {
                            qDebug() << "[UserSet] EndAcquisition 예외(재시도 전):" << ex.what();
                        }
                        // 더 긴 대기로 카메라 상태 안정화
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        try {
                            ptrUserSetLoad->Execute();
                            userSetLoadExecuted = true;
                            qDebug() << "[UserSet] UserSetLoad executed successfully after temporary EndAcquisition";
                        } catch (const std::exception& ex2) {
                            qWarning() << "[UserSet] UserSetLoad retry failed:" << ex2.what();
                        } catch (...) {
                            qWarning() << "[UserSet] UserSetLoad retry failed (unknown error)";
                        }

                        // UserSet0(라이브)로 전환하려는 경우라면 재시도 후 즉시 BeginAcquisition 시도
                        if (userSetName == "UserSet0" && userSetLoadExecuted) {
                            try {
                                camera->BeginAcquisition();
                                qDebug() << "[UserSet] UserSet0 로드 후 BeginAcquisition 호출(재시도 경로)";
                            } catch (const std::exception& ex3) {
                                qDebug() << "[UserSet] BeginAcquisition 예외(재시도 경로):" << ex3.what();
                            }
                        }
                    }
                } catch (...) {
                    qWarning() << "[UserSet] UserSetLoad 재시도 중 알 수 없는 예외 발생";
                }
                // 계속 진행
            } catch (...) {
                qWarning() << "[UserSet] UserSetLoad execution failed (unknown error)";
                // 폴백: 시도해보고 계속 진행
                try {
                    if (camera->IsStreaming()) {
                        camera->EndAcquisition();
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        try {
                            ptrUserSetLoad->Execute();
                            userSetLoadExecuted = true;
                            qDebug() << "[UserSet] UserSetLoad executed successfully after temporary EndAcquisition (unknown-ex)";
                        } catch (...) {
                            qWarning() << "[UserSet] UserSetLoad retry failed (unknown error)";
                        }
                    }
                } catch (...) {}
            }
        } else {
            qDebug() << "[UserSet] UserSetLoad 노드 없음";
        }
        
        // **중요**: UserSet 적용 후 충분한 지연 추가
        // 카메라가 새로운 UserSet을 적용하는데 필요한 시간
        // UserSet0(라이브 모드)의 경우 더 긴 시간 필요
        if (userSetName == "UserSet0") {
            std::this_thread::sleep_for(std::chrono::milliseconds(800));  // 라이브 모드는 800ms
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));  // 트리거 모드는 500ms
        }
        
        // ===== UserSet Default 및 FileAccess 설정 =====
        // SpinView와 동일하게 UserSetDefault와 FileAccess를 설정
        try {
            // UserSetDefault 설정 (기본 로드할 UserSet 지정)
            auto pUserSetDefault = nodeMap.GetNode("UserSetDefault");
            if (pUserSetDefault && Spinnaker::GenApi::IsWritable(pUserSetDefault)) {
                Spinnaker::GenApi::CEnumerationPtr userSetDefaultPtr(pUserSetDefault);
                auto entry = userSetDefaultPtr->GetEntryByName(userSetName.toStdString().c_str());
                if (entry && Spinnaker::GenApi::IsReadable(entry)) {
                    userSetDefaultPtr->SetIntValue(entry->GetValue());
                    qDebug() << "[UserSet] UserSetDefault set to:" << userSetName;
                }
            }
            
            // FileAccess 설정 (파일 접근 권한 설정)
            auto pFileAccess = nodeMap.GetNode("FileAccessControl");
            if (pFileAccess && Spinnaker::GenApi::IsWritable(pFileAccess)) {
                Spinnaker::GenApi::CEnumerationPtr fileAccessPtr(pFileAccess);
                auto readWriteEntry = fileAccessPtr->GetEntryByName("Read/Write");
                if (readWriteEntry && Spinnaker::GenApi::IsReadable(readWriteEntry)) {
                    fileAccessPtr->SetIntValue(readWriteEntry->GetValue());
                    qDebug() << "[UserSet] FileAccessControl set to Read/Write";
                }
            } else {
                // FileAccessControl 대신 다른 파일 접근 노드 시도
                auto pFileOpen = nodeMap.GetNode("FileOpeningMode");
                if (pFileOpen && Spinnaker::GenApi::IsWritable(pFileOpen)) {
                    Spinnaker::GenApi::CEnumerationPtr fileOpenPtr(pFileOpen);
                    auto readWriteEntry = fileOpenPtr->GetEntryByName("Read/Write");
                    if (readWriteEntry && Spinnaker::GenApi::IsReadable(readWriteEntry)) {
                        fileOpenPtr->SetIntValue(readWriteEntry->GetValue());
                        qDebug() << "[UserSet] FileOpeningMode set to Read/Write";
                    }
                }
            }
            
        } catch (const std::exception& e) {
            qDebug() << "[UserSet] Warning: UserSetDefault/FileAccess setting error:" << e.what();
        }
        
        // ===== 중요: UserSetLoad 후 노드맵 갱신 =====
        // Spinnaker SDK는 UserSet 로드 후 노드 캐시를 갱신해야 함

        try {
            // 노드맵 캐시 무효화 및 재로드
            auto pCommand = nodeMap.GetNode("DeviceRegistersStreamingStart");
            if (pCommand && Spinnaker::GenApi::IsWritable(pCommand)) {
                Spinnaker::GenApi::CCommandPtr cmdPtr(pCommand);
                cmdPtr->Execute();
                qDebug() << "[UserSet] DeviceRegistersStreamingStart executed";
            }
        } catch (...) {
            qDebug() << "[UserSet] Warning: Could not execute DeviceRegistersStreamingStart";
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // UserSet 로드 후 현재 설정 확인 (읽기만, 변경하지 않음)
        try {
            // TriggerMode 확인 (읽기만)
            auto pTriggerMode = nodeMap.GetNode("TriggerMode");
            if (pTriggerMode && Spinnaker::GenApi::IsReadable(pTriggerMode)) {
                Spinnaker::GenApi::CEnumerationPtr triggerModePtr(pTriggerMode);
                auto entry = triggerModePtr->GetCurrentEntry();
                if (entry && Spinnaker::GenApi::IsReadable(entry)) {
                    QString triggerMode = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
                    qDebug() << "[UserSet] Current TriggerMode after load:" << triggerMode;
                }
            } else {
                qDebug() << "[UserSet] TriggerMode 노드 읽을 수 없음";
            }
            
            // TriggerSource 확인
            auto pTriggerSource = nodeMap.GetNode("TriggerSource");
            if (pTriggerSource && Spinnaker::GenApi::IsReadable(pTriggerSource)) {
                Spinnaker::GenApi::CEnumerationPtr triggerSrcPtr(pTriggerSource);
                auto entry = triggerSrcPtr->GetCurrentEntry();
                if (entry && Spinnaker::GenApi::IsReadable(entry)) {
                    QString triggerSrc = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
                    qDebug() << "[UserSet] Current TriggerSource after load:" << triggerSrc;
                }
            } else {
                qDebug() << "[UserSet] TriggerSource 노드 읽을 수 없음";
            }
            
            // AcquisitionMode 확인 (읽기만)
            auto pAcquisitionMode = nodeMap.GetNode("AcquisitionMode");
            if (pAcquisitionMode && Spinnaker::GenApi::IsReadable(pAcquisitionMode)) {
                Spinnaker::GenApi::CEnumerationPtr acqModePtr(pAcquisitionMode);
                auto entry = acqModePtr->GetCurrentEntry();
                if (entry && Spinnaker::GenApi::IsReadable(entry)) {
                    QString acqMode = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
                    qDebug() << "[UserSet] Current AcquisitionMode after load:" << acqMode;
                }
            } else {
                qDebug() << "[UserSet] AcquisitionMode 노드 읽을 수 없음";
            }
            
            // TriggerSelector 확인
            auto pTriggerSelector = nodeMap.GetNode("TriggerSelector");
            if (pTriggerSelector && Spinnaker::GenApi::IsReadable(pTriggerSelector)) {
                Spinnaker::GenApi::CEnumerationPtr triggerSelPtr(pTriggerSelector);
                auto entry = triggerSelPtr->GetCurrentEntry();
                if (entry && Spinnaker::GenApi::IsReadable(entry)) {
                    QString triggerSel = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
                    qDebug() << "[UserSet] Current TriggerSelector after load:" << triggerSel;
                }
            } else {
                qDebug() << "[UserSet] TriggerSelector 노드 읽을 수 없음";
            }
            
            // FrameRate 확인
            auto pFrameRate = nodeMap.GetNode("AcquisitionFrameRate");
            if (pFrameRate && Spinnaker::GenApi::IsReadable(pFrameRate)) {
                Spinnaker::GenApi::CFloatPtr frameRatePtr(pFrameRate);
                qDebug() << "[UserSet] AcquisitionFrameRate after load:" << frameRatePtr->GetValue() << "fps";
            } else {
                qDebug() << "[UserSet] AcquisitionFrameRate 노드 읽을 수 없음";
            }
            
            // ===== UserSet1 TRIGGER 모드 추가 검증 =====
            if (userSetName == "UserSet1") {
                qDebug() << "[UserSet] ===== UserSet1 TRIGGER 모드 상세 검증 =====";
                
                // TriggerMode 상태 재확인
                auto pTriggerModeVerify = nodeMap.GetNode("TriggerMode");
                if (pTriggerModeVerify && Spinnaker::GenApi::IsReadable(pTriggerModeVerify)) {
                    Spinnaker::GenApi::CEnumerationPtr triggerModePtr(pTriggerModeVerify);
                    auto entry = triggerModePtr->GetCurrentEntry();
                    if (entry) {
                        QString triggerMode = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
                        qDebug() << "[UserSet1] VERIFY - TriggerMode:" << triggerMode;
                        
                        if (triggerMode != "On") {
                            qDebug() << "[UserSet1] WARNING - TriggerMode is OFF! Attempting to enable...";
                            auto onEntry = triggerModePtr->GetEntryByName("On");
                            if (onEntry && Spinnaker::GenApi::IsReadable(onEntry)) {
                                triggerModePtr->SetIntValue(onEntry->GetValue());
                                qDebug() << "[UserSet1] TriggerMode forced to ON";
                                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                            }
                        }
                    }
                }
                
                // TriggerSource LINE0 확인
                auto pTriggerSourceVerify = nodeMap.GetNode("TriggerSource");
                if (pTriggerSourceVerify && Spinnaker::GenApi::IsReadable(pTriggerSourceVerify)) {
                    Spinnaker::GenApi::CEnumerationPtr triggerSrcPtr(pTriggerSourceVerify);
                    auto entry = triggerSrcPtr->GetCurrentEntry();
                    if (entry) {
                        QString triggerSrc = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
                        qDebug() << "[UserSet1] VERIFY - TriggerSource:" << triggerSrc;
                        
                        if (triggerSrc != "Line0") {
                            qDebug() << "[UserSet1] WARNING - TriggerSource is not Line0! Attempting to set Line0...";
                            auto line0Entry = triggerSrcPtr->GetEntryByName("Line0");
                            if (line0Entry && Spinnaker::GenApi::IsReadable(line0Entry)) {
                                triggerSrcPtr->SetIntValue(line0Entry->GetValue());
                                qDebug() << "[UserSet1] TriggerSource forced to Line0";
                                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                            }
                        }
                    }
                }
                
                // LineSelector 확인
                auto pLineSelector = nodeMap.GetNode("LineSelector");
                if (pLineSelector && Spinnaker::GenApi::IsReadable(pLineSelector)) {
                    Spinnaker::GenApi::CEnumerationPtr lineSelectorPtr(pLineSelector);
                    auto entry = lineSelectorPtr->GetCurrentEntry();
                    if (entry) {
                        QString lineSelector = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
                        qDebug() << "[UserSet1] LineSelector:" << lineSelector;
                    }
                }
                
                // LineMode 확인 (입력/출력)
                auto pLineMode = nodeMap.GetNode("LineMode");
                if (pLineMode && Spinnaker::GenApi::IsReadable(pLineMode)) {
                    Spinnaker::GenApi::CEnumerationPtr lineModePtr(pLineMode);
                    auto entry = lineModePtr->GetCurrentEntry();
                    if (entry) {
                        QString lineMode = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
                        qDebug() << "[UserSet1] LineMode:" << lineMode;
                        
                        if (lineMode != "Input") {
                            qDebug() << "[UserSet1] WARNING - LineMode is not Input! Setting to Input...";
                            auto inputEntry = lineModePtr->GetEntryByName("Input");
                            if (inputEntry && Spinnaker::GenApi::IsReadable(inputEntry)) {
                                lineModePtr->SetIntValue(inputEntry->GetValue());
                                qDebug() << "[UserSet1] LineMode forced to Input";
                            }
                        }
                    }
                }
                
                qDebug() << "[UserSet1] ===== 상세 검증 완료 =====";
            }
            
        } catch (const std::exception& e) {
            qDebug() << "[UserSet] Warning: Parameter read error:" << e.what();
        }
        
        // 현재 UserSet 표시 업데이트
        currentUserSetLabel->setText(QString("%1 (%2)").arg(userSetName).arg(modeName));
        currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #4CAF50; }");
        
        // ===== 노드 값 강제 갱신 =====
        // GetNode() 호출로 캐시된 노드 포인터 다시 얻기
        try {
            auto pTriggerMode = nodeMap.GetNode("TriggerMode");
            auto pTriggerSource = nodeMap.GetNode("TriggerSource");
            auto pTriggerSelector = nodeMap.GetNode("TriggerSelector");
            qDebug() << "[UserSet] Node pointers refreshed after UserSet load";
        } catch (...) {
            qDebug() << "[UserSet] Warning: Could not refresh node pointers";
        }
        
        // 스트리밍이 실행 중이었다면 다시 시작
        if (wasStreaming) {
            camera->BeginAcquisition();
        }
        
        // **UserSet0(라이브 모드)일 때는 반드시 streaming 시작**
        if (userSetName == "UserSet0") {
            qDebug() << "[UserSet] UserSet0 감지 - camera->IsStreaming():" << camera->IsStreaming();
            if (!camera->IsStreaming()) {
                try {
                    camera->BeginAcquisition();
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // BeginAcquisition 후 대기
                    bool isStreamingAfter = camera->IsStreaming();
                    qDebug() << "[UserSet] ✓ UserSet0 - BeginAcquisition 완료, IsStreaming:" << isStreamingAfter;
                    
                    // **★ 중요: BeginAcquisition 직후 버퍼의 오래된 프레임 모두 제거 ★**
                    qDebug() << "[UserSet] 버퍼 플러시 시작 (오래된 프레임 제거)";
                    int flushedCount = 0;
                    try {
                        while (flushedCount < 30) {  // 최대 30개 프레임 제거
                            try {
                                Spinnaker::ImagePtr oldImage = camera->GetNextImage(1);  // 1ms timeout
                                if (oldImage) {
                                    oldImage->Release();
                                    flushedCount++;
                                } else {
                                    break;
                                }
                            } catch (...) {
                                break;  // 타임아웃 또는 에러
                            }
                        }
                        qDebug() << "[UserSet] ✓ 버퍼 플러시 완료 (" << flushedCount << "개 프레임 제거)";
                    } catch (...) {
                        qDebug() << "[UserSet] 버퍼 플러시 중 예외 발생";
                    }
                    
                } catch (const std::exception& e) {
                    qDebug() << "[UserSet] ✗ BeginAcquisition 실패:" << e.what();
                }
            } else {
                qDebug() << "[UserSet] UserSet0이지만 이미 streaming 중";
                
                // **이미 스트리밍 중이어도 버퍼 플러시**
                qDebug() << "[UserSet] 버퍼 플러시 시작 (이미 스트리밍 중)";
                int flushedCount = 0;
                try {
                    while (flushedCount < 30) {
                        try {
                            Spinnaker::ImagePtr oldImage = camera->GetNextImage(1);
                            if (oldImage) {
                                oldImage->Release();
                                flushedCount++;
                            } else {
                                break;
                            }
                        } catch (...) {
                            break;
                        }
                    }
                    qDebug() << "[UserSet] ✓ 버퍼 플러시 완료 (" << flushedCount << "개 프레임 제거)";
                } catch (...) {
                    qDebug() << "[UserSet] 버퍼 플러시 중 예외 발생";
                }
            }
        }
        
        // 파라미터 출력
        printCameraParameters(nodeMap, QString("%1 로드 후").arg(userSetName));
        
        currentUserSetLabel->setText(QString("%1 로드됨").arg(userSetName));
        currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #4CAF50; }");
        
        // **중요**: 라이브 이미지 스레드 다시 시작
        if (wasLiveThreadRunning) {
            liveImageThreadRunning = true;
        }
        
    } catch (const Spinnaker::Exception& e) {
        currentUserSetLabel->setText("로드 실패");
        currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #f44336; }");
        QMessageBox::critical(this, "오류", 
            QString("UserSet 로드 실패: %1").arg(QString::fromStdString(e.what())));
        // 라이브 스레드 복구
        if (wasLiveThreadRunning) {
            liveImageThreadRunning = true;
        }
    } catch (const std::exception& e) {
        currentUserSetLabel->setText("로드 실패");
        currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #f44336; }");
        QMessageBox::critical(this, "오류", 
            QString("UserSet 로드 실패: %1").arg(QString::fromStdString(e.what())));
        // 라이브 스레드 복구
        if (wasLiveThreadRunning) {
            liveImageThreadRunning = true;
        }
    }
}

void CameraSettingsDialog::onLoadUserSet0() {
    loadUserSet("UserSet0", "LIVE 모드");
    
    // 버튼 스타일 업데이트 - UserSet0 초록색, UserSet1 빨간색
    loadUserSet0Btn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 10px; }");
    loadUserSet1Btn->setStyleSheet("QPushButton { background-color: #f44336; color: white; font-weight: bold; padding: 10px; }");
    
    // UserSet0 로드 후: 트리거 버튼 비활성화 + 성공 횟수 라벨 숨김
    triggerToggleBtn->setEnabled(false);
    triggerToggleBtn->setStyleSheet(
        "QPushButton { "
        "    background-color: #BDBDBD; "
        "    color: #999; "
        "    font-weight: bold; "
        "    padding: 12px; "
        "    border-radius: 5px; "
        "    font-size: 12px; "
        "}"
    );
    
    // 성공 횟수 라벨 숨김 (라이브 모드에서는 불필요)
    if (triggerStatusLabel) {
        triggerStatusLabel->setVisible(false);
    }
    
    // 라이브 영상 표시 시작을 위한 신호
    // TeachingWidget에서 라이브 프레임 보내도록 요청 필요
    triggerImageLabel->setText("라이브 영상 시작...");
    triggerImageLabel->setStyleSheet(
        "QLabel { "
        "    background-color: #1a1a1a; "
        "    border: 2px solid #666; "
        "    border-radius: 5px; "
        "    qproperty-alignment: AlignCenter; "
        "    color: #999; "
        "    font-size: 11px; "
        "}"
    );
    
    // **중요**: 라이브 이미지 스레드 강제 재시작 (항상 재생성)
    // UserSet0 전환 시마다 새 스레드를 생성하여 최신 카메라 상태를 반영
    liveImageThreadRunning = false;  // 기존 스레드 종료 신호
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // 스레드 종료 대기
    
    if (liveImageThread) {
        try {
            if (liveImageThread->joinable()) {
                liveImageThread->join();
            }
        } catch (...) {}
        delete liveImageThread;
        liveImageThread = nullptr;
    }
    
    liveImageThreadRunning = true;
    
    // 라이브 이미지 스레드 새로 시작
    liveImageThread = new std::thread([this]() {
        qDebug() << "[LiveThread] 라이브 이미지 스레드 시작";
        
        Spinnaker::ImageProcessor processor;
        try {
            processor.SetColorProcessing(Spinnaker::SPINNAKER_COLOR_PROCESSING_ALGORITHM_DIRECTIONAL_FILTER);
        } catch (...) {}

        int loopCount = 0;
        while (liveImageThreadRunning) {
            loopCount++;
            
            if (isTriggerTesting) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            int selectedCameraIndex = getSelectedCameraIndex();
            if (selectedCameraIndex < 0 || selectedCameraIndex >= static_cast<int>(m_spinCameras.size())) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            Spinnaker::CameraPtr camera = m_spinCameras[selectedCameraIndex];
            if (!camera) {
                qDebug() << "[LiveThread] Camera is null";
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            try {
                Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
                auto pUserSetSelector = nodeMap.GetNode("UserSetSelector");
                if (!pUserSetSelector || !Spinnaker::GenApi::IsReadable(pUserSetSelector)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }

                Spinnaker::GenApi::CEnumerationPtr userSetPtr(pUserSetSelector);
                auto entry = userSetPtr->GetCurrentEntry();
                QString currentUserSet = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
                if (currentUserSet != "UserSet0") {
                    if (loopCount % 10 == 0) {
                        qDebug() << "[LiveThread] UserSet is:" << currentUserSet << "(Expected: UserSet0)";
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
            } catch (...) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            try {
                if (!camera->IsStreaming()) {
                    qDebug() << "[LiveThread] Camera not streaming - BeginAcquisition called";
                    camera->BeginAcquisition();
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                
                Spinnaker::ImagePtr pImage = nullptr;
                try {
                    // 타임아웃 2ms (1ms보다 약간 여유)
                    pImage = camera->GetNextImage(2);
                } catch (...) {
                    pImage = nullptr;
                }

                if (!pImage) {
                    // 프레임 없으면 바로 루프 (sleep 없음)
                    continue;
                }

                if (pImage->IsIncomplete()) {
                    pImage->Release();
                    continue;
                }

                // 이미지 변환 최소화
                unsigned char* buffer = nullptr;
                size_t width = 0, height = 0;
                
                try {
                    if (pImage->GetPixelFormat() == Spinnaker::PixelFormat_RGB8) {
                        // 이미 RGB면 바로 사용
                        buffer = static_cast<unsigned char*>(pImage->GetData());
                        width = pImage->GetWidth();
                        height = pImage->GetHeight();
                    } else {
                        // 변환 필요하면
                        Spinnaker::ImagePtr convertedImage = processor.Convert(pImage, Spinnaker::PixelFormat_BGR8);
                        if (convertedImage && !convertedImage->IsIncomplete()) {
                            buffer = static_cast<unsigned char*>(convertedImage->GetData());
                            width = convertedImage->GetWidth();
                            height = convertedImage->GetHeight();
                        }
                    }
                } catch (...) {
                    pImage->Release();
                    continue;
                }

                if (buffer && width > 0 && height > 0) {
                    cv::Mat cvImage(height, width, CV_8UC3, buffer);
                    cv::Mat displayMat;
                    cv::resize(cvImage, displayMat, cv::Size(280, 210), 0, 0, cv::INTER_LINEAR);
                    
                    QImage qImage(displayMat.data, displayMat.cols, displayMat.rows, displayMat.step, QImage::Format_RGB888);
                    QPixmap pixmap = QPixmap::fromImage(qImage);
                    
                    if (triggerImageLabel) triggerImageLabel->setPixmap(pixmap);
                }

                pImage->Release();
            } catch (...) {
                // 예외 발생 시에만 sleep
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });  // 라이브 스레드 람다 함수 끝
}  // onLoadUserSet0 끝

void CameraSettingsDialog::onLoadUserSet1() {
    loadUserSet("UserSet1", "TRIGGER 모드");
    
    // 버튼 스타일 업데이트 - UserSet1 초록색, UserSet0 빨간색
    loadUserSet0Btn->setStyleSheet("QPushButton { background-color: #f44336; color: white; font-weight: bold; padding: 10px; }");
    loadUserSet1Btn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 10px; }");
    
    // UserSet1 로드 후: 트리거 버튼 활성화 + 성공 횟수 라벨 표시
    triggerToggleBtn->setEnabled(true);
    triggerToggleBtn->setStyleSheet(
        "QPushButton { "
        "    background-color: #2196F3; "
        "    color: white; "
        "    font-weight: bold; "
        "    padding: 12px; "
        "    border-radius: 5px; "
        "    font-size: 12px; "
        "} "
        "QPushButton:hover { background-color: #1976D2; } "
        "QPushButton:pressed { background-color: #1565C0; }"
    );
    
    // 성공 횟수 라벨 표시 (트리거 모드에서 필요)
    if (triggerStatusLabel) {
        triggerStatusLabel->setVisible(true);
    }
    
    // 트리거 모드로 전환 시 표시 업데이트
    triggerImageLabel->setText("트리거 대기...");
    triggerImageLabel->setStyleSheet(
        "QLabel { "
        "    background-color: #1a1a1a; "
        "    border: 2px solid #666; "
        "    border-radius: 5px; "
        "    qproperty-alignment: AlignCenter; "
        "    color: #999; "
        "    font-size: 11px; "
        "}"
    );
    
    // 트리거 테스트 중이었다면 중지
    if (isTriggerTesting) {
        onStopTriggerTest();
    }
}
#endif

#ifdef USE_SPINNAKER
void CameraSettingsDialog::printCameraParameters(Spinnaker::GenApi::INodeMap& nodeMap, const QString& stage) {
    qDebug() << "===== Camera Parameters" << stage << "=====";
    
    try {
        // ExposureTime
        auto pExposureTime = nodeMap.GetNode("ExposureTime");
        if (pExposureTime && Spinnaker::GenApi::IsReadable(pExposureTime)) {
            Spinnaker::GenApi::CFloatPtr floatPtr(pExposureTime);
            qDebug() << "ExposureTime:" << floatPtr->GetValue() << "μs";
        }
        
        // Gain
        auto pGain = nodeMap.GetNode("Gain");
        if (pGain && Spinnaker::GenApi::IsReadable(pGain)) {
            Spinnaker::GenApi::CFloatPtr floatPtr(pGain);
            qDebug() << "Gain:" << floatPtr->GetValue() << "dB";
        }
        
        // TriggerMode
        auto pTriggerMode = nodeMap.GetNode("TriggerMode");
        if (pTriggerMode && Spinnaker::GenApi::IsReadable(pTriggerMode)) {
            Spinnaker::GenApi::CEnumerationPtr enumPtr(pTriggerMode);
            auto entry = enumPtr->GetCurrentEntry();
            qDebug() << "TriggerMode:" << entry->GetSymbolic().c_str();
        }
        
        // TriggerSource
        auto pTriggerSource = nodeMap.GetNode("TriggerSource");
        if (pTriggerSource && Spinnaker::GenApi::IsReadable(pTriggerSource)) {
            Spinnaker::GenApi::CEnumerationPtr enumPtr(pTriggerSource);
            auto entry = enumPtr->GetCurrentEntry();
            qDebug() << "TriggerSource:" << entry->GetSymbolic().c_str();
        }
        
        // Width
        auto pWidth = nodeMap.GetNode("Width");
        if (pWidth && Spinnaker::GenApi::IsReadable(pWidth)) {
            Spinnaker::GenApi::CIntegerPtr intPtr(pWidth);
            qDebug() << "Width:" << intPtr->GetValue();
        }
        
        // Height
        auto pHeight = nodeMap.GetNode("Height");
        if (pHeight && Spinnaker::GenApi::IsReadable(pHeight)) {
            Spinnaker::GenApi::CIntegerPtr intPtr(pHeight);
            qDebug() << "Height:" << intPtr->GetValue();
        }
        
        // PixelFormat
        auto pPixelFormat = nodeMap.GetNode("PixelFormat");
        if (pPixelFormat && Spinnaker::GenApi::IsReadable(pPixelFormat)) {
            Spinnaker::GenApi::CEnumerationPtr enumPtr(pPixelFormat);
            auto entry = enumPtr->GetCurrentEntry();
            qDebug() << "PixelFormat:" << entry->GetSymbolic().c_str();
        }
        
        // AcquisitionMode
        auto pAcquisitionMode = nodeMap.GetNode("AcquisitionMode");
        if (pAcquisitionMode && Spinnaker::GenApi::IsReadable(pAcquisitionMode)) {
            Spinnaker::GenApi::CEnumerationPtr enumPtr(pAcquisitionMode);
            auto entry = enumPtr->GetCurrentEntry();
            qDebug() << "AcquisitionMode:" << entry->GetSymbolic().c_str();
        }
        
        // UserSetSelector
        auto pUserSetSelector = nodeMap.GetNode("UserSetSelector");
        if (pUserSetSelector && Spinnaker::GenApi::IsReadable(pUserSetSelector)) {
            Spinnaker::GenApi::CEnumerationPtr enumPtr(pUserSetSelector);
            auto entry = enumPtr->GetCurrentEntry();
            qDebug() << "UserSetSelector:" << entry->GetSymbolic().c_str();
        }
        
    } catch (Spinnaker::Exception& e) {
        qDebug() << "Error reading parameters:" << e.what();
    }
    
    qDebug() << "=======================================";
}

void CameraSettingsDialog::onStartTriggerTest() {
    int selectedCameraIndex = getSelectedCameraIndex();
    if (selectedCameraIndex < 0 || selectedCameraIndex >= static_cast<int>(m_spinCameras.size())) {
        triggerStatusLabel->setText("카메라 선택 필요");
        triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #f44336; }");
        triggerIndicatorLabel->setText("Error!");
        triggerIndicatorLabel->setStyleSheet("QLabel { background-color: #f44336; border: 2px solid #d32f2f; border-radius: 5px; color: white; font-weight: bold; font-size: 11px; }");
        return;
    }
    
    try {
        Spinnaker::CameraPtr camera = m_spinCameras[selectedCameraIndex];
        Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
        
        // TriggerMode 확인
        auto pTriggerMode = nodeMap.GetNode("TriggerMode");
        if (!pTriggerMode || !Spinnaker::GenApi::IsReadable(pTriggerMode)) {
            triggerStatusLabel->setText("트리거 모드 확인 불가");
            triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #f44336; }");
            triggerIndicatorLabel->setText("Error!");
            triggerIndicatorLabel->setStyleSheet("QLabel { background-color: #f44336; border: 2px solid #d32f2f; border-radius: 5px; color: white; font-weight: bold; font-size: 11px; }");
            return;
        }
        
        Spinnaker::GenApi::CEnumerationPtr triggerModePtr(pTriggerMode);
        auto entry = triggerModePtr->GetCurrentEntry();
        QString currentMode = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
        
        if (currentMode != "On") {
            triggerStatusLabel->setText("트리거 모드 OFF (UserSet1 로드필요)");
            triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #f44336; }");
            triggerIndicatorLabel->setText("Error!");
            triggerIndicatorLabel->setStyleSheet("QLabel { background-color: #f44336; border: 2px solid #d32f2f; border-radius: 5px; color: white; font-weight: bold; font-size: 11px; }");
            return;
        }
        
        // ===== 중요: 트리거 감지를 위해 Streaming 시작 =====
        if (!camera->IsStreaming()) {
            camera->BeginAcquisition();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        
        // 테스트 시작
        isTriggerTesting = true;
        triggerDetectionCount = 0;
        lastExposureCount = 0;
        
        triggerToggleBtn->setText("테스트 중지");
        triggerToggleBtn->setStyleSheet(
            "QPushButton { "
            "    background-color: #f44336; "
            "    color: white; "
            "    font-weight: bold; "
            "    padding: 12px; "
            "    border-radius: 5px; "
            "    font-size: 12px; "
            "} "
            "QPushButton:hover { background-color: #d32f2f; } "
            "QPushButton:pressed { background-color: #c62828; }"
        );
        
        triggerStatusLabel->setText("대기 중...");
        triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #2196F3; }");
        
        triggerIndicatorLabel->setText("Ready!");
        triggerIndicatorLabel->setStyleSheet("QLabel { background-color: #FFC107; border: 2px solid #FFA000; border-radius: 5px; color: #333; font-weight: bold; font-size: 11px; }");
        
        // 트리거 테스트 스레드 시작
        triggerTestThreadRunning = true;
        if (triggerTestThread) {
            triggerTestThread->join();
            delete triggerTestThread;
        }
        triggerTestThread = new std::thread([this]() {
            while (liveImageThreadRunning && isTriggerTesting) {
                updateTriggerTestStatus();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
        
    } catch (const std::exception& e) {
        triggerStatusLabel->setText("오류 발생");
        triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #f44336; }");
        triggerIndicatorLabel->setText("Error!");
        triggerIndicatorLabel->setStyleSheet("QLabel { background-color: #f44336; border: 2px solid #d32f2f; border-radius: 5px; color: white; font-weight: bold; font-size: 11px; }");
    }
}

void CameraSettingsDialog::onStopTriggerTest() {
    isTriggerTesting = false;
    triggerTestThreadRunning = false;
    if (triggerTestThread && triggerTestThread->joinable()) {
        triggerTestThread->join();
        delete triggerTestThread;
        triggerTestThread = nullptr;
    }
    
    triggerToggleBtn->setText("트리거 테스트 시작");
    triggerToggleBtn->setStyleSheet(
        "QPushButton { "
        "    background-color: #2196F3; "
        "    color: white; "
        "    font-weight: bold; "
        "    padding: 12px; "
        "    border-radius: 5px; "
        "    font-size: 12px; "
        "} "
        "QPushButton:hover { background-color: #1976D2; } "
        "QPushButton:pressed { background-color: #1565C0; }"
    );
    
    triggerStatusLabel->setText(QString("완료 (감지: %1회)").arg(triggerDetectionCount.load()));
    triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #4CAF50; }");
    
    triggerIndicatorLabel->setText("Done!");
    triggerIndicatorLabel->setStyleSheet("QLabel { background-color: #4CAF50; border: 2px solid #388E3C; border-radius: 5px; color: white; font-weight: bold; font-size: 11px; }");
}

void CameraSettingsDialog::onTriggerTestTimeout() {
    updateTriggerTestStatus();
}

void CameraSettingsDialog::updateTriggerTestStatus() {
    if (!isTriggerTesting) {
        return;
    }
    
    int selectedCameraIndex = getSelectedCameraIndex();
    if (selectedCameraIndex < 0 || selectedCameraIndex >= static_cast<int>(m_spinCameras.size())) {
        return;
    }
    
    try {
        Spinnaker::CameraPtr camera = m_spinCameras[selectedCameraIndex];
        Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
        
        // TriggerSource 읽기
        auto pTriggerSource = nodeMap.GetNode("TriggerSource");
        if (pTriggerSource && Spinnaker::GenApi::IsReadable(pTriggerSource)) {
            Spinnaker::GenApi::CEnumerationPtr enumPtr(pTriggerSource);
            auto entry = enumPtr->GetCurrentEntry();
            QString triggerSource = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
            
            // 라인 정보 표시 (Line0, Line1, Line2, Line3 등)
            if (triggerSource.startsWith("Line")) {
                triggerLineLabel->setText(triggerSource);
                triggerLineLabel->setStyleSheet("QLabel { font-weight: bold; color: #4CAF50; }");
            } else {
                triggerLineLabel->setText(triggerSource);
                triggerLineLabel->setStyleSheet("QLabel { font-weight: bold; color: #FF9800; }");
            }
        }
        
        // TriggerActivation 읽기
        auto pTriggerActivation = nodeMap.GetNode("TriggerActivation");
        if (pTriggerActivation && Spinnaker::GenApi::IsReadable(pTriggerActivation)) {
            Spinnaker::GenApi::CEnumerationPtr enumPtr(pTriggerActivation);
            auto entry = enumPtr->GetCurrentEntry();
            QString activation = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
            triggerActivationLabel->setText(activation);
            triggerActivationLabel->setStyleSheet("QLabel { font-weight: bold; color: #333; }");
        }
        
        // TriggerPolarity 읽기 (RisingEdge/FallingEdge)
        auto pTriggerPolarity = nodeMap.GetNode("TriggerPolarity");
        if (pTriggerPolarity && Spinnaker::GenApi::IsReadable(pTriggerPolarity)) {
            Spinnaker::GenApi::CEnumerationPtr enumPtr(pTriggerPolarity);
            auto entry = enumPtr->GetCurrentEntry();
            QString polarity = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
            triggerEdgeLabel->setText(polarity);  // "RisingEdge" 또는 "FallingEdge"
            triggerEdgeLabel->setStyleSheet("QLabel { font-weight: bold; color: #333; }");
        }
        
        // ===== 실제 트리거 감지: 직접 이미지 획득 방식 =====
        // SingleFrame + Hardware Trigger: 매 500ms마다 이미지 확인
        Spinnaker::ImagePtr pImage = nullptr;
        
        try {
            // 짧은 타임아웃 (1ms) - 빠른 응답성과 프레임 레이그 제거
            // 실패해도 예외 아님 (타임아웃은 정상)
            pImage = camera->GetNextImage(1);
        } catch (...) {
            // 타임아웃 예외는 무시
            pImage = nullptr;
        }
        
        if (pImage && !pImage->IsIncomplete()) {
            // ✓ 유효한 이미지 획득 = 트리거 감지!
            int currentCount = ++triggerDetectionCount;
            
            // UI 피드백 (메인 스레드에서 실행)
            QMetaObject::invokeMethod(this, [this, currentCount]() {
                if (!isTriggerTesting) return;
                
                if (triggerCountLabel) {
                    triggerCountLabel->setText(QString::number(currentCount));
                    triggerCountLabel->setStyleSheet("QLabel { font-weight: bold; color: #2196F3; font-size: 14px; }");
                }
                
                if (triggerStatusLabel) {
                    triggerStatusLabel->setText("Trigger On!");
                    triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #4CAF50; font-size: 13px; }");
                }
                
                if (triggerIndicatorLabel) {
                    triggerIndicatorLabel->setText("On!");
                    triggerIndicatorLabel->setStyleSheet("QLabel { background-color: #4CAF50; border: 2px solid #2E7D32; border-radius: 5px; color: white; font-weight: bold; font-size: 11px; }");
                }
            }, Qt::QueuedConnection);
            
            uint64_t timestamp = pImage->GetTimeStamp();
            size_t width = pImage->GetWidth();
            size_t height = pImage->GetHeight();
            qDebug() << "[TriggerTest] ✓ #" << currentCount << width << "x" << height;
            
            // ===== 이미지 표시 =====
            try {
                Spinnaker::ImageProcessor processor;
                processor.SetColorProcessing(Spinnaker::SPINNAKER_COLOR_PROCESSING_ALGORITHM_DIRECTIONAL_FILTER);
                
                // BGR8로 변환 (OpenCV는 BGR 순서 사용)
                Spinnaker::ImagePtr convertedImage = processor.Convert(pImage, Spinnaker::PixelFormat_BGR8);
                
                if (convertedImage && !convertedImage->IsIncomplete()) {
                    unsigned char* buffer = static_cast<unsigned char*>(convertedImage->GetData());
                    cv::Mat cvImage(height, width, CV_8UC3, buffer);
                    
                    // 280x210 크기로 리사이즈
                    cv::Mat resized;
                    cv::resize(cvImage, resized, cv::Size(280, 210));
                    
                    // OpenCV Mat to QPixmap 변환
                    cv::Mat rgbImage;
                    cv::cvtColor(resized, rgbImage, cv::COLOR_BGR2RGB);
                    
                    QImage qImage(rgbImage.data, rgbImage.cols, rgbImage.rows, 
                                  static_cast<int>(rgbImage.step), QImage::Format_RGB888);
                    QPixmap pixmap = QPixmap::fromImage(qImage);
                    
                    // UI 업데이트 (메인 스레드에서 실행)
                    QMetaObject::invokeMethod(this, [this, pixmap]() {
                        if (isTriggerTesting && triggerImageLabel) {
                            triggerImageLabel->setPixmap(pixmap);
                        }
                    }, Qt::QueuedConnection);
                    
                    convertedImage->Release();
                }
            } catch (...) {
                // 이미지 처리 실패는 무시
            }
            
            pImage->Release();
            
            // ===== SingleFrame 모드: 다음 트리거 대기를 위해 acquisition 재시작 =====
            try {
                if (camera->IsStreaming()) {
                    camera->EndAcquisition();
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                camera->BeginAcquisition();
            } catch (...) {
                // 무시
            }
            
            // 상태 업데이트 (메인 스레드에서)
            QMetaObject::invokeMethod(this, [this]() {
                if (isTriggerTesting) {
                    triggerStatusLabel->setText("Ready!");
                    triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #2196F3; font-size: 13px; }");
                    triggerIndicatorLabel->setText("Ready!");
                    triggerIndicatorLabel->setStyleSheet("QLabel { background-color: #FFC107; border: 2px solid #FFA000; border-radius: 5px; color: #333; font-weight: bold; font-size: 11px; }");
                }
            }, Qt::QueuedConnection);
        } else {
            // 타임아웃 또는 불완전한 이미지 - 정상 (트리거 아직 안 받음)
            if (pImage) {
                pImage->Release();
            }
            
            // Streaming 상태 확인 및 필요시 재개
            try {
                if (!camera->IsStreaming()) {
                    camera->BeginAcquisition();
                }
            } catch (...) {
                // 무시
            }
        }
        
    } catch (const std::exception& e) {
        qDebug() << "Error updating trigger status:" << e.what();
    }
}

void CameraSettingsDialog::updateLiveImageDisplay(const cv::Mat& frame) {
    // UserSet0 (라이브 모드)일 때만 영상 표시
    int selectedCameraIndex = getSelectedCameraIndex();
    if (selectedCameraIndex < 0 || selectedCameraIndex >= static_cast<int>(m_spinCameras.size())) {
        return;
    }
    
    try {
        Spinnaker::CameraPtr camera = m_spinCameras[selectedCameraIndex];
        Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
        
        // 현재 UserSet 확인
        auto pUserSetSelector = nodeMap.GetNode("UserSetSelector");
        if (!pUserSetSelector || !Spinnaker::GenApi::IsReadable(pUserSetSelector)) {
            return;
        }
        
        Spinnaker::GenApi::CEnumerationPtr userSetPtr(pUserSetSelector);
        auto entry = userSetPtr->GetCurrentEntry();
        QString currentUserSet = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
        
        // UserSet0이 아니면 표시 안 함
        if (currentUserSet != "UserSet0") {
            return;
        }
        
        // 프레임이 없으면 무시
        if (frame.empty()) {
            return;
        }
        
        // 280x210 크기로 리사이즈
        cv::Mat resized;
        cv::resize(frame, resized, cv::Size(280, 210));
        
        // BGR to RGB 변환
        cv::Mat rgbImage;
        if (resized.channels() == 3) {
            cv::cvtColor(resized, rgbImage, cv::COLOR_BGR2RGB);
        } else if (resized.channels() == 1) {
            cv::cvtColor(resized, rgbImage, cv::COLOR_GRAY2RGB);
        } else {
            return;
        }
        
        // Mat to QPixmap 변환
        QImage qImage(rgbImage.data, rgbImage.cols, rgbImage.rows, 
                      static_cast<int>(rgbImage.step), QImage::Format_RGB888);
        QPixmap pixmap = QPixmap::fromImage(qImage);
        
        // UI 업데이트
        triggerImageLabel->setPixmap(pixmap);
        
    } catch (const std::exception& e) {
        // 무시
    }
}

#endif
