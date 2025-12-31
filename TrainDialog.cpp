#include "TrainDialog.h"
#include <QGroupBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QApplication>
#include <QCoreApplication>
#include <QTimer>
#include <QDateTime>
#include <QScrollBar>
#include <QFormLayout>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include "CustomFileDialog.h"
#include "CustomMessageBox.h"

TrainDialog::TrainDialog(QWidget *parent)
    : QWidget(parent)
    , m_dragging(false)
    , m_firstShow(true)
    , dockerTrainProcess(nullptr)
    , trainingTimer(nullptr)
    , totalTrainingTimer(nullptr)
    , progressUpdateTimer(nullptr)
    , isTraining(false)
    , trainingOverlay(nullptr)
    , trainingProgressBar(nullptr)
    , trainingStatusLabel(nullptr)
    , totalPatternCount(0)
    , completedPatternCount(0)
{
    setupUI();
    applyBlackTheme();
    
    // 진행률 시간 갱신 타이머 설정 (1초마다)
    progressUpdateTimer = new QTimer(this);
    connect(progressUpdateTimer, &QTimer::timeout, this, [this]() {
        if (isTraining && trainingStatusLabel && !currentProgressMessage.isEmpty()) {
            trainingStatusLabel->setText(currentProgressMessage + getTotalTimeString());
        }
    });
    
    // 화면 밖으로도 이동 가능하도록 설정 (윈도우 매니저 우회)
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint);
    resize(1400, 900);
}

TrainDialog::~TrainDialog()
{
    if (dockerTrainProcess) {
        if (dockerTrainProcess->state() == QProcess::Running) {
            dockerTrainProcess->kill();
            dockerTrainProcess->waitForFinished(3000);
        }
        delete dockerTrainProcess;
        dockerTrainProcess = nullptr;
    }
    
    if (trainingTimer) {
        delete trainingTimer;
        trainingTimer = nullptr;
    }
    
    if (totalTrainingTimer) {
        delete totalTrainingTimer;
        totalTrainingTimer = nullptr;
    }
    
    if (progressUpdateTimer) {
        progressUpdateTimer->stop();
        delete progressUpdateTimer;
        progressUpdateTimer = nullptr;
    }
}

void TrainDialog::setupUI()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // ===== 좌측: 버튼 + 모드 선택 + 패턴 목록 + 티칭 이미지 =====
    QWidget *leftWidget = new QWidget(this);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);
    
    // 모델 옵션 UI
    QGroupBox *optionsGroupBox = new QGroupBox("모델 옵션", this);
    QFormLayout *optionsLayout = new QFormLayout(optionsGroupBox);
    optionsLayout->setSpacing(8);
    
    // Backbone 선택
    backboneComboBox = new QComboBox(this);
    backboneComboBox->addItem("resnet18");
    backboneComboBox->addItem("resnet50");
    backboneComboBox->addItem("wide_resnet50_2");
    backboneComboBox->setCurrentIndex(2);  // 기본값: wide_resnet50_2
    optionsLayout->addRow("Backbone:", backboneComboBox);
    
    // Coreset Ratio
    coresetRatioSpinBox = new QDoubleSpinBox(this);
    coresetRatioSpinBox->setRange(0.0, 1.0);
    coresetRatioSpinBox->setSingleStep(0.01);
    coresetRatioSpinBox->setDecimals(2);
    coresetRatioSpinBox->setValue(0.01);  // 기본값: 0.01
    optionsLayout->addRow("Coreset Ratio:", coresetRatioSpinBox);
    
    // Num Neighbors
    numNeighborsSpinBox = new QSpinBox(this);
    numNeighborsSpinBox->setRange(1, 50);
    numNeighborsSpinBox->setValue(9);  // 기본값: 9
    optionsLayout->addRow("Num Neighbors:", numNeighborsSpinBox);
    
    // ReBuild Docker 버튼
    rebuildDockerButton = new QPushButton("🔄 Docker 이미지 재빌드", this);
    rebuildDockerButton->setMinimumHeight(35);
    rebuildDockerButton->setStyleSheet(
        "QPushButton { background-color: #f57c00; color: #ffffff; border: 1px solid #e65100; }"
        "QPushButton:hover { background-color: #fb8c00; }"
        "QPushButton:pressed { background-color: #e65100; }"
    );
    optionsLayout->addRow(rebuildDockerButton);
    
    // Docker 이미지 정보 표시 라벨
    dockerImageInfoLabel = new QLabel("Docker 이미지 확인 중...", this);
    dockerImageInfoLabel->setWordWrap(true);
    dockerImageInfoLabel->setStyleSheet(
        "QLabel { color: #aaaaaa; font-size: 11px; padding: 5px; "
        "background-color: #2a2a2a; border: 1px solid #3d3d3d; border-radius: 3px; }"
    );
    dockerImageInfoLabel->setMinimumHeight(60);
    optionsLayout->addRow(dockerImageInfoLabel);
    
    leftLayout->addWidget(optionsGroupBox);
    
    // 버튼들 (맨 위에 배치)
    addImagesButton = new QPushButton("이미지 추가", this);
    addImagesButton->setMinimumHeight(35);
    leftLayout->addWidget(addImagesButton);
    
    deleteSelectedImageButton = new QPushButton("이미지 삭제", this);
    deleteSelectedImageButton->setMinimumHeight(35);
    deleteSelectedImageButton->setEnabled(false);
    leftLayout->addWidget(deleteSelectedImageButton);
    
    clearImagesButton = new QPushButton("전체 이미지 삭제", this);
    clearImagesButton->setMinimumHeight(35);
    leftLayout->addWidget(clearImagesButton);
    
    autoTrainButton = new QPushButton("체크된 패턴 학습", this);
    autoTrainButton->setMinimumHeight(40);
    autoTrainButton->setEnabled(false);
    autoTrainButton->setStyleSheet(
        "QPushButton { background-color: #2e7d32; color: #ffffff; border: 1px solid #1b5e20; }"
        "QPushButton:hover { background-color: #388e3c; }"
        "QPushButton:pressed { background-color: #1b5e20; }"
        "QPushButton:disabled { background-color: #555555; color: #999999; }"
    );
    leftLayout->addWidget(autoTrainButton);
    
    closeButton = new QPushButton("닫기", this);
    closeButton->setMinimumHeight(40);
    closeButton->setStyleSheet(
        "QPushButton { background-color: #c62828; color: #ffffff; border: 1px solid #b71c1c; }"
        "QPushButton:hover { background-color: #d32f2f; }"
        "QPushButton:pressed { background-color: #b71c1c; }"
    );
    leftLayout->addWidget(closeButton);
    
    // 패턴 목록
    patternListWidget = new QListWidget(this);
    leftLayout->addWidget(patternListWidget);
    
    // 티칭 이미지
    teachingImageLabel = new QLabel(this);
    teachingImageLabel->setAlignment(Qt::AlignCenter);
    teachingImageLabel->setMinimumSize(280, 280);
    teachingImageLabel->setStyleSheet("QLabel { border: 1px solid #3d3d3d; background-color: #252525; }");
    teachingImageLabel->setText("패턴을 선택하세요");
    leftLayout->addWidget(teachingImageLabel);
    
    leftWidget->setMaximumWidth(350);
    mainLayout->addWidget(leftWidget);

    // ===== 우측: 상단 썸네일 + 중앙 큰 이미지 =====
    QWidget *rightWidget = new QWidget(this);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);
    
    // 상단: 수집된 학습 이미지 (1줄 가로 스크롤)
    QWidget *imageWidget = new QWidget(this);
    QVBoxLayout *imageWidgetLayout = new QVBoxLayout(imageWidget);
    imageWidgetLayout->setContentsMargins(0, 0, 0, 0);
    imageWidgetLayout->setSpacing(5);
    
    imageCountLabel = new QLabel("이미지 개수: 0", this);
    imageCountLabel->setAlignment(Qt::AlignCenter);
    QFont countFont = imageCountLabel->font();
    countFont.setPointSize(11);
    countFont.setBold(true);
    imageCountLabel->setFont(countFont);
    imageWidgetLayout->addWidget(imageCountLabel);
    
    imageListWidget = new QListWidget(this);
    imageListWidget->setViewMode(QListWidget::IconMode);
    imageListWidget->setIconSize(QSize(120, 120));
    imageListWidget->setResizeMode(QListWidget::Adjust);
    imageListWidget->setMovement(QListWidget::Static);
    imageListWidget->setFlow(QListWidget::LeftToRight);
    imageListWidget->setWrapping(false);
    imageListWidget->setFixedHeight(150);
    imageWidgetLayout->addWidget(imageListWidget);
    
    imageWidget->setMaximumHeight(190);
    rightLayout->addWidget(imageWidget);
    
    // 중앙: 선택된 이미지 크게 표시
    previewImageLabel = new QLabel(this);
    previewImageLabel->setAlignment(Qt::AlignCenter);
    previewImageLabel->setMinimumSize(600, 400);
    previewImageLabel->setStyleSheet("QLabel { border: 1px solid #3d3d3d; background-color: #252525; }");
    previewImageLabel->setText("이미지를 클릭하세요");
    rightLayout->addWidget(previewImageLabel);
    
    mainLayout->addWidget(rightWidget);

    // 시그널 연결
    if (autoTrainButton) connect(autoTrainButton, &QPushButton::clicked, this, &TrainDialog::onStartAutoTrainClicked);
    if (closeButton) connect(closeButton, &QPushButton::clicked, this, &TrainDialog::onCloseClicked);
    if (clearImagesButton) connect(clearImagesButton, &QPushButton::clicked, this, &TrainDialog::onClearImagesClicked);
    if (addImagesButton) connect(addImagesButton, &QPushButton::clicked, this, &TrainDialog::onAddImagesClicked);
    if (deleteSelectedImageButton) connect(deleteSelectedImageButton, &QPushButton::clicked, this, &TrainDialog::onDeleteSelectedImageClicked);
    if (patternListWidget) connect(patternListWidget, &QListWidget::itemSelectionChanged, this, &TrainDialog::onPatternSelectionChanged);
    if (imageListWidget) connect(imageListWidget, &QListWidget::itemClicked, this, &TrainDialog::onImageItemClicked);
    if (rebuildDockerButton) connect(rebuildDockerButton, &QPushButton::clicked, this, &TrainDialog::onRebuildDockerClicked);
}

