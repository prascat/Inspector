#include "TestDialog.h"
#include "TeachingWidget.h"
#include "CustomMessageBox.h"
#include "CustomFileDialog.h"
#include <QHeaderView>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>
#include <QXmlStreamWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QApplication>
#include <QKeyEvent>

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
    
    QPushButton *saveButton = new QPushButton("결과 저장", this);
    saveButton->setMinimumWidth(100);
    saveButton->setStyleSheet(
        "QPushButton { background-color: #1976d2; color: white; border: none; "
        "padding: 8px 16px; border-radius: 4px; font-size: 13px; }"
        "QPushButton:hover { background-color: #2196f3; }"
        "QPushButton:pressed { background-color: #0d47a1; }"
    );
    bottomLayout->addWidget(saveButton);
    
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
    connect(saveButton, &QPushButton::clicked, this, &TestDialog::onSaveResults);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    connect(imageListWidget, &QListWidget::itemClicked, this, &TestDialog::onImageSelected);
    connect(resultTableWidget, &QTableWidget::cellClicked, this, &TestDialog::onResultTableClicked);
    
    // 다크 테마 적용
    setStyleSheet("QDialog { background-color: #1e1e1e; }");
}

void TestDialog::onLoadImages()
{
    QStringList filePaths = CustomFileDialog::getOpenFileNames(
        this,
        "이미지 파일 선택",
        QDir::homePath(),
        "Images (*.png *.jpg *.jpeg *.bmp)"
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
    
    // 검사 시작 전 기존 결과 모두 클리어
    resultTableWidget->setRowCount(0);
    stripResults.clear();
    crimpResults.clear();
    
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
    if (!teachingWidget) return;
    
    // 이미지 로드
    cv::Mat image = cv::imread(imagePath.toStdString());
    if (image.empty()) {
        qWarning() << "[TestDialog] 이미지 로드 실패:" << imagePath;
        return;
    }
    
    // TeachingWidget의 cameraFrames에 이미지 설정
    int imageIndex = (currentStripCrimpMode == 0) ? 0 : 1;
    teachingWidget->setCameraFrame(imageIndex, image);
    
    // RUN 버튼을 물리적으로 클릭 (FID 검출 및 패턴 회전 처리 포함)
    teachingWidget->triggerRunButton();
    
    // RUN 버튼 처리 대기 (이벤트 루프 실행)
    QApplication::processEvents();
    QThread::msleep(100); // 검사 완료 대기
    QApplication::processEvents();
    
    // CameraView에서 검사 결과 가져오기
    CameraView *cameraView = teachingWidget->getCameraView();
    if (!cameraView) return;
    
    const InspectionResult& result = cameraView->getLastInspectionResult();
    
    QList<PatternInfo> &patterns = cameraView->getPatterns();
    
    // 현재 모드의 INS 패턴만 필터링
    QList<PatternInfo*> currentInsPatterns;
    for (PatternInfo &pattern : patterns) {
        if (pattern.type == PatternType::INS && 
            pattern.stripCrimpMode == currentStripCrimpMode) {
            currentInsPatterns.append(&pattern);
        }
    }
    
    if (currentInsPatterns.isEmpty()) {
        qDebug() << "[TestDialog] 현재 모드에 INS 패턴이 없음";
        return;
    }
    
    // 테이블에 새 행 추가
    int row = resultTableWidget->rowCount();
    resultTableWidget->insertRow(row);
    
    QFileInfo fileInfo(imagePath);
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    
    // 시간, 이미지명
    QTableWidgetItem *timeItem = new QTableWidgetItem(timestamp);
    resultTableWidget->setItem(row, 0, timeItem);
    
    QTableWidgetItem *nameItem = new QTableWidgetItem(fileInfo.fileName());
    resultTableWidget->setItem(row, 1, nameItem);
    
    // 결과 데이터 저장용
    TestResultRow resultRow;
    resultRow.timestamp = timestamp;
    resultRow.imageName = fileInfo.fileName();
    
    // 각 INS 패턴 결과 (PASS/NG)
    for (int i = 0; i < currentInsPatterns.size(); ++i) {
        PatternInfo *pattern = currentInsPatterns[i];
        int col = 2 + i; // 시간, 이미지명 다음부터
        
        // 패턴 결과 확인
        bool patternPassed = true;
        if (result.insResults.contains(pattern->id)) {
            patternPassed = result.insResults[pattern->id];
        }
        
        // 결과 (PASS/NG)
        QString resultText = patternPassed ? "PASS" : "NG";
        QTableWidgetItem *resultItem = new QTableWidgetItem(resultText);
        if (patternPassed) {
            resultItem->setForeground(QBrush(QColor("#4caf50")));
        } else {
            resultItem->setForeground(QBrush(QColor("#f44336")));
        }
        resultTableWidget->setItem(row, col, resultItem);
        
        // 결과 데이터에 저장
        resultRow.patternResults[pattern->name] = resultText;
    }
    
    // 현재 모드의 결과 리스트에 추가
    if (currentStripCrimpMode == 0) {
        stripResults.append(resultRow);
    } else {
        crimpResults.append(resultRow);
    }
    
    // 검사 완료 후 RUN 버튼 다시 끄기 (STOP 상태로 만들기)
    teachingWidget->triggerRunButton();
    QApplication::processEvents();
    
    // 스크롤을 가장 아래로
    resultTableWidget->scrollToBottom();
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
    
    // 현재 모드의 저장된 결과도 클리어
    if (currentStripCrimpMode == 0) {
        stripResults.clear();
    } else {
        crimpResults.clear();
    }
    
    statusLabel->setText("결과 지워짐");
}

void TestDialog::onStripCrimpModeChanged(int mode)
{
    currentStripCrimpMode = mode;
    
    // TeachingWidget의 setStripCrimpMode 호출 (레시피 자동 변경)
    if (teachingWidget) {
        teachingWidget->setStripCrimpMode(mode);
    }
    
    // 모드 변경 시 테이블 재구성 (INS 패턴이 모드별로 다름)
    rebuildResultTable();
}

void TestDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    
    // 다이얼로그가 표시될 때 테이블 재구성
    rebuildResultTable();
}

