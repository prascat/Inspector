#include "CameraSettingsDialog.h"
#include <QDebug>
#include <chrono>
#include <thread>

CameraSettingsDialog::CameraSettingsDialog(QWidget* parent)
    : QDialog(parent)
    , currentCameraIndex(-1)
    , m_configManager(ConfigManager::instance())
    , isTriggerTesting(false)
    , triggerDetectionCount(0)
    , triggerTestTimer(nullptr)
    , lastExposureCount(0)
{
    setWindowTitle("카메라 UserSet 관리");
    setMinimumSize(500, 400);
    
    setupUI();
}

CameraSettingsDialog::~CameraSettingsDialog() {
}

void CameraSettingsDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
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
                Spinnaker::GenApi::CStringPtr ptrDeviceID = nodeMap.GetNode("DeviceID");
                Spinnaker::GenApi::CStringPtr ptrDeviceModelName = nodeMap.GetNode("DeviceModelName");
                
                QString deviceID = "Unknown";
                QString modelName = "Unknown";
                
                if (IsReadable(ptrDeviceID)) {
                    deviceID = QString::fromLocal8Bit(ptrDeviceID->GetValue().c_str());
                }
                if (IsReadable(ptrDeviceModelName)) {
                    modelName = QString::fromLocal8Bit(ptrDeviceModelName->GetValue().c_str());
                }
                
                cameraCombo->addItem(QString("카메라 %1: %2 (%3)").arg(i + 1).arg(modelName).arg(deviceID));
            } else {
                cameraCombo->addItem(QString("카메라 %1: 초기화되지 않음").arg(i + 1));
            }
        } catch (const std::exception& e) {
            cameraCombo->addItem(QString("카메라 %1: 오류").arg(i + 1));
        }
    }
    
    currentCameraIndex = 0;
}
#endif

void CameraSettingsDialog::setupUserSetSettings() {
    userSetGroup = new QGroupBox("UserSet 로드", this);
    QVBoxLayout* userSetLayout = new QVBoxLayout(userSetGroup);
    
    // UserSet 로드 버튼
    QHBoxLayout* buttonLayout = new QHBoxLayout;
    
    loadUserSet0Btn = new QPushButton("LIVE 모드 (UserSet0) 로드", this);
    loadUserSet0Btn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; padding: 10px; }");
    buttonLayout->addWidget(loadUserSet0Btn);
    
    loadUserSet1Btn = new QPushButton("TRIGGER 모드 (UserSet1) 로드", this);
    loadUserSet1Btn->setStyleSheet("QPushButton { background-color: #FF9800; color: white; font-weight: bold; padding: 10px; }");
    buttonLayout->addWidget(loadUserSet1Btn);
    
    userSetLayout->addLayout(buttonLayout);
    
    // 현재 UserSet 상태
    QHBoxLayout* statusLayout = new QHBoxLayout;
    statusLayout->addWidget(new QLabel("현재 UserSet:", this));
    currentUserSetLabel = new QLabel("카메라 선택 필요", this);
    currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #FF9800; }");
    statusLayout->addWidget(currentUserSetLabel);
    statusLayout->addStretch();
    
    userSetLayout->addLayout(statusLayout);
    
    // 시그널 연결
    connect(loadUserSet0Btn, &QPushButton::clicked, this, &CameraSettingsDialog::onLoadUserSet0);
    connect(loadUserSet1Btn, &QPushButton::clicked, this, &CameraSettingsDialog::onLoadUserSet1);
}

