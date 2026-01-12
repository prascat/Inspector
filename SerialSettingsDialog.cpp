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
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setMinimumSize(600, 700);
    resize(600, 700);
    setStyleSheet(
        "QDialog {"
        "    background-color: rgba(30, 30, 30, 240);"
        "    border: 2px solid rgba(100, 100, 100, 200);"
        "}"
        "QGroupBox {"
        "    color: white;"
        "    background-color: transparent;"
        "    border: 1px solid rgba(100, 100, 100, 150);"
        "    margin-top: 10px;"
        "    padding-top: 10px;"
        "}"
        "QGroupBox::title {"
        "    color: white;"
        "    subcontrol-origin: margin;"
        "    left: 10px;"
        "    padding: 0 5px;"
        "}"
        "QLabel {"
        "    color: white;"
        "    background-color: transparent;"
        "}"
        "QComboBox {"
        "    background-color: rgba(50, 50, 50, 180);"
        "    color: white;"
        "    border: 1px solid rgba(100, 100, 100, 150);"
        "    padding: 5px;"
        "}"
        "QComboBox::drop-down {"
        "    border: none;"
        "    width: 20px;"
        "}"
        "QComboBox::down-arrow {"
        "    image: none;"
        "    border-left: 5px solid transparent;"
        "    border-right: 5px solid transparent;"
        "    border-top: 5px solid white;"
        "    width: 0;"
        "    height: 0;"
        "    margin-right: 5px;"
        "}"
        "QComboBox QAbstractItemView {"
        "    background-color: rgba(50, 50, 50, 240);"
        "    color: white;"
        "    selection-background-color: rgba(70, 70, 70, 200);"
        "}"
        "QSpinBox {"
        "    background-color: rgba(50, 50, 50, 180);"
        "    color: white;"
        "    border: 1px solid rgba(100, 100, 100, 150);"
        "    padding: 3px;"
        "}"
        "QSpinBox::up-button {"
        "    border: none;"
        "    width: 16px;"
        "}"
        "QSpinBox::down-button {"
        "    border: none;"
        "    width: 16px;"
        "}"
        "QSpinBox::up-arrow {"
        "    image: none;"
        "    border-left: 4px solid transparent;"
        "    border-right: 4px solid transparent;"
        "    border-bottom: 4px solid white;"
        "    width: 0;"
        "    height: 0;"
        "}"
        "QSpinBox::down-arrow {"
        "    image: none;"
        "    border-left: 4px solid transparent;"
        "    border-right: 4px solid transparent;"
        "    border-top: 4px solid white;"
        "    width: 0;"
        "    height: 0;"
        "}"
        "QLineEdit {"
        "    background-color: rgba(50, 50, 50, 180);"
        "    color: white;"
        "    border: 1px solid rgba(100, 100, 100, 150);"
        "    padding: 5px;"
        "}"
        "QTextEdit {"
        "    background-color: rgba(50, 50, 50, 180);"
        "    color: white;"
        "    border: 1px solid rgba(100, 100, 100, 150);"
        "}"
        "QCheckBox {"
        "    color: white;"
        "}"
        "QPushButton {"
        "    background-color: rgba(70, 70, 70, 200);"
        "    color: white;"
        "    border: 1px solid rgba(100, 100, 100, 150);"
        "    padding: 8px 16px;"
        "    font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "    background-color: rgba(90, 90, 90, 220);"
        "}"
        "QPushButton:pressed {"
        "    background-color: rgba(60, 60, 60, 220);"
        "}"
    );
    
    setupUI();
    connectSignals();
    updateUITexts();
    loadSettings();  // 설정 로드
    refreshPortList();
    updateConnectionStatus();
    tryAutoConnect(); // 저장된 설정으로 자동 연결 시도
}

