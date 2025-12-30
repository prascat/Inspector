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

ClientDialog* ClientDialog::m_instance = nullptr;

ClientDialog* ClientDialog::instance(QWidget* parent) {
    if (!m_instance) {
        m_instance = new ClientDialog(parent);
    }
    return m_instance;
}

ClientDialog::ClientDialog(QWidget* parent)
    : QDialog(parent)
    , serverIp("127.0.0.1")
    , serverPort(5000)
    , autoConnect(false)
    , reconnectInterval(10)
    , reconnectThread(nullptr)
    , shouldReconnect(false)
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
    connect(testSocket, &QTcpSocket::readyRead, this, &ClientDialog::onDataReceived);
    connect(statusTimer, &QTimer::timeout, this, &ClientDialog::updateConnectionStatus);
    
    statusTimer->start(1000); // 1초마다 상태 업데이트

    setupUI();
    loadSettings();
    updateLanguage();
}

ClientDialog::~ClientDialog()
{
    stopReconnectThread();
    
    if (testSocket->state() == QAbstractSocket::ConnectedState) {
        testSocket->disconnectFromHost();
    }
    
    m_instance = nullptr;
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

    // 재연결 간격 입력
    reconnectIntervalEdit = new QLineEdit(this);
    reconnectIntervalEdit->setPlaceholderText("예: 10");
    reconnectIntervalEdit->setValidator(new QIntValidator(1, 300, this));
    formLayout->addRow("재연결 간격(초):", reconnectIntervalEdit);
    
    // 자동 연결 체크박스
    autoConnectCheckBox = new QCheckBox("프로그램 시작 시 자동 연결", this);
    formLayout->addRow("", autoConnectCheckBox);
    // 자동연결 체크박스 변경 시 즉시 재연결 시작/중단
    connect(autoConnectCheckBox, &QCheckBox::stateChanged, this, [this](int state) {
        if (state == Qt::Checked) {
            qDebug() << "[자동연결] 체크됨 - 재연결 스레드 시작";
            startReconnectThread();
        } else {
            qDebug() << "[자동연결] 해제됨 - 재연결 스레드 중단";
            stopReconnectThread();
        }
    });

    serverGroup->setLayout(formLayout);
    mainLayout->addWidget(serverGroup);

    // 연결 상태 그룹
    QGroupBox* statusGroup = new QGroupBox("연결 상태", this);
    QVBoxLayout* statusLayout = new QVBoxLayout();
    
    connectionStatusLabel = new QLabel("✗ 연결 안됨", this);
    connectionStatusLabel->setStyleSheet(
        "QLabel { "
        "padding: 10px; "
        "background-color: #f8d7da; "
        "border: 1px solid #f5c6cb; "
        "border-radius: 5px; "
        "color: #721c24; "
        "font-weight: bold; "
        "}"
    );
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
    reconnectInterval = config->getReconnectInterval();
    
    ipEdit->setText(serverIp);
    portEdit->setText(QString::number(serverPort));
    reconnectIntervalEdit->setText(QString::number(reconnectInterval));
    autoConnectCheckBox->setChecked(autoConnect);
}

void ClientDialog::saveSettings()
{
    serverIp = ipEdit->text().trimmed();
    serverPort = portEdit->text().toInt();
    reconnectInterval = reconnectIntervalEdit->text().toInt();
    if (reconnectInterval < 1) reconnectInterval = 10;
    autoConnect = autoConnectCheckBox->isChecked();
    
    ConfigManager* config = ConfigManager::instance();
    config->setServerIp(serverIp);
    config->setServerPort(serverPort);
    config->setAutoConnect(autoConnect);
    config->setReconnectInterval(reconnectInterval);
    config->saveConfig();
    
    qDebug() << "서버 설정 저장됨 - IP:" << serverIp << "Port:" << serverPort << "재연결 간격:" << reconnectInterval << "초";
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
    if (autoConnectCheckBox) {
        autoConnectCheckBox->setChecked(enable);
    }
}

void ClientDialog::setReconnectInterval(int seconds)
{
    reconnectInterval = seconds;
    if (reconnectIntervalEdit) {
        reconnectIntervalEdit->setText(QString::number(seconds));
    }
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
    
    // 연결 성공 시 재연결 스레드 중지
    if (reconnectThread && reconnectThread->isRunning()) {
        stopReconnectThread();
    }
    
    statusLabel->setText(QString("서버에 연결되었습니다: %1:%2")
                        .arg(testSocket->peerAddress().toString())
                        .arg(testSocket->peerPort()));
    connectionStatusLabel->setText("✓ 연결됨");
    connectionStatusLabel->setStyleSheet("QLabel { padding: 10px; background-color: #d4edda; border-radius: 5px; color: #155724; }");
    testButton->setEnabled(true);
    testButton->setText("연결 해제");
}

void ClientDialog::onSocketDisconnected()
{
    isConnected = false;
    statusLabel->setText("서버와의 연결이 해제되었습니다.");
    connectionStatusLabel->setText("미연결");
    connectionStatusLabel->setStyleSheet("QLabel { padding: 10px; background-color: #f0f0f0; border-radius: 5px; }");
    testButton->setEnabled(true);
    testButton->setText("연결 테스트");
    
    // 자동 연결이 활성화된 경우만 재연결 스레드 시작
    if (autoConnect) {
        startReconnectThread();
    }
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
}

