#include "CustomMessageBox.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QScreen>
#include <QIcon>
#include <QStyle>
#include <QPixmap>
#include <QThread>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QJsonValue>

CustomMessageBox::CustomMessageBox(QWidget* parent)
    : QDialog(parent), currentIcon(NoIcon), result(QMessageBox::NoButton), hasInputField(false), 
      savedParent(parent), progressBar(nullptr), statusLabel(nullptr), isLoadingDialog(false) {
    
    // 다이얼로그 속성 설정
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setStyleSheet(
        "QDialog {"
        "    background-color: rgba(68, 68, 68, 200);"
        "    border: 1px solid white;"
        "}"
        "QLabel {"
        "    color: white;"
        "    background-color: transparent;"
        "}"
        "QLineEdit {"
        "    background-color: rgb(80, 80, 80);"
        "    color: white;"
        "    border: 1px solid rgb(100, 100, 100);"
        "    padding: 8px;"
        "    font-size: 12px;"
        "}"
        "QLineEdit:focus {"
        "    border: 2px solid #3498db;"
        "}"
        "QPushButton {"
        "    background-color: rgb(80, 80, 80);"
        "    color: white;"
        "    border: 1px solid rgb(100, 100, 100);"
        "    padding: 8px 24px;"
        "    font-weight: bold;"
        "    min-width: 60px;"
        "}"
        "QPushButton:hover {"
        "    background-color: rgb(100, 100, 100);"
        "}"
        "QPushButton:pressed {"
        "    background-color: rgb(60, 60, 60);"
        "}"
        "QProgressBar {"
        "    background-color: rgb(70, 70, 70);"
        "    border: none;"
        "    text-align: center;"
        "}"
        "QProgressBar::chunk {"
        "    background-color: rgb(42, 130, 218);"
        "}"
    );
    
    setupUI();
}

CustomMessageBox::CustomMessageBox(QWidget* parent, IconType iconType, const QString& title,
                                   const QString& message, QMessageBox::StandardButtons buttons)
    : QDialog(parent), currentIcon(NoIcon), result(QMessageBox::NoButton), hasInputField(false), 
      savedParent(parent), progressBar(nullptr), statusLabel(nullptr), isLoadingDialog(false) {
    
    // 다이얼로그 속성 설정
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setStyleSheet(
        "QDialog {"
        "    background-color: rgba(68, 68, 68, 200);"
        "    border: 1px solid white;"
        "}"
        "QLabel {"
        "    color: white;"
        "    background-color: transparent;"
        "}"
        "QLineEdit {"
        "    background-color: rgb(80, 80, 80);"
        "    color: white;"
        "    border: 1px solid rgb(100, 100, 100);"
        "    padding: 8px;"
        "    font-size: 12px;"
        "}"
        "QLineEdit:focus {"
        "    border: 2px solid #3498db;"
        "}"
        "QPushButton {"
        "    background-color: rgb(80, 80, 80);"
        "    color: white;"
        "    border: 1px solid rgb(100, 100, 100);"
        "    padding: 8px 24px;"
        "    font-weight: bold;"
        "    min-width: 60px;"
        "}"
        "QPushButton:hover {"
        "    background-color: rgb(100, 100, 100);"
        "}"
        "QPushButton:pressed {"
        "    background-color: rgb(60, 60, 60);"
        "}"
    );
    
    setupUI();
    
    // 편의 생성자에서 모든 속성 설정
    setTitle(title);
    setMessage(message);
    setIcon(iconType);
    setButtons(buttons);
}