void CameraSettingsDialog::setupTriggerTestUI() {
    triggerTestGroup = new QGroupBox("트리거 테스트", this);
    QVBoxLayout* triggerLayout = new QVBoxLayout(triggerTestGroup);
    
    // 테스트 시작/중지 버튼
    QHBoxLayout* buttonLayout = new QHBoxLayout;
    
    startTriggerTestBtn = new QPushButton("트리거 테스트 시작", this);
    startTriggerTestBtn->setStyleSheet("QPushButton { background-color: #2196F3; color: white; font-weight: bold; padding: 8px; }");
    buttonLayout->addWidget(startTriggerTestBtn);
    
    stopTriggerTestBtn = new QPushButton("테스트 중지", this);
    stopTriggerTestBtn->setStyleSheet("QPushButton { background-color: #f44336; color: white; font-weight: bold; padding: 8px; }");
    stopTriggerTestBtn->setEnabled(false);
    buttonLayout->addWidget(stopTriggerTestBtn);
    
    triggerLayout->addLayout(buttonLayout);
    
    // 트리거 상태 정보
    QHBoxLayout* statusLayout = new QHBoxLayout;
    statusLayout->addWidget(new QLabel("상태:", this));
    triggerStatusLabel = new QLabel("대기 중", this);
    triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #999; }");
    statusLayout->addWidget(triggerStatusLabel);
    statusLayout->addStretch();
    triggerLayout->addLayout(statusLayout);
    
    // 트리거 정보 표시 영역
    QGridLayout* infoLayout = new QGridLayout;
    
    // 라인 정보
    infoLayout->addWidget(new QLabel("트리거 라인:", this), 0, 0);
    triggerLineLabel = new QLabel("-", this);
    triggerLineLabel->setStyleSheet("QLabel { font-weight: bold; color: #333; }");
    infoLayout->addWidget(triggerLineLabel, 0, 1);
    
    // 엣지 정보
    infoLayout->addWidget(new QLabel("트리거 엣지:", this), 1, 0);
    triggerEdgeLabel = new QLabel("-", this);
    triggerEdgeLabel->setStyleSheet("QLabel { font-weight: bold; color: #333; }");
    infoLayout->addWidget(triggerEdgeLabel, 1, 1);
    
    // 활성화 정보
    infoLayout->addWidget(new QLabel("트리거 활성화:", this), 2, 0);
    triggerActivationLabel = new QLabel("-", this);
    triggerActivationLabel->setStyleSheet("QLabel { font-weight: bold; color: #333; }");
    infoLayout->addWidget(triggerActivationLabel, 2, 1);
    
    // 감지 횟수
    infoLayout->addWidget(new QLabel("감지 횟수:", this), 3, 0);
    triggerCountLabel = new QLabel("0", this);
    triggerCountLabel->setStyleSheet("QLabel { font-weight: bold; color: #2196F3; font-size: 14px; }");
    infoLayout->addWidget(triggerCountLabel, 3, 1);
    
    infoLayout->setColumnStretch(1, 1);
    triggerLayout->addLayout(infoLayout);
    
    // 시그널 연결
    connect(startTriggerTestBtn, &QPushButton::clicked, this, &CameraSettingsDialog::onStartTriggerTest);
    connect(stopTriggerTestBtn, &QPushButton::clicked, this, &CameraSettingsDialog::onStopTriggerTest);
    
    // 타이머 설정 (500ms 주기로 상태 업데이트)
    triggerTestTimer = new QTimer(this);
    connect(triggerTestTimer, &QTimer::timeout, this, &CameraSettingsDialog::onTriggerTestTimeout);
}

