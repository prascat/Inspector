#include "SimulationDialog.h"
#include "TeachingWidget.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QListWidget>
#include <QProgressDialog>
#include <QSplitter>
#include <QTextEdit>
#include <QGroupBox>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonDocument>
#include <QDir>
#include <QDateTime>
#include <QTimer>
#include <QRandomGenerator>
#include <QCloseEvent>
#include "CommonDefs.h"
#include "RecipeManager.h"
#include "TeachingWidget.h"

SimulationDialog::SimulationDialog(TeachingWidget *parentWidget)
    : QDialog(qobject_cast<QWidget*>(parentWidget)), currentIndex(-1), teachingImageIndex(-1), 
      aiTrainer(nullptr), teachingWidget(parentWidget)
{
    setWindowTitle("시뮬레이션 모드 - 비전 티칭 레시피 관리");
    setModal(false); // 비모달로 설정하여 메인 창과 동시 사용 가능
    resize(1280, 800); // 티칭위젯과 동일한 크기
    setMinimumSize(1280, 800); // 최소 크기도 동일하게
    
    // AI 이상 탐지 학습기 초기화
    aiTrainer = new AITrainer(this);
    connect(aiTrainer, &AITrainer::trainingProgress, 
            this, &SimulationDialog::onTrainingProgress);
    connect(aiTrainer, &AITrainer::trainingCompleted,
            this, &SimulationDialog::onTrainingCompleted);
    
    setupUI();
    updateControls();
    
    // Docker 상태 초기 확인
    QTimer::singleShot(500, this, &SimulationDialog::refreshDockerStatus);
}

void SimulationDialog::onTrainingProgress(int percentage, const QString& message)
{
    // 프로그레스바 표시 및 업데이트
    trainingProgressBar->setVisible(true);
    trainingProgressBar->setValue(percentage);
    trainingProgressBar->setFormat(QString("%1% - %2").arg(percentage).arg(message));
    
    // 버튼 텍스트도 업데이트
    trainButton->setText(QString("학습중 %1%").arg(percentage));
    qDebug() << "AI 학습 진행:" << percentage << "%" << message;
}

void SimulationDialog::onTrainingCompleted(bool success, const QString& message)
{
    // 프로그레스바 숨김
    trainingProgressBar->setVisible(false);
    
    // 버튼 상태 복원
    trainButton->setText(TR("TRAINING"));
    trainButton->setEnabled(true);
    
    if (success) {
        QMessageBox::information(
            this,
            "AI 학습 완료",
            QString("AI 이상 탐지 모델 학습이 완료되었습니다!\n\n%1\n\n"
                    "이제 테스트 이미지에서 이상 패턴을 탐지할 수 있습니다.")
                .arg(message)
        );
        
        // 학습 완료 후 버튼 색상을 다르게 표시 (학습된 상태 표시)
        trainButton->setStyleSheet(
            "QPushButton {"
            "    background-color: #20c997;"  // 다른 초록색 (학습 완료)
            "    color: white;"
            "    border: none;"
            "    border-radius: 4px;"
            "    font-weight: bold;"
            "    padding: 5px 10px;"
            "}"
            "QPushButton:hover {"
            "    background-color: #1ba085;"
            "}"
            "QPushButton:pressed {"
            "    background-color: #17a673;"
            "}"
        );
        
        // 학습 완료 후 결과 버튼 활성화
        trainResultsButton->setEnabled(true);
        
        // 이상 탐지 버튼 활성화
        detectButton->setEnabled(true);
        
    } else {
        QMessageBox::critical(
            this,
            "AI 학습 실패",
            QString("AI 이상 탐지 모델 학습에 실패했습니다.\n\n오류: %1")
                .arg(message)
        );
    }
}

void SimulationDialog::onTrainingButtonClicked()
{
    if (trainingImagePaths.isEmpty()) {
        QMessageBox::warning(this, "경고", "학습할 이미지가 없습니다. 먼저 학습 이미지를 추가해주세요.");
        return;
    }
    
    // PatchCore 모델을 위한 최소 이미지 수 체크
    const int MIN_IMAGES_FOR_PATCHCORE = 10;
    if (trainingImagePaths.size() < MIN_IMAGES_FOR_PATCHCORE) {
        QMessageBox::warning(this, "이미지 부족", 
            QString("AI 학습을 위해서는 최소 %1장의 이미지가 필요합니다.\n\n"
                    "현재 이미지 수: %2장\n"
                    "부족한 이미지 수: %3장\n\n"
                    "더 많은 이미지를 추가한 후 학습을 진행해주세요.")
                .arg(MIN_IMAGES_FOR_PATCHCORE)
                .arg(trainingImagePaths.size())
                .arg(MIN_IMAGES_FOR_PATCHCORE - trainingImagePaths.size()));
        return;
    }
    
    if (currentRecipeName.isEmpty()) {
        QMessageBox::warning(this, "경고", "레시피가 선택되지 않았습니다. 먼저 레시피를 생성하거나 선택해주세요.");
        return;
    }
    
    // 학습 비율 슬라이더에서 값 가져오기
    int trainRatio = trainRatioSlider->value();
    int testRatio = 100 - trainRatio;
    
    // 모델 디렉토리와 파일 경로 설정 (실행 파일 기준)
    QString modelDir = QString("models/%1").arg(currentRecipeName);
    QString modelPath = QString("%1/model.ckpt").arg(modelDir);
    
    // 기존 모델 파일 체크
    QDir dir;
    if (QFile::exists(modelPath)) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            "기존 모델 발견",
            QString("레시피 '%1'에 대한 학습된 모델이 이미 존재합니다.\n\n"
                    "모델 파일: %2\n\n"
                    "기존 모델을 덮어쓰시겠습니까?")
                .arg(currentRecipeName)
                .arg(modelPath),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        
        if (reply == QMessageBox::No) {
            return;
        }
    }
    
    // 바로 학습 시작 - 확인 다이얼로그 제거
    {
        // 데이터셋 폴더 구조 생성
        QString dataDir = QString("recipes/%1").arg(currentRecipeName);
        QString trainDir = QString("%1/train/good").arg(dataDir);
        QString testDir = QString("%1/test/good").arg(dataDir);
        
        // 데이터셋 디렉토리 생성
        if (!dir.mkpath(trainDir) || !dir.mkpath(testDir)) {
            QMessageBox::critical(this, "오류", 
                QString("데이터셋 디렉토리를 생성할 수 없습니다: %1").arg(dataDir));
            return;
        }
        
        // 모델 디렉토리 생성
        if (!dir.mkpath(modelDir)) {
            QMessageBox::critical(this, "오류", 
                QString("모델 디렉토리를 생성할 수 없습니다: %1").arg(modelDir));
            return;
        }
        
        // 이미지 분할 및 복사
        int totalImages = trainingImagePaths.size();
        int trainCount = (totalImages * trainRatio) / 100;
        int testCount = totalImages - trainCount;
        
        qDebug() << "데이터셋 생성:";
        qDebug() << "- 총 이미지 수:" << totalImages;
        qDebug() << "- 학습용:" << trainCount << "개 (" << trainRatio << "%)";
        qDebug() << "- 테스트용:" << testCount << "개 (" << testRatio << "%)";
        qDebug() << "- 데이터셋 경로:" << dataDir;
        
        // 학습용 이미지 복사
        for (int i = 0; i < trainCount; i++) {
            QString sourcePath = trainingImagePaths[i];
            QFileInfo fileInfo(sourcePath);
            QString destPath = QString("%1/%2_%3.%4")
                                .arg(trainDir)
                                .arg(fileInfo.baseName())
                                .arg(i, 6, 10, QChar('0'))
                                .arg(fileInfo.suffix());
            
            if (!QFile::copy(sourcePath, destPath)) {
                qWarning() << "학습 이미지 복사 실패:" << sourcePath << "->" << destPath;
            }
        }
        
        // 테스트용 이미지 복사
        for (int i = trainCount; i < totalImages; i++) {
            QString sourcePath = trainingImagePaths[i];
            QFileInfo fileInfo(sourcePath);
            QString destPath = QString("%1/%2_%3.%4")
                                .arg(testDir)
                                .arg(fileInfo.baseName())
                                .arg(i - trainCount, 6, 10, QChar('0'))
                                .arg(fileInfo.suffix());
            
            if (!QFile::copy(sourcePath, destPath)) {
                qWarning() << "테스트 이미지 복사 실패:" << sourcePath << "->" << destPath;
            }
        }
        
        qDebug() << "✅ 데이터셋 구조 생성 완료:";
        qDebug() << "📁" << trainDir << ":" << trainCount << "개 파일";
        qDebug() << "📁" << testDir << ":" << testCount << "개 파일";
        
        // 학습 설정 (사용자가 설정한 값 사용)
        AITrainer::TrainingConfig config;
        config.datasetName = currentRecipeName;
        config.resultDir = modelDir;
        config.modelPath = modelPath;     // 모델 저장 경로 명시
        config.backbone = "wide_resnet50_2";  // PatchCore에 최적화된 백본
        config.batchSize = 16;            // 배치 크기
        config.coresetRatio = 0.1;        // Coreset 샘플링 비율
        config.trainRatio = trainRatio;   // 사용자가 설정한 학습 비율
        config.testRatio = testRatio;     // 사용자가 설정한 테스트 비율
        config.datasetPath = dataDir;     // 생성된 데이터셋 경로 추가
        
        // 버튼 상태 변경
        trainButton->setEnabled(false);
        trainButton->setText(TR("TRAINING_IN_PROGRESS"));
        
        qDebug() << "AI 학습 시작:";
        qDebug() << "- 레시피:" << currentRecipeName;
        qDebug() << "- 학습 이미지 수:" << trainingImagePaths.size();
        qDebug() << "- 모델: PatchCore (1 epoch)";
        qDebug() << "- 데이터 분할:" << trainRatio << ":" << testRatio;
        qDebug() << "- 모델 저장 경로:" << modelPath;
        
        // AI 이상 탐지 학습 시작
        aiTrainer->trainModel(trainingImagePaths, config);
    }
}

