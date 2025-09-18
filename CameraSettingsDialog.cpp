#include "CameraSettingsDialog.h"

CameraSettingsDialog::CameraSettingsDialog(QWidget* parent)
    : QDialog(parent)
    , isListening(false)
    , currentCameraIndex(-1)
    , triggerCheckTimer(new QTimer(this))
{
    setWindowTitle("카메라 설정");
    setMinimumSize(600, 400);
    
    setupUI();
    
    // 저장된 설정 로드
    loadSettings();
    
    // 타이머 시그널 연결
    connect(triggerCheckTimer, SIGNAL(timeout()), this, SLOT(checkHardwareTrigger()));
}

CameraSettingsDialog::~CameraSettingsDialog() {
    // 설정 저장
    saveSettings();
    
    if (isListening) {
        stopHardwareTriggerDetection();
    }
}

void CameraSettingsDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    
    // 다이얼로그가 표시될 때마다 카메라 설정을 다시 로드
    if (currentCameraIndex >= 0 && currentCameraIndex < static_cast<int>(m_spinCameras.size())) {
        std::cout << "다이얼로그 표시 - 카메라 설정 다시 로드" << std::endl;
        loadCurrentCameraSettings();
    }
}

void CameraSettingsDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // 카메라 선택 그룹
    QGroupBox* cameraGroup = new QGroupBox("카메라 선택", this);
    QVBoxLayout* cameraLayout = new QVBoxLayout(cameraGroup);
    
    QHBoxLayout* cameraSelectLayout = new QHBoxLayout;
    cameraSelectLayout->addWidget(new QLabel("카메라:", this));
    
    // 카메라 콤보박스 (Spinnaker 카메라로 채워질 예정)
    cameraCombo = new QComboBox(this);
    cameraCombo->addItem("카메라를 검색 중...");
    connect(cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            [this](int index) { 
                currentCameraIndex = index;
                statusLabel->setText(QString("상태: 카메라 %1 선택됨").arg(index + 1));
                loadCurrentCameraSettings(); // 카메라 선택 시 설정 로드
            });
    cameraSelectLayout->addWidget(cameraCombo);
    cameraSelectLayout->addStretch();
    
    cameraLayout->addLayout(cameraSelectLayout);
    mainLayout->addWidget(cameraGroup);
    
    // 트리거 설정 그룹
    QGroupBox* triggerGroup = new QGroupBox("트리거 설정", this);
    QVBoxLayout* triggerLayout = new QVBoxLayout(triggerGroup);
    
    // 트리거 모드
    QHBoxLayout* triggerModeLayout = new QHBoxLayout;
    triggerModeLayout->addWidget(new QLabel("트리거 모드:", this));
    triggerModeCombo = new QComboBox(this);
    triggerModeCombo->addItem("Off (연속 촬영)", "Off");
    triggerModeCombo->addItem("On (트리거 사용)", "On");
    triggerModeLayout->addWidget(triggerModeCombo);
    triggerModeLayout->addStretch();
    triggerLayout->addLayout(triggerModeLayout);
    
    // 트리거 소스
    QHBoxLayout* triggerSourceLayout = new QHBoxLayout;
    triggerSourceLayout->addWidget(new QLabel("트리거 소스:", this));
    triggerSourceCombo = new QComboBox(this);
    triggerSourceCombo->addItem("Software (소프트웨어)", "Software");
    triggerSourceCombo->addItem("Line0 (하드웨어)", "Line0");
    triggerSourceCombo->addItem("Line1 (하드웨어)", "Line1");
    triggerSourceCombo->addItem("Line2 (하드웨어)", "Line2");
    triggerSourceCombo->addItem("Line3 (하드웨어)", "Line3");
    triggerSourceLayout->addWidget(triggerSourceCombo);
    triggerSourceLayout->addStretch();
    triggerLayout->addLayout(triggerSourceLayout);
    
    // 트리거 선택자
    QHBoxLayout* triggerSelectorLayout = new QHBoxLayout;
    triggerSelectorLayout->addWidget(new QLabel("트리거 선택자:", this));
    triggerSelectorCombo = new QComboBox(this);
    triggerSelectorCombo->addItem("FrameStart", "FrameStart");
    triggerSelectorCombo->addItem("AcquisitionStart", "AcquisitionStart");
    triggerSelectorLayout->addWidget(triggerSelectorCombo);
    triggerSelectorLayout->addStretch();
    triggerLayout->addLayout(triggerSelectorLayout);
    
    // 트리거 활성화
    QHBoxLayout* triggerActivationLayout = new QHBoxLayout;
    triggerActivationLayout->addWidget(new QLabel("트리거 활성화:", this));
    triggerActivationCombo = new QComboBox(this);
    triggerActivationCombo->addItem("Rising Edge", "RisingEdge");
    triggerActivationCombo->addItem("Falling Edge", "FallingEdge");
    triggerActivationLayout->addWidget(triggerActivationCombo);
    triggerActivationLayout->addStretch();
    triggerLayout->addLayout(triggerActivationLayout);
    
    // 트리거 딜레이
    QHBoxLayout* triggerDelayLayout = new QHBoxLayout;
    triggerDelayLayout->addWidget(new QLabel("트리거 딜레이 (μs):", this));
    triggerDelaySpinBox = new QSpinBox(this);
    triggerDelaySpinBox->setRange(0, 1000000);
    triggerDelaySpinBox->setValue(0);
    triggerDelayLayout->addWidget(triggerDelaySpinBox);
    triggerDelayLayout->addStretch();
    triggerLayout->addLayout(triggerDelayLayout);
    
    mainLayout->addWidget(triggerGroup);
    
    // 노출/게인 설정 그룹
    QGroupBox* imageGroup = new QGroupBox("이미지 설정", this);
    QVBoxLayout* imageLayout = new QVBoxLayout(imageGroup);
    
    // 노출 자동
    QHBoxLayout* exposureAutoLayout = new QHBoxLayout;
    exposureAutoLayout->addWidget(new QLabel("노출 자동:", this));
    exposureAutoCombo = new QComboBox(this);
    exposureAutoCombo->addItem("Off", "Off");
    exposureAutoCombo->addItem("Once", "Once");
    exposureAutoCombo->addItem("Continuous", "Continuous");
    exposureAutoLayout->addWidget(exposureAutoCombo);
    exposureAutoLayout->addStretch();
    imageLayout->addLayout(exposureAutoLayout);
    
    // 노출 시간
    QHBoxLayout* exposureTimeLayout = new QHBoxLayout;
    exposureTimeLayout->addWidget(new QLabel("노출 시간 (μs):", this));
    exposureSpinBox = new QSpinBox(this);
    exposureSpinBox->setRange(1, 1000000);
    exposureSpinBox->setValue(10000);
    exposureTimeLayout->addWidget(exposureSpinBox);
    exposureTimeLayout->addStretch();
    imageLayout->addLayout(exposureTimeLayout);
    
    // 게인 자동
    QHBoxLayout* gainAutoLayout = new QHBoxLayout;
    gainAutoLayout->addWidget(new QLabel("게인 자동:", this));
    gainAutoCombo = new QComboBox(this);
    gainAutoCombo->addItem("Off", "Off");
    gainAutoCombo->addItem("Once", "Once");
    gainAutoCombo->addItem("Continuous", "Continuous");
    gainAutoLayout->addWidget(gainAutoCombo);
    gainAutoLayout->addStretch();
    imageLayout->addLayout(gainAutoLayout);
    
    // 게인 값
    QHBoxLayout* gainValueLayout = new QHBoxLayout;
    gainValueLayout->addWidget(new QLabel("게인 값 (dB):", this));
    gainSpinBox = new QSpinBox(this);
    gainSpinBox->setRange(0, 40);
    gainSpinBox->setValue(0);
    gainValueLayout->addWidget(gainSpinBox);
    gainValueLayout->addStretch();
    imageLayout->addLayout(gainValueLayout);
    
    mainLayout->addWidget(imageGroup);
    
    // 트리거 테스트 그룹
    QGroupBox* testGroup = new QGroupBox("트리거 테스트", this);
    QVBoxLayout* testLayout = new QVBoxLayout(testGroup);
    
    // 트리거 상태 표시
    triggerStatusLabel = new QLabel("트리거 상태: 대기 중", this);
    triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #666; padding: 5px; }");
    testLayout->addWidget(triggerStatusLabel);
    
    // 테스트 버튼들
    QHBoxLayout* testBtnLayout = new QHBoxLayout;
    startListeningBtn = new QPushButton("트리거 감지 시작", this);
    stopListeningBtn = new QPushButton("트리거 감지 중지", this);
    stopListeningBtn->setEnabled(false);
    
    testBtnLayout->addWidget(startListeningBtn);
    testBtnLayout->addWidget(stopListeningBtn);
    testBtnLayout->addStretch();
    testLayout->addLayout(testBtnLayout);
    
    mainLayout->addWidget(testGroup);
    
    // 상태 레이블
    statusLabel = new QLabel("상태: 준비", this);
    statusLabel->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 8px; border: 1px solid #ccc; border-radius: 4px; }");
    mainLayout->addWidget(statusLabel);
    
    // 버튼들
    QHBoxLayout* buttonLayout = new QHBoxLayout;
    QPushButton* applyBtn = new QPushButton("설정 적용", this);
    QPushButton* closeBtn = new QPushButton("닫기", this);
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(applyBtn);
    buttonLayout->addWidget(closeBtn);
    
    mainLayout->addLayout(buttonLayout);
    
    // 시그널 연결
    connect(applyBtn, &QPushButton::clicked, this, &CameraSettingsDialog::applySettings);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(startListeningBtn, &QPushButton::clicked, this, &CameraSettingsDialog::startHardwareTriggerDetection);
    connect(stopListeningBtn, &QPushButton::clicked, this, &CameraSettingsDialog::stopHardwareTriggerDetection);
    
    // 트리거 모드 변경 시 트리거 테스트 버튼 활성화/비활성화
    connect(triggerModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &CameraSettingsDialog::onTriggerModeChanged);
    
    // 트리거 소스 변경 시에도 UI 업데이트
    connect(triggerSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &CameraSettingsDialog::onTriggerModeChanged);
    
    // 설정 변경 시 자동 저장만 (카메라 적용은 테스트 버튼 클릭 시)
    connect(triggerModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &CameraSettingsDialog::saveSettings);
    connect(triggerSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &CameraSettingsDialog::saveSettings);
    connect(triggerSelectorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &CameraSettingsDialog::saveSettings);
    connect(triggerActivationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &CameraSettingsDialog::saveSettings);
    connect(triggerDelaySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &CameraSettingsDialog::saveSettings);
    connect(exposureAutoCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &CameraSettingsDialog::saveSettings);
    connect(exposureSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &CameraSettingsDialog::saveSettings);
    connect(gainAutoCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &CameraSettingsDialog::saveSettings);
    connect(gainSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &CameraSettingsDialog::saveSettings);
}

