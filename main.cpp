#include <QApplication>
#include <QPalette>
#include <QDebug>
#include <QFile>
#include <QDateTime>
#include <QVector>
#include <QScreen>
#include <unistd.h>  // _exit()
#include <signal.h>  // 시그널 핸들러
#include <csignal>
#include "TeachingWidget.h"
#include "CustomMessageBox.h"
#include "Spinnaker.h"

// 전역 메시지 핸들러 (qDebug를 오버레이 로그로 리다이렉트)
TeachingWidget* g_teachingWidget = nullptr;
QVector<QString> g_pendingLogMessages;  // 초기 로그 버퍼

// Spinnaker System 정리 함수
void cleanupSpinnaker() {
    static bool cleaned = false;
    if (cleaned) return;
    cleaned = true;
    
    qWarning() << "[Cleanup] Spinnaker System 정리 시작";
    try {
        // System 인스턴스 획득 시도 - 이미 정리되었을 수 있음
        Spinnaker::SystemPtr system = nullptr;
        try {
            system = Spinnaker::System::GetInstance();
        } catch (const Spinnaker::Exception& e) {
            // System 인스턴스를 가져올 수 없으면 이미 정리된 것
            qWarning() << "[Cleanup] Spinnaker System 이미 정리됨 또는 초기화 안됨";
            return;
        }
        
        if (system) {
            try {
                Spinnaker::CameraList camList = system->GetCameras();
                if (camList.GetSize() > 0) {
                    for (unsigned int i = 0; i < camList.GetSize(); i++) {
                        try {
                            Spinnaker::CameraPtr cam = camList.GetByIndex(i);
                            if (cam && cam->IsInitialized()) {
                                if (cam->IsStreaming()) {
                                    cam->EndAcquisition();
                                }
                                cam->DeInit();
                            }
                        } catch (...) {
                            // 개별 카메라 정리 실패는 무시
                        }
                    }
                    camList.Clear();
                }
            } catch (const Spinnaker::Exception& e) {
                qWarning() << "[Cleanup] 카메라 목록 처리 중 예외:" << e.what();
            }
            
            // System 인스턴스 해제
            try {
                system->ReleaseInstance();
                qWarning() << "[Cleanup] Spinnaker System 정리 완료";
            } catch (const Spinnaker::Exception& e) {
                qWarning() << "[Cleanup] System ReleaseInstance 실패:" << e.what();
            }
        }
    } catch (const Spinnaker::Exception& e) {
        qWarning() << "[Cleanup] Spinnaker 정리 중 예외:" << e.what();
    } catch (const std::exception& e) {
        qWarning() << "[Cleanup] 표준 예외:" << e.what();
    } catch (...) {
        qWarning() << "[Cleanup] Spinnaker 정리 중 알 수 없는 예외";
    }
}

// 시그널 핸들러 (비정상 종료 시 정리)
void signalHandler(int sig) {
    static volatile sig_atomic_t handling = 0;
    if (handling) {
        _exit(128 + sig);  // 재진입 방지
    }
    handling = 1;
    
    const char* signalName = "UNKNOWN";
    switch (sig) {
        case SIGINT:  signalName = "SIGINT"; break;
        case SIGTERM: signalName = "SIGTERM"; break;
        case SIGSEGV: signalName = "SIGSEGV"; break;
        case SIGABRT: signalName = "SIGABRT"; break;
    }
    
    fprintf(stderr, "\n[SignalHandler] 시그널 수신: %s (%d)\n", signalName, sig);
    fprintf(stderr, "[SignalHandler] 정리 작업 시작...\n");
    
    // Spinnaker 정리
    cleanupSpinnaker();
    
    // 설정 저장
    if (ConfigManager::instance()) {
        ConfigManager::instance()->saveConfig();
    }
    
    fprintf(stderr, "[SignalHandler] 정리 완료. 종료합니다.\n");
    fflush(stderr);
    
    // 원래 시그널 동작 복원 후 재발생
    signal(sig, SIG_DFL);
    raise(sig);
}