SimulationDialog::~SimulationDialog()
{
}

void SimulationDialog::closeEvent(QCloseEvent *event)
{
    // 시뮬레이션 다이얼로그 닫을 때 자동으로 라이브 모드로 복귀하지 않음
    // 사용자가 수동으로 LIVE/SIM 버튼을 눌러야 함
    
    QDialog::closeEvent(event);
}

void SimulationDialog::setupUI()
{
    // 간단한 블랙 테마 스타일 설정
    setStyleSheet(R"(
        QDialog {
            background-color: #2b2b2b;
            color: #ffffff;
        }
        QListWidget {
            background-color: #3c3c3c;
            border: 1px solid #555555;
            border-radius: 4px;
            selection-background-color: #0078d4;
            color: #ffffff;
            font-size: 11px;
        }
        QListWidget::item {
            padding: 4px;
            border-bottom: 1px solid #444444;
        }
        QListWidget::item:selected {
            background-color: #0078d4;
        }
        QLabel {
            color: #ffffff;
        }
        QPushButton {
            background-color: #4a4a4a;
            color: white;
            border: 1px solid #666666;
            border-radius: 4px;
            padding: 6px 12px;
            font-size: 11px;
        }
        QPushButton:hover {
            background-color: #5a5a5a;
        }
        QPushButton:pressed {
            background-color: #3a3a3a;
        }
        QPushButton:disabled {
            background-color: #333333;
            color: #888888;
        }
        /* QMessageBox 버튼들은 기본 스타일 사용 */
        QMessageBox QPushButton {
            background-color: #0078d4;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 8px 16px;
            font-size: 12px;
            min-width: 60px;
        }
        QMessageBox QPushButton:hover {
            background-color: #106ebe;
        }
        QMessageBox QPushButton:pressed {
            background-color: #005a9e;
        }
        QGroupBox {
            font-weight: bold;
            border: 1px solid #555555;
            border-radius: 4px;
            margin-top: 8px;
            padding-top: 8px;
            color: #ffffff;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 8px;
            padding: 0 4px 0 4px;
        }
    )");

    // 메인 레이아웃
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    
    // 상단 레시피 정보
    recipeInfoLabel = new QLabel("레시피: 없음");
    recipeInfoLabel->setStyleSheet("font-weight: bold; padding: 4px; background-color: #4a4a4a; border-radius: 4px;");
    mainLayout->addWidget(recipeInfoLabel);
    
    // 메인 컨텐츠 영역 (수평 분할)
    QHBoxLayout* contentLayout = new QHBoxLayout();
    
    // 왼쪽 패널
    QWidget* leftPanel = new QWidget();
    leftPanel->setFixedWidth(320);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setSpacing(10);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    
    // 레시피 관리 버튼만 유지 (목록은 제거)
    QHBoxLayout* recipeButtonLayout = new QHBoxLayout();
    QPushButton* manageRecipeBtn = new QPushButton("레시피 관리");
    
    manageRecipeBtn->setFixedHeight(35);
    manageRecipeBtn->setStyleSheet("QPushButton { background-color: #1976d2; color: white; }");
    
    recipeButtonLayout->addWidget(manageRecipeBtn);
    leftLayout->addLayout(recipeButtonLayout);
    
    // 카메라 선택 UI 추가
    cameraSelectionLabel = new QLabel("카메라 선택");
    cameraSelectionLabel->setStyleSheet("font-weight: bold; font-size: 12px; color: #ffffff; margin-top: 10px;");
    leftLayout->addWidget(cameraSelectionLabel);
    
    cameraComboBox = new QComboBox();
    cameraComboBox->setStyleSheet(R"(
        QComboBox {
            background-color: #3c3c3c;
            border: 1px solid #555555;
            border-radius: 4px;
            color: #ffffff;
            font-size: 11px;
            padding: 4px;
        }
        QComboBox::drop-down {
            border: none;
        }
        QComboBox::down-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 4px solid #ffffff;
            margin-right: 6px;
        }
        QComboBox QAbstractItemView {
            background-color: #3c3c3c;
            border: 1px solid #555555;
            color: #ffffff;
            selection-background-color: #0078d4;
        }
    )");
    leftLayout->addWidget(cameraComboBox);
    
    // 카메라 선택 변경 시그널 연결
    connect(cameraComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SimulationDialog::onCameraSelectionChanged);
    
    // 레시피 관리 버튼 시그널 연결 (TeachingWidget의 관리 다이얼로그 호출)
    connect(manageRecipeBtn, &QPushButton::clicked, this, [this]() {
        if (teachingWidget) {
            teachingWidget->manageRecipes();
        } else {
            QMessageBox::warning(this, "오류", "TeachingWidget 참조가 없습니다.");
        }
    });
    
    // 구분선
    QFrame* line1 = new QFrame();
    line1->setFrameShape(QFrame::HLine);
    line1->setStyleSheet("QFrame { color: #555555; }");
    leftLayout->addWidget(line1);
    
    // 티칭 이미지 변경 버튼
    QHBoxLayout* imageHeaderLayout = new QHBoxLayout();
    loadImagesButton = new QPushButton("티칭 이미지 변경");
    loadImagesButton->setFixedHeight(35);
    loadImagesButton->setEnabled(true); // 항상 활성화
    loadImagesButton->setStyleSheet("QPushButton { background-color: #17a2b8; }");
    
    imageHeaderLayout->addWidget(loadImagesButton);
    leftLayout->addLayout(imageHeaderLayout);
    
    // 구분선
    QFrame* line2 = new QFrame();
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet("QFrame { color: #555555; }");
    leftLayout->addWidget(line2);
    
    // 학습 이미지 목록
    QHBoxLayout* trainingHeaderLayout = new QHBoxLayout();
    QLabel* trainingLabel = new QLabel("학습");
    trainingLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    
    loadTrainingImagesButton = new QPushButton("추가");
    loadTrainingImagesButton->setFixedSize(55, 32);
    loadTrainingImagesButton->setEnabled(false);
    loadTrainingImagesButton->setStyleSheet("QPushButton { background-color: #28a745; }");
    
    removeTrainingImageButton = new QPushButton("삭제");
    removeTrainingImageButton->setFixedSize(55, 32);
    removeTrainingImageButton->setEnabled(false);
    
    trainButton = new QPushButton("학습");
    trainButton->setFixedSize(55, 32);
    trainButton->setEnabled(false);
    trainButton->setStyleSheet("QPushButton { background-color: #ffc107; color: #000; }");
    
    trainResultsButton = new QPushButton("결과");
    trainResultsButton->setFixedSize(55, 32);
    trainResultsButton->setEnabled(false);
    trainResultsButton->setStyleSheet("QPushButton { background-color: #17a2b8; color: #fff; }");
    
    detectButton = new QPushButton("탐지");
    detectButton->setFixedSize(55, 32);
    detectButton->setStyleSheet("QPushButton { background-color: #dc3545; }");
    
    // 학습 진행률 프로그레스바
    trainingProgressBar = new QProgressBar();
    trainingProgressBar->setRange(0, 100);
    trainingProgressBar->setValue(0);
    trainingProgressBar->setVisible(false); // 처음에는 숨김
    trainingProgressBar->setStyleSheet("QProgressBar { border: 2px solid grey; border-radius: 5px; text-align: center; } QProgressBar::chunk { background-color: #4CAF50; width: 20px; }");
    
    trainingHeaderLayout->addWidget(trainingLabel);
    trainingHeaderLayout->addStretch();
    trainingHeaderLayout->addWidget(loadTrainingImagesButton);
    trainingHeaderLayout->addWidget(removeTrainingImageButton);
    trainingHeaderLayout->addWidget(trainButton);
    trainingHeaderLayout->addWidget(trainResultsButton);
    trainingHeaderLayout->addWidget(detectButton);
    
    // 학습 진행률을 별도 레이아웃에 추가
    QVBoxLayout* trainingContentLayout = new QVBoxLayout();
    trainingContentLayout->addLayout(trainingHeaderLayout);
    trainingContentLayout->addWidget(trainingProgressBar); // 프로그레스바 추가
    
    // 학습 데이터 비율 설정
    QHBoxLayout* trainRatioLayout = new QHBoxLayout();
    trainRatioLabel = new QLabel("학습 비율:");
    trainRatioLabel->setStyleSheet("color: #cccccc; font-size: 10px;");
    
    trainRatioSlider = new QSlider(Qt::Horizontal);
    trainRatioSlider->setRange(60, 90);
    trainRatioSlider->setValue(80);
    trainRatioSlider->setFixedWidth(100);
    
    trainRatioValueLabel = new QLabel("80%");
    trainRatioValueLabel->setStyleSheet("color: #ffffff; font-size: 10px; min-width: 30px;");
    
    trainRatioLayout->addWidget(trainRatioLabel);
    trainRatioLayout->addWidget(trainRatioSlider);
    trainRatioLayout->addWidget(trainRatioValueLabel);
    trainRatioLayout->addStretch();
    
    trainingContentLayout->addLayout(trainRatioLayout);
    leftLayout->addLayout(trainingContentLayout);
    
    // 구분선
    QFrame* line3 = new QFrame();
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet("QFrame { color: #555555; }");
    leftLayout->addWidget(line3);
    
    // Docker 관리 패널 (심플하게)
    QGroupBox* dockerGroup = new QGroupBox("Docker 관리");
    dockerGroup->setMaximumHeight(140);
    dockerGroup->setStyleSheet("QGroupBox { font-size: 13px; font-weight: bold; }");
    leftLayout->addWidget(dockerGroup);
    
    QVBoxLayout* dockerLayout = new QVBoxLayout(dockerGroup);
    dockerLayout->setSpacing(5);
    dockerLayout->setContentsMargins(8, 8, 8, 8);
    
    // Docker 상태 표시 (스크롤 가능)
    dockerStatusLabel = new QLabel("Docker 상태 확인 중...");
    dockerStatusLabel->setStyleSheet("color: #cccccc; font-size: 10px; font-family: 'Monaco', 'Menlo', 'Courier New', 'Consolas', Arial; background-color: #2b2b2b; padding: 4px; border-radius: 3px;");
    dockerStatusLabel->setWordWrap(true);
    dockerStatusLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    dockerStatusLabel->setMaximumHeight(60);
    dockerLayout->addWidget(dockerStatusLabel);
    
    // Docker 버튼들 (2x3 그리드, 크기 키움)
    QGridLayout* dockerButtonLayout = new QGridLayout();
    dockerButtonLayout->setSpacing(4);
    
    dockerInstallButton = new QPushButton("설치");
    dockerInstallButton->setFixedSize(68, 28);
    dockerInstallButton->setStyleSheet("QPushButton { background-color: #007bff; font-size: 11px; }");
    
    dockerBuildButton = new QPushButton("빌드");
    dockerBuildButton->setFixedSize(68, 28);
    dockerBuildButton->setStyleSheet("QPushButton { background-color: #fd7e14; font-size: 11px; }");
    
    dockerStartButton = new QPushButton("시작");
    dockerStartButton->setFixedSize(68, 28);
    dockerStartButton->setStyleSheet("QPushButton { background-color: #28a745; font-size: 11px; }");
    
    dockerStopButton = new QPushButton("중지");
    dockerStopButton->setFixedSize(68, 28);
    dockerStopButton->setStyleSheet("QPushButton { background-color: #dc3545; font-size: 11px; }");
    
    dockerDeleteButton = new QPushButton("삭제");
    dockerDeleteButton->setFixedSize(68, 28);
    dockerDeleteButton->setStyleSheet("QPushButton { background-color: #6c757d; font-size: 11px; }");
    
    dockerRefreshButton = new QPushButton("새로고침");
    dockerRefreshButton->setFixedSize(68, 28);
    dockerRefreshButton->setStyleSheet("QPushButton { background-color: #6f42c1; font-size: 11px; }");
    
    dockerButtonLayout->addWidget(dockerInstallButton, 0, 0);
    dockerButtonLayout->addWidget(dockerBuildButton, 0, 1);
    dockerButtonLayout->addWidget(dockerStartButton, 0, 2);
    dockerButtonLayout->addWidget(dockerStopButton, 1, 0);
    dockerButtonLayout->addWidget(dockerDeleteButton, 1, 1);
    dockerButtonLayout->addWidget(dockerRefreshButton, 1, 2);
    
    dockerLayout->addLayout(dockerButtonLayout);
    
    // Docker 버튼 시그널 연결
    connect(dockerInstallButton, &QPushButton::clicked, this, &SimulationDialog::dockerInstall);
    connect(dockerBuildButton, &QPushButton::clicked, this, &SimulationDialog::dockerBuild);
    connect(dockerStartButton, &QPushButton::clicked, this, &SimulationDialog::dockerStart);
    connect(dockerStopButton, &QPushButton::clicked, this, &SimulationDialog::dockerStop);
    connect(dockerDeleteButton, &QPushButton::clicked, this, &SimulationDialog::dockerDelete);
    connect(dockerRefreshButton, &QPushButton::clicked, this, &SimulationDialog::refreshDockerStatus);
    
    contentLayout->addWidget(leftPanel);
    
    // 오른쪽 패널
    QWidget* rightPanel = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(8, 0, 0, 0);
    
    // 이미지 표시 영역
    imageDisplayLabel = new ZoomLabel();
    imageDisplayLabel->setAlignment(Qt::AlignCenter);
    imageDisplayLabel->setStyleSheet(
        "QLabel {"
        "    background-color: #3c3c3c;"
        "    border: 1px solid #555555;"
        "    border-radius: 4px;"
        "    color: #cccccc;"
        "}"
    );
    imageDisplayLabel->setText(TR("SELECT_IMAGE"));
    imageDisplayLabel->setMinimumSize(500, 400);
    rightLayout->addWidget(imageDisplayLabel, 1);
    
    // 하단 네비게이션
    QHBoxLayout* navLayout = new QHBoxLayout();
    prevButton = new QPushButton("이전");
    prevButton->setFixedSize(60, 30);
    prevButton->setEnabled(false);
    
    imageInfoLabel = new QLabel("이미지 정보: -");
    imageInfoLabel->setStyleSheet("background-color: #4a4a4a; padding: 4px; border-radius: 4px; font-size: 10px;");
    imageInfoLabel->setAlignment(Qt::AlignCenter);
    
    nextButton = new QPushButton("다음");
    nextButton->setFixedSize(60, 30);
    nextButton->setEnabled(false);
    
    navLayout->addWidget(prevButton);
    navLayout->addWidget(imageInfoLabel, 1);
    navLayout->addWidget(nextButton);
    rightLayout->addLayout(navLayout);

    // 점수 표시용 레이블: 이미지 정보 바로 아래에 위치
    scoreLabel = new QLabel(this);
    scoreLabel->setText("");
    QFont scoreFont = scoreLabel->font();
    scoreFont.setPointSize(10);
    scoreLabel->setFont(scoreFont);
    scoreLabel->setStyleSheet("color: #ffd166; padding: 2px;");
    scoreLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    rightLayout->addWidget(scoreLabel);
    
    contentLayout->addWidget(rightPanel, 1);
    mainLayout->addLayout(contentLayout);
    
    // 시그널 연결
    connect(loadImagesButton, &QPushButton::clicked, this, &SimulationDialog::loadImages);
    connect(loadTrainingImagesButton, &QPushButton::clicked, this, &SimulationDialog::loadTrainingImages);
    connect(removeTrainingImageButton, &QPushButton::clicked, this, &SimulationDialog::removeTrainingImage);
    connect(trainButton, &QPushButton::clicked, this, &SimulationDialog::onTrainingButtonClicked);
    connect(trainResultsButton, &QPushButton::clicked, this, &SimulationDialog::onTrainResultsButtonClicked);
    connect(detectButton, &QPushButton::clicked, this, &SimulationDialog::onDetectionButtonClicked);
    connect(trainRatioSlider, &QSlider::valueChanged, this, &SimulationDialog::onTrainRatioChanged);
    connect(prevButton, &QPushButton::clicked, this, &SimulationDialog::onPrevClicked);
    connect(nextButton, &QPushButton::clicked, this, &SimulationDialog::onNextClicked);
}

