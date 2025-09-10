#include "CameraSettingsDialog.h"
#include <QDebug>

CameraSettingsDialog::CameraSettingsDialog(QVector<CameraInfo>& infos, QWidget* parent)
    : QDialog(parent), cameraInfos(infos), currentCameraIndex(0)
{
    setWindowTitle("카메라 레시피 관리");
    setMinimumSize(500, 300);
    
    setupUI();
}

void CameraSettingsDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // 카메라 선택 영역
    QGroupBox* sourceGroup = new QGroupBox("소스 카메라 (레시피 소스)", this);
    QVBoxLayout* sourceLayout = new QVBoxLayout(sourceGroup);
    
    sourceComboBox = new QComboBox(sourceGroup);
    for (int i = 0; i < cameraInfos.size(); i++) {
        sourceComboBox->addItem(QString("카메라 %1 (%2)").arg(i + 1).arg(cameraInfos[i].name), cameraInfos[i].uniqueId);
    }
    sourceLayout->addWidget(sourceComboBox);
    mainLayout->addWidget(sourceGroup);
    
    // 대상 카메라 선택 영역
    QGroupBox* targetGroup = new QGroupBox("대상 카메라 (레시피 적용 대상)", this);
    QVBoxLayout* targetLayout = new QVBoxLayout(targetGroup);
    
    targetComboBox = new QComboBox(targetGroup);
    for (int i = 0; i < cameraInfos.size(); i++) {
        targetComboBox->addItem(QString("카메라 %1 (%2)").arg(i + 1).arg(cameraInfos[i].name), cameraInfos[i].uniqueId);
    }
    if (cameraInfos.size() > 0) {
        targetComboBox->setCurrentIndex(0);
    }
    targetLayout->addWidget(targetComboBox);
    mainLayout->addWidget(targetGroup);
    
    // 레시피 관리 방법 선택
    QGroupBox* methodGroup = new QGroupBox("레시피 관리 방법", this);
    QVBoxLayout* methodLayout = new QVBoxLayout(methodGroup);
    
    copyRadio = new QRadioButton("복사: 소스 카메라의 레시피를 대상 카메라로 복사", methodGroup);
    swapRadio = new QRadioButton("교환: 두 카메라의 레시피를 서로 교환", methodGroup);
    moveRadio = new QRadioButton("이동: 소스 카메라의 레시피를 대상 카메라로 이동 (소스는 비움)", methodGroup);
    
    copyRadio->setChecked(true);  // 기본값은 복사
    
    methodLayout->addWidget(copyRadio);
    methodLayout->addWidget(swapRadio);
    methodLayout->addWidget(moveRadio);
    
    mainLayout->addWidget(methodGroup);
    
    // 하단 버튼 영역
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    QPushButton* closeButton = new QPushButton("닫기", this);
    applyButton = new QPushButton("적용", this);
    
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyButton, &QPushButton::clicked, this, &CameraSettingsDialog::onApplyClicked);
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeButton);
    buttonLayout->addWidget(applyButton);
    
    mainLayout->addLayout(buttonLayout);
}

CameraSettingsDialog::~CameraSettingsDialog() {
}

void CameraSettingsDialog::onApplyClicked() {
    int sourceIndex = sourceComboBox->currentIndex();
    int targetIndex = targetComboBox->currentIndex();
    
    QString sourceUuid = sourceComboBox->currentData().toString();
    QString targetUuid = targetComboBox->currentData().toString();
    
    // 동일한 카메라인 경우 경고
    if (sourceUuid == targetUuid) {
        QMessageBox::information(this, "동일한 카메라", "소스와 대상이 같은 카메라입니다. 변경 사항이 없습니다.");
        return;
    }
    
    // 확인 메시지
    QString methodStr;
    int methodCode = 0;  // 0: 복사, 1: 교환, 2: 이동
    
    if (copyRadio->isChecked()) {
        methodStr = "복사";
        methodCode = 0;
    } else if (swapRadio->isChecked()) {
        methodStr = "교환";
        methodCode = 1;
    } else if (moveRadio->isChecked()) {
        methodStr = "이동";
        methodCode = 2;
    }
    
    QString confirmMsg = QString("카메라 %1의 레시피를 카메라 %2로 %3하시겠습니까?")
                         .arg(sourceComboBox->currentText())
                         .arg(targetComboBox->currentText())
                         .arg(methodStr);
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, "레시피 관리 확인", 
                                                           confirmMsg,
                                                           QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        emit recipeReassigned(sourceIndex, targetIndex, methodCode, sourceUuid, targetUuid);
        
        QMessageBox::information(this, "레시피 관리 완료", 
                               QString("레시피가 성공적으로 %1되었습니다.").arg(methodStr));
        
        accept();
    }
}

void CameraSettingsDialog::setRecipeCamera(const QString& recipeCameraUuid) {
    // 레시피 카메라 UUID와 일치하는 카메라 찾기
    for (int i = 0; i < cameraInfos.size(); i++) {
        if (cameraInfos[i].uniqueId == recipeCameraUuid) {
            sourceComboBox->setCurrentIndex(i);
            return;
        }
    }
}