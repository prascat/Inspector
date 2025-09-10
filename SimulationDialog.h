#ifndef SIMULATIONDIALOG_H
#define SIMULATIONDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QListWidget>
#include <QProgressDialog>
#include <QProgressBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QSplitter>
#include <QTextEdit>
#include <QGroupBox>
#include <QComboBox>
#include <QApplication>
#include <QFileInfo>
#include <QCloseEvent>
#include <QTimer>
#include <QDateTime>
#include <QTextStream>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QInputDialog>
#include <QXmlStreamReader>
#include <QDir>
#include <QDomDocument>
#include <QUuid>
#include <QRandomGenerator>
#include <QRegularExpression>
#include "AITrainer.h"
#include <opencv2/opencv.hpp>
#include "RecipeManager.h"
#include "TrainResultsDialog.h"
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPaintEvent>

// 전방 선언
class TeachingWidget;

// 간단한 확대/축소 + 패닝이 가능한 라벨
class ZoomLabel : public QLabel {
    Q_OBJECT
public:
    explicit ZoomLabel(QWidget* parent = nullptr) : QLabel(parent), scaleFactor(1.0), dragging(false) {
        setBackgroundRole(QPalette::Base);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        setMouseTracking(true);
    }

    void setPixmap(const QPixmap& pm) {
        basePixmap = pm;
        scaleFactor = 1.0;
        // 기본적으로 라벨 가운데에 위치시킴
        int ox = (width() - basePixmap.width()) / 2;
        int oy = (height() - basePixmap.height()) / 2;
        offset = QPoint(ox, oy);
        update();
    }

