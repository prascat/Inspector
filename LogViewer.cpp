#include "LogViewer.h"
#include "CommonDefs.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QTextStream>
#include <QFileDialog>
#include <QDebug>
#include <QTextCharFormat>
#include <QScrollBar>
#include <QDir>
#include <QDateTime>
#include <QCoreApplication>

LogViewer::LogViewer(QWidget *parent) : QWidget(parent) {
    setWindowTitle(TR("INSPECTION_LOG"));
    
    // 로그 파일 초기화
    openLogFile();
    
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    // 헤더: 접기/펼치기 버튼
    QHBoxLayout *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(5, 5, 5, 5);
    
    collapseButton = new QPushButton("▶ INSPECTION LOG", this);
    collapseButton->setFlat(true);
    collapseButton->setStyleSheet(
        "QPushButton {"
        "  text-align: left;"
        "  padding: 5px;"
        "  background-color: #1E1E1E;"
        "  color: #FFFFFF;"
        "  border: none;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #2B2B2B;"
        "}"
    );
    connect(collapseButton, &QPushButton::clicked, this, &LogViewer::toggleCollapse);
    
    headerLayout->addWidget(collapseButton);
    headerLayout->addStretch();
    
    layout->addLayout(headerLayout);
    
    logTextEdit = new QTextEdit(this);
    logTextEdit->setReadOnly(true);
    logTextEdit->setLineWrapMode(QTextEdit::NoWrap);
    logTextEdit->setFont(QFont("Courier New", 14));
    logTextEdit->setVisible(false);  // 초기에는 숨김
    
    // 어두운 배경색으로 설정하여 색상 텍스트가 잘 보이도록
    logTextEdit->setStyleSheet(
        "QTextEdit {"
        "  background-color: #2B2B2B;"  // 어두운 회색 배경
        "  color: #FFFFFF;"             // 기본 흰색 텍스트
        "  border: none;"
        "}"
    );
    layout->addWidget(logTextEdit);
    
    // 초기 상태: 접혀있음
    setMaximumHeight(COLLAPSED_HEIGHT);
    setMinimumHeight(COLLAPSED_HEIGHT);
    m_isCollapsed = true;
}

