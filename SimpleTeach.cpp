#include <QScrollBar>
#include "SimpleTeach.h"
#include "TeachingWidget.h"
#include <opencv2/opencv.hpp>

SimpleTeach::SimpleTeach(TeachingWidget* teachingWidget, QWidget *parent)
    : QWidget(parent), 
      teachingWidget(teachingWidget),
      selectedCameraIndex(-1),
      currentStep(CAMERA_SELECTION),
      zoomFactor(1.0),
      panOffset(0, 0),
      isPanning(false),
      twoFingerMode(false),
      lastPinchScale(1.0),
      currentAction(None)
{
    setFixedSize(800, 600);
    
    // **터치 이벤트 및 제스처 활성화**
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    grabGesture(Qt::PinchGesture);
    grabGesture(Qt::PanGesture);
    
    // **라이브 업데이트 타이머 초기화**
    liveUpdateTimer = new QTimer(this);
    liveUpdateTimer->setInterval(CAMERA_INTERVAL);
    connect(liveUpdateTimer, &QTimer::timeout, this, &SimpleTeach::updateLiveImage);
    
    moveTimer = new QTimer(this);
    moveTimer->setInterval(100);
    moveTimer->setSingleShot(false);
    connect(moveTimer, &QTimer::timeout, this, &SimpleTeach::onRepeatAction);
    
    sizeTimer = new QTimer(this);
    sizeTimer->setInterval(100);
    sizeTimer->setSingleShot(false);
    connect(sizeTimer, &QTimer::timeout, this, &SimpleTeach::onRepeatAction);
    
    setupUI();
    updateCameraSlots();
    updateStepDisplay();
    
}

SimpleTeach::~SimpleTeach() {
    // 소멸자
}

void SimpleTeach::setupUI() {
    // **메인 레이아웃 - 800x600에 맞게 설정**
    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(5, 5, 5, 5);  // 작은 마진
    mainLayout->setSpacing(5);  // 작은 간격
    
    // **스택 위젯 생성**
    contentStack = new QStackedWidget(this);
    contentStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(contentStack, 1);
    
    // 카메라 선택 페이지 생성
    createCameraSelectionPage();
    
    // 티칭 영상 표시 페이지 생성  
    createTeachingViewPage();
    
    // **하단 버튼 영역 - 높이 제한**
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);
    buttonLayout->setContentsMargins(5, 5, 5, 5);
    
    backButton = new QPushButton("◀ BACK", this);
    backButton->setFixedSize(80, 35);  // 작은 크기
    backButton->setEnabled(false);
    
    nextButton = new QPushButton("NEXT ▶", this);
    nextButton->setFixedSize(80, 35);  // 작은 크기
    nextButton->setEnabled(false);
    
    QPushButton* closeButton = new QPushButton("✕ CLOSE", this);
    closeButton->setFixedSize(80, 35);  // 작은 크기
    
    // **버튼 스타일 - 작은 폰트**
    QString buttonStyle = 
        "QPushButton {"
        "    font-size: 12px; font-weight: bold;"
        "    border: 2px solid #ccc; border-radius: 5px;"
        "    padding: 5px; background-color: white;"
        "}"
        "QPushButton:hover {"
        "    border-color: #2196F3; background-color: #E3F2FD;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #BBDEFB;"
        "}"
        "QPushButton:disabled {"
        "    color: #999; border-color: #ddd; background-color: #f5f5f5;"
        "}";
    
    backButton->setStyleSheet(buttonStyle);
    nextButton->setStyleSheet(buttonStyle);
    closeButton->setStyleSheet(buttonStyle);
    
    // 버튼 이벤트 연결
    connect(backButton, &QPushButton::clicked, this, &SimpleTeach::onBackClicked);
    connect(nextButton, &QPushButton::clicked, this, &SimpleTeach::onNextClicked);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    
    // **버튼 레이아웃 - 좌우 정렬**
    buttonLayout->addWidget(backButton);
    buttonLayout->addWidget(nextButton);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(closeButton);
    
    mainLayout->addLayout(buttonLayout);
}

void SimpleTeach::createCameraSelectionPage() {
    cameraListWidget = new QWidget();
    cameraListWidget->setStyleSheet("background-color: white;");
    
    QVBoxLayout* layout = new QVBoxLayout(cameraListWidget);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);
    
    QLabel* titleLabel = new QLabel("카메라 선택");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
    layout->addWidget(titleLabel);
    
    cameraGridLayout = new QGridLayout();
    cameraGridLayout->setSpacing(15);
    
    // 카메라 정보 가져오기
    cameraInfos = teachingWidget->getCameraInfos();
    
    // 2x2 그리드로 카메라 버튼 배치
    for (int i = 0; i < 4; i++) {
        int row = i / 2;
        int col = i % 2;
        
        // QPushButton 사용
        QPushButton* cameraBtn = new QPushButton();
        cameraBtn->setMinimumSize(180, 120);
        cameraBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        
        // 연결 상태에 따른 텍스트와 색상 설정
        bool isConnected = (i < cameraInfos.size() && cameraInfos[i].isConnected);
        
        if (isConnected) {
            // **연결된 카메라 - 초록색 (CONNECTED)**
            cameraBtn->setText(QString("CAM %1\n\nCONNECTED").arg(i + 1));
            cameraBtn->setStyleSheet(
                "QPushButton {"
                "   font-size: 28px;"
                "   font-weight: bold;"
                "   color: white;"
                "   background-color: #4CAF50;"  // 초록색
                "   border: 4px solid #388E3C;"
                "   border-radius: 15px;"
                "   padding: 15px;"
                "}"
                "QPushButton:hover {"
                "   background-color: #66BB6A;"  // 연한 초록색
                "   border: 4px solid #4CAF50;"
                "}"
                "QPushButton:pressed {"
                "   background-color: #388E3C;"  // 진한 초록색
                "}"
            );
            
            // 클릭 이벤트 연결 - SELECTED로 변경하고 카메라 선택
            connect(cameraBtn, &QPushButton::clicked, this, [this, i, cameraBtn]() {
                // **선택된 카메라 - 파란색 (SELECTED)**
                cameraBtn->setText(QString("CAM %1\n\nSELECTED").arg(i + 1));
                cameraBtn->setStyleSheet(
                    "QPushButton {"
                    "   font-size: 28px;"
                    "   font-weight: bold;"
                    "   color: white;"
                    "   background-color: #2196F3;"  // 파란색
                    "   border: 4px solid #1976D2;"
                    "   border-radius: 15px;"
                    "   padding: 15px;"
                    "}"
                );
                
                // 다른 모든 버튼들은 CONNECTED(초록색)로 되돌리기
                for (int j = 0; j < cameraSelectButtons.size(); j++) {
                    if (j != i && cameraSelectButtons[j]) {
                        QPushButton* otherBtn = qobject_cast<QPushButton*>(cameraSelectButtons[j]);
                        if (otherBtn && otherBtn->isEnabled()) {
                            otherBtn->setText(QString("CAM %1\n\nCONNECTED").arg(j + 1));
                            otherBtn->setStyleSheet(
                                "QPushButton {"
                                "   font-size: 28px;"
                                "   font-weight: bold;"
                                "   color: white;"
                                "   background-color: #4CAF50;"  // 다시 초록색
                                "   border: 4px solid #388E3C;"
                                "   border-radius: 15px;"
                                "   padding: 15px;"
                                "}"
                                "QPushButton:hover {"
                                "   background-color: #66BB6A;"
                                "   border: 4px solid #4CAF50;"
                                "}"
                                "QPushButton:pressed {"
                                "   background-color: #388E3C;"
                                "}"
                            );
                        }
                    }
                }
                
                // 카메라 선택 처리
                onCameraSelected(i);
            });
            
        } else {
            // **연결되지 않은 카메라 - 빨간색 (DISCONNECTED)**
            cameraBtn->setText(QString("CAM %1\n\nDISCONNECTED").arg(i + 1));
            cameraBtn->setStyleSheet(
                "QPushButton {"
                "   font-size: 28px;"
                "   font-weight: bold;"
                "   color: white;"
                "   background-color: #F44336;"  // 빨간색
                "   border: 4px solid #D32F2F;"
                "   border-radius: 15px;"
                "   padding: 15px;"
                "   background-image: repeating-linear-gradient("
                "      45deg,"
                "      transparent,"
                "      transparent 10px,"
                "      rgba(255,255,255,0.1) 10px,"
                "      rgba(255,255,255,0.1) 20px"
                "   );"
                "}"
            );
            cameraBtn->setEnabled(false);  // 비활성화
        }
        
        cameraGridLayout->addWidget(cameraBtn, row, col);
        cameraSelectButtons.append(cameraBtn);
    }
    
    layout->addLayout(cameraGridLayout);
    layout->addStretch();
    
    contentStack->addWidget(cameraListWidget);
}

