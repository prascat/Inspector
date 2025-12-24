#ifndef TRAINDIALOG_H
#define TRAINDIALOG_H

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QVector>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScrollArea>
#include <QGridLayout>
#include <QRadioButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QSplitter>
#include <QProgressBar>
#include <QProcess>
#include <QElapsedTimer>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <opencv2/opencv.hpp>
#include "CommonDefs.h"

class TrainDialog : public QWidget
{
    Q_OBJECT

public:
    explicit TrainDialog(QWidget *parent = nullptr);
    ~TrainDialog();

    // ANOMALY 패턴 목록 설정 (모든 패턴 포함하여 FID 찾기 가능하도록)
    void setAnomalyPatterns(const QVector<PatternInfo*>& patterns);
    void setAllPatterns(const QVector<PatternInfo*>& patterns);
    
    // 캡처된 이미지 추가
    void addCapturedImage(const cv::Mat& image, int cameraIndex);

signals:
    void trainRequested(const QString& patternName);
    void trainingFinished(bool success);

private slots:
    void onCloseClicked();
    void onClearImagesClicked();
    void onAddImagesClicked();
    void onDeleteSelectedImageClicked();
    void onModeChanged(int id);
    void onPatternSelectionChanged();
    void onStartAutoTrainClicked();
    void onImageItemClicked(QListWidgetItem* item);
    void onDockerOutputReady();
    void onDockerFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onRebuildDockerClicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUI();
    void applyBlackTheme();
    void updateImageGrid(bool scrollToEnd = true);
    void updateTeachingImagePreview();
    void trainPattern(const QString& patternName);
    void trainNextPattern();
    void updateTrainingProgress(const QString& message);
    void startDockerTraining(const QString& patternName, const QString& tempDir, const QString& outputDir);
    void checkAndCleanDockerImages();  // Docker 이미지 확인 및 정리
    QString getTotalTimeString() const;  // 총 경과 시간 문자열 반환
    QString getPatternProgressString() const;  // 패턴 진행률 문자열 반환 [1/3]

    QListWidget *patternListWidget;
    QPushButton *closeButton;
    QPushButton *clearImagesButton;
    QPushButton *addImagesButton;
    QPushButton *deleteSelectedImageButton;
    QPushButton *autoTrainButton;
    QLabel *imageCountLabel;
    QLabel *teachingImageLabel;
    
    // PatchCore 옵션 UI
    QComboBox *backboneComboBox;
    QDoubleSpinBox *coresetRatioSpinBox;
    QSpinBox *numNeighborsSpinBox;
    QPushButton *rebuildDockerButton;
    QLabel *dockerImageInfoLabel;  // Docker 이미지 빌드 옵션 표시
    
    // 이미지 미리보기 리스트
    QListWidget *imageListWidget;
    QLabel *previewImageLabel;
    
    // 학습 진행 상태 표시
    QWidget *trainingOverlay;
    QProgressBar *trainingProgressBar;
    QLabel *trainingStatusLabel;
    
    QVector<PatternInfo*> anomalyPatterns;
    QVector<PatternInfo*> allPatterns;  // 모든 패턴 (FID 찾기용)
    
    // 패턴별 캡처된 이미지 저장 (패턴 이름 -> 이미지 리스트)
    QMap<QString, QVector<cv::Mat>> patternImages;
    
    // 현재 선택된 패턴 이름
    QString currentSelectedPattern;
    
    // 체크박스와 패턴 매핑
    QMap<QString, QCheckBox*> patternCheckBoxes;
    
    // 마우스 드래그 관련
    bool m_dragging;
    QPoint m_dragPosition;
    bool m_firstShow;
    
    // 학습 관련
    QProcess *dockerTrainProcess;
    QElapsedTimer *trainingTimer;      // 개별 패턴 학습 시간
    QElapsedTimer *totalTrainingTimer; // 전체 학습 시간
    QTimer *progressUpdateTimer;       // 진행률 시간 갱신 타이머
    QStringList pendingPatterns;
    QString currentTrainingPattern;
    QString currentProgressMessage;    // 현재 진행 메시지 (시간 제외)
    QString tempTrainingDir;
    bool isTraining;
    int totalPatternCount;             // 전체 학습할 패턴 수
    int completedPatternCount;         // 완료된 패턴 수
};

#endif // TRAINDIALOG_H
