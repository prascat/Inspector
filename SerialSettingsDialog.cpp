#include "SerialSettingsDialog.h"
#include "SerialCommunication.h"
#include "ConfigManager.h"
#include "CustomMessageBox.h"
#include <QMessageBox>
#include <QDateTime>
#include <QGroupBox>
#include <QGridLayout>

SerialSettingsDialog::SerialSettingsDialog(SerialCommunication* serialComm, QWidget *parent)
    : QDialog(parent)
    , serialComm(serialComm)
{
    setWindowTitle("시리얼 통신 설정");
    setMinimumSize(500, 500);
    resize(500, 500);
    setStyleSheet(
        "QDialog { background: #2b2b2b; color: white; }"
        "QFrame { background: #3a3a3a; border: 1px solid #555; border-radius: 4px; }"
        "QLabel { color: white; }"
        "QPushButton { background: #4a4a4a; border: 1px solid #666; border-radius: 3px; "
        "padding: 5px; color: white; }"
        "QPushButton:hover { background: #5a5a5a; }"
        "QComboBox, QLineEdit, QSpinBox { background: #1e1e1e; color: white; "
        "border: 1px solid #666; padding: 3px; }"
        "QTextEdit { background: #1e1e1e; color: white; border: 1px solid #666; }"
        "QCheckBox { color: white; }"
    );
    
    setupUI();
    connectSignals();
    updateUITexts();
    loadSettings();  // 설정 로드
    refreshPortList();
    updateConnectionStatus();
    tryAutoConnect(); // 저장된 설정으로 자동 연결 시도
}

void SerialSettingsDialog::setupUI()
{
    // 단순한 수직 레이아웃
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    
    // 연결 설정
    QGroupBox* connectionGroup = new QGroupBox("연결 설정");
    QGridLayout* connectionLayout = new QGridLayout(connectionGroup);
    
    portLabel = new QLabel("포트:");
    portComboBox = new QComboBox();
    refreshButton = new QPushButton("새로고침");
    refreshButton->setMaximumWidth(80);
    
    baudRateLabel = new QLabel("속도:");
    baudRateSpinBox = new QSpinBox();
    baudRateSpinBox->setRange(1200, 115200);
    baudRateSpinBox->setValue(115200);
    
    connectionLayout->addWidget(portLabel, 0, 0);
    connectionLayout->addWidget(portComboBox, 0, 1);
    connectionLayout->addWidget(refreshButton, 0, 2);
    connectionLayout->addWidget(baudRateLabel, 1, 0);
    connectionLayout->addWidget(baudRateSpinBox, 1, 1);
    
    // 연결 버튼
    QHBoxLayout* connectLayout = new QHBoxLayout();
    connectButton = new QPushButton("연결");
    disconnectButton = new QPushButton("해제");
    statusLabel = new QLabel("연결 안됨");
    statusLabel->setAlignment(Qt::AlignCenter);
    
    connectLayout->addWidget(connectButton);
    connectLayout->addWidget(disconnectButton);
    connectLayout->addStretch();
    connectLayout->addWidget(statusLabel);
    
    connectionLayout->addLayout(connectLayout, 2, 0, 1, 3);
    mainLayout->addWidget(connectionGroup);
    
    // 테스트 명령
    QGroupBox* testGroup = new QGroupBox("명령 테스트");
    QHBoxLayout* testLayout = new QHBoxLayout(testGroup);
    
    testCommandLabel = new QLabel("명령:");
    testCommandLineEdit = new QLineEdit();
    sendTestButton = new QPushButton("전송");
    sendTestButton->setMaximumWidth(60);
    
    testLayout->addWidget(testCommandLabel);
    testLayout->addWidget(testCommandLineEdit);
    testLayout->addWidget(sendTestButton);
    
    sendRealSerialCheckBox = new QCheckBox("실제 시리얼로 전송");
    testLayout->addWidget(sendRealSerialCheckBox);
    
    mainLayout->addWidget(testGroup);
    
    // 로그
    QGroupBox* logGroup = new QGroupBox("로그");
    QVBoxLayout* logLayout = new QVBoxLayout(logGroup);
    
    logTextEdit = new QTextEdit();
    logTextEdit->setMaximumHeight(150);
    logTextEdit->setReadOnly(true);
    
    QHBoxLayout* logButtonLayout = new QHBoxLayout();
    clearLogButton = new QPushButton("로그 지우기");
    clearReceiveButton = new QPushButton("수신 지우기");
    logButtonLayout->addWidget(clearLogButton);
    logButtonLayout->addWidget(clearReceiveButton);
    logButtonLayout->addStretch();
    
    logLayout->addWidget(logTextEdit);
    logLayout->addLayout(logButtonLayout);
    
    receiveTextEdit = new QTextEdit();
    receiveTextEdit->setMaximumHeight(100);
    receiveTextEdit->setReadOnly(true);
    receiveTextEdit->setPlaceholderText("수신 데이터");
    
    logLayout->addWidget(receiveTextEdit);
    mainLayout->addWidget(logGroup);
    
    // 하단 버튼들
    saveSettingsButton = new QPushButton("설정 저장");
    saveSettingsButton->setMaximumWidth(80);
    closeButton = new QPushButton("닫기");
    closeButton->setMaximumWidth(80);
    
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    bottomLayout->addWidget(saveSettingsButton);
    bottomLayout->addStretch();
    bottomLayout->addWidget(closeButton);
    
    mainLayout->addLayout(bottomLayout);
}