#ifdef USE_SPINNAKER
void CameraSettingsDialog::updateCurrentUserSetLabel() {
    int selectedCameraIndex = getSelectedCameraIndex();
    if (selectedCameraIndex < 0 || selectedCameraIndex >= static_cast<int>(m_spinCameras.size())) {
        currentUserSetLabel->setText("카메라 선택 필요");
        currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #FF9800; }");
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
            return;
        }
        
        // 현재 UserSet 항목 가져오기
        auto entry = ptrUserSetSelector->GetCurrentEntry();
        if (IsReadable(entry)) {
            QString currentUserSet = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
            QString modeName;
            
            if (currentUserSet == "UserSet0") {
                modeName = "LIVE 모드";
            } else if (currentUserSet == "UserSet1") {
                modeName = "TRIGGER 모드";
            } else {
                modeName = "기타";
            }
            
            currentUserSetLabel->setText(QString("%1 (%2)").arg(currentUserSet).arg(modeName));
            currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #4CAF50; }");
        } else {
            currentUserSetLabel->setText("조회 불가");
            currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #f44336; }");
        }
    } catch (const std::exception& e) {
        currentUserSetLabel->setText("오류");
        currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #f44336; }");
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

    try {
        Spinnaker::CameraPtr camera = m_spinCameras[selectedCameraIndex];
        Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
        
        // 카메라 스트리밍 상태 확인 및 정지
        bool wasStreaming = false;
        if (camera->IsStreaming()) {
            wasStreaming = true;
            camera->EndAcquisition();
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
        
        // UserSetLoad 노드 가져오기  
        Spinnaker::GenApi::CCommandPtr ptrUserSetLoad = nodeMap.GetNode("UserSetLoad");
        if (!IsAvailable(ptrUserSetLoad) || !IsWritable(ptrUserSetLoad)) {
            throw std::runtime_error("UserSetLoad 노드를 사용할 수 없습니다.");
        }
        
        // UserSet 로드 실행
        ptrUserSetLoad->Execute();
        
        // 약간의 지연 추가 (UserSet 로드 완료 대기)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // ===== UserSet 로드 후 런타임 설정 적용 =====
        // UserSet 로드만으로는 카메라의 센서 설정에 반영되지 않을 수 있으므로
        // 중요한 파라미터들을 명시적으로 다시 설정하여 카메라에 적용
        
        try {
            // TriggerMode 확인 및 재설정
            auto pTriggerMode = nodeMap.GetNode("TriggerMode");
            if (pTriggerMode && Spinnaker::GenApi::IsWritable(pTriggerMode)) {
                Spinnaker::GenApi::CEnumerationPtr triggerModePtr(pTriggerMode);
                auto entry = triggerModePtr->GetCurrentEntry();
                if (entry && Spinnaker::GenApi::IsReadable(entry)) {
                    QString triggerMode = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
                    qDebug() << "Current TriggerMode after load:" << triggerMode;
                }
            }
            
            // AcquisitionMode 설정 (SingleFrame 또는 Continuous)
            auto pAcquisitionMode = nodeMap.GetNode("AcquisitionMode");
            if (pAcquisitionMode && Spinnaker::GenApi::IsWritable(pAcquisitionMode)) {
                Spinnaker::GenApi::CEnumerationPtr acqModePtr(pAcquisitionMode);
                auto singleFrameEntry = acqModePtr->GetEntryByName("SingleFrame");
                if (singleFrameEntry && Spinnaker::GenApi::IsReadable(singleFrameEntry)) {
                    acqModePtr->SetIntValue(singleFrameEntry->GetValue());
                    qDebug() << "AcquisitionMode set to SingleFrame";
                }
            }
            
            // TriggerSelector (Frameburst 등)
            auto pTriggerSelector = nodeMap.GetNode("TriggerSelector");
            if (pTriggerSelector && Spinnaker::GenApi::IsWritable(pTriggerSelector)) {
                Spinnaker::GenApi::CEnumerationPtr triggerSelPtr(pTriggerSelector);
                auto framestartEntry = triggerSelPtr->GetEntryByName("FrameStart");
                if (framestartEntry && Spinnaker::GenApi::IsReadable(framestartEntry)) {
                    triggerSelPtr->SetIntValue(framestartEntry->GetValue());
                    qDebug() << "TriggerSelector set to FrameStart";
                }
            }
            
            // 다시 약간의 지연
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            
        } catch (const std::exception& e) {
            qDebug() << "Warning: Runtime parameter apply error:" << e.what();
            // 에러가 발생해도 계속 진행
        }
        
        // 현재 UserSet 표시 업데이트
        currentUserSetLabel->setText(QString("%1 (%2)").arg(userSetName).arg(modeName));
        currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #4CAF50; }");
        
        // 스트리밍이 실행 중이었다면 다시 시작
        if (wasStreaming) {
            camera->BeginAcquisition();
        }
        
        // 파라미터 출력
        printCameraParameters(nodeMap, QString("%1 로드 후").arg(userSetName));
        
        currentUserSetLabel->setText(QString("%1 로드됨").arg(userSetName));
        currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #4CAF50; }");
        
    } catch (const Spinnaker::Exception& e) {
        currentUserSetLabel->setText("로드 실패");
        currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #f44336; }");
        QMessageBox::critical(this, "오류", 
            QString("UserSet 로드 실패: %1").arg(QString::fromStdString(e.what())));
    } catch (const std::exception& e) {
        currentUserSetLabel->setText("로드 실패");
        currentUserSetLabel->setStyleSheet("QLabel { font-weight: bold; color: #f44336; }");
        QMessageBox::critical(this, "오류", 
            QString("UserSet 로드 실패: %1").arg(QString::fromStdString(e.what())));
    }
}

void CameraSettingsDialog::onLoadUserSet0() {
    loadUserSet("UserSet0", "LIVE 모드");
}

void CameraSettingsDialog::onLoadUserSet1() {
    loadUserSet("UserSet1", "TRIGGER 모드");
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
            return;
        }
        
        Spinnaker::GenApi::CEnumerationPtr triggerModePtr(pTriggerMode);
        auto entry = triggerModePtr->GetCurrentEntry();
        QString currentMode = QString::fromLocal8Bit(entry->GetSymbolic().c_str());
        
        if (currentMode != "On") {
            triggerStatusLabel->setText("트리거 모드 OFF (UserSet1 로드필요)");
            triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #f44336; }");
            return;
        }
        
        // 테스트 시작
        isTriggerTesting = true;
        triggerDetectionCount = 0;
        lastExposureCount = 0;  // 초기값 리셋
        
        triggerStatusLabel->setText("테스트 중...");
        triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #2196F3; }");
        
        startTriggerTestBtn->setEnabled(false);
        stopTriggerTestBtn->setEnabled(true);
        
        triggerTestTimer->start(500);  // 500ms 주기로 상태 업데이트
        
    } catch (const std::exception& e) {
        triggerStatusLabel->setText("오류 발생");
        triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #f44336; }");
        qDebug() << "Trigger test start error:" << e.what();
    }
}

