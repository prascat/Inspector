#ifndef SERIALCOMMUNICATION_H
#define SERIALCOMMUNICATION_H

#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QString>
#include <QDebug>
#include <QStringList>

class TeachingWidget; // Forward declaration

class SerialCommunication : public QObject
{
    Q_OBJECT

public:
    explicit SerialCommunication(QObject *parent = nullptr);
    ~SerialCommunication();

    // 시리얼 포트 연결/해제
    bool connectToPort(const QString &portName, int baudRate = 9600);
    bool autoConnectToAvailablePort(int baudRate = 9600);
    void tryAutoConnect();  // 저장된 설정으로 자동 연결 시도
    QStringList getAvailableSerialPorts();
    void disconnectPort();
    bool isConnected() const;

    // 데이터 전송
    void sendResponse(const QString &response);
    void sendRawData(const QByteArray &data);  // Raw 바이트 전송
    void sendInspectionResult(int frameIndex, bool isPassed);  // 4바이트 검사 결과 전송

    // TeachingWidget 설정
    void setTeachingWidget(TeachingWidget *teachingWidget);

private slots:
    void readSerialData();
    void handleSerialError(QSerialPort::SerialPortError error);

public slots:
    // 명령 처리 (테스트용 public 슬롯)
    void processCommand(const QString &command);

private:
    QSerialPort *serialPort;
    TeachingWidget *teachingWidget;
    
    // 검사 수행
    void performInspection(int cameraNumber);

signals:
    void connectionStatusChanged(bool connected);
    void commandReceived(const QString &command);
    void inspectionCompleted(int cameraNumber, const QString &result);
    void errorOccurred(const QString &error);
};

#endif // SERIALCOMMUNICATION_H
