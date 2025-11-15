#include "SerialCommunication.h"
#include "TeachingWidget.h"
#include "ConfigManager.h"
#include <QApplication>
#include <QThread>
#include <QSerialPortInfo>

SerialCommunication::SerialCommunication(QObject *parent)
    : QObject(parent)
    , serialPort(nullptr)
    , teachingWidget(nullptr)
{
    serialPort = new QSerialPort(this);
    
    // 시리얼 포트 신호 연결
    connect(serialPort, &QSerialPort::readyRead, this, &SerialCommunication::readSerialData);
    connect(serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
            this, &SerialCommunication::handleSerialError);
}

SerialCommunication::~SerialCommunication()
{
    disconnectPort();
}

bool SerialCommunication::connectToPort(const QString &portName, int baudRate)
{
    if (serialPort->isOpen()) {
        serialPort->close();
    }

    serialPort->setPortName(portName);
    serialPort->setBaudRate(baudRate);
    serialPort->setDataBits(QSerialPort::Data8);
    serialPort->setParity(QSerialPort::NoParity);
    serialPort->setStopBits(QSerialPort::OneStop);
    serialPort->setFlowControl(QSerialPort::NoFlowControl);

    bool connected = serialPort->open(QIODevice::ReadWrite);
    
    if (connected) {
        qDebug() << "시리얼 포트 연결 성공:" << portName << "at" << baudRate << "baud";
        emit connectionStatusChanged(true);
    } else {
        qDebug() << "시리얼 포트 연결 실패:" << serialPort->errorString();
        emit errorOccurred(serialPort->errorString());
    }
    
    return connected;
}

void SerialCommunication::disconnectPort()
{
    if (serialPort && serialPort->isOpen()) {
        serialPort->close();
        qDebug() << "시리얼 포트 연결 해제됨";
        emit connectionStatusChanged(false);
    }
}

bool SerialCommunication::isConnected() const
{
    return serialPort && serialPort->isOpen();
}

void SerialCommunication::tryAutoConnect()
{
    ConfigManager* config = ConfigManager::instance();
    QString savedPort = config->getSerialPort();
    int savedBaudRate = config->getSerialBaudRate();
    
    // 저장된 포트가 있고, "사용 가능한 포트 없음"이 아닌 경우
    if (!savedPort.isEmpty() && savedPort != "사용 가능한 포트 없음") {
        // 사용 가능한 포트 목록 가져오기
        QStringList availablePorts = getAvailableSerialPorts();
        
        // 저장된 포트가 사용 가능한지 확인
        for (const QString& port : availablePorts) {
            if (port.startsWith(savedPort) || port.contains(savedPort)) {
                // 자동 연결 시도
                connectToPort(savedPort, savedBaudRate);
                break;
            }
        }
    }
}

QStringList SerialCommunication::getAvailableSerialPorts()
{
    QStringList portNames;
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    
    for (const QSerialPortInfo &port : ports) {
        QString portName = port.portName();
        QString description = port.description();
        QString manufacturer = port.manufacturer();
        bool isSerialToUSB = false;
        
        // Windows: COM 포트 체크
        #ifdef Q_OS_WIN
        if (portName.startsWith("COM")) {
            // USB Serial 장치 확인
            if (description.contains("USB", Qt::CaseInsensitive) ||
                description.contains("Serial", Qt::CaseInsensitive) ||
                manufacturer.contains("FTDI", Qt::CaseInsensitive) ||
                manufacturer.contains("Prolific", Qt::CaseInsensitive) ||
                manufacturer.contains("CH340", Qt::CaseInsensitive) ||
                manufacturer.contains("CH341", Qt::CaseInsensitive) ||
                manufacturer.contains("CP210", Qt::CaseInsensitive)) {
                isSerialToUSB = true;
            }
        }
        #endif
        
        // Linux: ttyUSB, ttyACM 포트 체크
        #ifdef Q_OS_LINUX
        if (portName.startsWith("ttyUSB") || portName.startsWith("ttyACM")) {
            isSerialToUSB = true;
        }
        #endif
        
        // macOS: tty.usbserial, tty.usbmodem 포트 체크
        #ifdef Q_OS_MACOS
        if (portName.contains("tty.usbserial") || portName.contains("tty.usbmodem")) {
            isSerialToUSB = true;
        }
        #endif
        
        // 크로스 플랫폼: 일반적인 USB Serial 장치 확인
        if (!isSerialToUSB) {
            if (description.contains("USB", Qt::CaseInsensitive) ||
                description.contains("Serial", Qt::CaseInsensitive) ||
                manufacturer.contains("FTDI", Qt::CaseInsensitive) ||
                manufacturer.contains("Prolific", Qt::CaseInsensitive) ||
                manufacturer.contains("Silicon Labs", Qt::CaseInsensitive) ||
                manufacturer.contains("WCH", Qt::CaseInsensitive)) {
                isSerialToUSB = true;
            }
        }
        
        if (isSerialToUSB) {
            QString displayName = QString("%1 (%2)").arg(portName, description.isEmpty() ? manufacturer : description);
            portNames << displayName;
            qDebug() << "[Serial] 사용 가능한 포트:" << displayName;
        }
    }
    
    return portNames;
}

