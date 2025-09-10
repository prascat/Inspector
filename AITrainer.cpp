#include "AITrainer.h"
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHttpPart>
#include <QHttpMultiPart>
#include <QUrlQuery>
#include <QBuffer>
#include <QPixmap>
#include <QStandardPaths>
#include <QUuid>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QDateTime>
#include <QFile>
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>

AITrainer::AITrainer(QObject *parent)
    : QObject(parent)
    , modelTrained(false)
    , dockerProcess(nullptr)
    , progressTimer(new QTimer(this))
    , networkManager(new QNetworkAccessManager(this))
    , dockerImage("ai-server")
    , apiBaseUrl("http://localhost:5000")
    , dockerContainerName("ai-server")
{
    // 진행 상황 체크 타이머
    progressTimer->setSingleShot(false);
    progressTimer->setInterval(500);
    connect(progressTimer, &QTimer::timeout, this, &AITrainer::checkTrainingStatus);
}

AITrainer::~AITrainer()
{
    if (dockerProcess && dockerProcess->state() == QProcess::Running) {
        dockerProcess->kill();
        dockerProcess->waitForFinished(3000);
    }
}

bool AITrainer::trainModel(const QStringList& normalImagePaths, const TrainingConfig& config)
{
    if (normalImagePaths.isEmpty()) {
        setError("학습 이미지가 없습니다.");
        return false;
    }

    currentConfig = config;
    
    qDebug() << "=== AI 이상 탐지 학습 시작 ===";
    qDebug() << "레시피:" << config.datasetName;
    qDebug() << "모델: AI (1 epoch)";

    // Docker 컨테이너 시작
    if (!startDockerContainer()) {
        setError("Docker 컨테이너 시작에 실패했습니다.");
        return false;
    }

    // API 요청 생성 (간단한 정보만)
    QJsonObject requestData;
    requestData["recipe_name"] = config.datasetName;     // SIM_20250820_110755

    qDebug() << "학습 요청:";
    qDebug() << " - 레시피:" << config.datasetName;
    qDebug() << " - 모델: AI (1 epoch)";

    // HTTP 요청 전송
    QNetworkRequest request(QUrl(apiBaseUrl + "/api/train"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QJsonDocument jsonDoc(requestData);
    QNetworkReply* reply = networkManager->post(request, jsonDoc.toJson());
    
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        handleTrainingResponse(reply);
        reply->deleteLater();
    });

    return true;
}

bool AITrainer::startDockerContainer()
{
    // Docker 컨테이너가 이미 실행 중인지 확인
    if (isDockerContainerRunning()) {
        qDebug() << "Docker 컨테이너가 이미 실행 중입니다.";
        return true;
    }

    // 컨테이너가 존재하는지 확인하고 시작
    QProcess checkProcess;
    checkProcess.start("docker", QStringList() << "ps" << "-a" << "--format" << "{{.Names}}" << "--filter" << QString("name=%1").arg(dockerContainerName));
    checkProcess.waitForFinished(10000);
    
    QString output = checkProcess.readAllStandardOutput().trimmed();
    
    if (output.contains(dockerContainerName)) {
        // 컨테이너 존재, 시작
        qDebug() << "기존 Docker 컨테이너를 시작합니다...";
        QProcess startProcess;
        startProcess.start("docker", QStringList() << "start" << dockerContainerName);
        startProcess.waitForFinished(30000);
        
        if (startProcess.exitCode() != 0) {
            qWarning() << "Docker 컨테이너 시작 실패:" << startProcess.readAllStandardError();
            return false;
        }
    } else {
        // 새 컨테이너 생성 및 실행
        qDebug() << "새 Docker 컨테이너를 생성합니다...";
        QStringList args;
        args << "run" << "-d" << "--name" << dockerContainerName 
             << "-p" << "5000:5000" 
             << "-v" << QString("%1/deploy:/app/host").arg(QDir::currentPath())
             << "-v" << QString("%1/ai_api.py:/app/ai_api.py").arg(QDir::currentPath())
             << "-v" << QString("%1/ai_trainer.py:/app/ai_trainer.py").arg(QDir::currentPath())
             << "-v" << QString("%1/ai_inference.py:/app/ai_inference.py").arg(QDir::currentPath())
             << dockerImage;
             
        QProcess runProcess;
        runProcess.start("docker", args);
        runProcess.waitForFinished(30000);
        
        if (runProcess.exitCode() != 0) {
            qWarning() << "Docker 컨테이너 실행 실패:" << runProcess.readAllStandardError();
            return false;
        }
    }

    // 컨테이너가 준비될 때까지 대기
    QTimer::singleShot(3000, [this]() {
        // 헬스 체크 시작
        sendHealthCheckRequest();
    });

    return true;
}

