#include "CameraSettingsDialog.h"
#include "CustomMessageBox.h"
#include "ConfigManager.h"
#include <QDebug>
#include <QMessageBox>
#include <QTabWidget>

CameraSettingsDialog::CameraSettingsDialog(QWidget *parent)
    : QDialog(parent)
    , currentCameraIndex(-1)
    , m_dragging(false)
{
    setWindowTitle("카메라 설정");
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
    setWindowModality(Qt::WindowModal);
    setMinimumSize(700, 800);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setupUI();
    applyBlackTheme();
}

CameraSettingsDialog::~CameraSettingsDialog()
{
}

#ifdef USE_SPINNAKER
void CameraSettingsDialog::setCameras(const std::vector<Spinnaker::CameraPtr>& cameras)
{
    spinCameras = cameras;
    
    cameraComboBox->clear();
    
    for (size_t i = 0; i < spinCameras.size(); ++i) {
        try {
            Spinnaker::GenApi::INodeMap& nodeMapTLDevice = spinCameras[i]->GetTLDeviceNodeMap();
            Spinnaker::GenApi::CStringPtr ptrDeviceModelName = nodeMapTLDevice.GetNode("DeviceModelName");
            Spinnaker::GenApi::CStringPtr ptrDeviceSerialNumber = nodeMapTLDevice.GetNode("DeviceSerialNumber");
            
            QString modelName = "Unknown";
            QString serialNumber = "Unknown";
            
            if (Spinnaker::GenApi::IsReadable(ptrDeviceModelName)) {
                modelName = QString::fromStdString(ptrDeviceModelName->GetValue().c_str());
            }
            if (Spinnaker::GenApi::IsReadable(ptrDeviceSerialNumber)) {
                serialNumber = QString::fromStdString(ptrDeviceSerialNumber->GetValue().c_str());
            }
            
            QString displayName = QString("Camera %1: %2 (S/N: %3)")
                .arg(i).arg(modelName).arg(serialNumber);
            cameraComboBox->addItem(displayName);
            
        } catch (Spinnaker::Exception& e) {
            qWarning() << "카메라 정보 읽기 실패:" << e.what();
            cameraComboBox->addItem(QString("Camera %1: Error").arg(i));
        }
    }
    
    if (!spinCameras.empty()) {
        cameraComboBox->setCurrentIndex(0);
        onCameraSelected(0);
    }
}
#endif

void CameraSettingsDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // 타이틀 바
    titleBar = new QWidget(this);
    titleBar->setObjectName("titleBar");
    titleBar->setFixedHeight(50);
    QHBoxLayout* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(20, 0, 10, 0);
    
    titleLabel = new QLabel("⚙️ 카메라 설정", titleBar);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    
    closeButtonTop = new QPushButton("✕", titleBar);
    closeButtonTop->setFixedSize(30, 30);
    closeButtonTop->setStyleSheet(
        "QPushButton { background-color: transparent; color: #ffffff; border: none; font-size: 20px; }"
        "QPushButton:hover { background-color: #c62828; border-radius: 15px; }"
    );
    connect(closeButtonTop, &QPushButton::clicked, this, &QDialog::reject);
    titleLayout->addWidget(closeButtonTop);
    
    mainLayout->addWidget(titleBar);
    
    // 탭 위젯
    tabWidget = new QTabWidget(this);
    tabWidget->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #3d3d3d; background-color: rgba(68, 68, 68, 200); }"
        "QTabBar::tab { background-color: rgba(45, 45, 45, 220); color: #ffffff; padding: 10px 20px; margin-right: 2px; border: 1px solid #3d3d3d; border-bottom: none; }"
        "QTabBar::tab:selected { background-color: rgba(68, 68, 68, 200); border-bottom: 1px solid rgba(68, 68, 68, 200); }"
        "QTabBar::tab:hover { background-color: rgba(80, 80, 80, 220); }"
    );
    
    // === 탭 1: 기본 설정 ===
    QWidget* basicTab = new QWidget();
    QVBoxLayout* basicLayout = new QVBoxLayout(basicTab);
    basicLayout->setSpacing(15);
    basicLayout->setContentsMargins(20, 20, 20, 20);
    
    // 카메라 선택
    QGroupBox* cameraGroup = new QGroupBox("카메라 선택", basicTab);
    QVBoxLayout* cameraLayout = new QVBoxLayout(cameraGroup);
    
    cameraComboBox = new QComboBox(basicTab);
    connect(cameraComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CameraSettingsDialog::onCameraSelected);
    cameraLayout->addWidget(cameraComboBox);
    
    basicLayout->addWidget(cameraGroup);
    
    // UserSet 선택
    QGroupBox* userSetGroup = new QGroupBox("UserSet 관리", basicTab);
    QHBoxLayout* userSetLayout = new QHBoxLayout(userSetGroup);
    
    userSetComboBox = new QComboBox(basicTab);
    userSetComboBox->addItem("UserSet1");
    userSetComboBox->addItem("UserSet2");
    userSetComboBox->addItem("UserSet3");
    userSetComboBox->addItem("Default");
    userSetLayout->addWidget(userSetComboBox);
    
    loadUserSetButton = new QPushButton("불러오기", basicTab);
    connect(loadUserSetButton, &QPushButton::clicked, this, &CameraSettingsDialog::onLoadUserSet);
    userSetLayout->addWidget(loadUserSetButton);
    
    saveUserSetButton = new QPushButton("저장하기", basicTab);
    connect(saveUserSetButton, &QPushButton::clicked, this, &CameraSettingsDialog::onSaveUserSet);
    userSetLayout->addWidget(saveUserSetButton);
    
    basicLayout->addWidget(userSetGroup);
    basicLayout->addStretch();
    
    tabWidget->addTab(basicTab, "기본 설정");
    
    // === 탭 2: 이미지 설정 ===
    QWidget* imageTab = new QWidget();
    QVBoxLayout* imageTabLayout = new QVBoxLayout(imageTab);
    imageTabLayout->setSpacing(15);
    imageTabLayout->setContentsMargins(20, 20, 20, 20);
    
    // 이미지 해상도
    QGroupBox* resolutionGroup = new QGroupBox("이미지 해상도", imageTab);
    QFormLayout* resolutionLayout = new QFormLayout(resolutionGroup);
    
    widthSpinBox = new QSpinBox(imageTab);
    widthSpinBox->setRange(64, 5472);
    widthSpinBox->setSingleStep(4);
    resolutionLayout->addRow("Width:", widthSpinBox);
    
    heightSpinBox = new QSpinBox(imageTab);
    heightSpinBox->setRange(64, 3648);
    heightSpinBox->setSingleStep(4);
    resolutionLayout->addRow("Height:", heightSpinBox);
    
    offsetXSpinBox = new QSpinBox(imageTab);
    offsetXSpinBox->setRange(0, 5000);
    offsetXSpinBox->setSingleStep(4);
    resolutionLayout->addRow("Offset X:", offsetXSpinBox);
    
    offsetYSpinBox = new QSpinBox(imageTab);
    offsetYSpinBox->setRange(0, 3000);
    offsetYSpinBox->setSingleStep(4);
    resolutionLayout->addRow("Offset Y:", offsetYSpinBox);
    
    imageTabLayout->addWidget(resolutionGroup);
    
    // 픽셀 포맷
    QGroupBox* pixelFormatGroup = new QGroupBox("픽셀 포맷", imageTab);
    QVBoxLayout* pixelFormatLayout = new QVBoxLayout(pixelFormatGroup);
    
    pixelFormatComboBox = new QComboBox(imageTab);
    pixelFormatComboBox->addItem("Mono8");
    pixelFormatComboBox->addItem("Mono16");
    pixelFormatComboBox->addItem("RGB8");
    pixelFormatComboBox->addItem("BGR8");
    pixelFormatComboBox->addItem("BayerRG8");
    pixelFormatComboBox->addItem("BayerBG8");
    pixelFormatLayout->addWidget(pixelFormatComboBox);
    
    imageTabLayout->addWidget(pixelFormatGroup);
    
    // 프레임 레이트
    QGroupBox* frameRateGroup = new QGroupBox("프레임 레이트", imageTab);
    QFormLayout* frameRateLayout = new QFormLayout(frameRateGroup);
    
    frameRateEnableCheckBox = new QCheckBox("활성화 (OFF = 최대 속도)", imageTab);
    connect(frameRateEnableCheckBox, &QCheckBox::stateChanged,
            this, &CameraSettingsDialog::onFrameRateEnableChanged);
    frameRateLayout->addRow("Frame Rate Enable:", frameRateEnableCheckBox);
    
    frameRateSpinBox = new QDoubleSpinBox(imageTab);
    frameRateSpinBox->setRange(1.0, 300.0);
    frameRateSpinBox->setSingleStep(1.0);
    frameRateSpinBox->setDecimals(2);
    frameRateSpinBox->setSuffix(" fps");
    frameRateLayout->addRow("Frame Rate:", frameRateSpinBox);
    
    frameRateRangeLabel = new QLabel("범위: - | ⚠️ Frame Rate Enable을 OFF하면 최대 속도로 동작합니다", imageTab);
    frameRateRangeLabel->setStyleSheet("color: #999999; font-size: 11px;");
    frameRateLayout->addRow("", frameRateRangeLabel);
    
    imageTabLayout->addWidget(frameRateGroup);
    imageTabLayout->addStretch();
    
    tabWidget->addTab(imageTab, "이미지 설정");
    
    // === 탭 3: 노출 & 게인 ===
    QWidget* exposureTab = new QWidget();
    QVBoxLayout* exposureTabLayout = new QVBoxLayout(exposureTab);
    exposureTabLayout->setSpacing(15);
    exposureTabLayout->setContentsMargins(20, 20, 20, 20);
    
    // 노출 설정
    QGroupBox* exposureGroup = new QGroupBox("노출 설정", exposureTab);
    QFormLayout* exposureLayout = new QFormLayout(exposureGroup);
    
    exposureAutoCheckBox = new QCheckBox("자동", exposureTab);
    connect(exposureAutoCheckBox, &QCheckBox::stateChanged,
            this, &CameraSettingsDialog::onExposureAutoChanged);
    exposureLayout->addRow("Auto Exposure:", exposureAutoCheckBox);
    
    exposureTimeSpinBox = new QDoubleSpinBox(exposureTab);
    exposureTimeSpinBox->setRange(0, 1000000);
    exposureTimeSpinBox->setSingleStep(100);
    exposureTimeSpinBox->setDecimals(0);
    exposureTimeSpinBox->setSuffix(" μs");
    exposureLayout->addRow("Exposure Time:", exposureTimeSpinBox);
    
    exposureRangeLabel = new QLabel("범위: -", exposureTab);
    exposureRangeLabel->setStyleSheet("color: #999999; font-size: 11px;");
    exposureLayout->addRow("", exposureRangeLabel);
    
    exposureTabLayout->addWidget(exposureGroup);
    
    // 게인 설정
    QGroupBox* gainGroup = new QGroupBox("게인 설정", exposureTab);
    QFormLayout* gainLayout = new QFormLayout(gainGroup);
    
    gainAutoCheckBox = new QCheckBox("자동", exposureTab);
    connect(gainAutoCheckBox, &QCheckBox::stateChanged,
            this, &CameraSettingsDialog::onGainAutoChanged);
    gainLayout->addRow("Auto Gain:", gainAutoCheckBox);
    
    gainSpinBox = new QDoubleSpinBox(exposureTab);
    gainSpinBox->setRange(0, 48);
    gainSpinBox->setSingleStep(0.1);
    gainSpinBox->setDecimals(1);
    gainSpinBox->setSuffix(" dB");
    gainLayout->addRow("Gain:", gainSpinBox);
    
    gainRangeLabel = new QLabel("범위: -", exposureTab);
    gainRangeLabel->setStyleSheet("color: #999999; font-size: 11px;");
    gainLayout->addRow("", gainRangeLabel);
    
    exposureTabLayout->addWidget(gainGroup);
    exposureTabLayout->addStretch();
    
    tabWidget->addTab(exposureTab, "노출 & 게인");
    
    // === 탭 4: 색상 & 화질 ===
    QWidget* colorTab = new QWidget();
    QVBoxLayout* colorLayout = new QVBoxLayout(colorTab);
    colorLayout->setSpacing(15);
    colorLayout->setContentsMargins(20, 20, 20, 20);
    
    // 화이트 밸런스
    QGroupBox* wbGroup = new QGroupBox("화이트 밸런스", colorTab);
    QFormLayout* wbLayout = new QFormLayout(wbGroup);
    
    whiteBalanceAutoCheckBox = new QCheckBox("자동", colorTab);
    wbLayout->addRow("Auto White Balance:", whiteBalanceAutoCheckBox);
    
    whiteBalanceRedSpinBox = new QDoubleSpinBox(colorTab);
    whiteBalanceRedSpinBox->setRange(0, 8);
    whiteBalanceRedSpinBox->setSingleStep(0.01);
    whiteBalanceRedSpinBox->setDecimals(2);
    wbLayout->addRow("Red:", whiteBalanceRedSpinBox);
    
    whiteBalanceBlueSpinBox = new QDoubleSpinBox(colorTab);
    whiteBalanceBlueSpinBox->setRange(0, 8);
    whiteBalanceBlueSpinBox->setSingleStep(0.01);
    whiteBalanceBlueSpinBox->setDecimals(2);
    wbLayout->addRow("Blue:", whiteBalanceBlueSpinBox);
    
    colorLayout->addWidget(wbGroup);
    
    // 감마
    QGroupBox* gammaGroup = new QGroupBox("감마", colorTab);
    QFormLayout* gammaLayout = new QFormLayout(gammaGroup);
    
    gammaEnableCheckBox = new QCheckBox("활성화", colorTab);
    gammaLayout->addRow("Gamma Enable:", gammaEnableCheckBox);
    
    gammaSpinBox = new QDoubleSpinBox(colorTab);
    gammaSpinBox->setRange(0.25, 4.0);
    gammaSpinBox->setSingleStep(0.05);
    gammaSpinBox->setDecimals(2);
    gammaLayout->addRow("Gamma:", gammaSpinBox);
    
    colorLayout->addWidget(gammaGroup);
    
    // 블랙 레벨
    QGroupBox* blackLevelGroup = new QGroupBox("블랙 레벨", colorTab);
    QFormLayout* blackLevelLayout = new QFormLayout(blackLevelGroup);
    
    blackLevelSpinBox = new QDoubleSpinBox(colorTab);
    blackLevelSpinBox->setRange(0.0, 10.0);
    blackLevelSpinBox->setSingleStep(0.1);
    blackLevelSpinBox->setDecimals(2);
    blackLevelLayout->addRow("Black Level:", blackLevelSpinBox);
    
    colorLayout->addWidget(blackLevelGroup);
    
    // 샤프니스
    QGroupBox* sharpnessGroup = new QGroupBox("샤프니스", colorTab);
    QFormLayout* sharpnessLayout = new QFormLayout(sharpnessGroup);
    
    sharpnessEnableCheckBox = new QCheckBox("활성화", colorTab);
    sharpnessLayout->addRow("Sharpness Enable:", sharpnessEnableCheckBox);
    
    sharpnessSpinBox = new QDoubleSpinBox(colorTab);
    sharpnessSpinBox->setRange(0.0, 4.0);
    sharpnessSpinBox->setSingleStep(0.1);
    sharpnessSpinBox->setDecimals(2);
    sharpnessLayout->addRow("Sharpness:", sharpnessSpinBox);
    
    colorLayout->addWidget(sharpnessGroup);
    colorLayout->addStretch();
    
    tabWidget->addTab(colorTab, "색상 & 화질");
    
    // === 탭 5: 트리거 ===
    QWidget* triggerTab = new QWidget();
    QVBoxLayout* triggerLayout = new QVBoxLayout(triggerTab);
    triggerLayout->setSpacing(15);
    triggerLayout->setContentsMargins(20, 20, 20, 20);
    
    // Acquisition Mode
    QGroupBox* acquisitionGroup = new QGroupBox("Acquisition 설정", triggerTab);
    QFormLayout* acquisitionLayout = new QFormLayout(acquisitionGroup);
    
    acquisitionModeComboBox = new QComboBox(triggerTab);
    acquisitionModeComboBox->addItem("Continuous");
    acquisitionModeComboBox->addItem("SingleFrame");
    acquisitionModeComboBox->addItem("MultiFrame");
    acquisitionLayout->addRow("Acquisition Mode:", acquisitionModeComboBox);
    
    triggerLayout->addWidget(acquisitionGroup);
    
    // 트리거 설정
    QGroupBox* triggerGroup = new QGroupBox("트리거 설정", triggerTab);
    QFormLayout* triggerFormLayout = new QFormLayout(triggerGroup);
    
    triggerModeComboBox = new QComboBox(triggerTab);
    triggerModeComboBox->addItem("Off");
    triggerModeComboBox->addItem("On");
    triggerFormLayout->addRow("Trigger Mode:", triggerModeComboBox);
    
    triggerSourceComboBox = new QComboBox(triggerTab);
    triggerSourceComboBox->addItem("Software");
    triggerSourceComboBox->addItem("Line0");
    triggerSourceComboBox->addItem("Line1");
    triggerSourceComboBox->addItem("Line2");
    triggerSourceComboBox->addItem("Line3");
    triggerFormLayout->addRow("Trigger Source:", triggerSourceComboBox);
    
    // 트리거 영상 저장 체크박스 추가
    saveTriggerImagesCheckBox = new QCheckBox("트리거 영상 자동 저장", triggerTab);
    saveTriggerImagesCheckBox->setChecked(ConfigManager::instance()->getSaveTriggerImages());
    connect(saveTriggerImagesCheckBox, &QCheckBox::stateChanged, [](int state) {
        ConfigManager::instance()->setSaveTriggerImages(state == Qt::Checked);
        qDebug() << "[CameraSettings] 트리거 영상 저장:" << (state == Qt::Checked);
    });
    triggerFormLayout->addRow("", saveTriggerImagesCheckBox);
    
    triggerLayout->addWidget(triggerGroup);
    triggerLayout->addStretch();
    
    tabWidget->addTab(triggerTab, "트리거");
    
    mainLayout->addWidget(tabWidget);
    
    // 하단 버튼 영역
    QWidget* buttonBar = new QWidget(this);
    buttonBar->setObjectName("buttonBar");
    QHBoxLayout* buttonLayout = new QHBoxLayout(buttonBar);
    buttonLayout->setContentsMargins(20, 15, 20, 15);
    buttonLayout->addStretch();
    
    applyButton = new QPushButton("적용", this);
    applyButton->setMinimumWidth(100);
    applyButton->setMinimumHeight(35);
    connect(applyButton, &QPushButton::clicked, this, &CameraSettingsDialog::onApplySettings);
    buttonLayout->addWidget(applyButton);
    
    closeButton = new QPushButton("닫기", this);
    closeButton->setMinimumWidth(100);
    closeButton->setMinimumHeight(35);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(closeButton);
    
    mainLayout->addWidget(buttonBar);
}

