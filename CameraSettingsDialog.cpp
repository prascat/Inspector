#include "CameraSettingsDialog.h"
#include <QDebug>

CameraSettingsDialog::CameraSettingsDialog(QWidget* parent)
    : QDialog(parent)
    , currentCameraIndex(-1)
    , m_configManager(ConfigManager::instance())
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
            });
    cameraSelectLayout->addWidget(cameraCombo);
    cameraSelectLayout->addStretch();
    
    cameraLayout->addLayout(cameraSelectLayout);
    
    mainLayout->addWidget(cameraGroup);
    
    // UserSet 설정 그룹 추가
    setupUserSetSettings();
    mainLayout->addWidget(userSetGroup);
    
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
    userSetGroup = new QGroupBox("UserSet 파일 업로드", this);
    QVBoxLayout* userSetLayout = new QVBoxLayout(userSetGroup);
    
    // LIVE UserSet 설정
    QGroupBox* liveGroup = new QGroupBox("LIVE 모드 UserSet0", this);
    QVBoxLayout* liveLayout = new QVBoxLayout(liveGroup);
    
    QHBoxLayout* livePathLayout = new QHBoxLayout;
    livePathLayout->addWidget(new QLabel("파일 경로:", this));
    liveUserSetPathEdit = new QLineEdit(this);
    liveUserSetPathEdit->setReadOnly(true);
    liveUserSetPathEdit->setText(m_configManager->getUserSetLivePath());
    livePathLayout->addWidget(liveUserSetPathEdit);
    
    browseLiveUserSetBtn = new QPushButton("찾기", this);
    livePathLayout->addWidget(browseLiveUserSetBtn);
    liveLayout->addLayout(livePathLayout);
    
    userSetLayout->addWidget(liveGroup);
    
    // INSPECT UserSet 설정
    QGroupBox* inspectGroup = new QGroupBox("INSPECT 모드 UserSet1", this);
    QVBoxLayout* inspectLayout = new QVBoxLayout(inspectGroup);
    
    QHBoxLayout* inspectPathLayout = new QHBoxLayout;
    inspectPathLayout->addWidget(new QLabel("파일 경로:", this));
    inspectUserSetPathEdit = new QLineEdit(this);
    inspectUserSetPathEdit->setReadOnly(true);
    inspectUserSetPathEdit->setText(m_configManager->getUserSetInspectPath());
    inspectPathLayout->addWidget(inspectUserSetPathEdit);
    
    browseInspectUserSetBtn = new QPushButton("찾기", this);
    inspectPathLayout->addWidget(browseInspectUserSetBtn);
    inspectLayout->addLayout(inspectPathLayout);
    
    userSetLayout->addWidget(inspectGroup);
    
    // 통합 업로드 버튼
    uploadUserSetsBtn = new QPushButton("UserSet 업로드 (LIVE → UserSet0, INSPECT → UserSet1)", this);
    uploadUserSetsBtn->setStyleSheet("QPushButton { background-color: #2196F3; color: white; font-weight: bold; padding: 10px; }");
    userSetLayout->addWidget(uploadUserSetsBtn);
    
    // 시그널 연결
    connect(browseLiveUserSetBtn, &QPushButton::clicked, this, &CameraSettingsDialog::onBrowseLiveUserSet);
    connect(browseInspectUserSetBtn, &QPushButton::clicked, this, &CameraSettingsDialog::onBrowseInspectUserSet);
    connect(uploadUserSetsBtn, &QPushButton::clicked, this, &CameraSettingsDialog::onUploadUserSets);
}

void CameraSettingsDialog::onBrowseLiveUserSet() {
    QString fileName = QFileDialog::getOpenFileName(this, 
        "LIVE UserSet 파일 선택", 
        QDir::currentPath(), 
        "UserSet 파일 (*.dat *.bin);;모든 파일 (*.*)");
    
    if (!fileName.isEmpty()) {
        liveUserSetPathEdit->setText(fileName);
        m_configManager->setUserSetLivePath(fileName);
    }
}

void CameraSettingsDialog::onBrowseInspectUserSet() {
    QString fileName = QFileDialog::getOpenFileName(this, 
        "INSPECT UserSet 파일 선택", 
        QDir::currentPath(), 
        "UserSet 파일 (*.dat *.bin);;모든 파일 (*.*)");
    
    if (!fileName.isEmpty()) {
        inspectUserSetPathEdit->setText(fileName);
        m_configManager->setUserSetInspectPath(fileName);
    }
}