void TestDialog::rebuildResultTable()
{
    if (!teachingWidget) return;
    
    // 기존 테이블 내용 저장 (필요시)
    QList<QStringList> existingData;
    for (int row = 0; row < resultTableWidget->rowCount(); ++row) {
        QStringList rowData;
        for (int col = 0; col < resultTableWidget->columnCount(); ++col) {
            QTableWidgetItem *item = resultTableWidget->item(row, col);
            rowData << (item ? item->text() : "");
        }
        existingData.append(rowData);
    }
    
    // CameraView에서 현재 모드의 INS 패턴 가져오기
    CameraView *cameraView = teachingWidget->getCameraView();
    if (!cameraView) return;
    
    QList<PatternInfo> &patterns = cameraView->getPatterns();
    
    qDebug() << "[TestDialog] rebuildResultTable - 전체 패턴 수:" << patterns.size() 
             << "현재 모드:" << currentStripCrimpMode;
    
    // 현재 STRIP/CRIMP 모드에 맞는 INS 패턴만 필터링
    QStringList insPatternNames;
    
    for (const PatternInfo &pattern : patterns) {
        if (pattern.type == PatternType::INS) {
            qDebug() << "  - INS 패턴:" << pattern.name 
                     << "stripCrimpMode:" << pattern.stripCrimpMode 
                     << "enabled:" << pattern.enabled;
        }
        
        if (pattern.type == PatternType::INS && 
            pattern.stripCrimpMode == currentStripCrimpMode &&
            pattern.enabled) {
            insPatternNames.append(pattern.name);
        }
    }
    
    // 현재 패턴명 리스트 저장 (결과 저장 시 사용)
    currentPatternNames = insPatternNames;
    
    // 테이블 컬럼 재구성: 시간, 이미지명, INS패턴1, INS패턴2, ...
    int totalColumns = 2 + insPatternNames.size(); // 시간, 이미지명 + INS 패턴들
    
    resultTableWidget->clear();
    resultTableWidget->setColumnCount(totalColumns);
    
    // 헤더 설정
    QStringList headers;
    headers << "시간" << "이미지명";
    
    for (const QString &patternName : insPatternNames) {
        headers << patternName;
    }
    
    resultTableWidget->setHorizontalHeaderLabels(headers);
    
    // 컬럼 너비 설정
    resultTableWidget->setColumnWidth(0, 150); // 시간
    resultTableWidget->setColumnWidth(1, 200); // 이미지명
    
    for (int i = 0; i < insPatternNames.size(); ++i) {
        resultTableWidget->setColumnWidth(2 + i, 100); // INS 패턴 결과
    }
    
    resultTableWidget->horizontalHeader()->setStretchLastSection(true);
    
    qDebug() << "[TestDialog] 테이블 재구성 완료 - 현재 모드:" << (currentStripCrimpMode == 0 ? "STRIP" : "CRIMP")
             << ", INS 패턴 수:" << insPatternNames.size() << ", 총 컬럼:" << totalColumns;
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

void TestDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete) {
        // 현재 선택된 이미지 항목 삭제
        QListWidgetItem *currentItem = imageListWidget->currentItem();
        if (currentItem) {
            int row = imageListWidget->row(currentItem);
            if (row >= 0 && row < imagePathList.size()) {
                // 리스트에서 제거
                imagePathList.removeAt(row);
                delete imageListWidget->takeItem(row);
                
                statusLabel->setText(QString("이미지 삭제됨 (남은 이미지: %1개)").arg(imagePathList.size()));
                
                // 이미지가 없으면 검사 버튼 비활성화
                if (imagePathList.isEmpty()) {
                    runButton->setEnabled(false);
                }
            }
        }
    }
    
    QDialog::keyPressEvent(event);
}