void SimulationDialog::loadImages()
{
    if (currentRecipeName.isEmpty()) {
        QMessageBox::warning(this, "경고", "먼저 새 레시피를 생성하거나 기존 레시피를 불러와주세요.");
        return;
    }
    
    // 현재 선택된 카메라 확인
    QString selectedCameraUuid = getSelectedCameraUuid();
    if (selectedCameraUuid.isEmpty()) {
        QMessageBox::warning(this, "경고", "먼저 카메라를 선택해주세요.");
        return;
    }
    
    // 티칭 이미지 변경 확인 (항상 물어보기)
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "티칭 이미지 변경",
        QString("카메라 '%1'의 티칭 이미지를 새로운 이미지로 변경하시겠습니까?").arg(selectedCameraUuid),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::No) {
        return;
    }
    
    QString imageFile = QFileDialog::getOpenFileName(
        this,
        "새 티칭 이미지 선택",
        "",
        "이미지 파일 (*.jpg *.jpeg *.png *.bmp *.tiff *.tif);;모든 파일 (*)"
    );
    
    if (!imageFile.isEmpty()) {
        // 대상 디렉토리 생성: recipes/레시피명/teach/
        QString targetDir = QString("recipes/%1/teach").arg(currentRecipeName);
        QDir dir;
        if (!dir.exists(targetDir)) {
            if (!dir.mkpath(targetDir)) {
                QMessageBox::critical(this, "오류", QString("디렉토리를 생성할 수 없습니다: %1").arg(targetDir));
                return;
            }
        }
        
        // 새 티칭 이미지 파일명: teach/카메라UUID.jpg
        QString targetFile = QString("%1/%2.jpg").arg(targetDir, selectedCameraUuid);
        
        // 기존 파일이 있으면 삭제
        if (QFile::exists(targetFile)) {
            QFile::remove(targetFile);
        }
        
        // 파일 복사
        if (QFile::copy(imageFile, targetFile)) {
            // 이미지를 카메라 뷰에 로드하고 표시
            cv::Mat newImage = cv::imread(imageFile.toStdString());
            if (!newImage.empty()) {
                // 현재 이미지를 업데이트
                currentImage = newImage.clone();
                
                // 시뮬레이션 이미지로 설정하여 TeachingWidget에 전달
                emit imageSelected(newImage, targetFile, currentRecipeName);
                
                // 시뮬레이션 다이얼로그의 이미지 뷰에도 표시
                if (imageDisplayLabel) {
                    // OpenCV Mat을 QImage로 변환
                    QImage qimg;
                    if (newImage.channels() == 3) {
                        cv::Mat rgb;
                        cv::cvtColor(newImage, rgb, cv::COLOR_BGR2RGB);
                        qimg = QImage(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
                    } else if (newImage.channels() == 1) {
                        qimg = QImage(newImage.data, newImage.cols, newImage.rows, newImage.step, QImage::Format_Grayscale8);
                    }
                    
                    if (!qimg.isNull()) {
                        imageDisplayLabel->setPixmap(QPixmap::fromImage(qimg));
                    }
                }
                
                // 시뮬레이션 모드에서 새로운 티칭 이미지로 즉시 갱신
                // 새로 복사된 티칭 이미지를 다시 로드하여 시뮬레이션 뷰 갱신
                cv::Mat updatedImage = cv::imread(targetFile.toStdString());
                if (!updatedImage.empty()) {
                    currentImage = updatedImage.clone();
                    // TeachingWidget에도 업데이트된 이미지 전달
                    emit imageSelected(updatedImage, targetFile, currentRecipeName);
                }
                
                QMessageBox::information(this, "변경 완료", 
                    QString("카메라 '%1'의 티칭 이미지가 성공적으로 변경되었습니다.").arg(selectedCameraUuid));
            } else {
                QMessageBox::critical(this, "오류", "이미지를 로드할 수 없습니다.");
            }
        } else {
            QMessageBox::warning(this, "복사 실패", 
                               QString("파일을 복사할 수 없습니다:\n%1\n→ %2").arg(imageFile, targetFile));
        }
    }
}