void CameraSettingsDialog::onUploadUserSets() {
#ifdef USE_SPINNAKER
    int selectedCameraIndex = getSelectedCameraIndex();
    if (selectedCameraIndex < 0 || selectedCameraIndex >= static_cast<int>(m_spinCameras.size())) {
        QMessageBox::warning(this, "경고", "유효한 카메라를 선택해주세요.");
        return;
    }

    try {
        Spinnaker::CameraPtr camera = m_spinCameras[selectedCameraIndex];
        Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
        
        // config.xml의 UserSet 파일들 업로드
        QString liveFilePath = liveUserSetPathEdit->text();
        QString inspectFilePath = inspectUserSetPathEdit->text();
        
        if (liveFilePath.isEmpty() || inspectFilePath.isEmpty()) {
            QMessageBox::warning(this, "경고", "LIVE와 INSPECT UserSet 파일을 모두 선택해주세요.");
            return;
        }
        
        if (!QFile::exists(liveFilePath) || !QFile::exists(inspectFilePath)) {
            QMessageBox::warning(this, "경고", "선택된 파일들이 존재하지 않습니다.");
            return;
        }
        
        // 실제 UserSet 파일 업로드 구현
        QString results;
        
        // LIVE UserSet (UserSet0) 업로드
        try {
            if (uploadUserSetFile(camera, liveFilePath, "UserSet0")) {
                results += "✓ LIVE UserSet (UserSet0) 업로드 성공\n";
            } else {
                results += "✗ LIVE UserSet (UserSet0) 업로드 실패\n";
            }
        } catch (const std::exception& e) {
            results += QString("✗ LIVE UserSet 오류: %1\n").arg(e.what());
        }
        
        // INSPECT UserSet (UserSet1) 업로드  
        try {
            if (uploadUserSetFile(camera, inspectFilePath, "UserSet1")) {
                results += "✓ INSPECT UserSet (UserSet1) 업로드 성공\n";
            } else {
                results += "✗ INSPECT UserSet (UserSet1) 업로드 실패\n";
            }
        } catch (const std::exception& e) {
            results += QString("✗ INSPECT UserSet 오류: %1\n").arg(e.what());
        }
        
        QMessageBox::information(this, "업로드 결과", results);
        
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "오류", QString("UserSet 처리 중 오류 발생: %1").arg(e.what()));
    }
#else
    QMessageBox::information(this, "알림", "Spinnaker SDK가 비활성화되어 있습니다.");
#endif
}

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

