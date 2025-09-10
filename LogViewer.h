#ifndef LOGVIEWER_H
#define LOGVIEWER_H

#include <QDialog>
#include <QTextEdit>
#include <QPushButton>
#include "LanguageManager.h"

class LogViewer : public QDialog
{
    Q_OBJECT

public:
    explicit LogViewer(QWidget *parent = nullptr);
    void appendLog(const QString& text);

public slots:
    void receiveLogMessage(const QString& message);

private slots:
    void saveLog();
    void updateUITexts();

private:
    void trimLogIfNeeded();  // **새 함수 추가**
    
    QTextEdit *logTextEdit;
    QPushButton *clearButton;
    QPushButton *saveButton;
    
    // **로그 자동 관리를 위한 설정값들**
    static const int MAX_LOG_LINES = 1000;      // 최대 라인 수
    static const int TRIM_TO_LINES = 800;       // 삭제 후 남길 라인 수
    static const int CHECK_INTERVAL = 50;       // 체크 간격 (매 50줄마다)
    int currentLineCount = 0;                    // 현재 라인 수 카운터
};

#endif // LOGVIEWER_H