void SimulationDialog::loadTrainingImages()
{
    if (currentRecipeName.isEmpty()) {
        QMessageBox::warning(this, "경고", "먼저 새 레시피를 생성하거나 기존 레시피를 불러와주세요.");
        return;
    }
    
    QStringList trainingFiles = QFileDialog::getOpenFileNames(
        this,
        "학습용 이미지 선택",
        "",
        "이미지 파일 (*.jpg *.jpeg *.png *.bmp *.tiff *.tif);;모든 파일 (*)"
    );
    
    if (!trainingFiles.isEmpty()) {
        // 대상 디렉토리 생성: recipes/레시피명/teach/
        QString targetDir = QString("recipes/%1/teach").arg(currentRecipeName);
        QDir dir;
        if (!dir.exists(targetDir)) {
            if (!dir.mkpath(targetDir)) {
                QMessageBox::critical(this, "오류", QString("디렉토리를 생성할 수 없습니다: %1").arg(targetDir));
                return;
            }
        }
        
        QStringList copiedPaths;
        int successCount = 0;
        
        for (const QString& sourceFile : trainingFiles) {
            QFileInfo fileInfo(sourceFile);
            QString fileName = fileInfo.fileName();
            QString targetFile = QString("%1/%2").arg(targetDir, fileName);
            
            // 파일명 중복 처리
            int counter = 1;
            while (QFile::exists(targetFile)) {
                QString baseName = fileInfo.completeBaseName();
                QString extension = fileInfo.suffix();
                fileName = QString("%1_%2.%3").arg(baseName).arg(counter).arg(extension);
                targetFile = QString("%1/%2").arg(targetDir, fileName);
                counter++;
            }
            
            // 파일 복사
            if (QFile::copy(sourceFile, targetFile)) {
                copiedPaths.append(targetFile);
                successCount++;
            } else {
                QMessageBox::warning(this, "복사 실패", 
                                   QString("파일을 복사할 수 없습니다:\n%1\n→ %2").arg(sourceFile, targetFile));
            }
        }
        
        if (!copiedPaths.isEmpty()) {
            trainingImagePaths.append(copiedPaths); // 복사된 파일 경로들을 추가
            
            // 학습 이미지 목록 UI 업데이트 - 비활성화
            // trainingImageListWidget->clear();
            // for (const QString& path : trainingImagePaths) {
            //     QFileInfo fileInfo(path);
            //     trainingImageListWidget->addItem(fileInfo.fileName());
            // }
            
            QString message = QString("학습 이미지 %1개가 추가되었습니다. (총 %2개)\n위치: %3")
                .arg(successCount).arg(trainingImagePaths.size()).arg(targetDir);
            QMessageBox::information(this, "추가 완료", message);
            
            // 버튼 상태 업데이트
            updateControls();
        }
    }
}

void SimulationDialog::setTrainingImagePaths(const QStringList& paths)
{
    trainingImagePaths = paths;
    
    // 학습 이미지 목록 UI 업데이트 - 비활성화
    // trainingImageListWidget->clear();
    // for (const QString& path : trainingImagePaths) {
    //     QFileInfo fileInfo(path);
    //     trainingImageListWidget->addItem(fileInfo.fileName());
    // }
    
    updateControls();
}

void SimulationDialog::loadRecipeImages(const QString& recipeName)
{
    // 이미 로딩 중이면 중복 호출 방지
    if (loadingRecipeImages) {
        qDebug() << QString("레시피 '%1' 이미지가 이미 로딩 중입니다. 중복 호출을 무시합니다.").arg(recipeName);
        return;
    }
    
    // 현재 레시피 이름 설정
    currentRecipeName = recipeName;
    
    loadingRecipeImages = true; // 로딩 시작 표시
    // 레시피 이미지 폴더 경로: recipes/레시피명/teach/
    QString recipeImagesDir = QString("recipes/%1/teach").arg(recipeName);
    QDir dir(recipeImagesDir);
    
    // 폴더가 존재하지 않으면 리턴
    if (!dir.exists()) {
        imagePaths.clear();
        // imageListWidget->clear();
        updateControls();
        loadingRecipeImages = false; // 로딩 완료 표시
        return;
    }
    
    // 이미지 파일 필터
    QStringList nameFilters;
    nameFilters << "*.jpg" << "*.jpeg" << "*.png" << "*.bmp" << "*.tiff" << "*.tif";
    dir.setNameFilters(nameFilters);
    
    // 이미지 파일 목록 가져오기
    QStringList imageFiles = dir.entryList(QDir::Files);
    
    // 모든 티칭 이미지를 표시 (필터링 제거)
    QStringList filteredImageFiles = imageFiles;
    
    // 절대 경로로 변환
    imagePaths.clear();
    for (const QString& fileName : filteredImageFiles) {
        QString fullPath = dir.absoluteFilePath(fileName);
        imagePaths.append(fullPath);
    }
    
    // UI 업데이트 - 이미지 리스트 위젯 제거됨
    // imageListWidget->clear();
    int selectedItemIndex = -1; // 선택된 카메라에 해당하는 이미지 인덱스
    
    for (int i = 0; i < imagePaths.size(); ++i) {
        QFileInfo fileInfo(imagePaths[i]);
        QString fileName = fileInfo.fileName();
        
        // 파일명에서 카메라 UUID 추출 (cameraUuid.jpg 형식)
        QString imageCameraUuid = fileName.split('.')[0];
        
        // 선택된 카메라의 이미지인지 확인
        bool isSelectedCamera = (!selectedCameraUuid.isEmpty() && imageCameraUuid == selectedCameraUuid);
        
        if (isSelectedCamera) {
            selectedItemIndex = i; // 자동 선택할 인덱스 저장
            teachingImageIndex = i; // 티칭 이미지 인덱스 설정
        }
        
        // 리스트 위젯 아이템 생성 코드 제거됨
        // QListWidgetItem* item = new QListWidgetItem(displayName);
        // imageListWidget->addItem(item);
    }
    
    updateControls();
    
    // 이미지가 있으면 선택된 카메라에 해당하는 이미지를 자동 선택
    if (!imagePaths.isEmpty()) {
        int imageIndexToSelect = 0; // 기본값은 첫 번째 이미지
        
        // 선택된 카메라가 있으면 해당 카메라의 이미지를 찾아서 선택
        if (!selectedCameraUuid.isEmpty() && selectedItemIndex >= 0) {
            imageIndexToSelect = selectedItemIndex;
            qDebug() << QString("카메라 '%1' 선택됨 - 이미지 인덱스 %2 자동 선택").arg(selectedCameraUuid).arg(selectedItemIndex);
        } else {
            qDebug() << QString("기본 이미지 인덱스 0 선택 (카메라: %1, selectedItemIndex: %2)").arg(selectedCameraUuid).arg(selectedItemIndex);
        }
        
        currentIndex = imageIndexToSelect;
        // imageListWidget->setCurrentRow(imageIndexToSelect);
        
        // 선택된 이미지 로드
        QString imagePath = imagePaths[imageIndexToSelect];
        currentImage = cv::imread(imagePath.toStdString());
        
        if (!currentImage.empty()) {
            // 시뮬레이션 다이얼로그 내부에서만 이미지 표시
            updateImageDisplay();
            
            // 이미지 정보 업데이트
            QFileInfo fileInfo(imagePath);
            QString info = QString("%1 (%2x%3) [%4/%5]")
                .arg(fileInfo.fileName())
                .arg(currentImage.cols)
                .arg(currentImage.rows)
                .arg(imageIndexToSelect + 1)
                .arg(imagePaths.size());
            imageInfoLabel->setText(info);
        }
        
        qDebug() << QString("레시피 '%1'에서 %2개의 티칭 이미지를 로드했습니다. 선택된 카메라: %3")
                    .arg(recipeName).arg(imagePaths.size()).arg(selectedCameraUuid.isEmpty() ? "없음" : selectedCameraUuid);
        
        // 선택된 카메라의 티칭 이미지를 TeachingWidget에 전달
        if (!selectedCameraUuid.isEmpty() && !currentImage.empty()) {
            emit imageSelected(currentImage, imagePath, recipeName);
        }
    }
    
    // 레시피 정보 UI 업데이트
    updateRecipeInfo();
    
    loadingRecipeImages = false; // 로딩 완료 표시
}

void SimulationDialog::onCameraSelectionChanged(int index)
{
    if (index < 0 || !cameraComboBox) return;
    
    QString previousCameraUuid = selectedCameraUuid;
    selectedCameraUuid = cameraComboBox->itemData(index).toString();
    qDebug() << QString("카메라 선택 변경: '%1' → '%2'").arg(previousCameraUuid).arg(selectedCameraUuid);
    
    // 현재 레시피가 로드되어 있으면 이미지 목록을 다시 로드
    if (!currentRecipeName.isEmpty()) {
        qDebug() << "이미지 목록 다시 로드 시작...";
        loadRecipeImages(currentRecipeName);
        // loadRecipeImages에서 이미 선택된 카메라의 이미지를 자동으로 선택하고 TeachingWidget에 전달함
    }
    
    // TeachingWidget에 선택된 카메라 UUID 전달
    if (teachingWidget) {
        qDebug() << QString("TeachingWidget 포인터 유효, 카메라 UUID '%1' 전달 시도").arg(selectedCameraUuid);
        // 먼저 카메라 UUID를 설정
        if (!selectedCameraUuid.isEmpty()) {
            qDebug() << QString("selectCameraTeachingImage('%1') 호출").arg(selectedCameraUuid);
            teachingWidget->selectCameraTeachingImage(selectedCameraUuid);
        } else {
            qDebug() << "선택된 카메라 UUID가 비어있음";
        }
    } else {
        qDebug() << "TeachingWidget 포인터가 null임";
    }
        
    // 카메라 UUID 설정 후 패턴 트리 업데이트 (selectCameraTeachingImage에서 이미 호출됨)
    // teachingWidget->updatePatternTree(); // 중복 호출 제거
}