void CameraSettingsDialog::applyBlackTheme()
{
    setStyleSheet(
        "QDialog { background-color: rgba(68, 68, 68, 200); border: 1px solid white; }"
        "#titleBar { background-color: rgba(45, 45, 45, 220); }"
        "#buttonBar { background-color: rgba(45, 45, 45, 220); }"
        "QScrollArea { background-color: transparent; border: none; }"
        "QGroupBox { border: 1px solid #3d3d3d; border-radius: 5px; margin-top: 10px; padding-top: 10px; color: #ffffff; font-weight: bold; background-color: rgba(37, 37, 37, 180); }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
        "QLabel { color: #ffffff; background-color: transparent; }"
        "QComboBox { background-color: rgb(80, 80, 80); color: #ffffff; border: 1px solid rgb(100, 100, 100); padding: 5px; border-radius: 3px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: none; border: none; }"
        "QComboBox QAbstractItemView { background-color: rgb(80, 80, 80); color: #ffffff; selection-background-color: #4CAF50; }"
        "QSpinBox, QDoubleSpinBox { background-color: rgb(80, 80, 80); color: #ffffff; border: 1px solid rgb(100, 100, 100); padding: 5px; border-radius: 3px; }"
        "QCheckBox { color: #ffffff; background-color: transparent; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid #3d3d3d; background-color: rgb(80, 80, 80); border-radius: 3px; }"
        "QCheckBox::indicator:checked { background-color: #4CAF50; border: 1px solid #4CAF50; }"
        "QScrollBar:vertical { border: none; background-color: rgba(30, 30, 30, 100); width: 12px; margin: 0; }"
        "QScrollBar::handle:vertical { background-color: rgb(80, 80, 80); min-height: 30px; border-radius: 6px; }"
        "QScrollBar::handle:vertical:hover { background-color: rgb(100, 100, 100); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
        "QPushButton { background-color: rgb(80, 80, 80); color: #ffffff; border: 1px solid rgb(100, 100, 100); padding: 8px 20px; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: rgb(100, 100, 100); }"
        "QPushButton:pressed { background-color: rgb(60, 60, 60); }"
    );
}

