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
        qDebug() << "[Serial] Port connected:" << portName << "at" << baudRate << "baud";
        emit connectionStatusChanged(true);
    } else {
        qDebug() << "[Serial] Connection failed:" << serialPort->errorString();
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
        
        // Linux: ttyUSB, ttyACM, ttyTHS (Jetson), ttyS 포트 체크
        #ifdef Q_OS_LINUX
        if (portName.startsWith("ttyUSB") || 
            portName.startsWith("ttyACM") ||
            portName.startsWith("ttyTHS") ||  // Jetson UART
            portName.startsWith("ttyS")) {    // 일반 시리얼 포트
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
            QString info;
            if (!description.isEmpty()) {
                info = description;
            } else if (!manufacturer.isEmpty()) {
                info = manufacturer;
            } else {
                // 내장 UART 포트인 경우 적절한 설명 추가
                if (portName.startsWith("ttyTHS")) {
                    info = "Jetson 내장 UART";
                } else if (portName.startsWith("ttyS")) {
                    info = "시리얼 포트";
                } else {
                    info = "시리얼 장치";
                }
            }
            
            QString displayName = QString("%1 (%2)").arg(portName, info);
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
                    qDebug() << "[Serial] Auto-connect success:" << portName;
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
    }
}

void SerialCommunication::sendRawData(const QByteArray &data)
{
    if (serialPort && serialPort->isOpen()) {
        qint64 bytesWritten = serialPort->write(data);
        serialPort->flush();
        qDebug() << "[Serial] Raw data sent:" << bytesWritten << "bytes";
    }
}

void SerialCommunication::sendInspectionResult(int frameIndex, bool isPassed)
{
    if (serialPort && serialPort->isOpen()) {
        QByteArray data;
        data.append(static_cast<char>(0xFF));           // 시작 바이트
        data.append(static_cast<char>(frameIndex + 1)); // 프레임 번호 (0~3 → 1~4)
        data.append(static_cast<char>(isPassed ? 0x00 : 0x01)); // 검사 결과 (PASS:0x00, NG:0x01)
        data.append(static_cast<char>(0xEF));           // 종료 바이트
        
        qint64 bytesWritten = serialPort->write(data);
        serialPort->flush();  // 버퍼 즉시 전송
        
        qDebug().noquote() << QString("[Serial] Result sent: Frame[%1] %2 (%3) - %4 bytes written")
            .arg(frameIndex)
            .arg(isPassed ? "PASS" : "NG")
            .arg(QString(data.toHex(' ')))
            .arg(bytesWritten);
    } else {
        qWarning() << "[시리얼통신] 포트가 열려있지 않음 - 전송 실패";
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
    
    // 2바이트 명령 처리
    if (data.size() >= 2) {
        unsigned char byte1 = static_cast<unsigned char>(data[0]);
        unsigned char byte2 = static_cast<unsigned char>(data[1]);
        
        // 검증: byte1 XOR byte2 = 0xFF
        if ((byte1 ^ byte2) == 0xFF) {
            // 명령 파싱
            int frameIndex = -1;
            if (byte1 == 0x01 && byte2 == 0xFE) {
                frameIndex = 0;
            } else if (byte1 == 0x02 && byte2 == 0xFD) {
                frameIndex = 1;
            } else if (byte1 == 0x03 && byte2 == 0xFC) {
                frameIndex = 2;
            } else if (byte1 == 0x04 && byte2 == 0xFB) {
                frameIndex = 3;
            }
            
            if (frameIndex >= 0) {
                qDebug().noquote() << QString("[Serial] Inspect request: Frame[%1] (0x%2 0x%3)")
                    .arg(frameIndex)
                    .arg(byte1, 2, 16, QChar('0'))
                    .arg(byte2, 2, 16, QChar('0'));
                
                QString command = QString::number(frameIndex);
                emit commandReceived(command);
                processCommand(command);
                return;
            }
        }
        
        qDebug() << "잘못된 시리얼 명령 형식:" << data.toHex();
    }
    
    // 기존 텍스트 명령 처리 (호환성)
    QString command = QString::fromUtf8(data).trimmed();
    if (!command.isEmpty()) {
        qDebug() << "수신된 텍스트 명령:" << command;
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
    // 프레임 인덱스로 받음 (0, 1, 2, 3)
    bool ok;
    int frameIndex = command.toInt(&ok);
    
    if (ok && frameIndex >= 0 && frameIndex < 4) {
        if (!teachingWidget) {
            qDebug() << "[시리얼통신] ERROR: TeachingWidget이 설정되지 않음";
            return;
        }
        
        // 서버 메시지처럼 처리: nextFrameIndex 설정
        // frameIndex: 0,1,2,3 -> 카메라0(0,1), 카메라1(2,3)
        int cameraNumber = frameIndex / 2;  // 0,1 -> 0번 카메라, 2,3 -> 1번 카메라
        
        // TeachingWidget의 nextFrameIndex 설정 (서버 요청과 동일)
        teachingWidget->setNextFrameIndex(cameraNumber, frameIndex);
        
        // 검사는 TeachingWidget의 타이머나 트리거에서 자동 처리됨
        // ACK 응답 제거 - 검사 완료 후 4바이트로 결과 전송
        
    } else {
        qDebug() << "[시리얼통신] ERROR: 잘못된 프레임 인덱스" << command;
        // ERROR 응답 제거
    }
}

void SerialCommunication::performInspection(int cameraNumber)
{
    if (!teachingWidget) {
        qDebug() << "TeachingWidget이 설정되지 않음";
        return;
    }
    
    try {
        qDebug() << "카메라" << cameraNumber << "번 검사 수행 중...";
        
        InspectionResult result = teachingWidget->runSingleInspection(cameraNumber);
        
        qDebug() << "검사 결과 받음 - isPassed:" << result.isPassed;
        
        // 텍스트 응답 제거 - 검사 완료 후 4바이트로 자동 전송됨
        
        qDebug() << "검사 완료";
        emit inspectionCompleted(cameraNumber, result.isPassed ? "PASS" : "FAIL");
        
    } catch (const std::exception& e) {
    
    }
}