void SimulationDialog::updateCameraList(const QString& recipeName)
{
    if (!cameraComboBox) return;
    
    cameraComboBox->clear();
    
    // RecipeManager를 사용하여 레시피에서 카메라 UUID 읽기
    RecipeManager manager;
    QStringList cameraUuids = manager.getRecipeCameraUuids(recipeName);
    
    if (cameraUuids.isEmpty()) {
        // XML에서 카메라 정보를 읽지 못한 경우, 기존 방식으로 폴백
        // 레시피 이미지 폴더에서 실제 저장된 이미지 파일들을 확인
        QString recipeImagesDir = QString("recipes/%1/teach").arg(recipeName);
        QDir dir(recipeImagesDir);
        
        if (dir.exists()) {
            // 이미지 파일에서 카메라 UUID 추출
            QStringList nameFilters;
            nameFilters << "*.jpg" << "*.jpeg" << "*.png" << "*.bmp" << "*.tiff" << "*.tif";
            dir.setNameFilters(nameFilters);
            QStringList imageFiles = dir.entryList(QDir::Files);
            
            for (const QString& fileName : imageFiles) {
                // 파일명에서 카메라 UUID 추출: cameraUuid.jpg 형식 (타임스탬프 제거됨)
                QStringList parts = fileName.split('.');
                if (parts.size() >= 2) {
                    QString cameraUuid = parts[0]; // 확장자를 제거한 전체가 카메라 UUID
                    if (!cameraUuid.isEmpty() && !cameraUuids.contains(cameraUuid)) {
                        cameraUuids.append(cameraUuid);
                    }
                }
            }
        }
        
        // 레시피에서 패턴들도 확인해서 카메라 UUID 추가 (패턴에만 있고 이미지가 없는 경우)
        QVector<PatternInfo> patterns;
        if (manager.loadRecipeByName(recipeName, patterns)) {
            for (const PatternInfo& pattern : patterns) {
                if (!pattern.cameraUuid.isEmpty() && !cameraUuids.contains(pattern.cameraUuid)) {
                    cameraUuids.append(pattern.cameraUuid);
                }
            }
        }
    }
    
    // 콤보박스에 카메라들 추가 (XML 순서 유지)
    for (const QString& uuid : cameraUuids) {
        cameraComboBox->addItem(uuid, uuid); // 카메라 UUID를 그대로 표시
    }
    
    // XML의 첫 번째 카메라를 기본 선택 (레시피에서 정의한 순서대로)
    if (cameraComboBox->count() > 0) {
        selectedCameraUuid = cameraComboBox->itemData(0).toString();
        cameraComboBox->setCurrentIndex(0);
        qDebug() << QString("기본 선택된 카메라: %1 (XML 첫 번째 카메라)").arg(selectedCameraUuid);
    } else {
        selectedCameraUuid = QString();
    }
    
    qDebug() << QString("updateCameraList: 레시피 '%1'에서 %2개 카메라 발견").arg(recipeName).arg(cameraUuids.size());
    for (const QString& uuid : cameraUuids) {
        qDebug() << QString("  - 카메라 UUID: %1").arg(uuid);
    }
    
    // 각 카메라별 패턴 수도 출력
    QVector<PatternInfo> patterns;
    if (manager.loadRecipeByName(recipeName, patterns)) {
        QMap<QString, int> cameraPatternCount;
        for (const PatternInfo& pattern : patterns) {
            QString patternCameraUuid = pattern.cameraUuid.isEmpty() ? "default" : pattern.cameraUuid;
            cameraPatternCount[patternCameraUuid]++;
        }
        
        qDebug() << QString("카메라별 패턴 수:");
        for (auto it = cameraPatternCount.begin(); it != cameraPatternCount.end(); ++it) {
            qDebug() << QString("  - %1: %2개 패턴").arg(it.key()).arg(it.value());
        }
    }
}

void SimulationDialog::clearForNewRecipe()
{
    // 이미지 목록 클리어
    imagePaths.clear();
    // if (imageListWidget) {
    //     imageListWidget->clear();
    // }
    
    // 카메라 목록 클리어
    if (cameraComboBox) {
        cameraComboBox->clear();
    }
    selectedCameraUuid = QString();
    
    // 현재 이미지 정보 초기화
    currentIndex = -1;
    teachingImageIndex = -1;
    currentImage = cv::Mat();
    
    // UI 업데이트
    updateImageDisplay();
    updateControls();
    
    qDebug() << "새 레시피 생성을 위해 시뮬레이션 다이얼로그 초기화 완료";
}

void SimulationDialog::onImageListClicked(int row)
{
    if (row >= 0 && row < imagePaths.size()) {
        currentIndex = row;
        
        // 이미지 로드 및 TeachingWidget으로 전송
        loadImageAtIndex(row);
        updateControls();
        
        qDebug() << QString("사용자가 이미지를 선택했습니다: %1").arg(imagePaths[row]);
    }
}

void SimulationDialog::onTrainingImageListClicked(int row)
{
    // 삭제 버튼 활성화/비활성화
    removeTrainingImageButton->setEnabled(row >= 0 && row < trainingImagePaths.size());
    
    if (row >= 0 && row < trainingImagePaths.size()) {
        QString imagePath = trainingImagePaths[row];
        
        // 학습 이미지를 로드하여 시뮬레이션 이미지뷰에만 표시
        cv::Mat trainingImage = cv::imread(imagePath.toStdString());
        if (!trainingImage.empty()) {
            currentImage = trainingImage.clone();
            updateImageDisplay();
            
            // 이미지 정보 업데이트 (학습 이미지 표시)
            QFileInfo fileInfo(imagePath);
            imageInfoLabel->setText(QString("학습 이미지: %1 (%2x%3)")
                .arg(fileInfo.fileName())
                .arg(trainingImage.cols)
                .arg(trainingImage.rows));
            
            // 일반 이미지 선택 해제 - 위젯 제거됨
            // imageListWidget->clearSelection();
            
            // 주의: 티칭위젯에는 신호를 보내지 않음 (학습 이미지는 패턴 학습용)
        }
    }
}

void SimulationDialog::onPrevClicked()
{
    if (currentIndex > 0) {
        currentIndex--;
        // imageListWidget->setCurrentRow(currentIndex);
        loadImageAtIndex(currentIndex);
        updateControls();
    }
}

void SimulationDialog::onNextClicked()
{
    if (currentIndex < imagePaths.size() - 1) {
        currentIndex++;
        // imageListWidget->setCurrentRow(currentIndex);
        loadImageAtIndex(currentIndex);
        updateControls();
    }
}