void CameraSettingsDialog::onCameraSelected(int index)
{
    if (index < 0 || index >= static_cast<int>(spinCameras.size())) {
        return;
    }
    
    currentCameraIndex = index;
#ifdef USE_SPINNAKER
    currentCamera = spinCameras[index];
    updateUIFromCamera();
#endif
}

void CameraSettingsDialog::onLoadUserSet()
{
#ifdef USE_SPINNAKER
    if (!currentCamera) {
        CustomMessageBox msgBox(this, CustomMessageBox::Warning, "경고",
            "카메라가 선택되지 않았습니다.");
        msgBox.exec();
        return;
    }
    
    QString userSetName = userSetComboBox->currentText();
    
    try {
        Spinnaker::GenApi::INodeMap& nodeMap = currentCamera->GetNodeMap();
        
        // UserSetSelector 설정
        Spinnaker::GenApi::CEnumerationPtr ptrUserSetSelector = nodeMap.GetNode("UserSetSelector");
        if (Spinnaker::GenApi::IsWritable(ptrUserSetSelector)) {
            Spinnaker::GenApi::CEnumEntryPtr ptrUserSet = ptrUserSetSelector->GetEntryByName(userSetName.toStdString().c_str());
            if (Spinnaker::GenApi::IsReadable(ptrUserSet)) {
                ptrUserSetSelector->SetIntValue(ptrUserSet->GetValue());
                
                // UserSetLoad 실행
                Spinnaker::GenApi::CCommandPtr ptrUserSetLoad = nodeMap.GetNode("UserSetLoad");
                if (Spinnaker::GenApi::IsWritable(ptrUserSetLoad)) {
                    ptrUserSetLoad->Execute();
                    
                    qDebug() << "[CameraSettings]" << userSetName << "로드 완료";
                    
                    // 최대 성능을 위해 자동 기능들 모두 OFF
                    try {
                        // Exposure Auto OFF
                        Spinnaker::GenApi::CEnumerationPtr ptrExposureAuto = nodeMap.GetNode("ExposureAuto");
                        if (Spinnaker::GenApi::IsWritable(ptrExposureAuto)) {
                            ptrExposureAuto->SetIntValue(ptrExposureAuto->GetEntryByName("Off")->GetValue());
                            qDebug() << "[CameraSettings] ExposureAuto -> Off";
                            
                            // Exposure Time 최소값으로 설정 (최대 프레임 레이트)
                            Spinnaker::GenApi::CFloatPtr ptrExposureTime = nodeMap.GetNode("ExposureTime");
                            if (Spinnaker::GenApi::IsWritable(ptrExposureTime)) {
                                ptrExposureTime->SetValue(ptrExposureTime->GetMin());
                                qDebug() << "[CameraSettings] ExposureTime -> Min:" << ptrExposureTime->GetMin();
                            }
                        }
                        
                        // Gain Auto OFF
                        Spinnaker::GenApi::CEnumerationPtr ptrGainAuto = nodeMap.GetNode("GainAuto");
                        if (Spinnaker::GenApi::IsWritable(ptrGainAuto)) {
                            ptrGainAuto->SetIntValue(ptrGainAuto->GetEntryByName("Off")->GetValue());
                            qDebug() << "[CameraSettings] GainAuto -> Off";
                        }
                        
                        // White Balance Auto OFF
                        Spinnaker::GenApi::CEnumerationPtr ptrBalanceWhiteAuto = nodeMap.GetNode("BalanceWhiteAuto");
                        if (Spinnaker::GenApi::IsWritable(ptrBalanceWhiteAuto)) {
                            ptrBalanceWhiteAuto->SetIntValue(ptrBalanceWhiteAuto->GetEntryByName("Off")->GetValue());
                            qDebug() << "[CameraSettings] BalanceWhiteAuto -> Off";
                        }
                        
                        // Frame Rate Enable OFF (중요! 이게 켜져있으면 최대 속도 안나옴)
                        Spinnaker::GenApi::CBooleanPtr ptrFrameRateEnable = nodeMap.GetNode("AcquisitionFrameRateEnable");
                        if (Spinnaker::GenApi::IsWritable(ptrFrameRateEnable)) {
                            ptrFrameRateEnable->SetValue(false);
                            qDebug() << "[CameraSettings] AcquisitionFrameRateEnable -> Off (최대 속도 모드)";
                        }
                        
                        qDebug() << "[CameraSettings] 모든 Auto 기능 OFF 완료 - 최대 성능 모드";
                        
                    } catch (Spinnaker::Exception& e) {
                        qWarning() << "[CameraSettings] Auto OFF 설정 실패:" << e.what();
                    }
                    
                    // UI 업데이트
                    updateUIFromCamera();
                    
                    CustomMessageBox msgBox(this, CustomMessageBox::Information, "완료",
                        QString("%1이(가) 로드되었습니다.\n모든 Auto 기능이 OFF되어 최대 성능으로 설정되었습니다.").arg(userSetName));
                    msgBox.exec();
                }
            }
        }
    } catch (Spinnaker::Exception& e) {
        qWarning() << "[CameraSettings] UserSet 로드 실패:" << e.what();
        CustomMessageBox msgBox(this, CustomMessageBox::Critical, "오류",
            QString("UserSet 로드 실패:\n%1").arg(e.what()));
        msgBox.exec();
    }
#endif
}

