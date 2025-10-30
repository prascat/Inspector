#include "FilterPropertyWidget.h"
#include <QDialog>
#include <QDebug>

FilterPropertyWidget::FilterPropertyWidget(int filterType, QWidget* parent)
    : QWidget(parent), filterType(filterType), adaptiveGroup(nullptr) {
    setupUI();
}

void FilterPropertyWidget::setFilterType(int type) {
    if (this->filterType != type) {
        this->filterType = type;
        
        // UI 재설정
        QLayoutItem* child;
        while ((child = mainLayout->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
        }
        
        setupUI();
    }
}

int FilterPropertyWidget::getFilterType() const {
    return filterType;
}

void FilterPropertyWidget::setupUI() {
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(5);
    
    formLayout = new QFormLayout();
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(5);
    
    mainLayout->addLayout(formLayout);
    
    switch (filterType) {
        case FILTER_THRESHOLD: setupThresholdUI(); break;
        case FILTER_BLUR: setupBlurUI(); break;
        case FILTER_CANNY: setupCannyUI(); break;
        case FILTER_SOBEL: setupSobelUI(); break;
        case FILTER_LAPLACIAN: setupLaplacianUI(); break;
        case FILTER_SHARPEN: setupSharpenUI(); break;
        case FILTER_BRIGHTNESS: setupBrightnessUI(); break;
        case FILTER_CONTRAST: setupContrastUI(); break;
        case FILTER_CONTOUR: setupContourUI(); break;
        case FILTER_MASK: setupMaskUI(); break;
        default: break;
    }
}

QSlider* FilterPropertyWidget::addSlider(const QString& name, const QString& labelText, int min, int max, int value, int step) {
    QSlider* slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(min, max);
    slider->setSingleStep(step);
    slider->setValue(value);
    slider->setProperty("paramName", name);
    
    QLabel* valueLabel = new QLabel(QString::number(value), this);
    valueLabel->setMinimumWidth(30);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    
    QHBoxLayout* layout = new QHBoxLayout();
    layout->addWidget(slider);
    layout->addWidget(valueLabel);
    
    formLayout->addRow(labelText + ":", layout);
    
    sliders[name] = slider;
    valueLabels[name] = valueLabel;
    
    connect(slider, &QSlider::valueChanged, this, &FilterPropertyWidget::handleSliderValueChanged);
    
    return slider;
}

QComboBox* FilterPropertyWidget::addComboBox(const QString& name, const QString& labelText) {
    QComboBox* combo = new QComboBox(this);
    combo->setProperty("paramName", name);
    
    formLayout->addRow(labelText + ":", combo);
    
    combos[name] = combo;
    
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &FilterPropertyWidget::handleComboIndexChanged);
    
    return combo;
}

void FilterPropertyWidget::setupThresholdUI() {
    // 이진화 타입 콤보박스
    addComboBox("thresholdType", "이진화 타입");
    QComboBox* threshTypeCombo = combos["thresholdType"];
    
    threshTypeCombo->blockSignals(true);
    threshTypeCombo->addItem("기본 이진화", cv::THRESH_BINARY);
    threshTypeCombo->addItem("역이진화", cv::THRESH_BINARY_INV);
    threshTypeCombo->addItem("절단", cv::THRESH_TRUNC);
    threshTypeCombo->addItem("Zero", cv::THRESH_TOZERO);
    threshTypeCombo->addItem("Zero Inv", cv::THRESH_TOZERO_INV);
    threshTypeCombo->blockSignals(false);
    
    // 기본 임계값 슬라이더
    addSlider("threshold", "임계값", 0, 255, 128);
}

void FilterPropertyWidget::setupBlurUI() {
    // 커널 크기 슬라이더
    addSlider("kernelSize", "커널 크기", 1, 31, 3, 2);
}

void FilterPropertyWidget::setupCannyUI() {
    // 임계값 1, 2 슬라이더
    addSlider("threshold1", "하한 임계값", 0, 255, 100);
    addSlider("threshold2", "상한 임계값", 0, 255, 200);
}

void FilterPropertyWidget::setupSobelUI() {
    // 소벨 커널 크기 슬라이더
    addSlider("sobelKernelSize", "커널 크기", 1, 7, 3, 2);
}