void TrainDialog::applyBlackTheme()
{
    setStyleSheet(
        "QWidget { background-color: #1e1e1e; color: #ffffff; }"
        "QLabel { color: #ffffff; }"
        "QListWidget { background-color: #252525; color: #ffffff; border: 1px solid #3d3d3d; }"
        "QListWidget::item { padding: 8px; }"
        "QListWidget::item:hover { background-color: #3d3d3d; }"
        "QListWidget::item:selected { background-color: #0d47a1; }"
        "QRadioButton { color: #ffffff; spacing: 5px; }"
        "QRadioButton::indicator { width: 18px; height: 18px; border-radius: 9px; border: 2px solid #3d3d3d; background-color: #252525; }"
        "QRadioButton::indicator:checked { background-color: #0d47a1; border-color: #0d47a1; }"
        "QCheckBox { color: #ffffff; spacing: 5px; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border: 2px solid #3d3d3d; background-color: #252525; }"
        "QCheckBox::indicator:checked { background-color: #0d47a1; border-color: #0d47a1; }"
        "QScrollArea { background-color: #252525; border: 1px solid #3d3d3d; }"
        "QPushButton { background-color: #2d2d2d; color: #ffffff; border: 1px solid #3d3d3d; padding: 8px; }"
        "QPushButton:hover { background-color: #3d3d3d; }"
        "QPushButton:pressed { background-color: #4d4d4d; }"
        "QPushButton:disabled { background-color: #1a1a1a; color: #666666; }"
    );
}

void TrainDialog::setAnomalyPatterns(const QVector<PatternInfo*>& patterns)
{
    anomalyPatterns = patterns;
    
    // 리스트 초기화
    patternListWidget->clear();
    patternCheckBoxes.clear();
    
    // ANOMALY 검사방법 패턴만 추가
    for (PatternInfo* pattern : patterns) {
        if (pattern && pattern->type == PatternType::INS &&
            pattern->inspectionMethod == InspectionMethod::ANOMALY) {
            
            // 커스텀 위젯 생성 (체크박스 + 패턴 정보)
            QWidget *itemWidget = new QWidget();
            itemWidget->setStyleSheet("background-color: transparent;");
            QHBoxLayout *itemLayout = new QHBoxLayout(itemWidget);
            itemLayout->setContentsMargins(5, 2, 5, 2);
            itemLayout->setSpacing(8);
            
            QCheckBox *checkBox = new QCheckBox();
            checkBox->setStyleSheet(
                "QCheckBox::indicator { width: 18px; height: 18px; border: 2px solid #888; background-color: #222; border-radius: 3px; }"
                "QCheckBox::indicator:checked { background-color: #4CAF50; border-color: #4CAF50; }"
                "QCheckBox::indicator:hover { border-color: #aaa; }"
            );
            
            // 학습 가중치 파일 확인
            bool isTrained = AnomalyWeightUtils::hasTrainedWeight(pattern->name);
            
            QString labelText = QString("%1 (ROI: %2x%3)")
                .arg(pattern->name)
                .arg(static_cast<int>(pattern->rect.width()))
                .arg(static_cast<int>(pattern->rect.height()));
            
            if (isTrained) {
                // 메타데이터에서 모델 옵션 읽기
                QString metadataPath = QCoreApplication::applicationDirPath() + 
                                      QString("/../deploy/weights/%1/metadata.json").arg(pattern->name);
                QFile metaFile(metadataPath);
                QString modelInfo;
                
                if (metaFile.exists() && metaFile.open(QIODevice::ReadOnly)) {
                    QByteArray data = metaFile.readAll();
                    metaFile.close();
                    
                    QJsonDocument doc = QJsonDocument::fromJson(data);
                    if (!doc.isNull() && doc.isObject()) {
                        QJsonObject obj = doc.object();
                        QString backbone = obj.value("backbone").toString("unknown");
                        double coresetRatio = obj.value("coreset_ratio").toDouble(-1.0);
                        int numNeighbors = obj.value("num_neighbors").toInt(-1);
                        
                        modelInfo = QString(" [%1, CR:% 2, NN:%3]")
                            .arg(backbone)
                            .arg(coresetRatio, 0, 'f', 3)
                            .arg(numNeighbors);
                    } else {
                        modelInfo = " [Trained]";
                    }
                } else {
                    modelInfo = " [Trained]";
                }
                
                labelText += modelInfo;
            }
            
            QLabel *label = new QLabel(labelText);
            label->setStyleSheet(isTrained ? 
                "color: #f44336; background: transparent; font-weight: bold;" :
                "color: #ffffff; background: transparent;");
            
            itemLayout->addWidget(checkBox);
            itemLayout->addWidget(label, 1);  // stretch factor 1로 남은 공간 차지
            
            // 리스트 아이템 생성 (텍스트 비우고 위젯만 사용)
            QListWidgetItem *item = new QListWidgetItem();
            item->setSizeHint(QSize(0, 35));  // 높이 지정
            item->setData(Qt::UserRole, pattern->name);
            item->setData(Qt::UserRole + 1, isTrained);  // 학습 여부 저장
            
            patternListWidget->addItem(item);
            patternListWidget->setItemWidget(item, itemWidget);
            
            // 체크박스 저장
            patternCheckBoxes[pattern->name] = checkBox;
            
            // 체크박스 변경 시 자동 학습 버튼 활성화 상태 업데이트
            connect(checkBox, &QCheckBox::stateChanged, [this](int) {
                bool anyChecked = false;
                for (auto checkbox : patternCheckBoxes) {
                    if (checkbox->isChecked()) {
                        anyChecked = true;
                        break;
                    }
                }
                
                // 공용 이미지가 있는지 확인
                bool hasImages = !commonImages.isEmpty();
                
                autoTrainButton->setEnabled(anyChecked && hasImages);
            });
        }
    }
    
    if (patternListWidget->count() == 0) {
        QListWidgetItem *emptyItem = new QListWidgetItem("ANOMALY 검사방법 패턴이 없습니다.");
        emptyItem->setFlags(Qt::NoItemFlags);
        patternListWidget->addItem(emptyItem);
        autoTrainButton->setEnabled(false);
    }
}