void SerialSettingsDialog::connectSignals()
{
    connect(refreshButton, &QPushButton::clicked, this, &SerialSettingsDialog::refreshPortList);
    connect(connectButton, &QPushButton::clicked, this, &SerialSettingsDialog::connectToPort);
    connect(disconnectButton, &QPushButton::clicked, this, &SerialSettingsDialog::disconnectFromPort);
    connect(sendTestButton, &QPushButton::clicked, this, &SerialSettingsDialog::sendTestCommand);
    connect(clearLogButton, &QPushButton::clicked, this, &SerialSettingsDialog::clearLog);
    connect(clearReceiveButton, &QPushButton::clicked, this, &SerialSettingsDialog::clearReceiveData);
    connect(saveSettingsButton, &QPushButton::clicked, this, &SerialSettingsDialog::saveSettings);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    
    // Enter 키로 명령 전송
    connect(testCommandLineEdit, &QLineEdit::returnPressed, this, &SerialSettingsDialog::sendTestCommand);
    
    // 시리얼 통신 신호 연결
    if (serialComm) {
        connect(serialComm, &SerialCommunication::connectionStatusChanged,
                this, &SerialSettingsDialog::onConnectionStatusChanged);
        connect(serialComm, &SerialCommunication::commandReceived,
                this, &SerialSettingsDialog::onCommandReceived);
        connect(serialComm, &SerialCommunication::inspectionCompleted,
                this, &SerialSettingsDialog::onInspectionCompleted);
        connect(serialComm, &SerialCommunication::errorOccurred,
                this, &SerialSettingsDialog::onErrorOccurred);
    }
}

void SerialSettingsDialog::updateUITexts()
{
    // 새로운 UI는 이미 한글로 하드코딩되어 있으므로 별도 처리 불필요
    // 기존 번역 시스템 대신 직접 텍스트 설정
    setWindowTitle("시리얼 통신 설정");
}