int SerialSettingsDialog::exec() {
    // 부모 중심에 배치
    if (parentWidget()) {
        QWidget* topWindow = parentWidget()->window();
        QRect parentRect = topWindow->frameGeometry();
        
        int x = parentRect.x() + (parentRect.width() - width()) / 2;
        int y = parentRect.y() + (parentRect.height() - height()) / 2;
        
        // 타이틀바 높이만큼 보정
        int titleBarHeight = topWindow->frameGeometry().height() - topWindow->geometry().height();
        y -= titleBarHeight / 2;
        
        move(x, y);
    }
    
    return QDialog::exec();
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
    refreshButton->setMaximumWidth(100);
    
    baudRateLabel = new QLabel("속도:");
    baudRateSpinBox = new QSpinBox();
    baudRateSpinBox->setRange(1200, 115200);
    baudRateSpinBox->setValue(115200);
    
    connectionLayout->addWidget(portLabel, 0, 0);
    connectionLayout->addWidget(portComboBox, 0, 1);
    connectionLayout->addWidget(refreshButton, 0, 2);
    connectionLayout->addWidget(baudRateLabel, 1, 0);
    connectionLayout->addWidget(baudRateSpinBox, 1, 1);
    
    // 자동 연결 체크박스
    autoConnectCheckBox = new QCheckBox("자동 연결");
    connectionLayout->addWidget(autoConnectCheckBox, 1, 2);
    
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
    
    sendModeComboBox = new QComboBox();
    sendModeComboBox->addItem("ASCII");
    sendModeComboBox->addItem("HEX");
    sendModeComboBox->setMaximumWidth(80);
    
    sendTestButton = new QPushButton("전송");
    sendTestButton->setMaximumWidth(60);
    
    testLayout->addWidget(testCommandLabel);
    testLayout->addWidget(testCommandLineEdit);
    testLayout->addWidget(sendModeComboBox);
    testLayout->addWidget(sendTestButton);
    
    sendRealSerialCheckBox = new QCheckBox("실제 시리얼로 전송");
    testLayout->addWidget(sendRealSerialCheckBox);
    
    mainLayout->addWidget(testGroup);
    
    // 로그
    QGroupBox* logGroup = new QGroupBox("로그");
    QVBoxLayout* logLayout = new QVBoxLayout(logGroup);
    
    logTextEdit = new QTextEdit();
    logTextEdit->setMinimumHeight(350);
    logTextEdit->setReadOnly(true);
    logLayout->addWidget(logTextEdit);
    
    mainLayout->addWidget(logGroup);
    
    // 수신 데이터
    QGroupBox* receiveGroup = new QGroupBox("수신 데이터");
    QVBoxLayout* receiveLayout = new QVBoxLayout(receiveGroup);
    
    receiveTextEdit = new QTextEdit();
    receiveTextEdit->setMinimumHeight(150);
    receiveTextEdit->setReadOnly(true);
    receiveLayout->addWidget(receiveTextEdit);
    
    mainLayout->addWidget(receiveGroup);
    
    // 하단 버튼들
    saveSettingsButton = new QPushButton("설정 저장");
    saveSettingsButton->setMaximumWidth(120);
    clearLogButton = new QPushButton("로그 지우기");
    clearLogButton->setMaximumWidth(120);
    clearReceiveButton = new QPushButton("수신 지우기");
    clearReceiveButton->setMaximumWidth(120);
    closeButton = new QPushButton("닫기");
    closeButton->setMaximumWidth(80);
    
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    bottomLayout->addWidget(saveSettingsButton);
    bottomLayout->addWidget(clearLogButton);
    bottomLayout->addWidget(clearReceiveButton);
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
    
    bool isHexMode = (sendModeComboBox->currentText() == "HEX");
    QByteArray dataToSend;
    
    if (isHexMode) {
        // HEX 모드: 공백으로 구분된 16진수 바이트 파싱
        QStringList hexBytes = command.split(" ", Qt::SkipEmptyParts);
        for (const QString& hexByte : hexBytes) {
            bool ok;
            quint8 byte = hexByte.toUInt(&ok, 16);
            if (ok) {
                dataToSend.append(byte);
            } else {
                addLogMessage(QString("[오류] 잘못된 HEX 값: %1").arg(hexByte));
                return;
            }
        }
        addLogMessage(QString("전송 (HEX): %1 (%2 bytes)").arg(command).arg(dataToSend.size()));
    } else {
        // ASCII 모드: 문자열 그대로 전송
        dataToSend = command.toUtf8();
        addLogMessage(QString("전송 (ASCII): %1").arg(command));
    }
    
    if (sendRealSerialCheckBox->isChecked()) {
        // 실제 시리얼 포트로 전송
        addLogMessage("→ 실제 시리얼 포트로 전송");
        if (isHexMode) {
            serialComm->sendRawData(dataToSend);
        } else {
            serialComm->sendResponse(command);
        }
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
    bool savedAutoConnect = config->getSerialAutoConnect();
    
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
    
    autoConnectCheckBox->setChecked(savedAutoConnect);
    addLogMessage(QString("자동 연결 설정 로드됨: %1").arg(savedAutoConnect ? "활성화" : "비활성화"));
}

void SerialSettingsDialog::saveSettings()
{
    ConfigManager* config = ConfigManager::instance();
    
    // 현재 연결된 포트와 보드레이트를 저장
    QString currentPort = portComboBox->currentText();
    int currentBaudRate = baudRateSpinBox->value();
    bool autoConnect = autoConnectCheckBox->isChecked();
    
    config->setSerialPort(currentPort);
    config->setSerialBaudRate(currentBaudRate);
    config->setSerialAutoConnect(autoConnect);
    
    addLogMessage(QString("설정 저장됨: %1 @ %2 baud, 자동연결: %3")
        .arg(currentPort).arg(currentBaudRate).arg(autoConnect ? "활성화" : "비활성화"));
}

void SerialSettingsDialog::tryAutoConnect()
{
    ConfigManager* config = ConfigManager::instance();
    bool autoConnect = config->getSerialAutoConnect();
    
    // 자동 연결이 비활성화된 경우 스킨
    if (!autoConnect) {
        addLogMessage("자동 연결이 비활성화되어 있습니다.");
        return;
    }
    
    QString savedPort = config->getSerialPort();
    
    if (!savedPort.isEmpty() && serialComm) {
        // 이미 refreshPortList()에서 포트 목록을 가져왔으므로 중복 호출 방지
        // portComboBox에 있는 항목 중에서 찾기
        int portIndex = -1;
        for (int i = 0; i < portComboBox->count(); ++i) {
            QString portText = portComboBox->itemText(i);
            if (portText.startsWith(savedPort)) {
                portIndex = i;
                break;
            }
        }
        
        if (portIndex >= 0) {
            addLogMessage(QString("저장된 설정으로 자동 연결 시도: %1").arg(savedPort));
            connectToPort();
            return;
        }
        
        addLogMessage(QString("저장된 포트 %1을 찾을 수 없습니다").arg(savedPort));
    }
}