void CameraSettingsDialog::onSaveUserSet()
{
#ifdef USE_SPINNAKER
    if (!currentCamera) {
        CustomMessageBox msgBox(this, CustomMessageBox::Warning, "경고",
            "카메라가 선택되지 않았습니다.");
        msgBox.exec();
        return;
    }
    
    QString userSetName = userSetComboBox->currentText();
    
    if (userSetName == "Default") {
        CustomMessageBox msgBox(this, CustomMessageBox::Warning, "경고",
            "Default UserSet은 저장할 수 없습니다.");
        msgBox.exec();
        return;
    }
    
    CustomMessageBox confirmBox(this, CustomMessageBox::Question, "확인",
        QString("%1에 현재 설정을 저장하시겠습니까?").arg(userSetName),
        QMessageBox::Yes | QMessageBox::No);
    
    if (confirmBox.exec() != QMessageBox::Yes) {
        return;
    }
    
    try {
        Spinnaker::GenApi::INodeMap& nodeMap = currentCamera->GetNodeMap();
        
        // UserSetSelector 설정
        Spinnaker::GenApi::CEnumerationPtr ptrUserSetSelector = nodeMap.GetNode("UserSetSelector");
        if (Spinnaker::GenApi::IsWritable(ptrUserSetSelector)) {
            Spinnaker::GenApi::CEnumEntryPtr ptrUserSet = ptrUserSetSelector->GetEntryByName(userSetName.toStdString().c_str());
            if (Spinnaker::GenApi::IsReadable(ptrUserSet)) {
                ptrUserSetSelector->SetIntValue(ptrUserSet->GetValue());
                
                // UserSetSave 실행
                Spinnaker::GenApi::CCommandPtr ptrUserSetSave = nodeMap.GetNode("UserSetSave");
                if (Spinnaker::GenApi::IsWritable(ptrUserSetSave)) {
                    ptrUserSetSave->Execute();
                    
                    qDebug() << "[CameraSettings]" << userSetName << "저장 완료";
                    
                    // UserSetDefault를 현재 UserSet으로 설정
                    Spinnaker::GenApi::CEnumerationPtr ptrUserSetDefault = nodeMap.GetNode("UserSetDefault");
                    if (Spinnaker::GenApi::IsWritable(ptrUserSetDefault)) {
                        Spinnaker::GenApi::CEnumEntryPtr ptrDefaultUserSet = ptrUserSetDefault->GetEntryByName(userSetName.toStdString().c_str());
                        if (Spinnaker::GenApi::IsReadable(ptrDefaultUserSet)) {
                            ptrUserSetDefault->SetIntValue(ptrDefaultUserSet->GetValue());
                            qDebug() << "[CameraSettings]" << userSetName << "을(를) 기본값으로 설정 완료";
                        }
                    }
                    
                    CustomMessageBox msgBox(this, CustomMessageBox::Information, "완료",
                        QString("%1에 저장되었고 기본값으로 설정되었습니다.").arg(userSetName));
                    msgBox.exec();
                }
            }
        }
    } catch (Spinnaker::Exception& e) {
        qWarning() << "[CameraSettings] UserSet 저장 실패:" << e.what();
        CustomMessageBox msgBox(this, CustomMessageBox::Critical, "오류",
            QString("UserSet 저장 실패:\n%1").arg(e.what()));
        msgBox.exec();
    }
#endif
}