void LogViewer::appendLog(const QString& text) {
    if (!text.trimmed().isEmpty()) {
        // 현재 커서를 끝으로 이동
        QTextCursor cursor = logTextEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        logTextEdit->setTextCursor(cursor);
        
        // 텍스트 색상 결정
        QTextCharFormat format;
        
        // 종합적인 검사 결과 우선 처리
        if (text.contains("검사 완료:")) {
            if (text.contains("합격")) {
                format.setForeground(QColor("#4CAF50")); // 초록색
                format.setFontWeight(QFont::Bold);
            } else if (text.contains("불합격")) {
                format.setForeground(QColor("#F44336")); // 빨간색
                format.setFontWeight(QFont::Bold);
            }
        }
        else if (text.contains("전체 검사 결과:")) {
            if (text.contains("합격")) {
                format.setForeground(QColor("#4CAF50")); // 초록색
                format.setFontWeight(QFont::Bold);
            } else if (text.contains("불합격")) {
                format.setForeground(QColor("#F44336")); // 빨간색
                format.setFontWeight(QFont::Bold);
            }
        }
        else if (text.contains("검사 시작") || text.contains("검사 종료")) {
            format.setForeground(QColor("#2196F3")); // 파란색
            format.setFontWeight(QFont::Bold);
        }
        // 개별 패턴 결과 - 불합격을 먼저 확인
        else if (text.contains("불합격") || text.contains("FAIL") || text.contains("실패")) {
            format.setForeground(QColor("#E57373")); // 연한 빨간색
        }
        else if (text.contains("합격") || text.contains("PASS")) {
            format.setForeground(QColor("#81C784")); // 연한 초록색
        }
        else if (text.contains("FID") || text.contains("INS")) {
            format.setForeground(QColor("#FF9800")); // 주황색
        }
        else if (text.contains("템플릿") || text.contains("색상") || text.contains("이진화") || text.contains("엣지")) {
            format.setForeground(QColor("#00BCD4")); // 청록색
        }
        else if (text.contains("점수:") || text.contains("임계값:")) {
            format.setForeground(QColor("#90A4AE")); // 연한 회색
        }
        else if (text.contains("마스크") && text.contains("→")) {
            format.setForeground(QColor("#FFA726")); // 연한 주황색 (마스크 처리)
        }
        else {
            format.setForeground(QColor("#FFFFFF")); // 기본 흰색
        }
        
        // 타임스탬프가 있는 경우 분리해서 색상 적용
        if (text.contains("] \"") && text.contains("\" - \"")) {
            // InsProcessor 로그 형식: [InsProcessor] "2025-05-28 07:10:40.307" - "메시지"
            QStringList parts = text.split("\" - \"");
            if (parts.size() >= 2) {
                // 첫 번째 부분 (타임스탬프 포함)
                QTextCharFormat timestampFormat;
                timestampFormat.setForeground(QColor("#9E9E9E")); // 회색
                cursor.insertText(parts[0] + "\" - \"", timestampFormat);
                
                // 두 번째 부분 (실제 메시지)
                QString message = parts[1];
                if (message.endsWith("\"")) {
                    message.chop(1); // 마지막 따옴표 제거
                }
                
                // 메시지 부분에 대해 다시 색상 판정
                QTextCharFormat messageFormat = format;
                if (message.contains("검사 완료:") || message.contains("전체 검사 결과:")) {
                    if (message.contains("합격")) {
                        messageFormat.setForeground(QColor("#4CAF50")); // 진한 초록색
                        messageFormat.setFontWeight(QFont::Bold);
                    } else if (message.contains("불합격")) {
                        messageFormat.setForeground(QColor("#F44336")); // 진한 빨간색
                        messageFormat.setFontWeight(QFont::Bold);
                    }
                }
                
                cursor.insertText(message, messageFormat);
            } else {
                cursor.insertText(text, format);
            }
        } else {
            cursor.insertText(text, format);
        }
        
        // 줄바꿈 추가
        cursor.insertText("\n");
        
        // **라인 수 카운터 증가**
        currentLineCount++;
        
        // **일정 간격으로 로그 정리 체크**
        if (currentLineCount % CHECK_INTERVAL == 0) {
            trimLogIfNeeded();
        }
        
        // 자동 스크롤
        logTextEdit->ensureCursorVisible();
    }
}

// **새 함수 구현 - 로그 자동 정리**
void LogViewer::trimLogIfNeeded() {
    if (currentLineCount <= MAX_LOG_LINES) {
        return; // 아직 한계에 도달하지 않음
    }
    
    // 현재 스크롤 위치 기억 (사용자가 위쪽을 보고 있을 수 있음)
    QScrollBar* vScrollBar = logTextEdit->verticalScrollBar();
    bool wasAtBottom = (vScrollBar->value() == vScrollBar->maximum());
    
    // 텍스트 전체 가져오기
    QString fullText = logTextEdit->toPlainText();
    QStringList lines = fullText.split('\n');
    
    if (lines.size() > MAX_LOG_LINES) {
        // 오래된 라인들 제거하고 최근 라인들만 유지
        int linesToRemove = lines.size() - TRIM_TO_LINES;
        QStringList recentLines = lines.mid(linesToRemove);
        
        // **색상 정보를 유지하면서 텍스트 재구성**
        logTextEdit->clear();
        
        // 정리 안내 메시지 추가 (회색으로)
        QTextCursor cursor = logTextEdit->textCursor();
        QTextCharFormat headerFormat;
        headerFormat.setForeground(QColor("#9E9E9E"));
        headerFormat.setFontWeight(QFont::Bold);
        cursor.insertText(QString("=== 로그 정리됨: %1줄 삭제, 최근 %2줄 유지 ===\n")
                         .arg(linesToRemove).arg(recentLines.size()), headerFormat);
        
        // 최근 로그들을 다시 추가 (색상 정보는 잃어버리지만 성능상 필요)
        QTextCharFormat normalFormat;
        normalFormat.setForeground(QColor("#FFFFFF"));
        for (const QString& line : recentLines) {
            if (!line.isEmpty()) {
                cursor.insertText(line + "\n", normalFormat);
            }
        }
        
        // 라인 카운터 업데이트
        currentLineCount = recentLines.size() + 1; // 헤더 라인 포함
        
    }
    
    // 스크롤 위치 복원 (사용자가 맨 아래에 있었다면 맨 아래로)
    if (wasAtBottom) {
        logTextEdit->ensureCursorVisible();
    }
}