void FilterPropertyWidget::setupLaplacianUI() {
    // 라플라시안 커널 크기 슬라이더
    addSlider("laplacianKernelSize", "커널 크기", 1, 7, 3, 2);
}

void FilterPropertyWidget::setupSharpenUI() {
    // 선명하게 강도 슬라이더
    addSlider("sharpenStrength", "강도", 1, 10, 3);
}

void FilterPropertyWidget::setupBrightnessUI() {
    // 밝기 슬라이더
    addSlider("brightness", "밝기", -100, 100, 0);
}

void FilterPropertyWidget::setupContrastUI() {
    // 대비 슬라이더
    addSlider("contrast", "대비", -100, 100, 0);
}

void FilterPropertyWidget::setupContourUI() {
    // 임계값 슬라이더
    addSlider("threshold", "임계값", 0, 255, 128);
    
    // 최소 영역 슬라이더
    addSlider("minArea", "최소 영역", 10, 5000, 100);
    
    // 컨투어 검출 대상 콤보박스 (신규 추가)
    QComboBox* targetCombo = addComboBox("contourTarget", "검출 대상");
    targetCombo->addItem("밝은 물체", 0);   // THRESH_BINARY (기존 방식)
    targetCombo->addItem("어두운 물체", 1);  // THRESH_BINARY_INV (반전)
    
    // 모드 콤보박스
    QComboBox* modeCombo = addComboBox("contourMode", "모드");
    modeCombo->addItem("외곽선만", cv::RETR_EXTERNAL);
    modeCombo->addItem("모든 계층", cv::RETR_LIST);
    modeCombo->addItem("계층 구조", cv::RETR_CCOMP);
    modeCombo->addItem("트리 구조", cv::RETR_TREE);
    
    // 근사화 콤보박스
    QComboBox* approxCombo = addComboBox("contourApprox", "근사화");
    approxCombo->addItem("모든 점", cv::CHAIN_APPROX_NONE);
    approxCombo->addItem("점 압축", cv::CHAIN_APPROX_SIMPLE);
    approxCombo->addItem("정확하게", cv::CHAIN_APPROX_TC89_L1);
    approxCombo->addItem("느슨하게", cv::CHAIN_APPROX_TC89_KCOS);
}

void FilterPropertyWidget::setupMaskUI() {
    addSlider("maskValue", "마스크 값", 0, 255, 0);
}

void FilterPropertyWidget::handleSliderValueChanged(int value) {
    QSlider* slider = qobject_cast<QSlider*>(sender());
    if (slider) {
        QString paramName = slider->property("paramName").toString();
        
        // 특정 슬라이더는 항상 홀수 값을 유지
        if ((paramName == "kernelSize" || paramName == "sobelKernelSize" || 
             paramName == "laplacianKernelSize" || paramName == "blockSize") && value % 2 == 0) {
            value = value - 1; // 홀수로 조정
            slider->blockSignals(true);
            slider->setValue(value);
            slider->blockSignals(false);
        }
        
        // 값 라벨 업데이트
        if (valueLabels.contains(paramName)) {
            valueLabels[paramName]->setText(QString::number(value));
        }
        
        // 파라미터 변경 신호 발생
        emit paramChanged(paramName, value);
    }
}

void FilterPropertyWidget::handleComboIndexChanged(int index) {
    QComboBox* combo = qobject_cast<QComboBox*>(sender());
    if (combo) {
        QString paramName = combo->property("paramName").toString();
        int value = combo->itemData(index).toInt();
        
        // 이진화 타입 특별 처리
        if (paramName == "thresholdType") {
            int type = value;
            bool isAdaptive = (type == THRESH_ADAPTIVE_MEAN || type == THRESH_ADAPTIVE_GAUSSIAN);
            
            // 일반 임계값 활성화/비활성화
            if (sliders.contains("threshold")) {
                sliders["threshold"]->setEnabled(!isAdaptive);
                valueLabels["threshold"]->setEnabled(!isAdaptive);
            }
            
            // 적응형 파라미터 그룹 표시/숨김
            if (adaptiveGroup) {
                adaptiveGroup->setVisible(isAdaptive);
            }
        }
        // 파라미터 변경 신호 발생
        emit paramChanged(paramName, value);
    }
}