void CameraSettingsDialog::onApplySettings()
{
#ifdef USE_SPINNAKER
    if (!currentCamera) {
        CustomMessageBox msgBox(this, CustomMessageBox::Warning, "경고",
            "카메라가 선택되지 않았습니다.");
        msgBox.exec();
        return;
    }
    
    try {
        Spinnaker::GenApi::INodeMap& nodeMap = currentCamera->GetNodeMap();
        
        // Exposure 설정
        Spinnaker::GenApi::CEnumerationPtr ptrExposureAuto = nodeMap.GetNode("ExposureAuto");
        if (Spinnaker::GenApi::IsWritable(ptrExposureAuto)) {
            if (exposureAutoCheckBox->isChecked()) {
                ptrExposureAuto->SetIntValue(ptrExposureAuto->GetEntryByName("Continuous")->GetValue());
            } else {
                ptrExposureAuto->SetIntValue(ptrExposureAuto->GetEntryByName("Off")->GetValue());
                
                Spinnaker::GenApi::CFloatPtr ptrExposureTime = nodeMap.GetNode("ExposureTime");
                if (Spinnaker::GenApi::IsWritable(ptrExposureTime)) {
                    ptrExposureTime->SetValue(exposureTimeSpinBox->value());
                }
            }
        }
        
        // Gain 설정
        Spinnaker::GenApi::CEnumerationPtr ptrGainAuto = nodeMap.GetNode("GainAuto");
        if (Spinnaker::GenApi::IsWritable(ptrGainAuto)) {
            if (gainAutoCheckBox->isChecked()) {
                ptrGainAuto->SetIntValue(ptrGainAuto->GetEntryByName("Continuous")->GetValue());
            } else {
                ptrGainAuto->SetIntValue(ptrGainAuto->GetEntryByName("Off")->GetValue());
                
                Spinnaker::GenApi::CFloatPtr ptrGain = nodeMap.GetNode("Gain");
                if (Spinnaker::GenApi::IsWritable(ptrGain)) {
                    ptrGain->SetValue(gainSpinBox->value());
                }
            }
        }
        
        // White Balance 설정
        Spinnaker::GenApi::CEnumerationPtr ptrBalanceWhiteAuto = nodeMap.GetNode("BalanceWhiteAuto");
        if (Spinnaker::GenApi::IsWritable(ptrBalanceWhiteAuto)) {
            if (whiteBalanceAutoCheckBox->isChecked()) {
                ptrBalanceWhiteAuto->SetIntValue(ptrBalanceWhiteAuto->GetEntryByName("Continuous")->GetValue());
            } else {
                ptrBalanceWhiteAuto->SetIntValue(ptrBalanceWhiteAuto->GetEntryByName("Off")->GetValue());
                
                // Red
                Spinnaker::GenApi::CEnumerationPtr ptrBalanceRatioSelector = nodeMap.GetNode("BalanceRatioSelector");
                if (Spinnaker::GenApi::IsWritable(ptrBalanceRatioSelector)) {
                    ptrBalanceRatioSelector->SetIntValue(ptrBalanceRatioSelector->GetEntryByName("Red")->GetValue());
                    Spinnaker::GenApi::CFloatPtr ptrBalanceRatio = nodeMap.GetNode("BalanceRatio");
                    if (Spinnaker::GenApi::IsWritable(ptrBalanceRatio)) {
                        ptrBalanceRatio->SetValue(whiteBalanceRedSpinBox->value());
                    }
                    
                    // Blue
                    ptrBalanceRatioSelector->SetIntValue(ptrBalanceRatioSelector->GetEntryByName("Blue")->GetValue());
                    if (Spinnaker::GenApi::IsWritable(ptrBalanceRatio)) {
                        ptrBalanceRatio->SetValue(whiteBalanceBlueSpinBox->value());
                    }
                }
            }
        }
        
        // Gamma 설정
        Spinnaker::GenApi::CBooleanPtr ptrGammaEnable = nodeMap.GetNode("GammaEnable");
        if (Spinnaker::GenApi::IsWritable(ptrGammaEnable)) {
            ptrGammaEnable->SetValue(gammaEnableCheckBox->isChecked());
            
            if (gammaEnableCheckBox->isChecked()) {
                Spinnaker::GenApi::CFloatPtr ptrGamma = nodeMap.GetNode("Gamma");
                if (Spinnaker::GenApi::IsWritable(ptrGamma)) {
                    ptrGamma->SetValue(gammaSpinBox->value());
                }
            }
        }
        
        // 이미지 해상도 설정
        Spinnaker::GenApi::CIntegerPtr ptrWidth = nodeMap.GetNode("Width");
        if (Spinnaker::GenApi::IsWritable(ptrWidth)) {
            ptrWidth->SetValue(widthSpinBox->value());
        }
        
        Spinnaker::GenApi::CIntegerPtr ptrHeight = nodeMap.GetNode("Height");
        if (Spinnaker::GenApi::IsWritable(ptrHeight)) {
            ptrHeight->SetValue(heightSpinBox->value());
        }
        
        Spinnaker::GenApi::CIntegerPtr ptrOffsetX = nodeMap.GetNode("OffsetX");
        if (Spinnaker::GenApi::IsWritable(ptrOffsetX)) {
            ptrOffsetX->SetValue(offsetXSpinBox->value());
        }
        
        Spinnaker::GenApi::CIntegerPtr ptrOffsetY = nodeMap.GetNode("OffsetY");
        if (Spinnaker::GenApi::IsWritable(ptrOffsetY)) {
            ptrOffsetY->SetValue(offsetYSpinBox->value());
        }
        
        // 픽셀 포맷 설정
        Spinnaker::GenApi::CEnumerationPtr ptrPixelFormat = nodeMap.GetNode("PixelFormat");
        if (Spinnaker::GenApi::IsWritable(ptrPixelFormat)) {
            QString pixelFormat = pixelFormatComboBox->currentText();
            Spinnaker::GenApi::CEnumEntryPtr ptrPixelFormatEntry = ptrPixelFormat->GetEntryByName(pixelFormat.toStdString().c_str());
            if (Spinnaker::GenApi::IsReadable(ptrPixelFormatEntry)) {
                ptrPixelFormat->SetIntValue(ptrPixelFormatEntry->GetValue());
            }
        }
        
        // 프레임 레이트 설정
        Spinnaker::GenApi::CBooleanPtr ptrFrameRateEnable = nodeMap.GetNode("AcquisitionFrameRateEnable");
        if (Spinnaker::GenApi::IsWritable(ptrFrameRateEnable)) {
            bool enableFrameRate = frameRateEnableCheckBox->isChecked();
            ptrFrameRateEnable->SetValue(enableFrameRate);
            
            if (enableFrameRate) {
                Spinnaker::GenApi::CFloatPtr ptrFrameRate = nodeMap.GetNode("AcquisitionFrameRate");
                if (Spinnaker::GenApi::IsWritable(ptrFrameRate)) {
                    ptrFrameRate->SetValue(frameRateSpinBox->value());
                    qDebug() << "[CameraSettings] Frame Rate 제한:" << frameRateSpinBox->value() << "fps";
                }
            } else {
                qDebug() << "[CameraSettings] Frame Rate 제한 해제 - 최대 속도 모드";
            }
        }
        
        // 블랙 레벨 설정
        Spinnaker::GenApi::CFloatPtr ptrBlackLevel = nodeMap.GetNode("BlackLevel");
        if (Spinnaker::GenApi::IsWritable(ptrBlackLevel)) {
            ptrBlackLevel->SetValue(blackLevelSpinBox->value());
        }
        
        // 샤프니스 설정
        Spinnaker::GenApi::CBooleanPtr ptrSharpnessEnable = nodeMap.GetNode("SharpeningEnable");
        if (Spinnaker::GenApi::IsWritable(ptrSharpnessEnable)) {
            ptrSharpnessEnable->SetValue(sharpnessEnableCheckBox->isChecked());
            
            if (sharpnessEnableCheckBox->isChecked()) {
                Spinnaker::GenApi::CFloatPtr ptrSharpness = nodeMap.GetNode("Sharpening");
                if (Spinnaker::GenApi::IsWritable(ptrSharpness)) {
                    ptrSharpness->SetValue(sharpnessSpinBox->value());
                }
            }
        }
        
        // Acquisition Mode 설정
        Spinnaker::GenApi::CEnumerationPtr ptrAcquisitionMode = nodeMap.GetNode("AcquisitionMode");
        if (Spinnaker::GenApi::IsWritable(ptrAcquisitionMode)) {
            QString acqMode = acquisitionModeComboBox->currentText();
            Spinnaker::GenApi::CEnumEntryPtr ptrAcqModeEntry = ptrAcquisitionMode->GetEntryByName(acqMode.toStdString().c_str());
            if (Spinnaker::GenApi::IsReadable(ptrAcqModeEntry)) {
                ptrAcquisitionMode->SetIntValue(ptrAcqModeEntry->GetValue());
            }
        }
        
        // 트리거 설정
        Spinnaker::GenApi::CEnumerationPtr ptrTriggerMode = nodeMap.GetNode("TriggerMode");
        if (Spinnaker::GenApi::IsWritable(ptrTriggerMode)) {
            QString trigMode = triggerModeComboBox->currentText();
            Spinnaker::GenApi::CEnumEntryPtr ptrTrigModeEntry = ptrTriggerMode->GetEntryByName(trigMode.toStdString().c_str());
            if (Spinnaker::GenApi::IsReadable(ptrTrigModeEntry)) {
                ptrTriggerMode->SetIntValue(ptrTrigModeEntry->GetValue());
            }
        }
        
        Spinnaker::GenApi::CEnumerationPtr ptrTriggerSource = nodeMap.GetNode("TriggerSource");
        if (Spinnaker::GenApi::IsWritable(ptrTriggerSource)) {
            QString trigSource = triggerSourceComboBox->currentText();
            Spinnaker::GenApi::CEnumEntryPtr ptrTrigSourceEntry = ptrTriggerSource->GetEntryByName(trigSource.toStdString().c_str());
            if (Spinnaker::GenApi::IsReadable(ptrTrigSourceEntry)) {
                ptrTriggerSource->SetIntValue(ptrTrigSourceEntry->GetValue());
            }
        }
        
        qDebug() << "[CameraSettings] 설정 적용 완료";
        
        CustomMessageBox msgBox(this, CustomMessageBox::Information, "완료",
            "설정이 적용되었습니다.");
        msgBox.exec();
        
    } catch (Spinnaker::Exception& e) {
        qWarning() << "[CameraSettings] 설정 적용 실패:" << e.what();
        CustomMessageBox msgBox(this, CustomMessageBox::Critical, "오류",
            QString("설정 적용 실패:\n%1").arg(e.what()));
        msgBox.exec();
    }
#endif
}