void CameraSettingsDialog::applySettings() {
    int selectedCameraIndex = getSelectedCameraIndex();
    
    statusLabel->setText("상태: 설정 적용 시작...");
    
    if (selectedCameraIndex < 0 || selectedCameraIndex >= static_cast<int>(m_spinCameras.size())) {
        statusLabel->setText(QString("상태: 카메라 선택 오류 (인덱스: %1, 카메라 수: %2)").arg(selectedCameraIndex).arg(m_spinCameras.size()));
        return;
    }

#ifdef USE_SPINNAKER
    try {
        auto& camera = m_spinCameras[selectedCameraIndex];
        statusLabel->setText("상태: 카메라 객체 확보됨");
        
        // 카메라가 초기화되어 있는지 확인
        if (!camera->IsInitialized()) {
            statusLabel->setText("상태: 카메라 초기화 중...");
            camera->Init();
            statusLabel->setText("상태: 카메라 초기화 완료");
        }
        
        // 카메라가 스트리밍 중이면 중지
        if (camera->IsStreaming()) {
            statusLabel->setText("상태: 스트리밍 중지 중...");
            camera->EndAcquisition();
            statusLabel->setText("상태: 스트리밍 중지 완료");
        }
        
        Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
        statusLabel->setText("상태: NodeMap 접근 성공");
        
        QString triggerMode = triggerModeCombo->currentData().toString();
        statusLabel->setText(QString("상태: 트리거 모드 설정 중... (%1)").arg(triggerMode));
        
        // 트리거 설정 (예제 코드 방식 적용)
        Spinnaker::GenApi::CEnumerationPtr ptrTriggerMode = nodeMap.GetNode("TriggerMode");
        if (!IsReadable(ptrTriggerMode)) {
            statusLabel->setText("상태: TriggerMode 노드를 읽을 수 없습니다");
            return;
        }
        
        // 먼저 TriggerMode를 Off로 설정 (예제 방식)
        Spinnaker::GenApi::CEnumEntryPtr ptrTriggerModeOff = ptrTriggerMode->GetEntryByName("Off");
        if (!IsReadable(ptrTriggerModeOff)) {
            statusLabel->setText("상태: TriggerMode Off 엔트리를 읽을 수 없습니다");
            return;
        }
        
        if (!IsWritable(ptrTriggerMode)) {
            statusLabel->setText("상태: TriggerMode 노드가 쓰기 불가능합니다");
            return;
        }
        
        statusLabel->setText("상태: TriggerMode를 Off로 설정 중...");
        ptrTriggerMode->SetIntValue(ptrTriggerModeOff->GetValue());
        statusLabel->setText("상태: TriggerMode Off 설정 완료");
        
        if (triggerMode == "Off") {
            statusLabel->setText("상태: 연속 촬영 모드 설정 완료");
            
        } else if (triggerMode == "On") {
            statusLabel->setText("상태: 트리거 모드 설정 중...");
            
            // 트리거 선택자 설정 (먼저 설정)
            QString triggerSelector = triggerSelectorCombo->currentData().toString();
            Spinnaker::GenApi::CEnumerationPtr ptrTriggerSelector = nodeMap.GetNode("TriggerSelector");
            if (IsReadable(ptrTriggerSelector) && IsWritable(ptrTriggerSelector)) {
                Spinnaker::GenApi::CEnumEntryPtr ptrTriggerSelectorEntry = ptrTriggerSelector->GetEntryByName(triggerSelector.toStdString().c_str());
                if (IsReadable(ptrTriggerSelectorEntry)) {
                    statusLabel->setText(QString("상태: TriggerSelector를 %1로 설정 중...").arg(triggerSelector));
                    ptrTriggerSelector->SetIntValue(ptrTriggerSelectorEntry->GetValue());
                }
            }
            
            // 트리거 소스 설정 (사용자가 선택한 소스에 따라)
            QString triggerSource = triggerSourceCombo->currentData().toString();
            Spinnaker::GenApi::CEnumerationPtr ptrTriggerSource = nodeMap.GetNode("TriggerSource");
            if (!IsReadable(ptrTriggerSource)) {
                statusLabel->setText("상태: TriggerSource 노드를 읽을 수 없습니다");
                return;
            }
            if (!IsWritable(ptrTriggerSource)) {
                statusLabel->setText("상태: TriggerSource 노드가 쓰기 불가능합니다");
                return;
            }
            
            Spinnaker::GenApi::CEnumEntryPtr ptrTriggerSourceEntry = ptrTriggerSource->GetEntryByName(triggerSource.toStdString().c_str());
            if (!IsReadable(ptrTriggerSourceEntry)) {
                statusLabel->setText(QString("상태: TriggerSource %1 엔트리를 읽을 수 없습니다").arg(triggerSource));
                return;
            }
            
            if (triggerSource == "Software") {
                statusLabel->setText("상태: 소프트웨어 트리거로 설정 중...");
            } else {
                statusLabel->setText(QString("상태: 하드웨어 트리거(%1)로 설정 중...").arg(triggerSource));
            }
            
            ptrTriggerSource->SetIntValue(ptrTriggerSourceEntry->GetValue());
            
            // 트리거 활성화 설정
            QString triggerActivation = triggerActivationCombo->currentData().toString();
            Spinnaker::GenApi::CEnumerationPtr ptrTriggerActivation = nodeMap.GetNode("TriggerActivation");
            if (IsReadable(ptrTriggerActivation) && IsWritable(ptrTriggerActivation)) {
                Spinnaker::GenApi::CEnumEntryPtr ptrTriggerActivationEntry = ptrTriggerActivation->GetEntryByName(triggerActivation.toStdString().c_str());
                if (IsReadable(ptrTriggerActivationEntry)) {
                    statusLabel->setText(QString("상태: TriggerActivation을 %1로 설정 중...").arg(triggerActivation));
                    ptrTriggerActivation->SetIntValue(ptrTriggerActivationEntry->GetValue());
                }
            }
            
            // 트리거 딜레이 설정
            Spinnaker::GenApi::CFloatPtr ptrTriggerDelay = nodeMap.GetNode("TriggerDelay");
            if (IsReadable(ptrTriggerDelay) && IsWritable(ptrTriggerDelay)) {
                double delayValue = static_cast<double>(triggerDelaySpinBox->value());
                double minValue = ptrTriggerDelay->GetMin();
                double maxValue = ptrTriggerDelay->GetMax();
                
                if (delayValue < minValue) delayValue = minValue;
                if (delayValue > maxValue) delayValue = maxValue;
                
                statusLabel->setText(QString("상태: TriggerDelay를 %1μs로 설정 중...").arg(delayValue));
                ptrTriggerDelay->SetValue(delayValue);
            }
            
            // 마지막에 트리거 모드를 On으로 설정
            Spinnaker::GenApi::CEnumEntryPtr ptrTriggerModeOn = ptrTriggerMode->GetEntryByName("On");
            if (!IsReadable(ptrTriggerModeOn)) {
                statusLabel->setText("상태: TriggerMode On 엔트리를 읽을 수 없습니다");
                return;
            }
            
            statusLabel->setText("상태: TriggerMode를 On으로 설정 중...");
            ptrTriggerMode->SetIntValue(ptrTriggerModeOn->GetValue());
            
            // 완료 메시지 - 트리거 소스에 따라 구분
            if (triggerSource == "Software") {
                statusLabel->setText("상태: 소프트웨어 트리거 설정 완료");
            } else {
                statusLabel->setText(QString("상태: 하드웨어 트리거(%1) 설정 완료").arg(triggerSource));
            }
        }
        
        statusLabel->setText("상태: 노출 설정 적용 중...");
        // 노출 설정 (예제 방식)
        QString exposureAutoStr = exposureAutoCombo->currentData().toString();
        Spinnaker::GenApi::CEnumerationPtr ptrExposureAuto = nodeMap.GetNode("ExposureAuto");
        if (IsReadable(ptrExposureAuto) && IsWritable(ptrExposureAuto)) {
            Spinnaker::GenApi::CEnumEntryPtr ptrExposureAutoEntry = ptrExposureAuto->GetEntryByName(exposureAutoStr.toStdString().c_str());
            if (IsReadable(ptrExposureAutoEntry)) {
                statusLabel->setText(QString("상태: ExposureAuto를 %1로 설정 중...").arg(exposureAutoStr));
                ptrExposureAuto->SetIntValue(ptrExposureAutoEntry->GetValue());
            }
        }
        
        if (exposureAutoStr == "Off") {
            Spinnaker::GenApi::CFloatPtr ptrExposureTime = nodeMap.GetNode("ExposureTime");
            if (IsReadable(ptrExposureTime) && IsWritable(ptrExposureTime)) {
                double exposureValue = exposureSpinBox->value();
                
                // 허용 범위 확인 및 조정
                double minValue = ptrExposureTime->GetMin();
                double maxValue = ptrExposureTime->GetMax();
                
                if (exposureValue < minValue) {
                    exposureValue = minValue;
                    statusLabel->setText(QString("상태: ExposureTime 값이 최소값 %1μs로 조정됨").arg(minValue));
                } else if (exposureValue > maxValue) {
                    exposureValue = maxValue;
                    statusLabel->setText(QString("상태: ExposureTime 값이 최대값 %1μs로 조정됨").arg(maxValue));
                }
                
                statusLabel->setText(QString("상태: ExposureTime을 %1μs로 설정 중... (범위: %2~%3)")
                                   .arg(exposureValue).arg(minValue).arg(maxValue));
                ptrExposureTime->SetValue(exposureValue);
            }
        }
        
        statusLabel->setText("상태: 게인 설정 적용 중...");
        // 게인 설정 (예제 방식)
        QString gainAutoStr = gainAutoCombo->currentData().toString();
        Spinnaker::GenApi::CEnumerationPtr ptrGainAuto = nodeMap.GetNode("GainAuto");
        if (IsReadable(ptrGainAuto) && IsWritable(ptrGainAuto)) {
            Spinnaker::GenApi::CEnumEntryPtr ptrGainAutoEntry = ptrGainAuto->GetEntryByName(gainAutoStr.toStdString().c_str());
            if (IsReadable(ptrGainAutoEntry)) {
                statusLabel->setText(QString("상태: GainAuto를 %1로 설정 중...").arg(gainAutoStr));
                ptrGainAuto->SetIntValue(ptrGainAutoEntry->GetValue());
            }
        }
        
        if (gainAutoStr == "Off") {
            Spinnaker::GenApi::CFloatPtr ptrGain = nodeMap.GetNode("Gain");
            if (IsReadable(ptrGain) && IsWritable(ptrGain)) {
                double gainValue = gainSpinBox->value();
                
                // 허용 범위 확인 및 조정
                double minValue = ptrGain->GetMin();
                double maxValue = ptrGain->GetMax();
                
                if (gainValue < minValue) {
                    gainValue = minValue;
                    statusLabel->setText(QString("상태: Gain 값이 최소값 %1dB로 조정됨").arg(minValue));
                } else if (gainValue > maxValue) {
                    gainValue = maxValue;
                    statusLabel->setText(QString("상태: Gain 값이 최대값 %1dB로 조정됨").arg(maxValue));
                }
                
                statusLabel->setText(QString("상태: Gain을 %1dB로 설정 중... (범위: %2~%3)")
                                   .arg(gainValue).arg(minValue).arg(maxValue));
                ptrGain->SetValue(gainValue);
            }
        }
        
        statusLabel->setText("상태: UserSet 영구 저장 시도 중...");
        // Spinnaker 예제 방식으로 UserSet 저장 + 콘솔 디버깅
        std::cout << "\n========== UserSet 저장 시작 ==========" << std::endl;
        
        // 저장 전 현재 트리거 설정 확인
        Spinnaker::GenApi::CEnumerationPtr ptrTriggerModeCheck = nodeMap.GetNode("TriggerMode");
        Spinnaker::GenApi::CEnumerationPtr ptrTriggerSourceCheck = nodeMap.GetNode("TriggerSource");
        if (IsAvailable(ptrTriggerModeCheck) && IsReadable(ptrTriggerModeCheck)) {
            QString beforeSaveTriggerMode = QString::fromStdString(ptrTriggerModeCheck->GetCurrentEntry()->GetSymbolic().c_str());
            std::cout << "저장 전 트리거 모드: " << beforeSaveTriggerMode.toStdString() << std::endl;
        }
        if (IsAvailable(ptrTriggerSourceCheck) && IsReadable(ptrTriggerSourceCheck)) {
            QString beforeSaveTriggerSource = QString::fromStdString(ptrTriggerSourceCheck->GetCurrentEntry()->GetSymbolic().c_str());
            std::cout << "저장 전 트리거 소스: " << beforeSaveTriggerSource.toStdString() << std::endl;
        }
        
        try {
            // 1. 먼저 UserSetSelector를 UserSet1으로 설정
            Spinnaker::GenApi::CEnumerationPtr ptrUserSetSelector = nodeMap.GetNode("UserSetSelector");
            if (!IsAvailable(ptrUserSetSelector) || !IsWritable(ptrUserSetSelector)) {
                statusLabel->setText("상태: UserSetSelector 노드 접근 실패");
                std::cout << "UserSetSelector 노드 접근 실패!" << std::endl;
                throw std::runtime_error("UserSetSelector not available");
            }
            
            Spinnaker::GenApi::CEnumEntryPtr ptrUserSet1 = ptrUserSetSelector->GetEntryByName("UserSet1");
            if (!IsAvailable(ptrUserSet1) || !IsReadable(ptrUserSet1)) {
                statusLabel->setText("상태: UserSet1 엔트리 접근 실패");
                std::cout << "UserSet1 엔트리 접근 실패!" << std::endl;
                throw std::runtime_error("UserSet1 entry not available");
            }
            
            statusLabel->setText("상태: UserSetSelector를 UserSet1으로 설정 중...");
            ptrUserSetSelector->SetIntValue(ptrUserSet1->GetValue());
            std::cout << "UserSetSelector를 UserSet1으로 설정 완료" << std::endl;
            
            // 2. UserSetSave 명령 실행
            Spinnaker::GenApi::CCommandPtr ptrUserSetSave = nodeMap.GetNode("UserSetSave");
            if (!IsAvailable(ptrUserSetSave) || !IsWritable(ptrUserSetSave)) {
                statusLabel->setText("상태: UserSetSave 명령 접근 실패");
                std::cout << "UserSetSave 명령 접근 실패!" << std::endl;
                throw std::runtime_error("UserSetSave not available");
            }
            
            statusLabel->setText("상태: 현재 설정을 UserSet1에 저장 중...");
            std::cout << "UserSetSave 실행..." << std::endl;
            ptrUserSetSave->Execute();
            
            statusLabel->setText("상태: UserSet1 저장 완료");
            std::cout << "UserSet1 저장 완료" << std::endl;
            
            // 3. UserSetDefault를 UserSet1으로 설정 (카메라 시작시 자동 로드)
            Spinnaker::GenApi::CEnumerationPtr ptrUserSetDefault = nodeMap.GetNode("UserSetDefault");
            if (IsAvailable(ptrUserSetDefault) && IsWritable(ptrUserSetDefault)) {
                Spinnaker::GenApi::CEnumEntryPtr ptrUserSetDefaultEntry = ptrUserSetDefault->GetEntryByName("UserSet1");
                if (IsAvailable(ptrUserSetDefaultEntry) && IsReadable(ptrUserSetDefaultEntry)) {
                    statusLabel->setText("상태: UserSet1을 기본값으로 설정 중...");
                    std::cout << "UserSetDefault를 UserSet1으로 설정..." << std::endl;
                    ptrUserSetDefault->SetIntValue(ptrUserSetDefaultEntry->GetValue());
                    
                    // 기본값 설정 검증
                    QString currentDefault = QString::fromStdString(ptrUserSetDefault->GetCurrentEntry()->GetSymbolic().c_str());
                    std::cout << "UserSetDefault 설정 완료: " << currentDefault.toStdString() << std::endl;
                    statusLabel->setText(QString("상태: 기본값 설정 완료 - 현재: %1").arg(currentDefault));
                } else {
                    std::cout << "UserSetDefault 엔트리 접근 실패" << std::endl;
                }
            } else {
                std::cout << "UserSetDefault 노드 접근 실패" << std::endl;
            }
            
            // 저장 후 즉시 검증 - UserSet에 실제로 저장되었는지 확인
            std::cout << "저장 후 즉시 검증..." << std::endl;
            if (IsAvailable(ptrTriggerModeCheck) && IsReadable(ptrTriggerModeCheck)) {
                QString afterSaveTriggerMode = QString::fromStdString(ptrTriggerModeCheck->GetCurrentEntry()->GetSymbolic().c_str());
                std::cout << "저장 후 트리거 모드: " << afterSaveTriggerMode.toStdString() << std::endl;
            }
            if (IsAvailable(ptrTriggerSourceCheck) && IsReadable(ptrTriggerSourceCheck)) {
                QString afterSaveTriggerSource = QString::fromStdString(ptrTriggerSourceCheck->GetCurrentEntry()->GetSymbolic().c_str());
                std::cout << "저장 후 트리거 소스: " << afterSaveTriggerSource.toStdString() << std::endl;
            }
            
            std::cout << "======================================\n" << std::endl;
            
        } catch (const Spinnaker::Exception& e) {
            QString errorDetail = QString("UserSet 저장 오류: %1").arg(e.what());
            std::cout << "저장 예외: " << e.what() << std::endl;
            statusLabel->setText("상태: " + errorDetail);
        } catch (const std::exception& e) {
            QString errorDetail = QString("저장 오류: %1").arg(e.what());
            std::cout << "일반 예외: " << e.what() << std::endl;
            statusLabel->setText("상태: " + errorDetail);
        }
        
        // 설정이 제대로 적용되었는지 즉시 확인
        loadCurrentCameraSettings();
        statusLabel->setText("상태: 모든 설정이 성공적으로 적용되었습니다");
        statusLabel->setStyleSheet("QLabel { background-color: #d4edda; color: #155724; padding: 8px; border: 1px solid #c3e6cb; border-radius: 4px; }");
        
    } catch (const Spinnaker::Exception& e) {
        QString errorMsg = QString("카메라 설정 실패: %1 (오류 코드: %2)")
                          .arg(e.what())
                          .arg(e.GetError());
        statusLabel->setText("상태: " + errorMsg);
        statusLabel->setStyleSheet("QLabel { background-color: #f8d7da; color: #721c24; padding: 8px; border: 1px solid #f5c6cb; border-radius: 4px; }");
    } catch (const std::exception& e) {
        QString errorMsg = QString("일반 오류: %1").arg(e.what());
        statusLabel->setText("상태: " + errorMsg);
        statusLabel->setStyleSheet("QLabel { background-color: #f8d7da; color: #721c24; padding: 8px; border: 1px solid #f5c6cb; border-radius: 4px; }");
    }
#else
    statusLabel->setText("상태: Spinnaker SDK가 비활성화되어 있습니다");
#endif
}