void SerialSettingsDialog::refreshPortList()
{
    portComboBox->clear();
    
    if (!serialComm) return;
    
    QStringList availablePorts = serialComm->getAvailableSerialPorts();
    
    // 우선순위 포트 검색 (FTDI, USB Serial 등)
    bool foundPriorityDevice = false;
    
    for (const QString& port : availablePorts) {
        portComboBox->addItem(port);
        
        // 우선순위 장치 확인 (FTDI, USB Serial 등)
        QString portLower = port.toLower();
        if (portLower.contains("ftdi") || 
            portLower.contains("usb serial") ||
            portLower.contains("ch340") ||
            portLower.contains("ch341") ||
            portLower.contains("cp210")) {
            foundPriorityDevice = true;
            portComboBox->setCurrentText(port);
        }
    }
    
    if (availablePorts.isEmpty()) {
        portComboBox->addItem(TR("NO_PORTS_AVAILABLE"));
        connectButton->setEnabled(false);
    } else {
        connectButton->setEnabled(true);
        if (!foundPriorityDevice && !availablePorts.isEmpty()) {
            portComboBox->setCurrentIndex(0);
        }
    }
    
    addLogMessage(QString("포트 목록 갱신됨: %1개 포트 발견").arg(availablePorts.count()));
    if (foundPriorityDevice) {
        addLogMessage("우선순위 USB Serial 장치 발견됨");
    }
}

void SerialSettingsDialog::connectToPort()
{
    if (!serialComm) return;
    
    QString selectedPortDisplay = portComboBox->currentText();
    int baudRate = baudRateSpinBox->value();
    
    if (selectedPortDisplay.isEmpty() || selectedPortDisplay == TR("NO_PORTS_AVAILABLE")) {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Warning);
        msgBox.setTitle(TR("WARNING"));
        msgBox.setMessage(TR("PLEASE_SELECT_PORT"));
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }
    
    // 표시 이름에서 실제 포트 이름 추출 (괄호 앞 부분)
    QString selectedPort = selectedPortDisplay.split(" (").first();
    
    addLogMessage(QString("연결 시도: %1 (%2 baud)").arg(selectedPort).arg(baudRate));
    
    if (serialComm->connectToPort(selectedPort, baudRate)) {
        addLogMessage("연결 성공!");
        saveSettings();  // 연결 성공 시 설정 저장
    } else {
        addLogMessage("연결 실패!");
    }
}

void SerialSettingsDialog::disconnectFromPort()
{
    if (!serialComm) return;
    
    serialComm->disconnectPort();
    addLogMessage("연결 해제됨");
}

void SerialSettingsDialog::updateConnectionStatus()
{
    if (!serialComm) {
        statusLabel->setText("시리얼 통신 객체 없음");
        return;
    }
    
    if (serialComm->isConnected()) {
        statusLabel->setText("연결됨");
        statusLabel->setStyleSheet("color: green;");
        connectButton->setEnabled(false);
        disconnectButton->setEnabled(true);
        sendTestButton->setEnabled(true);
    } else {
        statusLabel->setText("연결 안됨");
        statusLabel->setStyleSheet("color: red;");
        connectButton->setEnabled(true);
        disconnectButton->setEnabled(false);
        sendTestButton->setEnabled(false);
    }
}

void SerialSettingsDialog::onConnectionStatusChanged(bool connected)
{
    updateConnectionStatus();
    
    if (connected) {
        addLogMessage("시리얼 포트 연결됨");
    } else {
        addLogMessage("시리얼 포트 연결 해제됨");
    }
}

void SerialSettingsDialog::onCommandReceived(const QString& command)
{
    addLogMessage(QString("수신: %1").arg(command));
    addReceiveData(QString("RX: %1").arg(command));
}

void SerialSettingsDialog::onInspectionCompleted(int cameraNumber, const QString& result)
{
    addLogMessage(QString("응답: 카메라%1 -> %2").arg(cameraNumber).arg(result));
}

void SerialSettingsDialog::onErrorOccurred(const QString& error)
{
    addLogMessage(QString("에러: %1").arg(error));
}