void CameraSettingsDialog::onExposureAutoChanged(int state)
{
    exposureTimeSpinBox->setEnabled(state == Qt::Unchecked);
}

void CameraSettingsDialog::onGainAutoChanged(int state)
{
    gainSpinBox->setEnabled(state == Qt::Unchecked);
}

void CameraSettingsDialog::onFrameRateEnableChanged(int state)
{
#ifdef USE_SPINNAKER
    if (!currentCamera) {
        return;
    }
    
    try {
        Spinnaker::GenApi::INodeMap& nodeMap = currentCamera->GetNodeMap();
        
        // Frame Rate Enable 설정 즉시 적용
        Spinnaker::GenApi::CBooleanPtr ptrFrameRateEnable = nodeMap.GetNode("AcquisitionFrameRateEnable");
        if (Spinnaker::GenApi::IsWritable(ptrFrameRateEnable)) {
            bool enableFrameRate = (state == Qt::Checked);
            ptrFrameRateEnable->SetValue(enableFrameRate);
            
            if (enableFrameRate) {
                // Frame Rate 값도 함께 적용
                Spinnaker::GenApi::CFloatPtr ptrFrameRate = nodeMap.GetNode("AcquisitionFrameRate");
                if (Spinnaker::GenApi::IsWritable(ptrFrameRate)) {
                    ptrFrameRate->SetValue(frameRateSpinBox->value());
                    qDebug() << "[CameraSettings] Frame Rate 제한 즉시 적용:" << frameRateSpinBox->value() << "fps";
                }
            } else {
                qDebug() << "[CameraSettings] Frame Rate 제한 해제 즉시 적용 - 최대 속도 모드";
            }
        }
    } catch (Spinnaker::Exception& e) {
        qWarning() << "[CameraSettings] Frame Rate 설정 실패:" << e.what();
    }
#endif
}

