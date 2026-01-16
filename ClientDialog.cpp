#include "ClientDialog.h"
#include "ConfigManager.h"
#include "LanguageManager.h"
#include "CustomMessageBox.h"
#include <QJsonArray>
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
    , heartbeatInterval(30)
    , reconnectThread(nullptr)
    , shouldReconnect(false)
    , isConnected(false)
    , sequenceNumber(0)
{
    setWindowTitle("서버 연결 설정");
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setMinimumWidth(500);
    setModal(true);

    testSocket = new QTcpSocket(this);
    statusTimer = new QTimer(this);
    heartbeatTimer = new QTimer(this);
    
    connect(testSocket, &QTcpSocket::connected, this, &ClientDialog::onSocketConnected);
    connect(testSocket, &QTcpSocket::disconnected, this, &ClientDialog::onSocketDisconnected);
    connect(testSocket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &ClientDialog::onSocketError);
    connect(testSocket, &QTcpSocket::readyRead, this, &ClientDialog::onDataReceived);
    connect(statusTimer, &QTimer::timeout, this, &ClientDialog::updateConnectionStatus);
    connect(heartbeatTimer, &QTimer::timeout, this, &ClientDialog::sendHeartbeat);
    
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
    
    // Heartbeat 주기 입력
    heartbeatIntervalEdit = new QLineEdit(this);
    heartbeatIntervalEdit->setPlaceholderText("예: 30");
    heartbeatIntervalEdit->setValidator(new QIntValidator(5, 300, this));
    formLayout->addRow("Heartbeat 주기(초):", heartbeatIntervalEdit);
    
    // 자동 연결 체크박스
    autoConnectCheckBox = new QCheckBox("프로그램 시작 시 자동 연결", this);
    formLayout->addRow("", autoConnectCheckBox);
    // 자동연결 체크박스 변경 시 즉시 재연결 시작/중단
    connect(autoConnectCheckBox, &QCheckBox::stateChanged, this, [this](int state) {
        if (state == Qt::Checked) {
            qDebug() << "[AutoConnect] Checked - Starting reconnect thread";
            startReconnectThread();
        } else {
            qDebug() << "[AutoConnect] Unchecked - Stopping reconnect thread";
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
    heartbeatInterval = config->getHeartbeatInterval();
    
    ipEdit->setText(serverIp);
    portEdit->setText(QString::number(serverPort));
    reconnectIntervalEdit->setText(QString::number(reconnectInterval));
    heartbeatIntervalEdit->setText(QString::number(heartbeatInterval));
    autoConnectCheckBox->setChecked(autoConnect);
}

void ClientDialog::saveSettings()
{
    serverIp = ipEdit->text().trimmed();
    serverPort = portEdit->text().toInt();
    reconnectInterval = reconnectIntervalEdit->text().toInt();
    if (reconnectInterval < 1) reconnectInterval = 10;
    heartbeatInterval = heartbeatIntervalEdit->text().toInt();
    if (heartbeatInterval < 5) heartbeatInterval = 30;
    autoConnect = autoConnectCheckBox->isChecked();
    
    ConfigManager* config = ConfigManager::instance();
    config->setServerIp(serverIp);
    config->setServerPort(serverPort);
    config->setAutoConnect(autoConnect);
    config->setReconnectInterval(reconnectInterval);
    config->setHeartbeatInterval(heartbeatInterval);
    config->saveConfig();
    
    qDebug() << "Server settings saved - IP:" << serverIp << "Port:" << serverPort << "Reconnect interval:" << reconnectInterval << "sec" << "Heartbeat interval:" << heartbeatInterval << "sec";
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
    
    // Heartbeat 타이머 시작 (설정값 사용)
    int intervalMs = heartbeatInterval * 1000;
    heartbeatTimer->start(intervalMs);
    qDebug() << "[Protocol] Heartbeat timer started -" << heartbeatInterval << "sec interval";
    
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
    
    // Stop Heartbeat timer
    heartbeatTimer->stop();
    qDebug() << "[Protocol] Heartbeat timer stopped";
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
    processReceivedData();
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

// ===== 프로토콜 처리 함수 =====

#include <QJsonDocument>
#include <QDateTime>
#include <cstring>

// 프로토콜 메시지 전송 (헤더 + JSON)
bool ClientDialog::sendProtocolMessage(MessageType type, const QByteArray& jsonData)
{
    if (!isConnected || testSocket->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "[Protocol] 서버 미연결 - 전송 실패";
        return false;
    }
    
    // 헤더 생성
    ProtocolHeader header;
    header.stx = STX;
    header.messageType = static_cast<uint32_t>(type);
    header.sequenceNumber = ++sequenceNumber;
    header.dataLength = jsonData.size();
    header.timestamp = QDateTime::currentMSecsSinceEpoch();
    header.checksum = 0;  // 현재 미사용
    header.reserved = 0;
    
    // 헤더를 바이트 배열로 변환
    QByteArray headerData(reinterpret_cast<const char*>(&header), sizeof(ProtocolHeader));
    
    // 전체 메시지 = 헤더 + 본문
    QByteArray fullMessage = headerData + jsonData;
    
    qint64 bytesWritten = testSocket->write(fullMessage);
    if (bytesWritten == -1) {
        qWarning() << "[Protocol] 전송 실패:" << testSocket->errorString();
        return false;
    }
    
    testSocket->flush();
    
    QString typeName;
    switch(type) {
        case MessageType::RECIPE_ALL_REQUEST: typeName = "RECIPE_ALL_REQUEST"; break;
        case MessageType::RECIPE_ALL_RESPONSE: typeName = "RECIPE_ALL_RESPONSE"; break;
        case MessageType::RECIPE_READY: typeName = "RECIPE_READY"; break;
        case MessageType::RECIPE_OK: typeName = "RECIPE_OK"; break;
        case MessageType::RECIPE_EMPTY: typeName = "RECIPE_EMPTY"; break;
        case MessageType::INSPECT_RESPONSE: typeName = "INSPECT_RESPONSE"; break;
        case MessageType::HEARTBEAT_OK: typeName = "HEARTBEAT_OK"; break;
        case MessageType::ERROR: typeName = "ERROR"; break;
        default: typeName = "UNKNOWN"; break;
    }
    
    qDebug().noquote() << QString("[Protocol] Sent %1 - Type:0x%2 Seq:%3 Size:%4+%5=%6 bytes")
        .arg(typeName)
        .arg(static_cast<uint32_t>(type), 2, 16, QChar('0'))
        .arg(header.sequenceNumber)
        .arg(sizeof(ProtocolHeader))
        .arg(jsonData.size())
        .arg(bytesWritten);
    
    return true;
}

// 검사 결과 전송
bool ClientDialog::sendInspectionResult(const QJsonObject& result)
{
    QJsonDocument doc(result);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    
    qDebug().noquote() << "[Protocol] 검사 결과 전송:" << QString::fromUtf8(jsonData);
    
    return sendProtocolMessage(MessageType::INSPECT_RESPONSE, jsonData);
}

// Heartbeat 전송 (레거시 타입 사용)
bool ClientDialog::sendHeartbeat()
{
    QJsonObject heartbeat;
    heartbeat["type"] = "heartbeat";
    heartbeat["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    
    QJsonDocument doc(heartbeat);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
    
    return sendProtocolMessage(MessageType::HEARTBEAT_OK, jsonData);
}

// 수신 데이터 처리
void ClientDialog::processReceivedData()
{
    receiveBuffer.append(testSocket->readAll());
    
    while (receiveBuffer.size() >= static_cast<int>(sizeof(ProtocolHeader))) {
        // 헤더 파싱
        ProtocolHeader header;
        if (!parseHeader(receiveBuffer.left(sizeof(ProtocolHeader)), header)) {
            qWarning() << "[Protocol] 헤더 파싱 실패 - STX 불일치, 1바이트 버림";
            receiveBuffer.remove(0, 1);  // STX 찾을 때까지 1바이트씩 버림
            continue;
        }
        
        // 데이터 길이 검증
        if (header.dataLength < 0 || header.dataLength > MAX_DATA_LENGTH) {
            qWarning() << "[Protocol] 비정상 데이터 길이:" << header.dataLength << "- 연결 해제";
            receiveBuffer.clear();
            testSocket->disconnectFromHost();
            return;
        }
        
        // 전체 메시지가 수신될 때까지 대기
        int totalSize = sizeof(ProtocolHeader) + header.dataLength;
        if (receiveBuffer.size() < totalSize) {
            return;  // 더 기다림
        }
        
        // 본문 추출
        QByteArray jsonData = receiveBuffer.mid(sizeof(ProtocolHeader), header.dataLength);
        receiveBuffer.remove(0, totalSize);
        
        qDebug().noquote() << QString("[Protocol] 수신 완료 - Type:0x%1 Seq:%2 Size:%3 bytes")
            .arg(header.messageType, 2, 16, QChar('0'))
            .arg(header.sequenceNumber)
            .arg(header.dataLength);
        
        // 메시지 타입별 처리
        MessageType type = static_cast<MessageType>(header.messageType);
        
        switch (type) {
        case MessageType::RECIPE_READY: {
            // Lims → Vision: Lims가 선택한 회로선택 Vision에 요청
            if (header.dataLength > 0) {
                QJsonDocument doc = QJsonDocument::fromJson(jsonData);
                if (!doc.isNull() && doc.isObject()) {
                    handleRecipeReady(doc.object());
                } else {
                    qWarning() << "[Protocol] JSON 파싱 실패:" << QString::fromUtf8(jsonData);
                }
            }
            break;
        }
        
        case MessageType::RECIPE_ALL_RESPONSE: {
            // Lims → Vision: JSON(회로명, 전선, 전선길이, 단자, 씰)
            if (header.dataLength > 0) {
                QJsonDocument doc = QJsonDocument::fromJson(jsonData);
                if (!doc.isNull()) {
                    handleRecipeAllResponse(doc);
                } else {
                    qWarning() << "[Protocol] JSON 파싱 실패:" << QString::fromUtf8(jsonData);
                }
            }
            break;
        }
        
        case MessageType::ERROR: {
            qWarning() << "[Protocol] 서버 오류 메시지:" << QString::fromUtf8(jsonData);
            break;
        }
        
        default:
            qWarning() << "[Protocol] 알 수 없는 메시지 타입:" << header.messageType;
            break;
        }
    }
}

// 헤더 파싱
bool ClientDialog::parseHeader(const QByteArray& headerData, ProtocolHeader& header)
{
    if (headerData.size() < static_cast<int>(sizeof(ProtocolHeader))) {
        return false;
    }
    
    std::memcpy(&header, headerData.constData(), sizeof(ProtocolHeader));
    
    // STX 검증
    if (header.stx != STX) {
        return false;
    }
    
    return true;
}

// 회로 준비 요청 처리 (Lims → Vision)
void ClientDialog::handleRecipeReady(const QJsonObject& request)
{
    qDebug().noquote() << "[Protocol] 회로 준비 요청 수신:" 
                       << QJsonDocument(request).toJson(QJsonDocument::Compact);
    
    emit recipeReadyReceived(request);
}

// 회로 목록 응답 처리 (Lims → Vision)
void ClientDialog::handleRecipeAllResponse(const QJsonDocument& response)
{
    qDebug().noquote() << "[Protocol] 회로 목록 수신:" 
                       << response.toJson(QJsonDocument::Compact);
    
    // JSON 배열 추출
    if (response.isArray()) {
        QJsonArray recipes = response.array();
        qDebug() << "[Protocol] emit recipeListReceived 호출 - 레시피" << recipes.size() << "개";
        emit recipeListReceived(recipes);
        qDebug() << "[Protocol] emit recipeListReceived 완료";
    } else {
        qDebug() << "[Protocol] 회로 목록이 배열 형식이 아닙니다";
    }
}
