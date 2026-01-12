#ifndef TRAINDIALOG_H
#define TRAINDIALOG_H

#include <QWidget>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
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
#include <QTabWidget>
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
    
    // 현재 레시피명 설정
    void setCurrentRecipeName(const QString& recipeName);
    
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
    void onPadimPatternSelectionChanged();
    void onStartAutoTrainClicked();
    void onImageItemClicked(QListWidgetItem* item);
    void onTrainOutputReady();
    void onTrainFinished(int exitCode, QProcess::ExitStatus exitStatus);

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
    void updatePadimTeachingImagePreview();
    void trainPattern(const QString& patternName);
    void trainNextPattern();
    void updateTrainingProgress(const QString& message);
    void startDockerTraining(const QString& patternName, const QString& tempDir, const QString& outputDir);
    void updateTrainingStatus();  // weights 폴더 체크하여 학습 여부 갱신
    QString getTotalTimeString() const;  // 총 경과 시간 문자열 반환
    QString getPatternProgressString() const;  // 패턴 진행률 문자열 반환 [1/3]

    QTabWidget *modelTabWidget;
    
    // A-PC 탭 위젯들
    QTableWidget *patternTableWidget;
    QComboBox *backboneComboBox;
    QDoubleSpinBox *coresetRatioSpinBox;
    QSpinBox *numNeighborsSpinBox;
    QCheckBox *selectAllCheckBox;
    
    // A-PD 탭 위젯들
    QTableWidget *padimPatternTableWidget;
    QComboBox *padimBackboneComboBox;
    QSpinBox *padimNumLayersSpinBox;
    QCheckBox *padimSelectAllCheckBox;
    
    QPushButton *closeButton;
    QPushButton *clearImagesButton;
    QPushButton *addImagesButton;
    QPushButton *deleteSelectedImageButton;
    QPushButton *autoTrainButton;
    QLabel *imageCountLabel;
    
    // 이미지 미리보기 리스트
    QListWidget *imageListWidget;
    QLabel *previewImageLabel;
    
    // 학습 진행 상태 표시
    QWidget *trainingOverlay;
    QProgressBar *trainingProgressBar;
    QLabel *trainingStatusLabel;
    
    QVector<PatternInfo*> anomalyPatterns;
    QVector<PatternInfo*> allPatterns;  // 모든 패턴 (FID 찾기용)
    
    // 공용 캡처된 이미지 저장 (모든 패턴이 공유)
    QVector<cv::Mat> commonImages;
    
    // 현재 선택된 패턴 이름
    QString currentSelectedPattern;
    
    // 체크박스와 패턴 매핑 (A-PC, A-PD 각각)
    QMap<QString, QCheckBox*> patternCheckBoxes;
    QMap<QString, QCheckBox*> padimPatternCheckBoxes;
    
    // 마우스 드래그 관련
    bool m_dragging;
    QPoint m_dragPosition;
    bool m_firstShow;
    
    // 학습 관련
    QProcess *trainProcess;
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
    
    QString currentRecipeName;         // 현재 레시피명
};

#endif // TRAINDIALOG_H