bool AITrainer::isDockerContainerRunning()
{
    QProcess process;
    process.start("docker", QStringList() << "ps" << "--format" << "{{.Names}}" << "--filter" << QString("name=%1").arg(dockerContainerName));
    process.waitForFinished(10000);
    
    QString output = process.readAllStandardOutput().trimmed();
    return output.contains(dockerContainerName);
}

QNetworkReply* AITrainer::sendHealthCheckRequest()
{
    QNetworkRequest request(QUrl(apiBaseUrl + "/api/health"));
    QNetworkReply* reply = networkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            qDebug() << "API 서버 준비 완료";
        } else {
            qWarning() << "API 서버 연결 실패:" << reply->errorString();
        }
        reply->deleteLater();
    });
    
    return reply;
}

void AITrainer::handleTrainingResponse(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        setError(QString("학습 요청 실패: %1").arg(reply->errorString()));
        emit trainingCompleted(false, lastError);
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
    QJsonObject response = jsonDoc.object();
    
    qDebug() << "학습 요청 응답:" << responseData;

    // Flask API에서 여러 응답 형식 지원
    QString status = response["status"].toString();
    if (status == "success" || status == "training_started" || status == "started" || status.isEmpty()) {
        qDebug() << "학습 시작 - 진행 상황 모니터링 시작";
        
        // 학습 상태 모니터링 시작
        startTrainingStatusCheck();
        
        emit trainingProgress(0, "학습 시작됨...");
    } else {
        QString error = response["error"].toString();
        if (error.isEmpty()) {
            error = response["message"].toString();
        }
        setError(QString("학습 시작 실패: %1").arg(error));
        emit trainingCompleted(false, lastError);
    }
}

void AITrainer::startTrainingStatusCheck()
{
    qDebug() << "학습 상태 모니터링 시작";
    progressTimer->start();
}

void AITrainer::stopTrainingStatusCheck()
{
    qDebug() << "학습 상태 모니터링 중지";
    progressTimer->stop();
}

void AITrainer::checkTrainingStatus()
{
    QNetworkRequest request(QUrl(apiBaseUrl + "/api/training_status"));
    QNetworkReply* reply = networkManager->get(request);
    
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
            QJsonObject response = jsonDoc.object();
            
            QString status = response["status"].toString();
            int progress = response["progress"].toInt();
            QString message = response["message"].toString();
            
            qDebug() << QString("학습 상태: \"%1\" %2%% \"%3\"").arg(status).arg(progress).arg(message);
            qDebug() << "전체 응답:" << responseData;
            
            // 실제 학습 진행률 추정 (training 상태일 때)
            if (status == "training" && progress == 30) {
                // 실제 학습이 진행 중일 때는 더 세밀한 진행률 계산
                static int actualProgress = 0;
                actualProgress += 2; // 1초마다 2% 증가 (임시)
                if (actualProgress > 95) actualProgress = 95; // 95%까지만
                
                emit trainingProgress(actualProgress, QString("학습 진행 중... (%1)").arg(message));
            } else {
                emit trainingProgress(progress, message);
            }
            
            if (status == "completed") {
                stopTrainingStatusCheck();
                modelTrained = true;
                modelPath = currentConfig.modelPath;
                emit trainingCompleted(true, "학습이 완료되었습니다.");
            } else if (status == "error") {
                stopTrainingStatusCheck();
                setError(message);
                emit trainingCompleted(false, lastError);
            }
        }
        reply->deleteLater();
    });
}

