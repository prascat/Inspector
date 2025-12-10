#include "TestDialog.h"
#include "TeachingWidget.h"
#include "CustomMessageBox.h"
#include <QHeaderView>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>

TestDialog::TestDialog(TeachingWidget *parent)
    : QDialog(parent)
    , teachingWidget(parent)
    , currentStripCrimpMode(0) // 기본값: STRIP
    , isDragging(false)
{
    setWindowTitle("테스트 검사");
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setMinimumSize(1000, 700);
    
    setupUI();
    
    // STRIP/CRIMP 모드 동기화
    if (teachingWidget) {
        currentStripCrimpMode = teachingWidget->getCurrentStripCrimpMode();
        if (currentStripCrimpMode == 0) {
            stripRadio->setChecked(true);
        } else {
            crimpRadio->setChecked(true);
        }
    }
}

TestDialog::~TestDialog()
{
}

void TestDialog::syncStripCrimpMode(int mode)
{
    currentStripCrimpMode = mode;
    
    // 라디오 버튼 업데이트 (시그널 발생 방지)
    if (stripRadio && crimpRadio) {
        stripRadio->blockSignals(true);
        crimpRadio->blockSignals(true);
        
        if (mode == 0) {
            stripRadio->setChecked(true);
        } else {
            crimpRadio->setChecked(true);
        }
        
        stripRadio->blockSignals(false);
        crimpRadio->blockSignals(false);
    }
}

void TestDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    
    // 상단 모드 선택 및 버튼
    QHBoxLayout *topLayout = new QHBoxLayout();
    
    // STRIP/CRIMP 라디오 버튼
    QLabel *modeTitle = new QLabel("검사 모드:", this);
    modeTitle->setStyleSheet("QLabel { color: #ffffff; font-size: 14px; font-weight: bold; }");
    topLayout->addWidget(modeTitle);
    
    stripRadio = new QRadioButton("STRIP", this);
    stripRadio->setStyleSheet("QRadioButton { color: #ffffff; font-size: 13px; }");
    stripRadio->setChecked(true);
    topLayout->addWidget(stripRadio);
    
    crimpRadio = new QRadioButton("CRIMP", this);
    crimpRadio->setStyleSheet("QRadioButton { color: #ffffff; font-size: 13px; }");
    topLayout->addWidget(crimpRadio);
    
    // 라디오 버튼 그룹
    QButtonGroup *modeGroup = new QButtonGroup(this);
    modeGroup->addButton(stripRadio, 0);
    modeGroup->addButton(crimpRadio, 1);
    
    connect(stripRadio, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            onStripCrimpModeChanged(0);
        }
    });
    
    connect(crimpRadio, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            onStripCrimpModeChanged(1);
        }
    });
    
    topLayout->addStretch();
    
    loadButton = new QPushButton("이미지 불러오기", this);
    loadButton->setMinimumWidth(120);
    loadButton->setStyleSheet(
        "QPushButton { background-color: #0d47a1; color: white; border: none; "
        "padding: 8px 16px; border-radius: 4px; font-size: 13px; }"
        "QPushButton:hover { background-color: #1565c0; }"
        "QPushButton:pressed { background-color: #0a3d91; }"
    );
    topLayout->addWidget(loadButton);
    
    mainLayout->addLayout(topLayout);
    
    // 스플리터 (이미지 목록 | 결과 테이블)
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    
    // 왼쪽: 이미지 목록
    QWidget *leftWidget = new QWidget(this);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    
    QLabel *imageListLabel = new QLabel("이미지 목록", this);
    imageListLabel->setStyleSheet("QLabel { color: #ffffff; font-size: 13px; font-weight: bold; }");
    leftLayout->addWidget(imageListLabel);
    
    imageListWidget = new QListWidget(this);
    imageListWidget->setIconSize(QSize(100, 100));
    imageListWidget->setViewMode(QListView::IconMode);
    imageListWidget->setResizeMode(QListView::Adjust);
    imageListWidget->setSpacing(10);
    imageListWidget->setStyleSheet(
        "QListWidget { background-color: #2d2d2d; border: 1px solid #3d3d3d; }"
        "QListWidget::item { background-color: #2d2d2d; color: #ffffff; padding: 5px; }"
        "QListWidget::item:selected { background-color: #0d47a1; }"
        "QListWidget::item:hover { background-color: #3d3d3d; }"
    );
    leftLayout->addWidget(imageListWidget);
    
    splitter->addWidget(leftWidget);
    
    // 오른쪽: 결과 테이블
    QWidget *rightWidget = new QWidget(this);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    
    QLabel *resultLabel = new QLabel("검사 결과", this);
    resultLabel->setStyleSheet("QLabel { color: #ffffff; font-size: 13px; font-weight: bold; }");
    rightLayout->addWidget(resultLabel);
    
    resultTableWidget = new QTableWidget(this);
    resultTableWidget->setColumnCount(6);
    resultTableWidget->setHorizontalHeaderLabels({"시간", "이미지명", "패턴명", "검사방법", "결과", "검사수치"});
    resultTableWidget->horizontalHeader()->setStretchLastSection(true);
    resultTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    resultTableWidget->setColumnWidth(0, 150);
    resultTableWidget->setColumnWidth(1, 150);
    resultTableWidget->setColumnWidth(2, 120);
    resultTableWidget->setColumnWidth(3, 120);
    resultTableWidget->setColumnWidth(4, 80);
    resultTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultTableWidget->setStyleSheet(
        "QTableWidget { background-color: #2d2d2d; border: 1px solid #3d3d3d; color: #ffffff; }"
        "QTableWidget::item { padding: 5px; }"
        "QTableWidget::item:selected { background-color: #0d47a1; }"
        "QHeaderView::section { background-color: #1e1e1e; color: #ffffff; padding: 5px; "
        "border: 1px solid #3d3d3d; font-weight: bold; }"
    );
    rightLayout->addWidget(resultTableWidget);
    
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    
    mainLayout->addWidget(splitter);
    
    // 하단 버튼 및 상태
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    
    statusLabel = new QLabel("준비", this);
    statusLabel->setStyleSheet("QLabel { color: #aaaaaa; font-size: 12px; }");
    bottomLayout->addWidget(statusLabel);
    
    bottomLayout->addStretch();
    
    clearButton = new QPushButton("결과 지우기", this);
    clearButton->setMinimumWidth(100);
    clearButton->setStyleSheet(
        "QPushButton { background-color: #424242; color: white; border: none; "
        "padding: 8px 16px; border-radius: 4px; font-size: 13px; }"
        "QPushButton:hover { background-color: #525252; }"
        "QPushButton:pressed { background-color: #323232; }"
    );
    bottomLayout->addWidget(clearButton);
    
    runButton = new QPushButton("검사 실행", this);
    runButton->setMinimumWidth(100);
    runButton->setEnabled(false);
    runButton->setStyleSheet(
        "QPushButton { background-color: #2e7d32; color: white; border: none; "
        "padding: 8px 16px; border-radius: 4px; font-size: 13px; }"
        "QPushButton:hover { background-color: #388e3c; }"
        "QPushButton:pressed { background-color: #1b5e20; }"
        "QPushButton:disabled { background-color: #424242; color: #888888; }"
    );
    bottomLayout->addWidget(runButton);
    
    closeButton = new QPushButton("닫기", this);
    closeButton->setMinimumWidth(80);
    closeButton->setStyleSheet(
        "QPushButton { background-color: #c62828; color: white; border: none; "
        "padding: 8px 16px; border-radius: 4px; font-size: 13px; }"
        "QPushButton:hover { background-color: #d32f2f; }"
        "QPushButton:pressed { background-color: #b71c1c; }"
    );
    bottomLayout->addWidget(closeButton);
    
    mainLayout->addLayout(bottomLayout);
    
    // 시그널 연결
    connect(loadButton, &QPushButton::clicked, this, &TestDialog::onLoadImages);
    connect(runButton, &QPushButton::clicked, this, &TestDialog::onRunTest);
    connect(clearButton, &QPushButton::clicked, this, &TestDialog::onClearResults);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    connect(imageListWidget, &QListWidget::itemClicked, this, &TestDialog::onImageSelected);
    
    // 다크 테마 적용
    setStyleSheet("QDialog { background-color: #1e1e1e; }");
}

void TestDialog::onLoadImages()
{
    QStringList filePaths = QFileDialog::getOpenFileNames(
        this,
        "이미지 선택",
        QDir::homePath(),
        "이미지 파일 (*.png *.jpg *.jpeg *.bmp)"
    );
    
    if (filePaths.isEmpty()) {
        return;
    }
    
    loadImageThumbnails(filePaths);
    statusLabel->setText(QString("이미지 %1개 로드됨").arg(filePaths.size()));
}

void TestDialog::loadImageThumbnails(const QStringList &imagePaths)
{
    imageListWidget->clear();
    imagePathList = imagePaths;
    
    for (const QString &imagePath : imagePaths) {
        QFileInfo fileInfo(imagePath);
        
        // 썸네일 생성
        cv::Mat image = cv::imread(imagePath.toStdString());
        if (image.empty()) {
            continue;
        }
        
        // 100x100 크기로 리사이즈
        cv::Mat thumbnail;
        cv::resize(image, thumbnail, cv::Size(100, 100));
        
        // OpenCV BGR -> QImage RGB 변환
        cv::Mat rgb;
        cv::cvtColor(thumbnail, rgb, cv::COLOR_BGR2RGB);
        QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
        QPixmap pixmap = QPixmap::fromImage(qimg.copy());
        
        // 리스트 아이템 생성
        QListWidgetItem *item = new QListWidgetItem(QIcon(pixmap), fileInfo.fileName());
        item->setData(Qt::UserRole, imagePath); // 전체 경로 저장
        imageListWidget->addItem(item);
    }
    
    runButton->setEnabled(!imagePathList.isEmpty());
}

