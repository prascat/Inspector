#include "FilterDialog.h"
#include "TeachingWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QScrollArea>
#include <QLabel>
#include <QDebug>

FilterDialog::FilterDialog(CameraView* cameraView, int patternIndex, QWidget* parent)
    : QDialog(parent), cameraView(cameraView), patternIndex(-1)
{
    filterTypes = FILTER_TYPE_LIST;
    for (int filterType : filterTypes) {
        filterNames[filterType] = getFilterTypeName(filterType);
        defaultParams[filterType] = ImageProcessor::getDefaultParams(filterType);
    }

    setWindowTitle("필터 관리");
    setMinimumSize(700, 500);

    setupUI();
    setPatternIndex(patternIndex);
    
    // 부모 위젯 중앙에 배치
    if (parent) {
        move(parent->geometry().center() - rect().center());
    }
}

QUuid FilterDialog::getPatternId(int index) const {
    if (cameraView && index >= 0 && index < cameraView->getPatterns().size()) {
        return cameraView->getPatterns()[index].id;
    }
    return QUuid(); // 빈 UUID 반환 (유효하지 않은 인덱스)
}

void FilterDialog::setupUI() {
    QVBoxLayout* dialogLayout = new QVBoxLayout(this);
    
    // 패턴 정보 레이블 추가
    QLabel* patternInfoLabel = new QLabel("패턴 정보", this);
    patternInfoLabel->setObjectName("patternInfoLabel");
    patternInfoLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    dialogLayout->addWidget(patternInfoLabel);
    
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget* filtersWidget = new QWidget(scrollArea);
    createFilterControls(filtersWidget);

    scrollArea->setWidget(filtersWidget);
    dialogLayout->addWidget(scrollArea);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    QPushButton* cancelButton = new QPushButton("취소", this);
    QPushButton* applyButton = new QPushButton("적용", this);
    
    // 두 버튼의 스타일과 크기를 동일하게 설정
    cancelButton->setFixedSize(100, 30);
    applyButton->setFixedSize(100, 30);
    applyButton->setStyleSheet("background-color: #4CAF50; color: white;");
    cancelButton->setStyleSheet("background-color: white; color: black; border: 1px solid #CCCCCC;");
    
    connect(cancelButton, &QPushButton::clicked, this, &FilterDialog::onCancelClicked);
    connect(applyButton, &QPushButton::clicked, this, &FilterDialog::onApplyClicked);
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(applyButton);
    dialogLayout->addLayout(buttonLayout);
}

void FilterDialog::createFilterControls(QWidget* filtersWidget) {
    QGridLayout* filtersLayout = new QGridLayout(filtersWidget);
    const int columns = 2; // 한 줄에 2개씩 배치로 변경
    int row = 0, col = 0;
    
    // 제목 라벨
    QLabel* titleLabel = new QLabel("필터 선택", filtersWidget);
    titleLabel->setStyleSheet("font-weight: bold; font-size: 16px;");
    filtersLayout->addWidget(titleLabel, row, 0, 1, columns);
    row++;
    
    for (int filterType : filterTypes) {
        QGroupBox* groupBox = new QGroupBox(filterNames[filterType], filtersWidget);
        addFilterWidget(filterType, groupBox);
        filtersLayout->addWidget(groupBox, row, col);
        col++;
        if (col >= columns) { // 2개마다 행 변경
            col = 0;
            row++;
        }
    }
    
    // 여백 설정 - 필터 그룹박스 간 간격 조정
    filtersLayout->setSpacing(15);
    filtersLayout->setContentsMargins(10, 10, 10, 10);
}