void TrainDialog::setAllPatterns(const QVector<PatternInfo*>& patterns)
{
    allPatterns = patterns;
    // 전체 패턴 설정 (로그 제거)
}

void TrainDialog::addCapturedImage(const cv::Mat& image, int stripCrimpMode)
{
    // 공용 이미지 리스트에 추가
    commonImages.append(image.clone());
    
    // UI 업데이트
    updateImageGrid();
    
    imageCountLabel->setText(QString("이미지 개수: %1").arg(commonImages.size()));
    
    bool anyChecked = false;
    for (auto checkbox : patternCheckBoxes) {
        if (checkbox->isChecked()) {
            anyChecked = true;
            break;
        }
    }
    
    // 공용 이미지가 있으면 학습 가능
    bool hasImages = !commonImages.isEmpty();
    
    autoTrainButton->setEnabled(anyChecked && hasImages);
}

void TrainDialog::updateImageGrid(bool scrollToEnd)
{
    // 현재 스크롤 위치 저장
    int scrollPos = imageListWidget->horizontalScrollBar()->value();
    
    imageListWidget->clear();
    
    // 공용 이미지 리스트 표시
    for (int i = 0; i < commonImages.size(); ++i) {
        const cv::Mat& img = commonImages[i];
        
        // OpenCV Mat을 QImage로 변환
        cv::Mat rgb;
        if (img.channels() == 1) {
            cv::cvtColor(img, rgb, cv::COLOR_GRAY2RGB);
        } else if (img.channels() == 3) {
            cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB);
        } else {
            rgb = img.clone();
        }
        
        QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
        QPixmap pixmap = QPixmap::fromImage(qimg.copy());
        
        // 썸네일 아이콘 생성
        QIcon icon(pixmap);
        
        QListWidgetItem* item = new QListWidgetItem(icon, QString::number(i + 1));
        item->setData(Qt::UserRole, i);  // 인덱스 저장
        imageListWidget->addItem(item);
    }
    
    // 스크롤 위치 처리
    if (scrollToEnd && imageListWidget->count() > 0) {
        // 스크롤바를 맨 오른쪽으로 이동 (최신 이미지 보이도록)
        imageListWidget->scrollToItem(imageListWidget->item(imageListWidget->count() - 1));
    } else {
        // 기존 스크롤 위치 복원
        imageListWidget->horizontalScrollBar()->setValue(scrollPos);
    }
}

void TrainDialog::onModeChanged(int id)
{
    // STRIP/CRIMP 모드 구분 제거됨 - 함수 유지는 하되 동작 없음
    Q_UNUSED(id);
    
    // 패턴 목록 갱신
    setAnomalyPatterns(anomalyPatterns);
    
    // 공용 이미지로 UI 업데이트
    updateImageGrid();
    imageCountLabel->setText(QString("이미지 개수: %1").arg(commonImages.size()));
    previewImageLabel->setText("이미지를 클릭하세요");
}

void TrainDialog::onClearImagesClicked()
{
    if (commonImages.isEmpty()) {
        return;
    }
    
    CustomMessageBox msgBox(this, CustomMessageBox::Question, "확인",
        QString("공용 이미지 %1개를 모두 삭제하시겠습니까?").arg(commonImages.size()),
        QMessageBox::Yes | QMessageBox::No);
    
    if (msgBox.exec() == QMessageBox::Yes) {
        commonImages.clear();
        updateImageGrid();
        imageCountLabel->setText("이미지 개수: 0");
        
        // 공용 이미지가 없으면 학습 불가
        bool hasImages = false;
        
        bool anyChecked = false;
        for (auto checkbox : patternCheckBoxes) {
            if (checkbox->isChecked()) {
                anyChecked = true;
                break;
            }
        }
        
        autoTrainButton->setEnabled(anyChecked && hasImages);
        previewImageLabel->setText("이미지를 클릭하세요");
    }
}

void TrainDialog::onPatternSelectionChanged()
{
    QListWidgetItem *item = patternListWidget->currentItem();
    if (!item) {
        teachingImageLabel->setText("패턴을 선택하세요");
        currentSelectedPattern.clear();
        return;
    }
    
    QString patternName = item->data(Qt::UserRole).toString();
    currentSelectedPattern = patternName;
    
    // 공용 이미지 개수 업데이트 (모든 패턴이 동일한 이미지 사용)
    imageCountLabel->setText(QString("이미지 개수: %1").arg(commonImages.size()));
    
    // 티칭 이미지 업데이트
    updateTeachingImagePreview();
}