void SerialSettingsDialog::sendTestCommand()
{
    if (!serialComm || !serialComm->isConnected()) {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Warning);
        msgBox.setTitle(TR("WARNING"));
        msgBox.setMessage(TR("PLEASE_CONNECT_FIRST"));
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }
    
    QString command = testCommandLineEdit->text().trimmed();
    if (command.isEmpty()) {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Warning);
        msgBox.setTitle(TR("WARNING"));
        msgBox.setMessage(TR("PLEASE_ENTER_COMMAND"));
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }
    
    addLogMessage(QString("전송: %1").arg(command));
    
    if (sendRealSerialCheckBox->isChecked()) {
        // 실제 시리얼 포트로 전송
        addLogMessage("→ 실제 시리얼 포트로 전송");
        serialComm->sendResponse(command);
    } else {
        // 내부 processCommand로 시뮬레이션
        addLogMessage("→ 내부 명령 처리로 시뮬레이션");
        QMetaObject::invokeMethod(serialComm, "processCommand",
                                Qt::QueuedConnection,
                                Q_ARG(QString, command));
    }
    
    // 전송 후 입력창 클리어
    testCommandLineEdit->clear();
}

void SerialSettingsDialog::clearLog()
{
    logTextEdit->clear();
}

void SerialSettingsDialog::clearReceiveData()
{
    receiveTextEdit->clear();
}

void SerialSettingsDialog::addLogMessage(const QString& message)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString logEntry = QString("[%1] %2").arg(timestamp).arg(message);
    
    logTextEdit->append(logEntry);
    
    // 자동 스크롤
    QTextCursor cursor = logTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    logTextEdit->setTextCursor(cursor);
}

void SerialSettingsDialog::addReceiveData(const QString& data)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString entry = QString("[%1] %2").arg(timestamp).arg(data);
    
    receiveTextEdit->append(entry);
    
    // 자동 스크롤
    QTextCursor cursor = receiveTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    receiveTextEdit->setTextCursor(cursor);
}

void SerialSettingsDialog::loadSettings()
{
    ConfigManager* config = ConfigManager::instance();
    
    // 저장된 시리얼 포트 설정 로드
    QString savedPort = config->getSerialPort();
    int savedBaudRate = config->getSerialBaudRate();
    
    if (!savedPort.isEmpty()) {
        // 콤보박스에서 해당 포트를 찾아서 설정
        int index = portComboBox->findText(savedPort);
        if (index >= 0) {
            portComboBox->setCurrentIndex(index);
            addLogMessage(QString("저장된 포트 설정 로드됨: %1").arg(savedPort));
        }
    }
    
    baudRateSpinBox->setValue(savedBaudRate);
    addLogMessage(QString("저장된 보드레이트 설정 로드됨: %1").arg(savedBaudRate));
}

void SerialSettingsDialog::saveSettings()
{
    ConfigManager* config = ConfigManager::instance();
    
    // 현재 연결된 포트와 보드레이트를 저장
    QString currentPort = portComboBox->currentText();
    int currentBaudRate = baudRateSpinBox->value();
    
    config->setSerialPort(currentPort);
    config->setSerialBaudRate(currentBaudRate);
    
    addLogMessage(QString("설정 저장됨: %1 @ %2 baud").arg(currentPort).arg(currentBaudRate));
}

void SerialSettingsDialog::tryAutoConnect()
{
    ConfigManager* config = ConfigManager::instance();
    QString savedPort = config->getSerialPort();
    
    if (!savedPort.isEmpty() && serialComm) {
        // 포트가 실제로 사용 가능한지 확인
        QStringList availablePorts = serialComm->getAvailableSerialPorts();
        
        for (const QString& port : availablePorts) {
            if (port.startsWith(savedPort)) {  // 저장된 포트와 일치하거나 시작하는 포트 찾기
                addLogMessage(QString("저장된 설정으로 자동 연결 시도: %1").arg(savedPort));
                connectToPort();
                return;
            }
        }
        
        addLogMessage(QString("저장된 포트 %1을 찾을 수 없습니다").arg(savedPort));
    }
}