void CameraSettingsDialog::applyTriggerModeOnly() {
    int selectedCameraIndex = getSelectedCameraIndex();
    
    if (selectedCameraIndex < 0 || selectedCameraIndex >= static_cast<int>(m_spinCameras.size())) {
        return;
    }

#ifdef USE_SPINNAKER
    try {
        auto& camera = m_spinCameras[selectedCameraIndex];
        
        // 카메라가 초기화되어 있는지 확인
        if (!camera->IsInitialized()) {
            camera->Init();
        }
        
        // 카메라가 스트리밍 중이면 중지
        bool wasStreaming = false;
        if (camera->IsStreaming()) {
            camera->EndAcquisition();
            wasStreaming = true;
        }
        
        Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
        QString triggerMode = triggerModeCombo->currentData().toString();
        
        // 트리거 설정
        Spinnaker::GenApi::CEnumerationPtr ptrTriggerMode = nodeMap.GetNode("TriggerMode");
        if (!IsReadable(ptrTriggerMode)) {
            return;
        }
        
        // 먼저 TriggerMode를 Off로 설정
        Spinnaker::GenApi::CEnumEntryPtr ptrTriggerModeOff = ptrTriggerMode->GetEntryByName("Off");
        if (IsReadable(ptrTriggerModeOff)) {
            ptrTriggerMode->SetIntValue(ptrTriggerModeOff->GetValue());
        }
        
        if (triggerMode == "On") {
            // 트리거 소스 설정
            QString triggerSource = triggerSourceCombo->currentData().toString();
            Spinnaker::GenApi::CEnumerationPtr ptrTriggerSource = nodeMap.GetNode("TriggerSource");
            if (IsReadable(ptrTriggerSource) && IsWritable(ptrTriggerSource)) {
                Spinnaker::GenApi::CEnumEntryPtr ptrTriggerSourceEntry = ptrTriggerSource->GetEntryByName(triggerSource.toStdString().c_str());
                if (IsReadable(ptrTriggerSourceEntry)) {
                    ptrTriggerSource->SetIntValue(ptrTriggerSourceEntry->GetValue());
                }
            }
            
            // 트리거 활성화 설정
            QString triggerActivation = triggerActivationCombo->currentData().toString();
            Spinnaker::GenApi::CEnumerationPtr ptrTriggerActivation = nodeMap.GetNode("TriggerActivation");
            if (IsReadable(ptrTriggerActivation) && IsWritable(ptrTriggerActivation)) {
                Spinnaker::GenApi::CEnumEntryPtr ptrTriggerActivationEntry = ptrTriggerActivation->GetEntryByName(triggerActivation.toStdString().c_str());
                if (IsReadable(ptrTriggerActivationEntry)) {
                    ptrTriggerActivation->SetIntValue(ptrTriggerActivationEntry->GetValue());
                }
            }
            
            // 트리거 딜레이 설정
            Spinnaker::GenApi::CFloatPtr ptrTriggerDelay = nodeMap.GetNode("TriggerDelay");
            if (IsReadable(ptrTriggerDelay) && IsWritable(ptrTriggerDelay)) {
                double delayValue = static_cast<double>(triggerDelaySpinBox->value());
                double minValue = ptrTriggerDelay->GetMin();
                double maxValue = ptrTriggerDelay->GetMax();
                
                if (delayValue < minValue) delayValue = minValue;
                if (delayValue > maxValue) delayValue = maxValue;
                
                ptrTriggerDelay->SetValue(delayValue);
            }
            
            // 트리거 모드를 On으로 설정
            Spinnaker::GenApi::CEnumEntryPtr ptrTriggerModeOn = ptrTriggerMode->GetEntryByName("On");
            if (IsReadable(ptrTriggerModeOn)) {
                ptrTriggerMode->SetIntValue(ptrTriggerModeOn->GetValue());
            }
        }
        
        // 스트리밍이 되고 있었다면 다시 시작
        if (wasStreaming) {
            camera->BeginAcquisition();
        }
        
    } catch (const Spinnaker::Exception& e) {
        // 조용히 처리 - 에러 메시지는 표시하지 않음
    } catch (const std::exception& e) {
        // 조용히 처리
    }
#endif
}