void TestDialog::onResultTableClicked(int row, int column)
{
    if (row < 0 || !teachingWidget) return;
    
    // 테이블에서 이미지명 가져오기 (1번 컬럼)
    QTableWidgetItem *nameItem = resultTableWidget->item(row, 1);
    if (!nameItem) return;
    
    QString imageName = nameItem->text();
    
    // imagePathList에서 해당 이미지 찾기
    QString imagePath;
    for (const QString &path : imagePathList) {
        if (path.endsWith(imageName)) {
            imagePath = path;
            break;
        }
    }
    
    if (imagePath.isEmpty()) {
        qWarning() << "[TestDialog] 이미지 경로를 찾을 수 없음:" << imageName;
        return;
    }
    
    // 이미지 로드
    cv::Mat image = cv::imread(imagePath.toStdString());
    if (image.empty()) {
        qWarning() << "[TestDialog] 이미지 로드 실패:" << imagePath;
        return;
    }
    
    // TeachingWidget의 cameraFrames에 이미지 설정
    int imageIndex = (currentStripCrimpMode == 0) ? 0 : 1;
    teachingWidget->setCameraFrame(imageIndex, image);
    
    // RUN 버튼을 물리적으로 클릭 (검사 실행)
    teachingWidget->triggerRunButton();
    
    statusLabel->setText(QString("검사 결과 표시: %1").arg(imageName));
}