AITrainer::DetectionResult AITrainer::detectAnomaly(const QString& testImagePath, const QString& recipeName)
{
    DetectionResult result;
    
    if (!QFile::exists(testImagePath)) {
        result.errorMessage = "이미지 파일이 존재하지 않습니다.";
        return result;
    }
    
    // 파일명 추출 (확장자 제거)
    QString imageBaseName = QFileInfo(testImagePath).baseName();
    
    // API 요청 생성 (파일 경로만 전송)
    QJsonObject requestData;
    requestData["image_path"] = testImagePath;
    requestData["recipe_name"] = recipeName;
    requestData["original_filename"] = imageBaseName;
    
    QNetworkRequest request(QUrl(apiBaseUrl + "/api/predict"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    
    QJsonDocument jsonDoc(requestData);
    QByteArray requestBody = jsonDoc.toJson();
    
    qDebug() << "[AITrainer] API 요청 전송 중...";
    qDebug() << "[AITrainer] URL:" << apiBaseUrl + "/api/predict";
    qDebug() << "[AITrainer] 레시피 이름:" << recipeName;
    qDebug() << "[AITrainer] 이미지 경로:" << testImagePath;
    qDebug() << "[AITrainer] 출력 파일명:" << imageBaseName;
    qDebug() << "[AITrainer] 요청 크기:" << requestBody.size() << "bytes";
    
    QNetworkReply* reply = networkManager->post(request, requestBody);
    
    // 타임아웃 설정 (30초)
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, [reply]() {
        reply->abort();
    });
    timeoutTimer.start(30000);
    
    // 이벤트 루프를 실행하여 응답을 기다림
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        qDebug() << "[AITrainer] API 응답 데이터 크기:" << responseData.size() << "bytes";
        qDebug() << "[AITrainer] API 응답 내용 (첫 500자):" << responseData.left(500);
        
        QJsonParseError parseError;
        QJsonDocument responseDoc = QJsonDocument::fromJson(responseData, &parseError);
        
        if (parseError.error != QJsonParseError::NoError) {
            result.errorMessage = QString("JSON 파싱 오류: %1").arg(parseError.errorString());
            reply->deleteLater();
            return result;
        }
        
        QJsonObject responseObj = responseDoc.object();
        qDebug() << "[AITrainer] 응답 JSON 객체:" << responseObj;
        
        // Accept multiple success indicators. Some APIs return "ok" instead of "success".
        QString status = responseObj.value("status").toString();
        if (status == "success" || status == "ok" || responseObj.contains("score")) {
            result.isSuccess = true;
            result.anomalyScore = responseObj.value("score").toDouble();
            result.isAnomalous = responseObj.value("is_anomaly").toBool();

            qDebug() << "[AITrainer] 탐지 완료";
            qDebug() << "[AITrainer] 이상도 점수:" << result.anomalyScore;
            qDebug() << "[AITrainer] 이상 여부:" << result.isAnomalous;

            // 결과 디렉터리와 파일 목록이 제공되면 첫 번째 파일을 결과 이미지 경로로 사용
            if (responseObj.contains("results_dir") && responseObj.contains("files")) {
                QString resultsDir = responseObj["results_dir"].toString();
                QJsonArray files = responseObj["files"].toArray();
                if (!files.isEmpty()) {
                    QString fname = files.at(0).toString();
                    QString candidate = QString("%1/%2").arg(resultsDir, fname);
                    // Candidate may be an absolute container path like /app/host/..., try it first
                    if (QFile::exists(candidate)) {
                        result.resultImagePath = candidate;
                        qDebug() << "[AITrainer] 결과 이미지 경로 설정:" << result.resultImagePath;
                    } else {
                        // If candidate is under /app/host, map it to host's deploy folder where we mounted it
                        const QString hostPrefix = "/app/host";
                        if (candidate.startsWith(hostPrefix)) {
                            QString relativePart = candidate.mid(hostPrefix.length());
                            QString hostCandidate = QDir::cleanPath(QDir::currentPath() + "/deploy" + relativePart);
                            if (QFile::exists(hostCandidate)) {
                                result.resultImagePath = hostCandidate;
                                qDebug() << "[AITrainer] 결과 이미지 경로 설정(호스트 매핑):" << result.resultImagePath;
                            } else {
                                qDebug() << "[AITrainer] 호스트 매핑 경로에도 파일 없음:" << hostCandidate;
                            }
                        } else {
                            // 마지막으로 시도: 현재 작업 디렉터리와 결합
                            QString hostCandidate = QDir::cleanPath(QDir::currentPath() + "/" + candidate);
                            if (QFile::exists(hostCandidate)) {
                                result.resultImagePath = hostCandidate;
                                qDebug() << "[AITrainer] 결과 이미지 경로 설정(호스트 상대):" << result.resultImagePath;
                            } else {
                                qDebug() << "[AITrainer] 결과 이미지 파일을 찾을 수 없음:" << candidate << hostCandidate;
                            }
                        }
                    }
                }
            }
        } else {
            // If API returned non-standard error format, capture message if present
            if (responseObj.contains("message")) {
                result.errorMessage = responseObj["message"].toString();
            } else {
                result.errorMessage = QString("API 오류 상태: %1").arg(status);
            }
        }
    } else {
        result.errorMessage = QString("추론 실패: %1").arg(reply->errorString());
    }
    
    reply->deleteLater();
    return result;
}