void CameraSettingsDialog::updateUIFromCamera()
{
#ifdef USE_SPINNAKER
    if (!currentCamera) {
        return;
    }
    
    try {
        Spinnaker::GenApi::INodeMap& nodeMap = currentCamera->GetNodeMap();
        
        // Exposure 읽기
        Spinnaker::GenApi::CEnumerationPtr ptrExposureAuto = nodeMap.GetNode("ExposureAuto");
        if (Spinnaker::GenApi::IsReadable(ptrExposureAuto)) {
            QString exposureAutoStr = QString::fromStdString(ptrExposureAuto->GetCurrentEntry()->GetSymbolic().c_str());
            exposureAutoCheckBox->setChecked(exposureAutoStr != "Off");
        }
        
        Spinnaker::GenApi::CFloatPtr ptrExposureTime = nodeMap.GetNode("ExposureTime");
        if (Spinnaker::GenApi::IsReadable(ptrExposureTime)) {
            exposureTimeSpinBox->setValue(ptrExposureTime->GetValue());
            exposureTimeSpinBox->setRange(ptrExposureTime->GetMin(), ptrExposureTime->GetMax());
            exposureRangeLabel->setText(QString("범위: %1 - %2 μs")
                .arg(ptrExposureTime->GetMin(), 0, 'f', 0)
                .arg(ptrExposureTime->GetMax(), 0, 'f', 0));
        }
        
        // Gain 읽기
        Spinnaker::GenApi::CEnumerationPtr ptrGainAuto = nodeMap.GetNode("GainAuto");
        if (Spinnaker::GenApi::IsReadable(ptrGainAuto)) {
            QString gainAutoStr = QString::fromStdString(ptrGainAuto->GetCurrentEntry()->GetSymbolic().c_str());
            gainAutoCheckBox->setChecked(gainAutoStr != "Off");
        }
        
        Spinnaker::GenApi::CFloatPtr ptrGain = nodeMap.GetNode("Gain");
        if (Spinnaker::GenApi::IsReadable(ptrGain)) {
            gainSpinBox->setValue(ptrGain->GetValue());
            gainSpinBox->setRange(ptrGain->GetMin(), ptrGain->GetMax());
            gainRangeLabel->setText(QString("범위: %1 - %2 dB")
                .arg(ptrGain->GetMin(), 0, 'f', 1)
                .arg(ptrGain->GetMax(), 0, 'f', 1));
        }
        
        // White Balance 읽기
        Spinnaker::GenApi::CEnumerationPtr ptrBalanceWhiteAuto = nodeMap.GetNode("BalanceWhiteAuto");
        if (Spinnaker::GenApi::IsReadable(ptrBalanceWhiteAuto)) {
            QString wbAutoStr = QString::fromStdString(ptrBalanceWhiteAuto->GetCurrentEntry()->GetSymbolic().c_str());
            whiteBalanceAutoCheckBox->setChecked(wbAutoStr != "Off");
        }
        
        Spinnaker::GenApi::CEnumerationPtr ptrBalanceRatioSelector = nodeMap.GetNode("BalanceRatioSelector");
        Spinnaker::GenApi::CFloatPtr ptrBalanceRatio = nodeMap.GetNode("BalanceRatio");
        if (Spinnaker::GenApi::IsWritable(ptrBalanceRatioSelector) && Spinnaker::GenApi::IsReadable(ptrBalanceRatio)) {
            // Red
            ptrBalanceRatioSelector->SetIntValue(ptrBalanceRatioSelector->GetEntryByName("Red")->GetValue());
            whiteBalanceRedSpinBox->setValue(ptrBalanceRatio->GetValue());
            
            // Blue
            ptrBalanceRatioSelector->SetIntValue(ptrBalanceRatioSelector->GetEntryByName("Blue")->GetValue());
            whiteBalanceBlueSpinBox->setValue(ptrBalanceRatio->GetValue());
        }
        
        // Gamma 읽기
        Spinnaker::GenApi::CBooleanPtr ptrGammaEnable = nodeMap.GetNode("GammaEnable");
        if (Spinnaker::GenApi::IsReadable(ptrGammaEnable)) {
            gammaEnableCheckBox->setChecked(ptrGammaEnable->GetValue());
        }
        
        Spinnaker::GenApi::CFloatPtr ptrGamma = nodeMap.GetNode("Gamma");
        if (Spinnaker::GenApi::IsReadable(ptrGamma)) {
            gammaSpinBox->setValue(ptrGamma->GetValue());
        }
        
        // 이미지 해상도 읽기
        Spinnaker::GenApi::CIntegerPtr ptrWidth = nodeMap.GetNode("Width");
        if (Spinnaker::GenApi::IsReadable(ptrWidth)) {
            widthSpinBox->setValue(ptrWidth->GetValue());
            widthSpinBox->setRange(ptrWidth->GetMin(), ptrWidth->GetMax());
        }
        
        Spinnaker::GenApi::CIntegerPtr ptrHeight = nodeMap.GetNode("Height");
        if (Spinnaker::GenApi::IsReadable(ptrHeight)) {
            heightSpinBox->setValue(ptrHeight->GetValue());
            heightSpinBox->setRange(ptrHeight->GetMin(), ptrHeight->GetMax());
        }
        
        Spinnaker::GenApi::CIntegerPtr ptrOffsetX = nodeMap.GetNode("OffsetX");
        if (Spinnaker::GenApi::IsReadable(ptrOffsetX)) {
            offsetXSpinBox->setValue(ptrOffsetX->GetValue());
            offsetXSpinBox->setRange(ptrOffsetX->GetMin(), ptrOffsetX->GetMax());
        }
        
        Spinnaker::GenApi::CIntegerPtr ptrOffsetY = nodeMap.GetNode("OffsetY");
        if (Spinnaker::GenApi::IsReadable(ptrOffsetY)) {
            offsetYSpinBox->setValue(ptrOffsetY->GetValue());
            offsetYSpinBox->setRange(ptrOffsetY->GetMin(), ptrOffsetY->GetMax());
        }
        
        // 픽셀 포맷 읽기
        Spinnaker::GenApi::CEnumerationPtr ptrPixelFormat = nodeMap.GetNode("PixelFormat");
        if (Spinnaker::GenApi::IsReadable(ptrPixelFormat)) {
            QString pixelFormat = QString::fromStdString(ptrPixelFormat->GetCurrentEntry()->GetSymbolic().c_str());
            int index = pixelFormatComboBox->findText(pixelFormat);
            if (index >= 0) {
                pixelFormatComboBox->setCurrentIndex(index);
            }
        }
        
        // 프레임 레이트 읽기
        Spinnaker::GenApi::CBooleanPtr ptrFrameRateEnable = nodeMap.GetNode("AcquisitionFrameRateEnable");
        if (Spinnaker::GenApi::IsReadable(ptrFrameRateEnable)) {
            frameRateEnableCheckBox->setChecked(ptrFrameRateEnable->GetValue());
        }
        
        Spinnaker::GenApi::CFloatPtr ptrFrameRate = nodeMap.GetNode("AcquisitionFrameRate");
        if (Spinnaker::GenApi::IsReadable(ptrFrameRate)) {
            double cameraMin = ptrFrameRate->GetMin();
            double cameraMax = ptrFrameRate->GetMax();
            frameRateSpinBox->setValue(ptrFrameRate->GetValue());
            // SpinBox 범위는 카메라 최대값과 300 중 큰 값을 사용
            frameRateSpinBox->setRange(cameraMin, std::max(cameraMax, 300.0));
            frameRateRangeLabel->setText(QString("카메라 범위: %1 - %2 fps (현재 설정 기준)")
                .arg(cameraMin, 0, 'f', 2)
                .arg(cameraMax, 0, 'f', 2));
        }
        
        // 블랙 레벨 읽기
        Spinnaker::GenApi::CFloatPtr ptrBlackLevel = nodeMap.GetNode("BlackLevel");
        if (Spinnaker::GenApi::IsReadable(ptrBlackLevel)) {
            blackLevelSpinBox->setValue(ptrBlackLevel->GetValue());
            blackLevelSpinBox->setRange(ptrBlackLevel->GetMin(), ptrBlackLevel->GetMax());
        }
        
        // 샤프니스 읽기
        Spinnaker::GenApi::CBooleanPtr ptrSharpnessEnable = nodeMap.GetNode("SharpeningEnable");
        if (Spinnaker::GenApi::IsReadable(ptrSharpnessEnable)) {
            sharpnessEnableCheckBox->setChecked(ptrSharpnessEnable->GetValue());
        }
        
        Spinnaker::GenApi::CFloatPtr ptrSharpness = nodeMap.GetNode("Sharpening");
        if (Spinnaker::GenApi::IsReadable(ptrSharpness)) {
            sharpnessSpinBox->setValue(ptrSharpness->GetValue());
            sharpnessSpinBox->setRange(ptrSharpness->GetMin(), ptrSharpness->GetMax());
        }
        
        // Acquisition Mode 읽기
        Spinnaker::GenApi::CEnumerationPtr ptrAcquisitionMode = nodeMap.GetNode("AcquisitionMode");
        if (Spinnaker::GenApi::IsReadable(ptrAcquisitionMode)) {
            QString acqMode = QString::fromStdString(ptrAcquisitionMode->GetCurrentEntry()->GetSymbolic().c_str());
            int index = acquisitionModeComboBox->findText(acqMode);
            if (index >= 0) {
                acquisitionModeComboBox->setCurrentIndex(index);
            }
        }
        
        // 트리거 설정 읽기
        Spinnaker::GenApi::CEnumerationPtr ptrTriggerMode = nodeMap.GetNode("TriggerMode");
        if (Spinnaker::GenApi::IsReadable(ptrTriggerMode)) {
            QString trigMode = QString::fromStdString(ptrTriggerMode->GetCurrentEntry()->GetSymbolic().c_str());
            int index = triggerModeComboBox->findText(trigMode);
            if (index >= 0) {
                triggerModeComboBox->setCurrentIndex(index);
            }
        }
        
        Spinnaker::GenApi::CEnumerationPtr ptrTriggerSource = nodeMap.GetNode("TriggerSource");
        if (Spinnaker::GenApi::IsReadable(ptrTriggerSource)) {
            QString trigSource = QString::fromStdString(ptrTriggerSource->GetCurrentEntry()->GetSymbolic().c_str());
            int index = triggerSourceComboBox->findText(trigSource);
            if (index >= 0) {
                triggerSourceComboBox->setCurrentIndex(index);
            }
        }
        
        // Auto 상태에 따라 수동 입력 활성화/비활성화
        onExposureAutoChanged(exposureAutoCheckBox->isChecked() ? Qt::Checked : Qt::Unchecked);
        onGainAutoChanged(gainAutoCheckBox->isChecked() ? Qt::Checked : Qt::Unchecked);
        
        qDebug() << "[CameraSettings] UI 업데이트 완료";
        
    } catch (Spinnaker::Exception& e) {
        qWarning() << "[CameraSettings] 카메라 설정 읽기 실패:" << e.what();
    }
#endif
}

void CameraSettingsDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void CameraSettingsDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void CameraSettingsDialog::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    }
}

 