void TrainDialog::updateTeachingImagePreview()
{
    QListWidgetItem *item = patternListWidget->currentItem();
    if (!item) {
        teachingImageLabel->setText("패턴을 선택하세요");
        return;
    }
    
    QString patternName = item->data(Qt::UserRole).toString();
    if (patternName.isEmpty()) {
        teachingImageLabel->setText("유효하지 않은 패턴");
        return;
    }
    
    // 패턴 찾기
    PatternInfo* selectedPattern = nullptr;
    for (PatternInfo* pattern : anomalyPatterns) {
        if (pattern && pattern->name == patternName) {
            selectedPattern = pattern;
            break;
        }
    }
    
    if (!selectedPattern) {
        teachingImageLabel->setText("패턴을 찾을 수 없음");
        return;
    }
    
    // 템플릿 이미지 가져오기
    QImage templateImage = selectedPattern->templateImage;
    
    if (templateImage.isNull()) {
        teachingImageLabel->setText("티칭 이미지 없음");
        qDebug() << "[updateTeachingImagePreview] 템플릿 이미지 없음 - 패턴:" << patternName 
                 << "frameIndex:" << selectedPattern->frameIndex;
        return;
    }
    
    // 레이블 크기에 맞게 이미지 스케일링
    QPixmap pixmap = QPixmap::fromImage(templateImage);
    QPixmap scaled = pixmap.scaled(teachingImageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    teachingImageLabel->setPixmap(scaled);
}

void TrainDialog::onStartAutoTrainClicked()
{
    // 체크된 패턴 목록 수집
    QStringList checkedPatterns;
    for (auto it = patternCheckBoxes.begin(); it != patternCheckBoxes.end(); ++it) {
        if (it.value()->isChecked()) {
            checkedPatterns.append(it.key());
        }
    }
    
    if (checkedPatterns.isEmpty()) {
        CustomMessageBox msgBox(this, CustomMessageBox::Warning, "경고",
            "학습할 패턴을 선택하세요 (체크박스).");
        msgBox.exec();
        return;
    }
    
    // 공용 이미지가 있는지 확인
    if (commonImages.isEmpty()) {
        CustomMessageBox msgBox(this, CustomMessageBox::Warning, "경고",
            "학습 이미지가 없습니다.");
        msgBox.exec();
        return;
    }
    
    // 체크된 패턴 중 이미 학습된 패턴이 있는지 확인 (실시간 파일 체크)
    QStringList trainedPatterns;
    for (const QString& patternName : checkedPatterns) {
        if (AnomalyWeightUtils::hasTrainedWeight(patternName)) {
            trainedPatterns.append(patternName);
        }
    }
    
    // 이미 학습된 패턴이 있으면 경고
    if (!trainedPatterns.isEmpty()) {
        QString trainedList = trainedPatterns.join("\n  - ");
        CustomMessageBox msgBox(this, CustomMessageBox::Question, "경고",
            QString("이미 학습된 모델이 존재합니다:\n  - %1\n\n다시 학습하면 기존 모델이 삭제됩니다.\n계속하시겠습니까?").arg(trainedList),
            QMessageBox::Yes | QMessageBox::No);
        
        if (msgBox.exec() != QMessageBox::Yes) {
            return;
        }
    }
    
    // 학습 대기 목록에 추가
    pendingPatterns = checkedPatterns;
    
    // 학습 진행 오버레이 생성
    if (!trainingOverlay) {
        trainingOverlay = new QWidget(this);
        trainingOverlay->setStyleSheet("background-color: rgba(0, 0, 0, 180);");
        trainingOverlay->setGeometry(rect());
        
        QVBoxLayout *overlayLayout = new QVBoxLayout(trainingOverlay);
        overlayLayout->setAlignment(Qt::AlignCenter);
        
        trainingStatusLabel = new QLabel("Preparing training...", trainingOverlay);
        trainingStatusLabel->setStyleSheet("color: #ffffff; font-size: 18px; font-weight: bold; background-color: transparent;");
        trainingStatusLabel->setAlignment(Qt::AlignCenter);
        overlayLayout->addWidget(trainingStatusLabel);
        
        trainingProgressBar = new QProgressBar(trainingOverlay);
        trainingProgressBar->setMinimum(0);
        trainingProgressBar->setMaximum(0);  // 무한 진행 표시
        trainingProgressBar->setFixedWidth(400);
        trainingProgressBar->setStyleSheet(
            "QProgressBar { border: 1px solid #3d3d3d; background-color: #252525; color: #ffffff; text-align: center; height: 25px; }"
            "QProgressBar::chunk { background-color: #4caf50; }"
        );
        overlayLayout->addWidget(trainingProgressBar, 0, Qt::AlignCenter);
        
        QPushButton *cancelButton = new QPushButton("Cancel Training", trainingOverlay);
        cancelButton->setStyleSheet(
            "QPushButton { background-color: #d32f2f; color: white; padding: 10px 30px; border-radius: 5px; font-weight: bold; }"
            "QPushButton:hover { background-color: #f44336; }"
        );
        connect(cancelButton, &QPushButton::clicked, [this]() {
            if (dockerTrainProcess && dockerTrainProcess->state() == QProcess::Running) {
                dockerTrainProcess->kill();
                dockerTrainProcess->waitForFinished(3000);
            }
            pendingPatterns.clear();
            isTraining = false;
            if (progressUpdateTimer) progressUpdateTimer->stop();
            if (trainingOverlay) trainingOverlay->hide();
            updateTrainingProgress("Training cancelled.");
        });
        overlayLayout->addWidget(cancelButton, 0, Qt::AlignCenter);
    }
    
    trainingOverlay->setGeometry(rect());
    trainingOverlay->show();
    trainingOverlay->raise();
    
    isTraining = true;
    totalPatternCount = checkedPatterns.size();
    completedPatternCount = 0;
    currentProgressMessage = "Preparing...";
    
    // 전체 학습 타이머 시작
    if (totalTrainingTimer) delete totalTrainingTimer;
    totalTrainingTimer = new QElapsedTimer();
    totalTrainingTimer->start();
    
    // 진행률 시간 갱신 타이머 시작 (1초마다)
    if (progressUpdateTimer) {
        progressUpdateTimer->start(1000);
    }
    
    // 첫 번째 패턴 학습 시작
    trainNextPattern();
}

void TrainDialog::onCloseClicked()
{
    close();
}

void TrainDialog::onImageItemClicked(QListWidgetItem* item)
{
    if (!item) {
        if (deleteSelectedImageButton) deleteSelectedImageButton->setEnabled(false);
        if (previewImageLabel) {
            previewImageLabel->clear();
            previewImageLabel->setText("이미지를 선택하세요");
        }
        return;
    }
    
    if (deleteSelectedImageButton) deleteSelectedImageButton->setEnabled(true);
    
    int index = item->data(Qt::UserRole).toInt();
    
    if (index < 0 || index >= commonImages.size()) {
        if (previewImageLabel) {
            previewImageLabel->clear();
            previewImageLabel->setText("이미지 로드 실패");
        }
        return;
    }
    
    const cv::Mat& img = commonImages[index];
    
    if (img.empty()) {
        if (previewImageLabel) {
            previewImageLabel->clear();
            previewImageLabel->setText("빈 이미지");
        }
        return;
    }
    
    // OpenCV Mat을 QImage로 변환
    cv::Mat rgb;
    if (img.channels() == 1) {
        cv::cvtColor(img, rgb, cv::COLOR_GRAY2RGB);
    } else if (img.channels() == 3) {
        cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB);
    } else {
        rgb = img.clone();
    }
    
    // QImage 생성 시 데이터 복사 (rgb가 지역 변수라서 복사 필수)
    QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
    QImage qimgCopy = qimg.copy(); // 데이터 복사
    QPixmap pixmap = QPixmap::fromImage(qimgCopy);
    
    if (!previewImageLabel) return;
    
    // 레이블 크기에 맞게 스케일링
    if (previewImageLabel->width() > 0 && previewImageLabel->height() > 0) {
        QPixmap scaled = pixmap.scaled(previewImageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        previewImageLabel->setPixmap(scaled);
    } else {
        previewImageLabel->setPixmap(pixmap);
    }
}

void TrainDialog::onDeleteSelectedImageClicked()
{
    QListWidgetItem* item = imageListWidget->currentItem();
    if (!item) {
        return;
    }
    
    int index = item->data(Qt::UserRole).toInt();
    
    if (index < 0 || index >= commonImages.size()) {
        return;
    }
    
    // 공용 이미지 삭제
    commonImages.removeAt(index);
    
    // UI 업데이트 (스크롤 위치 유지)
    updateImageGrid(false);
    imageCountLabel->setText(QString("이미지 개수: %1").arg(commonImages.size()));
    
    // 미리보기 초기화
    previewImageLabel->clear();
    previewImageLabel->setText("이미지를 클릭하세요");
    
    // 삭제 버튼 비활성화
    deleteSelectedImageButton->setEnabled(false);
    
    // 공용 이미지가 있는지 확인
    bool hasImages = !commonImages.isEmpty();
    
    bool anyChecked = false;
    for (auto checkbox : patternCheckBoxes) {
        if (checkbox->isChecked()) {
            anyChecked = true;
            break;
        }
    }
    
    autoTrainButton->setEnabled(anyChecked && hasImages);
}

void TrainDialog::onAddImagesClicked()
{
    // 커스텀 메시지 박스 생성
    CustomMessageBox msgBox(this, CustomMessageBox::Question, 
                           "이미지 추가", 
                           "이미지 추가 방식을 선택하세요");
    
    // Yes/No 버튼을 폴더/파일로 사용
    msgBox.setButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    msgBox.setButtonText(QMessageBox::Yes, "폴더 선택");
    msgBox.setButtonText(QMessageBox::No, "파일 선택");
    msgBox.setButtonText(QMessageBox::Cancel, "취소");
    
    // 부모 다이얼로그 중앙에 배치
    QRect parentRect = geometry();
    msgBox.move(parentRect.center() - msgBox.rect().center());
    
    int result = msgBox.exec();
    
    QStringList imagePaths;
    
    if (result == QMessageBox::Yes) {
        // 폴더 선택
        QString dirPath = CustomFileDialog::getExistingDirectory(this, 
            "이미지 폴더 선택",
            QDir::homePath());
        
        if (!dirPath.isEmpty()) {
            QDir dir(dirPath);
            QStringList filters;
            filters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.tiff";
            QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files);
            
            for (const QFileInfo &fileInfo : fileList) {
                imagePaths.append(fileInfo.absoluteFilePath());
            }
        }
    }
    else if (result == QMessageBox::No) {
        // 다중 파일 선택
        imagePaths = CustomFileDialog::getOpenFileNames(this, 
            "학습 이미지 선택",
            QDir::homePath(),
            "이미지 파일 (*.png *.jpg *.jpeg *.bmp *.tiff)");
    }
    
    if (imagePaths.isEmpty()) {
        return;
    }
    
    // 공용 이미지 리스트에 추가
    int addedCount = 0;
    
    for (const QString& fileName : imagePaths) {
        cv::Mat img = cv::imread(fileName.toStdString());
        if (!img.empty()) {
            commonImages.append(img);
            addedCount++;
        }
    }
    
    if (addedCount > 0) {
        updateImageGrid();
        imageCountLabel->setText(QString("이미지 개수: %1").arg(commonImages.size()));
        
        bool anyChecked = false;
        for (auto checkbox : patternCheckBoxes) {
            if (checkbox->isChecked()) {
                anyChecked = true;
                break;
            }
        }
        
        // 공용 이미지가 있으면 활성화
        bool hasImages = !commonImages.isEmpty();
        autoTrainButton->setEnabled(anyChecked && hasImages);
        
        qDebug() << "[TrainDialog] 추가된 이미지 수:" << addedCount;
    }
}

void TrainDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void TrainDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton && m_dragging) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void TrainDialog::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    }
}

void TrainDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete) {
        // Delete 키로 선택된 이미지 삭제
        if (deleteSelectedImageButton && deleteSelectedImageButton->isEnabled()) {
            onDeleteSelectedImageClicked();
        }
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void TrainDialog::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    
    if (m_firstShow) {
        m_firstShow = false;
        
        // 부모 위젯 중앙에 위치시키기
        if (parentWidget()) {
            QPoint parentTopLeft = parentWidget()->mapToGlobal(QPoint(0, 0));
            int x = parentTopLeft.x() + (parentWidget()->width() - width()) / 2;
            int y = parentTopLeft.y() + (parentWidget()->height() - height()) / 2;
            move(x, y);
        }
    }
    
    // Docker 이미지 상태 확인 및 정리
    checkAndCleanDockerImages();
}

void TrainDialog::checkAndCleanDockerImages()
{
    // Docker 이미지 확인
    QProcess checkProcess;
    checkProcess.start("docker", QStringList() << "images" << "--format" << "{{.Repository}}:{{.Tag}}");
    checkProcess.waitForFinished(3000);
    
    if (checkProcess.exitCode() != 0) {
        qWarning() << "[Docker] Docker 이미지 확인 실패";
        if (dockerImageInfoLabel) {
            dockerImageInfoLabel->setText("❌ Docker 이미지 확인 실패\nDocker가 실행 중인지 확인하세요.");
            dockerImageInfoLabel->setStyleSheet(
                "QLabel { color: #ff5555; font-size: 11px; padding: 5px; "
                "background-color: #2a2a2a; border: 1px solid #ff5555; border-radius: 3px; }"
            );
        }
        return;
    }
    
    QString output = checkProcess.readAllStandardOutput();
    QStringList images = output.split('\n', Qt::SkipEmptyParts);
    
    bool hasPatchcoreTrainer = false;
    for (const QString& image : images) {
        if (image.contains("patchcore-trainer:latest")) {
            hasPatchcoreTrainer = true;
            break;
        }
    }
    
    // patchcore-trainer 이미지가 없으면 경고
    if (!hasPatchcoreTrainer) {
        qWarning() << "[Docker] patchcore-trainer:latest 이미지가 없습니다.";
        
        if (dockerImageInfoLabel) {
            dockerImageInfoLabel->setText(
                "⚠️ Docker 이미지 없음\n\n"
                "'Docker 이미지 재빌드' 버튼을 눌러\n"
                "이미지를 생성하세요."
            );
            dockerImageInfoLabel->setStyleSheet(
                "QLabel { color: #ffaa00; font-size: 11px; padding: 5px; "
                "background-color: #2a2a2a; border: 1px solid #ffaa00; border-radius: 3px; }"
            );
        }
        
        QTimer::singleShot(100, this, [this]() {
            CustomMessageBox msgBox(this);
            msgBox.setIcon(CustomMessageBox::Warning);
            msgBox.setTitle("Docker 이미지 없음");
            msgBox.setMessage("patchcore-trainer:latest Docker 이미지가 없습니다.\n\n"
                            "'Docker 이미지 재빌드' 버튼을 눌러 이미지를 생성하세요.");
            msgBox.setButtons(QMessageBox::Ok);
            msgBox.exec();
        });
    } else {
        // Docker 이미지 있음
        if (dockerImageInfoLabel) {
            dockerImageInfoLabel->setText("✅ Docker 이미지 있음");
            dockerImageInfoLabel->setStyleSheet(
                "QLabel { color: #55ff55; font-size: 11px; padding: 5px; "
                "background-color: #2a2a2a; border: 1px solid #55ff55; border-radius: 3px; }"
            );
        }
        qDebug() << "[Docker] patchcore-trainer:latest 이미지 있음";
    }
    
    // 댕글링 이미지 정리 (백그라운드)
    QProcess::startDetached("docker", QStringList() << "image" << "prune" << "-f");
    qDebug() << "[Docker] 댕글링 이미지 자동 정리 실행";
}

void TrainDialog::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    
    // 오버레이가 있으면 다이얼로그 크기에 맞춤
    if (trainingOverlay) {
        trainingOverlay->setGeometry(rect());
    }
}

void TrainDialog::trainNextPattern()
{
    if (pendingPatterns.isEmpty()) {
        // 모든 학습 완료
        isTraining = false;
        if (trainingOverlay) trainingOverlay->hide();
        
        // 진행률 타이머 정지
        if (progressUpdateTimer) {
            progressUpdateTimer->stop();
        }
        
        // 총 소요시간 계산
        QString totalTimeStr = "";
        if (totalTrainingTimer) {
            qint64 totalMs = totalTrainingTimer->elapsed();
            int totalSec = totalMs / 1000;
            int minutes = totalSec / 60;
            int seconds = totalSec % 60;
            totalTimeStr = QString("%1분 %2초").arg(minutes).arg(seconds);
        }
        
        // 패턴 목록 갱신 ([Trained] 표시 업데이트)
        for (int i = 0; i < patternListWidget->count(); ++i) {
            QListWidgetItem* item = patternListWidget->item(i);
            QString patternName = item->data(Qt::UserRole).toString();
            bool wasTrained = AnomalyWeightUtils::hasTrainedWeight(patternName);
            item->setData(Qt::UserRole + 1, wasTrained);
            
            QString displayText = patternName;
            if (wasTrained) {
                displayText += " [Trained]";
            }
            item->setText(displayText);
        }
        
        CustomMessageBox msgBox(this, CustomMessageBox::Information, "완료",
            QString("모든 패턴 학습이 완료되었습니다.\n\n학습 패턴: %1개\n총 소요시간: %2")
                .arg(completedPatternCount).arg(totalTimeStr));
        msgBox.exec();
        
        emit trainingFinished(true);
        return;
    }
    
    currentTrainingPattern = pendingPatterns.takeFirst();
    trainPattern(currentTrainingPattern);
}

