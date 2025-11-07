#include <QApplication>
#include <QDebug>
#include <QTimer>
#include <QFile>
#include "TeachingWidget.h"
#include "SerialCommunication.h"
#include "ConfigManager.h"

// 전역 로그 객체를 위한 포인터
QObject* globalLogReceiver = nullptr;

// 자동 시리얼 연결 함수
void tryAutoConnectSerial(SerialCommunication* serialComm) {
    if (!serialComm) return;
    
    ConfigManager* config = ConfigManager::instance();
    QString savedPort = config->getSerialPort();
    int savedBaudRate = config->getSerialBaudRate();
    
    // 저장된 포트가 있고, "사용 가능한 포트 없음"이 아닌 경우
    if (!savedPort.isEmpty() && savedPort != "사용 가능한 포트 없음") {
        qDebug() << "[Auto Connect] 저장된 시리얼 설정 확인됨:" << savedPort << "@" << savedBaudRate;
        
        // 사용 가능한 포트 목록 가져오기
        QStringList availablePorts = serialComm->getAvailableSerialPorts();
        
        // 저장된 포트가 사용 가능한지 확인
        bool portFound = false;
        for (const QString& port : availablePorts) {
            if (port.startsWith(savedPort) || port.contains(savedPort)) {
                portFound = true;
                qDebug() << "[Auto Connect] 저장된 포트 발견됨:" << port;
                
                // 자동 연결 시도
                if (serialComm->connectToPort(savedPort, savedBaudRate)) {
                    qDebug() << "[Auto Connect] 자동 연결 성공!" << savedPort << "@" << savedBaudRate;
                } else {
                    qDebug() << "[Auto Connect] 자동 연결 실패:" << savedPort;
                }
                break;
            }
        }
        
        if (!portFound) {
            qDebug() << "[Auto Connect] 저장된 포트를 찾을 수 없습니다:" << savedPort;
            qDebug() << "[Auto Connect] 사용 가능한 포트:" << availablePorts;
        }
    } else {
        qDebug() << "[Auto Connect] 저장된 시리얼 설정이 없습니다. 수동으로 연결하세요.";
    }
}

// Qt 메시지 핸들러 (모든 QDebug 출력을 가로챔)
void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    // 원래 콘솔 출력은 유지
    fprintf(stderr, "%s\n", qPrintable(msg));
    
    // 로그 창으로도 전달
    if (globalLogReceiver) {
        QMetaObject::invokeMethod(globalLogReceiver, "receiveLogMessage",
            Qt::QueuedConnection,
            Q_ARG(QString, msg));
    }
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // 메시지 핸들러 설치
    qInstallMessageHandler(messageHandler);
    
    // 티칭 위젯 생성
    TeachingWidget widget(0, "카메라 1");
    
    // 초기 윈도우 크기 설정 (로그 뷰어 접혀있는 상태 기준)
    widget.resize(1200, 700);
    
    widget.show();
    
    // 글로벌 로그 리시버 설정
    globalLogReceiver = widget.getLogViewer();
    
    // 시리얼 통신 객체 생성 및 설정
    SerialCommunication* serialComm = new SerialCommunication(&app);
    serialComm->setTeachingWidget(&widget);
    
    // TeachingWidget에 시리얼 통신 객체 설정
    widget.setSerialCommunication(serialComm);
    
    // 시리얼 이벤트 연결
    QObject::connect(serialComm, &SerialCommunication::commandReceived, [](const QString& command) {
        qDebug() << "[Serial] 명령 수신됨:" << command;
    });
    
    QObject::connect(serialComm, &SerialCommunication::inspectionCompleted, 
                    [](int cameraNumber, const QString& result) {
        qDebug() << "[Serial] 카메라" << cameraNumber << "검사 완료:" << result;
    });
    
    QObject::connect(serialComm, &SerialCommunication::connectionStatusChanged, 
                    [](bool connected) {
        if (connected) {
            qDebug() << "[Serial] 시리얼 포트 연결됨 - 명령 대기 중...";
        } else {
            qDebug() << "[Serial] 시리얼 포트 연결 해제됨";
        }
    });
    
    QObject::connect(serialComm, &SerialCommunication::errorOccurred, [](const QString& error) {
        qDebug() << "[Serial] 에러:" << error;
    });
    
    qDebug() << "[Serial] 시리얼 통신 준비됨.";
    
    // 자동 연결 시도 (약간의 지연 후)
    QTimer::singleShot(1000, [serialComm]() {
        tryAutoConnectSerial(serialComm);
    });
    
    return app.exec();
}