bool AITrainer::loadModel(const QString& recipeName)
{
    // 이미 로딩된 모델인지 확인
    if (loadedRecipes.contains(recipeName)) {
        qDebug() << "[AITrainer] Model already loaded for recipe:" << recipeName;
        return true;
    }

    // 모델이 존재하는지 확인 (ONNX 우선, 없으면 PyTorch)
    QString appBase = QDir::cleanPath(QCoreApplication::applicationDirPath());
    QString onnxCandidate1 = QDir::cleanPath(appBase + "/models/" + recipeName + "/model.onnx");
    QString onnxCandidate2 = QDir::cleanPath(QDir::currentPath() + "/models/" + recipeName + "/model.onnx");
    QString ckptCandidate1 = QDir::cleanPath(appBase + "/models/" + recipeName + "/model.ckpt");
    QString ckptCandidate2 = QDir::cleanPath(QDir::currentPath() + "/models/" + recipeName + "/model.ckpt");

    bool onnxExists = QFile::exists(onnxCandidate1) || QFile::exists(onnxCandidate2);
    bool ckptExists = QFile::exists(ckptCandidate1) || QFile::exists(ckptCandidate2);
    bool modelExists = onnxExists || ckptExists;

    if (!modelExists) {
        qWarning() << "[AITrainer] Model not found for recipe:" << recipeName;
        return false;
    }

    // ONNX 모델이 있으면 우선 사용
    if (onnxExists) {
        qDebug() << "[AITrainer] ONNX model found for recipe:" << recipeName;
    } else if (ckptExists) {
        qDebug() << "[AITrainer] PyTorch model found for recipe:" << recipeName;
    }

    QJsonObject loadModelData;
    loadModelData["recipe_name"] = recipeName;
    
    QNetworkRequest loadRequest(QUrl(apiBaseUrl + "/api/load_model"));
    loadRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QJsonDocument loadDoc(loadModelData);
    QByteArray loadBody = loadDoc.toJson();
    
    // 동기적으로 모델 로드 요청
    QEventLoop loadLoop;
    QNetworkReply* loadReply = networkManager->post(loadRequest, loadBody);
    
    connect(loadReply, &QNetworkReply::finished, &loadLoop, &QEventLoop::quit);
    
    QTimer loadTimer;
    loadTimer.setSingleShot(true);
    connect(&loadTimer, &QTimer::timeout, [&]() {
        loadReply->abort();
        loadLoop.quit();
    });
    loadTimer.start(10000); // 10초 타임아웃
    loadLoop.exec();
    
    if (loadReply->error() == QNetworkReply::NoError) {
        QByteArray loadResponseData = loadReply->readAll();
        QJsonDocument loadJsonDoc = QJsonDocument::fromJson(loadResponseData);
        QJsonObject loadResponse = loadJsonDoc.object();
        
        QString loadStatus = loadResponse["status"].toString();
        if (loadStatus == "success") {
            qDebug() << "[AITrainer] Model loaded successfully for recipe:" << recipeName;
            loadedRecipes.insert(recipeName); // 로딩 완료로 표시
            loadReply->deleteLater();
            return true;
        } else {
            qWarning() << "[AITrainer] Model loading failed:" << loadResponse["error"].toString();
        }
    } else {
        qWarning() << "[AITrainer] Model loading request failed:" << loadReply->errorString();
    }
    
    loadReply->deleteLater();
    return false;
}