void CameraSettingsDialog::startHardwareTriggerDetection() {
    if (!isListening) {
        isListening = true;
        triggerCheckTimer->start(50);
        
        // 트리거 소스에 따라 다른 메시지 표시
        QString triggerSource = triggerSourceCombo->currentData().toString();
        if (triggerSource == "Software") {
            statusLabel->setText("상태: 소프트웨어 트리거 대기 중...");
            startListeningBtn->setText("소프트웨어 트리거 중지");
        } else {
            statusLabel->setText(QString("상태: 하드웨어 트리거(%1) 감지 중...").arg(triggerSource));
            startListeningBtn->setText("하드웨어 트리거 감지중지");
        }
        
        statusLabel->setStyleSheet("QLabel { background-color: #fff3cd; color: #856404; padding: 8px; border: 1px solid #ffeaa7; border-radius: 4px; }");
        
        startListeningBtn->setEnabled(false);
        stopListeningBtn->setEnabled(true);
    }
}

void CameraSettingsDialog::stopHardwareTriggerDetection() {
    if (isListening) {
        isListening = false;
        triggerCheckTimer->stop();
        
        statusLabel->setText("상태: 트리거 감지 중지됨");
        statusLabel->setStyleSheet("QLabel { background-color: #f0f0f0; padding: 8px; border: 1px solid #ccc; border-radius: 4px; }");
        triggerStatusLabel->setText("트리거 상태: 대기 중");
        triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #666; padding: 5px; }");
        
        // 버튼 텍스트를 트리거 소스에 맞게 복원
        QString triggerSource = triggerSourceCombo->currentData().toString();
        if (triggerSource == "Software") {
            startListeningBtn->setText("소프트웨어 트리거 시작");
        } else {
            startListeningBtn->setText("하드웨어 트리거 감지시작");
        }
        
        startListeningBtn->setEnabled(true);
        stopListeningBtn->setEnabled(false);
    }
}