void TrainDialog::trainPattern(const QString& patternName)
{
    qDebug() << "[TRAIN] 학습 시작:" << patternName;
    
    updateTrainingProgress(QString("Preparing '%1'...").arg(patternName));
    
    // 기존 weights 폴더 삭제
    AnomalyWeightUtils::removeWeightFolder(patternName);
    
    // 해당 패턴 찾기
    PatternInfo* targetPattern = nullptr;
    for (PatternInfo* pattern : anomalyPatterns) {
        if (pattern->name == patternName) {
            targetPattern = pattern;
            break;
        }
    }
    
    if (!targetPattern) {
        qWarning() << "[TRAIN] 패턴을 찾을 수 없음:" << patternName;
        trainNextPattern();
        return;
    }
    
    // 공용 이미지 가져오기
    if (commonImages.isEmpty()) {
        qWarning() << "[TRAIN] 공용 학습 이미지가 없음";
        trainNextPattern();
        return;
    }
    
    qDebug() << "[TRAIN] 패턴" << patternName << "공용 이미지 개수:" << commonImages.size();
    
    // 임시 폴더 생성
    tempTrainingDir = QCoreApplication::applicationDirPath() + QString("/../deploy/data/train/temp_%1_%2")
        .arg(patternName)
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    QString goodDir = tempTrainingDir + "/good";
    QDir().mkpath(goodDir);
    
    qDebug() << "[TRAIN] 임시 폴더:" << tempTrainingDir;
    
    // ROI 정보
    int roiX = static_cast<int>(targetPattern->rect.x());
    int roiY = static_cast<int>(targetPattern->rect.y());
    int roiW = static_cast<int>(targetPattern->rect.width());
    int roiH = static_cast<int>(targetPattern->rect.height());
    
    qDebug() << "[TRAIN] ROI:" << roiX << roiY << roiW << roiH;
    
    // 부모 FID 패턴 찾기 (좌표 보정용)
    PatternInfo* parentFidPattern = nullptr;
    cv::Mat fidTemplate;
    cv::Mat fidMask;
    QPointF fidTeachingCenter;
    QPointF insTeachingCenter(targetPattern->rect.x() + roiW / 2.0, 
                              targetPattern->rect.y() + roiH / 2.0);
    bool useFidMatching = false;
    
    // Parent FID 패턴 찾기 (parentId 사용) - allPatterns에서 검색
    if (!targetPattern->parentId.isNull()) {
        for (PatternInfo* pattern : allPatterns) {
            if (pattern->id == targetPattern->parentId && pattern->type == PatternType::FID) {
                parentFidPattern = pattern;
                break;
            }
        }
    }
    
    if (parentFidPattern && !parentFidPattern->matchTemplate.isNull()) {
        // QImage를 cv::Mat으로 변환
        QImage tempImg = parentFidPattern->matchTemplate.convertToFormat(QImage::Format_RGB888);
        fidTemplate = cv::Mat(tempImg.height(), tempImg.width(), CV_8UC3,
                              const_cast<uchar*>(tempImg.bits()), tempImg.bytesPerLine()).clone();
        cv::cvtColor(fidTemplate, fidTemplate, cv::COLOR_RGB2BGR);
        
        // 마스크가 있으면 변환
        if (!parentFidPattern->matchTemplateMask.isNull()) {
            QImage maskImg = parentFidPattern->matchTemplateMask.convertToFormat(QImage::Format_Grayscale8);
            fidMask = cv::Mat(maskImg.height(), maskImg.width(), CV_8UC1,
                              const_cast<uchar*>(maskImg.bits()), maskImg.bytesPerLine()).clone();
        }
        
        fidTeachingCenter = parentFidPattern->rect.center();
        useFidMatching = true;
        qDebug() << "[TRAIN] FID 매칭 사용:" << parentFidPattern->name;
    } else {
        qDebug() << "[TRAIN] FID 매칭 없이 고정 좌표 사용 (부모 FID:" 
                 << (targetPattern->parentId.isNull() ? "없음" : "템플릿 없음") << ")";
    }
    
    // 개별 패턴 타이머 시작
    if (trainingTimer) delete trainingTimer;
    trainingTimer = new QElapsedTimer();
    trainingTimer->start();
    
    updateTrainingProgress(QString("%1 Extracting ROI '%2'... (0/%3)%4")
        .arg(getPatternProgressString()).arg(patternName).arg(commonImages.size()).arg(getTotalTimeString()));
    
    int croppedCount = 0;
    int fidMatchFailCount = 0;
    
    for (int i = 0; i < commonImages.size(); ++i) {
        // 진행률 업데이트 (5개마다 또는 마지막)
        if (i % 5 == 0 || i == commonImages.size() - 1) {
            updateTrainingProgress(QString("%1 Extracting ROI '%2'... (%3/%4)%5")
                .arg(getPatternProgressString()).arg(patternName).arg(i + 1).arg(commonImages.size()).arg(getTotalTimeString()));
            QApplication::processEvents();
        }
        
        cv::Mat image = commonImages[i];
        if (image.empty()) continue;
        
        int finalRoiX = roiX, finalRoiY = roiY;
        
        if (useFidMatching && !fidTemplate.empty()) {
            // FID ROI 영역에서만 검색 (마진 추가)
            int searchMargin = 50;  // 검색 마진 (픽셀)
            int fidRoiX = std::max(0, static_cast<int>(parentFidPattern->rect.x()) - searchMargin);
            int fidRoiY = std::max(0, static_cast<int>(parentFidPattern->rect.y()) - searchMargin);
            int fidRoiW = static_cast<int>(parentFidPattern->rect.width()) + searchMargin * 2;
            int fidRoiH = static_cast<int>(parentFidPattern->rect.height()) + searchMargin * 2;
            
            // 이미지 범위 체크
            if (fidRoiX + fidRoiW > image.cols) fidRoiW = image.cols - fidRoiX;
            if (fidRoiY + fidRoiH > image.rows) fidRoiH = image.rows - fidRoiY;
            
            // 검색 영역 추출
            cv::Mat searchRegion = image(cv::Rect(fidRoiX, fidRoiY, fidRoiW, fidRoiH));
            
            cv::Mat result;
            int matchMethod = cv::TM_CCOEFF_NORMED;
            
            if (!fidMask.empty()) {
                cv::matchTemplate(searchRegion, fidTemplate, result, matchMethod, fidMask);
            } else {
                cv::matchTemplate(searchRegion, fidTemplate, result, matchMethod);
            }
            
            double minVal, maxVal;
            cv::Point minLoc, maxLoc;
            cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
            
            if (maxVal < 0.7) {
                fidMatchFailCount++;
                continue;
            }
            
            // 검색 영역 내 좌표를 전체 이미지 좌표로 변환
            double fidMatchCenterX = fidRoiX + maxLoc.x + fidTemplate.cols / 2.0;
            double fidMatchCenterY = fidRoiY + maxLoc.y + fidTemplate.rows / 2.0;
            
            double relativeX = insTeachingCenter.x() - fidTeachingCenter.x();
            double relativeY = insTeachingCenter.y() - fidTeachingCenter.y();
            
            double newInsCenterX = fidMatchCenterX + relativeX;
            double newInsCenterY = fidMatchCenterY + relativeY;
            
            finalRoiX = static_cast<int>(newInsCenterX - roiW / 2.0);
            finalRoiY = static_cast<int>(newInsCenterY - roiH / 2.0);
        }
        
        // ROI 범위 체크
        if (finalRoiX < 0 || finalRoiY < 0 || 
            finalRoiX + roiW > image.cols || finalRoiY + roiH > image.rows) {
            continue;
        }
        
        // ROI 크롭 및 저장
        cv::Rect roiRect(finalRoiX, finalRoiY, roiW, roiH);
        cv::Mat croppedImage = image(roiRect).clone();
        
        QString outputPath = QString("%1/%2.png").arg(goodDir).arg(i, 4, 10, QChar('0'));
        cv::imwrite(outputPath.toStdString(), croppedImage);
        croppedCount++;
    }
    
    qDebug() << "[TRAIN] ROI 크롭 완료:" << croppedCount << "개 (FID 실패:" << fidMatchFailCount << "개)";
    
    if (croppedCount == 0) {
        CustomMessageBox msgBox(this, CustomMessageBox::Warning, "경고",
            QString("'%1' 패턴에 유효한 ROI 이미지가 없습니다.").arg(patternName));
        msgBox.exec();
        QDir(tempTrainingDir).removeRecursively();
        trainNextPattern();
        return;
    }
    
    // Docker 학습 실행
    QString weightsBaseDir = QCoreApplication::applicationDirPath() + "/../deploy/weights";
    QString outputDir = weightsBaseDir + "/" + patternName;
    QDir().mkpath(outputDir);
    
    QString dockerScript = QCoreApplication::applicationDirPath() + "/../docker/docker_run_with_data.sh";
    QStringList args;
    args << tempTrainingDir << outputDir << patternName;
    
    // PatchCore 옵션 추가
    if (backboneComboBox && coresetRatioSpinBox && numNeighborsSpinBox) {
        args << "--backbone" << backboneComboBox->currentText();
        args << "--coreset-ratio" << QString::number(coresetRatioSpinBox->value(), 'g');
        args << "--num-neighbors" << QString::number(numNeighborsSpinBox->value());
    }
    
    qDebug() << "[TRAIN] Docker 학습 시작:" << dockerScript << args;
    updateTrainingProgress(QString("%1 Training model '%2'...%3")
        .arg(getPatternProgressString()).arg(patternName).arg(getTotalTimeString()));
    
    // 기존 프로세스 정리
    if (dockerTrainProcess) {
        if (dockerTrainProcess->state() == QProcess::Running) {
            dockerTrainProcess->kill();
            dockerTrainProcess->waitForFinished(3000);
        }
        delete dockerTrainProcess;
    }
    
    dockerTrainProcess = new QProcess(this);
    dockerTrainProcess->setWorkingDirectory(QCoreApplication::applicationDirPath() + "/..");
    dockerTrainProcess->setProcessChannelMode(QProcess::MergedChannels);
    
    connect(dockerTrainProcess, &QProcess::readyReadStandardOutput, this, &TrainDialog::onDockerOutputReady);
    connect(dockerTrainProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
            this, &TrainDialog::onDockerFinished);
    
    dockerTrainProcess->start(dockerScript, args);
}

QString TrainDialog::getTotalTimeString() const
{
    if (!totalTrainingTimer) return "";
    
    qint64 totalMs = totalTrainingTimer->elapsed();
    int totalSec = totalMs / 1000;
    int mins = totalSec / 60;
    int secs = totalSec % 60;
    return QString(" [%1:%2]").arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
}

