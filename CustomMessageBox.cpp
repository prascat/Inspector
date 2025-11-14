#include "CustomMessageBox.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QScreen>
#include <QIcon>
#include <QStyle>
#include <QPixmap>

CustomMessageBox::CustomMessageBox(QWidget* parent)
    : QDialog(parent), currentIcon(NoIcon), result(QMessageBox::NoButton), hasInputField(false), savedParent(parent) {
    
    // 다이얼로그 속성 설정
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setStyleSheet(
        "QDialog {"
        "    background-color: white;"
        "    border-radius: 8px;"
        "    border: 1px solid #e0e0e0;"
        "}"
        "QLabel {"
        "    color: black;"
        "    background-color: transparent;"
        "}"
        "QLineEdit {"
        "    background-color: white;"
        "    color: black;"
        "    border: 1px solid #cccccc;"
        "    padding: 8px;"
        "    border-radius: 4px;"
        "    font-size: 12px;"
        "}"
        "QLineEdit:focus {"
        "    border: 2px solid #3498db;"
        "}"
        "QPushButton {"
        "    background-color: #f0f0f0;"
        "    color: black;"
        "    border: 1px solid #cccccc;"
        "    padding: 8px 24px;"
        "    border-radius: 4px;"
        "    font-weight: bold;"
        "    min-width: 60px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #e0e0e0;"
        "    border-color: #999999;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #d0d0d0;"
        "}"
    );
    
    setupUI();
}

CustomMessageBox::CustomMessageBox(QWidget* parent, IconType iconType, const QString& title,
                                   const QString& message, QMessageBox::StandardButtons buttons)
    : QDialog(parent), currentIcon(NoIcon), result(QMessageBox::NoButton), hasInputField(false), savedParent(parent) {
    
    // 다이얼로그 속성 설정
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setStyleSheet(
        "QDialog {"
        "    background-color: white;"
        "    border-radius: 8px;"
        "    border: 1px solid #e0e0e0;"
        "}"
        "QLabel {"
        "    color: black;"
        "    background-color: transparent;"
        "}"
        "QLineEdit {"
        "    background-color: white;"
        "    color: black;"
        "    border: 1px solid #cccccc;"
        "    padding: 8px;"
        "    border-radius: 4px;"
        "    font-size: 12px;"
        "}"
        "QLineEdit:focus {"
        "    border: 2px solid #3498db;"
        "}"
        "QPushButton {"
        "    background-color: #f0f0f0;"
        "    color: black;"
        "    border: 1px solid #cccccc;"
        "    padding: 8px 24px;"
        "    border-radius: 4px;"
        "    font-weight: bold;"
        "    min-width: 60px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #e0e0e0;"
        "    border-color: #999999;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #d0d0d0;"
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
    titleLabel->setStyleSheet("QLabel { font-size: 14px; font-weight: bold; color: #000000; }");
    textLayout->addWidget(titleLabel);
    
    messageLabel = new QLabel();
    messageLabel->setStyleSheet("QLabel { font-size: 12px; color: #333333; }");
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

int CustomMessageBox::exec() {
    adjustSize();
    
    // 부모 중심에 배치
    if (savedParent) {
        QWidget* topWindow = savedParent->window();
        QRect parentRect = topWindow->frameGeometry();
        
        int x = parentRect.x() + (parentRect.width() - width()) / 2;
        int y = parentRect.y() + (parentRect.height() - height()) / 2;
        
        // 타이틀바 높이만큼 위로 보정 (frameGeometry에 타이틀바 포함되어 있으므로)
        int titleBarHeight = topWindow->frameGeometry().height() - topWindow->geometry().height();
        y -= titleBarHeight / 2;
        
        move(x, y);
    }
    
    return QDialog::exec();
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
