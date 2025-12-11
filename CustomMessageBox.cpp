#include "CustomMessageBox.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QScreen>
#include <QIcon>
#include <QStyle>
#include <QPixmap>
#include <QThread>

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
    QHBoxLayout* buttonLayout = new QHBoxLayout();
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
        statusLabel->setText("완료!");
    }
    QApplication::processEvents();
    QThread::msleep(200);
    close();
    deleteLater();
}