void CustomMessageBox::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout();
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);
    
    // 아이콘 + 제목/메시지 레이아웃
    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(15);
    
    // 아이콘
    iconLabel = new QLabel();
    iconLabel->setFixedSize(64, 64);
    contentLayout->addWidget(iconLabel);
    
    // 제목과 메시지
    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setSpacing(8);
    
    titleLabel = new QLabel();
    titleLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: white; }");
    textLayout->addWidget(titleLabel);
    
    messageLabel = new QLabel();
    messageLabel->setStyleSheet("QLabel { font-size: 12px; color: white; }");
    messageLabel->setWordWrap(true);
    messageLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    textLayout->addWidget(messageLabel);
    textLayout->addStretch();
    
    contentLayout->addLayout(textLayout, 1);
    mainLayout->addLayout(contentLayout);
    
    // 입력 필드 (기본적으로는 숨김)
    inputEdit = new QLineEdit();
    inputEdit->setVisible(false);
    inputEdit->setPlaceholderText("텍스트 입력");
    inputEdit->setMinimumHeight(32);
    mainLayout->addWidget(inputEdit);
    
    // 버튼 레이아웃
    buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);
    buttonLayout->addStretch();
    
    okButton = new QPushButton("OK");
    yesButton = new QPushButton("Yes");
    noButton = new QPushButton("No");
    cancelButton = new QPushButton("Cancel");
    
    connect(okButton, &QPushButton::clicked, [this]() {
        result = QMessageBox::Ok;
        accept();
    });
    
    connect(yesButton, &QPushButton::clicked, [this]() {
        result = QMessageBox::Yes;
        accept();
    });
    
    connect(noButton, &QPushButton::clicked, [this]() {
        result = QMessageBox::No;
        reject();
    });
    
    connect(cancelButton, &QPushButton::clicked, [this]() {
        result = QMessageBox::Cancel;
        reject();
    });
    
    // 초기에는 모든 버튼 숨김
    okButton->hide();
    yesButton->hide();
    noButton->hide();
    cancelButton->hide();
    
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(yesButton);
    buttonLayout->addWidget(noButton);
    buttonLayout->addWidget(cancelButton);
    
    mainLayout->addLayout(buttonLayout);
    setLayout(mainLayout);
    
    setMinimumWidth(400);
    setMaximumWidth(600);
}

void CustomMessageBox::setTitle(const QString& title) {
    titleText = title;
    titleLabel->setText(title);
}

void CustomMessageBox::setMessage(const QString& message) {
    messageText = message;
    messageLabel->setText(message);
    adjustSize();
}

void CustomMessageBox::setIcon(IconType iconType) {
    currentIcon = iconType;
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    
    if (iconType == Information) {
        QPixmap infoPixmap = style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(64, 64);
        iconLabel->setPixmap(infoPixmap);
    } else if (iconType == Warning) {
        QPixmap warnPixmap = style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(64, 64);
        iconLabel->setPixmap(warnPixmap);
    } else if (iconType == Critical) {
        QPixmap critPixmap = style()->standardIcon(QStyle::SP_MessageBoxCritical).pixmap(64, 64);
        iconLabel->setPixmap(critPixmap);
    } else if (iconType == Question) {
        QPixmap quesPixmap = style()->standardIcon(QStyle::SP_MessageBoxQuestion).pixmap(64, 64);
        iconLabel->setPixmap(quesPixmap);
    }
}

void CustomMessageBox::setButtons(QMessageBox::StandardButtons buttons) {
    buttonFlags = buttons;
    
    // 모든 버튼 숨김
    okButton->hide();
    yesButton->hide();
    noButton->hide();
    cancelButton->hide();
    
    // 필요한 버튼 표시
    if (buttons & QMessageBox::Ok) okButton->show();
    if (buttons & QMessageBox::Yes) yesButton->show();
    if (buttons & QMessageBox::No) noButton->show();
    if (buttons & QMessageBox::Cancel) cancelButton->show();
}

void CustomMessageBox::setButtonText(QMessageBox::StandardButton button, const QString& text) {
    switch (button) {
        case QMessageBox::Ok:
            okButton->setText(text);
            break;
        case QMessageBox::Yes:
            yesButton->setText(text);
            break;
        case QMessageBox::No:
            noButton->setText(text);
            break;
        case QMessageBox::Cancel:
            cancelButton->setText(text);
            break;
        default:
            break;
    }
}

