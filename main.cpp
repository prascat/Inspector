#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QDateTime>
#include "TeachingWidget.h"

// 전역 메시지 핸들러 (qDebug를 오버레이 로그로 리다이렉트)
TeachingWidget* g_teachingWidget = nullptr;

void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    if (g_teachingWidget) {
        // 타임스탬프 추가
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        QString formattedMessage = QString("%1 - %2").arg(timestamp).arg(msg);
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
    
    return app.exec();
}