    void resizeEvent(QResizeEvent* ev) override {
        Q_UNUSED(ev)
        // 리사이즈 시 기본 스케일(1.0)일 때는 중앙 정렬 유지
        if (!basePixmap.isNull() && qFuzzyCompare(scaleFactor, 1.0)) {
            int ox = (width() - basePixmap.width()) / 2;
            int oy = (height() - basePixmap.height()) / 2;
            offset = QPoint(ox, oy);
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent* ev) override {
        Q_UNUSED(ev)
        QPainter p(this);
        p.fillRect(rect(), palette().window());
        if (basePixmap.isNull()) return;
        p.save();
        p.translate(offset);
        p.scale(scaleFactor, scaleFactor);
        p.drawPixmap(0, 0, basePixmap);
        p.restore();
    }

    void wheelEvent(QWheelEvent* ev) override {
        constexpr double step = 1.15;
        double oldScale = scaleFactor;
        if (ev->angleDelta().y() > 0) scaleFactor *= step; else scaleFactor /= step;
        scaleFactor = qBound(0.1, scaleFactor, 10.0);

        // 마우스 포인트를 중심으로 줌 위치 보정
        QPointF pos = ev->position();
        QPointF delta = pos - offset;
        QPointF rel = delta / oldScale;
        QPointF newOffset = pos - rel * scaleFactor;
        offset = QPoint(int(newOffset.x()), int(newOffset.y()));

        update();
    }

    void mousePressEvent(QMouseEvent* ev) override {
        if (ev->button() == Qt::LeftButton) {
            dragging = true;
            lastPos = ev->pos();
            setCursor(Qt::ClosedHandCursor);
        }
    }

    void mouseMoveEvent(QMouseEvent* ev) override {
        if (dragging) {
            QPoint delta = ev->pos() - lastPos;
            lastPos = ev->pos();
            offset += delta;
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent* ev) override {
        Q_UNUSED(ev)
        dragging = false;
        setCursor(Qt::ArrowCursor);
    }

private:
    QPixmap basePixmap;
    double scaleFactor;
    QPoint offset;
    bool dragging;
    QPoint lastPos;
};

class SimulationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SimulationDialog(TeachingWidget* parentWidget = nullptr);
    ~SimulationDialog();
    
    // 현재 선택된 이미지 반환
    cv::Mat getCurrentImage() const { return currentImage; }
    QString getCurrentImagePath() const;
    bool hasImages() const { return !imagePaths.isEmpty(); }
    int getCurrentIndex() const { return currentIndex; }
    int getImageCount() const { return imagePaths.size(); }
    QString getSelectedCameraUuid() const { return selectedCameraUuid; }
    
    // 새 레시피 생성 시 초기화
    void clearForNewRecipe();
    QString getCurrentRecipeName() const { return currentRecipeName; }
    
    // 부모창 상태 설정
    void setParentClosing(bool closing) { parentClosing = closing; }
    
    // 티칭원본 인덱스 업데이트 (외부에서 호출)
    void updateTeachingImageIndex(int newIndex);
    QStringList getImagePaths() const { return imagePaths; } // 모든 이미지 경로 반환
    QStringList getTrainingImagePaths() const { return trainingImagePaths; } // 학습 이미지 경로 반환
    void setTrainingImagePaths(const QStringList& paths); // 학습 이미지 경로 설정
    
    // 레시피 이미지 로드 (외부에서 호출)
    void loadRecipeImages(const QString& recipeName);
    
    // 레시피 목록 새로고침 (레시피 삭제 시 호출)
    void refreshRecipeList();
    
    // 카메라 목록 업데이트 (레시피 로드 시 호출)
    void updateCameraList(const QString& recipeName);

protected:
    void closeEvent(QCloseEvent *event) override;

signals:
    void imageSelected(const cv::Mat& image, const QString& imagePath, const QString& recipeName);
    void recipeNameChanged(const QString& recipeName);
    void recipeSelected(const QString& recipeName); // 새로 추가

private slots:
    void loadImages();
    void loadTrainingImages(); // 학습 이미지 추가 슬롯
    void removeTrainingImage(); // 학습 이미지 삭제 슬롯
    void onTrainingButtonClicked(); // 학습 버튼 클릭 슬롯
    void onTrainResultsButtonClicked(); // 학습 결과 버튼 클릭 슬롯
    void onDetectionButtonClicked(); // 이상 탐지 버튼 클릭 슬롯
    
    // Docker 관리 슬롯들
    void dockerInstall(); // Docker 설치 안내
    void dockerBuild(); // Docker 이미지 빌드
    void dockerStart(); // Docker 컨테이너 시작
    void dockerStop(); // Docker 컨테이너 중지
    void dockerDelete(); // Docker 이미지 삭제
    void refreshDockerStatus(); // Docker 상태 새로고침
    
    void onTrainingProgress(int percentage, const QString& message); // 학습 진행 상황
    void onTrainingCompleted(bool success, const QString& message); // 학습 완료
    void onImageListClicked(int row);
    void onTrainingImageListClicked(int row); // 학습 이미지 클릭 슬롯
    void onPrevClicked();
    void onNextClicked();
    void onTrainRatioChanged(int value); // 학습 비율 변경 슬롯
    void onCameraSelectionChanged(int index); // 카메라 선택 변경 슬롯

private:
    void setupUI();
    void updateImageDisplay();
    void updateControls();
    void updateImageList(); // 이미지 목록 UI 갱신
    void loadImageAtIndex(int index);
    QString getSimulationDataFilePath() const;
    void updateRecipeInfo();
    void clearRecipe();
    void ensureRecipeInConfig(); // 레시피가 config에 있는지 확인하고 없으면 추가
    
    // UI 컴포넌트
    // QListWidget* imageListWidget; // 제거됨
    // QListWidget* trainingImageListWidget; // 제거됨 - 학습 이미지 목록
    QLabel* imageDisplayLabel;
    QLabel* heatmapDisplayLabel;
    QPushButton* loadImagesButton;
    QPushButton* loadTrainingImagesButton; // 학습 이미지 추가 버튼
    
    // 카메라 선택 UI
    QComboBox* cameraComboBox;
    QLabel* cameraSelectionLabel;
    QPushButton* removeTrainingImageButton; // 학습 이미지 삭제 버튼
    QPushButton* trainButton; // 학습 버튼
    QPushButton* trainResultsButton; // 학습 결과 버튼
    QPushButton* detectButton; // 이상 탐지 버튼
    QProgressBar* trainingProgressBar; // 학습 진행률 표시
    
    // 학습 데이터 비율 설정 UI
    QSlider* trainRatioSlider;
    QLabel* trainRatioLabel;
    QLabel* trainRatioValueLabel;
    
    // Docker 관리 UI
    QLabel* dockerStatusLabel;
    QPushButton* dockerInstallButton;
    QPushButton* dockerBuildButton;
    QPushButton* dockerStartButton;
    QPushButton* dockerStopButton;
    QPushButton* dockerDeleteButton;
    QPushButton* dockerRefreshButton;
    
    QPushButton* prevButton;
    QPushButton* nextButton;
    QLabel* imageInfoLabel;
    QLabel* scoreLabel;
    // QLabel* imageCountLabel; // 제거됨
    // QLabel* trainCountLabel;  // 제거됨 - 학습 이미지 개수 라벨
    QLabel* recipeInfoLabel;
    
    // 데이터
    QStringList imagePaths;
    QStringList trainingImagePaths; // 학습 이미지 경로들
    cv::Mat currentImage;
    int currentIndex;
    int teachingImageIndex; // 티칭원본 이미지 인덱스
    QString currentRecipeName;
    QString currentRecipePath;
    QString selectedCameraUuid; // 시뮬레이션에서 선택된 카메라 UUID
    
    // 부모창 상태 플래그
    bool parentClosing = false;
    bool creatingNewRecipe = false;  // 새 레시피 생성 중인지 확인하는 플래그
    bool loadingRecipeImages = false; // 레시피 이미지 로딩 중인지 확인하는 플래그
    
    // RecipeManager 인스턴스
    RecipeManager recipeManager;
    
    // TeachingWidget 참조 (레시피 관리 함수 호출용)
    TeachingWidget* teachingWidget;
    
    // AI 이상 탐지 학습기
    AITrainer* aiTrainer;
};

#endif // SIMULATIONDIALOG_H