void SimulationDialog::loadImageAtIndex(int index)
{
    if (index < 0 || index >= imagePaths.size()) {
        return;
    }
    
    QString imagePath = imagePaths[index];
    currentImage = cv::imread(imagePath.toStdString());
    
    if (!currentImage.empty()) {
        // OpenCV Mat을 QImage로 변환 (데이터 복사)
        QImage qImage;
        if (currentImage.channels() == 3) {
            cv::Mat rgbImage;
            cv::cvtColor(currentImage, rgbImage, cv::COLOR_BGR2RGB);
            qImage = QImage(rgbImage.data, rgbImage.cols, rgbImage.rows, rgbImage.step, QImage::Format_RGB888).copy();
        } else {
            qImage = QImage(currentImage.data, currentImage.cols, currentImage.rows, currentImage.step, QImage::Format_Grayscale8).copy();
        }
        
        // 이미지를 라벨 크기에 맞게 비율 유지하면서 축소
        QPixmap pixmap = QPixmap::fromImage(qImage);
        QSize labelSize = imageDisplayLabel->size();
        
        // 여백을 고려한 실제 표시 영역 계산
        QSize availableSize = labelSize - QSize(40, 40); // 패딩 20px씩 빼기
        
        // 비율 유지하면서 축소
        QPixmap scaledPixmap = pixmap.scaled(availableSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (auto z = qobject_cast<ZoomLabel*>(imageDisplayLabel)) z->setPixmap(scaledPixmap);
    else imageDisplayLabel->setPixmap(scaledPixmap);
        
        // 이미지 정보 업데이트 (간략하게)
        QFileInfo fileInfo(imagePath);
        QString info = QString("%1 (%2x%3) [%4/%5]")
            .arg(fileInfo.fileName())
            .arg(currentImage.cols)
            .arg(currentImage.rows)
            .arg(index + 1)
            .arg(imagePaths.size());
        imageInfoLabel->setText(info);
        
        // 메인 화면에 이미지 전송
        emit imageSelected(currentImage, imagePath, currentRecipeName);
        
    } else {
        imageDisplayLabel->clear();
        imageDisplayLabel->setText(TR("IMAGE_LOAD_FAILED"));
        imageInfoLabel->setText(TR("ERROR_CANNOT_LOAD_IMAGE"));
    }
}

void SimulationDialog::updateImageDisplay()
{
    if (currentImage.empty()) {
        imageDisplayLabel->clear();
        imageDisplayLabel->setText(TR("NO_IMAGE_AVAILABLE"));
        return;
    }
    
    // OpenCV Mat을 QImage로 변환 (데이터 복사)
    QImage qImage;
    if (currentImage.channels() == 3) {
        cv::Mat rgbImage;
        cv::cvtColor(currentImage, rgbImage, cv::COLOR_BGR2RGB);
        qImage = QImage(rgbImage.data, rgbImage.cols, rgbImage.rows, rgbImage.step, QImage::Format_RGB888).copy();
    } else {
        qImage = QImage(currentImage.data, currentImage.cols, currentImage.rows, currentImage.step, QImage::Format_Grayscale8).copy();
    }
    
    // 이미지를 라벨 크기에 맞게 비율 유지하면서 축소
    QPixmap pixmap = QPixmap::fromImage(qImage);
    QSize labelSize = imageDisplayLabel->size();
    
    // 여백을 고려한 실제 표시 영역 계산
    QSize availableSize = labelSize - QSize(40, 40); // 패딩 20px씩 빼기
    
    // 비율 유지하면서 축소
    QPixmap scaledPixmap = pixmap.scaled(availableSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (auto z = qobject_cast<ZoomLabel*>(imageDisplayLabel)) z->setPixmap(scaledPixmap);
    else imageDisplayLabel->setPixmap(scaledPixmap);
}

void SimulationDialog::updateControls()
{
    bool hasImages = !imagePaths.isEmpty();
    bool hasPrev = currentIndex > 0;
    bool hasNext = currentIndex < imagePaths.size() - 1;
    bool hasRecipe = !currentRecipeName.isEmpty();
    bool hasTrainingImages = !trainingImagePaths.isEmpty();
    
    // 모델 파일 존재 확인
    bool hasModel = false;
    if (hasRecipe) {
        QString modelPath = QString("models/%1/model.ckpt").arg(currentRecipeName);
        hasModel = QFile::exists(modelPath);
    }
    
    prevButton->setEnabled(hasImages && hasPrev);
    nextButton->setEnabled(hasImages && hasNext);
    
    // 학습 이미지 추가 버튼은 레시피가 있을 때 활성화
    loadTrainingImagesButton->setEnabled(hasRecipe);
    
    // 학습 버튼은 학습 이미지가 있을 때 활성화
    trainButton->setEnabled(hasTrainingImages);
    
    // 결과 버튼은 학습 결과 폴더가 있을 때 활성화
    bool hasTrainResults = false;
    if (hasRecipe) {
        QString resultsPath = QString("/models/%1/test_results").arg(currentRecipeName);
        hasTrainResults = QDir(resultsPath).exists();
    }
    trainResultsButton->setEnabled(hasTrainResults);
    
    // 탐지 버튼은 이미지와 모델이 모두 있을 때 활성화
    detectButton->setEnabled(hasImages && hasModel);
}

QString SimulationDialog::getCurrentImagePath() const
{
    if (currentIndex >= 0 && currentIndex < imagePaths.size()) {
        return imagePaths[currentIndex];
    }
    return QString();
}

QString SimulationDialog::getSimulationDataFilePath() const
{
    if (!currentRecipeName.isEmpty()) {
        return currentRecipeName + "_simulation.json";
    }
    return "default_simulation.json";
}

void SimulationDialog::updateRecipeInfo()
{
    if (currentRecipeName.isEmpty()) {
        recipeInfoLabel->setText("레시피: 없음");
        setWindowTitle("시뮬레이션 모드 - 비전 티칭 레시피 관리");
        
        // 버튼 상태 업데이트
        trainButton->setStyleSheet("QPushButton { background-color: #ffc107; color: #000; }");
        trainButton->setText("학습");
    } else {
        // 모델 파일 체크 (실행 파일 기준)
        QString modelPath = QString("models/%1/model.ckpt").arg(currentRecipeName);
        bool modelExists = QFile::exists(modelPath);
        
        QString modelStatus = modelExists ? "학습완료" : "미학습";
        
        recipeInfoLabel->setText(QString("레시피: %1 (이미지: %2개, %3)")
                                 .arg(currentRecipeName).arg(imagePaths.size()).arg(modelStatus));
        setWindowTitle(QString("시뮬레이션 모드 - %1").arg(currentRecipeName));
        
        // 버튼 상태 업데이트
        if (modelExists) {
            trainButton->setStyleSheet("QPushButton { background-color: #20c997; color: #fff; }");
            trainButton->setText("재학습");
        } else {
            trainButton->setStyleSheet("QPushButton { background-color: #ffc107; color: #000; }");
            trainButton->setText("학습");
        }
    }
    
    // 모델 상태 변경 시 버튼 상태도 업데이트
    updateControls();
}

void SimulationDialog::clearRecipe()
{
    imagePaths.clear();
    trainingImagePaths.clear(); // 학습 이미지 경로들도 초기화
    currentIndex = -1;
    currentRecipeName = "";
    currentRecipePath = "";
    teachingImageIndex = 0; // 티칭 이미지 인덱스도 초기화
    
    // UI 요소들 완전 초기화
    // imageListWidget->clear();
    imageDisplayLabel->clear();
    imageDisplayLabel->setText("이미지를 선택하세요");
    imageInfoLabel->setText("이미지 정보: -");
    
    // 버튼 상태 초기화
    loadImagesButton->setEnabled(true); // 이미지 추가는 항상 활성화
    trainButton->setEnabled(false);
    detectButton->setEnabled(false);
    
    updateControls();
    updateRecipeInfo(); // 레시피 정보도 업데이트
}

void SimulationDialog::updateImageList()
{
    // 이미지 리스트 위젯이 제거되어 더 이상 필요하지 않음
}
}

void SimulationDialog::updateTeachingImageIndex(int newIndex) {
    if (newIndex >= 0 && newIndex < imagePaths.size()) {
        teachingImageIndex = newIndex;
        updateImageList(); // UI 업데이트
    }
}

void SimulationDialog::onDetectionButtonClicked()
{
    // 현재 이미지가 로드되어 있는지 확인
    if (currentImage.empty()) {
        QMessageBox::warning(this, "이상 탐지", "탐지할 이미지를 먼저 로드하세요.");
        return;
    }
    
    if (currentRecipeName.isEmpty()) {
        QMessageBox::warning(this, "이상 탐지", "레시피가 선택되지 않았습니다. 먼저 레시피를 선택해주세요.");
        return;
    }
    
    // 레시피별 모델 파일 경로 확인 (실행 파일 기준)
    QString modelDir = QString("models/%1").arg(currentRecipeName);
    QString modelPath = QString("%1/model.ckpt").arg(modelDir);
    
    if (!QFile::exists(modelPath)) {
        QMessageBox::warning(this, "이상 탐지", 
            QString("레시피 '%1'에 대한 학습된 모델이 없습니다.\n\n"
                    "모델 파일: %2\n\n"
                    "먼저 학습을 진행해주세요.")
                .arg(currentRecipeName)
                .arg(modelPath));
        return;
    }
    
    // AI 이상 탐지 모델이 학습되어 있는지 확인
    if (!aiTrainer) {
        QMessageBox::warning(this, "이상 탐지", "AI 이상 탐지 학습기가 초기화되지 않았습니다.");
        return;
    }
    
    // 버튼 비활성화 및 텍스트 변경
    detectButton->setText("탐지중...");
    detectButton->setEnabled(false);
    
    qDebug() << "이상 탐지 시작:";
    qDebug() << "- 레시피:" << currentRecipeName;
    qDebug() << "- 모델 경로:" << modelPath;
    qDebug() << "- 이미지:" << getCurrentImagePath();
    
    // 현재 이미지 파일명 추출 (확장자 제거)
    QString currentImagePath = getCurrentImagePath();
    QString imageBaseName = QFileInfo(currentImagePath).baseName();
    
    // 이상 탐지 실행 (파일 경로 직접 전달)
    AITrainer::DetectionResult result = aiTrainer->detectAnomaly(currentImagePath, currentRecipeName);
    
    // 버튼 상태 복원
    detectButton->setText("탐지");
    detectButton->setEnabled(true);
    
    // 결과 표시
    if (!result.errorMessage.isEmpty()) {
        QMessageBox::critical(this, "이상 탐지 실패", 
                             QString("이상 탐지에 실패했습니다.\n\n오류: %1").arg(result.errorMessage));
    } else {
        // 결과 이미지 저장 경로 생성 (deploy/results 사용)
        QString resultsDir = QString("results/%1").arg(currentRecipeName);
        QDir dir;
        if (!dir.exists(resultsDir)) {
            dir.mkpath(resultsDir);
        }
        
        // Docker 내 결과 이미지를 host로 복사
        QString currentImagePath = getCurrentImagePath();
        QString originalFileName = QFileInfo(currentImagePath).baseName();
        QString originalExtension = QFileInfo(currentImagePath).suffix();
        
    // Docker 내 결과 이미지 경로 (애플리케이션 실행 디렉터리를 기준으로 구성)
    QString dockerBase = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/results/");
    QString dockerResultPath = QString("%1/%2/%3.%4").arg(dockerBase).arg(currentRecipeName).arg(originalFileName).arg(originalExtension);
        
        // Host 결과 경로
        QString hostResultPath = QString("%1/%2.%3").arg(resultsDir, originalFileName, originalExtension);
        
        if (QFile::exists(dockerResultPath)) {
            qDebug() << "[탐지] Docker 결과 이미지 존재:" << dockerResultPath;
            // 공유 폴더이므로 호스트 경로로 바로 사용
            hostResultPath = dockerResultPath;
        } else {
            qDebug() << "[탐지] Docker 결과 이미지 없음:" << dockerResultPath;
        }
        
        // 결과 파일명 생성
        QString resultFileName = QString("%1_result_%2.png")
                                .arg(originalFileName)
                                .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
        QString resultFilePath = QString("%1/%2").arg(resultsDir, resultFileName);
        
        // 결과 이미지 저장 (우선순위: resultImagePath > overlayBase64 > heatmapBase64)
        QPixmap resultPixmap;
        bool hasResultImage = false;

        // 폴백: AITrainer가 result.resultImagePath를 채우지 못했을 경우, 공용 results 폴더에서 파일을 찾아본다.
        if (result.resultImagePath.isEmpty()) {
            QStringList candidateDirs;
            // 컨테이너에서 쓰는 경로와 애플리케이션 실행 경로 기반 후보
            candidateDirs << QDir::cleanPath(QCoreApplication::applicationDirPath() + "/results/") + currentRecipeName;
            candidateDirs << QDir::cleanPath(QDir::currentPath() + "/deploy/results/") + "/" + currentRecipeName;
            candidateDirs << QDir::cleanPath(QDir::currentPath() + "/results/") + "/" + currentRecipeName;

            QString foundPath;
            QString targetBase = originalFileName; // base name to look for

            for (const QString& dirPath : candidateDirs) {
                QDir d(dirPath);
                if (!d.exists()) continue;
                // 우선 동일한 파일명 검색
                QStringList pats;
                pats << QString("%1.*").arg(targetBase) << "*.bmp" << "*.png" << "*.jpg";
                for (const QString& pat : pats) {
                    QStringList matches = d.entryList(QStringList() << pat, QDir::Files, QDir::Time);
                    if (!matches.isEmpty()) {
                        // 가장 최근 파일 선택
                        QString candidate = d.absoluteFilePath(matches.first());
                        foundPath = candidate;
                        break;
                    }
                }
                if (!foundPath.isEmpty()) break;
            }

            if (!foundPath.isEmpty()) {
                result.resultImagePath = foundPath;
                qDebug() << "[SimulationDialog] 폴백으로 결과 이미지 발견:" << result.resultImagePath;
            } else {
                qDebug() << "[SimulationDialog] 폴백으로도 결과 이미지 못찾음";
            }
        }
        
        if (!result.resultImagePath.isEmpty() && QFile::exists(result.resultImagePath)) {
            resultPixmap.load(result.resultImagePath);
            hasResultImage = true;
        } else if (!result.overlayBase64.isEmpty()) {
            QByteArray overlayData = QByteArray::fromBase64(result.overlayBase64.toUtf8());
            resultPixmap.loadFromData(overlayData);
            hasResultImage = true;
        } else if (!result.heatmapBase64.isEmpty()) {
            QByteArray heatmapData = QByteArray::fromBase64(result.heatmapBase64.toUtf8());
            resultPixmap.loadFromData(heatmapData);
            hasResultImage = true;
        }
        
        if (hasResultImage && !resultPixmap.isNull()) {
            // 결과 이미지 저장
            if (resultPixmap.save(resultFilePath)) {
                // 원본 이미지(currentImage)를 QPixmap으로 변환
                QPixmap origPixmap;
                if (!currentImage.empty()) {
                    QImage qOrig;
                    if (currentImage.channels() == 3) {
                        cv::Mat rgbImage;
                        cv::cvtColor(currentImage, rgbImage, cv::COLOR_BGR2RGB);
                        qOrig = QImage(rgbImage.data, rgbImage.cols, rgbImage.rows, rgbImage.step, QImage::Format_RGB888).copy();
                    } else {
                        qOrig = QImage(currentImage.data, currentImage.cols, currentImage.rows, currentImage.step, QImage::Format_Grayscale8).copy();
                    }
                    if (!qOrig.isNull()) origPixmap = QPixmap::fromImage(qOrig);
                }

                QPixmap heatmapPixmap = resultPixmap;

                // 라벨 크기와 여백을 고려해 너비 결정
                QSize labelSize = imageDisplayLabel->size();
                int targetWidth = qMax(1, labelSize.width() - 40);

                QPixmap scaledOrig = origPixmap.isNull() ? QPixmap() : origPixmap.scaledToWidth(targetWidth, Qt::SmoothTransformation);
                QPixmap scaledHeatmap = heatmapPixmap.scaledToWidth(targetWidth, Qt::SmoothTransformation);

                // 둘을 세로로 합성
                QPixmap composite;
                if (!scaledOrig.isNull()) {
                    int w = qMax(scaledOrig.width(), scaledHeatmap.width());
                    int h = scaledOrig.height() + (scaledHeatmap.isNull() ? 0 : scaledHeatmap.height());
                    composite = QPixmap(w, h);
                    composite.fill(Qt::black);
                    QPainter painter(&composite);
                    painter.drawPixmap(0, 0, scaledOrig);
                    if (!scaledHeatmap.isNull()) painter.drawPixmap(0, scaledOrig.height(), scaledHeatmap);
                    painter.end();
                } else {
                    composite = scaledHeatmap;
                }

                QPixmap finalPixmap = composite.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        // ZoomLabel::setPixmap ensures base pixmap and resets zoom
                        ZoomLabel* zlabel = qobject_cast<ZoomLabel*>(imageDisplayLabel);
                        if (zlabel) zlabel->setPixmap(finalPixmap);
                        else imageDisplayLabel->setPixmap(finalPixmap);

                // 스코어 표시
                scoreLabel->setText(QString("이상도 점수: %1").arg(QString::number(result.anomalyScore, 'f', 6)));

            } else {
                QMessageBox::warning(this, "저장 실패", "결과 이미지 저장에 실패했습니다.");
            }
        } else {
            // 히트맵 없이 결과만 표시: 이상도 점수를 별도 레이블에 표시
            scoreLabel->setText(QString("이상도 점수: %1").arg(QString::number(result.anomalyScore, 'f', 6)));
        }
    }
}

void SimulationDialog::removeTrainingImage()
{
    // 리스트 위젯이 제거되어 더 이상 사용하지 않음
    QMessageBox::information(this, "알림", "이 기능은 더 이상 사용되지 않습니다.");
}
}