void CameraSettingsDialog::onStopTriggerTest() {
    isTriggerTesting = false;
    triggerTestTimer->stop();
    
    triggerStatusLabel->setText(QString("테스트 완료 (감지: %1회)").arg(triggerDetectionCount));
    triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #4CAF50; }");
    
    startTriggerTestBtn->setEnabled(true);
    stopTriggerTestBtn->setEnabled(false);
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
        
        // ===== 실제 트리거 감지: Spinnaker SDK 통계 정보 사용 =====
        // EventExposureEnd 이벤트를 통해 실제 노출(프레임) 발생 감지
        Spinnaker::GenApi::INodeMap& tlNodeMap = camera->GetTLStreamNodeMap();
        
        // StreamBufferCount (처리된 버퍼 수)로 트리거 감지
        auto pStreamBufferCount = tlNodeMap.GetNode("StreamBufferCount");
        if (pStreamBufferCount && Spinnaker::GenApi::IsReadable(pStreamBufferCount)) {
            Spinnaker::GenApi::CIntegerPtr bufferCountPtr(pStreamBufferCount);
            int currentBufferCount = static_cast<int>(bufferCountPtr->GetValue());
            
            // 버퍼 카운트가 증가 = 새로운 프레임(트리거 발생) 감지
            if (currentBufferCount > lastExposureCount) {
                {
                    std::lock_guard<std::mutex> lock(triggerCountMutex);
                    triggerDetectionCount++;
                }
                lastExposureCount = currentBufferCount;
                
                // UI 피드백
                triggerCountLabel->setText(QString::number(triggerDetectionCount));
                triggerCountLabel->setStyleSheet("QLabel { font-weight: bold; color: #2196F3; font-size: 14px; }");
                
                // 잠깐 다른 색으로 표시 후 돌아옴
                triggerStatusLabel->setText("트리거 감지!");
                triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #4CAF50; }");
                
                qDebug() << "트리거 감지됨! BufferCount:" << currentBufferCount << "총 감지:" << triggerDetectionCount;
                
                QTimer::singleShot(300, [this]() {
                    if (isTriggerTesting) {
                        triggerStatusLabel->setText("테스트 중...");
                        triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #2196F3; }");
                    }
                });
            }
        }
        
    } catch (const std::exception& e) {
        qDebug() << "Error updating trigger status:" << e.what();
    }
}

#endif