void TestDialog::closeEvent(QCloseEvent *event)
{
    if (hasUnsavedResults()) {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Question);
        msgBox.setTitle("검사 결과 저장");
        msgBox.setMessage("저장되지 않은 검사 결과가 있습니다.\n결과를 저장하시겠습니까?");
        msgBox.setButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        
        int ret = msgBox.exec();
        
        if (ret == QMessageBox::Yes) {
            onSaveResults();
            // 저장 다이얼로그에서 취소하면 닫기도 취소
            if (hasUnsavedResults()) {
                event->ignore();
                return;
            }
        } else if (ret == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
    }
    
    event->accept();
}

bool TestDialog::hasUnsavedResults() const
{
    return !stripResults.isEmpty() || !crimpResults.isEmpty();
}

void TestDialog::onSaveResults()
{
    if (!hasUnsavedResults()) {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Information);
        msgBox.setTitle("알림");
        msgBox.setMessage("저장할 검사 결과가 없습니다.");
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }
    
    // 파일 형식 선택
    CustomMessageBox formatBox(this);
    formatBox.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    formatBox.setStyleSheet("QDialog { background-color: #000000; }");
    formatBox.setIcon(CustomMessageBox::Question);
    formatBox.setTitle("저장 형식 선택");
    formatBox.setMessage("어떤 형식으로 저장하시겠습니까?");
    formatBox.setButtons(QMessageBox::Ok | QMessageBox::No | QMessageBox::Cancel);
    formatBox.setButtonText(QMessageBox::Ok, "TXT");
    formatBox.setButtonText(QMessageBox::No, "XML");
    formatBox.setButtonText(QMessageBox::Cancel, "CANCEL");
    
    int formatChoice = formatBox.exec();
    
    QString filter;
    QString defaultExt;
    
    if (formatChoice == QMessageBox::Ok) {
        // TXT 선택
        filter = "Text Files (*.txt)";
        defaultExt = ".txt";
    } else if (formatChoice == QMessageBox::No) {
        // XML 선택
        filter = "XML Files (*.xml)";
        defaultExt = ".xml";
    } else {
        return; // CANCEL
    }
    
    // 저장 경로 선택
    QString defaultFileName = QString("test_results_%1%2")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"))
        .arg(defaultExt);
    
    QString filePath = CustomFileDialog::getSaveFileName(
        this,
        "검사 결과 저장",
        defaultFileName,
        filter
    );
    
    if (filePath.isEmpty()) {
        return;
    }
    
    // 확장자 확인 및 추가
    if (!filePath.endsWith(defaultExt, Qt::CaseInsensitive)) {
        filePath += defaultExt;
    }
    
    // 형식에 따라 저장
    try {
        if (formatChoice == QMessageBox::Ok) {
            saveResultsToTxt(filePath);
        } else if (formatChoice == QMessageBox::No) {
            saveResultsToXml(filePath);
        }
        
        // 저장 성공 시 결과 클리어
        stripResults.clear();
        crimpResults.clear();
        
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Information);
        msgBox.setTitle("저장 완료");
        msgBox.setMessage(QString("검사 결과가 저장되었습니다.\n%1").arg(filePath));
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
        
    } catch (const std::exception &e) {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Critical);
        msgBox.setTitle("저장 실패");
        msgBox.setMessage(QString("파일 저장 중 오류가 발생했습니다.\n%1").arg(e.what()));
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
    }
}

void TestDialog::saveResultsToTxt(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        throw std::runtime_error("파일을 열 수 없습니다.");
    }
    
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    
    out << "===============================================\n";
    out << "          테스트 검사 결과 리포트\n";
    out << "===============================================\n";
    out << "생성 일시: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n\n";
    
    // STRIP 결과
    if (!stripResults.isEmpty()) {
        out << "[ STRIP 모드 검사 결과 ]\n";
        out << "-----------------------------------------------\n";
        
        for (const TestResultRow &row : stripResults) {
            out << "시간: " << row.timestamp << " | 이미지: " << row.imageName << "\n";
            for (auto it = row.patternResults.begin(); it != row.patternResults.end(); ++it) {
                out << "  - " << it.key() << ": " << it.value() << "\n";
            }
            out << "\n";
        }
    }
    
    // CRIMP 결과
    if (!crimpResults.isEmpty()) {
        out << "[ CRIMP 모드 검사 결과 ]\n";
        out << "-----------------------------------------------\n";
        
        for (const TestResultRow &row : crimpResults) {
            out << "시간: " << row.timestamp << " | 이미지: " << row.imageName << "\n";
            for (auto it = row.patternResults.begin(); it != row.patternResults.end(); ++it) {
                out << "  - " << it.key() << ": " << it.value() << "\n";
            }
            out << "\n";
        }
    }
    
    out << "===============================================\n";
    file.close();
}