void AITrainer::unloadModel(const QString& recipeName)
{
    if (loadedRecipes.contains(recipeName)) {
        qDebug() << "[AITrainer] Unloading model for recipe:" << recipeName;
        
        // 서버 측에 unload API 호출
        QJsonObject unloadData;
        unloadData["recipe_name"] = recipeName;
        
        QNetworkRequest unloadRequest(QUrl(apiBaseUrl + "/api/unload_model"));
        unloadRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        
        QJsonDocument unloadDoc(unloadData);
        QByteArray unloadBody = unloadDoc.toJson();
        
        // 동기적으로 모델 언로드 요청
        QEventLoop unloadLoop;
        QNetworkReply* unloadReply = networkManager->post(unloadRequest, unloadBody);
        
        connect(unloadReply, &QNetworkReply::finished, &unloadLoop, &QEventLoop::quit);
        
        QTimer unloadTimer;
        unloadTimer.setSingleShot(true);
        connect(&unloadTimer, &QTimer::timeout, [&]() {
            unloadReply->abort();
            unloadLoop.quit();
        });
        unloadTimer.start(10000); // 10초 타임아웃
        unloadLoop.exec();
        
        if (unloadReply->error() == QNetworkReply::NoError) {
            QByteArray unloadResponseData = unloadReply->readAll();
            QJsonDocument unloadJsonDoc = QJsonDocument::fromJson(unloadResponseData);
            QJsonObject unloadResponse = unloadJsonDoc.object();
            
            QString unloadStatus = unloadResponse["status"].toString();
            if (unloadStatus == "success") {
                qDebug() << "[AITrainer] Model unloaded successfully from server for recipe:" << recipeName;
                loadedRecipes.remove(recipeName); // 클라이언트 측에서도 제거
            } else {
                qWarning() << "[AITrainer] Model unloading failed:" << unloadResponse["error"].toString();
            }
        } else {
            qWarning() << "[AITrainer] Model unloading request failed:" << unloadReply->errorString();
        }
        
        unloadReply->deleteLater();
    } else {
        qDebug() << "[AITrainer] No loaded model found for recipe:" << recipeName;
    }
}


void AITrainer::unloadAllModels()
{
    qDebug() << "[AITrainer] Unloading all models";
    
    // 로딩된 모든 레시피에 대해 언로드 요청
    QStringList recipesToUnload = loadedRecipes.values();
    for (const QString& recipe : recipesToUnload) {
        unloadModel(recipe);
    }
    
    // 모든 모델이 언로드되었는지 확인
    if (loadedRecipes.isEmpty()) {
        qDebug() << "[AITrainer] All models unloaded successfully";
    } else {
        qWarning() << "[AITrainer] Some models failed to unload:" << loadedRecipes;
    }
}