void SimpleTeach::createPatternEditButtons() {
    patternEditWidget = new QWidget();
    patternEditWidget->setStyleSheet("background-color: #e8e8e8; border: 1px solid #ccc; border-radius: 5px; padding: 3px;");
    
    QVBoxLayout* editLayout = new QVBoxLayout(patternEditWidget);
    editLayout->setContentsMargins(3, 3, 3, 3);
    editLayout->setSpacing(3);
    
    QString buttonStyle = 
        "QPushButton {"
        "    font-size: 18px; font-weight: bold;"
        "    border: 2px solid #666; border-radius: 6px;"
        "    padding: 5px; background-color: white; color: #333;"
        "}"
        "QPushButton:hover { background-color: #e0e0e0; border-color: #333; }"
        "QPushButton:pressed { background-color: #d0d0d0; }";
    
    // **이동 버튼들**
    QGridLayout* moveLayout = new QGridLayout();
    moveLayout->setSpacing(3);
    moveLayout->setContentsMargins(5, 3, 5, 3);
    
    moveUpButton = new QPushButton("⬆");
    moveDownButton = new QPushButton("⬇");
    moveLeftButton = new QPushButton("⬅");
    moveRightButton = new QPushButton("➡");
    
    moveUpButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    moveDownButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    moveLeftButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    moveRightButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    moveUpButton->setStyleSheet(buttonStyle);
    moveDownButton->setStyleSheet(buttonStyle);
    moveLeftButton->setStyleSheet(buttonStyle);
    moveRightButton->setStyleSheet(buttonStyle);
    
    moveLayout->addWidget(moveUpButton, 0, 1);
    moveLayout->addWidget(moveLeftButton, 1, 0);
    moveLayout->addWidget(moveRightButton, 1, 2);
    moveLayout->addWidget(moveDownButton, 2, 1);
    
    moveLayout->setRowStretch(0, 1);
    moveLayout->setRowStretch(1, 1);
    moveLayout->setRowStretch(2, 1);
    moveLayout->setColumnStretch(0, 1);
    moveLayout->setColumnStretch(1, 1);
    moveLayout->setColumnStretch(2, 1);
    
    editLayout->addLayout(moveLayout, 1);
    
    // **크기 조정 버튼들**
    QGridLayout* sizeLayout = new QGridLayout();
    sizeLayout->setSpacing(3);
    sizeLayout->setContentsMargins(5, 3, 5, 3);
    
    sizeUpButton = new QPushButton("⬆");
    sizeDownButton = new QPushButton("⬇");
    sizeLeftButton = new QPushButton("⬅");
    sizeRightButton = new QPushButton("➡");
    
    QString sizeButtonStyle = 
        "QPushButton {"
        "    font-size: 18px; font-weight: bold;"
        "    border: 2px solid #FF5722; border-radius: 6px;"
        "    padding: 5px; background-color: white; color: #FF5722;"
        "}"
        "QPushButton:hover { background-color: #FF5722; color: white; }"
        "QPushButton:pressed { background-color: #E64A19; }";
    
    sizeUpButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    sizeDownButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    sizeLeftButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    sizeRightButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    sizeUpButton->setStyleSheet(sizeButtonStyle);
    sizeDownButton->setStyleSheet(sizeButtonStyle);
    sizeLeftButton->setStyleSheet(sizeButtonStyle);
    sizeRightButton->setStyleSheet(sizeButtonStyle);
    
    sizeLayout->addWidget(sizeUpButton, 0, 0);
    sizeLayout->addWidget(sizeRightButton, 0, 1);
    sizeLayout->addWidget(sizeDownButton, 1, 0);
    sizeLayout->addWidget(sizeLeftButton, 1, 1);
    
    sizeLayout->setRowStretch(0, 1);
    sizeLayout->setRowStretch(1, 1);
    sizeLayout->setColumnStretch(0, 1);
    sizeLayout->setColumnStretch(1, 1);
    
    editLayout->addLayout(sizeLayout, 1);
    
    // **마우스 이벤트 연결 - pressed/released 사용**
    connect(moveUpButton, &QPushButton::pressed, this, [this]() { startRepeatAction(MoveUp); });
    connect(moveUpButton, &QPushButton::released, this, &SimpleTeach::stopRepeatAction);
    
    connect(moveDownButton, &QPushButton::pressed, this, [this]() { startRepeatAction(MoveDown); });
    connect(moveDownButton, &QPushButton::released, this, &SimpleTeach::stopRepeatAction);
    
    connect(moveLeftButton, &QPushButton::pressed, this, [this]() { startRepeatAction(MoveLeft); });
    connect(moveLeftButton, &QPushButton::released, this, &SimpleTeach::stopRepeatAction);
    
    connect(moveRightButton, &QPushButton::pressed, this, [this]() { startRepeatAction(MoveRight); });
    connect(moveRightButton, &QPushButton::released, this, &SimpleTeach::stopRepeatAction);
    
    connect(sizeUpButton, &QPushButton::pressed, this, [this]() { startRepeatAction(SizeUp); });
    connect(sizeUpButton, &QPushButton::released, this, &SimpleTeach::stopRepeatAction);
    
    connect(sizeDownButton, &QPushButton::pressed, this, [this]() { startRepeatAction(SizeDown); });
    connect(sizeDownButton, &QPushButton::released, this, &SimpleTeach::stopRepeatAction);
    
    connect(sizeLeftButton, &QPushButton::pressed, this, [this]() { startRepeatAction(SizeLeft); });
    connect(sizeLeftButton, &QPushButton::released, this, &SimpleTeach::stopRepeatAction);
    
    connect(sizeRightButton, &QPushButton::pressed, this, [this]() { startRepeatAction(SizeRight); });
    connect(sizeRightButton, &QPushButton::released, this, &SimpleTeach::stopRepeatAction);
    
    patternEditWidget->setVisible(true);
}