void TestDialog::onImageSelected(QListWidgetItem *item)
{
    if (!item) return;
    
    QString imagePath = item->data(Qt::UserRole).toString();
    statusLabel->setText(QString("선택: %1").arg(QFileInfo(imagePath).fileName()));
    
    // 선택한 이미지를 TeachingWidget에 표시
    if (teachingWidget) {
        cv::Mat image = cv::imread(imagePath.toStdString());
        if (!image.empty()) {
            // 현재 모드에 맞게 cameraFrame 업데이트
            if (currentStripCrimpMode == 0) {
                // STRIP 모드
                teachingWidget->setCameraFrame(0, image);
            } else {
                // CRIMP 모드
                teachingWidget->setCameraFrame(1, image);
            }
        }
    }
}

void TestDialog::onRunTest()
{
    if (!teachingWidget) {
        CustomMessageBox(this, CustomMessageBox::Warning, "오류", "TeachingWidget이 없습니다.").exec();
        return;
    }
    
    if (imagePathList.isEmpty()) {
        CustomMessageBox(this, CustomMessageBox::Warning, "알림", "검사할 이미지가 없습니다.").exec();
        return;
    }
    
    // 현재 선택된 라디오 버튼 모드 사용
    // currentStripCrimpMode는 이미 라디오 버튼 토글로 업데이트됨
    
    int totalImages = imagePathList.size();
    int processedImages = 0;
    
    for (const QString &imagePath : imagePathList) {
        runInspectionOnImage(imagePath);
        processedImages++;
        statusLabel->setText(QString("검사 중... %1/%2").arg(processedImages).arg(totalImages));
        QApplication::processEvents();
    }
    
    statusLabel->setText(QString("검사 완료: %1개 이미지 처리됨").arg(totalImages));
}

void TestDialog::runInspectionOnImage(const QString &imagePath)
{
    // 이미지 로드
    cv::Mat image = cv::imread(imagePath.toStdString());
    if (image.empty()) {
        qWarning() << "[TestDialog] 이미지 로드 실패:" << imagePath;
        return;
    }
    
    // TeachingWidget의 cameraFrames에 이미지 설정
    if (currentStripCrimpMode == 0) {
        // STRIP 모드
        teachingWidget->setCameraFrame(0, image);
    } else {
        // CRIMP 모드
        teachingWidget->setCameraFrame(1, image);
    }
    
    // 검사 직접 실행 (RUN 버튼 시뮬레이션 없이)
    InspectionResult result = teachingWidget->runInspection();
    
    // 결과를 테이블에 추가 (INS 패턴별로)
    QFileInfo fileInfo(imagePath);
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    
    // 디버그: 검사 결과 개수 확인
    qDebug() << "[TestDialog] 검사 완료 - insResults 개수:" << result.insResults.size()
             << "insMethodTypes 개수:" << result.insMethodTypes.size();
    
    // INS 결과가 없으면 1행 추가 (전체 결과만)
    if (result.insResults.isEmpty()) {
        QString overallResult = result.isPassed ? "PASS" : "NG";
        addResultToTable(timestamp, fileInfo.fileName(), "-", "-", overallResult, "-");
        return;
    }
    
    // INS 패턴별로 행 추가
    for (auto it = result.insResults.begin(); it != result.insResults.end(); ++it) {
        QUuid patternId = it.key();
        bool patternPassed = it.value();
        
        // 검사 방법 타입 확인
        int methodType = -1;
        if (result.insMethodTypes.contains(patternId)) {
            methodType = result.insMethodTypes[patternId];
        }
        
        // 패턴 이름 미리 가져오기 (디버그용)
        QString patternName = "Unknown";
        if (teachingWidget) {
            patternName = teachingWidget->getPatternName(patternId);
        }
        
        qDebug() << "[TestDialog] 패턴:" << patternName << "methodType:" << methodType 
                 << "passed:" << patternPassed << "mode:" << currentStripCrimpMode;
        
        // 현재 모드에 맞지 않는 검사는 스킵
        // STRIP 전용: StripThickness(3), StripNeckCut(4)
        // CRIMP 전용: 나머지 검사 (PatternMatch, Segmentation, Anomaly 등)
        if (currentStripCrimpMode == 0) {
            // STRIP 모드: StripThickness, StripNeckCut만 표시
            if (methodType != 3 && methodType != 4) {
                qDebug() << "[TestDialog] STRIP 모드에서" << patternName << "스킵 (methodType:" << methodType << ")";
                continue;
            }
        } else {
            // CRIMP 모드: StripThickness, StripNeckCut 제외
            if (methodType == 3 || methodType == 4) {
                qDebug() << "[TestDialog] CRIMP 모드에서" << patternName << "스킵 (methodType:" << methodType << ")";
                continue;
            }
        }
        
        // 검사 방법 가져오기
        QString inspectionMethod = "-";
        switch (methodType) {
            case 0: inspectionMethod = "PatternMatch"; break;
            case 1: inspectionMethod = "Segmentation"; break;
            case 2: inspectionMethod = "Anomaly"; break;
            case 3: inspectionMethod = "StripThickness"; break;
            case 4: inspectionMethod = "StripNeckCut"; break;
            default: inspectionMethod = QString("Type%1").arg(methodType); break;
        }
        
        // 검사 결과 (불량 검출 = NG)
        QString inspectionResult = patternPassed ? "PASS" : "NG";
        
        // 검사 수치 가져오기
        QString inspectionValue = "-";
        
        // 검사 방법별 수치 표시
        if (methodType == 2) {
            // Anomaly 검사: defect 개수 표시
            if (result.anomalyDefectContours.contains(patternId)) {
                int defectCount = result.anomalyDefectContours[patternId].size();
                inspectionValue = QString("%1개 검출").arg(defectCount);
            }
        } else if (result.insScores.contains(patternId)) {
            // PatternMatch, Segmentation 등: 점수 표시
            double score = result.insScores[patternId];
            inspectionValue = QString::number(score, 'f', 2);
        } else if (result.stripMeasuredThicknessAvg.contains(patternId)) {
            // StripThickness: 평균 두께
            int avgThickness = result.stripMeasuredThicknessAvg[patternId];
            inspectionValue = QString("%1 px").arg(avgThickness);
        } else if (result.stripNeckAvgWidths.contains(patternId)) {
            // StripNeckCut: 평균 너비
            double avgWidth = result.stripNeckAvgWidths[patternId];
            inspectionValue = QString("%1 px").arg(avgWidth, 0, 'f', 1);
        }
        
        addResultToTable(timestamp, fileInfo.fileName(), patternName, 
                        inspectionMethod, inspectionResult, inspectionValue);
    }
}