QString TrainDialog::getPatternProgressString() const
{
    if (totalPatternCount <= 1) return "";
    int currentPatternIndex = totalPatternCount - pendingPatterns.size();
    return QString(" [%1/%2]").arg(currentPatternIndex).arg(totalPatternCount);
}

void TrainDialog::onDockerOutputReady()
{
    if (!dockerTrainProcess) return;
    
    QString output = dockerTrainProcess->readAllStandardOutput();
    qDebug() << "[DOCKER]" << output;
    
    QString totalElapsedStr = getTotalTimeString();
    
    QString patternProgress = getPatternProgressString();
    
    // Coreset Sampling 진행률 파싱: "Selecting Coreset Indices.:  45%|████▍     | 918/2048"
    QRegularExpression coresetRe(R"(Coreset.*?(\d+)/(\d+))");
    QRegularExpressionMatch coresetMatch = coresetRe.match(output);
    if (coresetMatch.hasMatch()) {
        QString current = coresetMatch.captured(1);
        QString total = coresetMatch.captured(2);
        updateTrainingProgress(QString("%1 Training '%2'... Sampling %3/%4%5")
            .arg(patternProgress).arg(currentTrainingPattern).arg(current).arg(total).arg(totalElapsedStr));
        return;
    }
    
    // OpenVINO 변환 진행률 파싱
    if (output.contains("Converting") || output.contains("Exporting") || output.contains("OpenVINO")) {
        updateTrainingProgress(QString("%1 Training '%2'... Converting to OpenVINO%3")
            .arg(patternProgress).arg(currentTrainingPattern).arg(totalElapsedStr));
        return;
    }
    
    // Training 완료 파싱
    if (output.contains("Training completed")) {
        updateTrainingProgress(QString("%1 Training '%2'... Exporting model%3")
            .arg(patternProgress).arg(currentTrainingPattern).arg(totalElapsedStr));
        return;
    }
    
    // Epoch 진행률 파싱
    if (output.contains("Epoch") || output.contains("epoch")) {
        updateTrainingProgress(QString("%1 Training '%2'... Epoch running%3")
            .arg(patternProgress).arg(currentTrainingPattern).arg(totalElapsedStr));
        return;
    }
    
    // 기본: Training model 표시
    if (output.contains("Training") || output.contains("Starting")) {
        updateTrainingProgress(QString("%1 Training model '%2'...%3")
            .arg(patternProgress).arg(currentTrainingPattern).arg(totalElapsedStr));
    }
}

void TrainDialog::onDockerFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug() << "[TRAIN] Docker 종료: exitCode=" << exitCode << ", status=" << exitStatus;
    
    // 소요 시간 계산 (개별 패턴)
    QString elapsedStr = "";
    if (trainingTimer) {
        qint64 elapsedMs = trainingTimer->elapsed();
        int elapsedSec = elapsedMs / 1000;
        int minutes = elapsedSec / 60;
        int seconds = elapsedSec % 60;
        elapsedStr = QString(" [%1:%2]").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
    }
    
    // 임시 폴더 정리 (ROI 크롭 폴더)
    if (!tempTrainingDir.isEmpty()) {
        QDir(tempTrainingDir).removeRecursively();
        tempTrainingDir.clear();
    }
    
    // weights 출력 폴더 정리 (bin, xml, norm_stats.txt만 남기고 삭제)
    if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
        QString weightsDir = QCoreApplication::applicationDirPath() + "/../deploy/weights/" + currentTrainingPattern;
        QDir outputDir(weightsDir);
        if (outputDir.exists()) {
            // 삭제할 파일/폴더: patchcore_model.pt, Patchcore/, temp_dataset/
            QStringList itemsToRemove;
            itemsToRemove << "patchcore_model.pt" << "Patchcore" << "temp_dataset";
            
            for (const QString& item : itemsToRemove) {
                QString itemPath = weightsDir + "/" + item;
                QFileInfo fi(itemPath);
                if (fi.exists()) {
                    if (fi.isDir()) {
                        QDir(itemPath).removeRecursively();
                        qDebug() << "[TRAIN] 폴더 삭제:" << itemPath;
                    } else {
                        QFile::remove(itemPath);
                        qDebug() << "[TRAIN] 파일 삭제:" << itemPath;
                    }
                }
            }
        }
    }
    
    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        qWarning() << "[TRAIN] 학습 실패:" << currentTrainingPattern;
        updateTrainingProgress(QString("Training '%1' FAILED!%2").arg(currentTrainingPattern).arg(elapsedStr));
        
        // 잠시 대기 후 다음 패턴으로
        QTimer::singleShot(1000, this, &TrainDialog::trainNextPattern);
    } else {
        completedPatternCount++;
        qDebug() << "[TRAIN] 학습 완료:" << currentTrainingPattern << elapsedStr 
                 << QString("(%1/%2)").arg(completedPatternCount).arg(totalPatternCount);
        updateTrainingProgress(QString("Training '%1' completed!%2 (%3/%4)")
            .arg(currentTrainingPattern).arg(elapsedStr)
            .arg(completedPatternCount).arg(totalPatternCount));
        
        // 다음 패턴 학습
        QTimer::singleShot(500, this, &TrainDialog::trainNextPattern);
    }
}

