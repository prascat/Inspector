#include "ClientDialog.h"
#include "ConfigManager.h"
#include "LanguageManager.h"
#include "CustomMessageBox.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QIntValidator>
#include <QDebug>

ClientDialog::ClientDialog(QWidget* parent)
    : QDialog(parent)
    , serverIp("127.0.0.1")
    , serverPort(5000)
    , autoConnect(false)
    , isConnected(false)
{
    setWindowTitle("서버 연결 설정");
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setMinimumWidth(500);
    setModal(true);

    testSocket = new QTcpSocket(this);
    statusTimer = new QTimer(this);
    
    connect(testSocket, &QTcpSocket::connected, this, &ClientDialog::onSocketConnected);
    connect(testSocket, &QTcpSocket::disconnected, this, &ClientDialog::onSocketDisconnected);
    connect(testSocket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &ClientDialog::onSocketError);
    connect(statusTimer, &QTimer::timeout, this, &ClientDialog::updateConnectionStatus);
    
    statusTimer->start(1000); // 1초마다 상태 업데이트

    setupUI();
    loadSettings();
    updateLanguage();
}

ClientDialog::~ClientDialog()
{
    if (testSocket->state() == QAbstractSocket::ConnectedState) {
        testSocket->disconnectFromHost();
    }
}

int ClientDialog::exec() {
    if (parentWidget()) {
        QWidget* topWindow = parentWidget()->window();
        QRect parentRect = topWindow->frameGeometry();
        
        int x = parentRect.x() + (parentRect.width() - width()) / 2;
        int y = parentRect.y() + (parentRect.height() - height()) / 2;
        
        int titleBarHeight = topWindow->frameGeometry().height() - topWindow->geometry().height();
        y -= titleBarHeight / 2;
        
        move(x, y);
    }
    
    return QDialog::exec();
}

void ClientDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // 서버 설정 그룹
    QGroupBox* serverGroup = new QGroupBox("서버 설정", this);
    QFormLayout* formLayout = new QFormLayout();
    formLayout->setSpacing(10);

    // IP 주소 입력
    ipEdit = new QLineEdit(this);
    ipEdit->setPlaceholderText("예: 192.168.0.100");
    formLayout->addRow("서버 IP:", ipEdit);

    // 포트 입력
    portEdit = new QLineEdit(this);
    portEdit->setPlaceholderText("예: 5000");
    portEdit->setValidator(new QIntValidator(1, 65535, this));
    formLayout->addRow("포트:", portEdit);

    serverGroup->setLayout(formLayout);
    mainLayout->addWidget(serverGroup);

    // 연결 상태 그룹
    QGroupBox* statusGroup = new QGroupBox("연결 상태", this);
    QVBoxLayout* statusLayout = new QVBoxLayout();
    
    connectionStatusLabel = new QLabel("미연결", this);
    connectionStatusLabel->setStyleSheet("QLabel { padding: 10px; background-color: #f0f0f0; border-radius: 5px; }");
    statusLayout->addWidget(connectionStatusLabel);
    
    statusLabel = new QLabel("", this);
    statusLabel->setWordWrap(true);
    statusLabel->setStyleSheet("QLabel { color: #666; font-size: 11px; }");
    statusLayout->addWidget(statusLabel);
    
    statusGroup->setLayout(statusLayout);
    mainLayout->addWidget(statusGroup);

    mainLayout->addStretch();

    // 버튼 레이아웃
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    testButton = new QPushButton("연결 테스트", this);
    testButton->setMinimumHeight(35);
    connect(testButton, &QPushButton::clicked, this, &ClientDialog::onTestConnection);

    saveButton = new QPushButton("저장", this);
    saveButton->setMinimumHeight(35);
    connect(saveButton, &QPushButton::clicked, this, &ClientDialog::onSaveSettings);

    cancelButton = new QPushButton("취소", this);
    cancelButton->setMinimumHeight(35);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    buttonLayout->addWidget(testButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(saveButton);
    buttonLayout->addWidget(cancelButton);

    mainLayout->addLayout(buttonLayout);
}

void ClientDialog::loadSettings()
{
    ConfigManager* config = ConfigManager::instance();
    
    serverIp = config->getServerIp();
    serverPort = config->getServerPort();
    autoConnect = config->getAutoConnect();
    
    ipEdit->setText(serverIp);
    portEdit->setText(QString::number(serverPort));
    
    qDebug() << "서버 설정 로드됨 - IP:" << serverIp << "Port:" << serverPort;
}

void ClientDialog::saveSettings()
{
    serverIp = ipEdit->text().trimmed();
    serverPort = portEdit->text().toInt();
    
    ConfigManager* config = ConfigManager::instance();
    config->setServerIp(serverIp);
    config->setServerPort(serverPort);
    config->setAutoConnect(autoConnect);
    config->saveConfig();
    
    qDebug() << "서버 설정 저장됨 - IP:" << serverIp << "Port:" << serverPort;
}