// 이 메서드를 통해서만 로그 메시지 처리
void LogViewer::receiveLogMessage(const QString& message) {
    appendLog(message);
    writeToLogFile(message);
}

void LogViewer::openLogFile() {
    // 실행 파일 경로 가져오기
    QString appPath = QCoreApplication::applicationDirPath();
    
    // logs 폴더 경로
    QString logsDir = appPath + "/logs";
    
    // logs 폴더가 없으면 생성
    QDir dir;
    if (!dir.exists(logsDir)) {
        dir.mkpath(logsDir);
    }
    
    // 현재 날짜로 파일명 생성 (예: 2025-11-12.log)
    QString currentDate = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    currentLogFilePath = logsDir + "/" + currentDate + ".log";
    
    // 기존 파일이 열려있으면 닫기
    if (logFile) {
        if (logStream) {
            delete logStream;
            logStream = nullptr;
        }
        logFile->close();
        delete logFile;
        logFile = nullptr;
    }
    
    // 새 로그 파일 열기 (append 모드)
    logFile = new QFile(currentLogFilePath);
    if (logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        logStream = new QTextStream(logFile);
        logStream->setEncoding(QStringConverter::Utf8);
        qDebug() << "로그 파일 열림:" << currentLogFilePath;
    } else {
        qDebug() << "로그 파일 열기 실패:" << currentLogFilePath;
        delete logFile;
        logFile = nullptr;
    }
}

void LogViewer::writeToLogFile(const QString& message) {
    if (!logStream) {
        return;
    }
    
    // 날짜가 바뀌었는지 확인
    QString currentDate = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QString expectedFilePath = QCoreApplication::applicationDirPath() + "/logs/" + currentDate + ".log";
    
    // 날짜가 바뀌었으면 새 파일 열기
    if (currentLogFilePath != expectedFilePath) {
        openLogFile();
        if (!logStream) {
            return;
        }
    }
    
    // 로그 파일에 기록
    *logStream << message << "\n";
    logStream->flush();
}

void LogViewer::toggleCollapse() {
    setCollapsed(!m_isCollapsed);
}

void LogViewer::setCollapsed(bool collapsed) {
    m_isCollapsed = collapsed;
    logTextEdit->setVisible(!collapsed);
    
    if (collapsed) {
        // 접힐 때: 높이를 최소로
        setMaximumHeight(COLLAPSED_HEIGHT);
        setMinimumHeight(COLLAPSED_HEIGHT);
    } else {
        // 펼칠 때: 높이를 원래대로
        setMaximumHeight(QWIDGETSIZE_MAX);
        setMinimumHeight(EXPANDED_HEIGHT);
    }
    
    updateCollapseButton();
    
    // 부모 윈도우 크기 조정 신호 발출
    emit collapseStateChanged(collapsed);
}

void LogViewer::updateCollapseButton() {
    if (m_isCollapsed) {
        collapseButton->setText("▶ INSPECTION LOG");
    } else {
        collapseButton->setText("▼ INSPECTION LOG");
    }
}