#include <QApplication>
#include <QDebug>
#include <QTimer>
#include <QFile>
#include <QDateTime>
#include "TeachingWidget.h"
#include "SerialCommunication.h"
#include "ConfigManager.h"

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

// 전역 메시지 핸들러 (qDebug를 오버레이 로그로 리다이렉트)
TeachingWidget* g_teachingWidget = nullptr;

void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    if (g_teachingWidget) {
        // 타임스탬프 추가
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        QString formattedMessage = QString("\"%1\" - \"%2\"").arg(timestamp).arg(msg);
        g_teachingWidget->receiveLogMessage(formattedMessage);
    }
    
    // 콘솔 출력도 유지 (디버깅용)
    QByteArray localMsg = msg.toLocal8Bit();
    switch (type) {
        case QtDebugMsg:
            fprintf(stderr, "%s\n", localMsg.constData());
            break;
        case QtWarningMsg:
            fprintf(stderr, "Warning: %s\n", localMsg.constData());
            break;
        case QtCriticalMsg:
            fprintf(stderr, "Critical: %s\n", localMsg.constData());
            break;
        case QtFatalMsg:
            fprintf(stderr, "Fatal: %s\n", localMsg.constData());
            abort();
        case QtInfoMsg:
            fprintf(stderr, "Info: %s\n", localMsg.constData());
            break;
    }
}

int main(int argc, char *argv[]) {
    // OS별 Qt 플랫폼 플러그인 설정
#ifdef Q_OS_LINUX
    // Linux(Ubuntu)에서는 X11 사용
    qputenv("QT_QPA_PLATFORM", "xcb");
#endif
    // macOS에서는 기본 cocoa 플러그인 사용 (명시적 설정 불필요)
    
    QApplication app(argc, argv);
    
    // 티칭 위젯 생성
    TeachingWidget widget(0, "카메라 1");
    g_teachingWidget = &widget;
    
    // 커스텀 메시지 핸들러 설치
    qInstallMessageHandler(customMessageHandler);
    
    // 윈도우 타이틀 설정
    widget.setWindowTitle("KM Inspector");
    
    // 최대화 모드로 시작 (타이틀바 유지)
    widget.showMaximized();
    
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