int CustomMessageBox::exec() {
    adjustSize();
    
    // 부모 중심에 배치
    if (savedParent) {
        QPoint parentTopLeft = savedParent->mapToGlobal(QPoint(0, 0));
        int x = parentTopLeft.x() + (savedParent->width() - width()) / 2;
        int y = parentTopLeft.y() + (savedParent->height() - height()) / 2;
        move(x, y);
    }
    
    int dialogResult = QDialog::exec();
    return result;  // QDialog::exec() 대신 저장한 result 반환
}

void CustomMessageBox::setInputField(bool enabled, const QString& defaultText) {
    if (!inputEdit) return;
    
    hasInputField = enabled;
    inputEdit->setVisible(enabled);
    inputEdit->setText(defaultText);
    inputEdit->setPlaceholderText("텍스트 입력");
    
    if (enabled) {
        inputEdit->setFocus();
        inputEdit->selectAll();
    }
}

QString CustomMessageBox::getInputText() const {
    if (!inputEdit) return "";
    return inputEdit->text();
}

QPushButton* CustomMessageBox::addCustomButton(const QString& text) {
    qDebug() << "[CustomMessageBox] addCustomButton 호출:" << text;
    QPushButton* button = new QPushButton(text, this);
    button->setMinimumHeight(35);
    button->setMinimumWidth(100);
    button->setStyleSheet(
        "QPushButton {"
        "  background-color: #0078D7;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 5px;"
        "  padding: 8px 16px;"
        "  font-size: 12px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #1084D8;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #006CC1;"
        "}"
    );
    
    if (buttonLayout) {
        // stretch 뒤에 버튼 추가
        buttonLayout->addWidget(button);
        button->show(); // 명시적으로 표시
        qDebug() << "[CustomMessageBox] 버튼 추가 완료:" << text << "visible:" << button->isVisible();
    } else {
        qDebug() << "[CustomMessageBox] ERROR: buttonLayout이 null입니다!";
    }
    return button;
}

QHBoxLayout* CustomMessageBox::getButtonLayout() {
    return buttonLayout;
}

CustomMessageBox::ImageSourceChoice CustomMessageBox::showImageSourceDialog(QWidget* parent) {
    CustomMessageBox msgBox(parent);
    msgBox.setTitle("새 레시피 생성");
    msgBox.setMessage("영상을 어디서 가져오시겠습니까?");
    msgBox.setButtons(QMessageBox::NoButton);
    
    QPushButton *imageButton = msgBox.addCustomButton("이미지 찾기");
    QPushButton *recipeButton = msgBox.addCustomButton("레시피로 읽기");
    QPushButton *currentButton = msgBox.addCustomButton("현재 이미지");
    QPushButton *cancelButton = msgBox.addCustomButton("취소");
    
    // 버튼 크기에 맞게 다이얼로그 크기 자동 조정
    msgBox.adjustSize();
    
    ImageSourceChoice choice = ChoiceCancelled;
    
    QObject::connect(imageButton, &QPushButton::clicked, [&]() {
        choice = ChoiceImageFile;
        msgBox.accept();
    });
    QObject::connect(recipeButton, &QPushButton::clicked, [&]() {
        choice = ChoiceRecipe;
        msgBox.accept();
    });
    QObject::connect(currentButton, &QPushButton::clicked, [&]() {
        choice = ChoiceCurrentImage;
        msgBox.accept();
    });
    QObject::connect(cancelButton, &QPushButton::clicked, [&]() {
        choice = ChoiceCancelled;
        msgBox.reject();
    });
    
    msgBox.exec();
    return choice;
}