void TestDialog::addResultToTable(const QString &timestamp, const QString &imageName,
                                  const QString &patternName, const QString &inspectionMethod,
                                  const QString &result, const QString &value)
{
    int row = resultTableWidget->rowCount();
    resultTableWidget->insertRow(row);
    
    // 시간
    QTableWidgetItem *timeItem = new QTableWidgetItem(timestamp);
    resultTableWidget->setItem(row, 0, timeItem);
    
    // 이미지명
    QTableWidgetItem *nameItem = new QTableWidgetItem(imageName);
    resultTableWidget->setItem(row, 1, nameItem);
    
    // 패턴명
    QTableWidgetItem *patternItem = new QTableWidgetItem(patternName);
    resultTableWidget->setItem(row, 2, patternItem);
    
    // 검사방법
    QTableWidgetItem *methodItem = new QTableWidgetItem(inspectionMethod);
    resultTableWidget->setItem(row, 3, methodItem);
    
    // 결과 (PASS/NG/FAIL 색상 표시)
    QTableWidgetItem *resultItem = new QTableWidgetItem(result);
    if (result == "PASS") {
        resultItem->setForeground(QBrush(QColor("#4caf50"))); // 초록색
    } else if (result == "NG") {
        resultItem->setForeground(QBrush(QColor("#f44336"))); // 빨간색 (불량)
    } else if (result == "FAIL") {
        resultItem->setForeground(QBrush(QColor("#ff9800"))); // 주황색 (검사 실패)
    }
    resultTableWidget->setItem(row, 4, resultItem);
    
    // 검사수치
    QTableWidgetItem *valueItem = new QTableWidgetItem(value);
    resultTableWidget->setItem(row, 5, valueItem);
    
    // 스크롤을 가장 아래로
    resultTableWidget->scrollToBottom();
}

void TestDialog::onClearResults()
{
    resultTableWidget->setRowCount(0);
    statusLabel->setText("결과 지워짐");
}

void TestDialog::onStripCrimpModeChanged(int mode)
{
    currentStripCrimpMode = mode;
    
    // TeachingWidget의 setStripCrimpMode 호출 (레시피 자동 변경)
    if (teachingWidget) {
        teachingWidget->setStripCrimpMode(mode);
    }
}

void TestDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        isDragging = true;
        dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void TestDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (isDragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - dragPosition);
        event->accept();
    }
}

void TestDialog::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        isDragging = false;
        event->accept();
    }
}