void TrainDialog::onRebuildDockerClicked()
{
    // 학습 중이면 무시
    if (isTraining) {
        QMessageBox::warning(this, "경고", "학습 중에는 Docker 이미지를 재빌드할 수 없습니다.");
        return;
    }
    
    // CustomMessageBox로 질문
    CustomMessageBox msgBox(this, CustomMessageBox::Question, "Docker 이미지 재빌드",
                            "Docker 이미지를 재빌드하시겠습니까?\n기존 이미지가 삭제되고 새로 빌드됩니다.",
                            QMessageBox::Yes | QMessageBox::No);
    msgBox.setButtonText(QMessageBox::Yes, "예");
    msgBox.setButtonText(QMessageBox::No, "아니오");
    
    if (msgBox.exec() != QMessageBox::Yes) {
        return;
    }
    
    rebuildDockerButton->setEnabled(false);
    rebuildDockerButton->setText("재빌드 중...");
    
    // 버튼 진행률 표시 함수
    auto updateButtonProgress = [this](int progress) {
        QString style;
        if (progress >= 100) {
            // 100%일 때는 전체 녹색
            style = "QPushButton { "
                    "  background-color: #4caf50; "
                    "  color: white; "
                    "  padding: 10px 20px; "
                    "  border-radius: 5px; "
                    "  font-weight: bold; "
                    "}";
        } else {
            // 진행 중일 때는 그라디언트
            style = QString(
                "QPushButton { "
                "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
                "    stop:0 #4caf50, stop:%1 #4caf50, stop:%1 #555555, stop:1 #555555); "
                "  color: white; "
                "  padding: 10px 20px; "
                "  border-radius: 5px; "
                "  font-weight: bold; "
                "}"
            ).arg(progress / 100.0, 0, 'f', 2);
        }
        rebuildDockerButton->setStyleSheet(style);
    };
    
    // 초기 진행률 0%
    updateButtonProgress(0);
    QApplication::processEvents();
    
    // docker 폴더 경로 찾기
    QString appPath = QCoreApplication::applicationDirPath();
    QString dockerDir = QDir(appPath).filePath("../docker");
    QDir dir(dockerDir);
    if (!dir.exists()) {
        dockerDir = QDir(appPath).filePath("../../docker");  // macOS 앱 번들
        dir.setPath(dockerDir);
    }
    
    if (!dir.exists()) {
        if (trainingOverlay) trainingOverlay->hide();
        
        CustomMessageBox msgBox(this, CustomMessageBox::Critical, "오류",
                                "Docker 폴더를 찾을 수 없습니다: " + dockerDir);
        msgBox.exec();
        
        rebuildDockerButton->setEnabled(true);
        rebuildDockerButton->setText("🔄 Docker 이미지 재빌드");
        return;
    }
    
    dockerDir = dir.absolutePath();
    QString imageName = "patchcore-trainer";
    
    // 1단계: Docker 설치 확인 (0-10%)
    rebuildDockerButton->setText("🔄 Docker 설치 확인 중... (0%)");
    updateButtonProgress(5);
    QApplication::processEvents();
    
    QProcess checkDocker;
    checkDocker.start("docker", QStringList() << "--version");
    checkDocker.waitForFinished(5000);
    
    if (checkDocker.exitCode() != 0) {
        if (trainingOverlay) trainingOverlay->hide();
        
        CustomMessageBox msgBox(this, CustomMessageBox::Critical, "오류",
                                "Docker가 설치되지 않았거나 실행할 수 없습니다.\nDocker Desktop을 설치하고 실행해주세요.");
        msgBox.exec();
        
        rebuildDockerButton->setEnabled(true);
        rebuildDockerButton->setText("🔄 Docker 이미지 재빌드");
        return;
    }
    
    qDebug() << "[Docker] Version:" << checkDocker.readAllStandardOutput();
    
    rebuildDockerButton->setText("🔄 Docker 설치 확인 완료 (10%)");
    updateButtonProgress(10);
    QApplication::processEvents();
    
    // 2단계: 기존 이미지 삭제 (10-20%)
    rebuildDockerButton->setText("🗑️ 기존 이미지 삭제 중... (10%)");
    QApplication::processEvents();
    
    QProcess removeProcess;
    removeProcess.start("docker", QStringList() << "rmi" << "-f" << imageName);
    removeProcess.waitForFinished(30000);  // 30초 대기
    
    QString removeOutput = removeProcess.readAllStandardOutput();
    QString removeError = removeProcess.readAllStandardError();
    qDebug() << "[Docker Remove] Output:" << removeOutput;
    qDebug() << "[Docker Remove] Error:" << removeError;
    
    // 이미지가 없어도 에러를 무시하고 계속 진행 (이미지가 없으면 새로 빌드하면 됨)
    if (removeError.contains("No such image", Qt::CaseInsensitive)) {
        qDebug() << "[Docker Remove] 이미지가 없음. 새로 빌드 진행.";
    }
    
    rebuildDockerButton->setText("✅ 이미지 삭제 완료 (20%)");
    updateButtonProgress(20);
    QApplication::processEvents();
    
    // 3단계: 새 이미지 빌드 (20-100%)
    rebuildDockerButton->setText("🔨 Docker 이미지 빌드 시작... (20%)");
    QApplication::processEvents();
    
    QProcess buildProcess;
    buildProcess.setProcessChannelMode(QProcess::MergedChannels);  // stdout과 stderr 합치기
    buildProcess.setWorkingDirectory(dockerDir);
    buildProcess.start("docker", QStringList() << "build" << "-t" << imageName << ".");
    
    qDebug() << "[Docker Build] Working directory:" << dockerDir;
    qDebug() << "[Docker Build] Command: docker build -t" << imageName << ".";
    
    if (!buildProcess.waitForStarted(10000)) {
        qDebug() << "[Docker Build] Failed to start:" << buildProcess.errorString();
        rebuildDockerButton->setEnabled(true);
        rebuildDockerButton->setText("❌ Docker 시작 실패");
        CustomMessageBox msgBox(this, CustomMessageBox::Critical, "오류", 
                                "Docker 프로세스를 시작할 수 없습니다.\nDocker가 설치되어 있고 실행 중인지 확인하세요.");
        msgBox.exec();
        return;
    }
    
    qDebug() << "[Docker Build] Process started successfully";
    
    // 빌드 진행 상황 표시 및 로그 수집
    int stepCount = 0;
    int totalSteps = 15;  // Dockerfile의 대략적인 단계 수 (추정)
    QString allOutput;
    QString allError;
    
    while (buildProcess.state() == QProcess::Running) {
        buildProcess.waitForReadyRead(500);
        QString output = buildProcess.readAllStandardOutput();
        
        if (!output.isEmpty()) {
            allOutput += output;
            qDebug() << "[Docker Build Output]" << output.trimmed();
            
            // "Step X/Y" 패턴 찾기
            if (output.contains("Step ", Qt::CaseInsensitive)) {
                QRegularExpression re("Step (\\d+)/(\\d+)", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch match = re.match(output);
                if (match.hasMatch()) {
                    int currentStep = match.captured(1).toInt();
                    totalSteps = match.captured(2).toInt();
                    stepCount = currentStep;
                    
                    // 20% ~ 100% 사이로 매핑
                    int progress = 20 + (stepCount * 80 / totalSteps);
                    
                    rebuildDockerButton->setText(QString("🔨 빌드 중... Step %1/%2 (%3%)").arg(stepCount).arg(totalSteps).arg(progress));
                    updateButtonProgress(progress);
                    
                    qDebug() << "[Docker Build Progress]" << progress << "% - Step" << stepCount << "/" << totalSteps;
                }
            }
            QApplication::processEvents();
        } else {
            // 출력이 없으면 잠시 대기
            QThread::msleep(100);
        }
    }
    
    buildProcess.waitForFinished(-1);
    int exitCode = buildProcess.exitCode();
    
    // 남은 출력 읽기
    QString remainingOutput = buildProcess.readAllStandardOutput();
    if (!remainingOutput.isEmpty()) {
        allOutput += remainingOutput;
        qDebug() << "[Docker Build Remaining]" << remainingOutput.trimmed();
    }
    
    qDebug() << "[Docker Build] Exit code:" << exitCode;
    qDebug() << "[Docker Build] Final output length:" << allOutput.length();
    
    // 빌드 완료 시 진행률 100%로
    rebuildDockerButton->setText("✅ 빌드 완료! (100%)");
    updateButtonProgress(100);
    QApplication::processEvents();
    
    // 1.5초 후 원래 스타일로 복구
    QTimer::singleShot(1500, [this]() {
        rebuildDockerButton->setEnabled(true);
        rebuildDockerButton->setText("🔄 Docker 이미지 재빌드");
        rebuildDockerButton->setStyleSheet("");  // 원래 스타일로
    });
    
    if (exitCode == 0) {
        CustomMessageBox msgBox(this, CustomMessageBox::Information, "완료",
                                "Docker 이미지가 성공적으로 재빌드되었습니다.");
        msgBox.exec();
    } else {
        rebuildDockerButton->setEnabled(true);
        rebuildDockerButton->setText("❌ 재빌드 실패");
        rebuildDockerButton->setStyleSheet("QPushButton { background-color: #d32f2f; color: white; padding: 10px 20px; border-radius: 5px; font-weight: bold; }");
        
        // 3초 후 원래 스타일로 복구
        QTimer::singleShot(3000, [this]() {
            rebuildDockerButton->setText("🔄 Docker 이미지 재빌드");
            rebuildDockerButton->setStyleSheet("");
        });
        
        // 상세 에러 메시지 구성
        QString msg = QString("Docker 이미지 빌드 실패 (Exit code: %1)\n\n").arg(exitCode);
        
        // 출력의 마지막 30줄 표시
        QStringList outputLines = allOutput.split('\n', Qt::SkipEmptyParts);
        
        if (!outputLines.isEmpty()) {
            msg += "=== 빌드 출력 (마지막 30줄) ===\n";
            int startIdx = qMax(0, outputLines.size() - 30);
            for (int i = startIdx; i < outputLines.size(); i++) {
                msg += outputLines[i] + "\n";
            }
        } else {
            msg += "출력 없음. Docker가 실행 중인지 확인하세요.";
        }
        
        qDebug() << "[Docker Build Failed] Full output:" << allOutput;
        
        CustomMessageBox msgBox(this, CustomMessageBox::Critical, "오류", msg);
        msgBox.exec();
    }
}

void TrainDialog::updateTrainingProgress(const QString& message)
{
    // 시간 부분 제외한 메시지 저장 (타이머가 갱신할 때 사용)
    // 메시지에 시간이 포함되어 있으면 제거
    QString baseMessage = message;
    int timeIdx = baseMessage.lastIndexOf(" [");
    if (timeIdx > 0) {
        baseMessage = baseMessage.left(timeIdx);
    }
    currentProgressMessage = baseMessage;
    
    if (trainingStatusLabel) {
        trainingStatusLabel->setText(message);
    }
    qDebug() << "[TRAIN STATUS]" << message;
    
    // UI 블로킹 방지 - 이벤트 루프 처리
    QApplication::processEvents();
}