CustomMessageBox* CustomMessageBox::showLoading(QWidget* parent, const QString& title) {
    CustomMessageBox* dialog = new CustomMessageBox(parent);
    dialog->isLoadingDialog = true;
    dialog->titleText = title;
    
    // 기존 UI 숨기기
    if (dialog->iconLabel) dialog->iconLabel->hide();
    if (dialog->messageLabel) dialog->messageLabel->hide();
    if (dialog->inputEdit) dialog->inputEdit->hide();
    if (dialog->okButton) dialog->okButton->hide();
    if (dialog->yesButton) dialog->yesButton->hide();
    if (dialog->noButton) dialog->noButton->hide();
    if (dialog->cancelButton) dialog->cancelButton->hide();
    
    dialog->setupLoadingUI();
    
    // 화면 중앙에 배치
    if (parent) {
        QRect parentRect = parent->geometry();
        int x = parentRect.x() + (parentRect.width() - dialog->width()) / 2;
        int y = parentRect.y() + (parentRect.height() - dialog->height()) / 2;
        dialog->move(x, y);
    } else {
        QScreen *screen = QGuiApplication::primaryScreen();
        QRect screenGeometry = screen->geometry();
        int x = (screenGeometry.width() - dialog->width()) / 2;
        int y = (screenGeometry.height() - dialog->height()) / 2;
        dialog->move(x, y);
    }
    
    dialog->show();
    QApplication::processEvents();
    
    return dialog;
}

void CustomMessageBox::setupLoadingUI() {
    titleLabel->setText(titleText);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: white;");
    
    // 프로그레스바 생성
    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(false);
    progressBar->setFixedHeight(8);
    
    // 상태 레이블 생성
    statusLabel = new QLabel("초기화 중...", this);
    statusLabel->setStyleSheet("font-size: 12px; color: rgb(200, 200, 200);");
    statusLabel->setAlignment(Qt::AlignCenter);
    
    // 레이아웃에 추가
    QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(layout());
    if (mainLayout) {
        mainLayout->insertWidget(2, progressBar);
        mainLayout->insertWidget(3, statusLabel);
    }
    
    setFixedSize(400, 150);
}

void CustomMessageBox::updateProgress(int value, const QString& status) {
    if (progressBar) {
        progressBar->setValue(value);
    }
    if (statusLabel && !status.isEmpty()) {
        statusLabel->setText(status);
    }
    QApplication::processEvents();
}

void CustomMessageBox::finishLoading() {
    if (progressBar) {
        progressBar->setValue(100);
    }
    if (statusLabel) {
        statusLabel->setText("Completed!");
    }
    QApplication::processEvents();
    QThread::msleep(200);
    close();
    deleteLater();
}

QString CustomMessageBox::getTableDialogStyleSheet() {
    return 
        "QDialog { background-color: #1E1E1E; color: #FFFFFF; border: 2px solid #555555; } "
        "QLabel { color: #FFFFFF; } "
        "QTableWidget { "
        "  background-color: #2D2D2D; "
        "  color: #FFFFFF; "
        "  border: 2px solid #555555; "
        "  border-radius: 5px; "
        "  gridline-color: #404040; "
        "} "
        "QTableWidget::item { "
        "  padding: 8px; "
        "  border-bottom: 1px solid #404040; "
        "} "
        "QTableWidget::item:selected { "
        "  background-color: #0078D7; "
        "  color: #FFFFFF; "
        "} "
        "QTableWidget::item:hover { "
        "  background-color: #3D3D3D; "
        "} "
        "QHeaderView::section { "
        "  background-color: #404040; "
        "  color: #FFFFFF; "
        "  padding: 8px; "
        "  border: 1px solid #555555; "
        "  font-weight: bold; "
        "} "
        "QPushButton { "
        "  background-color: #0078D7; "
        "  color: white; "
        "  border: none; "
        "  padding: 10px 24px; "
        "  font-weight: bold; "
        "  border-radius: 4px; "
        "  min-width: 80px; "
        "  min-height: 40px; "
        "} "
        "QPushButton:hover { "
        "  background-color: #1E88E5; "
        "} "
        "QPushButton:pressed { "
        "  background-color: #005A9E; "
        "} "
        "QPushButton:disabled { "
        "  background-color: #555555; "
        "  color: #999999; "
        "} "
        "QPushButton#cancelButton, QPushButton#deleteButton { "
        "  background-color: #D32F2F; "
        "} "
        "QPushButton#cancelButton:hover, QPushButton#deleteButton:hover { "
        "  background-color: #E53935; "
        "} "
        "QPushButton#cancelButton:pressed, QPushButton#deleteButton:pressed { "
        "  background-color: #B71C1C; "
        "} "
        "QPushButton#closeButton { "
        "  background-color: #616161; "
        "} "
        "QPushButton#closeButton:hover { "
        "  background-color: #757575; "
        "} "
        "QPushButton#closeButton:pressed { "
        "  background-color: #424242; "
        "}";
}

