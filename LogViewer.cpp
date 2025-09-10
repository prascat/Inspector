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

LogViewer::LogViewer(QWidget *parent) : QDialog(parent) {
    setWindowTitle(TR("INSPECTION_LOG"));
    resize(800, 600);
    
    QVBoxLayout *layout = new QVBoxLayout(this);
    
    logTextEdit = new QTextEdit(this);
    logTextEdit->setReadOnly(true);
    logTextEdit->setLineWrapMode(QTextEdit::NoWrap);
    logTextEdit->setFont(QFont("Courier New", 14));
    
    // 어두운 배경색으로 설정하여 색상 텍스트가 잘 보이도록
    logTextEdit->setStyleSheet(
        "QTextEdit {"
        "  background-color: #2B2B2B;"  // 어두운 회색 배경
        "  color: #FFFFFF;"             // 기본 흰색 텍스트
        "  border: 1px solid #555555;"
        "}"
    );
    layout->addWidget(logTextEdit);
    
    // 하단 버튼 영역
    QHBoxLayout *btnLayout = new QHBoxLayout();
    
    // clearButton을 멤버 변수로 사용
    clearButton = new QPushButton(TR("CLEAR_LOG"), this);
    connect(clearButton, &QPushButton::clicked, [this]() {
        logTextEdit->clear();
        currentLineCount = 0;  // **라인 카운터 리셋**
    });
    
    // saveButton을 멤버 변수로 사용
    saveButton = new QPushButton(TR("SAVE_LOG"), this);
    connect(saveButton, &QPushButton::clicked, this, &LogViewer::saveLog);
    
    btnLayout->addWidget(clearButton);
    btnLayout->addWidget(saveButton);
    btnLayout->addStretch(1);
    
    layout->addLayout(btnLayout);

    connect(LanguageManager::instance(), &LanguageManager::languageChanged, this, &LogViewer::updateUITexts);
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
        else if (text.contains("검사 시작")) {
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
}

void LogViewer::saveLog() {
    QString fileName = QFileDialog::getSaveFileName(
        this, 
        TR("SAVE_LOG"), 
        QString(), 
        TR("TEXT_FILES") + " (*.txt)"
    );
    
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << logTextEdit->toPlainText(); // 색상 정보 없이 평문으로 저장
            file.close();
        }
    }
}

void LogViewer::updateUITexts() {
    setWindowTitle(TR("INSPECTION_LOG"));
    clearButton->setText(TR("CLEAR_LOG"));
    saveButton->setText(TR("SAVE_LOG"));
}