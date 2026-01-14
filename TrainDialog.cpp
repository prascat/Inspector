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
#include "ImageProcessor.h"
#include <QPainter>

TrainDialog::TrainDialog(QWidget *parent)
    : QWidget(parent)
    , m_dragging(false)
    , m_firstShow(true)
    , trainProcess(nullptr)
    , trainingTimer(nullptr)
    , totalTrainingTimer(nullptr)
    , progressUpdateTimer(nullptr)
    , isTraining(false)
    , trainingOverlay(nullptr)
    , trainingProgressBar(nullptr)
    , trainingStatusLabel(nullptr)
    , totalPatternCount(0)
    , completedPatternCount(0)
    , selectAllCheckBox(nullptr)
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
    resize(1000, 700);
}

TrainDialog::~TrainDialog()
{
    if (trainProcess) {
        if (trainProcess->state() == QProcess::Running) {
            trainProcess->kill();
            trainProcess->waitForFinished(3000);
        }
        delete trainProcess;
        trainProcess = nullptr;
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

    // ===== 좌측: 버튼 + 탭(A-PC/A-PD) + 패턴 목록 =====
    QWidget *leftWidget = new QWidget(this);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);
    
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
    
    // 탭 위젯 생성
    modelTabWidget = new QTabWidget(this);
    
    // ========== A-PC 탭 ==========
    QWidget *apcTab = new QWidget();
    QVBoxLayout *apcLayout = new QVBoxLayout(apcTab);
    apcLayout->setContentsMargins(5, 5, 5, 5);
    apcLayout->setSpacing(10);
    
    // A-PC 모델 옵션
    QGroupBox *apcOptionsGroup = new QGroupBox("PatchCore 옵션", apcTab);
    QFormLayout *apcOptionsLayout = new QFormLayout(apcOptionsGroup);
    apcOptionsLayout->setSpacing(8);
    
    backboneComboBox = new QComboBox(apcTab);
    backboneComboBox->addItem("resnet18");
    backboneComboBox->addItem("resnet50");
    backboneComboBox->addItem("wide_resnet50_2");
    backboneComboBox->setCurrentIndex(2);
    apcOptionsLayout->addRow("Backbone:", backboneComboBox);
    
    coresetRatioSpinBox = new QDoubleSpinBox(apcTab);
    coresetRatioSpinBox->setRange(0.0, 1.0);
    coresetRatioSpinBox->setSingleStep(0.01);
    coresetRatioSpinBox->setDecimals(2);
    coresetRatioSpinBox->setValue(0.01);
    apcOptionsLayout->addRow("Coreset Ratio:", coresetRatioSpinBox);
    
    numNeighborsSpinBox = new QSpinBox(apcTab);
    numNeighborsSpinBox->setRange(1, 50);
    numNeighborsSpinBox->setValue(9);
    apcOptionsLayout->addRow("Num Neighbors:", numNeighborsSpinBox);
    
    apcLayout->addWidget(apcOptionsGroup);
    
    // A-PC 패턴 테이블
    patternTableWidget = new QTableWidget(apcTab);
    patternTableWidget->setColumnCount(3);
    patternTableWidget->setHorizontalHeaderLabels({"", "패턴명", "학습여부"});
    patternTableWidget->horizontalHeader()->setStretchLastSection(false);
    patternTableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    patternTableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    patternTableWidget->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    patternTableWidget->setColumnWidth(0, 50);
    patternTableWidget->setColumnWidth(2, 80);
    patternTableWidget->verticalHeader()->setVisible(false);
    patternTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    patternTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    patternTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    
    selectAllCheckBox = new QCheckBox(patternTableWidget);
    selectAllCheckBox->setTristate(true);
    selectAllCheckBox->setStyleSheet(
        "QCheckBox::indicator:checked { background-color: #4CAF50; border: 2px solid #4CAF50; }"
        "QCheckBox::indicator:unchecked { background-color: white; border: 2px solid #CCCCCC; }"
        "QCheckBox::indicator:indeterminate { background-color: #81C784; border: 2px solid #4CAF50; }"
        "QCheckBox::indicator { width: 18px; height: 18px; }"
    );
    selectAllCheckBox->move(15, 5);
    selectAllCheckBox->raise();
    
    connect(selectAllCheckBox, &QCheckBox::stateChanged, this, [this](int state) {
        if (state == Qt::PartiallyChecked) return;
        bool checked = (state == Qt::Checked);
        for (auto checkbox : patternCheckBoxes) {
            checkbox->setChecked(checked);
        }
    });
    
    apcLayout->addWidget(patternTableWidget);
    
    modelTabWidget->addTab(apcTab, "A-PC (PatchCore)");
    
    // ========== A-PD 탭 ==========
    QWidget *apdTab = new QWidget();
    QVBoxLayout *apdLayout = new QVBoxLayout(apdTab);
    apdLayout->setContentsMargins(5, 5, 5, 5);
    apdLayout->setSpacing(10);
    
    // A-PD 모델 옵션
    QGroupBox *apdOptionsGroup = new QGroupBox("PaDiM 옵션", apdTab);
    QFormLayout *apdOptionsLayout = new QFormLayout(apdOptionsGroup);
    apdOptionsLayout->setSpacing(8);
    
    padimBackboneComboBox = new QComboBox(apdTab);
    padimBackboneComboBox->addItem("resnet18");
    padimBackboneComboBox->addItem("resnet50");
    padimBackboneComboBox->addItem("wide_resnet50_2");
    padimBackboneComboBox->setCurrentIndex(2);
    apdOptionsLayout->addRow("Backbone:", padimBackboneComboBox);
    
    padimNumLayersSpinBox = new QSpinBox(apdTab);
    padimNumLayersSpinBox->setRange(1, 4);
    padimNumLayersSpinBox->setValue(3);
    apdOptionsLayout->addRow("Num Layers:", padimNumLayersSpinBox);
    
    apdLayout->addWidget(apdOptionsGroup);
    
    // A-PD 패턴 테이블
    padimPatternTableWidget = new QTableWidget(apdTab);
    padimPatternTableWidget->setColumnCount(3);
    padimPatternTableWidget->setHorizontalHeaderLabels({"", "패턴명", "학습여부"});
    padimPatternTableWidget->horizontalHeader()->setStretchLastSection(false);
    padimPatternTableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    padimPatternTableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    padimPatternTableWidget->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    padimPatternTableWidget->setColumnWidth(0, 50);
    padimPatternTableWidget->setColumnWidth(2, 80);
    padimPatternTableWidget->verticalHeader()->setVisible(false);
    padimPatternTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    padimPatternTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    padimPatternTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    
    padimSelectAllCheckBox = new QCheckBox(padimPatternTableWidget);
    padimSelectAllCheckBox->setTristate(true);
    padimSelectAllCheckBox->setStyleSheet(
        "QCheckBox::indicator:checked { background-color: #4CAF50; border: 2px solid #4CAF50; }"
        "QCheckBox::indicator:unchecked { background-color: white; border: 2px solid #CCCCCC; }"
        "QCheckBox::indicator:indeterminate { background-color: #81C784; border: 2px solid #4CAF50; }"
        "QCheckBox::indicator { width: 18px; height: 18px; }"
    );
    padimSelectAllCheckBox->move(15, 5);
    padimSelectAllCheckBox->raise();
    
    connect(padimSelectAllCheckBox, &QCheckBox::stateChanged, this, [this](int state) {
        if (state == Qt::PartiallyChecked) return;
        bool checked = (state == Qt::Checked);
        for (auto checkbox : padimPatternCheckBoxes) {
            checkbox->setChecked(checked);
        }
    });
    
    apdLayout->addWidget(padimPatternTableWidget);
    
    modelTabWidget->addTab(apdTab, "A-PD (PaDiM)");
    
    leftLayout->addWidget(modelTabWidget);
    
    leftWidget->setMaximumWidth(280);
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
    imageListWidget->setIconSize(QSize(80, 80));
    imageListWidget->setResizeMode(QListWidget::Adjust);
    imageListWidget->setMovement(QListWidget::Static);
    imageListWidget->setFlow(QListWidget::LeftToRight);
    imageListWidget->setWrapping(false);
    imageListWidget->setFixedHeight(110);
    imageWidgetLayout->addWidget(imageListWidget);
    
    imageWidget->setMaximumHeight(150);
    rightLayout->addWidget(imageWidget);
    
    // 중앙: 선택된 이미지 표시
    previewImageLabel = new QLabel(this);
    previewImageLabel->setAlignment(Qt::AlignCenter);
    previewImageLabel->setMinimumSize(400, 300);
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
    if (patternTableWidget) connect(patternTableWidget, &QTableWidget::itemSelectionChanged, this, &TrainDialog::onPatternSelectionChanged);
    if (padimPatternTableWidget) connect(padimPatternTableWidget, &QTableWidget::itemSelectionChanged, this, &TrainDialog::onPadimPatternSelectionChanged);
    if (imageListWidget) connect(imageListWidget, &QListWidget::itemClicked, this, &TrainDialog::onImageItemClicked);
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
    
    // A-PC 테이블 초기화
    patternTableWidget->setRowCount(0);
    patternCheckBoxes.clear();
    
    // A-PD 테이블 초기화
    padimPatternTableWidget->setRowCount(0);
    padimPatternCheckBoxes.clear();
    
    // A-PC 패턴 추가
    int apcRow = 0;
    for (PatternInfo* pattern : patterns) {
        if (pattern && pattern->type == PatternType::INS &&
            pattern->inspectionMethod == InspectionMethod::A_PC) {
            
            patternTableWidget->insertRow(apcRow);
            
            // 체크박스
            QCheckBox *checkBox = new QCheckBox();
            checkBox->setStyleSheet(
                "QCheckBox::indicator:checked { background-color: #4CAF50; border: 2px solid #4CAF50; }"
                "QCheckBox::indicator:unchecked { background-color: white; border: 2px solid #CCCCCC; }"
                "QCheckBox::indicator { width: 18px; height: 18px; }"
            );
            QWidget *checkWidget = new QWidget();
            QHBoxLayout *checkLayout = new QHBoxLayout(checkWidget);
            checkLayout->addWidget(checkBox);
            checkLayout->setAlignment(Qt::AlignCenter);
            checkLayout->setContentsMargins(0, 0, 0, 0);
            patternTableWidget->setCellWidget(apcRow, 0, checkWidget);
            
            // 패턴명
            QTableWidgetItem *nameItem = new QTableWidgetItem(pattern->name);
            nameItem->setData(Qt::UserRole, pattern->name);
            nameItem->setTextAlignment(Qt::AlignCenter);
            patternTableWidget->setItem(apcRow, 1, nameItem);
            
            // 학습여부 확인 (TensorRT 또는 ONNX)
            QString recipesDir = QCoreApplication::applicationDirPath() + "/recipes";
            QString recipeDir = currentRecipeName.isEmpty() ? "default" : currentRecipeName;
            QString weightsPath = recipesDir + "/" + recipeDir + "/weights/" + pattern->name;
            QString trtFile = weightsPath + "/" + pattern->name + ".trt";
            QString onnxFile = weightsPath + "/" + pattern->name + ".onnx";
            bool isTrained = QFile::exists(trtFile) || QFile::exists(onnxFile);
            
            QTableWidgetItem *trainedItem = new QTableWidgetItem(isTrained ? "✓ 학습됨" : "미학습");
            trainedItem->setTextAlignment(Qt::AlignCenter);
            if (isTrained) {
                trainedItem->setForeground(QBrush(QColor("#4CAF50")));
                trainedItem->setFont(QFont("", -1, QFont::Bold));
            } else {
                trainedItem->setForeground(QBrush(QColor("#999999")));
            }
            patternTableWidget->setItem(apcRow, 2, trainedItem);
            
            patternCheckBoxes[pattern->name] = checkBox;
            
            connect(checkBox, &QCheckBox::stateChanged, [this](int) {
                bool anyChecked = false;
                bool allChecked = true;
                for (auto checkbox : patternCheckBoxes) {
                    if (checkbox->isChecked()) anyChecked = true;
                    else allChecked = false;
                }
                
                if (selectAllCheckBox) {
                    selectAllCheckBox->blockSignals(true);
                    if (allChecked && !patternCheckBoxes.isEmpty()) {
                        selectAllCheckBox->setCheckState(Qt::Checked);
                    } else if (!anyChecked) {
                        selectAllCheckBox->setCheckState(Qt::Unchecked);
                    } else {
                        selectAllCheckBox->setCheckState(Qt::PartiallyChecked);
                    }
                    selectAllCheckBox->blockSignals(false);
                }
                
                bool hasImages = !commonImages.isEmpty();
                autoTrainButton->setEnabled(anyChecked && hasImages);
            });
            
            apcRow++;
        }
    }
    
    // A-PD 패턴 추가
    int apdRow = 0;
    for (PatternInfo* pattern : patterns) {
        if (pattern && pattern->type == PatternType::INS &&
            pattern->inspectionMethod == InspectionMethod::A_PD) {
            
            padimPatternTableWidget->insertRow(apdRow);
            
            // 체크박스
            QCheckBox *checkBox = new QCheckBox();
            checkBox->setStyleSheet(
                "QCheckBox::indicator:checked { background-color: #4CAF50; border: 2px solid #4CAF50; }"
                "QCheckBox::indicator:unchecked { background-color: white; border: 2px solid #CCCCCC; }"
                "QCheckBox::indicator { width: 18px; height: 18px; }"
            );
            QWidget *checkWidget = new QWidget();
            QHBoxLayout *checkLayout = new QHBoxLayout(checkWidget);
            checkLayout->addWidget(checkBox);
            checkLayout->setAlignment(Qt::AlignCenter);
            checkLayout->setContentsMargins(0, 0, 0, 0);
            padimPatternTableWidget->setCellWidget(apdRow, 0, checkWidget);
            
            // 패턴명
            QTableWidgetItem *nameItem = new QTableWidgetItem(pattern->name);
            nameItem->setData(Qt::UserRole, pattern->name);
            nameItem->setTextAlignment(Qt::AlignCenter);
            padimPatternTableWidget->setItem(apdRow, 1, nameItem);
            
            // 학습여부 확인 (TensorRT 또는 ONNX)
            QString recipesDir = QCoreApplication::applicationDirPath() + "/recipes";
            QString recipeDir = currentRecipeName.isEmpty() ? "default" : currentRecipeName;
            QString weightsPath = recipesDir + "/" + recipeDir + "/weights/" + pattern->name;
            QString trtFile = weightsPath + "/" + pattern->name + "_padim.trt";
            QString onnxFile = weightsPath + "/" + pattern->name + "_padim.onnx";
            bool isTrained = QFile::exists(trtFile) || QFile::exists(onnxFile);
            
            QTableWidgetItem *trainedItem = new QTableWidgetItem(isTrained ? "✓ 학습됨" : "미학습");
            trainedItem->setTextAlignment(Qt::AlignCenter);
            if (isTrained) {
                trainedItem->setForeground(QBrush(QColor("#4CAF50")));
                trainedItem->setFont(QFont("", -1, QFont::Bold));
            } else {
                trainedItem->setForeground(QBrush(QColor("#999999")));
            }
            padimPatternTableWidget->setItem(apdRow, 2, trainedItem);
            
            padimPatternCheckBoxes[pattern->name] = checkBox;
            
            connect(checkBox, &QCheckBox::stateChanged, [this](int) {
                bool anyChecked = false;
                bool allChecked = true;
                for (auto checkbox : padimPatternCheckBoxes) {
                    if (checkbox->isChecked()) anyChecked = true;
                    else allChecked = false;
                }
                
                if (padimSelectAllCheckBox) {
                    padimSelectAllCheckBox->blockSignals(true);
                    if (allChecked && !padimPatternCheckBoxes.isEmpty()) {
                        padimSelectAllCheckBox->setCheckState(Qt::Checked);
                    } else if (!anyChecked) {
                        padimSelectAllCheckBox->setCheckState(Qt::Unchecked);
                    } else {
                        padimSelectAllCheckBox->setCheckState(Qt::PartiallyChecked);
                    }
                    padimSelectAllCheckBox->blockSignals(false);
                }
                
                bool hasImages = !commonImages.isEmpty();
                autoTrainButton->setEnabled(anyChecked && hasImages);
            });
            
            apdRow++;
        }
    }
    
    patternTableWidget->resizeRowsToContents();
    padimPatternTableWidget->resizeRowsToContents();
    
    if (patternTableWidget->rowCount() == 0 && padimPatternTableWidget->rowCount() == 0) {
        autoTrainButton->setEnabled(false);
    }
}

