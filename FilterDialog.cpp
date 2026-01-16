#include "FilterDialog.h"
#include "TeachingWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QScrollArea>
#include <QLabel>
#include <QDebug>
#include <QMouseEvent>
#include <QApplication>
#include <QScreen>

FilterDialog::FilterDialog(CameraView* cameraView, int patternIndex, QWidget* parent)
    : QWidget(parent), cameraView(cameraView), patternIndex(-1), dragging(false)
{
    filterTypes = FILTER_TYPE_LIST;
    for (int filterType : filterTypes) {
        filterNames[filterType] = getFilterTypeName(filterType);
        defaultParams[filterType] = ImageProcessor::getDefaultParams(filterType);
    }

    setWindowTitle("필터 관리");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumSize(700, 500);

    setupUI();
    setPatternIndex(patternIndex);
    
    // 부모 위젯 또는 화면 중앙에 배치
    if (parent) {
        // 부모 위젯의 중앙에 배치
        QRect parentGeometry = parent->geometry();
        int x = parentGeometry.x() + (parentGeometry.width() - width()) / 2;
        int y = parentGeometry.y() + (parentGeometry.height() - height()) / 2;
        move(x, y);
    } else {
        // 부모가 없으면 화면 중앙에 배치
        QRect screenGeometry = QApplication::primaryScreen()->geometry();
        int x = (screenGeometry.width() - width()) / 2;
        int y = (screenGeometry.height() - height()) / 2;
        move(x, y);
    }
}

QUuid FilterDialog::getPatternId(int index) const {
    if (cameraView && index >= 0 && index < cameraView->getPatterns().size()) {
        return cameraView->getPatterns()[index].id;
    }
    return QUuid(); // 빈 UUID 반환 (유효하지 않은 인덱스)
}

void FilterDialog::setupUI() {
    // 투명 배경을 위한 메인 위젯
    QWidget* mainWidget = new QWidget(this);
    mainWidget->setObjectName("mainWidget");
    mainWidget->setStyleSheet(
        "QWidget#mainWidget { "
        "  background-color: rgba(30, 30, 30, 240); "
        "  border: 2px solid rgba(100, 100, 100, 200); "
        "  color: white; "
        "}"
    );
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(mainWidget);
    
    QVBoxLayout* dialogLayout = new QVBoxLayout(mainWidget);
    dialogLayout->setContentsMargins(10, 10, 10, 10);
    
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
    
    for (int filterType : filterTypes) {
        QGroupBox* groupBox = new QGroupBox(filtersWidget);
        groupBox->setTitle(filterNames[filterType] + " 활성화");
        groupBox->setCheckable(true);
        groupBox->setChecked(appliedFilters.contains(filterType) && appliedFilters[filterType].enabled);
        groupBox->setStyleSheet(
            "QGroupBox { font-weight: bold; color: white; background-color: transparent; border: 1px solid rgba(255,255,255,50); }"
            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px 0 5px; }"
            "QGroupBox::indicator { width: 13px; height: 13px; }"
            "QGroupBox::indicator:unchecked { background-color: rgba(50, 50, 50, 180); border: 1px solid rgba(100, 100, 100, 150); }"
            "QGroupBox::indicator:checked { background-color: #4CAF50; border: 1px solid #45a049; }"
        );
        
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
    groupLayout->setContentsMargins(10, 15, 10, 10);

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
    
    // 그룹박스 체크 상태 변경 시 필터 적용
    connect(groupBox, &QGroupBox::toggled, this, [this, filterType, propertyWidget, groupBox](bool checked) {
        qDebug() << "[필터 토글]" << filterType << "checked:" << checked;
        propertyWidget->setEnabled(checked);
        onFilterCheckStateChanged(checked ? Qt::Checked : Qt::Unchecked);
    });
    groupBox->setProperty("filterType", filterType);
    
    // 파라미터 변경 시 필터 업데이트
    connect(propertyWidget, &FilterPropertyWidget::paramChanged, this, &FilterDialog::onFilterParamChanged);
    propertyWidget->setProperty("filterType", filterType);

    // 맵에 저장 (groupBox를 체크박스처럼 사용)
    filterCheckboxes[filterType] = groupBox;  // GroupBox를 저장
    filterWidgets[filterType] = propertyWidget;
}

