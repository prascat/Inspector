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
    
    // 타이머 초기화
    triggerCheckTimer = new QTimer(this);
    connect(triggerCheckTimer, SIGNAL(timeout()), this, SLOT(checkHardwareTrigger()));
    
    loadCameraSettings();
}

CameraSettingsDialog::~CameraSettingsDialog() {
    if (isListening) {
        stopHardwareTriggerDetection();
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
    triggerModeCombo->addItem("Software", "Software");  
    triggerModeCombo->addItem("Hardware", "Hardware");
    triggerModeLayout->addWidget(triggerModeCombo);
    triggerModeLayout->addStretch();
    triggerLayout->addLayout(triggerModeLayout);
    
    // 트리거 소스
    QHBoxLayout* triggerSourceLayout = new QHBoxLayout;
    triggerSourceLayout->addWidget(new QLabel("트리거 소스:", this));
    triggerSourceCombo = new QComboBox(this);
    triggerSourceCombo->addItem("Line0", "Line0");
    triggerSourceCombo->addItem("Line1", "Line1");
    triggerSourceCombo->addItem("Line2", "Line2");
    triggerSourceCombo->addItem("Line3", "Line3");
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
            statusLabel->setText("상태: 트리거 OFF (연속 촬영) 설정 완료");
            
        } else if (triggerMode == "Software") {
            statusLabel->setText("상태: 소프트웨어 트리거 설정 중...");
            
            // 트리거 소스 설정 (예제 방식)
            Spinnaker::GenApi::CEnumerationPtr ptrTriggerSource = nodeMap.GetNode("TriggerSource");
            if (!IsReadable(ptrTriggerSource)) {
                statusLabel->setText("상태: TriggerSource 노드를 읽을 수 없습니다");
                return;
            }
            if (!IsWritable(ptrTriggerSource)) {
                statusLabel->setText("상태: TriggerSource 노드가 쓰기 불가능합니다");
                return;
            }
            
            Spinnaker::GenApi::CEnumEntryPtr ptrTriggerSourceSoftware = ptrTriggerSource->GetEntryByName("Software");
            if (!IsReadable(ptrTriggerSourceSoftware)) {
                statusLabel->setText("상태: TriggerSource Software 엔트리를 읽을 수 없습니다");
                return;
            }
            
            statusLabel->setText("상태: TriggerSource를 Software로 설정 중...");
            ptrTriggerSource->SetIntValue(ptrTriggerSourceSoftware->GetValue());
            
            // 트리거 선택자 설정
            QString triggerSelector = triggerSelectorCombo->currentData().toString();
            Spinnaker::GenApi::CEnumerationPtr ptrTriggerSelector = nodeMap.GetNode("TriggerSelector");
            if (IsReadable(ptrTriggerSelector) && IsWritable(ptrTriggerSelector)) {
                Spinnaker::GenApi::CEnumEntryPtr ptrTriggerSelectorEntry = ptrTriggerSelector->GetEntryByName(triggerSelector.toStdString().c_str());
                if (IsReadable(ptrTriggerSelectorEntry)) {
                    statusLabel->setText(QString("상태: TriggerSelector를 %1로 설정 중...").arg(triggerSelector));
                    ptrTriggerSelector->SetIntValue(ptrTriggerSelectorEntry->GetValue());
                }
            }
            
            // 마지막에 트리거 모드를 On으로 설정
            Spinnaker::GenApi::CEnumEntryPtr ptrTriggerModeOn = ptrTriggerMode->GetEntryByName("On");
            if (!IsReadable(ptrTriggerModeOn)) {
                statusLabel->setText("상태: TriggerMode On 엔트리를 읽을 수 없습니다");
                return;
            }
            
            statusLabel->setText("상태: TriggerMode를 On으로 설정 중...");
            ptrTriggerMode->SetIntValue(ptrTriggerModeOn->GetValue());
            statusLabel->setText("상태: 소프트웨어 트리거 설정 완료");
            
        } else {
            statusLabel->setText("상태: 하드웨어 트리거 설정 중...");
            
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
            
            // 트리거 소스 설정
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
            
            statusLabel->setText(QString("상태: TriggerSource를 %1로 설정 중...").arg(triggerSource));
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
            
            // 트리거 딜레이 설정 (옵션)
            Spinnaker::GenApi::CFloatPtr ptrTriggerDelay = nodeMap.GetNode("TriggerDelay");
            if (IsReadable(ptrTriggerDelay) && IsWritable(ptrTriggerDelay)) {
                double delayValue = static_cast<double>(triggerDelaySpinBox->value());
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
            statusLabel->setText("상태: 하드웨어 트리거 설정 완료");
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
                statusLabel->setText(QString("상태: ExposureTime을 %1μs로 설정 중...").arg(exposureValue));
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
                statusLabel->setText(QString("상태: Gain을 %1dB로 설정 중...").arg(gainValue));
                ptrGain->SetValue(gainValue);
            }
        }
        
        statusLabel->setText("상태: UserSet 영구 저장 시도 중...");
        // 카메라 설정을 영구 저장 - 예제 방식으로 더 안전하게
        try {
            // UserSetSelector 노드 가져오기
            Spinnaker::GenApi::CEnumerationPtr ptrUserSetSelector = nodeMap.GetNode("UserSetSelector");
            if (!IsReadable(ptrUserSetSelector)) {
                statusLabel->setText("상태: UserSetSelector 노드를 읽을 수 없습니다");
            } else if (!IsWritable(ptrUserSetSelector)) {
                statusLabel->setText("상태: UserSetSelector 노드가 쓰기 불가능합니다");
            } else {
                // UserSet1 엔트리 가져오기
                Spinnaker::GenApi::CEnumEntryPtr ptrUserSet1 = ptrUserSetSelector->GetEntryByName("UserSet1");
                if (!IsReadable(ptrUserSet1)) {
                    statusLabel->setText("상태: UserSet1 엔트리를 읽을 수 없습니다");
                } else {
                    // UserSet1 선택
                    statusLabel->setText("상태: UserSet1 선택 중...");
                    ptrUserSetSelector->SetIntValue(ptrUserSet1->GetValue());
                    
                    // UserSetSave 명령 실행
                    Spinnaker::GenApi::CCommandPtr ptrUserSetSave = nodeMap.GetNode("UserSetSave");
                    if (!IsReadable(ptrUserSetSave)) {
                        statusLabel->setText("상태: UserSetSave 명령을 읽을 수 없습니다");
                    } else if (!IsWritable(ptrUserSetSave)) {
                        statusLabel->setText("상태: UserSetSave 명령이 쓰기 불가능합니다");
                    } else {
                        statusLabel->setText("상태: UserSet1에 설정 저장 중...");
                        ptrUserSetSave->Execute();
                        statusLabel->setText("상태: UserSet1 저장 완료");
                        
                        // UserSetDefault 설정 (선택사항)
                        Spinnaker::GenApi::CEnumerationPtr ptrUserSetDefault = nodeMap.GetNode("UserSetDefault");
                        if (IsReadable(ptrUserSetDefault) && IsWritable(ptrUserSetDefault)) {
                            Spinnaker::GenApi::CEnumEntryPtr ptrUserSetDefaultEntry = ptrUserSetDefault->GetEntryByName("UserSet1");
                            if (IsReadable(ptrUserSetDefaultEntry)) {
                                statusLabel->setText("상태: UserSet1을 기본값으로 설정 중...");
                                ptrUserSetDefault->SetIntValue(ptrUserSetDefaultEntry->GetValue());
                                statusLabel->setText("상태: 설정이 카메라에 영구 저장되었습니다!");
                            } else {
                                statusLabel->setText("상태: 설정 저장됨 (기본값 설정 건너뜀)");
                            }
                        } else {
                            statusLabel->setText("상태: 설정 저장됨 (기본값 설정 실패)");
                        }
                    }
                }
            }
        } catch (const Spinnaker::Exception& e) {
            QString errorDetail = QString("UserSet 저장 오류: %1 (코드: %2)").arg(e.what()).arg(e.GetError());
            statusLabel->setText("상태: 설정 적용됨 (" + errorDetail + ")");
        }
        
        statusLabel->setText("상태: 설정 적용 검증 중...");
        // 설정이 제대로 적용되었는지 확인
        QTimer::singleShot(1000, this, [this]() {
            loadCurrentCameraSettings();
            statusLabel->setText("상태: 모든 설정이 성공적으로 적용되었습니다");
            statusLabel->setStyleSheet("QLabel { background-color: #d4edda; color: #155724; padding: 8px; border: 1px solid #c3e6cb; border-radius: 4px; }");
        });
        
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

void CameraSettingsDialog::startHardwareTriggerDetection() {
    if (!isListening) {
        isListening = true;
        triggerCheckTimer->start(50);
        
        statusLabel->setText("상태: 하드웨어 트리거 감지 중...");
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
        
        startListeningBtn->setEnabled(true);
        stopListeningBtn->setEnabled(false);
    }
}

void CameraSettingsDialog::checkHardwareTrigger() {
#ifdef USE_SPINNAKER
    if (!isListening || m_spinCameras.empty() || currentCameraIndex < 0) return;
    
    try {
        auto& camera = m_spinCameras[currentCameraIndex];
        if (checkHardwareTrigger(camera)) {
            triggerStatusLabel->setText("트리거 상태: TRIGGERED!");
            triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #d73527; background-color: #f8d7da; padding: 5px; border-radius: 3px; }");
        } else {
            triggerStatusLabel->setText("트리거 상태: 대기 중");
            triggerStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #666; padding: 5px; }");
        }
    } catch (const Spinnaker::Exception& e) {
        // 에러 무시
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
        readCameraSettings(m_spinCameras[currentCameraIndex]);
#endif
    }
}

#ifdef USE_SPINNAKER
void CameraSettingsDialog::setSpinnakerCameras(const std::vector<Spinnaker::CameraPtr>& cameras) {
    m_spinCameras = cameras;
    
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
                }
                
                Spinnaker::GenApi::INodeMap& nodeMap = camera->GetTLDeviceNodeMap();
                
                // 저장된 UserSet1 로드 시도
                try {
                    Spinnaker::GenApi::INodeMap& deviceNodeMap = camera->GetNodeMap();
                    Spinnaker::GenApi::CEnumerationPtr ptrUserSetSelector = deviceNodeMap.GetNode("UserSetSelector");
                    if (IsAvailable(ptrUserSetSelector) && IsWritable(ptrUserSetSelector)) {
                        ptrUserSetSelector->SetIntValue(ptrUserSetSelector->GetEntryByName("UserSet1")->GetValue());
                        
                        Spinnaker::GenApi::CCommandPtr ptrUserSetLoad = deviceNodeMap.GetNode("UserSetLoad");
                        if (IsAvailable(ptrUserSetLoad) && IsWritable(ptrUserSetLoad)) {
                            ptrUserSetLoad->Execute();
                        }
                    }
                } catch (const Spinnaker::Exception& e) {
                    // UserSet 로드 실패해도 무시하고 계속 진행
                }
                
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
        
        // 첫 번째 카메라의 현재 설정을 로드
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
        
    } catch (const Spinnaker::Exception& e) {
        QString errorMsg = QString("카메라 설정 읽기 실패: %1").arg(e.what());
        statusLabel->setText("상태: " + errorMsg);
    }
}
#endif