void SimpleTeach::createTeachingViewPage() {
    teachingViewWidget = new QWidget();
    teachingViewWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    teachingLayout = new QHBoxLayout(teachingViewWidget);
    teachingLayout->setContentsMargins(5, 5, 5, 5);
    teachingLayout->setSpacing(5);
    
    // 영상 영역
    QWidget* imageWidget = new QWidget();
    imageWidget->setFixedSize(500, 520);
    imageWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    teachingLayout->addWidget(imageWidget);
    
    // **오른쪽 패널 - 터치 스크롤만 추가 (기존 건드리지 않음)**
    QScrollArea* rightScrollArea = new QScrollArea();
    rightScrollArea->setFixedWidth(285);
    rightScrollArea->setWidgetResizable(true);
    rightScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rightScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);  // **스크롤바 보이게**
    rightScrollArea->verticalScrollBar()->setSingleStep(30);
    rightScrollArea->setStyleSheet(
        "QScrollArea {"
        "    background-color: #f5f5f5;"
        "    border: none;"
        "}"
        // **터치 스크롤 활성화를 위한 스타일**
        "QScrollBar:vertical {"
        "    background: #e0e0e0;"
        "    width: 20px;"
        "    border-radius: 10px;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: #888888;"
        "    min-height: 30px;"
        "    border-radius: 8px;"
        "}"
    );
    
    QWidget* rightPanelWidget = new QWidget();
    rightPanelLayout = new QVBoxLayout(rightPanelWidget);
    rightPanelLayout->setContentsMargins(5, 5, 5, 5);
    rightPanelLayout->setSpacing(8);
    
    // 1. 패턴 목록
    QLabel* titleLabel = new QLabel("패턴목록");
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: white; background-color: #2196F3; padding: 8px; border-radius: 3px;");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setFixedHeight(30);
    rightPanelLayout->addWidget(titleLabel);
    
    // **패턴 스크롤 영역도 같은 방식**
    QScrollArea* patternScrollArea = new QScrollArea();
    patternScrollArea->setFixedHeight(150);
    patternScrollArea->setWidgetResizable(true);
    patternScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    patternScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);  // **스크롤바 보이게**
    patternScrollArea->verticalScrollBar()->setSingleStep(20);
    patternScrollArea->setStyleSheet(
        "QScrollArea { background-color: #f5f5f5; border: 1px solid #ccc; border-radius: 3px; }"
        "QScrollBar:vertical {"
        "    background: #e0e0e0;"
        "    width: 15px;"
        "    border-radius: 7px;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: #888888;"
        "    min-height: 20px;"
        "    border-radius: 5px;"
        "}"
    );
    
    // 패턴 버튼들을 담을 위젯
    patternButtonWidget = new QWidget();
    patternButtonLayout = new QVBoxLayout(patternButtonWidget);
    patternButtonLayout->setContentsMargins(5, 5, 5, 5);
    patternButtonLayout->setSpacing(3);
    patternButtonLayout->addStretch();
    
    patternScrollArea->setWidget(patternButtonWidget);
    rightPanelLayout->addWidget(patternScrollArea);

    // 나머지는 기존과 동일...
    createPatternEditButtons();
    rightPanelLayout->addWidget(patternEditWidget);

    QHBoxLayout* zoomLayout = new QHBoxLayout();
    zoomLayout->setSpacing(3);

    zoomInButton = new QPushButton("+Zoom");
    zoomOutButton = new QPushButton("-Zoom");
    zoomResetButton = new QPushButton("1:1");

    QString zoomButtonStyle = 
        "QPushButton {"
        "    font-size: 14px; font-weight: bold;"
        "    border: 2px solid #607D8B; border-radius: 6px;"
        "    padding: 8px; background-color: white; color: #607D8B;"
        "}"
        "QPushButton:hover { background-color: #607D8B; color: white; }"
        "QPushButton:pressed { background-color: #455A64; }";

    zoomInButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    zoomOutButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    zoomResetButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    zoomInButton->setStyleSheet(zoomButtonStyle);
    zoomOutButton->setStyleSheet(zoomButtonStyle);
    zoomResetButton->setStyleSheet(zoomButtonStyle);

    zoomLayout->addWidget(zoomInButton, 1);
    zoomLayout->addWidget(zoomOutButton, 1);
    zoomLayout->addWidget(zoomResetButton, 1);
    rightPanelLayout->addLayout(zoomLayout);

    loadExistingButton = new QPushButton("패턴 불러오기");
    loadExistingButton->setStyleSheet(
        "QPushButton {"
        "    font-size: 12px; font-weight: bold;"
        "    border: 2px solid #2196F3; border-radius: 6px;"
        "    padding: 10px; background-color: white; color: #2196F3;"
        "}"
        "QPushButton:hover { background-color: #2196F3; color: white; }"
        "QPushButton:pressed { background-color: #1976D2; }"
    );
    loadExistingButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    rightPanelLayout->addWidget(loadExistingButton);

    saveRecipeButton = new QPushButton("레시피 저장");
    saveRecipeButton->setStyleSheet(
        "QPushButton {"
        "    font-size: 12px; font-weight: bold;"
        "    border: 2px solid #FF9800; border-radius: 6px;"
        "    padding: 10px; background-color: white; color: #FF9800;"
        "}"
        "QPushButton:hover { background-color: #FF9800; color: white; }"
        "QPushButton:pressed { background-color: #f57c00; }"
    );
    saveRecipeButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    rightPanelLayout->addWidget(saveRecipeButton);
    
    rightPanelLayout->addStretch();
    
    rightScrollArea->setWidget(rightPanelWidget);
    teachingLayout->addWidget(rightScrollArea);
    
    // 기존 시그널 연결 그대로
    connect(loadExistingButton, &QPushButton::clicked, this, &SimpleTeach::onLoadExistingPatterns);
    connect(saveRecipeButton, &QPushButton::clicked, this, &SimpleTeach::onSaveRecipeClicked);
    connect(zoomInButton, &QPushButton::clicked, this, &SimpleTeach::onZoomInClicked);
    connect(zoomOutButton, &QPushButton::clicked, this, &SimpleTeach::onZoomOutClicked);
    connect(zoomResetButton, &QPushButton::clicked, this, &SimpleTeach::onZoomResetClicked);
    
    contentStack->addWidget(teachingViewWidget);
}

