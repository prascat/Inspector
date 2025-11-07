#ifndef LOGVIEWER_H
#define LOGVIEWER_H

#include <QWidget>
#include <QTextEdit>
#include <QPushButton>
#include "LanguageManager.h"

class LogViewer : public QWidget
{
    Q_OBJECT

public:
    explicit LogViewer(QWidget *parent = nullptr);
    void appendLog(const QString& text);
    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_isCollapsed; }

public slots:
    void receiveLogMessage(const QString& message);

signals:
    void collapseStateChanged(bool collapsed);

private slots:
    void toggleCollapse();

private:
    void trimLogIfNeeded();
    void updateCollapseButton();
    
    QTextEdit *logTextEdit;
    QPushButton *collapseButton;
    bool m_isCollapsed = false;
    
    // 높이 설정 상수
    static const int COLLAPSED_HEIGHT = 35;      // 접혔을 때 높이 (버튼만)
    static const int EXPANDED_HEIGHT = 150;      // 펼쳤을 때 높이
    
    // **로그 자동 관리를 위한 설정값들**
    static const int MAX_LOG_LINES = 1000;      // 최대 라인 수
    static const int TRIM_TO_LINES = 800;       // 삭제 후 남길 라인 수
    static const int CHECK_INTERVAL = 50;       // 체크 간격 (매 50줄마다)
    int currentLineCount = 0;                    // 현재 라인 수 카운터
};

#endif // LOGVIEWER_H