void TrainDialog::setAllPatterns(const QVector<PatternInfo*>& patterns)
{
    allPatterns = patterns;
    // 전체 패턴 설정 (로그 제거)
}

void TrainDialog::setCurrentRecipeName(const QString& recipeName)
{
    currentRecipeName = recipeName;
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
    int currentRow = patternTableWidget->currentRow();
    if (currentRow < 0) {
        currentSelectedPattern.clear();
        return;
    }
    
    QTableWidgetItem *nameItem = patternTableWidget->item(currentRow, 1);
    if (!nameItem) {
        currentSelectedPattern.clear();
        return;
    }
    
    QString patternName = nameItem->data(Qt::UserRole).toString();
    currentSelectedPattern = patternName;
    
    // 공용 이미지 개수 업데이트 (모든 패턴이 동일한 이미지 사용)
    imageCountLabel->setText(QString("이미지 개수: %1").arg(commonImages.size()));
    
    // 패턴 이미지를 메인 이미지창에 표시
    updateTeachingImagePreview();
}

void TrainDialog::onPadimPatternSelectionChanged()
{
    int currentRow = padimPatternTableWidget->currentRow();
    if (currentRow < 0) {
        currentSelectedPattern.clear();
        return;
    }
    
    QTableWidgetItem *nameItem = padimPatternTableWidget->item(currentRow, 1);
    if (!nameItem) {
        currentSelectedPattern.clear();
        return;
    }
    
    QString patternName = nameItem->data(Qt::UserRole).toString();
    currentSelectedPattern = patternName;
    
    // 공용 이미지 개수 업데이트 (모든 패턴이 동일한 이미지 사용)
    imageCountLabel->setText(QString("이미지 개수: %1").arg(commonImages.size()));
    
    // 패턴 이미지를 메인 이미지창에 표시
    updatePadimTeachingImagePreview();
}