void ClientDialog::onDataReceived()
{
    QByteArray data = testSocket->readAll();
    
    qDebug() << "[소켓통신 READ] 수신 데이터:" << data.toHex(' ') << "| 크기:" << data.size() << "bytes";
    
    // 바이너리 바이트 값(0x00, 0x01, 0x02, 0x03)을 정수로 변환 → 프레임 트리거로 사용
    // 여러 바이트를 동시에 받은 경우 모두 처리
    for (int i = 0; i < data.size(); ++i) {
        unsigned char byte = static_cast<unsigned char>(data[i]);
        int frameIndex = static_cast<int>(byte);
        
        if (frameIndex >= 0 && frameIndex <= 3) {
            qDebug() << "[소켓통신 READ] 프레임" << frameIndex << "트리거 수신";
            emit frameIndexReceived(frameIndex);
        } else {
            qWarning() << "[소켓통신 READ] 유효하지 않은 데이터:" << frameIndex;
        }
    }
}

void ClientDialog::updateConnectionStatus()
{
    // 주기적으로 연결 상태 확인
    if (testSocket->state() == QAbstractSocket::ConnectedState && !isConnected) {
        isConnected = true;
        // 연결됨 상태 표시
        connectionStatusLabel->setText("✓ 연결됨");
        connectionStatusLabel->setStyleSheet(
            "QLabel { "
            "padding: 10px; "
            "background-color: #d4edda; "
            "border: 1px solid #28a745; "
            "border-radius: 5px; "
            "color: #155724; "
            "font-weight: bold; "
            "}"
        );
    } else if (testSocket->state() != QAbstractSocket::ConnectedState && isConnected) {
        isConnected = false;
        onSocketDisconnected();
    }
    
    // 연결되지 않은 경우 상태 표시
    if (!isConnected && connectionStatusLabel->text() != "✗ 연결 안됨") {
        connectionStatusLabel->setText("✗ 연결 안됨");
        connectionStatusLabel->setStyleSheet(
            "QLabel { "
            "padding: 10px; "
            "background-color: #f8d7da; "
            "border: 1px solid #f5c6cb; "
            "border-radius: 5px; "
            "color: #721c24; "
            "font-weight: bold; "
            "}"
        );
    }
}

void ClientDialog::onSaveSettings()
{
    saveSettings();
    emit settingsChanged();
    
    // 상태 라벨에 저장 완료 메시지 표시
    statusLabel->setText("✓ 서버 설정이 저장되었습니다.");
    statusLabel->setStyleSheet("QLabel { color: #155724; font-size: 11px; }");
    
    // 3초 후 메시지 지우기
    QTimer::singleShot(3000, this, [this]() {
        if (statusLabel->text() == "✓ 서버 설정이 저장되었습니다.") {
            statusLabel->setText("");
        }
    });
    
    accept();
}

void ClientDialog::updateLanguage()
{
    // 언어 변경 시 UI 텍스트 업데이트
    setWindowTitle("서버 연결 설정");
}

void ClientDialog::initialize()
{
    // 설정 로드
    loadSettings();
    
    // 자동 연결이 활성화된 경우 재연결 스레드 시작
    if (autoConnect) {
        startReconnectThread();
    }
}

void ClientDialog::startReconnectThread()
{
    // 기존 스레드가 실행 중이면 먼저 종료
    if (reconnectThread && reconnectThread->isRunning()) {
        stopReconnectThread();
    }
    
    shouldReconnect = true;
    
    reconnectThread = QThread::create([this]() {
        while (shouldReconnect) {
            if (testSocket->state() != QAbstractSocket::ConnectedState) {
                QMetaObject::invokeMethod(this, "tryReconnect", Qt::QueuedConnection);
            }
            
            // reconnectInterval 초 대기
            for (int i = 0; i < reconnectInterval && shouldReconnect; ++i) {
                QThread::sleep(1);
            }
        }
    });
    
    reconnectThread->start();
}

void ClientDialog::stopReconnectThread()
{
    shouldReconnect = false;
    
    if (reconnectThread) {
        if (reconnectThread->isRunning()) {
            if (!reconnectThread->wait(5000)) {
                reconnectThread->terminate();
                reconnectThread->wait();
            }
        }
        delete reconnectThread;
        reconnectThread = nullptr;
    }
}

void ClientDialog::tryReconnect()
{
    if (testSocket->state() == QAbstractSocket::ConnectedState) {
        return; // 이미 연결됨
    }
    
    // 소켓이 연결 해제 상태가 아니면 강제 종료
    if (testSocket->state() != QAbstractSocket::UnconnectedState) {
        testSocket->abort();
    }
    
    testSocket->connectToHost(serverIp, serverPort);
}

bool ClientDialog::sendMessage(const QString& message)
{
    if (!isConnected || testSocket->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "[ClientDialog] 서버에 연결되지 않아 메시지를 전송할 수 없습니다:" << message;
        return false;
    }
    
    QByteArray data = message.toUtf8();
    qint64 bytesWritten = testSocket->write(data);
    
    if (bytesWritten == -1) {
        qWarning() << "[ClientDialog] 메시지 전송 실패:" << testSocket->errorString();
        return false;
    }
    
    testSocket->flush();
    qDebug() << "[ClientDialog] 메시지 전송 성공:" << message << "(" << bytesWritten << "bytes)";
    return true;
}

bool ClientDialog::sendData(const QByteArray& data)
{
    if (!isConnected || testSocket->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "[ClientDialog] 서버에 연결되지 않아 데이터를 전송할 수 없습니다";
        return false;
    }
    
    qint64 bytesWritten = testSocket->write(data);
    
    if (bytesWritten == -1) {
        qWarning() << "[ClientDialog] 데이터 전송 실패:" << testSocket->errorString();
        return false;
    }
    
    testSocket->flush();
    qDebug() << "[ClientDialog] 데이터 전송 성공:" << bytesWritten << "bytes";
    return true;
}