void FilterDialog::addFilterWidget(int filterType, QGroupBox* groupBox) {
    QVBoxLayout* groupLayout = new QVBoxLayout(groupBox);

    // 필터 활성화 체크박스 생성
    QCheckBox* checkBox = new QCheckBox("활성화", groupBox);
    checkBox->setChecked(appliedFilters.contains(filterType) && appliedFilters[filterType].enabled);
    groupLayout->addWidget(checkBox);

    QFrame* line = new QFrame(groupBox);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    groupLayout->addWidget(line);

    // 필터 프로퍼티 위젯 생성
    FilterPropertyWidget* propertyWidget = new FilterPropertyWidget(filterType, groupBox);
    groupLayout->addWidget(propertyWidget);
    
    // 필터 타입에 따른 기본 파라미터 설정
    QMap<QString, int> params = defaultParams[filterType];
    if (appliedFilters.contains(filterType)) {
        // 이미 적용된 필터 파라미터 사용
        params = appliedFilters[filterType].params;
    }
    propertyWidget->setParams(params);
    
    // 활성화 상태에 따라 프로퍼티 위젯 활성화/비활성화
    propertyWidget->setEnabled(checkBox->isChecked());
    
    // 체크박스 상태 변경 시 프로퍼티 위젯 활성화/비활성화 및 필터 적용
    connect(checkBox, &QCheckBox::checkStateChanged, this, &FilterDialog::onFilterCheckStateChanged);
    checkBox->setProperty("filterType", filterType);
    
    // 파라미터 변경 시 필터 업데이트
    connect(propertyWidget, &FilterPropertyWidget::paramChanged, this, &FilterDialog::onFilterParamChanged);
    propertyWidget->setProperty("filterType", filterType);

    // 맵에 저장
    filterCheckboxes[filterType] = checkBox;
    filterWidgets[filterType] = propertyWidget;
}