void CameraSettingsDialog::checkHardwareTrigger() {
#ifdef USE_SPINNAKER
    if (!isListening || m_spinCameras.empty() || currentCameraIndex < 0) return;
    
    // UI에서 선택된 트리거 소스 확인 (사용자가 변경한 값)
    QString selectedTriggerSource = triggerSourceCombo->currentData().toString();
    QString selectedTriggerMode = triggerModeCombo->currentData().toString();
    
    // 트리거 모드가 Off면 연속촬영 모드
    if (selectedTriggerMode == "Off") {
        triggerStatusLabel->setText("트리거 상태: OFF - 연속촬영 모드");
        triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #999; padding: 5px; }");
        return;
    }
    
    // 소프트웨어 트리거일 때
    if (selectedTriggerSource == "Software") {
        triggerStatusLabel->setText("트리거 상태: 소프트웨어 트리거 준비됨");
        triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #0066cc; padding: 5px; }");
        return;
    }
    
    // 하드웨어 트리거일 때만 실제 카메라 상태 확인
    try {
        auto& camera = m_spinCameras[currentCameraIndex];
        Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
        
        // 실제 하드웨어 트리거 상태 확인 방법들:
        // 방법 1: 카메라 통계 확인 (프레임 카운트 변화)
        static int lastFrameCount = 0;
        Spinnaker::GenApi::CIntegerPtr ptrFrameCount = nodeMap.GetNode("DeviceFrameCount");
        if (IsAvailable(ptrFrameCount) && IsReadable(ptrFrameCount)) {
            int currentFrameCount = static_cast<int>(ptrFrameCount->GetValue());
            if (currentFrameCount != lastFrameCount) {
                triggerStatusLabel->setText(QString("트리거 상태: 하드웨어(%1) TRIGGERED! (프레임: %2)").arg(selectedTriggerSource).arg(currentFrameCount));
                triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #d73527; background-color: #f8d7da; padding: 5px; border-radius: 3px; }");
                std::cout << "하드웨어 트리거 감지됨! 프레임 카운트: " << currentFrameCount << std::endl;
                lastFrameCount = currentFrameCount;
                return;
            }
        }
        
        // 방법 2: LineStatus 확인 (가능한 경우)
        QString lineStatusNodeName = QString("LineStatus");
        if (selectedTriggerSource.startsWith("Line")) {
            // Line0, Line1, Line2, Line3에 대해 해당 라인 상태 확인
            lineStatusNodeName = QString("LineStatus%1").arg(selectedTriggerSource.right(1));
        }
        
        Spinnaker::GenApi::CBooleanPtr ptrLineStatus = nodeMap.GetNode(lineStatusNodeName.toStdString().c_str());
        if (IsAvailable(ptrLineStatus) && IsReadable(ptrLineStatus)) {
            bool lineActive = ptrLineStatus->GetValue();
            if (lineActive) {
                triggerStatusLabel->setText(QString("트리거 상태: 하드웨어(%1) LINE ACTIVE!").arg(selectedTriggerSource));
                triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #d73527; background-color: #f8d7da; padding: 5px; border-radius: 3px; }");
                std::cout << "하드웨어 라인 활성화 감지: " << selectedTriggerSource.toStdString() << std::endl;
                return;
            }
        }
        
        // 방법 3: 이미지 획득 시도로 트리거 감지
        if (camera->IsStreaming()) {
            try {
                // 비차단 방식으로 이미지 확인 (타임아웃 1ms)
                Spinnaker::ImagePtr image = camera->GetNextImage(1);
                if (image && !image->IsIncomplete()) {
                    triggerStatusLabel->setText(QString("트리거 상태: 하드웨어(%1) 이미지 획득!").arg(selectedTriggerSource));
                    triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #d73527; background-color: #f8d7da; padding: 5px; border-radius: 3px; }");
                    std::cout << "하드웨어 트리거로 이미지 획득됨!" << std::endl;
                    image->Release();
                    return;
                }
            } catch (Spinnaker::Exception& e) {
                // 타임아웃은 정상 (트리거 대기 중)
            }
        }
        
        // 기본 상태: 대기 중
        triggerStatusLabel->setText(QString("트리거 상태: 하드웨어(%1) 대기 중").arg(selectedTriggerSource));
        triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #666; padding: 5px; }");
        
        // 실제 하드웨어 트리거 신호 감지 로직은 여기에 구현
        // (카메라 API나 GPIO를 통해 실제 신호 상태 확인)
        
    } catch (const Spinnaker::Exception& e) {
        triggerStatusLabel->setText("트리거 상태: 오류 발생");
        std::cout << "트리거 확인 오류: " << e.what() << std::endl;
    }
#endif
}

int CameraSettingsDialog::getSelectedCameraIndex() const {
    return currentCameraIndex;
}

void CameraSettingsDialog::loadCameraSettings() {
    statusLabel->setText("상태: 카메라 설정 로드됨");
    loadCurrentCameraSettings();
}