QJsonObject AITrainer::multi_predict(const QString& imagePath, const QString& recipeName, const QJsonArray& rects)
{
    QJsonObject responseObj;

    QJsonObject requestData;
    requestData["recipe_name"] = recipeName;
    requestData["rects"] = rects;

    // Prefer application directory as our host base (executable runs inside deploy)
    QString appBase = QDir::cleanPath(QCoreApplication::applicationDirPath());

    QNetworkReply* reply = nullptr;

    // If imagePath is provided and not empty, try to use image_filename first
    if (!imagePath.isEmpty()) {
        requestData["image_path"] = imagePath;

        // If the same filename exists in appBase/data/<recipe>/imgs or appBase/data/<recipe>, prefer sending image_filename
        QString fileName = QFileInfo(imagePath).fileName();
        if (!fileName.isEmpty()) {
            QString hostCandidate1 = QDir::cleanPath(appBase + "/data/" + recipeName + "/imgs/" + fileName);
            QString hostCandidate2 = QDir::cleanPath(appBase + "/data/" + recipeName + "/" + fileName);
            QString hostCandidate3 = QDir::cleanPath(QDir::currentPath() + "/data/" + recipeName + "/imgs/" + fileName);
            bool hasOriginal = QFile::exists(hostCandidate1) || QFile::exists(hostCandidate2) || QFile::exists(hostCandidate3);
            if (hasOriginal) {
                // Prefer image_filename so server will use /app/host/data/<recipe>/imgs/<filename>
                requestData.remove("image_path");
                requestData["image_filename"] = fileName;
            }
        }

        // Send as JSON
    QNetworkRequest request(QUrl(apiBaseUrl + "/api/multi_predict"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Accept", "application/json");

        QJsonDocument doc(requestData);
        QByteArray body = doc.toJson();
        reply = networkManager->post(request, body);
    qDebug() << "[AITrainer] multi_predict (json) request body:" << QString(body);
    } else {
        // No image data available
    qWarning() << "multi_predict: no image path provided";
        responseObj["error"] = "No image data provided";
        return responseObj;
    }

    // Diagnostic: check for model presence relative to application directory (avoid deploy/deploy)
    QString onnxCandidate1 = QDir::cleanPath(appBase + "/models/" + recipeName + "/model.onnx");
    QString onnxCandidate2 = QDir::cleanPath(QDir::currentPath() + "/models/" + recipeName + "/model.onnx");
    QString ckptCandidate1 = QDir::cleanPath(appBase + "/models/" + recipeName + "/model.ckpt");
    QString ckptCandidate2 = QDir::cleanPath(QDir::currentPath() + "/models/" + recipeName + "/model.ckpt");

    bool onnxExists = QFile::exists(onnxCandidate1) || QFile::exists(onnxCandidate2);
    bool ckptExists = QFile::exists(ckptCandidate1) || QFile::exists(ckptCandidate2);
    bool modelExists = onnxExists || ckptExists;

    QString modelType = onnxExists ? "ONNX" : (ckptExists ? "PyTorch" : "None");
            qDebug() << "multi_predict: checking model candidates:" << (onnxExists ? onnxCandidate1 : ckptCandidate1) << (onnxExists ? onnxCandidate2 : ckptCandidate2) << "exists=" << modelExists << "type=" << modelType;

    // 모델이 존재하면 미리 로드 (아직 로딩되지 않은 경우만)
    if (!loadedRecipes.contains(recipeName)) {
        loadModel(recipeName);
    }

    // 동기 응답 대기 (타임아웃 30초)
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, [&]() {
        reply->abort();
        loop.quit();
    });
    timer.start(30000);
    loop.exec();

    QByteArray resp = reply->readAll();
    if (reply->error() == QNetworkReply::NoError) {
        QJsonParseError err;
        QJsonDocument rdoc = QJsonDocument::fromJson(resp, &err);
        if (err.error == QJsonParseError::NoError && rdoc.isObject()) {
            responseObj = rdoc.object();
            // If multi_results exists, try to extract per-rect pct/area into top-level for easy UI use
            if (responseObj.contains("multi_results") && responseObj["multi_results"].isObject()) {
                QJsonObject mr = responseObj["multi_results"].toObject();
                if (mr.contains("results") && mr["results"].isArray()) {
                    QJsonArray resultsArr = mr["results"].toArray();
                    // Attach a summarized array of per-rect metrics
                    QJsonArray metrics;
                    for (const QJsonValue& v : resultsArr) {
                        if (!v.isObject()) continue;
                        QJsonObject o = v.toObject();
                        QJsonObject m;
                        m["id"] = o.value("id").toString();
                        if (o.contains("pct")) m["pct"] = o.value("pct");
                        if (o.contains("area")) m["area"] = o.value("area");
                        if (o.contains("score")) m["score"] = o.value("score");
                        metrics.append(m);
                    }
                    responseObj["rect_metrics"] = metrics;
                }
            }
        } else {
            qWarning() << "multi_predict: JSON 파싱 실패" << err.errorString();
            qDebug() << "multi_predict: raw response:" << QString(resp);
        }
    } else {
    qWarning() << "multi_predict: 요청 실패" << reply->errorString();
    qDebug() << "multi_predict: server response:" << QString(resp);
        // Attempt to parse stderr-like JSON-lines embedded in the response body
        // (Some servers emit JSON-lines to stderr; see ai_inference improvements)
        QString bodyStr = QString(resp);
        QStringList lines = bodyStr.split('\n');
        for (const QString& line : lines) {
            if (line.trimmed().startsWith('{') && line.contains("\"rect_log\"")) {
                qDebug() << "multi_predict: found rect_log line:" << line.left(400);
            }
        }
        // try to parse error JSON if available
        QJsonParseError err;
        QJsonDocument rdoc = QJsonDocument::fromJson(resp, &err);
        if (err.error == QJsonParseError::NoError && rdoc.isObject()) {
            responseObj = rdoc.object();
        }
    }

    reply->deleteLater();
    return responseObj;
}

bool AITrainer::isDockerAvailable() const
{
    QProcess process;
    process.start("docker", QStringList() << "--version");
    process.waitForFinished(5000);
    return process.exitCode() == 0;
}

void AITrainer::setError(const QString& error)
{
    lastError = error;
    qWarning() << "AITrainer Error:" << error;
}

void AITrainer::onDockerProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus)
    qDebug() << "Docker 프로세스 완료, 종료 코드:" << exitCode;
}

void AITrainer::onDockerProcessError(QProcess::ProcessError error)
{
    qWarning() << "Docker 프로세스 오류:" << error;
}