void FilterDialog::onFilterCheckStateChanged(int state) {
    QCheckBox* checkBox = qobject_cast<QCheckBox*>(sender());
    if (!checkBox) return;
    
    int filterType = checkBox->property("filterType").toInt();
    bool checked = (state == Qt::Checked);
    
    // 필터 프로퍼티 위젯 활성화/비활성화
    if (filterWidgets.contains(filterType)) {
        filterWidgets[filterType]->setEnabled(checked);
    }
    
    // 패턴 ID가 설정되어 있는지 확인
    if (patternId.isNull()) {
        return;
    }
    
    // 이미 적용된 필터 확인
    int existingFilterIndex = -1;
    const QList<FilterInfo>& currentFilters = cameraView->getPatternFilters(patternId);
    for (int i = 0; i < currentFilters.size(); i++) {
        if (currentFilters[i].type == filterType) {
            existingFilterIndex = i;
            break;
        }
    }
    
    if (checked) {
        // 체크 시 필터 추가 또는 활성화
        if (existingFilterIndex >= 0) {
            // 기존 필터 활성화
            cameraView->setPatternFilterEnabled(patternId, existingFilterIndex, true);
            
            // 실시간 미리보기를 위해 필터 선택 상태 설정
            if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
                parentWidget->selectFilterForPreview(patternId, existingFilterIndex);
            }
        } else {
            // 새 필터 추가
            cameraView->addPatternFilter(patternId, filterType);
            int newFilterIndex = cameraView->getPatternFilters(patternId).size() - 1;
            
            // 현재 UI의 파라미터 값으로 설정
            QMap<QString, int> params = filterWidgets[filterType]->getParams();
            for (auto it = params.begin(); it != params.end(); ++it) {
                cameraView->setPatternFilterParam(patternId, newFilterIndex, it.key(), it.value());
            }
            
            // 실시간 미리보기를 위해 필터 선택 상태 설정
            if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
                parentWidget->selectFilterForPreview(patternId, newFilterIndex);
            }
            
            // FILTER_MASK 타입 필터가 추가된 경우 겹치는 INS 패턴들의 템플릿 이미지 갱신
            if (filterType == FILTER_MASK) {
                TeachingWidget* parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget());
                if (parentWidget) {
                    // 먼저 화면 갱신하여 filteredFrame에 새 마스크 값 적용
                    parentWidget->updateCameraFrame();
                    
                    // 필터 패턴의 정보 가져오기
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::FIL) {
                        // 현재 카메라의 UUID 가져오기
                        QString currentCameraUuid = pattern->cameraUuid;
                        
                        // 모든 INS 패턴 찾기
                        const QList<PatternInfo>& allPatterns = cameraView->getPatterns();
                        for (const PatternInfo& insPattern : allPatterns) {
                            // 현재 카메라에 있는 INS 패턴 중 마스크 영역과 겹치는 패턴만 처리
                            if (insPattern.type == PatternType::INS && 
                                insPattern.cameraUuid == currentCameraUuid &&
                                insPattern.rect.intersects(pattern->rect)) {
                                // 해당 INS 패턴의 템플릿 이미지 갱신
                                PatternInfo* insPatternPtr = cameraView->getPatternById(insPattern.id);
                                if (insPatternPtr) {
                                    parentWidget->updateInsTemplateImage(insPatternPtr, insPatternPtr->rect);
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        // 체크 해제 시 필터 비활성화
        if (existingFilterIndex >= 0) {
            cameraView->setPatternFilterEnabled(patternId, existingFilterIndex, false);
            
            // FILTER_MASK 타입 필터가 비활성화된 경우 겹치는 INS 패턴들의 템플릿 이미지 갱신
            if (filterType == FILTER_MASK) {
                TeachingWidget* parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget());
                if (parentWidget) {
                    // 먼저 화면 갱신하여 filteredFrame에 마스크 비활성화 적용
                    parentWidget->updateCameraFrame();
                    
                    // 필터 패턴의 정보 가져오기
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern && pattern->type == PatternType::FIL) {
                        // 현재 카메라의 UUID 가져오기
                        QString currentCameraUuid = pattern->cameraUuid;
                        
                        // 모든 INS 패턴 찾기
                        const QList<PatternInfo>& allPatterns = cameraView->getPatterns();
                        for (const PatternInfo& insPattern : allPatterns) {
                            // 현재 카메라에 있는 INS 패턴 중 마스크 영역과 겹치는 패턴만 처리
                            if (insPattern.type == PatternType::INS && 
                                insPattern.cameraUuid == currentCameraUuid &&
                                insPattern.rect.intersects(pattern->rect)) {
                                // 해당 INS 패턴의 템플릿 이미지 갱신
                                PatternInfo* insPatternPtr = cameraView->getPatternById(insPattern.id);
                                if (insPatternPtr) {
                                    parentWidget->updateInsTemplateImage(insPatternPtr, insPatternPtr->rect);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 컨투어 필터 체크 해제 시 윤곽선 지우기
    if (!checked && filterType == FILTER_CONTOUR) {
        // 윤곽선 지우기 (빈 컨투어 리스트 전달)
        cameraView->setPatternContours(patternId, QList<QVector<QPoint>>());
    }

    // 화면 갱신
    if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
        parentWidget->setFilterAdjusting(true);
        
        parentWidget->updateCameraFrame();
        
        // 필터 변경으로 인해 모든 패턴의 템플릿 이미지 실시간 갱신
        printf("[FilterDialog] Real-time template update after filter check change\n");
        parentWidget->updateAllPatternTemplateImages();
        
        parentWidget->setFilterAdjusting(false);
    }
}

void FilterDialog::onFilterParamChanged(const QString& paramName, int value) {
    FilterPropertyWidget* propertyWidget = qobject_cast<FilterPropertyWidget*>(sender());
    if (!propertyWidget) return;
    
    int filterType = propertyWidget->property("filterType").toInt();
    
    // 필터가 활성화되어 있는지 확인
    if (!filterCheckboxes[filterType]->isChecked()) return;
    
    updateFilterParam(filterType, paramName, value);
}

void FilterDialog::setPatternId(const QUuid& id) {
    
    appliedFilters.clear();
    this->patternId = id;
    
    // 유효한 패턴 정보 설정
    PatternInfo* pattern = cameraView->getPatternById(patternId);
    if (pattern) {
        QString patternInfo = QString("패턴: %1 (%2×%3)")
            .arg(pattern->name)
            .arg(pattern->rect.width())
            .arg(pattern->rect.height());
        setWindowTitle(QString("필터 추가 - %1").arg(pattern->name));
        
        
        // 기존 필터 목록 가져오기
        const QList<FilterInfo>& filters = pattern->filters;
        for (const FilterInfo& filter : filters) {
            appliedFilters[filter.type] = filter;
        }
        
        // 패턴 정보 라벨 업데이트
        QLabel* patternLabel = findChild<QLabel*>("patternInfoLabel");
        if (patternLabel) {
            patternLabel->setText(patternInfo);
        }
        
        // UI 업데이트
        updateUIFromFilters();
    } else {
        QLabel* patternLabel = findChild<QLabel*>("patternInfoLabel");
        if (patternLabel) {
            patternLabel->setText("유효하지 않은 패턴");
        }
    }
}

void FilterDialog::setPatternIndex(int index) {
    appliedFilters.clear();
    this->patternIndex = index;
    QLabel* patternLabel = findChild<QLabel*>("patternInfoLabel");
    bool isValid = false;
    QString patternInfo = "유효하지 않은 패턴";
    
    if (cameraView && index >= 0) {
        const QList<PatternInfo>& patterns = cameraView->getPatterns();
        if (index < patterns.size()) {
            isValid = true;
            const PatternInfo& pattern = patterns[index];
            patternInfo = QString("패턴 #%1: %2 (%3×%4)")
                .arg(index + 1)
                .arg(pattern.name)
                .arg(pattern.rect.width())
                .arg(pattern.rect.height());
            setWindowTitle(QString("필터 추가 - 패턴 #%1: %2").arg(index + 1).arg(pattern.name));
            
            // UUID로 필터 리스트 가져오기
            QUuid patternId = getPatternId(index);
            if (!patternId.isNull()) {
                this->patternId = patternId;  // ID 설정
                const QList<FilterInfo>& filters = cameraView->getPatternFilters(patternId);
                for (const FilterInfo& filter : filters) {
                    appliedFilters[filter.type] = filter;
                }
            }
        }
    }
    
    if (patternLabel) patternLabel->setText(patternInfo);
    updateUIFromFilters();
}

void FilterDialog::updateUIFromFilters() {
    // 모든 필터 체크박스 업데이트
    for (int filterType : filterTypes) {
        QCheckBox* checkbox = filterCheckboxes[filterType];
        if (!checkbox) continue;
        
        bool checked = appliedFilters.contains(filterType) && appliedFilters[filterType].enabled;
        checkbox->setChecked(checked);
        
        // 필터 프로퍼티 위젯 활성화/비활성화
        if (filterWidgets.contains(filterType)) {
            FilterPropertyWidget* propWidget = filterWidgets[filterType];
            propWidget->setEnabled(checked);
            
            // 파라미터 값 설정
            if (appliedFilters.contains(filterType)) {
                propWidget->setParams(appliedFilters[filterType].params);
            }
        }
    }
}

void FilterDialog::updateFilterParam(int filterType, const QString& paramName, int value) {
    if (!filterCheckboxes[filterType]->isChecked()) return;
    
    // 필터 조정 모드 시작 - 프로퍼티 패널 업데이트 방지
    if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
        parentWidget->setFilterAdjusting(true);
    }
    
    // 패턴 ID가 설정되어 있는지 확인
    if (patternId.isNull()) {
        printf("[FilterDialog] 패턴 ID가 null입니다.\n");
        fflush(stdout);
        return;
    }
    
    printf("[FilterDialog] 필터 파라미터 업데이트: %d %s %d\n", 
           filterType, paramName.toStdString().c_str(), value);
    fflush(stdout);
    
    // 현재 필터 목록 가져오기
    const QList<FilterInfo>& currentFilters = cameraView->getPatternFilters(patternId);

    int existingFilterIndex = -1;
    for (int i = 0; i < currentFilters.size(); i++) {
        if (currentFilters[i].type == filterType) {
            existingFilterIndex = i;
            break;
        }
    }
    
    if (existingFilterIndex >= 0) {
        // 기존 필터의 파라미터 업데이트
        cameraView->setPatternFilterParam(patternId, existingFilterIndex, paramName, value);
        
        // 실시간 미리보기를 위해 필터 선택 상태 설정
        if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
            parentWidget->selectFilterForPreview(patternId, existingFilterIndex);
        }
    } else {
        // 새로운 필터 추가 및 파라미터 설정
        cameraView->addPatternFilter(patternId, filterType);
        int newFilterIndex = cameraView->getPatternFilters(patternId).size() - 1;
        
        // 모든 파라미터 설정
        QMap<QString, int> params = filterWidgets[filterType]->getParams();
        for (auto it = params.begin(); it != params.end(); ++it) {
            cameraView->setPatternFilterParam(patternId, newFilterIndex, it.key(), it.value());
        }
        
        // 실시간 미리보기를 위해 필터 선택 상태 설정
        if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
            parentWidget->selectFilterForPreview(patternId, newFilterIndex);
        }
    }
    
    if (filterType == FILTER_CONTOUR) {
        PatternInfo* pattern = cameraView->getPatternById(patternId);
        if (pattern) {
            // 필터가 비활성화된 경우 윤곽선 지우기
            if (!filterCheckboxes[filterType]->isChecked()) {
                // 윤곽선 지우기 (빈 컨투어 리스트 전달)
                cameraView->setPatternContours(patternId, QList<QVector<QPoint>>());
            } else {
                // 필터가 활성화된 경우 윤곽선 추출 및 표시
                TeachingWidget* parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget());
                if (parentWidget) {
                    // getCurrentFrame() 대신 getCurrentFilteredFrame() 사용
                    cv::Mat filteredFrame = parentWidget->getCurrentFilteredFrame();
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
                            int threshold = filterWidgets[filterType]->getParamValue("threshold", 128);
                            int minArea = filterWidgets[filterType]->getParamValue("minArea", 100);
                            int contourMode = filterWidgets[filterType]->getParamValue("contourMode", cv::RETR_EXTERNAL);
                            int contourApprox = filterWidgets[filterType]->getParamValue("contourApprox", cv::CHAIN_APPROX_SIMPLE);
                            int contourTarget = filterWidgets[filterType]->getParamValue("contourTarget", 0);

                            // 윤곽선 정보만 추출
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
            }
        }
    }
    
    // 메인 화면 즉시 업데이트 - 실시간 미리보기
    printf("[FilterDialog] 실시간 화면 업데이트 시작\n");
    fflush(stdout);
    if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
        parentWidget->updateCameraFrame();
        printf("[FilterDialog] updateCameraFrame() 호출 완료\n");
        fflush(stdout);
        
        // 추가: 카메라뷰 강제 리페인트
        if (cameraView) {
            cameraView->update();
        }
        
        // 마스크 필터(FILTER_MASK)인 경우 영향받는 INS 패턴들의 템플릿도 모두 갱신
        if (filterType == FILTER_MASK && paramName == "maskValue") {
            
            // 먼저 화면 갱신 - 이렇게 하면 filteredFrame에 새 마스크 값이 적용됨
            parentWidget->updateCameraFrame(); 
            
            // 필터 패턴의 정보 가져오기
            PatternInfo* pattern = cameraView->getPatternById(patternId);
            if (pattern && pattern->type == PatternType::FIL) {
                // 현재 카메라의 UUID 가져오기
                QString currentCameraUuid = pattern->cameraUuid;
                
                // 모든 INS 패턴 찾기
                const QList<PatternInfo>& allPatterns = cameraView->getPatterns();
                for (const PatternInfo& insPattern : allPatterns) {
                    // 현재 카메라에 있는 INS 패턴 중 마스크 영역과 겹치는 패턴만 처리
                    if (insPattern.type == PatternType::INS && 
                        insPattern.cameraUuid == currentCameraUuid &&
                        insPattern.rect.intersects(pattern->rect)) {
                        // 해당 INS 패턴의 템플릿 이미지 갱신 (여기가 중요!)
                        PatternInfo* insPatternPtr = cameraView->getPatternById(insPattern.id);
                        if (insPatternPtr) {
                            parentWidget->updateInsTemplateImage(insPatternPtr, insPatternPtr->rect);
                        }
                    }
                }
            }
        }
        
        // 필터 파라미터 변경으로 인해 모든 패턴의 템플릿 이미지 실시간 갱신
        printf("[FilterDialog] Real-time template update after parameter change\n");
        parentWidget->updateAllPatternTemplateImages();
        
        // 필터 조정 모드 종료 - 프로퍼티 패널 업데이트 허용
        parentWidget->setFilterAdjusting(false);
    }
}

QMap<QString, int> FilterDialog::getFilterParams(int filterType) {
    if (filterWidgets.contains(filterType)) {
        return filterWidgets[filterType]->getParams();
    }
    return defaultParams[filterType]; // 기본값 반환
}

void FilterDialog::onCancelClicked() {
    // 취소 시 필터 원래대로 복구
    if (!patternId.isNull() && cameraView) {
        PatternInfo* pattern = cameraView->getPatternById(patternId);
        if (pattern) {
            // 모든 필터를 원래 상태로 복원
            // 먼저 현재 모든 필터 제거
            while (!pattern->filters.isEmpty()) {
                cameraView->removePatternFilter(patternId, 0);
            }
            
            // 원래 필터들 다시 추가
            for (auto it = appliedFilters.begin(); it != appliedFilters.end(); ++it) {
                int filterType = it.key();
                const FilterInfo& filter = it.value();
                
                cameraView->addPatternFilter(patternId, filterType);
                int newIndex = pattern->filters.size() - 1;
                
                // 파라미터 설정
                for (auto paramIt = filter.params.begin(); paramIt != filter.params.end(); ++paramIt) {
                    cameraView->setPatternFilterParam(patternId, newIndex, paramIt.key(), paramIt.value());
                }
                
                // 활성화 상태 설정
                cameraView->setPatternFilterEnabled(patternId, newIndex, filter.enabled);
            }

            // 화면 갱신
            if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
                // 필터 선택 상태 해제
                parentWidget->selectFilterForPreview(QUuid(), -1);
                
                parentWidget->updateCameraFrame();
                
                // 필터 상태가 복구되었으므로 모든 패턴의 템플릿 이미지 갱신
                printf("[FilterDialog] Updating all pattern template images after filter cancel\n");
                parentWidget->updateAllPatternTemplateImages();
            }
        }
    }
    
    reject();
}

void FilterDialog::onApplyClicked() {
    
    // 패턴 ID 확인
    if (patternId.isNull()) {
        return;
    }
    
    // 패턴 정보 가져오기
    PatternInfo* pattern = cameraView->getPatternById(patternId);
    if (pattern) {
        bool isFidPattern = (pattern->type == PatternType::FID);
        bool isInsPattern = (pattern->type == PatternType::INS);
        cv::Mat templateMat; // 템플릿 이미지 업데이트용
        
        // CONTOUR 필터가 체크되어 있는지 확인
        bool hasActiveContourFilter = filterCheckboxes[FILTER_CONTOUR]->isChecked();
        
        // FID 또는 INS 패턴인 경우, 필터 적용 전 ROI 영역의 원본 이미지 저장
        if (isFidPattern || isInsPattern) {
            // TeachingWidget에서 현재 프레임 가져오기
            TeachingWidget* parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget());
            if (parentWidget) {
                cv::Mat currentFrame = parentWidget->getCurrentFrame();
                if (!currentFrame.empty()) {
                    // ROI 영역 추출
                    cv::Rect roi(pattern->rect.x(), pattern->rect.y(), 
                                pattern->rect.width(), pattern->rect.height());
                    
                    if (roi.x >= 0 && roi.y >= 0 && 
                        roi.x + roi.width <= currentFrame.cols &&
                        roi.y + roi.height <= currentFrame.rows) {
                        
                        // 원본 영역 저장
                        templateMat = currentFrame(roi).clone();
                    }
                }
            }
        }
        
        // 기존 필터 모두 제거
        while (!pattern->filters.isEmpty()) {
            cameraView->removePatternFilter(patternId, 0);
        }
        
        // CONTOUR 필터가 활성화되지 않았다면 윤곽선 지우기
        if (!hasActiveContourFilter) {
            cameraView->setPatternContours(patternId, QList<QVector<QPoint>>());
        }
        
        // 체크된 필터만 추가
        QList<int> maskFilterIndices; // 새로 추가된 마스크 필터의 인덱스 저장
        
        for (int filterType : filterTypes) {
            QCheckBox* checkbox = filterCheckboxes[filterType];
            bool isChecked = checkbox->isChecked();
            
            if (isChecked) {
                
                // 필터 추가
                cameraView->addPatternFilter(patternId, filterType);
                int newFilterIndex = cameraView->getPatternFilters(patternId).size() - 1;
                
                // 파라미터 설정
                QMap<QString, int> params = filterWidgets[filterType]->getParams();
                for (auto it = params.begin(); it != params.end(); ++it) {
                    cameraView->setPatternFilterParam(patternId, newFilterIndex, it.key(), it.value());
                }
                
                // 필터 활성화
                cameraView->setPatternFilterEnabled(patternId, newFilterIndex, true);
                
                // 마스크 필터인 경우 인덱스 저장
                if (filterType == FILTER_MASK) {
                    maskFilterIndices.append(newFilterIndex);
                }
                
                // FID 또는 INS 패턴인 경우 템플릿 이미지에 필터 적용
                if ((isFidPattern || isInsPattern) && !templateMat.empty()) {
                    FilterInfo currentFilter;
                    currentFilter.type = filterType;
                    currentFilter.params = params;
                    currentFilter.enabled = true;
                    
                    // ImageProcessor 클래스의 메서드를 사용해 필터 적용
                    cv::Mat filteredMat;
                    ImageProcessor processor;
                    processor.applyFilter(templateMat, filteredMat, currentFilter);
                    
                    if (!filteredMat.empty()) {
                        templateMat = filteredMat;
                    }
                }
            }
        }
        
        // 마스크 필터가 추가된 경우, 영향받는 INS 패턴들의 템플릿 갱신
        if (!maskFilterIndices.isEmpty() && pattern->type == PatternType::FIL) {
            TeachingWidget* parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget());
            if (parentWidget) {
                // 먼저 화면 갱신
                parentWidget->updateCameraFrame();
                
                // 현재 카메라의 UUID 가져오기
                QString currentCameraUuid = pattern->cameraUuid;
                
                // 모든 INS 패턴 찾기
                const QList<PatternInfo>& allPatterns = cameraView->getPatterns();
                for (const PatternInfo& insPattern : allPatterns) {
                    // 현재 카메라에 있는 INS 패턴 중 마스크 영역과 겹치는 패턴만 처리
                    if (insPattern.type == PatternType::INS &&
                        insPattern.cameraUuid == currentCameraUuid &&
                        insPattern.rect.intersects(pattern->rect)) {
                        // 해당 INS 패턴의 템플릿 이미지 갱신
                        PatternInfo* insPatternPtr = cameraView->getPatternById(insPattern.id);
                        if (insPatternPtr) {
                            parentWidget->updateInsTemplateImage(insPatternPtr, insPatternPtr->rect);
                        }
                    }
                }
            }
        }
        
        // CONTOUR 필터가 활성화된 경우 윤곽선 업데이트
        if (hasActiveContourFilter) {
            TeachingWidget* parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget());
            if (parentWidget) {
                // 필터가 적용된 현재 프레임 가져오기
                cv::Mat filteredFrame = parentWidget->getCurrentFilteredFrame();
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
                        int threshold = filterWidgets[FILTER_CONTOUR]->getParamValue("threshold", 128);
                        int minArea = filterWidgets[FILTER_CONTOUR]->getParamValue("minArea", 100);
                        int contourMode = filterWidgets[FILTER_CONTOUR]->getParamValue("contourMode", cv::RETR_EXTERNAL);
                        int contourApprox = filterWidgets[FILTER_CONTOUR]->getParamValue("contourApprox", cv::CHAIN_APPROX_SIMPLE);
                        
                        // 윤곽선 정보만 추출
                        QList<QVector<QPoint>> contours = ImageProcessor::extractContours(
                            roiMat, threshold, minArea, contourMode, contourApprox);
                        
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
        }
        
        // FID 패턴인 경우 필터 적용된 템플릿 이미지 업데이트
        if (isFidPattern && !templateMat.empty()) {
            // BGR -> RGB 변환 (QImage 변환용)
            cv::cvtColor(templateMat, templateMat, cv::COLOR_BGR2RGB);
            
            // Mat을 QImage로 변환하여 템플릿 이미지 업데이트
            QImage filteredImage(templateMat.data, templateMat.cols, templateMat.rows,
                               templateMat.step, QImage::Format_RGB888);
            pattern->templateImage = filteredImage.copy();
            
            
            // 필요하다면 TeachingWidget의 UI도 업데이트
            if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
                parentWidget->updateFidTemplateImage(patternId);
            }
        }
        
        // INS 패턴인 경우 필터 적용된 템플릿 이미지 업데이트
        if (isInsPattern && !templateMat.empty()) {
            // BGR -> RGB 변환 (QImage 변환용)
            cv::cvtColor(templateMat, templateMat, cv::COLOR_BGR2RGB);
            
            // Mat을 QImage로 변환하여 템플릿 이미지 업데이트
            QImage filteredImage(templateMat.data, templateMat.cols, templateMat.rows,
                               templateMat.step, QImage::Format_RGB888);
            pattern->templateImage = filteredImage.copy();
            
            
            // 필요하다면 TeachingWidget의 UI도 업데이트
            if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
                parentWidget->updateInsTemplateImage(patternId);
            }
        }
    }
    
    // 부모 TeachingWidget에 업데이트 신호 보내기
    auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget());
    if (parentWidget) {
        // 필터 선택 상태 해제
        parentWidget->selectFilterForPreview(QUuid(), -1);
        
        // 패턴 트리 업데이트하여 필터 항목들을 추가
        parentWidget->updatePatternTree();
        // 화면 갱신
        parentWidget->updateCameraFrame();
        
        // 모든 패턴의 템플릿 이미지를 필터 적용 상태로 갱신
        printf("[FilterDialog] Updating all pattern template images after filter apply\n");
        parentWidget->updateAllPatternTemplateImages();
    }
    
    
    accept();
}