void SimpleTeach::startRepeatAction(ActiveAction action) {
    currentAction = action;
    
    // **즉시 한 번 실행**
    performAction(action);
    
    // **타이머 시작**
    if (action >= MoveUp && action <= MoveRight) {
        moveTimer->start();
    } else if (action >= SizeUp && action <= SizeRight) {
        sizeTimer->start();
    }
}

void SimpleTeach::stopRepeatAction() {
    moveTimer->stop();
    sizeTimer->stop();
    currentAction = None;
}

void SimpleTeach::onRepeatAction() {
    if (currentAction != None) {
        performAction(currentAction);
    }
}

void SimpleTeach::performAction(ActiveAction action) {
    switch (action) {
        case MoveUp:
            moveSelectedPattern(0, -SIMPLE_MOVE_PIXELS);
            break;
        case MoveDown:
            moveSelectedPattern(0, SIMPLE_MOVE_PIXELS);
            break;
        case MoveLeft:
            moveSelectedPattern(-SIMPLE_MOVE_PIXELS, 0);
            break;
        case MoveRight:
            moveSelectedPattern(SIMPLE_MOVE_PIXELS, 0);
            break;
        case SizeUp:
            resizeSelectedPattern(0, SIMPLE_MOVE_PIXELS);
            break;
        case SizeDown:
            resizeSelectedPattern(0, -SIMPLE_MOVE_PIXELS);
            break;
        case SizeLeft:
            resizeSelectedPattern(-SIMPLE_MOVE_PIXELS, 0);
            break;
        case SizeRight:
            resizeSelectedPattern(SIMPLE_MOVE_PIXELS, 0);
            break;
        default:
            break;
    }
    
    updatePatternList();
    update();
}

void SimpleTeach::updateLiveImage() {
    if (!teachingWidget) {
        return;
    }
    
    // **TeachingWidget에서 현재 프레임 가져오기**
    cv::Mat frame = teachingWidget->getCurrentFrame();
    
    if (frame.empty()) {
        return;
    }
    
    
    try {
        QImage qimg;
        if (frame.channels() == 3) {
            cv::Mat rgbFrame;
            cv::cvtColor(frame, rgbFrame, cv::COLOR_BGR2RGB);
            qimg = QImage(rgbFrame.data, rgbFrame.cols, rgbFrame.rows, rgbFrame.step, QImage::Format_RGB888);
        } else if (frame.channels() == 1) {
            qimg = QImage(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_Grayscale8);
        } else {
            return;
        }
        
        if (qimg.isNull()) {
            return;
        }
        
        QPixmap pixmap = QPixmap::fromImage(qimg.copy());
        if (pixmap.isNull()) {
            return;
        }
        
        originalPixmap = pixmap;
        update();
        
    } catch (const cv::Exception& e) {
    }
}

void SimpleTeach::drawTeachingPatterns(QPainter& painter) {
    if (teachingPatterns.isEmpty()) return;
    
    QRect imageArea(5, 5, 500, 520);
    
    for (const PatternInfo& pattern : teachingPatterns) {
        bool isSelected = (pattern.id == selectedPatternId);
        
        QColor patternColor;
        QString patternTypeName;
        
        switch (pattern.type) {
            case PatternType::ROI:
                patternColor = UIColors::ROI_COLOR;           // #E6C27C (연한 노란색)
                patternTypeName = "ROI";
                break;
            case PatternType::Fiducial:
                patternColor = UIColors::FIDUCIAL_COLOR;      // #7094DB (연한 파란색)
                patternTypeName = "FID";
                break;
            case PatternType::Inspection:
                patternColor = UIColors::INSPECTION_COLOR;    // #8BCB8B (연한 초록색)
                patternTypeName = "INS";
                break;
            default:
                patternColor = Qt::gray;
                patternTypeName = "기타";
                break;
        }
        
        // 원본 좌표계 그대로 사용하여 SimpleTeach 영상 영역으로 변환
        QSize originalSize = originalPixmap.size();
        if (originalSize.isEmpty()) continue;
        
        double scaleX = 500.0 / originalSize.width();
        double scaleY = 520.0 / originalSize.height();
        double scale = qMin(scaleX, scaleY);
        
        QRect scaledRect(
            qRound(pattern.rect.x() * scale),
            qRound(pattern.rect.y() * scale),
            qRound(pattern.rect.width() * scale),
            qRound(pattern.rect.height() * scale)
        );
        
        scaledRect = QRect(
            qRound(scaledRect.x() * zoomFactor) + imageArea.x() + panOffset.x(),
            qRound(scaledRect.y() * zoomFactor) + imageArea.y() + panOffset.y(),
            qRound(scaledRect.width() * zoomFactor),
            qRound(scaledRect.height() * zoomFactor)
        );
        
        QSize scaledImageSize(qRound(originalSize.width() * scale * zoomFactor),
                              qRound(originalSize.height() * scale * zoomFactor));
        QPoint centerOffset((imageArea.width() - scaledImageSize.width()) / 2,
                           (imageArea.height() - scaledImageSize.height()) / 2);
        
        scaledRect.translate(centerOffset);
        
        if (!scaledRect.intersects(imageArea)) continue;
        
        int lineWidth = isSelected ? 3 : 2;
        
        if (isSelected) {
            painter.setPen(QPen(Qt::yellow, lineWidth));
            painter.setBrush(QBrush(QColor(255, 255, 0, 30)));
        } else {
            painter.setPen(QPen(patternColor, lineWidth));
            painter.setBrush(QBrush(QColor(patternColor.red(), patternColor.green(), patternColor.blue(), 20)));
        }
        
        painter.drawRect(scaledRect);
        
        QRect textRect = scaledRect;
        textRect.setHeight(15);
        textRect.translate(0, -15);
        
        textRect.adjust(-1, 0, 1, 0);
        
        if (textRect.intersects(imageArea)) {
            painter.fillRect(textRect, isSelected ? Qt::yellow : patternColor);
            painter.setPen(Qt::black);
            painter.setFont(QFont("Arial", 8, QFont::Bold));
            painter.drawText(textRect, Qt::AlignCenter, QString("[%1] %2").arg(patternTypeName).arg(pattern.name));
        }
        
        if (isSelected) {
            painter.setPen(QPen(Qt::red, 1));
            painter.setBrush(Qt::red);
            
            int handleSize = 6;
            
            auto drawHandle = [&](QPoint pos) {
                QRect handle(pos.x() - handleSize/2, pos.y() - handleSize/2, handleSize, handleSize);
                if (handle.intersects(imageArea)) {
                    painter.drawRect(handle);
                }
            };
            
            drawHandle(scaledRect.topLeft());
            drawHandle(scaledRect.topRight());
            drawHandle(scaledRect.bottomLeft());
            drawHandle(scaledRect.bottomRight());
        }
    }
}