void ClientDialog::setServerIp(const QString& ip)
{
    serverIp = ip;
    ipEdit->setText(ip);
}

void ClientDialog::setServerPort(int port)
{
    serverPort = port;
    portEdit->setText(QString::number(port));
}

void ClientDialog::setAutoConnect(bool enable)
{
    autoConnect = enable;
}

void ClientDialog::onTestConnection()
{
    if (testSocket->state() == QAbstractSocket::ConnectedState) {
        testSocket->disconnectFromHost();
        testButton->setText("연결 테스트");
        return;
    }

    QString ip = ipEdit->text().trimmed();
    int port = portEdit->text().toInt();

    if (ip.isEmpty()) {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Warning);
        msgBox.setTitle("입력 오류");
        msgBox.setMessage("서버 IP를 입력해주세요.");
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    if (port <= 0 || port > 65535) {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Warning);
        msgBox.setTitle("입력 오류");
        msgBox.setMessage("올바른 포트 번호를 입력해주세요. (1-65535)");
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    statusLabel->setText("연결 시도 중...");
    connectionStatusLabel->setText("연결 중...");
    connectionStatusLabel->setStyleSheet("QLabel { padding: 10px; background-color: #fff3cd; border-radius: 5px; color: #856404; }");
    testButton->setEnabled(false);

    testSocket->connectToHost(ip, port);
    
    // 5초 타임아웃
    QTimer::singleShot(5000, this, [this]() {
        if (testSocket->state() == QAbstractSocket::ConnectingState) {
            testSocket->abort();
            statusLabel->setText("연결 시간 초과");
            connectionStatusLabel->setText("연결 실패 (시간 초과)");
            connectionStatusLabel->setStyleSheet("QLabel { padding: 10px; background-color: #f8d7da; border-radius: 5px; color: #721c24; }");
            testButton->setEnabled(true);
            testButton->setText("연결 테스트");
        }
    });
}

void ClientDialog::onSocketConnected()
{
    isConnected = true;
    statusLabel->setText(QString("서버에 연결되었습니다: %1:%2")
                        .arg(testSocket->peerAddress().toString())
                        .arg(testSocket->peerPort()));
    connectionStatusLabel->setText("✓ 연결됨");
    connectionStatusLabel->setStyleSheet("QLabel { padding: 10px; background-color: #d4edda; border-radius: 5px; color: #155724; }");
    testButton->setEnabled(true);
    testButton->setText("연결 해제");
    
    CustomMessageBox msgBox(this);
    msgBox.setIcon(CustomMessageBox::Information);
    msgBox.setTitle("연결 성공");
    msgBox.setMessage("서버에 성공적으로 연결되었습니다.");
    msgBox.setButtons(QMessageBox::Ok);
    msgBox.exec();
}

void ClientDialog::onSocketDisconnected()
{
    isConnected = false;
    statusLabel->setText("서버와의 연결이 해제되었습니다.");
    connectionStatusLabel->setText("미연결");
    connectionStatusLabel->setStyleSheet("QLabel { padding: 10px; background-color: #f0f0f0; border-radius: 5px; }");
    testButton->setEnabled(true);
    testButton->setText("연결 테스트");
}

void ClientDialog::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    
    QString errorMsg = testSocket->errorString();
    statusLabel->setText(QString("연결 오류: %1").arg(errorMsg));
    connectionStatusLabel->setText("연결 실패");
    connectionStatusLabel->setStyleSheet("QLabel { padding: 10px; background-color: #f8d7da; border-radius: 5px; color: #721c24; }");
    testButton->setEnabled(true);
    testButton->setText("연결 테스트");
    
    CustomMessageBox msgBox(this);
    msgBox.setIcon(CustomMessageBox::Critical);
    msgBox.setTitle("연결 오류");
    msgBox.setMessage(QString("서버 연결에 실패했습니다:\n%1").arg(errorMsg));
    msgBox.setButtons(QMessageBox::Ok);
    msgBox.exec();
}

void ClientDialog::updateConnectionStatus()
{
    // 주기적으로 연결 상태 확인
    if (testSocket->state() == QAbstractSocket::ConnectedState && !isConnected) {
        isConnected = true;
    } else if (testSocket->state() != QAbstractSocket::ConnectedState && isConnected) {
        isConnected = false;
        onSocketDisconnected();
    }
}

void ClientDialog::onSaveSettings()
{
    saveSettings();
    emit settingsChanged();
    
    CustomMessageBox msgBox(this);
    msgBox.setIcon(CustomMessageBox::Information);
    msgBox.setTitle("저장 완료");
    msgBox.setMessage("서버 설정이 저장되었습니다.");
    msgBox.setButtons(QMessageBox::Ok);
    msgBox.exec();
    
    accept();
}

void ClientDialog::updateLanguage()
{
    // 언어 변경 시 UI 텍스트 업데이트
    setWindowTitle("서버 연결 설정");
}
