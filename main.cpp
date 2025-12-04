#include <QApplication>
#include <QPalette>
#include <QDebug>
#include <QFile>
#include <QDateTime>
#include <QVector>
#include "TeachingWidget.h"

// 전역 메시지 핸들러 (qDebug를 오버레이 로그로 리다이렉트)
TeachingWidget* g_teachingWidget = nullptr;
QVector<QString> g_pendingLogMessages;  // 초기 로그 버퍼

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
    
    // 티칭 위젯 생성
    TeachingWidget widget(0, "카메라 1");
    g_teachingWidget = &widget;
    
    // 버퍼에 저장된 초기 로그를 위젯에 전달
    flushPendingLogs();
    
    // 윈도우 타이틀 설정
    widget.setWindowTitle("KM Inspector");
    
    // 프레임리스 윈도우로 설정 (타이틀바 제거)
    widget.setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    
    // 최대화 모드로 시작
    widget.showMaximized();
    
    return app.exec();
}