void SimpleTeach::mousePressEvent(QMouseEvent* event) {
    if (currentStep == TEACHING_VIEW && event->button() == Qt::LeftButton && !twoFingerMode) {
        QRect imageArea(5, 5, 500, 520);
        if (imageArea.contains(event->pos())) {
            
            // **디스플레이 좌표를 원본 좌표로 변환**
            QPoint clickPos = event->pos() - imageArea.topLeft() - panOffset;
            
            QSize originalSize = originalPixmap.size();
            if (originalSize.isEmpty()) return;
            
            double scaleX = 500.0 / originalSize.width();
            double scaleY = 520.0 / originalSize.height();
            double scale = qMin(scaleX, scaleY);
            
            QSize scaledImageSize(qRound(originalSize.width() * scale * zoomFactor),
                                  qRound(originalSize.height() * scale * zoomFactor));
            QPoint centerOffset((imageArea.width() - scaledImageSize.width()) / 2,
                               (imageArea.height() - scaledImageSize.height()) / 2);
            
            clickPos -= centerOffset;
            clickPos = QPoint(qRound(clickPos.x() / zoomFactor),
                             qRound(clickPos.y() / zoomFactor));
            
            QPoint originalPos(qRound(clickPos.x() / scale),
                              qRound(clickPos.y() / scale));
            
            // **패턴 선택 확인**
            QUuid hitPatternId;
            for (const PatternInfo& pattern : teachingPatterns) {
                if (pattern.rect.contains(originalPos)) {
                    hitPatternId = pattern.id;
                    break;
                }
            }
            
            if (!hitPatternId.isNull()) {
                selectPattern(hitPatternId);
                
                // **CameraView에서도 같은 패턴 선택하도록 시그널 전송**
                if (teachingWidget && teachingWidget->getCameraView()) {
                    teachingWidget->getCameraView()->setSelectedPatternId(hitPatternId);
                }
            } else {
                // 패닝 시작
                isPanning = true;
                lastPanPoint = event->pos();
                setCursor(Qt::ClosedHandCursor);
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void SimpleTeach::updateScalingInfo() {
    // CameraView의 스케일링 정보는 건드리지 않음
    // SimpleTeach에서는 자체적으로 좌표 변환 처리
}

void SimpleTeach::setOriginalPixmap(const QPixmap& pixmap) {
    originalPixmap = pixmap;
    if (!originalPixmap.isNull()) {
        updateDisplayedPixmap();
    }
}

void SimpleTeach::setZoomFactor(double factor) {
    double oldZoom = zoomFactor;
    zoomFactor = qBound(0.2, factor, 5.0);
    
    if (qAbs(oldZoom - zoomFactor) > 0.01) {
        updateDisplayedPixmap();
        update();
    }
}

void SimpleTeach::resetZoom() {
    zoomFactor = 1.0;
    panOffset = QPoint(0, 0);
    updateDisplayedPixmap();
    update();
}

void SimpleTeach::updateDisplayedPixmap() {
    if (originalPixmap.isNull()) {
        scaledPixmap = QPixmap();
        return;
    }
    
    // **500x520 영역에 맞게 스케일링**
    QSize imageAreaSize(500, 520);
    QSize originalSize = originalPixmap.size();
    
    // 종횡비 유지하면서 500x520에 맞는 크기 계산
    double scaleX = (double)imageAreaSize.width() / originalSize.width();
    double scaleY = (double)imageAreaSize.height() / originalSize.height();
    double baseScale = qMin(scaleX, scaleY);
    
    // 줌 팩터 적용
    double finalScale = baseScale * zoomFactor;
    
    QSize scaledSize(qRound(originalSize.width() * finalScale),
                     qRound(originalSize.height() * finalScale));
    
    scaledPixmap = originalPixmap.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    
}

bool SimpleTeach::event(QEvent* event) {
    if (currentStep != TEACHING_VIEW) {
        return QWidget::event(event);
    }
    
    switch (event->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd: {
            QTouchEvent* touchEvent = static_cast<QTouchEvent*>(event);
            const QList<QTouchEvent::TouchPoint>& touchPoints = touchEvent->touchPoints();
            
            if (touchPoints.count() == 1) {
                const QTouchEvent::TouchPoint& point = touchPoints.first();
                QPoint currentPos = point.pos().toPoint();
                
                if (event->type() == QEvent::TouchBegin) {
                    isPanning = true;
                    twoFingerMode = false;
                    lastPanPoint = currentPos;
                    
                } else if (event->type() == QEvent::TouchUpdate && isPanning && !twoFingerMode) {
                    QPoint delta = currentPos - lastPanPoint;
                    panOffset += delta;
                    lastPanPoint = currentPos;
                    update();
                    
                } else if (event->type() == QEvent::TouchEnd) {
                    isPanning = false;
                }
                
            } else if (touchPoints.count() == 2) {
                const QTouchEvent::TouchPoint& point1 = touchPoints[0];
                const QTouchEvent::TouchPoint& point2 = touchPoints[1];
                
                QPoint pos1 = point1.pos().toPoint();
                QPoint pos2 = point2.pos().toPoint();
                
                if (event->type() == QEvent::TouchBegin || !twoFingerMode) {
                    twoFingerMode = true;
                    isPanning = false;
                    lastTouchPoint1 = pos1;
                    lastTouchPoint2 = pos2;
                    
                } else if (event->type() == QEvent::TouchUpdate && twoFingerMode) {
                    double currentDistance = QLineF(pos1, pos2).length();
                    double lastDistance = QLineF(lastTouchPoint1, lastTouchPoint2).length();
                    
                    if (lastDistance > 20) {
                        double scaleFactor = currentDistance / lastDistance;
                        
                        if (scaleFactor > 0.8 && scaleFactor < 1.2) {
                            double newZoom = zoomFactor * scaleFactor;
                            setZoomFactor(newZoom);
                        }
                    }
                    
                    lastTouchPoint1 = pos1;
                    lastTouchPoint2 = pos2;
                    
                } else if (event->type() == QEvent::TouchEnd) {
                    twoFingerMode = false;
                }
            }
            
            event->accept();
            return true;
        }
        
        default:
            break;
    }
    
    return QWidget::event(event);
}

void SimpleTeach::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    if (currentStep == TEACHING_VIEW) {
        // **500x520 영상 영역 정의**
        QRect imageArea(5, 5, 500, 520);
        
        // **originalPixmap이 있으면 그리기**
        if (!originalPixmap.isNull()) {
            QSize pixSize = originalPixmap.size();
            QSize viewSize = imageArea.size();
            
            if (!pixSize.isEmpty() && !viewSize.isEmpty()) {
                double scaleFactor;
                
                if (zoomFactor == 1.0) {
                    // **1:1 줌일 때 - 화면에 맞게 스케일링**
                    if (viewSize.width() * pixSize.height() > viewSize.height() * pixSize.width()) {
                        scaleFactor = (double)viewSize.height() / pixSize.height();
                    } else {
                        scaleFactor = (double)viewSize.width() / pixSize.width();
                    }
                } else {
                    // **다른 줌일 때**
                    if (viewSize.width() * pixSize.height() > viewSize.height() * pixSize.width()) {
                        scaleFactor = (double)viewSize.height() / pixSize.height();
                    } else {
                        scaleFactor = (double)viewSize.width() / pixSize.width();
                    }
                    scaleFactor *= zoomFactor;
                }
                
                int newWidth = qRound(pixSize.width() * scaleFactor);
                int newHeight = qRound(pixSize.height() * scaleFactor);
                
                // **중앙 정렬 - 정확한 중앙 계산**
                int centerOffsetX = (viewSize.width() - newWidth) / 2;
                int centerOffsetY = (viewSize.height() - newHeight) / 2;
                
                QRect targetRect(
                    imageArea.x() + centerOffsetX + panOffset.x(),
                    imageArea.y() + centerOffsetY + panOffset.y(),  // **이 부분이 중요**
                    newWidth,
                    newHeight
                );
                
                // **영상 그리기**
                painter.drawPixmap(targetRect, originalPixmap);
            }
        } else {
            // **영상이 없을 때 안내 메시지**
            painter.setPen(Qt::black);
            painter.setFont(QFont("Arial", 16));
            painter.drawText(imageArea, Qt::AlignCenter, "영상을 불러오는 중...");
        }
        
        // **패턴 그리기**
        drawTeachingPatterns(painter);
    } else {
        // **카메라 선택 화면에서는 기본 paintEvent 호출**
        QWidget::paintEvent(event);
    }
}

void SimpleTeach::mouseMoveEvent(QMouseEvent* event) {
    if (currentStep == TEACHING_VIEW && isPanning && !twoFingerMode) {
        QPoint delta = event->pos() - lastPanPoint;
        panOffset += delta;
        lastPanPoint = event->pos();
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void SimpleTeach::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        isPanning = false;
        setCursor(Qt::ArrowCursor);
    }
    QWidget::mouseReleaseEvent(event);
}

void SimpleTeach::wheelEvent(QWheelEvent* event) {
    if (currentStep == TEACHING_VIEW) {
        // **500x520 영역 내에서만 휠 줌 허용**
        QRect imageArea(5, 5, 500, 520);
        if (imageArea.contains(event->position().toPoint())) {
            double scaleFactor = event->angleDelta().y() > 0 ? 1.15 : 0.85;
            setZoomFactor(zoomFactor * scaleFactor);
            event->accept();
        }
    } else {
        QWidget::wheelEvent(event);
    }
}

// **확대/축소 버튼 슬롯들**
void SimpleTeach::onZoomInClicked() {
    setZoomFactor(zoomFactor * 1.25);
}

void SimpleTeach::onZoomOutClicked() {
    setZoomFactor(zoomFactor * 0.8);
}

void SimpleTeach::onZoomResetClicked() {
    resetZoom();
}

// **패턴 편집 터치 버튼 슬롯들 구현**
void SimpleTeach::onMoveUpClicked() {
    moveSelectedPattern(0, -SIMPLE_MOVE_PIXELS);  
    updatePatternList();
    update();
}

void SimpleTeach::onMoveDownClicked() {
    moveSelectedPattern(0, SIMPLE_MOVE_PIXELS);   
    updatePatternList();
    update();
}

void SimpleTeach::onMoveLeftClicked() {
    moveSelectedPattern(-SIMPLE_MOVE_PIXELS, 0);  
    updatePatternList();
    update();
}

void SimpleTeach::onMoveRightClicked() {
    moveSelectedPattern(SIMPLE_MOVE_PIXELS, 0);   
    updatePatternList();
    update();
}

void SimpleTeach::onSizeUpClicked() {
    resizeSelectedPattern(0, SIMPLE_MOVE_PIXELS); 
    updatePatternList();
    update();
}

void SimpleTeach::onSizeDownClicked() {
    resizeSelectedPattern(0, -SIMPLE_MOVE_PIXELS); 
    updatePatternList();
    update();
}

void SimpleTeach::onSizeLeftClicked() {
    resizeSelectedPattern(-SIMPLE_MOVE_PIXELS, 0); 
    updatePatternList();
    update();
}

void SimpleTeach::onSizeRightClicked() {
    resizeSelectedPattern(SIMPLE_MOVE_PIXELS, 0);  
    updatePatternList();
    update();
}

void SimpleTeach::moveSelectedPattern(int dx, int dy) {
    if (selectedPatternId.isNull()) return;
    
    // **teachingPatterns에서 패턴 찾아서 이동**
    for (PatternInfo& pattern : teachingPatterns) {
        if (pattern.id == selectedPatternId) {
            QRectF oldRect = pattern.rect;
            QRectF newRect = oldRect.translated(dx, dy);
            
            // **원본 이미지 범위 내로 제한**
            if (originalPixmap.isNull()) return;
            
            QSize imgSize = originalPixmap.size();
            if (newRect.left() < 0) newRect.moveLeft(0);
            if (newRect.top() < 0) newRect.moveTop(0);
            if (newRect.right() >= imgSize.width()) newRect.moveRight(imgSize.width() - 1);
            if (newRect.bottom() >= imgSize.height()) newRect.moveBottom(imgSize.height() - 1);
            
            // **teachingPatterns 업데이트**
            pattern.rect = newRect;
            
            // **CameraView에 변경사항 전송**
            emit patternMoved(selectedPatternId, newRect);      
            break;
        }
    }
}

void SimpleTeach::resizeSelectedPattern(int dw, int dh) {
    if (selectedPatternId.isNull()) return;
    
    // **teachingPatterns에서 패턴 찾아서 크기 변경**
    for (PatternInfo& pattern : teachingPatterns) {
        if (pattern.id == selectedPatternId) {
            QRectF oldRect = pattern.rect;
            QRectF newRect = oldRect.adjusted(0, 0, dw, dh);
            
            // **최소 크기 제한**
            if (newRect.width() < 10) newRect.setWidth(10);
            if (newRect.height() < 10) newRect.setHeight(10);
            
            // **원본 이미지 범위 내로 제한**
            if (originalPixmap.isNull()) return;
            
            QSize imgSize = originalPixmap.size();
            if (newRect.right() >= imgSize.width()) newRect.setWidth(imgSize.width() - newRect.x());
            if (newRect.bottom() >= imgSize.height()) newRect.setHeight(imgSize.height() - newRect.y());
            
            // **teachingPatterns 업데이트**
            pattern.rect = newRect;
            
            // **CameraView에 변경사항 전송**
            emit patternResized(selectedPatternId, newRect);
            break;
        }
    }
}



void SimpleTeach::onSaveRecipeClicked() {
    if (teachingPatterns.isEmpty()) {
        QMessageBox::warning(this, "저장 오류", "저장할 패턴이 없습니다.");
        return;
    }
    teachingWidget->saveRecipe();
}

void SimpleTeach::onLoadExistingPatterns() {
    // 개별 레시피 파일 시스템 사용 - 기본 레시피가 있다면 로드
    RecipeManager recipeManager;
    QStringList availableRecipes = recipeManager.getAvailableRecipes();
    
    bool success = false;
    if (!availableRecipes.isEmpty()) {
        // 첫 번째 사용 가능한 레시피 로드
        teachingWidget->onRecipeSelected(availableRecipes.first());
        success = true;
    }
    
    if (success) {
        loadExistingPatternsFromTeachingWidget();
        updatePatternList();
        update();
        
        QMessageBox::information(this, "불러오기 완료", 
            QString("총 %1개의 패턴을 불러왔습니다.").arg(teachingPatterns.size()));
    } else {
        QMessageBox::warning(this, "불러오기 실패", "레시피 파일을 불러올 수 없습니다.");
    }
}

void SimpleTeach::updatePatternList() {
    // 기존 패턴 버튼들 모두 제거
    for (QPushButton* button : patternButtons) {
        patternButtonLayout->removeWidget(button);
        button->deleteLater();
    }
    patternButtons.clear();
    
    // 패턴별로 버튼 생성
    for (const PatternInfo& pattern : teachingPatterns) {
        QString displayName = pattern.name;
        if (displayName.isEmpty()) {
            QString typeName;
            switch (pattern.type) {
                case PatternType::ROI: typeName = "ROI"; break;
                case PatternType::Fiducial: typeName = "FID"; break;
                case PatternType::Inspection: typeName = "INS"; break;
                default: typeName = "패턴"; break;
            }
            displayName = QString("%1_%2").arg(typeName).arg(pattern.id.toString().left(8));
        }
        
        QPushButton* patternButton = new QPushButton(displayName);
        patternButton->setFixedHeight(40);
        patternButton->setProperty("patternId", pattern.id.toString());
        
        QString buttonStyle;
        bool isSelected = (pattern.id == selectedPatternId);
        
        // **패턴 타입별 색상 설정**
        QColor typeColor;
        switch (pattern.type) {
            case PatternType::ROI: 
                typeColor = UIColors::ROI_COLOR;           // #E6C27C
                break;
            case PatternType::Fiducial: 
                typeColor = UIColors::FIDUCIAL_COLOR;      // #7094DB
                break;
            case PatternType::Inspection: 
                typeColor = UIColors::INSPECTION_COLOR;    // #8BCB8B
                break;
            default: 
                typeColor = Qt::gray; 
                break;
        }
        
        if (isSelected) {
            // **선택된 버튼은 해당 패턴 타입 색상으로 강조**
            buttonStyle = QString(
                "QPushButton {"
                "    font-size: 14px; font-weight: bold;"
                "    border: 3px solid %1; border-radius: 6px;"
                "    padding: 8px; background-color: %1; color: white;"
                "}"
                "QPushButton:pressed { background-color: %2; }"
            ).arg(typeColor.name()).arg(typeColor.darker(120).name());
        } else {
            // **일반 버튼은 흰 배경에 패턴 타입 색상 테두리**
            buttonStyle = QString(
                "QPushButton {"
                "    font-size: 14px; font-weight: bold;"
                "    border: 2px solid %1; border-radius: 6px;"
                "    padding: 8px; background-color: white; color: %1;"
                "}"
                "QPushButton:hover { background-color: %1; color: white; }"
                "QPushButton:pressed { background-color: %2; }"
            ).arg(typeColor.name()).arg(typeColor.darker(120).name());
        }
        
        patternButton->setStyleSheet(buttonStyle);
        
        connect(patternButton, &QPushButton::clicked, this, [this, pattern]() {
            selectPattern(pattern.id);
        });
        
        patternButtonLayout->insertWidget(patternButtonLayout->count() - 1, patternButton);
        patternButtons.append(patternButton);
    }
}

void SimpleTeach::selectPattern(const QUuid& patternId) {
    selectedPatternId = patternId;

    if (patternEditWidget) {
        patternEditWidget->setVisible(!selectedPatternId.isNull());
    }

    // **패턴 버튼들 스타일 업데이트**
    updatePatternList();

    if (teachingWidget && teachingWidget->getCameraView()) {
        teachingWidget->getCameraView()->setSelectedPatternId(patternId);
    }

    update();
}

void SimpleTeach::savePatternToTeachingWidget() {
    if (!teachingWidget || teachingPatterns.isEmpty()) return;
    
    for (const PatternInfo& pattern : teachingPatterns) {
        PatternInfo patternCopy = pattern;
        patternCopy.cameraUuid = selectedCameraUuid;
    }
}

void SimpleTeach::loadExistingPatternsFromTeachingWidget() {
    if (!teachingWidget) return;
    
    teachingPatterns.clear();
    
    // **TeachingWidget의 CameraView에서 실제 패턴들 가져오기**
    CameraView* cameraView = teachingWidget->getCameraView();
    if (!cameraView) return;
    
    const QList<PatternInfo>& allPatterns = cameraView->getPatterns();
    
    // **현재 선택된 카메라 UUID 가져오기**
    QString currentCameraUuid;
    if (selectedCameraIndex >= 0 && selectedCameraIndex < cameraInfos.size()) {
        currentCameraUuid = cameraInfos[selectedCameraIndex].uniqueId;
    }
    
    // **현재 카메라에 속한 패턴들만 필터링하여 복사**
    for (const PatternInfo& pattern : allPatterns) {
        if (pattern.cameraUuid == currentCameraUuid) {
            teachingPatterns.append(pattern);  // 전체 패턴 정보 복사
        }
    }
    
    for (const PatternInfo& pattern : teachingPatterns) {
    }
    
    // **패턴 목록 업데이트**
    updatePatternList();
}

// **나머지 기존 함수들**
void SimpleTeach::onCameraSelected(int cameraIndex) {
    selectedCameraIndex = cameraIndex;
    if (cameraIndex >= 0 && cameraIndex < cameraInfos.size()) {
        selectedCameraUuid = cameraInfos[cameraIndex].uniqueId;
        
        if (nextButton) {
            nextButton->setEnabled(true);
        }
        
        // **모든 라벨 업데이트**
        for (int i = 0; i < 4; i++) {
            int row = i / 2;
            int col = i % 2;
            QLayoutItem* item = cameraGridLayout->itemAtPosition(row, col);
            if (item && item->widget()) {
                QLabel* label = qobject_cast<QLabel*>(item->widget());
                if (label) {
                    if (i == cameraIndex) {
                        label->setText(QString("CAM %1\n\nSELECTED").arg(i + 1));
                        label->setStyleSheet(
                            "font-size: 14px; font-weight: bold; "
                            "border: 3px solid #FF9800; border-radius: 8px; "
                            "background-color: #FFF3E0; color: #E65100;"
                        );
                    } else if (i < cameraInfos.size() && cameraInfos[i].isConnected) {
                        label->setText(QString("CAM %1\n\nACTIVE").arg(i + 1));
                        label->setStyleSheet(
                            "font-size: 14px; font-weight: bold; "
                            "border: 3px solid #4CAF50; border-radius: 8px; "
                            "background-color: #E8F5E8; color: #2E7D32;"
                        );
                    }
                }
            }
        }
    }
}

void SimpleTeach::onNextClicked() {
    if (currentStep == CAMERA_SELECTION && selectedCameraIndex >= 0) {
        
        // **화면 전환**
        currentStep = TEACHING_VIEW;
        contentStack->setCurrentWidget(teachingViewWidget);
        updateStepDisplay();
        
        // **패턴 로드**
        loadExistingPatternsFromTeachingWidget();
        updatePatternList();
        
        // **시그널 연결 - 딱 한 번만**
        if (teachingWidget && teachingWidget->getCameraView()) {
            connect(this, &SimpleTeach::patternMoved, teachingWidget->getCameraView(), 
                    &CameraView::updatePatternRect, Qt::UniqueConnection);
            connect(this, &SimpleTeach::patternResized, teachingWidget->getCameraView(), 
                    &CameraView::updatePatternRect, Qt::UniqueConnection);
        }
        
        // **라이브 타이머 시작**
        if (!liveUpdateTimer->isActive()) {
            liveUpdateTimer->start();
        }
        
        // **첫 번째 프레임 즉시 표시**
        updateLiveImage();
        
    }
}

void SimpleTeach::onBackClicked() {
    // **라이브 업데이트 타이머 중지**
    if (liveUpdateTimer && liveUpdateTimer->isActive()) {
        liveUpdateTimer->stop();
    }
    
    if (currentStep == TEACHING_VIEW) {
        // 티칭 뷰에서 카메라 선택으로 돌아가기
        currentStep = CAMERA_SELECTION;
        selectedCameraIndex = -1;
        
        // 패턴 데이터 초기화
        teachingPatterns.clear();
        selectedPatternId = QUuid();
        
        // **원본 이미지도 초기화**
        originalPixmap = QPixmap();
        
        contentStack->setCurrentWidget(cameraListWidget);
        updateStepDisplay();
        
    }
}

void SimpleTeach::updateStepDisplay() {
    switch (currentStep) {
        case CAMERA_SELECTION:
            contentStack->setCurrentWidget(cameraListWidget);
            backButton->setEnabled(false);
            nextButton->setEnabled(selectedCameraIndex >= 0);
            setWindowTitle("Simple Teaching - Camera Selection");
            break;
            
        case TEACHING_VIEW:
            contentStack->setCurrentWidget(teachingViewWidget);
            backButton->setEnabled(true);
            nextButton->setEnabled(false);
            setWindowTitle("Simple Teaching - Teaching View");
            break;
    }
}

void SimpleTeach::updateCameraSlots() {
    if (teachingWidget) {
        cameraInfos = teachingWidget->getCameraInfos();
    }
}

bool SimpleTeach::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            QLabel* label = qobject_cast<QLabel*>(watched);
            if (label) {
                int cameraIndex = label->property("cameraIndex").toInt();
                onCameraSelected(cameraIndex);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SimpleTeach::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        close();
    }
    QWidget::keyPressEvent(event);
}

int SimpleTeach::getSelectedCameraIndex() const {
    return selectedCameraIndex;
}

CameraInfo SimpleTeach::getSelectedCameraInfo() const {
    if (selectedCameraIndex >= 0 && selectedCameraIndex < cameraInfos.size()) {
        return cameraInfos[selectedCameraIndex];
    }
    return CameraInfo();
}

QPoint SimpleTeach::mapToOriginal(const QPoint& displayPoint) {
    if (originalPixmap.isNull() || zoomFactor <= 0) return displayPoint;
    
    QRect imageArea(5, 5, 500, 520);
    QPoint centerPos = imageArea.center() + panOffset;
    QPoint imageTopLeft(centerPos.x() - scaledPixmap.width() / 2,
                       centerPos.y() - scaledPixmap.height() / 2);
    
    QPoint relativePos = displayPoint - imageTopLeft;
    return QPoint(relativePos.x() / zoomFactor, relativePos.y() / zoomFactor);
}

QPoint SimpleTeach::mapToDisplay(const QPoint& originalPoint) {
    if (originalPixmap.isNull() || zoomFactor <= 0) return originalPoint;
    
    QRect imageArea(5, 5, 500, 520);
    QPoint centerPos = imageArea.center() + panOffset;
    QPoint imageTopLeft(centerPos.x() - scaledPixmap.width() / 2,
                       centerPos.y() - scaledPixmap.height() / 2);
    
    QPoint scaledPos(originalPoint.x() * zoomFactor, originalPoint.y() * zoomFactor);
    return imageTopLeft + scaledPos;
}