void SimulationDialog::dockerInstall()
{
    QMessageBox::information(this, "Docker 설치", 
        "Docker Desktop을 설치해주세요.\n\n"
        "1. https://www.docker.com/products/docker-desktop 방문\n"
        "2. macOS용 Docker Desktop 다운로드\n"
        "3. 설치 후 Docker Desktop 실행\n"
        "4. '새로고침' 버튼 클릭");
}

void SimulationDialog::dockerBuild()
{
    // 현재 디렉토리가 build라면 상위로 이동
    QString workDir = ".";
    if (QDir::currentPath().endsWith("/build")) {
        workDir = "..";
    }
    
    // Dockerfile.patchcore 파일 존재 확인
    QFileInfo dockerFile("./Dockerfile.ai");
    if (!dockerFile.exists()) {
        QMessageBox::critical(this, "빌드 실패", "Dockerfile.ai 파일을 찾을 수 없습니다!\n\n현재 디렉토리에서 Dockerfile.ai 찾을 수 없습니다.");
        return;
    }
    
    dockerBuildButton->setEnabled(false);
    dockerBuildButton->setText("빌드중");
    
    QProcess* buildProcess = new QProcess(this);
    buildProcess->setWorkingDirectory(workDir);
    
    connect(buildProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [this, buildProcess](int exitCode, QProcess::ExitStatus exitStatus) {
            dockerBuildButton->setEnabled(true);
            dockerBuildButton->setText("빌드");
            
            if (exitCode == 0) {
                QMessageBox::information(this, "빌드 완료", "AI Docker 이미지 빌드가 성공적으로 완료되었습니다!\n\n이제 컨테이너를 시작할 수 있습니다.");
                refreshDockerStatus();
            } else {
                QString error = buildProcess->readAllStandardError();
                QString output = buildProcess->readAllStandardOutput();
                QString fullError = QString("Docker 이미지 빌드에 실패했습니다.\n\nSTDERR:\n%1\n\nSTDOUT:\n%2").arg(error, output);
                QMessageBox::critical(this, "빌드 실패", fullError);
            }
            buildProcess->deleteLater();
        });
    
    QStringList args;
    args << "build" << "-f" << "Dockerfile.ai" << "-t" << "patchcore-api:latest" << ".";
    
    qDebug() << "Docker 빌드 명령:" << "docker" << args.join(" ");
    buildProcess->start("docker", args);
}

void SimulationDialog::dockerStart()
{
    QProcess* startProcess = new QProcess(this);
    // 현재 디렉토리가 build라면 상위로 이동
    QString workDir = ".";
    if (QDir::currentPath().endsWith("/build")) {
        workDir = "..";
    }
    startProcess->setWorkingDirectory(workDir);
    
    dockerStartButton->setEnabled(false);
    dockerStartButton->setText("시작중");
    
    // 먼저 컨테이너가 존재하는지 확인
    QProcess* checkProcess = new QProcess(this);
    checkProcess->start("docker", QStringList() << "ps" << "-a" << "--filter" << "name=patchcore-server" << "--format" << "{{.Names}}");
    checkProcess->waitForFinished(3000);
    
    QString existingContainer = checkProcess->readAllStandardOutput().trimmed();
    checkProcess->deleteLater();
    
    QString dockerCommand;
    
    if (existingContainer.contains("patchcore-server")) {
        // 기존 컨테이너가 있으면 시작
        dockerCommand = "docker start patchcore-server";
        qDebug() << "기존 컨테이너 시작";
    } else {
        // 컨테이너가 없으면 새로 생성
        dockerCommand = "docker run -d --name patchcore-server -p 5000:5000 "
                       "-v $(pwd)/patchcore_api.py:/app/patchcore_api.py "
                       "-v $(pwd)/data:/app/data "
                       "-v $(pwd)/results:/app/results "
                       "-v $(pwd)/models:/app/models "
                       "patchcore-api:latest";
        qDebug() << "새 컨테이너 생성";
    }
    
    connect(startProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [this, startProcess](int exitCode, QProcess::ExitStatus exitStatus) {
            dockerStartButton->setEnabled(true);
            dockerStartButton->setText("시작");
            
            if (exitCode == 0) {
                QMessageBox::information(this, "컨테이너 시작", "PatchCore 컨테이너가 성공적으로 시작되었습니다!\n\nAPI 서버가 포트 5000에서 실행 중입니다.");
                refreshDockerStatus();
                
                // 2초 후 상태 다시 확인 (컨테이너 시작 시간 고려)
                QTimer::singleShot(2000, this, &SimulationDialog::refreshDockerStatus);
            } else {
                QString error = startProcess->readAllStandardError();
                QString output = startProcess->readAllStandardOutput();
                QString fullError = QString("시작 실패:\n\nSTDERR:\n%1\n\nSTDOUT:\n%2").arg(error, output);
                QMessageBox::critical(this, "시작 실패", fullError);
                refreshDockerStatus();
            }
            startProcess->deleteLater();
        });
    
    qDebug() << "Docker 시작 명령:" << dockerCommand;
    startProcess->start("/bin/sh", QStringList() << "-c" << dockerCommand);
}