void TestDialog::saveResultsToXml(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        throw std::runtime_error("파일을 열 수 없습니다.");
    }
    
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    
    xml.writeStartElement("TestResults");
    xml.writeAttribute("generatedAt", QDateTime::currentDateTime().toString(Qt::ISODate));
    
    // STRIP 결과
    if (!stripResults.isEmpty()) {
        xml.writeStartElement("StripMode");
        for (const TestResultRow &row : stripResults) {
            xml.writeStartElement("Result");
            xml.writeAttribute("timestamp", row.timestamp);
            xml.writeAttribute("image", row.imageName);
            
            for (auto it = row.patternResults.begin(); it != row.patternResults.end(); ++it) {
                xml.writeStartElement("Pattern");
                xml.writeAttribute("name", it.key());
                xml.writeAttribute("result", it.value());
                xml.writeEndElement();
            }
            
            xml.writeEndElement(); // Result
        }
        xml.writeEndElement(); // StripMode
    }
    
    // CRIMP 결과
    if (!crimpResults.isEmpty()) {
        xml.writeStartElement("CrimpMode");
        for (const TestResultRow &row : crimpResults) {
            xml.writeStartElement("Result");
            xml.writeAttribute("timestamp", row.timestamp);
            xml.writeAttribute("image", row.imageName);
            
            for (auto it = row.patternResults.begin(); it != row.patternResults.end(); ++it) {
                xml.writeStartElement("Pattern");
                xml.writeAttribute("name", it.key());
                xml.writeAttribute("result", it.value());
                xml.writeEndElement();
            }
            
            xml.writeEndElement(); // Result
        }
        xml.writeEndElement(); // CrimpMode
    }
    
    xml.writeEndElement(); // TestResults
    xml.writeEndDocument();
    file.close();
}

void TestDialog::saveResultsToJson(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        throw std::runtime_error("파일을 열 수 없습니다.");
    }
    
    QJsonObject root;
    root["generatedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    // STRIP 결과
    if (!stripResults.isEmpty()) {
        QJsonArray stripArray;
        for (const TestResultRow &row : stripResults) {
            QJsonObject resultObj;
            resultObj["timestamp"] = row.timestamp;
            resultObj["image"] = row.imageName;
            
            QJsonObject patternsObj;
            for (auto it = row.patternResults.begin(); it != row.patternResults.end(); ++it) {
                patternsObj[it.key()] = it.value();
            }
            resultObj["patterns"] = patternsObj;
            
            stripArray.append(resultObj);
        }
        root["stripMode"] = stripArray;
    }
    
    // CRIMP 결과
    if (!crimpResults.isEmpty()) {
        QJsonArray crimpArray;
        for (const TestResultRow &row : crimpResults) {
            QJsonObject resultObj;
            resultObj["timestamp"] = row.timestamp;
            resultObj["image"] = row.imageName;
            
            QJsonObject patternsObj;
            for (auto it = row.patternResults.begin(); it != row.patternResults.end(); ++it) {
                patternsObj[it.key()] = it.value();
            }
            resultObj["patterns"] = patternsObj;
            
            crimpArray.append(resultObj);
        }
        root["crimpMode"] = crimpArray;
    }
    
    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
}