void CameraSettingsDialog::loadCurrentCameraSettings() {
    if (currentCameraIndex >= 0 && currentCameraIndex < static_cast<int>(m_spinCameras.size())) {
#ifdef USE_SPINNAKER
        try {
            auto& camera = m_spinCameras[currentCameraIndex];
            
            if (!camera->IsInitialized()) {
                statusLabel->setText("상태: 카메라가 초기화되지 않았습니다");
                return;
            }
            
            Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
            
            // 현재 카메라 상태 정보 읽기만 (아무것도 변경하지 않음)
            std::cout << "\n========== 카메라 설정 정보 읽기 ===========" << std::endl;
            
            // 0. 현재 UserSetDefault 확인
            Spinnaker::GenApi::CEnumerationPtr ptrUserSetDefault = nodeMap.GetNode("UserSetDefault");
            if (IsAvailable(ptrUserSetDefault) && IsReadable(ptrUserSetDefault)) {
                QString currentUserSetDefault = QString::fromStdString(ptrUserSetDefault->GetCurrentEntry()->GetSymbolic().c_str());
                std::cout << "현재 UserSetDefault: " << currentUserSetDefault.toStdString() << std::endl;
            }
            
            // UserSet 현재 선택 확인
            Spinnaker::GenApi::CEnumerationPtr ptrUserSetSelector = nodeMap.GetNode("UserSetSelector");
            if (IsAvailable(ptrUserSetSelector) && IsReadable(ptrUserSetSelector)) {
                QString currentUserSetSelector = QString::fromStdString(ptrUserSetSelector->GetCurrentEntry()->GetSymbolic().c_str());
                std::cout << "현재 UserSetSelector: " << currentUserSetSelector.toStdString() << std::endl;
            }
            
            // 1. 현재 트리거 모드 읽기 및 UI 반영
            Spinnaker::GenApi::CEnumerationPtr ptrTriggerMode = nodeMap.GetNode("TriggerMode");
            QString currentTriggerMode = "Unknown";
            if (IsAvailable(ptrTriggerMode) && IsReadable(ptrTriggerMode)) {
                currentTriggerMode = QString::fromStdString(ptrTriggerMode->GetCurrentEntry()->GetSymbolic().c_str());
                std::cout << "현재 트리거 모드: " << currentTriggerMode.toStdString() << std::endl;
                
                // UI에 현재 트리거 모드 설정
                int triggerIndex = triggerModeCombo->findData(currentTriggerMode);
                if (triggerIndex >= 0) {
                    triggerModeCombo->setCurrentIndex(triggerIndex);
                    std::cout << "UI 트리거 모드 설정 완료: 인덱스 " << triggerIndex << std::endl;
                } else {
                    std::cout << "UI 트리거 모드 설정 실패: " << currentTriggerMode.toStdString() << " 항목을 찾을 수 없음" << std::endl;
                }
                
                // UI에 표시된 트리거 모드 확인
                QString uiTriggerMode = triggerModeCombo->currentData().toString();
                std::cout << "UI에 표시된 트리거 모드: " << uiTriggerMode.toStdString() << std::endl;
            }
            
            // 2. 트리거 소스 읽기 및 UI 반영
            QString currentTriggerSource = "Unknown";
            if (currentTriggerMode == "On") {  // 트리거가 On일 때만 소스 확인
                Spinnaker::GenApi::CEnumerationPtr ptrTriggerSource = nodeMap.GetNode("TriggerSource");
                if (IsAvailable(ptrTriggerSource) && IsReadable(ptrTriggerSource)) {
                    currentTriggerSource = QString::fromStdString(ptrTriggerSource->GetCurrentEntry()->GetSymbolic().c_str());
                    std::cout << "현재 트리거 소스: " << currentTriggerSource.toStdString() << std::endl;
                    
                    // UI에 현재 트리거 소스 설정
                    int sourceIndex = triggerSourceCombo->findData(currentTriggerSource);
                    if (sourceIndex >= 0) {
                        triggerSourceCombo->setCurrentIndex(sourceIndex);
                        std::cout << "UI 트리거 소스 설정 완료: 인덱스 " << sourceIndex << std::endl;
                    } else {
                        std::cout << "UI 트리거 소스 설정 실패: " << currentTriggerSource.toStdString() << " 항목을 찾을 수 없음" << std::endl;
                        
                        // 콤보박스에 어떤 항목들이 있는지 확인
                        std::cout << "트리거 소스 콤보박스 항목들:" << std::endl;
                        for (int i = 0; i < triggerSourceCombo->count(); ++i) {
                            QString itemText = triggerSourceCombo->itemText(i);
                            QString itemData = triggerSourceCombo->itemData(i).toString();
                            std::cout << "  인덱스 " << i << ": " << itemText.toStdString() << " (데이터: " << itemData.toStdString() << ")" << std::endl;
                        }
                    }
                    
                    // UI에 표시된 트리거 소스 확인
                    QString uiTriggerSource = triggerSourceCombo->currentData().toString();
                    std::cout << "UI에 표시된 트리거 소스: " << uiTriggerSource.toStdString() << std::endl;
                }
                
                // 3. 트리거 선택자 읽기 및 UI 반영
                Spinnaker::GenApi::CEnumerationPtr ptrTriggerSelector = nodeMap.GetNode("TriggerSelector");
                if (IsAvailable(ptrTriggerSelector) && IsReadable(ptrTriggerSelector)) {
                    QString currentSelector = QString::fromStdString(ptrTriggerSelector->GetCurrentEntry()->GetSymbolic().c_str());
                    std::cout << "현재 트리거 선택자: " << currentSelector.toStdString() << std::endl;
                    
                    int selectorIndex = triggerSelectorCombo->findData(currentSelector);
                    if (selectorIndex >= 0) {
                        triggerSelectorCombo->setCurrentIndex(selectorIndex);
                        std::cout << "UI 트리거 선택자 설정 완료: 인덱스 " << selectorIndex << std::endl;
                    } else {
                        std::cout << "UI 트리거 선택자 설정 실패: " << currentSelector.toStdString() << " 항목을 찾을 수 없음" << std::endl;
                    }
                    
                    // UI에 표시된 트리거 선택자 확인
                    QString uiTriggerSelector = triggerSelectorCombo->currentData().toString();
                    std::cout << "UI에 표시된 트리거 선택자: " << uiTriggerSelector.toStdString() << std::endl;
                }
                
                // 4. 트리거 활성화 읽기 및 UI 반영
                Spinnaker::GenApi::CEnumerationPtr ptrTriggerActivation = nodeMap.GetNode("TriggerActivation");
                if (IsAvailable(ptrTriggerActivation) && IsReadable(ptrTriggerActivation)) {
                    QString currentActivation = QString::fromStdString(ptrTriggerActivation->GetCurrentEntry()->GetSymbolic().c_str());
                    std::cout << "현재 트리거 활성화: " << currentActivation.toStdString() << std::endl;
                    
                    int activationIndex = triggerActivationCombo->findData(currentActivation);
                    if (activationIndex >= 0) {
                        triggerActivationCombo->setCurrentIndex(activationIndex);
                        std::cout << "UI 트리거 활성화 설정 완료: 인덱스 " << activationIndex << std::endl;
                    } else {
                        std::cout << "UI 트리거 활성화 설정 실패: " << currentActivation.toStdString() << " 항목을 찾을 수 없음" << std::endl;
                    }
                    
                    // UI에 표시된 트리거 활성화 확인
                    QString uiTriggerActivation = triggerActivationCombo->currentData().toString();
                    std::cout << "UI에 표시된 트리거 활성화: " << uiTriggerActivation.toStdString() << std::endl;
                }
                
                // 5. 트리거 딜레이 읽기 및 UI 반영
                Spinnaker::GenApi::CFloatPtr ptrTriggerDelay = nodeMap.GetNode("TriggerDelay");
                if (IsAvailable(ptrTriggerDelay) && IsReadable(ptrTriggerDelay)) {
                    double currentDelay = ptrTriggerDelay->GetValue();
                    std::cout << "현재 트리거 딜레이: " << currentDelay << " μs" << std::endl;
                    triggerDelaySpinBox->setValue(static_cast<int>(currentDelay));
                    std::cout << "UI 트리거 딜레이 설정 완료: " << static_cast<int>(currentDelay) << std::endl;
                    
                    // UI에 표시된 트리거 딜레이 확인
                    int uiTriggerDelay = triggerDelaySpinBox->value();
                    std::cout << "UI에 표시된 트리거 딜레이: " << uiTriggerDelay << " μs" << std::endl;
                }
            }
            
            std::cout << "========== UI 콤보박스 최종 상태 ===========" << std::endl;
            std::cout << "UI 트리거 모드: " << triggerModeCombo->currentData().toString().toStdString() << std::endl;
            std::cout << "UI 트리거 소스: " << triggerSourceCombo->currentData().toString().toStdString() << std::endl;
            std::cout << "UI 트리거 선택자: " << triggerSelectorCombo->currentData().toString().toStdString() << std::endl;
            std::cout << "UI 트리거 활성화: " << triggerActivationCombo->currentData().toString().toStdString() << std::endl;
            std::cout << "UI 트리거 딜레이: " << triggerDelaySpinBox->value() << " μs" << std::endl;
            std::cout << "===========================================" << std::endl;
            
            // 트리거 모드 요약 출력
            QString triggerSummary;
            if (currentTriggerMode == "Off") {
                triggerSummary = "연속 촬영 모드 (트리거 OFF)";
            } else if (currentTriggerMode == "On") {
                if (currentTriggerSource == "Software") {
                    triggerSummary = "소프트웨어 트리거 모드";
                } else {
                    triggerSummary = QString("하드웨어 트리거 모드 (%1)").arg(currentTriggerSource);
                }
            }
            std::cout << "트리거 설정 요약: " << triggerSummary.toStdString() << std::endl;
            
            statusLabel->setText(QString("상태: %1 - 설정 정보 읽기 완료").arg(triggerSummary));
            std::cout << "========================================\n" << std::endl;
            
            // 추가 설정 정보 읽기
            readCameraSettings(camera);
            
        } catch (const Spinnaker::Exception& e) {
            std::cout << "카메라 설정 읽기 예외: " << e.what() << std::endl;
            statusLabel->setText(QString("상태: 카메라 설정 읽기 실패 - %1").arg(e.what()));
        }
#endif
    }
}