void SimulationDialog::dockerStop()
{
    dockerStopButton->setEnabled(false);
    dockerStopButton->setText("중지중");
    
    QProcess* stopProcess = new QProcess(this);
    
    connect(stopProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [this, stopProcess](int exitCode, QProcess::ExitStatus exitStatus) {
            dockerStopButton->setEnabled(true);
            dockerStopButton->setText("중지");
            
            if (exitCode == 0) {
                QMessageBox::information(this, "컨테이너 중지", "PatchCore 컨테이너가 성공적으로 중지되었습니다!");
            } else {
                QString error = stopProcess->readAllStandardError();
                QString output = stopProcess->readAllStandardOutput();
                if (error.contains("No such container") || output.contains("No such container")) {
                    QMessageBox::information(this, "컨테이너 중지", "컨테이너가 이미 중지되어 있거나 존재하지 않습니다.");
                } else {
                    QMessageBox::warning(this, "중지 실패", QString("컨테이너 중지 실패:\n%1").arg(error));
                }
            }
            refreshDockerStatus();
            stopProcess->deleteLater();
        });
    
    qDebug() << "Docker 중지 명령: docker stop patchcore-server";
    stopProcess->start("docker", QStringList() << "stop" << "patchcore-server");
}

void SimulationDialog::dockerDelete()
{
    int ret = QMessageBox::question(this, "컨테이너/이미지 삭제", 
                                  "PatchCore 컨테이너와 이미지를 모두 삭제하시겠습니까?\n\n"
                                  "- 컨테이너가 중지되고 삭제됩니다\n"
                                  "- Docker 이미지도 삭제됩니다\n"
                                  "- 다시 사용하려면 빌드부터 해야 합니다",
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        QProcess* removeProcess = new QProcess(this);
        
        connect(removeProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, removeProcess](int exitCode, QProcess::ExitStatus exitStatus) {
                // 컨테이너 삭제 완료 후 이미지도 삭제
                QProcess* imageRemoveProcess = new QProcess(this);
                connect(imageRemoveProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    [this, imageRemoveProcess](int exitCode2, QProcess::ExitStatus exitStatus2) {
                        if (exitCode2 == 0) {
                            QMessageBox::information(this, "삭제 완료", "컨테이너와 이미지가 모두 삭제되었습니다!");
                        } else {
                            QMessageBox::information(this, "부분 삭제", "컨테이너는 삭제되었지만 이미지 삭제에 실패했습니다.");
                        }
                        refreshDockerStatus();
                        imageRemoveProcess->deleteLater();
                    });
                imageRemoveProcess->start("docker", QStringList() << "rmi" << "-f" << "patchcore-api:latest");
                removeProcess->deleteLater();
            });
        
        // 먼저 컨테이너 삭제 (강제로)
        removeProcess->start("docker", QStringList() << "rm" << "-f" << "patchcore-server");
    }
}

void SimulationDialog::refreshDockerStatus()
{
    // Docker 설치 확인
    QProcess dockerCheck;
    dockerCheck.start("docker", QStringList() << "--version");
    dockerCheck.waitForFinished(3000);
    
    bool dockerInstalled = (dockerCheck.exitCode() == 0);
    QString statusText = "";
    
    if (dockerInstalled) {
        statusText += "✅ Docker 설치됨\n";
        dockerInstallButton->setEnabled(false);
        
        // 모든 이미지 목록 확인
        QProcess allImagesCheck;
        allImagesCheck.start("docker", QStringList() << "images" << "--format" << "table {{.Repository}}:{{.Tag}}\t{{.Size}}" << "--no-trunc");
        allImagesCheck.waitForFinished(3000);
        
        QString allImages = allImagesCheck.readAllStandardOutput();
        QStringList imageLines = allImages.split('\n', Qt::SkipEmptyParts);
        
        if (imageLines.size() > 1) { // 헤더 제외
            statusText += QString("📦 이미지 %1개:\n").arg(imageLines.size() - 1);
            for (int i = 1; i < qMin(imageLines.size(), 4); ++i) { // 최대 3개만 표시
                QString line = imageLines[i].trimmed();
                if (!line.isEmpty()) {
                    QStringList parts = line.split('\t');
                    if (parts.size() >= 2) {
                        statusText += QString("  • %1 (%2)\n").arg(parts[0], parts[1]);
                    }
                }
            }
            if (imageLines.size() > 4) {
                statusText += QString("  ... 외 %1개\n").arg(imageLines.size() - 4);
            }
        } else {
            statusText += "📦 이미지 없음\n";
        }
        
        // PatchCore 이미지 확인
        QProcess imageCheck;
        imageCheck.start("docker", QStringList() << "images" << "-q" << "patchcore-api");
        imageCheck.waitForFinished(3000);
        
        bool imageExists = (imageCheck.exitCode() == 0 && !imageCheck.readAllStandardOutput().trimmed().isEmpty());
        
        if (imageExists) {
            statusText += "🎯 patchcore-api: 존재\n";
            dockerBuildButton->setEnabled(true);
            dockerDeleteButton->setEnabled(true);
            
            // 모든 컨테이너 상태 확인
            QProcess allContainersCheck;
            allContainersCheck.start("docker", QStringList() << "ps" << "-a" << "--format" << "{{.Names}}\t{{.Status}}");
            allContainersCheck.waitForFinished(3000);
            
            QString allContainers = allContainersCheck.readAllStandardOutput();
            QStringList containerLines = allContainers.split('\n', Qt::SkipEmptyParts);
            
            if (!containerLines.isEmpty()) {
                statusText += QString("🔧 컨테이너 %1개:\n").arg(containerLines.size());
                for (const QString& line : containerLines) {
                    QStringList parts = line.split('\t');
                    if (parts.size() >= 2) {
                        QString name = parts[0];
                        QString status = parts[1];
                        QString emoji = status.startsWith("Up") ? "🟢" : "🔴";
                        statusText += QString("  %1 %2\n").arg(emoji, name);
                    }
                }
            } else {
                statusText += "🔧 컨테이너 없음\n";
            }
            
            // PatchCore 컨테이너 상태 확인
            QProcess containerCheck;
            containerCheck.start("docker", QStringList() << "ps" << "-a" << "--filter" << "name=patchcore-server" << "--format" << "{{.Status}}");
            containerCheck.waitForFinished(3000);
            
            QString containerStatus = containerCheck.readAllStandardOutput().trimmed();
            
            if (!containerStatus.isEmpty() && containerStatus.startsWith("Up")) {
                dockerStartButton->setEnabled(false);
                dockerStopButton->setEnabled(true);
            } else {
                dockerStartButton->setEnabled(true);
                dockerStopButton->setEnabled(false);
            }
        } else {
            statusText += "🎯 patchcore-api: 없음\n";
            dockerBuildButton->setEnabled(true);
            dockerDeleteButton->setEnabled(false);
            dockerStartButton->setEnabled(false);
            dockerStopButton->setEnabled(false);
        }
    } else {
        statusText = "❌ Docker 설치 필요";
        dockerInstallButton->setEnabled(true);
        dockerBuildButton->setEnabled(false);
        dockerDeleteButton->setEnabled(false);
        dockerStartButton->setEnabled(false);
        dockerStopButton->setEnabled(false);
    }
    
    dockerStatusLabel->setText(statusText.trimmed());
}

void SimulationDialog::onTrainRatioChanged(int value)
{
    trainRatioValueLabel->setText(QString("%1%").arg(value));
}

void SimulationDialog::onTrainResultsButtonClicked()
{
    if (currentRecipeName.isEmpty()) {
        QMessageBox::warning(this, "경고", "레시피를 먼저 선택해주세요.");
        return;
    }
    
    // 학습 결과 폴더 경로 확인 (deploy/results 사용)
    QString resultsPath = QString("results/%1").arg(currentRecipeName);
    if (!QDir(resultsPath).exists()) {
        QMessageBox::information(this, "학습 결과", 
                                QString("레시피 '%1'의 탐지 결과가 없습니다.\n\n탐지를 먼저 진행해주세요.").arg(currentRecipeName));
        return;
    }
    
    // TrainResultsDialog 열기
    TrainResultsDialog* dialog = new TrainResultsDialog(currentRecipeName, this);
    dialog->exec();
    dialog->deleteLater();
}

void SimulationDialog::refreshRecipeList()
{
    // 현재 레시피 이름이 있다면 해당 레시피의 카메라 목록과 이미지 목록을 새로고침
    if (!currentRecipeName.isEmpty()) {
        // 카메라 목록 업데이트
        updateCameraList(currentRecipeName);
        
        // 이미지 목록 새로고침
        loadRecipeImages(currentRecipeName);
        
        // 현재 선택된 카메라에 맞는 이미지로 필터링
        if (!selectedCameraUuid.isEmpty()) {
            onCameraSelectionChanged(cameraComboBox->currentIndex());
        }
    } else {
        // 레시피가 선택되지 않은 상태라면 모든 목록 초기화
        if (cameraComboBox) {
            cameraComboBox->clear();
        }
        
        imagePaths.clear();
        // if (imageListWidget) {
        //     imageListWidget->clear();
        // }
        
        if (imageDisplayLabel) {
            imageDisplayLabel->clear();
            imageDisplayLabel->setText("이미지를 선택하세요");
        }
        
        currentIndex = -1;
        selectedCameraUuid.clear();
    }
}