QMap<QString, int> FilterPropertyWidget::getParams() const {
    QMap<QString, int> params;
    
    // 슬라이더 값 수집
    for (auto it = sliders.begin(); it != sliders.end(); ++it) {
        QString paramName = it.key();
        QSlider* slider = it.value();
        params[paramName] = slider->value();
    }
    
    // 콤보박스 값 수집
    for (auto it = combos.begin(); it != combos.end(); ++it) {
        QString paramName = it.key();
        QComboBox* combo = it.value();
        params[paramName] = combo->currentData().toInt();
    }
    
    return params;
}

void FilterPropertyWidget::setParams(const QMap<QString, int>& params) {
    // 슬라이더 값 설정
    for (auto it = params.begin(); it != params.end(); ++it) {
        QString paramName = it.key();
        int value = it.value();
    
        if (sliders.contains(paramName)) {
            QSlider* slider = sliders[paramName];
            slider->blockSignals(true);
            slider->setValue(value);
            slider->blockSignals(false);
            
            // 값 라벨도 업데이트
            if (valueLabels.contains(paramName)) {
                valueLabels[paramName]->setText(QString::number(value));
            }
        }
    }
    
    // 콤보박스 값 설정
    for (auto it = params.begin(); it != params.end(); ++it) {
        QString paramName = it.key();
        int value = it.value();
        
        if (combos.contains(paramName)) {
            QComboBox* combo = combos[paramName];
            for (int i = 0; i < combo->count(); ++i) {
                if (combo->itemData(i).toInt() == value) {
                    combo->blockSignals(true);
                    combo->setCurrentIndex(i);
                    combo->blockSignals(false);
                    break;
                }
            }
        }
    }
    
    // 이진화 타입 특별 처리
    if (params.contains("thresholdType") && adaptiveGroup) {
        int thresholdType = params["thresholdType"];
        bool isAdaptive = (thresholdType == THRESH_ADAPTIVE_MEAN || thresholdType == THRESH_ADAPTIVE_GAUSSIAN);
        
        // 일반 임계값 활성화/비활성화
        if (sliders.contains("threshold")) {
            sliders["threshold"]->setEnabled(!isAdaptive);
            valueLabels["threshold"]->setEnabled(!isAdaptive);
        }
        
        // 적응형 파라미터 그룹 표시/숨김
        adaptiveGroup->setVisible(isAdaptive);
    }
}

int FilterPropertyWidget::getParamValue(const QString& paramName, int defaultValue) const {
    // 컨투어 타겟 특별 처리
    if (paramName == "contourTarget") {
        QComboBox* targetCombo = findChild<QComboBox*>("contourTarget");
        if (targetCombo) {
            return targetCombo->currentData().toInt();
        }
    }
    
    // 슬라이더에서 값 가져오기
    if (sliders.contains(paramName)) {
        return sliders[paramName]->value();
    }
    
    // 콤보박스에서 값 가져오기
    if (combos.contains(paramName)) {
        return combos[paramName]->currentData().toInt();
    }
    
    // 기본값 반환
    return defaultValue;
}

void FilterPropertyWidget::setEnabled(bool enabled) {
    // 모든 컨트롤 활성화/비활성화
    for (auto it = sliders.begin(); it != sliders.end(); ++it) {
        it.value()->setEnabled(enabled);
    }
    
    for (auto it = valueLabels.begin(); it != valueLabels.end(); ++it) {
        it.value()->setEnabled(enabled);
    }
    
    for (auto it = combos.begin(); it != combos.end(); ++it) {
        it.value()->setEnabled(enabled);
    }
    
    // 그룹박스도 활성화/비활성화 (적응형 이진화 그룹 등)
    QList<QGroupBox*> groups = findChildren<QGroupBox*>();
    for (QGroupBox* group : groups) {
        group->setEnabled(enabled);
    }
    
    // 위젯 자체의 활성화 상태 설정
    QWidget::setEnabled(enabled);
    
    // 활성화 상태 변경 신호 발생
    emit enableStateChanged(enabled);
}