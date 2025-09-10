#ifndef AITRAINER_H
#define AITRAINER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QProgressDialog>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QProcess>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <opencv2/opencv.hpp>

class AITrainer : public QObject
{
    Q_OBJECT

public:
    explicit AITrainer(QObject *parent = nullptr);
    ~AITrainer();

    // 학습 설정
    struct TrainingConfig {
        QString datasetName;
        QString resultDir;
        QString modelPath;       // 모델 저장 경로
        QString datasetPath;     // 데이터셋 폴더 경로
        QString backbone;        // 백본 네트워크 (wide_resnet50_2, resnet50 등)
        double coresetRatio;     // Coreset 샘플링 비율
        int batchSize;           // 배치 크기
        int trainRatio;          // 학습 데이터 비율 (%)
        int testRatio;           // 테스트 데이터 비율 (%)
        
        // 생성자에서 기본값 설정
        TrainingConfig() 
            : datasetName("custom_anomaly_detection")
        , resultDir("results/ai_training")
            , modelPath("")
            , datasetPath("")
            , backbone("wide_resnet50_2")
            , coresetRatio(0.1)
            , batchSize(16)
            , trainRatio(80)
            , testRatio(20)
        {
        }
    };

    // 메인 학습 함수
    bool trainModel(const QStringList& normalImagePaths, const TrainingConfig& config = TrainingConfig());
    
    // 이상 탐지 수행
    struct DetectionResult {
        bool isSuccess;          // 탐지 성공 여부
        bool isAnomalous;
        double anomalyScore;
        QString anomalyMapPath;  // 이상 맵 이미지 경로
        QString resultImagePath; // 결과 이미지 경로
        QString anomalyMapBase64;// 이상 맵 Base64 데이터
        QString heatmapBase64;   // 히트맵 Base64 데이터
        QString overlayBase64;   // 오버레이 이미지 Base64 데이터
        QString errorMessage;
        
        // 생성자에서 기본값 설정
        DetectionResult()
            : isSuccess(false)
            , isAnomalous(false)
            , anomalyScore(0.0)
            , anomalyMapPath("")
            , resultImagePath("")
            , anomalyMapBase64("")
            , heatmapBase64("")
            , overlayBase64("")
            , errorMessage("")
        {
        }
    };
    
    DetectionResult detectAnomaly(const QString& testImagePath, const QString& recipeName);
    // 다중 영역 예측: rects는 [{"id":..., "x":..., "y":..., "w":..., "h":..., "angle":...}, ...]
    QJsonObject multi_predict(const QString& imagePath, const QString& recipeName, const QJsonArray& rects);
    
    // 모델 미리 로딩
    bool loadModel(const QString& recipeName);
    
    // 로딩된 모델 정리
    void unloadModel(const QString& recipeName);
    void unloadAllModels();
    
    // 모델 상태 확인
    bool isModelTrained() const { return modelTrained; }
    bool isDockerAvailable() const;
    QString getLastError() const { return lastError; }
    QString getModelPath() const { return modelPath; }

signals:
    void trainingProgress(int percentage, const QString& message);
    void trainingCompleted(bool success, const QString& message);

private slots:
    void onDockerProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onDockerProcessError(QProcess::ProcessError error);

private:
    // 멤버 변수
    bool modelTrained;
    QString lastError;
    QString modelPath;
    QString dockerImage;
    TrainingConfig currentConfig;
    QProcess* dockerProcess;
    QTimer* progressTimer;
    
    // 네트워크 관련
    QNetworkAccessManager* networkManager;
    QString apiBaseUrl;
    QString dockerContainerName;
    
    // 로딩된 모델 추적
    QSet<QString> loadedRecipes;
    
    // Docker 관련 함수
    bool startDockerContainer();
    bool stopDockerContainer();
    bool isDockerContainerRunning();
    bool checkDockerInstallation();
    bool buildDockerImage();
    
    // API 통신 함수
    QNetworkReply* sendTrainingRequest(const QStringList& normalImagePaths, const TrainingConfig& config);
    QNetworkReply* sendPredictionRequest(const cv::Mat& image);
    QNetworkReply* sendHealthCheckRequest();
    QNetworkReply* sendTrainingStatusRequest();
    
    // 학습 상태 모니터링
    void startTrainingStatusCheck();
    void stopTrainingStatusCheck();
    void checkTrainingStatus();
    
    // 결과 처리
    void handleTrainingResponse(QNetworkReply* reply);
    void handlePredictionResponse(QNetworkReply* reply);
    DetectionResult parseDetectionResult(const QJsonObject& result);
    
    // 유틸리티 함수
    void setError(const QString& error);
    QString createTempConfigFile(const QJsonObject& config);
    bool waitForDockerCompletion(int timeoutMs = 300000); // 5분 타임아웃
};

#endif // AITRAINER_H