#ifdef USE_SPINNAKER
void CameraSettingsDialog::setSpinnakerCameras(const std::vector<Spinnaker::CameraPtr>& cameras) {
    m_spinCameras = cameras;
    
    // 시그널을 일시적으로 차단하여 중복 호출 방지
    cameraCombo->blockSignals(true);
    
    // 카메라 콤보박스 업데이트
    cameraCombo->clear();
    
    if (cameras.empty()) {
        cameraCombo->addItem("카메라를 찾을 수 없습니다");
        currentCameraIndex = -1;
        statusLabel->setText("상태: 카메라를 찾을 수 없습니다");
    } else {
        for (size_t i = 0; i < cameras.size(); ++i) {
            try {
                auto& camera = cameras[i];
                
                // 카메라가 초기화되지 않았으면 초기화
                if (!camera->IsInitialized()) {
                    camera->Init();
                    // 초기화 후 카메라가 안정화될 시간 제공
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                
                Spinnaker::GenApi::INodeMap& nodeMap = camera->GetTLDeviceNodeMap();
                
                // 카메라 모델명 가져오기
                Spinnaker::GenApi::CStringPtr ptrModelName = nodeMap.GetNode("DeviceModelName");
                QString modelName = "Unknown Model";
                if (IsAvailable(ptrModelName) && IsReadable(ptrModelName)) {
                    modelName = QString::fromStdString(ptrModelName->GetValue().c_str());
                }
                
                // 시리얼 번호 가져오기
                Spinnaker::GenApi::CStringPtr ptrSerial = nodeMap.GetNode("DeviceSerialNumber");
                QString serial = "Unknown Serial";
                if (IsAvailable(ptrSerial) && IsReadable(ptrSerial)) {
                    serial = QString::fromStdString(ptrSerial->GetValue().c_str());
                }
                
                QString displayName = QString("카메라 %1: %2 (S/N: %3)")
                                     .arg(i + 1)
                                     .arg(modelName)
                                     .arg(serial);
                
                cameraCombo->addItem(displayName, static_cast<int>(i));
                
            } catch (const Spinnaker::Exception& e) {
                QString displayName = QString("카메라 %1: 정보 읽기 실패").arg(i + 1);
                cameraCombo->addItem(displayName, static_cast<int>(i));
            }
        }
        
        currentCameraIndex = 0;
        cameraCombo->setCurrentIndex(0);
        statusLabel->setText(QString("상태: %1개의 카메라 발견됨").arg(cameras.size()));
    }
    
    // 시그널 차단 해제
    cameraCombo->blockSignals(false);
    
    // 첫 번째 카메라의 현재 설정을 명시적으로 로드 (한 번만)
    if (!cameras.empty()) {
        loadCurrentCameraSettings();
    }
}

bool CameraSettingsDialog::checkHardwareTrigger(Spinnaker::CameraPtr camera) {
    try {
        Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
        
        Spinnaker::GenApi::CIntegerPtr ptrLineStatusAll = nodeMap.GetNode("LineStatusAll");
        if (IsAvailable(ptrLineStatusAll) && IsReadable(ptrLineStatusAll)) {
            int64_t lineStatus = ptrLineStatusAll->GetValue();
            
            bool line0High = (lineStatus & 0x01) != 0;
            return line0High;
        }
    } catch (const Spinnaker::Exception& e) {
        // 에러 무시
    }
    
    return false;
}

void CameraSettingsDialog::readCameraSettings(Spinnaker::CameraPtr camera) {
    try {
        Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
        
        // 트리거 모드 읽기
        Spinnaker::GenApi::CEnumerationPtr ptrTriggerMode = nodeMap.GetNode("TriggerMode");
        if (IsAvailable(ptrTriggerMode) && IsReadable(ptrTriggerMode)) {
            QString currentTriggerMode = QString::fromStdString(ptrTriggerMode->GetCurrentEntry()->GetSymbolic().c_str());
            
            for (int i = 0; i < triggerModeCombo->count(); ++i) {
                if (triggerModeCombo->itemData(i).toString() == currentTriggerMode) {
                    triggerModeCombo->setCurrentIndex(i);
                    break;
                }
            }
        }
        
        // 트리거 소스 읽기 (트리거가 On일 때만)
        if (triggerModeCombo->currentData().toString() != "Off") {
            Spinnaker::GenApi::CEnumerationPtr ptrTriggerSource = nodeMap.GetNode("TriggerSource");
            if (IsAvailable(ptrTriggerSource) && IsReadable(ptrTriggerSource)) {
                QString currentTriggerSource = QString::fromStdString(ptrTriggerSource->GetCurrentEntry()->GetSymbolic().c_str());
                
                for (int i = 0; i < triggerSourceCombo->count(); ++i) {
                    if (triggerSourceCombo->itemData(i).toString() == currentTriggerSource) {
                        triggerSourceCombo->setCurrentIndex(i);
                        break;
                    }
                }
            }
            
            // 트리거 선택자 읽기
            Spinnaker::GenApi::CEnumerationPtr ptrTriggerSelector = nodeMap.GetNode("TriggerSelector");
            if (IsAvailable(ptrTriggerSelector) && IsReadable(ptrTriggerSelector)) {
                QString currentTriggerSelector = QString::fromStdString(ptrTriggerSelector->GetCurrentEntry()->GetSymbolic().c_str());
                
                for (int i = 0; i < triggerSelectorCombo->count(); ++i) {
                    if (triggerSelectorCombo->itemData(i).toString() == currentTriggerSelector) {
                        triggerSelectorCombo->setCurrentIndex(i);
                        break;
                    }
                }
            }
            
            // 트리거 활성화 읽기
            Spinnaker::GenApi::CEnumerationPtr ptrTriggerActivation = nodeMap.GetNode("TriggerActivation");
            if (IsAvailable(ptrTriggerActivation) && IsReadable(ptrTriggerActivation)) {
                QString currentTriggerActivation = QString::fromStdString(ptrTriggerActivation->GetCurrentEntry()->GetSymbolic().c_str());
                
                for (int i = 0; i < triggerActivationCombo->count(); ++i) {
                    if (triggerActivationCombo->itemData(i).toString() == currentTriggerActivation) {
                        triggerActivationCombo->setCurrentIndex(i);
                        break;
                    }
                }
            }
            
            // 트리거 딜레이 읽기
            Spinnaker::GenApi::CFloatPtr ptrTriggerDelay = nodeMap.GetNode("TriggerDelay");
            if (IsAvailable(ptrTriggerDelay) && IsReadable(ptrTriggerDelay)) {
                double delayValue = ptrTriggerDelay->GetValue();
                triggerDelaySpinBox->setValue(static_cast<int>(delayValue));
            }
        }
        
        // 노출 자동 모드 읽기
        Spinnaker::GenApi::CEnumerationPtr ptrExposureAuto = nodeMap.GetNode("ExposureAuto");
        if (IsAvailable(ptrExposureAuto) && IsReadable(ptrExposureAuto)) {
            QString currentExposureAuto = QString::fromStdString(ptrExposureAuto->GetCurrentEntry()->GetSymbolic().c_str());
            
            for (int i = 0; i < exposureAutoCombo->count(); ++i) {
                if (exposureAutoCombo->itemData(i).toString() == currentExposureAuto) {
                    exposureAutoCombo->setCurrentIndex(i);
                    break;
                }
            }
        }
        
        // 노출 시간 읽기 (자동 모드가 Off일 때)
        if (exposureAutoCombo->currentData().toString() == "Off") {
            Spinnaker::GenApi::CFloatPtr ptrExposureTime = nodeMap.GetNode("ExposureTime");
            if (IsAvailable(ptrExposureTime) && IsReadable(ptrExposureTime)) {
                double exposureValue = ptrExposureTime->GetValue();
                exposureSpinBox->setValue(static_cast<int>(exposureValue));
            }
        }
        
        // 게인 자동 모드 읽기
        Spinnaker::GenApi::CEnumerationPtr ptrGainAuto = nodeMap.GetNode("GainAuto");
        if (IsAvailable(ptrGainAuto) && IsReadable(ptrGainAuto)) {
            QString currentGainAuto = QString::fromStdString(ptrGainAuto->GetCurrentEntry()->GetSymbolic().c_str());
            
            for (int i = 0; i < gainAutoCombo->count(); ++i) {
                if (gainAutoCombo->itemData(i).toString() == currentGainAuto) {
                    gainAutoCombo->setCurrentIndex(i);
                    break;
                }
            }
        }
        
        // 게인 값 읽기 (자동 모드가 Off일 때)
        if (gainAutoCombo->currentData().toString() == "Off") {
            Spinnaker::GenApi::CFloatPtr ptrGain = nodeMap.GetNode("Gain");
            if (IsAvailable(ptrGain) && IsReadable(ptrGain)) {
                double gainValue = ptrGain->GetValue();
                gainSpinBox->setValue(static_cast<int>(gainValue));
            }
        }
        
        statusLabel->setText("상태: 카메라 설정을 성공적으로 로드했습니다");
        
        // 트리거 모드 상태에 따라 UI 업데이트
        onTriggerModeChanged();
        
    } catch (const Spinnaker::Exception& e) {
        QString errorMsg = QString("카메라 설정 읽기 실패: %1").arg(e.what());
        statusLabel->setText("상태: " + errorMsg);
    }
}
#endif

void CameraSettingsDialog::onTriggerModeChanged() {
    // 트리거 모드에 따라 트리거 테스트 버튼들 활성화/비활성화
    QString triggerMode = triggerModeCombo->currentData().toString();
    bool isTriggerEnabled = (triggerMode != "Off");
    
    // 이미 감지 중이고 트리거가 OFF로 변경되면 감지 중지
    if (!isTriggerEnabled && isListening) {
        stopHardwareTriggerDetection();
    }
    
    // 즉시 카메라에 트리거 모드 적용
    applyTriggerModeOnly();
    
    // 트리거 테스트 관련 UI 활성화/비활성화 (카메라 설정 적용 후)
    startListeningBtn->setEnabled(isTriggerEnabled);
    
    // 상태 업데이트 - 트리거 소스에 따라 구분
    if (isTriggerEnabled) {
        QString triggerSource = triggerSourceCombo->currentData().toString();
        if (triggerSource == "Software") {
            triggerStatusLabel->setText("트리거 상태: 소프트웨어 트리거 - 테스트 가능");
            startListeningBtn->setText("소프트웨어 트리거 시작");
        } else {
            triggerStatusLabel->setText(QString("트리거 상태: 하드웨어 트리거(%1) - 테스트 가능").arg(triggerSource));
            startListeningBtn->setText("하드웨어 트리거 감지시작");
        }
        triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #0066cc; padding: 5px; }");
    } else {
        triggerStatusLabel->setText("트리거 상태: OFF - 테스트 불가");
        triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #999; padding: 5px; }");
        startListeningBtn->setText("트리거 테스트 시작");
    }
}

void CameraSettingsDialog::saveSettings() {
    QSettings settings("MV", "CameraSettings");
    
    // 트리거 설정 저장
    if (triggerModeCombo) {
        settings.setValue("triggerMode", triggerModeCombo->currentData().toString());
    }
    if (triggerSourceCombo) {
        settings.setValue("triggerSource", triggerSourceCombo->currentData().toString());
    }
    if (triggerSelectorCombo) {
        settings.setValue("triggerSelector", triggerSelectorCombo->currentData().toString());
    }
    if (triggerActivationCombo) {
        settings.setValue("triggerActivation", triggerActivationCombo->currentData().toString());
    }
    if (triggerDelaySpinBox) {
        settings.setValue("triggerDelay", triggerDelaySpinBox->value());
    }
    
    // 노출 설정 저장
    if (exposureAutoCombo) {
        settings.setValue("exposureAuto", exposureAutoCombo->currentData().toString());
    }
    if (exposureSpinBox) {
        settings.setValue("exposureTime", exposureSpinBox->value());
    }
    
    // 게인 설정 저장
    if (gainAutoCombo) {
        settings.setValue("gainAuto", gainAutoCombo->currentData().toString());
    }
    if (gainSpinBox) {
        settings.setValue("gain", gainSpinBox->value());
    }
}

void CameraSettingsDialog::loadSettings() {
    QSettings settings("MV", "CameraSettings");
    
    // 트리거 설정 로드
    if (triggerModeCombo) {
        QString savedTriggerMode = settings.value("triggerMode", "Off").toString();
        int index = triggerModeCombo->findData(savedTriggerMode);
        if (index != -1) {
            triggerModeCombo->setCurrentIndex(index);
        }
    }
    
    if (triggerSourceCombo) {
        QString savedTriggerSource = settings.value("triggerSource", "Software").toString();
        int index = triggerSourceCombo->findData(savedTriggerSource);
        if (index != -1) {
            triggerSourceCombo->setCurrentIndex(index);
        }
    }
    
    if (triggerSelectorCombo) {
        QString savedTriggerSelector = settings.value("triggerSelector", "FrameStart").toString();
        int index = triggerSelectorCombo->findData(savedTriggerSelector);
        if (index != -1) {
            triggerSelectorCombo->setCurrentIndex(index);
        }
    }
    
    if (triggerActivationCombo) {
        QString savedTriggerActivation = settings.value("triggerActivation", "RisingEdge").toString();
        int index = triggerActivationCombo->findData(savedTriggerActivation);
        if (index != -1) {
            triggerActivationCombo->setCurrentIndex(index);
        }
    }
    
    if (triggerDelaySpinBox) {
        int savedTriggerDelay = settings.value("triggerDelay", 0).toInt();
        triggerDelaySpinBox->setValue(savedTriggerDelay);
    }
    
    // 노출 설정 로드
    if (exposureAutoCombo) {
        QString savedExposureAuto = settings.value("exposureAuto", "Off").toString();
        int index = exposureAutoCombo->findData(savedExposureAuto);
        if (index != -1) {
            exposureAutoCombo->setCurrentIndex(index);
        }
    }
    
    if (exposureSpinBox) {
        int savedExposureTime = settings.value("exposureTime", 10000).toInt();
        exposureSpinBox->setValue(savedExposureTime);
    }
    
    // 게인 설정 로드
    if (gainAutoCombo) {
        QString savedGainAuto = settings.value("gainAuto", "Off").toString();
        int index = gainAutoCombo->findData(savedGainAuto);
        if (index != -1) {
            gainAutoCombo->setCurrentIndex(index);
        }
    }
    
    if (gainSpinBox) {
        int savedGain = settings.value("gain", 0).toInt();
        gainSpinBox->setValue(savedGain);
    }
}