void FilterDialog::onFilterCheckStateChanged(int state) {
    QObject* senderObj = sender();
    int filterType = -1;
    bool checked = (state == Qt::Checked);
    
    // QCheckBox 또는 QGroupBox에서 호출될 수 있음
    if (QCheckBox* checkBox = qobject_cast<QCheckBox*>(senderObj)) {
        filterType = checkBox->property("filterType").toInt();
    } else if (QGroupBox* groupBox = qobject_cast<QGroupBox*>(senderObj)) {
        filterType = groupBox->property("filterType").toInt();
    } else {
        return;
    }
    
    qDebug() << "[onFilterCheckStateChanged] filterType:" << filterType << "checked:" << checked << "patternId:" << patternId;
    
    // 패턴 ID가 설정되어 있는지 확인
    if (patternId.isNull()) {
        qDebug() << "[onFilterCheckStateChanged] patternId is null, returning";
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
    
    qDebug() << "[onFilterCheckStateChanged] existingFilterIndex:" << existingFilterIndex;
    
    // 실제 사용할 필터 인덱스 (새로 추가되거나 기존 인덱스)
    int actualFilterIndex = existingFilterIndex;
    
    if (checked) {
        // 체크 시 필터 추가 또는 활성화
        if (existingFilterIndex >= 0) {
            // 기존 필터 활성화
            cameraView->setPatternFilterEnabled(patternId, existingFilterIndex, true);
            qDebug() << "[onFilterCheckStateChanged] 기존 필터 활성화:" << existingFilterIndex;
            actualFilterIndex = existingFilterIndex;
            
            // 실시간 미리보기를 위해 필터 선택 상태 설정
            if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
                parentWidget->selectFilterForPreview(patternId, existingFilterIndex);
            }
        } else {
            // 새 필터 추가
            cameraView->addPatternFilter(patternId, filterType);
            int newFilterIndex = cameraView->getPatternFilters(patternId).size() - 1;
            qDebug() << "[onFilterCheckStateChanged] 새 필터 추가:" << newFilterIndex;
            actualFilterIndex = newFilterIndex;
            
            // 현재 UI의 파라미터 값으로 설정
            QMap<QString, int> params = filterWidgets[filterType]->getParams();
            for (auto it = params.begin(); it != params.end(); ++it) {
                cameraView->setPatternFilterParam(patternId, newFilterIndex, it.key(), it.value());
            }
            
            // 실시간 미리보기를 위해 필터 선택 상태 설정
            if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
                parentWidget->selectFilterForPreview(patternId, newFilterIndex);
            }
            
            // 필터가 추가된 경우 INS 패턴의 템플릿 이미지 갱신 (모든 필터 타입)
            TeachingWidget* parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget());
            if (parentWidget) {
                // 먼저 화면 갱신하여 filteredFrame에 새 필터 값 적용
                parentWidget->updateCameraFrame();
                
                // 필터 패턴의 정보 가져오기
                PatternInfo* pattern = cameraView->getPatternById(patternId);
                if (pattern) {
                    // 현재 카메라의 UUID 가져오기
                    QString currentCameraUuid = pattern->cameraUuid;
                    
                    // FIL 패턴인 경우: 겹치는 INS 패턴들의 템플릿 갱신
                    if (pattern->type == PatternType::FIL) {
                        const QList<PatternInfo>& allPatterns = cameraView->getPatterns();
                        for (const PatternInfo& insPattern : allPatterns) {
                            if (insPattern.type == PatternType::INS && 
                                insPattern.cameraUuid == currentCameraUuid &&
                                insPattern.rect.intersects(pattern->rect)) {
                                PatternInfo* insPatternPtr = cameraView->getPatternById(insPattern.id);
                                if (insPatternPtr) {
                                    parentWidget->updateInsTemplateImage(insPatternPtr, insPatternPtr->rect);
                                }
                            }
                        }
                    }
                    // INS 패턴 자신의 필터인 경우: 자신의 템플릿 갱신
                    else if (pattern->type == PatternType::INS) {
                        parentWidget->updateInsTemplateImage(pattern, pattern->rect);
                    }
                }
            }
        }
    } else {
        // 체크 해제 시 필터 비활성화
        if (existingFilterIndex >= 0) {
            cameraView->setPatternFilterEnabled(patternId, existingFilterIndex, false);
            qDebug() << "[onFilterCheckStateChanged] 필터 비활성화:" << existingFilterIndex;
            
            // 필터가 비활성화된 경우 INS 패턴의 템플릿 이미지 갱신 (모든 필터 타입)
            TeachingWidget* parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget());
            if (parentWidget) {
                // 먼저 화면 갱신하여 filteredFrame에 필터 비활성화 적용
                parentWidget->updateCameraFrame();
                
                // 필터 패턴의 정보 가져오기
                PatternInfo* pattern = cameraView->getPatternById(patternId);
                if (pattern) {
                    // 현재 카메라의 UUID 가져오기
                    QString currentCameraUuid = pattern->cameraUuid;
                    
                    // FIL 패턴인 경우: 겹치는 INS 패턴들의 템플릿 갱신
                    if (pattern->type == PatternType::FIL) {
                        const QList<PatternInfo>& allPatterns = cameraView->getPatterns();
                        for (const PatternInfo& insPattern : allPatterns) {
                            if (insPattern.type == PatternType::INS && 
                                insPattern.cameraUuid == currentCameraUuid &&
                                insPattern.rect.intersects(pattern->rect)) {
                                PatternInfo* insPatternPtr = cameraView->getPatternById(insPattern.id);
                                if (insPatternPtr) {
                                    parentWidget->updateInsTemplateImage(insPatternPtr, insPatternPtr->rect);
                                }
                            }
                        }
                    }
                    // INS 패턴 자신의 필터인 경우: 자신의 템플릿 갱신
                    else if (pattern->type == PatternType::INS) {
                        parentWidget->updateInsTemplateImage(pattern, pattern->rect);
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

    // 패턴 목록에서 필터 클릭할 때와 동일한 로직 사용
    if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
        parentWidget->setFilterAdjusting(true);
        
        if (checked && actualFilterIndex >= 0) {
            // 체크한 필터를 미리보기 선택 상태로 설정
            parentWidget->selectFilterForPreview(patternId, actualFilterIndex);
            
            // 패턴 정보 가져오기
            PatternInfo *pattern = cameraView->getPatternById(patternId);
            if (pattern && actualFilterIndex < pattern->filters.size())
            {
                const FilterInfo &filter = pattern->filters[actualFilterIndex];
                
                // cameraView의 패턴 오버레이 그리기 비활성화
                if (cameraView)
                {
                    cameraView->clearSelectedInspectionPattern();
                    cameraView->setSelectedPatternId(QUuid());
                }
                
                int frameIndex = parentWidget->getCamOff() ? parentWidget->getCurrentDisplayFrameIndex() : parentWidget->getCameraIndex();
                const std::array<cv::Mat, 4>& frames = parentWidget->getCameraFrames();
                
                if (frameIndex >= 0 && frameIndex < 4 && !frames[frameIndex].empty())
                {
                    cv::Mat sourceFrame = frames[frameIndex].clone();
                    
                    // 회전이 있는 경우: 회전된 사각형 영역에만 필터 적용
                    if (std::abs(pattern->angle) > 0.1)
                    {
                        cv::Point2f center(pattern->rect.x() + pattern->rect.width() / 2.0f,
                                          pattern->rect.y() + pattern->rect.height() / 2.0f);

                        // 1. 회전된 사각형 마스크 생성
                        cv::Mat mask = cv::Mat::zeros(sourceFrame.size(), CV_8UC1);
                        cv::Size2f patternSize(pattern->rect.width(), pattern->rect.height());

                        cv::Point2f vertices[4];
                        cv::RotatedRect rotatedRect(center, patternSize, pattern->angle);
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
                        double width = pattern->rect.width();
                        double height = pattern->rect.height();

                        int rotatedWidth, rotatedHeight;
                        TeachingWidget::calculateRotatedBoundingBox(width, height, pattern->angle, rotatedWidth, rotatedHeight);

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
                            qBound(0, static_cast<int>(pattern->rect.x()), sourceFrame.cols - 1),
                            qBound(0, static_cast<int>(pattern->rect.y()), sourceFrame.rows - 1),
                            qBound(1, static_cast<int>(pattern->rect.width()), sourceFrame.cols - static_cast<int>(pattern->rect.x())),
                            qBound(1, static_cast<int>(pattern->rect.height()), sourceFrame.rows - static_cast<int>(pattern->rect.y())));

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
            }
        }
        else
        {
            // 체크 해제 시 원본 화면으로 복원
            parentWidget->updateCameraFrame();
        }
        
        parentWidget->setFilterAdjusting(false);
    }
}

void FilterDialog::onFilterParamChanged(const QString& paramName, int value) {
    printf("[FilterDialog::onFilterParamChanged] 호출됨! paramName=%s, value=%d\n", 
           paramName.toStdString().c_str(), value);
    fflush(stdout);
    
    FilterPropertyWidget* propertyWidget = qobject_cast<FilterPropertyWidget*>(sender());
    if (!propertyWidget) {
        printf("[FilterDialog::onFilterParamChanged] propertyWidget is null!\n");
        fflush(stdout);
        return;
    }
    
    int filterType = propertyWidget->property("filterType").toInt();
    printf("[FilterDialog::onFilterParamChanged] filterType=%d\n", filterType);
    fflush(stdout);
    
    // 필터가 활성화되어 있는지 확인
    QWidget* checkboxWidget = filterCheckboxes.value(filterType, nullptr);
    if (!checkboxWidget) {
        printf("[FilterDialog::onFilterParamChanged] checkboxWidget is null!\n");
        fflush(stdout);
        return;
    }
    
    bool isChecked = false;
    if (QGroupBox* groupBox = qobject_cast<QGroupBox*>(checkboxWidget)) {
        isChecked = groupBox->isChecked();
    } else if (QCheckBox* checkbox = qobject_cast<QCheckBox*>(checkboxWidget)) {
        isChecked = checkbox->isChecked();
    }
    
    printf("[FilterDialog::onFilterParamChanged] isChecked=%d, 이제 updateFilterParam 호출\n", isChecked);
    fflush(stdout);
    
    // 체크 여부와 관계없이 updateFilterParam 호출 (내부에서 처리)
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
        QWidget* checkboxWidget = filterCheckboxes[filterType];
        if (!checkboxWidget) {
            continue;
        }
        
        bool checked = appliedFilters.contains(filterType) && appliedFilters[filterType].enabled;
 
        // GroupBox인 경우
        if (QGroupBox* groupBox = qobject_cast<QGroupBox*>(checkboxWidget)) {
            groupBox->blockSignals(true);
            groupBox->setChecked(checked);
            groupBox->blockSignals(false);
        } else if (QCheckBox* checkbox = qobject_cast<QCheckBox*>(checkboxWidget)) {
            checkbox->blockSignals(true);
            checkbox->setChecked(checked);
            checkbox->blockSignals(false);
        }

        // 필터 프로퍼티 위젯: 항상 파라미터 값을 표시 (활성화 상태와 별개)
        if (filterWidgets.contains(filterType)) {
            FilterPropertyWidget* propWidget = filterWidgets[filterType];
            if (!propWidget) {
                continue;
            }
            
            // 파라미터 값 설정 (활성화 여부와 상관없이)
            if (appliedFilters.contains(filterType)) {
                propWidget->setParams(appliedFilters[filterType].params);
            }
            
            // 활성화 상태 설정 (UI 회색처리)
            propWidget->setEnabled(checked);
        }
    }
}

void FilterDialog::updateFilterParam(int filterType, const QString& paramName, int value) {
    printf("[FilterDialog::updateFilterParam] 호출됨! filterType=%d, paramName=%s, value=%d\n", 
           filterType, paramName.toStdString().c_str(), value);
    fflush(stdout);
    
    // 필터가 활성화되어 있는지 확인
    QWidget* checkboxWidget = filterCheckboxes.value(filterType, nullptr);
    if (!checkboxWidget) {
        printf("[FilterDialog::updateFilterParam] checkboxWidget is null!\n");
        fflush(stdout);
        return;
    }
    
    bool isChecked = false;
    if (QGroupBox* groupBox = qobject_cast<QGroupBox*>(checkboxWidget)) {
        isChecked = groupBox->isChecked();
    } else if (QCheckBox* checkbox = qobject_cast<QCheckBox*>(checkboxWidget)) {
        isChecked = checkbox->isChecked();
    }
    
    // 체크되지 않았을 때는 파라미터만 저장하고 미리보기 갱신하지 않음
    if (!isChecked) {
        // appliedFilters에 파라미터만 저장 (다음에 체크할 때 사용)
        if (!appliedFilters.contains(filterType)) {
            FilterInfo info;
            info.type = filterType;
            info.enabled = false;
            info.params = defaultParams[filterType];
            appliedFilters[filterType] = info;
        }
        appliedFilters[filterType].params[paramName] = value;
        return;
    }
    
    // 필터 조정 모드 시작 - 프로퍼티 패널 업데이트 방지
    if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
        parentWidget->setFilterAdjusting(true);
    }
    
    // 패턴 ID가 설정되어 있는지 확인
    if (patternId.isNull()) {
        return;
    }

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
        
        // 컨투어 필터 특별 처리
        if (filterType == FILTER_CONTOUR) {
            if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
                cv::Mat filteredFrame = parentWidget->getCurrentFilteredFrame();
                if (!filteredFrame.empty()) {
                    PatternInfo* pattern = cameraView->getPatternById(patternId);
                    if (pattern) {
                        cv::Rect roi(pattern->rect.x(), pattern->rect.y(),
                                   pattern->rect.width(), pattern->rect.height());
                        
                        if (roi.x >= 0 && roi.y >= 0 &&
                            roi.x + roi.width <= filteredFrame.cols &&
                            roi.y + roi.height <= filteredFrame.rows) {
                            
                            cv::Mat roiMat = filteredFrame(roi).clone();
                            
                            const QList<FilterInfo>& filters = cameraView->getPatternFilters(patternId);
                            if (existingFilterIndex < filters.size()) {
                                int threshold = filters[existingFilterIndex].params.value("threshold", 128);
                                int minArea = filters[existingFilterIndex].params.value("minArea", 100);
                                int contourMode = filters[existingFilterIndex].params.value("contourMode", cv::RETR_EXTERNAL);
                                int contourApprox = filters[existingFilterIndex].params.value("contourApprox", cv::CHAIN_APPROX_SIMPLE);
                                int contourTarget = filters[existingFilterIndex].params.value("contourTarget", 0);
                                
                                QList<QVector<QPoint>> contours = ImageProcessor::extractContours(
                                    roiMat, threshold, minArea, contourMode, contourApprox, contourTarget);
                                
                                for (QVector<QPoint>& contour : contours) {
                                    for (QPoint& pt : contour) {
                                        pt += QPoint(roi.x, roi.y);
                                    }
                                }
                                
                                cameraView->setPatternContours(patternId, contours);
                            }
                        }
                    }
                }
            }
        }
        
        // 화면 갱신
        cameraView->update();
        
        // FilterDialog에서도 필터 적용 결과를 직접 렌더링 (cam on/off 구분 없이)
        if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
            PatternInfo* pattern = cameraView->getPatternById(patternId);
            if (pattern && existingFilterIndex < pattern->filters.size()) {
                const FilterInfo& filter = pattern->filters[existingFilterIndex];
                
                cv::Mat sourceFrame = parentWidget->getCurrentFrame();
                if (!sourceFrame.empty()) {
                    sourceFrame = sourceFrame.clone();
                    
                    // 회전이 있는 경우: 회전된 사각형 영역에만 필터 적용
                    if (std::abs(pattern->angle) > 0.1) {
                        cv::Point2f center(pattern->rect.x() + pattern->rect.width() / 2.0f,
                                          pattern->rect.y() + pattern->rect.height() / 2.0f);

                        cv::Mat mask = cv::Mat::zeros(sourceFrame.size(), CV_8UC1);
                        cv::Size2f patternSize(pattern->rect.width(), pattern->rect.height());

                        cv::Point2f vertices[4];
                        cv::RotatedRect rotatedRect(center, patternSize, pattern->angle);
                        rotatedRect.points(vertices);

                        std::vector<cv::Point> points;
                        for (int i = 0; i < 4; i++) {
                            points.push_back(cv::Point(static_cast<int>(std::round(vertices[i].x)),
                                                      static_cast<int>(std::round(vertices[i].y))));
                        }
                        cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{points}, cv::Scalar(255));

                        cv::Mat maskedImage = cv::Mat::zeros(sourceFrame.size(), sourceFrame.type());
                        sourceFrame.copyTo(maskedImage, mask);

                        double width = pattern->rect.width();
                        double height = pattern->rect.height();

                        int rotatedWidth, rotatedHeight;
                        parentWidget->calculateRotatedBoundingBox(width, height, pattern->angle, rotatedWidth, rotatedHeight);

                        int maxSize = std::max(rotatedWidth, rotatedHeight);
                        int halfSize = maxSize / 2;

                        cv::Rect expandedRoi(
                            qBound(0, static_cast<int>(center.x) - halfSize, sourceFrame.cols - 1),
                            qBound(0, static_cast<int>(center.y) - halfSize, sourceFrame.rows - 1),
                            qBound(1, maxSize, sourceFrame.cols - (static_cast<int>(center.x) - halfSize)),
                            qBound(1, maxSize, sourceFrame.rows - (static_cast<int>(center.y) - halfSize)));

                        if (expandedRoi.width > 0 && expandedRoi.height > 0 &&
                            expandedRoi.x + expandedRoi.width <= maskedImage.cols &&
                            expandedRoi.y + expandedRoi.height <= maskedImage.rows) {
                            cv::Mat roiMat = maskedImage(expandedRoi);
                            ImageProcessor processor;
                            cv::Mat filteredRoi;
                            processor.applyFilter(roiMat, filteredRoi, filter);
                            if (!filteredRoi.empty()) {
                                filteredRoi.copyTo(roiMat);
                            }
                        }

                        maskedImage.copyTo(sourceFrame, mask);
                    }
                    else {
                        // 회전이 없는 경우
                        cv::Rect roi(
                            qBound(0, static_cast<int>(pattern->rect.x()), sourceFrame.cols - 1),
                            qBound(0, static_cast<int>(pattern->rect.y()), sourceFrame.rows - 1),
                            qBound(1, static_cast<int>(pattern->rect.width()), sourceFrame.cols - static_cast<int>(pattern->rect.x())),
                            qBound(1, static_cast<int>(pattern->rect.height()), sourceFrame.rows - static_cast<int>(pattern->rect.y())));

                        if (roi.width > 0 && roi.height > 0) {
                            cv::Mat roiMat = sourceFrame(roi);
                            ImageProcessor processor;
                            cv::Mat filteredRoi;
                            processor.applyFilter(roiMat, filteredRoi, filter);
                            if (!filteredRoi.empty()) {
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
    } else {
        // 새로운 필터 추가 및 파라미터 설정
        cameraView->addPatternFilter(patternId, filterType);
        int newFilterIndex = cameraView->getPatternFilters(patternId).size() - 1;
        
        // 모든 파라미터 설정
        QMap<QString, int> params = filterWidgets[filterType]->getParams();
        for (auto it = params.begin(); it != params.end(); ++it) {
            cameraView->setPatternFilterParam(patternId, newFilterIndex, it.key(), it.value());
        }
        
        // 화면 갱신
        cameraView->update();
        
        if (auto parentWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
            parentWidget->updateCameraFrame();
        }
    }
    
    // 컨투어 필터 중복 처리 제거 (위에서 이미 처리됨)
    
    // 필터 조정 완료
    if (auto teachingWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
        teachingWidget->setFilterAdjusting(false);
    }
    
    // 마스크 필터인 경우 INS 패턴 템플릿 갱신
    if (filterType == FILTER_MASK && paramName == "maskValue") {
        if (auto teachingWidget = qobject_cast<TeachingWidget*>(this->parentWidget())) {
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
                            teachingWidget->updateInsTemplateImage(insPatternPtr, insPatternPtr->rect);
                        }
                    }
                }
            }
        }
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
            }
        }
    }
    
    close();
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
        QWidget* contourWidget = filterCheckboxes.value(FILTER_CONTOUR, nullptr);
        bool hasActiveContourFilter = false;
        if (QGroupBox* gb = qobject_cast<QGroupBox*>(contourWidget)) {
            hasActiveContourFilter = gb->isChecked();
        } else if (QCheckBox* cb = qobject_cast<QCheckBox*>(contourWidget)) {
            hasActiveContourFilter = cb->isChecked();
        }
        
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
            QWidget* checkboxWidget = filterCheckboxes[filterType];
            bool isChecked = false;
            if (QGroupBox* gb = qobject_cast<QGroupBox*>(checkboxWidget)) {
                isChecked = gb->isChecked();
            } else if (QCheckBox* cb = qobject_cast<QCheckBox*>(checkboxWidget)) {
                isChecked = cb->isChecked();
            }
            
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
    }
    
    close();
}

void FilterDialog::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging = true;
        dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
    QWidget::mousePressEvent(event);
}

void FilterDialog::mouseMoveEvent(QMouseEvent* event) {
    if (dragging && (event->buttons() & Qt::LeftButton)) {
        QPoint newPos = event->globalPosition().toPoint() - dragPosition;
        move(newPos);
        event->accept();
    }
    QWidget::mouseMoveEvent(event);
}

void FilterDialog::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void FilterDialog::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    
    // 첫 표시 시에만 중앙에 배치
    static bool firstShow = true;
    if (firstShow) {
        firstShow = false;
        
        QWidget* parent = parentWidget();
        if (parent && parent->isVisible()) {
            // 부모 위젯의 중앙에 배치
            QRect parentGeometry = parent->geometry();
            int x = parentGeometry.x() + (parentGeometry.width() - width()) / 2;
            int y = parentGeometry.y() + (parentGeometry.height() - height()) / 2;
            move(x, y);
        } else {
            // 부모가 없거나 보이지 않으면 화면 중앙에 배치
            QRect screenGeometry = QApplication::primaryScreen()->geometry();
            int x = (screenGeometry.width() - width()) / 2;
            int y = (screenGeometry.height() - height()) / 2;
            move(x, y);
        }
    }
}