void TrainDialog::updateTeachingImagePreview()
{
    int currentRow = patternTableWidget->currentRow();
    if (currentRow < 0) {
        previewImageLabel->setText("패턴을 선택하세요");
        return;
    }
    
    QTableWidgetItem *nameItem = patternTableWidget->item(currentRow, 1);
    if (!nameItem) {
        previewImageLabel->setText("패턴을 선택하세요");
        return;
    }
    
    QString patternName = nameItem->data(Qt::UserRole).toString();
    if (patternName.isEmpty()) {
        previewImageLabel->setText("유효하지 않은 패턴");
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
        previewImageLabel->setText("패턴을 찾을 수 없음");
        return;
    }
    
    // 템플릿 이미지 가져오기
    QImage templateImage = selectedPattern->templateImage;
    
    if (templateImage.isNull()) {
        previewImageLabel->setText("티칭 이미지 없음");
        qDebug() << "[updateTeachingImagePreview] 템플릿 이미지 없음 - 패턴:" << patternName 
                 << "frameIndex:" << selectedPattern->frameIndex;
        return;
    }
    
    // 레이블 크기에 맞게 이미지 스케일링
    QPixmap pixmap = QPixmap::fromImage(templateImage);
    QPixmap scaled = pixmap.scaled(previewImageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    previewImageLabel->setPixmap(scaled);
}

void TrainDialog::updatePadimTeachingImagePreview()
{
    int currentRow = padimPatternTableWidget->currentRow();
    if (currentRow < 0) {
        previewImageLabel->setText("패턴을 선택하세요");
        return;
    }
    
    QTableWidgetItem *nameItem = padimPatternTableWidget->item(currentRow, 1);
    if (!nameItem) {
        previewImageLabel->setText("패턴을 선택하세요");
        return;
    }
    
    QString patternName = nameItem->data(Qt::UserRole).toString();
    if (patternName.isEmpty()) {
        previewImageLabel->setText("유효하지 않은 패턴");
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
        previewImageLabel->setText("패턴을 찾을 수 없음");
        return;
    }
    
    // 템플릿 이미지 가져오기
    QImage templateImage = selectedPattern->templateImage;
    
    if (templateImage.isNull()) {
        previewImageLabel->setText("티칭 이미지 없음");
        qDebug() << "[updateTeachingImagePreview] 템플릿 이미지 없음 - 패턴:" << patternName 
                 << "frameIndex:" << selectedPattern->frameIndex;
        return;
    }
    
    // 레이블 크기에 맞게 이미지 스케일링
    QPixmap pixmap = QPixmap::fromImage(templateImage);
    QPixmap scaled = pixmap.scaled(previewImageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    previewImageLabel->setPixmap(scaled);
}

void TrainDialog::onStartAutoTrainClicked()
{
    // 체크된 패턴 목록 수집 (A-PC와 A-PD 모두)
    QStringList checkedPatterns;
    
    // A-PC 패턴 체크박스 확인
    for (auto it = patternCheckBoxes.begin(); it != patternCheckBoxes.end(); ++it) {
        if (it.value()->isChecked()) {
            checkedPatterns.append(it.key());
        }
    }
    
    // A-PD 패턴 체크박스 확인
    for (auto it = padimPatternCheckBoxes.begin(); it != padimPatternCheckBoxes.end(); ++it) {
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
            if (trainProcess && trainProcess->state() == QProcess::Running) {
                trainProcess->kill();
                trainProcess->waitForFinished(3000);
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
        
        // A-PC와 A-PD 체크박스 모두 확인
        bool anyChecked = false;
        for (auto checkbox : patternCheckBoxes) {
            if (checkbox->isChecked()) {
                anyChecked = true;
                break;
            }
        }
        if (!anyChecked) {
            for (auto checkbox : padimPatternCheckBoxes) {
                if (checkbox->isChecked()) {
                    anyChecked = true;
                    break;
                }
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
    
    // 다이얼로그를 열 때마다 학습 여부 갱신
    updateTrainingStatus();
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
        
        // 패턴 목록의 학습 상태 갱신
        for (int row = 0; row < patternTableWidget->rowCount(); ++row) {
            QTableWidgetItem* nameItem = patternTableWidget->item(row, 1);
            if (!nameItem) continue;
            
            QString patternName = nameItem->data(Qt::UserRole).toString();
            
            // 학습여부 확인 (현재 레시피의 weights 폴더 체크)
            QString recipesDir = QCoreApplication::applicationDirPath() + "/recipes";
            QString recipeDir = currentRecipeName.isEmpty() ? "default" : currentRecipeName;
            QString weightsPath = recipesDir + "/" + recipeDir + "/weights/" + patternName;
            bool isTrained = QDir(weightsPath).exists();
            
            // 학습여부 셀 업데이트
            QTableWidgetItem* statusItem = patternTableWidget->item(row, 2);
            if (statusItem) {
                statusItem->setText(isTrained ? "✓ 학습됨" : "미학습");
                if (isTrained) {
                    statusItem->setForeground(QBrush(QColor("#4CAF50")));
                    statusItem->setFont(QFont("", -1, QFont::Bold));
                } else {
                    statusItem->setForeground(QBrush(QColor("#999999")));
                }
            }
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
    
    // JETSON GPU 메모리 정리 (학습 전 필수)
#ifdef USE_TENSORRT
    qDebug() << "[TRAIN] GPU 메모리 정리 중...";
    ImageProcessor::releasePatchCoreTensorRT();
#endif
    
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
    tempTrainingDir = QCoreApplication::applicationDirPath() + QString("/data/train/temp_%1_%2")
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
    
    // 트레이닝 오버레이 숨기기 (이미지 표시를 위해)
    if (trainingOverlay) {
        trainingOverlay->hide();
    }
    
    for (int i = 0; i < commonImages.size(); ++i) {
        cv::Mat image = commonImages[i];
        if (image.empty()) continue;
        
        // BGR을 RGB로 변환하여 표시용 이미지 생성
        cv::Mat displayImage = image.clone();
        
        int finalRoiX = roiX, finalRoiY = roiY;
        
        if (useFidMatching && !fidTemplate.empty()) {
            // 부모 ROI 전체 영역에서 FID 패턴 검색 (실시간 검사와 동일)
            // FID 패턴의 중심이 포함된 ROI 찾기 (실시간 코드와 동일)
            cv::Rect searchROI;
            QPoint fidCenter = QPoint(
                static_cast<int>(parentFidPattern->rect.center().x()),
                static_cast<int>(parentFidPattern->rect.center().y()));
            
            for (PatternInfo* p : allPatterns) {
                if (p->type == PatternType::ROI && p->enabled) {
                    // FID 패턴이 이 ROI 내부에 있는지 확인 (중심점 기준)
                    if (p->rect.contains(fidCenter)) {
                        searchROI = cv::Rect(
                            static_cast<int>(p->rect.x()),
                            static_cast<int>(p->rect.y()),
                            static_cast<int>(p->rect.width()),
                            static_cast<int>(p->rect.height())
                        );
                        break; // 첫 번째로 찾은 포함하는 ROI 사용
                    }
                }
            }
            
            // ROI를 못 찾으면 FID 패턴 주변 영역 사용 (실시간 코드와 동일)
            if (searchROI.area() == 0) {
                int margin = std::max(static_cast<int>(parentFidPattern->rect.width()), 
                                     static_cast<int>(parentFidPattern->rect.height()));
                searchROI = cv::Rect(
                    std::max(0, static_cast<int>(parentFidPattern->rect.x()) - margin),
                    std::max(0, static_cast<int>(parentFidPattern->rect.y()) - margin),
                    std::min(image.cols - std::max(0, static_cast<int>(parentFidPattern->rect.x()) - margin),
                            static_cast<int>(parentFidPattern->rect.width()) + 2 * margin),
                    std::min(image.rows - std::max(0, static_cast<int>(parentFidPattern->rect.y()) - margin),
                            static_cast<int>(parentFidPattern->rect.height()) + 2 * margin));
            }
            
            int fidRoiX = searchROI.x;
            int fidRoiY = searchROI.y;
            int fidRoiW = searchROI.width;
            int fidRoiH = searchROI.height;
            
            // 이미지 범위 체크
            if (fidRoiX < 0) fidRoiX = 0;
            if (fidRoiY < 0) fidRoiY = 0;
            if (fidRoiX + fidRoiW > image.cols) fidRoiW = image.cols - fidRoiX;
            if (fidRoiY + fidRoiH > image.rows) fidRoiH = image.rows - fidRoiY;
            
            // 검색 영역 추출
            cv::Mat searchRegion = image(cv::Rect(fidRoiX, fidRoiY, fidRoiW, fidRoiH));
            
            // 템플릿 크기 검증
            if (fidTemplate.cols > searchRegion.cols || fidTemplate.rows > searchRegion.rows) {
                fidMatchFailCount++;
                qDebug() << QString("[TRAIN] FID template too large - skip: %1 (template:%2x%3, search:%4x%5)")
                    .arg(i).arg(fidTemplate.cols).arg(fidTemplate.rows).arg(searchRegion.cols).arg(searchRegion.rows);
                continue;
            }
            
            // 색상 채널 맞추기 (grayscale로 변환하여 매칭)
            cv::Mat searchGray, templateGray;
            if (searchRegion.channels() == 3) {
                cv::cvtColor(searchRegion, searchGray, cv::COLOR_BGR2GRAY);
            } else {
                searchGray = searchRegion;
            }
            
            if (fidTemplate.channels() == 3) {
                cv::cvtColor(fidTemplate, templateGray, cv::COLOR_BGR2GRAY);
            } else {
                templateGray = fidTemplate;
            }
            
            cv::Mat result;
            // 레시피의 FID 패턴 설정 사용
            int matchMethod = (parentFidPattern->fidMatchMethod == 0) ? cv::TM_CCOEFF_NORMED : cv::TM_CCORR_NORMED;
            double matchThreshold = parentFidPattern->matchThreshold / 100.0;  // 퍼센트를 0~1로 변환
            
            if (!fidMask.empty()) {
                cv::matchTemplate(searchGray, templateGray, result, matchMethod, fidMask);
            } else {
                cv::matchTemplate(searchGray, templateGray, result, matchMethod);
            }
            
            double minVal, maxVal;
            cv::Point minLoc, maxLoc;
            cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
            
            // 시각화: 검색 영역 (파란색)
            cv::rectangle(displayImage, cv::Rect(fidRoiX, fidRoiY, fidRoiW, fidRoiH), 
                         cv::Scalar(255, 0, 0), 3);
            
            // 시각화: FID 매칭 위치 (녹색=성공, 빨간색=실패)
            int matchX = fidRoiX + maxLoc.x;
            int matchY = fidRoiY + maxLoc.y;
            cv::Scalar matchColor = (maxVal >= matchThreshold) ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
            cv::rectangle(displayImage, cv::Rect(matchX, matchY, fidTemplate.cols, fidTemplate.rows),
                         matchColor, 3);
            
            // 매칭 점수 텍스트
            QString scoreText = QString("FID: %1%").arg(maxVal * 100.0, 0, 'f', 1);
            cv::putText(displayImage, scoreText.toStdString(), cv::Point(matchX, matchY - 10),
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, matchColor, 2);
            
            // FID 매칭 성공 시 상대 좌표로 계산하여 INS 패턴 영역 align
            if (maxVal >= matchThreshold) {
                // 검색 영역 내 좌표를 전체 이미지 좌표로 변환
                double fidMatchCenterX = fidRoiX + maxLoc.x + fidTemplate.cols / 2.0;
                double fidMatchCenterY = fidRoiY + maxLoc.y + fidTemplate.rows / 2.0;
                
                // 티칭 시의 상대 좌표 계산
                double relativeX = insTeachingCenter.x() - fidTeachingCenter.x();
                double relativeY = insTeachingCenter.y() - fidTeachingCenter.y();
                
                // FID 매칭 위치 기준으로 INS 패턴 위치 계산 (align)
                double newInsCenterX = fidMatchCenterX + relativeX;
                double newInsCenterY = fidMatchCenterY + relativeY;
                
                finalRoiX = static_cast<int>(newInsCenterX - roiW / 2.0);
                finalRoiY = static_cast<int>(newInsCenterY - roiH / 2.0);
                
                // 시각화: INS 패턴 영역 (노란색)
                cv::rectangle(displayImage, cv::Rect(finalRoiX, finalRoiY, roiW, roiH),
                             cv::Scalar(0, 255, 255), 3);
                cv::putText(displayImage, targetPattern->name.toStdString(), 
                           cv::Point(finalRoiX, finalRoiY - 10),
                           cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 2);
            } else {
                // FID 매칭 실패 시 건너뜀
                fidMatchFailCount++;
                qDebug() << QString("[TRAIN] FID match failed - skip: %1 (score=%2%, threshold=%3%)")
                    .arg(i).arg(maxVal * 100.0, 0, 'f', 1).arg(matchThreshold * 100.0, 0, 'f', 1);
                
                // 실패 이미지도 표시
                cv::cvtColor(displayImage, displayImage, cv::COLOR_BGR2RGB);
                QImage qImg(displayImage.data, displayImage.cols, displayImage.rows,
                           displayImage.step, QImage::Format_RGB888);
                previewImageLabel->setPixmap(QPixmap::fromImage(qImg.copy()));
                QApplication::processEvents();
                continue;
            }
        } else {
            // FID 매칭 없이 고정 좌표 사용 - INS 영역 표시
            cv::rectangle(displayImage, cv::Rect(finalRoiX, finalRoiY, roiW, roiH),
                         cv::Scalar(0, 255, 255), 3);
        }
        
        // 이미지를 previewImageLabel에 표시
        cv::cvtColor(displayImage, displayImage, cv::COLOR_BGR2RGB);
        QImage qImg(displayImage.data, displayImage.cols, displayImage.rows,
                   displayImage.step, QImage::Format_RGB888);
        previewImageLabel->setPixmap(QPixmap::fromImage(qImg.copy()));
        QApplication::processEvents();
        
        // 경계 조정 (ROI 영역 체크 제거 - INS는 ROI와 무관하게 크롭)
        // 이미지 범위를 벗어나면 경계에 맞춰 조정
        if (finalRoiX < 0) finalRoiX = 0;
        if (finalRoiY < 0) finalRoiY = 0;
        if (finalRoiX + roiW > image.cols) {
            if (image.cols >= roiW) {
                finalRoiX = image.cols - roiW;
            } else {
                qDebug() << "[TRAIN] 이미지 너비가 ROI보다 작음 - 건너뜀:" << image.cols << "<" << roiW;
                continue;
            }
        }
        if (finalRoiY + roiH > image.rows) {
            if (image.rows >= roiH) {
                finalRoiY = image.rows - roiH;
            } else {
                qDebug() << "[TRAIN] 이미지 높이가 ROI보다 작음 - 건너뜀:" << image.rows << "<" << roiH;
                continue;
            }
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
    
    // 학습 실행 - 레시피별 weights 폴더
    QString recipesDir = QCoreApplication::applicationDirPath() + "/recipes";
    QString recipeDir = currentRecipeName.isEmpty() ? "default" : currentRecipeName;
    QString weightsBaseDir = recipesDir + "/" + recipeDir + "/weights";
    QString outputDir = weightsBaseDir + "/" + patternName;
    
    // 기존 모델 디렉토리 삭제 후 재생성 (깨끗한 학습)
    QDir outputDirObj(outputDir);
    if (outputDirObj.exists()) {
        qDebug() << "[TRAIN] 기존 모델 디렉토리 삭제:" << outputDir;
        outputDirObj.removeRecursively();
    }
    QDir().mkpath(outputDir);
    
    // 로컬 Python 직접 실행 (JETSON 및 x86 공통)
    // python3 절대 경로 찾기
    QProcess whichProcess;
    whichProcess.start("which", QStringList() << "python3");
    whichProcess.waitForFinished();
    QString trainScript = QString(whichProcess.readAllStandardOutput()).trimmed();
    
    if (trainScript.isEmpty()) {
        trainScript = "/usr/bin/python3";  // fallback
    }
    
    QStringList args;
    
    // 패턴의 검사 방법에 따라 다른 스크립트 사용
    QString scriptPath;
    if (targetPattern->inspectionMethod == InspectionMethod::A_PC) {
        scriptPath = QCoreApplication::applicationDirPath() + "/train_patchcore_anomalib.py";
    } else if (targetPattern->inspectionMethod == InspectionMethod::A_PD) {
        scriptPath = QCoreApplication::applicationDirPath() + "/train_padim_anomalib.py";
    } else {
        qWarning() << "[TRAIN] 알 수 없는 검사 방법:" << targetPattern->inspectionMethod;
        trainNextPattern();
        return;
    }
    
    args << "-u";  // unbuffered output
    args << "-W" << "ignore";  // Python 워닝 메시지 숨기기
    args << scriptPath;
    args << "--data-dir" << tempTrainingDir;
    args << "--output" << outputDir;
    args << "--pattern-name" << patternName;
    
    // 모델별 옵션 추가
    if (targetPattern->inspectionMethod == InspectionMethod::A_PC) {
        // PatchCore 옵션
        if (backboneComboBox && coresetRatioSpinBox && numNeighborsSpinBox) {
            args << "--backbone" << backboneComboBox->currentText();
            args << "--coreset-ratio" << QString::number(coresetRatioSpinBox->value(), 'g');
            args << "--num-neighbors" << QString::number(numNeighborsSpinBox->value());
        }
    } else if (targetPattern->inspectionMethod == InspectionMethod::A_PD) {
        // PaDiM 옵션
        if (padimBackboneComboBox && padimNumLayersSpinBox) {
            args << "--backbone" << padimBackboneComboBox->currentText();
            args << "--n-features" << QString::number(padimNumLayersSpinBox->value());
        }
    }
    
    QString modelType = (targetPattern->inspectionMethod == InspectionMethod::A_PC) ? "PatchCore" : "PaDiM";
    qDebug() << "[TRAIN] 로컬 학습 시작 (" << modelType << "):" << trainScript << args;
    
    updateTrainingProgress(QString("%1 Training model '%2'...%3")
        .arg(getPatternProgressString()).arg(patternName).arg(getTotalTimeString()));
    
    // 기존 프로세스 정리
    if (trainProcess) {
        if (trainProcess->state() == QProcess::Running) {
            trainProcess->kill();
            trainProcess->waitForFinished(3000);
        }
        delete trainProcess;
    }
    
    trainProcess = new QProcess(this);
    
    // 작업 디렉토리 설정 (절대 경로 사용)
    QString workingDir = QCoreApplication::applicationDirPath();
    trainProcess->setWorkingDirectory(workingDir);
    
    // Python 환경 변수 설정
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    
    // HOME 디렉토리가 없으면 설정 (일부 시스템 서비스에서 필요)
    if (!env.contains("HOME")) {
        env.insert("HOME", QDir::homePath());
    }
    
    // PYTHONUNBUFFERED 설정으로 출력 버퍼링 방지
    env.insert("PYTHONUNBUFFERED", "1");
    
    // PYTHONPATH에 현재 디렉토리 추가
    QString pythonPath = workingDir;
    if (env.contains("PYTHONPATH")) {
        pythonPath += ":" + env.value("PYTHONPATH");
    }
    env.insert("PYTHONPATH", pythonPath);
    
    trainProcess->setProcessEnvironment(env);
    
    // 표준 출력과 에러를 분리해서 캡처
    trainProcess->setProcessChannelMode(QProcess::SeparateChannels);
    
    connect(trainProcess, &QProcess::readyReadStandardOutput, this, &TrainDialog::onTrainOutputReady);
    connect(trainProcess, &QProcess::readyReadStandardError, this, [this]() {
        if (trainProcess) {
            QString errorOutput = trainProcess->readAllStandardError();
            if (!errorOutput.trimmed().isEmpty()) {
                QString trimmed = errorOutput.trimmed();
                
                // Jetson Orin에서 정상적인 ONNX Runtime 및 TensorRT 경고 필터링
                bool isNormalWarning = 
                    trimmed.contains("device_discovery.cc") ||  // ONNX Runtime GPU discovery warning
                    trimmed.contains("/sys/class/drm/card") ||  // DRM device path warning
                    trimmed.contains("DiscoverDevicesForPlatform") ||
                    trimmed.contains("[W:onnxruntime:") ||  // ONNX Runtime warnings
                    trimmed.contains("TensorRT") && trimmed.contains("warning") ||  // TensorRT warnings
                    trimmed.contains("INFO") || trimmed.contains("WARNING") ||
                    trimmed.contains("Processing");
                
                // 진짜 에러만 출력 (정상 경고는 제외)
                if (!isNormalWarning && (
                    trimmed.contains("ERROR") || trimmed.contains("Error") || 
                    trimmed.contains("FAILED") || trimmed.contains("Failed") ||
                    trimmed.contains("Exception") || trimmed.contains("Traceback"))) {
                    qWarning() << "[TRAIN ERROR]" << errorOutput;
                }
            }
        }
    });
    connect(trainProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
            this, &TrainDialog::onTrainFinished);
    
    trainProcess->start(trainScript, args);
    
    // 프로세스 시작 실패 확인
    if (!trainProcess->waitForStarted(5000)) {
        QString errorMsg = QString("Failed to start Python process: %1\nError: %2")
            .arg(trainScript).arg(trainProcess->errorString());
        qCritical() << "[TRAIN]" << errorMsg;
        
        CustomMessageBox msgBox(this, CustomMessageBox::Critical, "학습 시작 실패", errorMsg);
        msgBox.exec();
        
        trainNextPattern();
        return;
    }
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

void TrainDialog::onTrainOutputReady()
{
    if (!trainProcess) return;
    
    QString output = trainProcess->readAllStandardOutput();
    
    // 출력 문자열 정리
    output = output.trimmed();
    
    // 빈 줄이면 무시
    if (output.isEmpty()) {
        return;
    }
    
    // 단순 줄바꿈만 있는 경우 무시
    if (output == "\n" || output == "\r\n" || output == "\r") {
        return;
    }
    
    // Traceback 필터링 - 에러만 간단하게 표시
    if (output.contains("Traceback (most recent call last):")) {
        // 실제 에러 메시지만 추출 (Error:, Exception: 등으로 시작하는 줄)
        QStringList lines = output.split('\n');
        for (const QString& line : lines) {
            QString trimmed = line.trimmed();
            if (trimmed.contains("Error:") || trimmed.contains("Exception:")) {
                qDebug() << "[TRAIN]" << trimmed;
                return;
            }
        }
        // 에러 타입을 못 찾으면 아무것도 출력 안 함
        return;
    }
    
    // Python 스크립트 출력은 진행바로만 표시 (디버그 로그 제거)
    
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
    
    // ONNX 변환 진행률 파싱
    if (output.contains("Exporting to ONNX")) {
        updateTrainingProgress(QString("%1 Training '%2'... ONNX%3")
            .arg(patternProgress).arg(currentTrainingPattern).arg(totalElapsedStr));
        return;
    }
    if (output.contains("ONNX exported")) {
        // ONNX 완료 시 로그 생략, TensorRT 변환 대기
        return;
    }
    
    // TensorRT 변환 진행률 파싱
    if (output.contains("Converting to TensorRT")) {
        updateTrainingProgress(QString("%1 Training '%2'... TensorRT%3")
            .arg(patternProgress).arg(currentTrainingPattern).arg(totalElapsedStr));
        return;
    }
    if (output.contains("TensorRT engine created")) {
        // TensorRT 완료 시 로그 생략
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
    
    // 기타 출력은 로그로만 남김 (중복 메시지 방지)
}

void TrainDialog::onTrainFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug() << "[TRAIN] 학습 프로세스 종료: exitCode=" << exitCode << ", status=" << exitStatus;
    
    // 프로세스 명시적 종료 및 메모리 해제
    if (trainProcess) {
        if (trainProcess->state() == QProcess::Running) {
            qDebug() << "[TRAIN] 프로세스가 아직 실행 중, 강제 종료...";
            trainProcess->kill();
            trainProcess->waitForFinished(3000);
        }
        
        // 프로세스 객체 삭제로 리소스 해제
        trainProcess->deleteLater();
        trainProcess = nullptr;
    }
    
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
    
    // weights 출력 폴더 정리 (bin, xml, 패턴명(정규화통계)만 남기고 삭제)
    if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
        QString recipesDir = QCoreApplication::applicationDirPath() + "/recipes";
        QString recipeDir = currentRecipeName.isEmpty() ? "default" : currentRecipeName;
        QString weightsDir = recipesDir + "/" + recipeDir + "/weights/" + currentTrainingPattern;
        QDir outputDir(weightsDir);
        if (outputDir.exists()) {
            // 삭제할 폴더: Patchcore/, temp_dataset/ (PyTorch 모델은 유지)
            QStringList itemsToRemove;
            itemsToRemove << "Patchcore" << "temp_dataset";
            
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

void TrainDialog::updateTrainingStatus()
{
    // 레시피가 없으면 갱신하지 않음
    if (currentRecipeName.isEmpty()) {
        return;
    }
    
    QString recipesDir = QCoreApplication::applicationDirPath() + "/recipes";
    QString recipeDir = currentRecipeName;
    
    // 테이블의 각 행을 순회하며 학습 여부 갱신
    for (int row = 0; row < patternTableWidget->rowCount(); ++row) {
        QTableWidgetItem *nameItem = patternTableWidget->item(row, 1);
        if (!nameItem) continue;
        
        QString patternName = nameItem->data(Qt::UserRole).toString();
        QString weightsPath = recipesDir + "/" + recipeDir + "/weights/" + patternName;
        QString modelFile = weightsPath + "/" + patternName + ".trt";
        
        bool isTrained = QFile::exists(modelFile);
        
        // 학습여부 컬럼 업데이트
        QTableWidgetItem *trainedItem = patternTableWidget->item(row, 2);
        if (trainedItem) {
            trainedItem->setText(isTrained ? "✓ 학습됨" : "미학습");
            
            if (isTrained) {
                trainedItem->setForeground(QBrush(QColor("#4CAF50")));
                trainedItem->setFont(QFont("", -1, QFont::Bold));
            } else {
                trainedItem->setForeground(QBrush(QColor("#999999")));
                trainedItem->setFont(QFont());
            }
        }
    }
}