// 시그널 핸들러 등록
void setupSignalHandlers() {
    signal(SIGINT, signalHandler);   // Ctrl+C
    signal(SIGTERM, signalHandler);  // kill
    signal(SIGSEGV, signalHandler);  // Segmentation Fault
    signal(SIGABRT, signalHandler);  // abort()
}

void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    // 타임스탬프 추가
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString formattedMessage = QString("%1 - %2").arg(timestamp).arg(msg);
    
    if (g_teachingWidget) {
        g_teachingWidget->receiveLogMessage(formattedMessage);
    } else {
        // 위젯 생성 전이면 버퍼에 저장
        g_pendingLogMessages.append(formattedMessage);
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

// 버퍼에 저장된 초기 로그를 위젯에 전달
void flushPendingLogs() {
    if (g_teachingWidget) {
        for (const QString& msg : g_pendingLogMessages) {
            g_teachingWidget->receiveLogMessage(msg);
        }
        g_pendingLogMessages.clear();
    }
}

int main(int argc, char *argv[]) {
    // 시그널 핸들러 등록 (가장 먼저)
    setupSignalHandlers();
    
    // OS별 Qt 플랫폼 플러그인 설정
#ifdef Q_OS_LINUX
    // Linux(Ubuntu)에서는 X11 사용
    qputenv("QT_QPA_PLATFORM", "xcb");
#endif
    // macOS에서는 기본 cocoa 플러그인 사용 (명시적 설정 불필요)
    
    QApplication app(argc, argv);
    
    // 애플리케이션 전체 다크 테마 스타일 적용
    app.setStyle("Fusion");
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(42, 42, 42));
    darkPalette.setColor(QPalette::AlternateBase, QColor(66, 66, 66));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    app.setPalette(darkPalette);
    
    // 메뉴바와 상태바 스타일
    app.setStyleSheet(
        "QMenuBar { background-color: rgb(53, 53, 53); color: white; } "
        "QMenuBar::item { background-color: transparent; padding: 4px 8px; } "
        "QMenuBar::item:selected { background-color: rgb(42, 130, 218); } "
        "QMenuBar::item:pressed { background-color: rgb(30, 100, 180); } "
        "QMenu { background-color: rgb(53, 53, 53); color: white; border: 1px solid rgb(80, 80, 80); } "
        "QMenu::item:selected { background-color: rgb(42, 130, 218); } "
        "QStatusBar { background-color: rgb(53, 53, 53); color: white; } "
        "QToolTip { background-color: rgb(70, 70, 70); color: white; border: 1px solid rgb(100, 100, 100); } "
    );
    
    // 커스텀 메시지 핸들러 먼저 설치 (초기 로그도 캡처하기 위해)
    qInstallMessageHandler(customMessageHandler);
    
    // 티칭 위젯을 힙에 생성 (소멸자 호출을 건너뛰기 위해)
    TeachingWidget *widget = new TeachingWidget(0, "카메라 1");
    g_teachingWidget = widget;
    
    // 버퍼에 저장된 초기 로그를 위젯에 전달
    flushPendingLogs();
    
    // 윈도우 타이틀 설정
    widget->setWindowTitle("KM Inspector");
    
    // 프레임리스 윈도우로 설정 (타이틀바 제거)
    widget->setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    
    // 최대화 모드로 시작
    widget->showMaximized();
    
    int result = app.exec();
    
    // app.exec() 종료 후 빠른 정리
    qDebug() << "[main] 애플리케이션 종료 시작";
    
    // Spinnaker 정리
    cleanupSpinnaker();
    
    // 설정 저장 (명시적으로)
    ConfigManager::instance()->saveConfig();
    
    // QApplication 종료 전에 전역 포인터 정리
    g_teachingWidget = nullptr;
    qInstallMessageHandler(nullptr);
    
    qDebug() << "[main] 애플리케이션 정상 종료";
    fflush(stdout);
    fflush(stderr);
    
    // 빠른 종료 - 모든 객체 소멸자를 건너뛰고 프로세스 종료
    // widget은 삭제하지 않음 (소멸자에서 Spinnaker SDK, OpenVINO mutex 문제)
    // Spinnaker SDK, OpenVINO 등의 mutex 문제 회피
    _exit(result);
    
    // return result;  // 도달하지 않음
}