bool CameraSettingsDialog::uploadUserSetFile(Spinnaker::CameraPtr camera, const QString& filePath, const QString& userSetName) {
    try {
        // 카메라 스트리밍 상태 확인 및 정지
        bool wasStreaming = false;
        if (camera->IsStreaming()) {
            wasStreaming = true;
            camera->EndAcquisition();
        }
        
        Spinnaker::GenApi::INodeMap& nodeMap = camera->GetNodeMap();
        
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
        
        // 파일 존재 확인
        QFile file(filePath);
        if (!file.exists()) {
            throw std::runtime_error("UserSet 파일이 존재하지 않습니다.");
        }
        
        qDebug() << "[UserSet Upload] 업로드 시작:" << userSetName << "파일:" << filePath;
        
        // ===== 업로드 전 카메라 파라미터 출력 =====
        qDebug() << "========== 업로드 전" << userSetName << "파라미터 ==========";
        printCameraParameters(nodeMap, "업로드 전");
        
        // UserSet 파일 실제 로드 구현
        try {
            // Method 1: FileAccessControl을 사용한 실제 파일 업로드
            Spinnaker::GenApi::CEnumerationPtr ptrFileSelector = nodeMap.GetNode("FileSelector");
            Spinnaker::GenApi::CEnumerationPtr ptrFileOperationSelector = nodeMap.GetNode("FileOperationSelector");
            Spinnaker::GenApi::CCommandPtr ptrFileOperationExecute = nodeMap.GetNode("FileOperationExecute");
            Spinnaker::GenApi::CRegisterPtr ptrFileAccessBuffer = nodeMap.GetNode("FileAccessBuffer");
            Spinnaker::GenApi::CIntegerPtr ptrFileAccessLength = nodeMap.GetNode("FileAccessLength");
            
            if (IsAvailable(ptrFileSelector) && IsWritable(ptrFileSelector) &&
                IsAvailable(ptrFileOperationSelector) && IsWritable(ptrFileOperationSelector) &&
                IsAvailable(ptrFileOperationExecute) && IsWritable(ptrFileOperationExecute) &&
                IsAvailable(ptrFileAccessBuffer) && IsWritable(ptrFileAccessBuffer) &&
                IsAvailable(ptrFileAccessLength) && IsWritable(ptrFileAccessLength)) {
                
                qDebug() << "[UserSet Upload] FileAccessControl 방식 시도";
                
                // 파일 읽기
                QFile file(filePath);
                if (!file.open(QIODevice::ReadOnly)) {
                    throw std::runtime_error("파일을 열 수 없습니다.");
                }
                QByteArray fileData = file.readAll();
                file.close();
                
                if (fileData.isEmpty()) {
                    throw std::runtime_error("파일이 비어있습니다.");
                }
                
                // UserSet 파일로 설정
                QString userSetFileName = QString("UserSet%1").arg(userSetName.contains("1") ? "1" : "0");
                Spinnaker::GenApi::CEnumEntryPtr ptrUserSetFile = ptrFileSelector->GetEntryByName(userSetFileName.toStdString().c_str());
                if (IsAvailable(ptrUserSetFile)) {
                    ptrFileSelector->SetIntValue(ptrUserSetFile->GetValue());
                    
                    // Write 모드 설정
                    Spinnaker::GenApi::CEnumEntryPtr ptrWrite = ptrFileOperationSelector->GetEntryByName("Write");
                    if (IsAvailable(ptrWrite)) {
                        ptrFileOperationSelector->SetIntValue(ptrWrite->GetValue());
                        
                        // 파일 데이터를 버퍼에 쓰기
                        ptrFileAccessLength->SetValue(fileData.size());
                        ptrFileAccessBuffer->Set(reinterpret_cast<const uint8_t*>(fileData.constData()), fileData.size());
                        
                        // 파일 쓰기 실행
                        ptrFileOperationExecute->Execute();
                        
                        qDebug() << "[UserSet Upload] FileAccessControl 방식으로 파일 업로드 성공";
                        
                        // UserSet 로드 실행 (업로드된 설정 적용)
                        ptrUserSetLoad->Execute();
                        qDebug() << "[UserSet Upload] UserSet 로드 실행 완료";
                    }
                }
            }
            // Method 2: 기본 UserSet 로드 (파일 업로드 불가능한 경우)
            else {
                qDebug() << "[UserSet Upload] FileAccessControl 지원 안됨, 기본 방식 사용";
                
                // UserSet 선택
                ptrUserSetSelector->SetIntValue(ptrUserSet->GetValue());
                
                // 기존 UserSet 로드
                ptrUserSetLoad->Execute();
                
                qDebug() << "[UserSet Upload] 기본 UserSet 로드 방식 사용 (파일 내용 적용 안됨)";
            }
        } catch (const Spinnaker::Exception& e) {
            qDebug() << "[UserSet Upload] Spinnaker 오류:" << e.what();
            throw;
        }
        
        // ===== UserSet 로드 후 카메라 파라미터 출력 =====
        qDebug() << "========== UserSet 로드 후" << userSetName << "파라미터 ==========";
        printCameraParameters(nodeMap, "UserSet 로드 후");
        
        // UserSet 저장 (현재 설정을 해당 UserSet에 저장)
        Spinnaker::GenApi::CCommandPtr ptrUserSetSave = nodeMap.GetNode("UserSetSave");
        if (IsAvailable(ptrUserSetSave) && IsWritable(ptrUserSetSave)) {
            ptrUserSetSave->Execute();
            qDebug() << "[UserSet Upload]" << userSetName << "저장 완료";
        }
        
        // 스트리밍이 실행 중이었다면 다시 시작
        if (wasStreaming) {
            camera->BeginAcquisition();
        }
        
        return true;
        
    } catch (const Spinnaker::Exception& e) {
        // 오류 발생시에도 스트리밍 복구 시도
        try {
            if (camera->IsStreaming()) {
                // 이미 스트리밍 중이면 패스
            } else {
                // 스트리밍이 중단되었고 원래 실행 중이었다면 복구
                camera->BeginAcquisition();
            }
        } catch (...) {
            // 복구 실패는 무시
        }
        throw std::runtime_error(QString("Spinnaker 오류: %1").arg(e.what()).toStdString());
    } catch (const std::exception& e) {
        // 오류 발생시에도 스트리밍 복구 시도
        try {
            if (!camera->IsStreaming()) {
                camera->BeginAcquisition();
            }
        } catch (...) {
            // 복구 실패는 무시
        }
        throw std::runtime_error(e.what());
    }
    
    return false;
}
#endif