bool SerialCommunication::autoConnectToAvailablePort(int baudRate)
{
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    
    qDebug() << "[Serial] 시리얼 포트 자동 연결 시도...";
    
    // 우선순위 순서대로 연결 시도
    QStringList priorityKeywords = {"FTDI", "CH340", "CH341", "CP210"};
    
    // 우선순위 장치 먼저 시도
    for (const QString& keyword : priorityKeywords) {
        for (const QSerialPortInfo &port : ports) {
            QString description = port.description();
            QString manufacturer = port.manufacturer();
            QString portName = port.portName();
            
            if (portName.contains(keyword, Qt::CaseInsensitive) ||
                description.contains(keyword, Qt::CaseInsensitive) ||
                manufacturer.contains(keyword, Qt::CaseInsensitive)) {
                
                qDebug() << "[Serial] 우선순위 포트 시도:" << portName << "(" << description << ")";
                
                if (connectToPort(portName, baudRate)) {
                    qDebug() << "[Serial] 자동 연결 성공:" << portName;
                    return true;
                }
            }
        }
    }
    
    // 우선순위 장치가 없으면 일반 USB Serial 장치 시도
    for (const QSerialPortInfo &port : ports) {
        QString portName = port.portName();
        QString description = port.description();
        
        bool isSerialToUSB = false;
        
        #ifdef Q_OS_WIN
        if (portName.startsWith("COM") && description.contains("USB", Qt::CaseInsensitive)) {
            isSerialToUSB = true;
        }
        #endif
        
        #ifdef Q_OS_LINUX
        if (portName.startsWith("ttyUSB") || portName.startsWith("ttyACM")) {
            isSerialToUSB = true;
        }
        #endif
        
        #ifdef Q_OS_MACOS
        if (portName.contains("tty.usbserial") || portName.contains("tty.usbmodem")) {
            isSerialToUSB = true;
        }
        #endif
        
        if (isSerialToUSB) {
            qDebug() << "[Serial] 일반 USB Serial 포트 시도:" << portName;
            
            if (connectToPort(portName, baudRate)) {
                qDebug() << "[Serial] 자동 연결 성공:" << portName;
                return true;
            }
        }
    }
    
    qDebug() << "[Serial] 사용 가능한 USB Serial 장치를 찾을 수 없습니다.";
    return false;
}

void SerialCommunication::sendResponse(const QString &response)
{
    if (serialPort && serialPort->isOpen()) {
        QByteArray data = response.toUtf8() + "\r\n";
        serialPort->write(data);
        qDebug() << "응답 전송:" << response;
    }
}

void SerialCommunication::setTeachingWidget(TeachingWidget *widget)
{
    teachingWidget = widget;
}

void SerialCommunication::readSerialData()
{
    if (!serialPort) return;
    
    QByteArray data = serialPort->readAll();
    QString command = QString::fromUtf8(data).trimmed();
    
    if (!command.isEmpty()) {
        qDebug() << "수신된 명령:" << command;
        emit commandReceived(command);
        processCommand(command);
    }
}

void SerialCommunication::handleSerialError(QSerialPort::SerialPortError error)
{
    if (error != QSerialPort::NoError) {
        QString errorString = serialPort->errorString();
        qDebug() << "시리얼 포트 에러:" << errorString;
        emit errorOccurred(errorString);
    }
}

void SerialCommunication::processCommand(const QString &command)
{
    qDebug() << "명령 처리 시작:" << command;
    
    // 단순히 숫자만 받아서 처리 (0, 1, 2 등)
    bool ok;
    int cameraNumber = command.toInt(&ok);
    
    if (ok && cameraNumber >= 0) {
        qDebug() << cameraNumber << "번 카메라 검사 명령 처리";
        performInspection(cameraNumber);
    } else {
        qDebug() << "잘못된 카메라 번호:" << command;
        sendResponse("ERROR");
    }
    
    qDebug() << "명령 처리 완료:" << command;
}

void SerialCommunication::performInspection(int cameraNumber)
{
    if (!teachingWidget) {
        qDebug() << "TeachingWidget이 설정되지 않음";
        sendResponse("ERROR");
        return;
    }
    
    try {
        qDebug() << "카메라" << cameraNumber << "번 검사 수행 중...";
        
        InspectionResult result = teachingWidget->runSingleInspection(cameraNumber);
        
        qDebug() << "검사 결과 받음 - isPassed:" << result.isPassed;
        
        // 검사 결과에 따라 응답 전송
        QString response;
        if (result.isPassed) {
            response = "PASS";
        } else {
            response = "FAIL";
        }
        
        qDebug() << "최종 응답:" << response;
        sendResponse(response);
        
        qDebug() << "검사 완료";
        emit inspectionCompleted(cameraNumber, response);
        
    } catch (const std::exception& e) {
    
    }
}