int CustomMessageBox::showTableSelectionDialog(
    QWidget* parent,
    const QString& title,
    const QString& message,
    const QStringList& headers,
    const QList<QStringList>& rows,
    QJsonArray* jsonData
) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setMinimumSize(1000, 400);
    dialog.setStyleSheet(getTableDialogStyleSheet());
    
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(15);
    layout->setContentsMargins(20, 20, 20, 20);
    
    // 타이틀
    QLabel* titleLabel = new QLabel(title);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #FFFFFF;");
    layout->addWidget(titleLabel);
    
    // 메시지
    if (!message.isEmpty()) {
        QLabel* messageLabel = new QLabel(message);
        messageLabel->setStyleSheet("color: #CCCCCC; font-size: 14px;");
        layout->addWidget(messageLabel);
    }
    
    // 테이블 생성
    QTableWidget* table = new QTableWidget();
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    
    // 데이터 추가
    table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); i++) {
        const QStringList& rowData = rows[i];
        for (int j = 0; j < rowData.size() && j < headers.size(); j++) {
            QTableWidgetItem* item = new QTableWidgetItem(rowData[j]);
            table->setItem(i, j, item);
            
            // JSON 데이터가 있으면 첫 번째 컬럼에 저장
            if (j == 0 && jsonData && i < jsonData->size()) {
                QJsonValue jsonValue = (*jsonData)[i];
                item->setData(Qt::UserRole, QVariant::fromValue(jsonValue));
            }
        }
    }
    
    // 컬럼 너비를 내용에 맞게 자동 조정
    table->resizeColumnsToContents();
    
    // 모든 컬럼을 대화형으로 설정하여 사용자가 조정할 수 있도록 함
    for (int i = 0; i < headers.size() - 1; i++) {
        table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Interactive);
        // 최소 너비 설정 (내용 + 여유 공간)
        int width = table->horizontalHeader()->sectionSize(i);
        table->horizontalHeader()->resizeSection(i, width + 20);
    }
    
    // 마지막 컬럼은 남은 공간을 채우도록 설정
    table->horizontalHeader()->setSectionResizeMode(headers.size() - 1, QHeaderView::Stretch);
    
    layout->addWidget(table);
    
    // 버튼
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    QPushButton* selectBtn = new QPushButton("선택");
    QPushButton* cancelBtn = new QPushButton("취소");
    cancelBtn->setObjectName("cancelButton");
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(selectBtn);
    buttonLayout->addWidget(cancelBtn);
    layout->addLayout(buttonLayout);
    
    int selectedRow = -1;
    
    QObject::connect(selectBtn, &QPushButton::clicked, [&]() {
        selectedRow = table->currentRow();
        if (selectedRow >= 0) {
            dialog.accept();
        }
    });
    
    QObject::connect(cancelBtn, &QPushButton::clicked, [&]() {
        selectedRow = -1;
        dialog.reject();
    });
    
    QObject::connect(table, &QTableWidget::cellDoubleClicked, [&](int row, int) {
        selectedRow = row;
        dialog.accept();
    });
    
    // 중앙 배치
    if (parent) {
        QRect parentRect = parent->frameGeometry();
        int x = parentRect.x() + (parentRect.width() - dialog.width()) / 2;
        int y = parentRect.y() + (parentRect.height() - dialog.height()) / 2;
        dialog.move(x, y);
    }
    
    if (dialog.exec() == QDialog::Accepted && selectedRow >= 0) {
        // JSON 데이터가 요청되었고 선택된 행이 있으면 업데이트
        if (jsonData && selectedRow < jsonData->size()) {
            QJsonValue selectedValue = (*jsonData)[selectedRow];
            jsonData->replace(0, selectedValue);  // 첫 번째 요소를 선택된 값으로 교체
        }
        return selectedRow;
    }
    
    return -1;
}