// 중복 및 오류 함수들 완전 제거

#include "RecipeManager.h"
#include "CameraView.h"
#include "CommonDefs.h"
#include "LanguageManager.h"
#include "TeachingWidget.h"
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QTreeWidget>
#include <QBuffer>
#include <QDomDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QDateTime>
#include <QPixmap>
#include <QImageReader>

RecipeManager::RecipeManager() {
}

// 이미지를 레시피 폴더로 복사하고 상대경로 반환
QString RecipeManager::copyImageToRecipeFolder(const QString& originalPath, const QString& recipeName) {
    if (originalPath.isEmpty() || recipeName.isEmpty()) {
        return originalPath;
    }
    // Validate recipeName to avoid accidental copies into a generic 'recipe' folder
    QString trimmedName = recipeName.trimmed();
    if (trimmedName.isEmpty()) {
        return originalPath;
    }
    // Common placeholder name used by some configs; treat as invalid to avoid surprises
    if (trimmedName.compare("recipe", Qt::CaseInsensitive) == 0) {
        return originalPath;
    }
    // Prevent directory traversal or malformed names
    if (trimmedName.contains('/') || trimmedName.contains('\\')) {
        return originalPath;
    }
    
    // 레시피 이미지 폴더 생성
    QString recipeTeachDir = QString("data/%1/teach").arg(recipeName);
    QDir dir;
    if (!dir.exists(recipeTeachDir)) {
        if (!dir.mkpath(recipeTeachDir)) {
            qDebug() << "Failed to create recipe images directory:" << recipeTeachDir;
            return originalPath; // 실패시 원래 경로 반환
        }
    }
    
    // 파일명 추출
    QFileInfo fileInfo(originalPath);
    QString fileName = fileInfo.fileName();
    QString targetPath = QString("%1/%2").arg(recipeTeachDir, fileName);
    
    // 파일이 이미 존재하면 덮어쓰기
    if (QFile::exists(targetPath)) {
        QFile::remove(targetPath);
    }
    
    // 파일 복사
    if (QFile::copy(originalPath, targetPath)) {
        return targetPath; // 상대경로 반환
    } else {
        qDebug() << "Failed to copy image from" << originalPath << "to" << targetPath;
        return originalPath; // 실패시 원래 경로 반환
    }
}

// 이미지 리스트를 레시피 폴더로 복사
QStringList RecipeManager::copyImagesToRecipeFolder(const QStringList& imagePaths, const QString& recipeName) {
    QStringList copiedPaths;
    for (const QString& imagePath : imagePaths) {
        QString copiedPath = copyImageToRecipeFolder(imagePath, recipeName);
        copiedPaths.append(copiedPath);
    }
    return copiedPaths;
}

bool RecipeManager::saveRecipe(const QString& fileName, 
                              const QVector<CameraInfo>& cameraInfos, 
                              int currentCameraIndex,
                              const QMap<QString, CalibrationInfo>& calibrationMap,
                              CameraView* cameraView,
                              const QStringList& simulationImagePaths,
                              int simulationCurrentIndex,
                              const QStringList& trainingImagePaths,
                              TeachingWidget* teachingWidget) {
    
    // 파일 경로의 디렉토리가 존재하는지 확인하고 생성
    QFileInfo fileInfo(fileName);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            setError(QString("디렉토리를 생성할 수 없습니다: %1").arg(dir.absolutePath()));
            return false;
        }
    }
    
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(QString("파일을 열 수 없습니다: %1").arg(fileName));
        return false;
    }
    
    QVector<CameraInfo> actualCameraInfos = cameraInfos;
    
    // 시뮬레이션 모드 감지 (cameraInfos가 비어있고 cameraView에 현재 카메라 UUID가 있는 경우)
    if (actualCameraInfos.isEmpty() && cameraView && !cameraView->getCurrentCameraUuid().isEmpty()) {
        QString currentUuid = cameraView->getCurrentCameraUuid();
        
        // 시뮬레이션 카메라 정보 자동 생성
        CameraInfo simCameraInfo;
        simCameraInfo.index = 0;
        simCameraInfo.name = currentUuid;
        simCameraInfo.uniqueId = currentUuid;
        simCameraInfo.locationId = "SIMULATION";
        simCameraInfo.vendorId = "SIM_VENDOR";
        simCameraInfo.productId = "SIM_PRODUCT";
        simCameraInfo.isConnected = true;
        
        // 시뮬레이션 데이터를 JSON으로 생성
        QJsonObject projectData;
        projectData["projectName"] = currentUuid;
        projectData["createdTime"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        
        // 매개변수로 받은 시뮬레이션 이미지 경로들을 우선 사용
        QJsonArray imagePaths;
        int imageCount = 0;
        int currentIndex = 0;
        
        if (!simulationImagePaths.isEmpty()) {
            // 레시피 이름 추출 (파일명에서 .xml 제거)
            QFileInfo recipeFileInfo(fileName);
            QString recipeName = recipeFileInfo.baseName();
            
            // 이미지들을 레시피 폴더로 복사하고 상대경로 획득
            QStringList copiedImagePaths = copyImagesToRecipeFolder(simulationImagePaths, recipeName);
            
            // 복사된 경로들을 JSON에 추가
            for (const QString& imagePath : copiedImagePaths) {
                imagePaths.append(imagePath);
            }
            imageCount = copiedImagePaths.size();
            currentIndex = simulationCurrentIndex;
        } else {
            // 매개변수가 없으면 기존 recipe.config에서 시뮬레이션 데이터를 읽어오기 시도
            if (QFile::exists(fileName)) {
                QFile existingFile(fileName);
                if (existingFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QXmlStreamReader existingXml(&existingFile);
                    while (!existingXml.atEnd()) {
                        existingXml.readNext();
                        if (existingXml.isStartElement() && existingXml.name() == QLatin1String("Camera")) {
                            QString existingUuid = existingXml.attributes().value("uuid").toString();
                            if (existingUuid == currentUuid) {
                                // 같은 UUID의 카메라를 찾았으면 시뮬레이션 데이터 읽기
                                while (!existingXml.atEnd() && !(existingXml.isEndElement() && existingXml.name() == QLatin1String("Camera"))) {
                                    existingXml.readNext();
                                    if (existingXml.isStartElement() && existingXml.name() == QLatin1String("simulationData")) {
                                        QString existingSimData = existingXml.readElementText();
                                        QJsonDocument existingDoc = QJsonDocument::fromJson(existingSimData.toUtf8());
                                        if (!existingDoc.isNull() && existingDoc.isObject()) {
                                            QJsonObject existingData = existingDoc.object();
                                            if (existingData.contains("imagePaths")) {
                                                imagePaths = existingData["imagePaths"].toArray();
                                                imageCount = existingData["imageCount"].toInt();
                                                currentIndex = existingData["currentIndex"].toInt();
                                            }
                                        }
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    }
                    existingFile.close();
                }
            }
        }
        
        projectData["imageCount"] = imageCount;
        projectData["currentIndex"] = currentIndex;
        projectData["imagePaths"] = imagePaths;
        
        // 학습 이미지 경로 추가
        QJsonArray trainingPaths;
        if (!trainingImagePaths.isEmpty()) {
            // 레시피 이름 추출
            QFileInfo recipeFileInfo(fileName);
            QString recipeName = recipeFileInfo.baseName();
            
            // 학습 이미지들을 레시피 폴더로 복사하고 상대경로 획득
            QStringList copiedTrainingPaths = copyImagesToRecipeFolder(trainingImagePaths, recipeName);
            
            for (const QString& trainingPath : copiedTrainingPaths) {
                trainingPaths.append(trainingPath);
            }
        }
        projectData["trainingImagePaths"] = trainingPaths;
        
        QJsonDocument jsonDoc(projectData);
        simCameraInfo.serialNumber = QString::fromUtf8(jsonDoc.toJson(QJsonDocument::Compact));
        
        actualCameraInfos.append(simCameraInfo);
    }
    
    if (actualCameraInfos.isEmpty()) {
        setError("카메라 정보가 없습니다.");
        file.close();
        return false;
    }
    
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    
    // 레시피 파일명에서 레시피 이름 추출
    QFileInfo recipeFileInfo(fileName);
    QString recipeName = recipeFileInfo.baseName();
    
    // 레시피 루트 엘리먼트
    xml.writeStartElement("Recipe");
    xml.writeAttribute("name", recipeName);
    xml.writeAttribute("version", "1.0");
    xml.writeAttribute("createdTime", QDateTime::currentDateTime().toString(Qt::ISODate));
    
    // 카메라들 컨테이너
    xml.writeStartElement("Cameras");
    
    int savedCameraCount = 0;
    
    
    // 모든 연결된 카메라에 대해 레시피 저장 (최대 2대)
    for (int camIdx = 0; camIdx < actualCameraInfos.size(); camIdx++) {
        
        QList<PatternInfo> allPatterns;
        int patternCount = 0;
        
        // CameraView가 있는 경우에만 패턴 확인
        if (cameraView != nullptr) {
            allPatterns = cameraView->getPatterns();
            
            // 이 카메라에 할당된 패턴 수 확인
            for (const PatternInfo& pattern : allPatterns) {
                if (pattern.cameraUuid == actualCameraInfos[camIdx].uniqueId) {
                    patternCount++;
                } else {
                }
            }
        }
        
        // 시뮬레이션 카메라는 패턴이 없어도 저장
        bool isSimulationCamera = (actualCameraInfos[camIdx].locationId == "SIMULATION" || 
                                  actualCameraInfos[camIdx].serialNumber == "SIM_SERIAL" ||
                                  actualCameraInfos[camIdx].uniqueId.startsWith("SIM_") ||
                                  actualCameraInfos[camIdx].uniqueId.isEmpty());
        
        // 현재 선택된 카메라는 패턴이 없어도 저장
        bool isCurrentCamera = (camIdx == currentCameraIndex);
        
        // 티칭 이미지가 있는지 확인 (frameIndex = camIdx 직접 매핑)
        bool hasTeachingImages = false;
        if (teachingWidget) {
            int frameIndex = camIdx;  // 프레임 번호를 카메라 인덱스와 1:1 매핑
            hasTeachingImages = (frameIndex < static_cast<int>(teachingWidget->cameraFrames.size()) &&
                                !teachingWidget->cameraFrames[frameIndex].empty());
        }
        
        if (patternCount == 0 && !isSimulationCamera && !isCurrentCamera && !hasTeachingImages) {
            qDebug() << QString("카메라 '%1' 건너뜀 (패턴 없음, 티칭 이미지 없음)").arg(actualCameraInfos[camIdx].uniqueId);
            continue;
        }
        
        // 카메라 정보 저장
        xml.writeStartElement("Camera");
        xml.writeAttribute("uuid", actualCameraInfos[camIdx].uniqueId);
        xml.writeAttribute("serialNumber", actualCameraInfos[camIdx].serialNumber);
        xml.writeAttribute("imageIndex", QString::number(actualCameraInfos[camIdx].imageIndex));
        
        // 먼저 width, height 속성 추가 (이미지 크기 정보)
        cv::Mat sizeCheckImage;
        if (teachingWidget && camIdx >= 0 && camIdx < static_cast<int>(teachingWidget->cameraFrames.size()) && 
            !teachingWidget->cameraFrames[camIdx].empty()) {
            sizeCheckImage = teachingWidget->cameraFrames[camIdx];
        } else if (teachingWidget) {
            sizeCheckImage = teachingWidget->getCurrentFrame();
        }
        
        if (!sizeCheckImage.empty()) {
            xml.writeAttribute("width", QString::number(sizeCheckImage.cols));
            xml.writeAttribute("height", QString::number(sizeCheckImage.rows));
        } else {
            xml.writeAttribute("width", "");
            xml.writeAttribute("height", "");
        }
        
        // 카메라별 티칭 이미지 정보 추가 (cameraFrames 기반)
        if (teachingWidget) {
            // frameIndex = camIdx (1:1 매핑)
            int frameIndex = camIdx;
            
            bool hasImage = frameIndex < static_cast<int>(teachingWidget->cameraFrames.size()) &&
                           !teachingWidget->cameraFrames[frameIndex].empty();
            
            // 해당 프레임의 이미지 저장
            if (hasImage) {
                std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 95};
                std::vector<uchar> buffer;
                cv::Mat frameImage = teachingWidget->cameraFrames[frameIndex];
                
                if (cv::imencode(".jpg", frameImage, buffer, params)) {
                    QByteArray imageData(reinterpret_cast<const char*>(buffer.data()), buffer.size());
                    QString imageBase64 = imageData.toBase64();
                    
                    xml.writeStartElement("TeachingImage");
                    xml.writeAttribute("imageIndex", QString::number(frameIndex));
                    xml.writeAttribute("name", QString("Frame_%1").arg(frameIndex));
                    xml.writeAttribute("width", QString::number(frameImage.cols));
                    xml.writeAttribute("height", QString::number(frameImage.rows));
                    xml.writeCharacters(imageBase64);
                    xml.writeEndElement(); // TeachingImage
                    
                    qDebug() << QString("카메라 %1 이미지 저장 (frameIndex=%2, 크기: %3 chars, 해상도: %4x%5)")
                                .arg(camIdx).arg(frameIndex).arg(imageBase64.size())
                                .arg(frameImage.cols).arg(frameImage.rows);
                }
            } else {
                // STRIP/CRIMP 이미지가 없으면 기존 방식으로 저장
                cv::Mat currentImage;
                
                // 해당 카메라의 cameraFrames에서 이미지 가져오기
                if (camIdx >= 0 && camIdx < static_cast<int>(teachingWidget->cameraFrames.size()) && 
                    !teachingWidget->cameraFrames[camIdx].empty()) {
                    currentImage = teachingWidget->cameraFrames[camIdx];
                } else {
                    // cameraFrames에 없으면 현재 프레임 사용 (fallback)
                    currentImage = teachingWidget->getCurrentFrame();
                }
                
                if (!currentImage.empty()) {
                    std::vector<uchar> buffer;
                    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 95};
                    
                    if (cv::imencode(".jpg", currentImage, buffer, params)) {
                        QByteArray imageData(reinterpret_cast<const char*>(buffer.data()), buffer.size());
                        QString teachingImageBase64 = imageData.toBase64();
                        xml.writeAttribute("teachingImage", teachingImageBase64);
                        
                        qDebug() << QString("카메라 '%1'의 프레임을 Base64로 저장 (크기: %2 chars, 해상도: %3x%4)")
                                    .arg(actualCameraInfos[camIdx].uniqueId).arg(teachingImageBase64.size())
                                    .arg(currentImage.cols).arg(currentImage.rows);
                    } else {
                        // 인코딩 실패시 기본 파일명으로 저장
                        QString teachingImageName = QString("%1.jpg").arg(actualCameraInfos[camIdx].uniqueId);
                        xml.writeAttribute("teachingImage", teachingImageName);
                        qDebug() << QString("이미지 인코딩 실패, 파일명으로 저장: %1").arg(teachingImageName);
                    }
                } else {
                    // 현재 이미지가 없으면 기본 파일명으로 저장
                    QString teachingImageName = QString("%1.jpg").arg(actualCameraInfos[camIdx].uniqueId);
                    xml.writeAttribute("teachingImage", teachingImageName);
                    qDebug() << QString("현재 이미지 없음, 파일명으로 저장: %1").arg(teachingImageName);
                }
            }
        } else {
            // teachingWidget이 없으면 기본 파일명으로 저장
            QString teachingImageName = QString("%1.jpg").arg(actualCameraInfos[camIdx].uniqueId);
            xml.writeAttribute("teachingImage", teachingImageName);
        }
        
        
        // 캘리브레이션 정보 저장
        QString currentCameraUuid = actualCameraInfos[camIdx].uniqueId;
        CalibrationInfo calibInfo;
        if (calibrationMap.contains(currentCameraUuid)) {
            calibInfo = calibrationMap[currentCameraUuid];
        }
        
        if (calibInfo.isCalibrated) {
            writeCalibrationInfo(xml, calibInfo);
        }
        
        // 카메라 설정 저장
        writeCameraSettings(xml, actualCameraInfos[camIdx]);
        
        // 패턴 저장 시작
        xml.writeStartElement("Patterns");
        
        // 패턴 저장 (CameraView가 있고 패턴이 있는 경우에만)
        if (cameraView != nullptr && patternCount > 0) {
            
            QList<QUuid> processedPatterns;
            
            // 1. ROI 패턴들 저장
            writeROIPatterns(xml, allPatterns, currentCameraUuid, processedPatterns);
            
            // 2. FID 패턴들 저장
            writeFIDPatterns(xml, allPatterns, currentCameraUuid, processedPatterns);
            
            // 3. 독립 패턴들 저장
            writeIndependentPatterns(xml, allPatterns, currentCameraUuid, processedPatterns);
            
        } else {
        }
        
        xml.writeEndElement(); // Patterns
        
        
        xml.writeEndElement(); // Camera
        savedCameraCount++;
        
    }
    
    xml.writeEndElement(); // Cameras
    xml.writeEndElement(); // Recipe
    xml.writeEndDocument();
    file.close();
    
    
    return savedCameraCount > 0;
}

bool RecipeManager::loadRecipe(const QString& fileName,
                              QVector<CameraInfo>& cameraInfos,
                              QMap<QString, CalibrationInfo>& calibrationMap,
                              CameraView* cameraView,
                              QTreeWidget* patternTree,
                              std::function<void(const QStringList&)> trainingImageCallback,
                              TeachingWidget* teachingWidget) {
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString errorMsg = QString("레시피 파일을 열 수 없습니다: %1 (에러: %2)").arg(fileName).arg(file.errorString());
        qDebug() << errorMsg;
        setError(errorMsg);
        
        // 파일이 없으면 config.xml의 최근 레시피 경로를 비움
        ConfigManager::instance()->setLastRecipePath("");
        ConfigManager::instance()->saveConfig();
        qDebug() << "[RecipeManager] config.xml의 최근 레시피 경로를 비웠습니다.";
        
        return false;
    }
    
    QXmlStreamReader xml(&file);
    
    // 기존 패턴 삭제 (UI 구성요소가 있는 경우에만)
    if (cameraView) {
        cameraView->clearPatterns();
    }
    if (patternTree) {
        patternTree->clear();
    }
    
    // 관계 정보 저장용 맵
    QMap<QString, QStringList> childrenMap;
    QMap<QString, QTreeWidgetItem*> itemMap;
    
    int totalLoadedPatterns = 0;
    QString loadedCameraNames;
    
    try {
        // XML 루트 요소 확인
        if (!xml.readNextStartElement()) {
            throw QString("XML 문서가 비어있거나 유효하지 않습니다.");
        }
        
        if (xml.name() != QLatin1String("Recipe")) {
            throw QString(QString("유효하지 않은 레시피 파일 형식입니다. 루트 요소: %1").arg(xml.name().toString()));
        }
        
        // 시뮬레이션 모드일 때 기존 카메라 정보 초기화
        bool isSimulationMode = false;
        for (const auto& cameraInfo : cameraInfos) {
            if (cameraInfo.locationId == "SIMULATION" || 
                cameraInfo.uniqueId.startsWith("SIM_") ||
                cameraInfo.serialNumber == "SIM_SERIAL") {
                isSimulationMode = true;
                break;
            }
        }
        
        if (isSimulationMode) {
            // 시뮬레이션 모드에서는 기존 cameraInfos를 비우고 레시피에서 읽은 정보로 채움
            cameraInfos.clear();
        }
        
        // Recipe 내부에서 Cameras 태그 찾기
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("Cameras")) {
                // 각 카메라별 레시피 읽기
                while (xml.readNextStartElement()) {
                    if (xml.name() == QLatin1String("Camera")) {
                        if (!readCameraSection(xml, cameraInfos, calibrationMap, cameraView, patternTree,
                                             childrenMap, itemMap, totalLoadedPatterns, loadedCameraNames, trainingImageCallback, teachingWidget)) {
                        }
                    } else {
                        xml.skipCurrentElement();
                    }
                }
            } else {
                xml.skipCurrentElement();
            }
        }
        
        // 부모-자식 관계 복원
        restorePatternRelationships(childrenMap, itemMap, cameraView);
        
        // 트리 확장 (UI가 있는 경우에만)
        if (patternTree) {
            patternTree->expandAll();
        }
        
        // XML 파싱 에러 확인
        if (xml.hasError()) {
            QString xmlError = QString("XML 파싱 에러: %1 (라인 %2, 컬럼 %3)")
                                .arg(xml.errorString()).arg(xml.lineNumber()).arg(xml.columnNumber());
            qDebug() << xmlError;
            setError(xmlError);
            file.close();
            return false;
        }
        
    } catch (const QString& error) {
        qDebug() << QString("레시피 로드 중 예외 발생: %1").arg(error);
        setError(error);
        file.close();
        return false;
    }
    
    file.close();
    
    // 모든 Camera 로드 완료 후, teachingWidget에 저장된 이미지를 현재 모드에 맞게 cameraFrames에 설정
    // ★ CAM OFF 상태에서만 cameraFrames 및 stripModeImage/crimpModeImage 설정
    if (teachingWidget && teachingWidget->camOff) {
        // Strip/Crimp 모드 이미지 분리 제거됨
        
        // ★ cameraFrames를 크기 2로 초기화 (STRIP=0, CRIMP=1)
        if (teachingWidget->cameraFrames.size() < 2) {
        }
        
        // Strip/Crimp 이미지 분리 제거됨
    } else if (teachingWidget && !teachingWidget->camOff) {
        // CAM ON 상태 - cameraFrames 설정 건너뜀
    }
    
    // base64 티칭 이미지가 로드된 경우 trainingImageCallback 호출
    // (현재 모드에 맞는 이미지는 위에서 이미 설정하고 콜백 호출했으므로 여기서는 건너뜀)
    /*
    if (trainingImageCallback && teachingWidget && !teachingWidget->cameraFrames.empty()) {
        QStringList imagePaths;
        // 더미 경로를 생성해서 콜백 호출 (실제 파일은 없지만 cameraFrames에 이미지가 있음)
        for (int i = 0; i < static_cast<int>(teachingWidget->cameraFrames.size()); i++) {
            if (!teachingWidget->cameraFrames[i].empty()) {
                imagePaths.append(QString("base64_image_%1").arg(i));
            }
        }
        
        if (!imagePaths.isEmpty()) {
            qDebug() << QString("base64 티칭 이미지 로드 완료 - trainingImageCallback 호출: %1개 이미지").arg(imagePaths.size());
            trainingImageCallback(imagePaths);
        }
    }
    */
    
    // 패턴이 없어도 티칭 이미지가 로드되었으면 성공으로 간주
    bool hasTeachingImages = teachingWidget && !teachingWidget->cameraFrames.empty();
    return totalLoadedPatterns > 0 || hasTeachingImages;
}

// === 저장 관련 함수들 구현 ===

void RecipeManager::writeCalibrationInfo(QXmlStreamWriter& xml, const CalibrationInfo& calibInfo) {
    xml.writeStartElement("Calibration");
    xml.writeAttribute("pixelToMmRatio", QString::number(calibInfo.pixelToMmRatio, 'f', 8));
    xml.writeAttribute("realWorldLength", QString::number(calibInfo.realWorldLength, 'f', 3));
    xml.writeAttribute("rectX", QString::number(calibInfo.calibrationRect.x()));
    xml.writeAttribute("rectY", QString::number(calibInfo.calibrationRect.y()));
    xml.writeAttribute("rectW", QString::number(calibInfo.calibrationRect.width()));
    xml.writeAttribute("rectH", QString::number(calibInfo.calibrationRect.height()));
    xml.writeEndElement();
}

void RecipeManager::writeCameraSettings(QXmlStreamWriter& xml, const CameraInfo& cameraInfo) {
    // 시뮬레이션 카메라 판단: locationId가 "SIMULATION"이거나 uniqueId가 "SIM_"로 시작하는 경우
    bool isSimulationCamera = (cameraInfo.locationId == "SIMULATION" || 
                              cameraInfo.uniqueId.startsWith("SIM_") ||
                              cameraInfo.uniqueId.isEmpty());
    
    if (isSimulationCamera) {
        // 시뮬레이션 카메라
        xml.writeStartElement("videoDeviceIndex");
        xml.writeCharacters("-1");
        xml.writeEndElement();
        
        xml.writeStartElement("deviceId");
        xml.writeCharacters("SIMULATION");
        xml.writeEndElement();
        
        xml.writeStartElement("uniqueId");
        xml.writeCharacters(cameraInfo.uniqueId);
        xml.writeEndElement();
        
        // serialNumber에 저장된 JSON 데이터를 simulationData로 저장
        if (!cameraInfo.serialNumber.isEmpty() && cameraInfo.serialNumber != "SIM_SERIAL") {
            xml.writeStartElement("simulationData");
            xml.writeCharacters(cameraInfo.serialNumber);
            xml.writeEndElement();
        }
    } else {
        // 실제 카메라 (Spinnaker 또는 OpenCV)
        // videoDeviceIndex 제거됨 (Spinnaker SDK만 사용)
        
        xml.writeStartElement("deviceId");
        xml.writeCharacters(cameraInfo.uniqueId);  // Spinnaker UUID 또는 OpenCV device ID
        xml.writeEndElement();
        
        xml.writeStartElement("uniqueId");
        xml.writeCharacters(cameraInfo.uniqueId);
        xml.writeEndElement();
        
        // OpenCV capture 제거됨 (Spinnaker SDK만 사용)
        // 카메라 설정은 Spinnaker UserSet으로 관리됨
    }
}

void RecipeManager::writeROIPatterns(QXmlStreamWriter& xml, const QList<PatternInfo>& allPatterns, 
                                    const QString& cameraUuid, QList<QUuid>& processedPatterns) {
    int roiCount = 0;
    
    for (const PatternInfo& pattern : allPatterns) {
        
        if (pattern.cameraUuid == cameraUuid && 
            pattern.type == PatternType::ROI && 
            pattern.parentId.isNull() &&
            !processedPatterns.contains(pattern.id)) {
            
            roiCount++;
            
            writePatternHeader(xml, pattern);
            if (!pattern.childIds.isEmpty()) {
                xml.writeAttribute("groupType", "ROI_GROUP");
            }
            
            writePatternRect(xml, pattern);  // 여기서 호출됨
            writeROIDetails(xml, pattern);
            writePatternFilters(xml, pattern);
            writePatternChildren(xml, pattern, allPatterns, processedPatterns);
            
            xml.writeEndElement(); // Pattern
            processedPatterns.append(pattern.id);
        }
    }
}

void RecipeManager::writeFIDPatterns(QXmlStreamWriter& xml, const QList<PatternInfo>& allPatterns, 
                                    const QString& cameraUuid, QList<QUuid>& processedPatterns) {
    int fidCount = 0;
    
    for (const PatternInfo& pattern : allPatterns) {
        
        if (pattern.cameraUuid == cameraUuid && 
            pattern.type == PatternType::FID && 
            pattern.parentId.isNull() &&
            !processedPatterns.contains(pattern.id)) {
            
            fidCount++;
            
            writePatternHeader(xml, pattern);
            if (!pattern.childIds.isEmpty()) {
                xml.writeAttribute("groupType", "FID_GROUP");
            }
            
            writePatternRect(xml, pattern);  // 여기서 호출됨
            writeFIDDetails(xml, pattern);
            writePatternFilters(xml, pattern);
            writePatternChildren(xml, pattern, allPatterns, processedPatterns);
            
            xml.writeEndElement(); // Pattern
            processedPatterns.append(pattern.id);
        }
    }
}

void RecipeManager::writeIndependentPatterns(QXmlStreamWriter& xml, const QList<PatternInfo>& allPatterns, 
                                           const QString& cameraUuid, QList<QUuid>& processedPatterns) {
    int independentCount = 0;
    
    for (const PatternInfo& pattern : allPatterns) {
        
        if (pattern.cameraUuid == cameraUuid && 
            pattern.parentId.isNull() &&
            !processedPatterns.contains(pattern.id)) {
            
            independentCount++;
            
            writePatternHeader(xml, pattern);
            writePatternRect(xml, pattern);  // 여기서 호출됨
            
            // 패턴 타입별 세부 정보 저장
            if (pattern.type == PatternType::INS) {
                writeINSDetails(xml, pattern);
            } else if (pattern.type == PatternType::FIL) {
                // Filter 타입 처리 (필요시 추가)
            }
            
            writePatternFilters(xml, pattern);
            writePatternChildren(xml, pattern, allPatterns, processedPatterns);
            
            xml.writeEndElement(); // Pattern
            processedPatterns.append(pattern.id);
        }
    }
}

void RecipeManager::writePatternHeader(QXmlStreamWriter& xml, const PatternInfo& pattern) {
    
    xml.writeStartElement("Pattern");
    xml.writeAttribute("id", pattern.id.toString());
    xml.writeAttribute("name", pattern.name);
    
    switch (pattern.type) {
        case PatternType::ROI: xml.writeAttribute("type", "ROI"); break;
        case PatternType::FID: xml.writeAttribute("type", "FID"); break;
        case PatternType::INS: xml.writeAttribute("type", "INS"); break;
        case PatternType::FIL: xml.writeAttribute("type", "Filter"); break;
    }
    
    xml.writeAttribute("color", pattern.color.name());
    if (!pattern.enabled) xml.writeAttribute("enabled", "false");
    
    // 부모 패턴 ID 저장
    if (!pattern.parentId.isNull()) {
        xml.writeAttribute("parentId", pattern.parentId.toString());
    }
    
}

void RecipeManager::writePatternRect(QXmlStreamWriter& xml, const PatternInfo& pattern) {
    
    xml.writeStartElement("Rect");
    xml.writeAttribute("x", QString::number(pattern.rect.x(), 'f', 2));
    xml.writeAttribute("y", QString::number(pattern.rect.y(), 'f', 2));
    xml.writeAttribute("width", QString::number(pattern.rect.width(), 'f', 2));
    xml.writeAttribute("height", QString::number(pattern.rect.height(), 'f', 2));
    xml.writeAttribute("angle", QString::number(pattern.angle, 'f', 2));
    xml.writeAttribute("frameIndex", QString::number(pattern.frameIndex));
    
    xml.writeEndElement();
    
}

void RecipeManager::writeROIDetails(QXmlStreamWriter& xml, const PatternInfo& pattern) {
    xml.writeStartElement("ROIDetails");
    // includeAllCamera 제거됨
    xml.writeEndElement();
}

void RecipeManager::writeFIDDetails(QXmlStreamWriter& xml, const PatternInfo& pattern) {
    xml.writeStartElement("FIDDetails");
    xml.writeAttribute("matchThreshold", QString::number(pattern.matchThreshold));
    if (pattern.useRotation) xml.writeAttribute("useRotation", "true");
    xml.writeAttribute("minAngle", QString::number(pattern.minAngle));
    xml.writeAttribute("maxAngle", QString::number(pattern.maxAngle));
    xml.writeAttribute("angleStep", QString::number(pattern.angleStep));
    xml.writeAttribute("matchMethod", QString::number(pattern.fidMatchMethod));
    if (pattern.runInspection) xml.writeAttribute("runInspection", "true");
    
    // 패턴의 실제 회전 각도 저장 (사용자가 회전시킨 각도)
    xml.writeAttribute("patternAngle", QString::number(pattern.angle, 'f', 2));
    
    if (!pattern.templateImage.isNull()) {
        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        // BMP 포맷 사용 (손실 없음, PNG보다 빠름)
        pattern.templateImage.save(&buffer, "BMP");
        xml.writeAttribute("templateImage", ba.toBase64());
    }
    
    // ★ FID matchTemplate 저장 (RGB32 포맷 - 매칭용)
    if (!pattern.matchTemplate.isNull())
    {
        QByteArray matchBa;
        QBuffer matchBuffer(&matchBa);
        matchBuffer.open(QIODevice::WriteOnly);
        pattern.matchTemplate.save(&matchBuffer, "BMP");
        xml.writeAttribute("matchTemplate", matchBa.toBase64());
    }
    
    // ★ FID matchTemplateMask 저장
    if (!pattern.matchTemplateMask.isNull())
    {
        QByteArray maskBa;
        QBuffer maskBuffer(&maskBa);
        maskBuffer.open(QIODevice::WriteOnly);
        pattern.matchTemplateMask.save(&maskBuffer, "BMP");
        xml.writeAttribute("matchTemplateMask", maskBa.toBase64());
    }
    
    xml.writeEndElement();
}

void RecipeManager::writeINSDetails(QXmlStreamWriter& xml, const PatternInfo& pattern) {
    xml.writeStartElement("INSDetails");
    xml.writeAttribute("inspectionMethod", QString::number(pattern.inspectionMethod));
    
    xml.writeAttribute("passThreshold", QString::number(pattern.passThreshold));
    xml.writeAttribute("ssimNgThreshold", QString::number(pattern.ssimNgThreshold));
    xml.writeAttribute("allowedNgRatio", QString::number(pattern.allowedNgRatio));
    xml.writeAttribute("anomalyMinBlobSize", QString::number(pattern.anomalyMinBlobSize));
    xml.writeAttribute("anomalyMinDefectWidth", QString::number(pattern.anomalyMinDefectWidth));
    xml.writeAttribute("anomalyMinDefectHeight", QString::number(pattern.anomalyMinDefectHeight));
    // invertResult 제거됨
    if (pattern.useRotation) xml.writeAttribute("useRotation", "true");
    xml.writeAttribute("minAngle", QString::number(pattern.minAngle));
    xml.writeAttribute("maxAngle", QString::number(pattern.maxAngle));
    xml.writeAttribute("angleStep", QString::number(pattern.angleStep));
    
    // 패턴의 실제 회전 각도 저장 (사용자가 회전시킨 각도)
    xml.writeAttribute("patternAngle", QString::number(pattern.angle, 'f', 2));
    
    // EDGE 검사 관련 속성 저장
    xml.writeAttribute("edgeEnabled", pattern.edgeEnabled ? "true" : "false");
    xml.writeAttribute("edgeOffsetX", QString::number(pattern.edgeOffsetX));
    xml.writeAttribute("stripEdgeBoxWidth", QString::number(pattern.stripEdgeBoxWidth));
    xml.writeAttribute("stripEdgeBoxHeight", QString::number(pattern.stripEdgeBoxHeight));
    xml.writeAttribute("edgeMaxOutliers", QString::number(pattern.edgeMaxOutliers));
    xml.writeAttribute("edgeDistanceMax", QString::number(pattern.edgeDistanceMax, 'f', 2));
    xml.writeAttribute("edgeStartPercent", QString::number(pattern.edgeStartPercent));
    xml.writeAttribute("edgeEndPercent", QString::number(pattern.edgeEndPercent));
    
    // STRIP 길이 캘리브레이션 관련 속성 저장
    xml.writeAttribute("stripLengthConversionMm", QString::number(pattern.stripLengthConversionMm, 'f', 3));
    xml.writeAttribute("stripLengthCalibrationPx", QString::number(pattern.stripLengthCalibrationPx, 'f', 2));
    xml.writeAttribute("stripLengthCalibrated", pattern.stripLengthCalibrated ? "true" : "false");
    xml.writeAttribute("stripLengthMin", QString::number(pattern.stripLengthMin, 'f', 2));
    xml.writeAttribute("stripLengthMax", QString::number(pattern.stripLengthMax, 'f', 2));
    xml.writeAttribute("stripLengthEnabled", pattern.stripLengthEnabled ? "true" : "false");
    
    // STRIP FRONT/REAR 활성화 여부 저장
    xml.writeAttribute("stripFrontEnabled", pattern.stripFrontEnabled ? "true" : "false");
    xml.writeAttribute("stripRearEnabled", pattern.stripRearEnabled ? "true" : "false");
    
    // STRIP 두께 검사 관련 속성 저장
    xml.writeAttribute("stripThicknessMin", QString::number(pattern.stripThicknessMin, 'f', 2));
    xml.writeAttribute("stripThicknessMax", QString::number(pattern.stripThicknessMax, 'f', 2));
    xml.writeAttribute("stripRearThicknessMin", QString::number(pattern.stripRearThicknessMin, 'f', 2));
    xml.writeAttribute("stripRearThicknessMax", QString::number(pattern.stripRearThicknessMax, 'f', 2));
    
    // STRIP Gradient 시작/끝 지점 및 두께 박스 크기 저장
    xml.writeAttribute("stripGradientStartPercent", QString::number(pattern.stripGradientStartPercent));
    xml.writeAttribute("stripGradientEndPercent", QString::number(pattern.stripGradientEndPercent));
    xml.writeAttribute("stripThicknessBoxWidth", QString::number(pattern.stripThicknessBoxWidth));
    xml.writeAttribute("stripThicknessBoxHeight", QString::number(pattern.stripThicknessBoxHeight));
    xml.writeAttribute("stripRearThicknessBoxWidth", QString::number(pattern.stripRearThicknessBoxWidth));
    xml.writeAttribute("stripRearThicknessBoxHeight", QString::number(pattern.stripRearThicknessBoxHeight));
    
    // BARREL LEFT 검사 파라미터 저장
    xml.writeAttribute("barrelLeftStripEnabled", pattern.barrelLeftStripEnabled ? "true" : "false");
    xml.writeAttribute("barrelLeftStripOffsetX", QString::number(pattern.barrelLeftStripOffsetX));
    xml.writeAttribute("barrelLeftStripBoxWidth", QString::number(pattern.barrelLeftStripBoxWidth));
    xml.writeAttribute("barrelLeftStripBoxHeight", QString::number(pattern.barrelLeftStripBoxHeight));
    xml.writeAttribute("barrelLeftStripLengthMin", QString::number(pattern.barrelLeftStripLengthMin, 'f', 3));
    xml.writeAttribute("barrelLeftStripLengthMax", QString::number(pattern.barrelLeftStripLengthMax, 'f', 3));
    
    // BARREL RIGHT 검사 파라미터 저장
    xml.writeAttribute("barrelRightStripEnabled", pattern.barrelRightStripEnabled ? "true" : "false");
    xml.writeAttribute("barrelRightStripOffsetX", QString::number(pattern.barrelRightStripOffsetX));
    xml.writeAttribute("barrelRightStripBoxWidth", QString::number(pattern.barrelRightStripBoxWidth));
    xml.writeAttribute("barrelRightStripBoxHeight", QString::number(pattern.barrelRightStripBoxHeight));
    xml.writeAttribute("barrelRightStripLengthMin", QString::number(pattern.barrelRightStripLengthMin, 'f', 3));
    xml.writeAttribute("barrelRightStripLengthMax", QString::number(pattern.barrelRightStripLengthMax, 'f', 3));
    
    // 템플릿 이미지 저장 (DIFF용 기본 템플릿)
    if (!pattern.templateImage.isNull()) {
        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        pattern.templateImage.save(&buffer, "BMP");
        xml.writeAttribute("templateImage", ba.toBase64());
    }
    
    // 패턴 매칭 설정 저장
    xml.writeAttribute("patternMatchEnabled", pattern.patternMatchEnabled ? "true" : "false");
    xml.writeAttribute("patternMatchMethod", QString::number(pattern.patternMatchMethod));
    xml.writeAttribute("patternMatchThreshold", QString::number(pattern.patternMatchThreshold, 'f', 1));
    xml.writeAttribute("patternMatchUseRotation", pattern.patternMatchUseRotation ? "true" : "false");
    xml.writeAttribute("patternMatchMinAngle", QString::number(pattern.patternMatchMinAngle, 'f', 1));
    xml.writeAttribute("patternMatchMaxAngle", QString::number(pattern.patternMatchMaxAngle, 'f', 1));
    xml.writeAttribute("patternMatchAngleStep", QString::number(pattern.patternMatchAngleStep, 'f', 1));
    
    // 패턴 매칭용 템플릿 이미지 저장
    if (!pattern.matchTemplate.isNull()) {
        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        pattern.matchTemplate.save(&buffer, "BMP");
        xml.writeAttribute("matchTemplate", ba.toBase64());
    }
    
    // matchTemplateMask 저장
    if (!pattern.matchTemplateMask.isNull()) {
        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        pattern.matchTemplateMask.save(&buffer, "BMP");
        xml.writeAttribute("matchTemplateMask", ba.toBase64());
    }
    
    xml.writeEndElement();
}

void RecipeManager::writePatternFilters(QXmlStreamWriter& xml, const PatternInfo& pattern) {
    if (!pattern.filters.isEmpty()) {
        xml.writeStartElement("Filters");
        
        for (const FilterInfo& filter : pattern.filters) {
            xml.writeStartElement("Filter");
            xml.writeAttribute("type", QString::number(filter.type));
            xml.writeAttribute("enabled", filter.enabled ? "true" : "false");
            
            // 파라미터 저장
            for (auto it = filter.params.begin(); it != filter.params.end(); ++it) {
                xml.writeStartElement("Param");
                xml.writeAttribute("name", it.key());
                xml.writeAttribute("value", QString::number(it.value()));
                xml.writeEndElement();
            }
            
            xml.writeEndElement(); // Filter
        }
        
        xml.writeEndElement(); // Filters
    }
}

void RecipeManager::writePatternChildren(QXmlStreamWriter& xml, const PatternInfo& pattern, 
                                       const QList<PatternInfo>& allPatterns, 
                                       QList<QUuid>& processedPatterns) {
    // parentId가 현재 패턴의 ID인 자식 패턴들을 찾기
    QList<PatternInfo> childPatterns;
    for (const PatternInfo& childPattern : allPatterns) {
        if (childPattern.parentId == pattern.id) {
            childPatterns.append(childPattern);
        }
    }
    
    if (!childPatterns.isEmpty()) {
        xml.writeStartElement("ChildPatterns");
        
        for (const PatternInfo& childPattern : childPatterns) {
            // 자식 패턴을 처리됨으로 표시 (독립 패턴으로 저장되지 않도록)
            processedPatterns.append(childPattern.id);
            
            // 자식 패턴 전체 저장
            writePatternHeader(xml, childPattern);
            writePatternRect(xml, childPattern);
            
            // 자식 패턴 타입에 따라 세부 정보 저장
            if (childPattern.type == PatternType::ROI) {
                writeROIDetails(xml, childPattern);
            } else if (childPattern.type == PatternType::FID) {
                writeFIDDetails(xml, childPattern);
            } else if (childPattern.type == PatternType::INS) {
                writeINSDetails(xml, childPattern);
            }
            
            writePatternFilters(xml, childPattern);
            
            // 재귀적으로 손자 패턴들도 저장
            writePatternChildren(xml, childPattern, allPatterns, processedPatterns);
            
            xml.writeEndElement(); // Pattern
        }
        
        xml.writeEndElement(); // ChildPatterns
    }
}

bool RecipeManager::readCameraSection(QXmlStreamReader& xml,
                                     QVector<CameraInfo>& cameraInfos,
                                     QMap<QString, CalibrationInfo>& calibrationMap,
                                     CameraView* cameraView,
                                     QTreeWidget* patternTree,
                                     QMap<QString, QStringList>& childrenMap,
                                     QMap<QString, QTreeWidgetItem*>& itemMap,
                                     int& totalLoadedPatterns,
                                     QString& loadedCameraNames,
                                     std::function<void(const QStringList&)> trainingImageCallback,
                                     TeachingWidget* teachingWidget) {
    QString cameraUuid = xml.attributes().value("uuid").toString();
    QString imageIndexAttr = xml.attributes().value("imageIndex").toString();
    int imageIndex = imageIndexAttr.isEmpty() ? 0 : imageIndexAttr.toInt();
    
    if (cameraUuid.isEmpty()) {
        xml.skipCurrentElement();
        return false;
    }
    
    // 해당 카메라가 로드하려는 카메라 목록에 있는지 확인하고 CameraInfo 찾기
    CameraInfo* currentCameraInfo = nullptr;
    for (CameraInfo& info : cameraInfos) {
        if (info.uniqueId == cameraUuid) {
            currentCameraInfo = &info;
            break;
        }
    }
    
    // 카메라가 없으면 레시피에서 카메라 정보를 생성해서 추가
    if (!currentCameraInfo) {
        // serialNumber 우선, 없으면 name(구버전 호환성), 그것도 없으면 기본값
        QString cameraSerial = xml.attributes().value("serialNumber").toString();
        if (cameraSerial.isEmpty()) {
            cameraSerial = xml.attributes().value("name").toString(); // 구버전 호환
        }
        if (cameraSerial.isEmpty()) {
            cameraSerial = QString("Camera_%1").arg(cameraUuid);
        }
        
        CameraInfo newCameraInfo;
        newCameraInfo.uniqueId = cameraUuid;
        newCameraInfo.serialNumber = cameraSerial;
        newCameraInfo.imageIndex = imageIndex;  // 이미지 인덱스 설정
        
        // 인덱스 설정 - cameraInfos의 현재 크기를 사용
        newCameraInfo.index = cameraInfos.size();
        // videoDeviceIndex 제거됨
        
        // 시뮬레이션 모드 체크 - cameraInfos가 비어있다면 시뮬레이션 모드
        if (cameraInfos.isEmpty()) {
            newCameraInfo.isConnected = true;   // camOff 모드에서는 연결된 것으로 표시
            newCameraInfo.serialNumber = QString::number(newCameraInfo.index);
            
            // 레시피에서 시뮬레이션 데이터 읽기
            QString simulationData = xml.attributes().value("simulationData").toString();
            if (!simulationData.isEmpty()) {
                newCameraInfo.serialNumber = simulationData;
            }
        } else {
            newCameraInfo.isConnected = false;  // 라이브 모드에서는 실제 연결 상태에 따라
        }
        
        cameraInfos.append(newCameraInfo);
        currentCameraInfo = &cameraInfos.last();
    }
    
    int cameraPatternCount = 0;
    tempChildPatterns.clear(); // 임시 자식 패턴 리스트 초기화
    
    // 카메라의 teachingImage 속성 확인 (구 형식 호환)
    QString teachingImageAttr = xml.attributes().value("teachingImage").toString();
    
    if (!teachingImageAttr.isEmpty()) {
        // base64 데이터인 경우 디코딩해서 cameraFrames에 설정
        if (teachingImageAttr.startsWith("/9j/") || teachingImageAttr.length() > 100) {
            // base64 디코딩
            QByteArray imageData = QByteArray::fromBase64(teachingImageAttr.toLatin1());
            
            // OpenCV Mat으로 변환
            std::vector<uchar> buffer(imageData.begin(), imageData.end());
            cv::Mat teachingImage = cv::imdecode(buffer, cv::IMREAD_COLOR);
            
            if (!teachingImage.empty() && teachingWidget) {
                // 저장된 이미지 크기 정보 읽기
                QString widthAttr = xml.attributes().value("width").toString();
                QString heightAttr = xml.attributes().value("height").toString();
                int originalWidth = widthAttr.isEmpty() ? teachingImage.cols : widthAttr.toInt();
                int originalHeight = heightAttr.isEmpty() ? teachingImage.rows : heightAttr.toInt();
                
                // 현재 이미지 크기와 원본 크기가 다르면 리사이즈
                if (teachingImage.cols != originalWidth || teachingImage.rows != originalHeight) {
                    cv::resize(teachingImage, teachingImage, cv::Size(originalWidth, originalHeight));
                    qDebug() << QString("이미지 크기 복원: %1x%2 -> %3x%4")
                                .arg(teachingImage.cols).arg(teachingImage.rows)
                                .arg(originalWidth).arg(originalHeight);
                }
                
                // 해당 카메라의 인덱스 찾기
                int cameraIdx = -1;
                for (int i = 0; i < cameraInfos.size(); ++i) {
                    if (cameraInfos[i].uniqueId == cameraUuid) {
                        cameraIdx = i;
                        break;
                    }
                }
                
                if (cameraIdx >= 0) {
                    // cameraFrames에 직접 설정
                    if (cameraIdx >= static_cast<int>(teachingWidget->cameraFrames.size())) {
                    }
                    teachingWidget->cameraFrames[cameraIdx] = teachingImage.clone();
                    
                    qDebug() << QString("카메라 '%1' (인덱스 %2) base64 티칭 이미지를 cameraFrames에 직접 설정: %3x%4")
                                .arg(cameraUuid).arg(cameraIdx).arg(teachingImage.cols).arg(teachingImage.rows);
                }
            }
        }
    }
    
    try {
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("TeachingImage")) {
                // 새 형식: TeachingImage 요소로 이미지 읽기
                int imageIndex = xml.attributes().value("imageIndex").toInt();
                QString imageName = xml.attributes().value("name").toString();
                int width = xml.attributes().value("width").toInt();
                int height = xml.attributes().value("height").toInt();
                QString base64Data = xml.readElementText();
                
                // TeachingImage 발견 (로그 제거)
                
                if (!base64Data.isEmpty() && teachingWidget) {
                    // base64 디코딩
                    QByteArray imageData = QByteArray::fromBase64(base64Data.toLatin1());
                    std::vector<uchar> buffer(imageData.begin(), imageData.end());
                    cv::Mat teachingImage = cv::imdecode(buffer, cv::IMREAD_COLOR);
                    
                    if (!teachingImage.empty()) {
                        // 이미지 크기 복원
                        if (width > 0 && height > 0 && (teachingImage.cols != width || teachingImage.rows != height)) {
                            cv::resize(teachingImage, teachingImage, cv::Size(width, height));
                        }
                        
                        // ★ imageIndex를 직접 사용해서 cameraFrames에 저장
                        // imageIndex는 0,1,2,3 순차 프레임 인덱스
                        if (imageIndex >= 0 && imageIndex < 4) {
                            // cameraFrames 크기 확장
                            if (imageIndex >= static_cast<int>(teachingWidget->cameraFrames.size())) {
                            }
                            teachingWidget->cameraFrames[imageIndex] = teachingImage.clone();
                            // cameraFrames 저장 완료 (로그 제거)
                        } else {
                            qDebug() << QString("[RecipeManager] ✗ imageIndex=%1은 범위를 벗어남 (0-3만 허용)").arg(imageIndex);
                        }
                    } else {
                        qDebug() << "[RecipeManager] ✗ 이미지 디코딩 실패";
                    }
                } else {
                    if (base64Data.isEmpty()) {
                        qDebug() << "[RecipeManager] ✗ base64 데이터가 비어있음";
                    }
                    if (!teachingWidget) {
                        qDebug() << "[RecipeManager] ✗ teachingWidget이 nullptr";
                    }
                }
            }
            else if (xml.name() == QLatin1String("Calibration")) {
                CalibrationInfo calibInfo = readCalibrationInfo(xml);
                calibrationMap[cameraUuid] = calibInfo;
            }
            else if (xml.name() == QLatin1String("Patterns")) {
                // Patterns 컨테이너 내부의 Pattern들 읽기
                while (xml.readNextStartElement()) {
                    if (xml.name() == QLatin1String("Pattern")) {
                        // **부모 패턴 읽기**
                        PatternInfo pattern = readPattern(xml, cameraUuid);
                        
                        if (!pattern.id.isNull()) {
                            // **부모 패턴을 CameraView에 추가**
                            if (cameraView) {
                                QUuid addedId = cameraView->addPattern(pattern);
                                if (!addedId.isNull()) {
                                    // **트리 아이템 생성 (부모만)**
                                    if (patternTree) {
                                        QTreeWidgetItem* item = createPatternTreeItem(pattern);
                                        patternTree->addTopLevelItem(item);
                                        itemMap[pattern.id.toString()] = item;
                                    }
                                    
                                    cameraPatternCount++;
                                    totalLoadedPatterns++;
                                }
                            } else {
                                // UI 없이 패턴만 저장하는 경우
                                // CameraInfo에는 patterns 멤버가 없으므로 다른 방법 필요
                                // 임시로 cameraInfos에 별도로 저장하지 않고 그냥 카운트만 증가
                                cameraPatternCount++;
                                totalLoadedPatterns++;
                            }
                        }
                    } else {
                        xml.skipCurrentElement();
                    }
                }
            }
            else if (xml.name() == QLatin1String("simulationData")) {
                // 시뮬레이션 데이터에서 학습 이미지 경로 읽기
                QString jsonData = xml.readElementText();
                QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData.toUtf8());
                
                if (!jsonDoc.isNull() && jsonDoc.isObject()) {
                    QJsonObject projectData = jsonDoc.object();
                    
                    // 학습 이미지 경로 추출
                    if (projectData.contains("trainingImagePaths") && trainingImageCallback) {
                        QJsonArray trainingArray = projectData["trainingImagePaths"].toArray();
                        QStringList trainingPaths;
                        
                        for (const QJsonValue& value : trainingArray) {
                            QString trainingPath = value.toString();
                            if (QFile::exists(trainingPath)) {
                                trainingPaths.append(trainingPath);
                            }
                        }
                        
                        if (!trainingPaths.isEmpty()) {
                            trainingImageCallback(trainingPaths);
                        }
                    }
                }
            }
            else {
                xml.skipCurrentElement();
            }
        }
        
        // **모든 자식 패턴들을 CameraView에 추가**
        for (const PatternInfo& childPattern : tempChildPatterns) {
            if (cameraView) {
                QUuid addedId = cameraView->addPattern(childPattern);
                if (!addedId.isNull()) {
                    // **부모 패턴의 childIds 업데이트**
                    if (!childPattern.parentId.isNull()) {
                        PatternInfo* parentPattern = cameraView->getPatternById(childPattern.parentId);
                        if (parentPattern && !parentPattern->childIds.contains(childPattern.id)) {
                            parentPattern->childIds.append(childPattern.id);
                            cameraView->updatePatternById(childPattern.parentId, *parentPattern);
                        }
                    }
                    
                    // **자식 패턴 트리 아이템 생성 (UI가 있는 경우에만)**
                    if (patternTree) {
                        QTreeWidgetItem* childItem = createPatternTreeItem(childPattern);
                        itemMap[childPattern.id.toString()] = childItem;
                        
                        // **부모 아이템 찾아서 자식으로 추가**
                        QTreeWidgetItem* parentItem = itemMap.value(childPattern.parentId.toString());
                        if (parentItem) {
                            parentItem->addChild(childItem);
                        } else {
                            // 부모를 찾을 수 없으면 최상위에 추가
                            patternTree->addTopLevelItem(childItem);
                        }
                    }
                    
                    cameraPatternCount++;
                    totalLoadedPatterns++;
                }
            } else {
                // UI 없이 패턴만 저장하는 경우
                // CameraInfo에는 patterns 멤버가 없으므로 다른 방법 필요
                // 임시로 cameraInfos에 별도로 저장하지 않고 그냥 카운트만 증가
                cameraPatternCount++;
                totalLoadedPatterns++;
            }
        }
        
    } catch (const std::exception& e) {
        qWarning() << "카메라 섹션 읽기 중 오류:" << e.what();
        return false;
    } catch (...) {
        qWarning() << "카메라 섹션 읽기 중 알 수 없는 오류";
        return false;
    }
    
    if (cameraPatternCount > 0) {
        loadedCameraNames += QString("- 카메라 %1: %2개 패턴\n").arg(cameraUuid).arg(cameraPatternCount);
    }
    
    return true;
}

CalibrationInfo RecipeManager::readCalibrationInfo(QXmlStreamReader& xml) {
    CalibrationInfo calibInfo;
    calibInfo.isCalibrated = true;
    calibInfo.pixelToMmRatio = xml.attributes().value("pixelToMmRatio").toDouble();
    calibInfo.realWorldLength = xml.attributes().value("realWorldLength").toDouble();
    
    int x = xml.attributes().value("rectX").toInt();
    int y = xml.attributes().value("rectY").toInt();
    int w = xml.attributes().value("rectW").toInt();
    int h = xml.attributes().value("rectH").toInt();
    calibInfo.calibrationRect = QRect(x, y, w, h);
    
    xml.skipCurrentElement();
    return calibInfo;
}

// 새로운 함수 추가 - 자식 패턴들을 읽어서 별도로 저장

QStringList RecipeManager::readChildPatterns(QXmlStreamReader& xml, const QString& cameraUuid, 
                                           const QUuid& parentId) {
    QStringList childIds;
    
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("Pattern")) {
            // **자식 패턴을 재귀적으로 읽기**
            PatternInfo childPattern = readPattern(xml, cameraUuid);
            
            if (!childPattern.id.isNull()) {
                // **부모 ID 설정**
                childPattern.parentId = parentId;
                
                // **전역 패턴 리스트에 추가** (나중에 CameraView에 추가하기 위해)
                tempChildPatterns.append(childPattern);
                
                // **부모의 자식 ID 목록에 추가**
                childIds.append(childPattern.id.toString());
                
            }
        } else {
            xml.skipCurrentElement();
        }
    }
    
    return childIds;
}

// readPattern() 함수 완전 수정 - 자식 패턴들도 읽어오기

PatternInfo RecipeManager::readPattern(QXmlStreamReader& xml, const QString& cameraUuid) {
    PatternInfo pattern;
    
    try {
        pattern.id = QUuid(xml.attributes().value("id").toString());
        pattern.name = xml.attributes().value("name").toString();
        pattern.cameraUuid = cameraUuid;
        
        QString typeStr = xml.attributes().value("type").toString();
        if (typeStr == "ROI") pattern.type = PatternType::ROI;
        else if (typeStr == "FID") pattern.type = PatternType::FID;
        else if (typeStr == "INS") pattern.type = PatternType::INS;
        else if (typeStr == "Filter") pattern.type = PatternType::FIL;
        else pattern.type = PatternType::INS;
        
        QString colorStr = xml.attributes().value("color").toString();
        if (!colorStr.isEmpty()) {
            pattern.color = QColor(colorStr);
        } else {
            // UIColors에서 정의된 색상 사용
            switch (pattern.type) {
                case PatternType::ROI: pattern.color = UIColors::ROI_COLOR; break;
                case PatternType::FID: pattern.color = UIColors::FIDUCIAL_COLOR; break;
                case PatternType::INS: pattern.color = UIColors::INSPECTION_COLOR; break;
                case PatternType::FIL: pattern.color = Qt::yellow; break;
                default: pattern.color = UIColors::INSPECTION_COLOR; break;
            }
        }
        
        pattern.enabled = xml.attributes().value("enabled").toString() != "false";
        
        // 부모 패턴 ID 읽기
        QString parentIdStr = xml.attributes().value("parentId").toString();
        if (!parentIdStr.isEmpty()) {
            pattern.parentId = QUuid(parentIdStr);
        }
        
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("Rect")) {
                readPatternRect(xml, pattern);
            }
            else if (xml.name() == QLatin1String("Details")) {
                qDebug() << QString("Details 태그 발견: %1, 패턴타입: %2").arg(xml.name().toString()).arg(static_cast<int>(pattern.type));
                readPatternDetails(xml, pattern);
            }
            else if (xml.name() == QLatin1String("FIDDetails")) {
                readFIDDetails(xml, pattern);
            }
            else if (xml.name() == QLatin1String("INSDetails")) {
                readINSDetails(xml, pattern);
            }
            else if (xml.name() == QLatin1String("ROIDetails")) {
                readROIDetails(xml, pattern);
            }
            else if (xml.name() == QLatin1String("Filters")) {
                readPatternFilters(xml, pattern);
            }
            else if (xml.name() == QLatin1String("ChildPatterns") || xml.name() == QLatin1String("Children")) {
                // **자식 패턴들 읽기 - 여기가 핵심!**
                QStringList childIdStrings = readChildPatterns(xml, cameraUuid, pattern.id);
                for (const QString& childIdStr : childIdStrings) {
                    pattern.childIds.append(QUuid(childIdStr));
                }
            }
            else {
                xml.skipCurrentElement();
            }
        }
        
    } catch (const std::exception& e) {
        qWarning() << "패턴 읽기 오류:" << e.what();
        pattern = PatternInfo();
    } catch (...) {
        qWarning() << "패턴 읽기 중 알 수 없는 오류";
        pattern = PatternInfo();
    }
    
    return pattern;
}

void RecipeManager::readPatternRect(QXmlStreamReader& xml, PatternInfo& pattern) {
    double x = xml.attributes().value("x").toDouble();
    double y = xml.attributes().value("y").toDouble();
    double width = xml.attributes().value("width").toDouble();
    double height = xml.attributes().value("height").toDouble();
    pattern.rect = QRectF(x, y, width, height);
    
    // 각도값 읽기 (기본값 0.0) - 기존 레시피 호환성 위해 속성 존재 여부 확인
    QString angleStr = xml.attributes().value("angle").toString();
    if (!angleStr.isEmpty()) {
        pattern.angle = angleStr.toDouble();
    } else {
        pattern.angle = 0.0; // 기본값 설정
    }
    
    // frameIndex 읽기 (기본값 0) - 기존 레시피 호환성 위해 속성 존재 여부 확인
    QString frameIndexStr = xml.attributes().value("frameIndex").toString();
    if (!frameIndexStr.isEmpty()) {
        pattern.frameIndex = frameIndexStr.toInt();
    } else {
        pattern.frameIndex = 0; // 기본값 설정
    }
    
    // 디버그: 모든 패턴의 각도값 확인
    
    xml.skipCurrentElement();
}

void RecipeManager::readPatternDetails(QXmlStreamReader& xml, PatternInfo& pattern) {
    qDebug() << QString("readPatternDetails 호출됨 - 패턴: %1, 타입: %2").arg(pattern.name).arg(static_cast<int>(pattern.type));
    
    while (xml.readNextStartElement()) {
        qDebug() << QString("Details 내부 요소: %1 (패턴: %2)").arg(xml.name().toString()).arg(pattern.name);
        
        if (xml.name() == QLatin1String("ROIDetails")) {
            readROIDetails(xml, pattern);
        }
        else if (xml.name() == QLatin1String("FIDDetails")) {
            qDebug() << QString("==> FIDDetails 처리 시작: %1").arg(pattern.name);
            readFIDDetails(xml, pattern);
            qDebug() << QString("==> FIDDetails 처리 완료: %1").arg(pattern.name);
        }
        else if (xml.name() == QLatin1String("INSDetails")) {
            qDebug() << QString("==> INSDetails 처리 시작: %1").arg(pattern.name);
            readINSDetails(xml, pattern);
            qDebug() << QString("==> INSDetails 처리 완료: %1").arg(pattern.name);
        }
        else {
            qDebug() << QString("알 수 없는 Details 요소: %1").arg(xml.name().toString());
            xml.skipCurrentElement();
        }
    }
}

void RecipeManager::readROIDetails(QXmlStreamReader& xml, PatternInfo& pattern) {
    // includeAllCamera 제거됨
    xml.skipCurrentElement();
}

void RecipeManager::readFIDDetails(QXmlStreamReader& xml, PatternInfo& pattern) {
    pattern.matchThreshold = xml.attributes().value("matchThreshold").toDouble();
    pattern.useRotation = (xml.attributes().value("useRotation").toString() == "true");
    pattern.minAngle = xml.attributes().value("minAngle").toDouble();
    pattern.maxAngle = xml.attributes().value("maxAngle").toDouble();
    pattern.angleStep = xml.attributes().value("angleStep").toDouble();
    
    // matchMethod 기본값 명시적 설정 (0 = TM_CCOEFF_NORMED)
    QString matchMethodStr = xml.attributes().value("matchMethod").toString();
    pattern.fidMatchMethod = matchMethodStr.isEmpty() ? 0 : matchMethodStr.toInt();
    
    // runInspection 기본값은 true (XML에 값이 없으면 true)
    QString runInspectionStr = xml.attributes().value("runInspection").toString();
    pattern.runInspection = runInspectionStr.isEmpty() ? true : (runInspectionStr == "true");
    
    // 패턴의 실제 회전 각도 읽기 (Rect에서 읽은 것과 중복이지만 안전을 위해)
    QString patternAngleStr = xml.attributes().value("patternAngle").toString();
    if (!patternAngleStr.isEmpty()) {
        pattern.angle = patternAngleStr.toDouble();
    }
    
    QString imageStr = xml.attributes().value("templateImage").toString();
    
    if (!imageStr.isEmpty()) {
        QByteArray imageData = QByteArray::fromBase64(imageStr.toLatin1());
        pattern.templateImage.loadFromData(imageData);  // 포맷 자동 감지
    }
    
    // ★ FID matchTemplate 로드 (RGB32 포맷 - 매칭용)
    QString matchTemplateStr = xml.attributes().value("matchTemplate").toString();
    if (!matchTemplateStr.isEmpty()) {
        QByteArray matchData = QByteArray::fromBase64(matchTemplateStr.toLatin1());
        pattern.matchTemplate.loadFromData(matchData);
    }
    
    // ★ FID matchTemplateMask 로드
    QString maskStr = xml.attributes().value("matchTemplateMask").toString();
    if (!maskStr.isEmpty()) {
        QByteArray maskData = QByteArray::fromBase64(maskStr.toLatin1());
        pattern.matchTemplateMask.loadFromData(maskData);
    }
    
    xml.skipCurrentElement();
}

void RecipeManager::readINSDetails(QXmlStreamReader& xml, PatternInfo& pattern) {
    pattern.inspectionMethod = xml.attributes().value("inspectionMethod").toInt();
    
    pattern.passThreshold = xml.attributes().value("passThreshold").toDouble();
    // invertResult 제거됨
    pattern.useRotation = (xml.attributes().value("useRotation").toString() == "true");
    pattern.minAngle = xml.attributes().value("minAngle").toDouble();
    pattern.maxAngle = xml.attributes().value("maxAngle").toDouble();
    pattern.angleStep = xml.attributes().value("angleStep").toDouble();
    
    // SSIM NG 임계값 읽기
    QString ssimNgStr = xml.attributes().value("ssimNgThreshold").toString();
    if (!ssimNgStr.isEmpty()) {
        pattern.ssimNgThreshold = ssimNgStr.toDouble();
    }
    
    // allowedNgRatio 읽기
    QString allowedNgRatioStr = xml.attributes().value("allowedNgRatio").toString();
    if (!allowedNgRatioStr.isEmpty()) {
        pattern.allowedNgRatio = allowedNgRatioStr.toDouble();
    }
    
    // ANOMALY 최소 불량 크기 읽기
    QString anomalyMinBlobSizeStr = xml.attributes().value("anomalyMinBlobSize").toString();
    if (!anomalyMinBlobSizeStr.isEmpty()) {
        pattern.anomalyMinBlobSize = anomalyMinBlobSizeStr.toInt();
    }
    QString anomalyMinDefectWidthStr = xml.attributes().value("anomalyMinDefectWidth").toString();
    if (!anomalyMinDefectWidthStr.isEmpty()) {
        pattern.anomalyMinDefectWidth = anomalyMinDefectWidthStr.toInt();
    }
    QString anomalyMinDefectHeightStr = xml.attributes().value("anomalyMinDefectHeight").toString();
    if (!anomalyMinDefectHeightStr.isEmpty()) {
        pattern.anomalyMinDefectHeight = anomalyMinDefectHeightStr.toInt();
    }
    
    // SSIM 허용 NG 비율 읽기
    QString allowedNgStr = xml.attributes().value("allowedNgRatio").toString();
    if (!allowedNgStr.isEmpty()) {
        pattern.allowedNgRatio = allowedNgStr.toDouble();
    }
    
    // 패턴의 실제 회전 각도 읽기 (레시피에서 읽은 것과 중복이지만 안전을 위해)
    QString patternAngleStr = xml.attributes().value("patternAngle").toString();
    if (!patternAngleStr.isEmpty()) {
        pattern.angle = patternAngleStr.toDouble();
    }
    
    // EDGE 검사 관련 속성 읽기 (기본값 사용)
    QString edgeEnabledStr = xml.attributes().value("edgeEnabled").toString();
    if (!edgeEnabledStr.isEmpty()) {
        pattern.edgeEnabled = (edgeEnabledStr == "true");
    } else {
        // 기존 레시피에 edgeEnabled 속성이 없으면 명시적으로 기본값 설정
        pattern.edgeEnabled = true;
    }
    
    QString edgeOffsetXStr = xml.attributes().value("edgeOffsetX").toString();
    if (!edgeOffsetXStr.isEmpty()) {
        pattern.edgeOffsetX = edgeOffsetXStr.toInt();
    } // 비어있으면 CommonDefs.h의 기본값(75) 사용
    
    QString edgeBoxWidthStr = xml.attributes().value("stripEdgeBoxWidth").toString();
    if (!edgeBoxWidthStr.isEmpty()) {
        pattern.stripEdgeBoxWidth = edgeBoxWidthStr.toInt();
    } // 비어있으면 CommonDefs.h의 기본값(90) 사용
    
    QString edgeBoxHeightStr = xml.attributes().value("stripEdgeBoxHeight").toString();
    if (!edgeBoxHeightStr.isEmpty()) {
        pattern.stripEdgeBoxHeight = edgeBoxHeightStr.toInt();
    } // 비어있으면 CommonDefs.h의 기본값(150) 사용
    
    QString edgeMaxOutliersStr = xml.attributes().value("edgeMaxOutliers").toString();
    if (!edgeMaxOutliersStr.isEmpty()) {
        pattern.edgeMaxOutliers = edgeMaxOutliersStr.toInt();
    } // 비어있으면 CommonDefs.h의 기본값(5) 사용
    
    QString edgeDistanceMaxStr = xml.attributes().value("edgeDistanceMax").toString();
    if (!edgeDistanceMaxStr.isEmpty()) {
        pattern.edgeDistanceMax = edgeDistanceMaxStr.toDouble();
    } // 비어있으면 CommonDefs.h의 기본값(10.0) 사용
    
    QString edgeStartPercentStr = xml.attributes().value("edgeStartPercent").toString();
    if (!edgeStartPercentStr.isEmpty()) {
        pattern.edgeStartPercent = edgeStartPercentStr.toInt();
    }
    
    QString edgeEndPercentStr = xml.attributes().value("edgeEndPercent").toString();
    if (!edgeEndPercentStr.isEmpty()) {
        pattern.edgeEndPercent = edgeEndPercentStr.toInt();
    }
    
    // STRIP 길이 캘리브레이션 관련 속성 읽기
    QString stripLengthConversionMmStr = xml.attributes().value("stripLengthConversionMm").toString();
    if (!stripLengthConversionMmStr.isEmpty()) {
        pattern.stripLengthConversionMm = stripLengthConversionMmStr.toDouble();
    } // 비어있으면 CommonDefs.h의 기본값(6.0) 사용
    
    QString stripLengthCalibrationPxStr = xml.attributes().value("stripLengthCalibrationPx").toString();
    if (!stripLengthCalibrationPxStr.isEmpty()) {
        pattern.stripLengthCalibrationPx = stripLengthCalibrationPxStr.toDouble();
    } // 비어있으면 CommonDefs.h의 기본값(0.0) 사용
    
    QString stripLengthCalibratedStr = xml.attributes().value("stripLengthCalibrated").toString();
    if (!stripLengthCalibratedStr.isEmpty()) {
        pattern.stripLengthCalibrated = (stripLengthCalibratedStr == "true");
    } // 비어있으면 CommonDefs.h의 기본값(false) 사용
    
    QString stripLengthMinStr = xml.attributes().value("stripLengthMin").toString();
    if (!stripLengthMinStr.isEmpty()) {
        pattern.stripLengthMin = stripLengthMinStr.toDouble();
    }
    
    QString stripLengthMaxStr = xml.attributes().value("stripLengthMax").toString();
    if (!stripLengthMaxStr.isEmpty()) {
        pattern.stripLengthMax = stripLengthMaxStr.toDouble();
    }
    
    QString stripLengthEnabledStr = xml.attributes().value("stripLengthEnabled").toString();
    if (!stripLengthEnabledStr.isEmpty()) {
        pattern.stripLengthEnabled = (stripLengthEnabledStr == "true");
    }
    
    // STRIP FRONT/REAR 활성화 여부 읽기
    QString stripFrontEnabledStr = xml.attributes().value("stripFrontEnabled").toString();
    if (!stripFrontEnabledStr.isEmpty()) {
        pattern.stripFrontEnabled = (stripFrontEnabledStr == "true");
    }
    
    QString stripRearEnabledStr = xml.attributes().value("stripRearEnabled").toString();
    if (!stripRearEnabledStr.isEmpty()) {
        pattern.stripRearEnabled = (stripRearEnabledStr == "true");
    }
    
    // STRIP 두께 검사 관련 속성 읽기
    QString stripThicknessMinStr = xml.attributes().value("stripThicknessMin").toString();
    if (!stripThicknessMinStr.isEmpty()) {
        pattern.stripThicknessMin = stripThicknessMinStr.toDouble();
    }
    
    QString stripThicknessMaxStr = xml.attributes().value("stripThicknessMax").toString();
    if (!stripThicknessMaxStr.isEmpty()) {
        pattern.stripThicknessMax = stripThicknessMaxStr.toDouble();
    }
    
    QString stripRearThicknessMinStr = xml.attributes().value("stripRearThicknessMin").toString();
    if (!stripRearThicknessMinStr.isEmpty()) {
        pattern.stripRearThicknessMin = stripRearThicknessMinStr.toDouble();
    }
    
    QString stripRearThicknessMaxStr = xml.attributes().value("stripRearThicknessMax").toString();
    if (!stripRearThicknessMaxStr.isEmpty()) {
        pattern.stripRearThicknessMax = stripRearThicknessMaxStr.toDouble();
    }
    
    // STRIP Gradient 시작/끝 지점 및 두께 박스 크기 읽기
    QString stripGradientStartPercentStr = xml.attributes().value("stripGradientStartPercent").toString();
    if (!stripGradientStartPercentStr.isEmpty()) {
        pattern.stripGradientStartPercent = stripGradientStartPercentStr.toInt();
    }
    
    QString stripGradientEndPercentStr = xml.attributes().value("stripGradientEndPercent").toString();
    if (!stripGradientEndPercentStr.isEmpty()) {
        pattern.stripGradientEndPercent = stripGradientEndPercentStr.toInt();
    }
    
    QString stripThicknessBoxWidthStr = xml.attributes().value("stripThicknessBoxWidth").toString();
    if (!stripThicknessBoxWidthStr.isEmpty()) {
        pattern.stripThicknessBoxWidth = stripThicknessBoxWidthStr.toInt();
    }
    
    QString stripThicknessBoxHeightStr = xml.attributes().value("stripThicknessBoxHeight").toString();
    if (!stripThicknessBoxHeightStr.isEmpty()) {
        pattern.stripThicknessBoxHeight = stripThicknessBoxHeightStr.toInt();
    }
    
    QString stripRearThicknessBoxWidthStr = xml.attributes().value("stripRearThicknessBoxWidth").toString();
    if (!stripRearThicknessBoxWidthStr.isEmpty()) {
        pattern.stripRearThicknessBoxWidth = stripRearThicknessBoxWidthStr.toInt();
    }
    
    QString stripRearThicknessBoxHeightStr = xml.attributes().value("stripRearThicknessBoxHeight").toString();
    if (!stripRearThicknessBoxHeightStr.isEmpty()) {
        pattern.stripRearThicknessBoxHeight = stripRearThicknessBoxHeightStr.toInt();
    }
    
    // BARREL LEFT 검사 파라미터 로드
    QString barrelLeftEnabledStr = xml.attributes().value("barrelLeftStripEnabled").toString();
    if (!barrelLeftEnabledStr.isEmpty()) {
        pattern.barrelLeftStripEnabled = (barrelLeftEnabledStr == "true");
    }
    QString barrelLeftOffsetXStr = xml.attributes().value("barrelLeftStripOffsetX").toString();
    if (!barrelLeftOffsetXStr.isEmpty()) {
        pattern.barrelLeftStripOffsetX = barrelLeftOffsetXStr.toInt();
    }
    QString barrelLeftBoxWidthStr = xml.attributes().value("barrelLeftStripBoxWidth").toString();
    if (!barrelLeftBoxWidthStr.isEmpty()) {
        pattern.barrelLeftStripBoxWidth = barrelLeftBoxWidthStr.toInt();
    }
    QString barrelLeftBoxHeightStr = xml.attributes().value("barrelLeftStripBoxHeight").toString();
    if (!barrelLeftBoxHeightStr.isEmpty()) {
        pattern.barrelLeftStripBoxHeight = barrelLeftBoxHeightStr.toInt();
    }
    QString barrelLeftLengthMinStr = xml.attributes().value("barrelLeftStripLengthMin").toString();
    if (!barrelLeftLengthMinStr.isEmpty()) {
        pattern.barrelLeftStripLengthMin = barrelLeftLengthMinStr.toDouble();
    }
    QString barrelLeftLengthMaxStr = xml.attributes().value("barrelLeftStripLengthMax").toString();
    if (!barrelLeftLengthMaxStr.isEmpty()) {
        pattern.barrelLeftStripLengthMax = barrelLeftLengthMaxStr.toDouble();
    }
    
    // BARREL RIGHT 검사 파라미터 로드
    QString barrelRightEnabledStr = xml.attributes().value("barrelRightStripEnabled").toString();
    if (!barrelRightEnabledStr.isEmpty()) {
        pattern.barrelRightStripEnabled = (barrelRightEnabledStr == "true");
    }
    QString barrelRightOffsetXStr = xml.attributes().value("barrelRightStripOffsetX").toString();
    if (!barrelRightOffsetXStr.isEmpty()) {
        pattern.barrelRightStripOffsetX = barrelRightOffsetXStr.toInt();
    }
    QString barrelRightBoxWidthStr = xml.attributes().value("barrelRightStripBoxWidth").toString();
    if (!barrelRightBoxWidthStr.isEmpty()) {
        pattern.barrelRightStripBoxWidth = barrelRightBoxWidthStr.toInt();
    }
    QString barrelRightBoxHeightStr = xml.attributes().value("barrelRightStripBoxHeight").toString();
    if (!barrelRightBoxHeightStr.isEmpty()) {
        pattern.barrelRightStripBoxHeight = barrelRightBoxHeightStr.toInt();
    }
    QString barrelRightLengthMinStr = xml.attributes().value("barrelRightStripLengthMin").toString();
    if (!barrelRightLengthMinStr.isEmpty()) {
        pattern.barrelRightStripLengthMin = barrelRightLengthMinStr.toDouble();
    }
    QString barrelRightLengthMaxStr = xml.attributes().value("barrelRightStripLengthMax").toString();
    if (!barrelRightLengthMaxStr.isEmpty()) {
        pattern.barrelRightStripLengthMax = barrelRightLengthMaxStr.toDouble();
    }
    
    // 기본 템플릿 이미지 로드 (DIFF용 또는 레거시)
    QString imageStr = xml.attributes().value("templateImage").toString();
    
    if (!imageStr.isEmpty()) {
        QByteArray imageData = QByteArray::fromBase64(imageStr.toLatin1());
        pattern.templateImage.loadFromData(imageData);
    }
    
    // 패턴 매칭 설정 로드
    QString patternMatchEnabledStr = xml.attributes().value("patternMatchEnabled").toString();
    if (!patternMatchEnabledStr.isEmpty()) {
        pattern.patternMatchEnabled = (patternMatchEnabledStr == "true");
    }
    
    QString patternMatchMethodStr = xml.attributes().value("patternMatchMethod").toString();
    if (!patternMatchMethodStr.isEmpty()) {
        pattern.patternMatchMethod = patternMatchMethodStr.toInt();
    }
    
    QString patternMatchThresholdStr = xml.attributes().value("patternMatchThreshold").toString();
    if (!patternMatchThresholdStr.isEmpty()) {
        pattern.patternMatchThreshold = patternMatchThresholdStr.toDouble();
    }
    
    QString patternMatchUseRotationStr = xml.attributes().value("patternMatchUseRotation").toString();
    if (!patternMatchUseRotationStr.isEmpty()) {
        pattern.patternMatchUseRotation = (patternMatchUseRotationStr == "true");
    }
    
    QString patternMatchMinAngleStr = xml.attributes().value("patternMatchMinAngle").toString();
    if (!patternMatchMinAngleStr.isEmpty()) {
        pattern.patternMatchMinAngle = patternMatchMinAngleStr.toDouble();
    }
    
    QString patternMatchMaxAngleStr = xml.attributes().value("patternMatchMaxAngle").toString();
    if (!patternMatchMaxAngleStr.isEmpty()) {
        pattern.patternMatchMaxAngle = patternMatchMaxAngleStr.toDouble();
    }
    
    QString patternMatchAngleStepStr = xml.attributes().value("patternMatchAngleStep").toString();
    if (!patternMatchAngleStepStr.isEmpty()) {
        pattern.patternMatchAngleStep = patternMatchAngleStepStr.toDouble();
    }
    
    // 패턴 매칭용 템플릿 이미지 로드
    QString matchTemplateStr = xml.attributes().value("matchTemplate").toString();
    if (!matchTemplateStr.isEmpty()) {
        QByteArray imageData = QByteArray::fromBase64(matchTemplateStr.toLatin1());
        QImage tempImage;
        bool loadSuccess = tempImage.loadFromData(imageData);
        if (loadSuccess) {
            // 기존 templateImage와 동일한 방식으로 저장
            pattern.matchTemplate = tempImage;
        }
    }
    
    // 패턴 매칭용 마스크 이미지 로드
    QString matchTemplateMaskStr = xml.attributes().value("matchTemplateMask").toString();
    if (!matchTemplateMaskStr.isEmpty()) {
        QByteArray imageData = QByteArray::fromBase64(matchTemplateMaskStr.toLatin1());
        QImage tempImage;
        bool loadSuccess = tempImage.loadFromData(imageData);
        if (loadSuccess) {
            pattern.matchTemplateMask = tempImage;
        }
    }
    
    xml.skipCurrentElement();
}

void RecipeManager::readPatternFilters(QXmlStreamReader& xml, PatternInfo& pattern) {
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("Filter")) {
            FilterInfo filter;
            filter.type = xml.attributes().value("type").toInt();
            filter.enabled = xml.attributes().value("enabled").toString() != "false";
            
            // 파라미터 읽기
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("Param")) {
                    QString paramName = xml.attributes().value("name").toString();
                    int paramValue = xml.attributes().value("value").toInt();
                    filter.params[paramName] = paramValue;
                    xml.skipCurrentElement();
                } else {
                    xml.skipCurrentElement();
                }
            }
            
            pattern.filters.append(filter);
        } else {
            xml.skipCurrentElement();
        }
    }
}

QStringList RecipeManager::readPatternChildren(QXmlStreamReader& xml) {
    QStringList children;
    while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("Child")) {
            QString childId = xml.attributes().value("id").toString();
            if (!childId.isEmpty()) {
                children.append(childId);
            }
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
    return children;
}

void RecipeManager::restorePatternRelationships(const QMap<QString, QStringList>& childrenMap,
                                               const QMap<QString, QTreeWidgetItem*>& itemMap,
                                               CameraView* cameraView) {
    
    // 1. parentId 기반으로 관계 복원 (우선순위 높음)
    if (cameraView) {
        const QList<PatternInfo>& allPatterns = cameraView->getPatterns();
        
        for (const PatternInfo& pattern : allPatterns) {
            if (!pattern.parentId.isNull()) {
                // CameraView에서 parentId 설정 (이미 PatternInfo에 저장되어 있음)
                // updatePatternTree()에서 자동으로 부모-자식 관계가 표시됨
            }
        }
    }
    
    // 2. 기존 childrenMap 기반 관계 복원 (호환성 유지용)
    for (auto it = childrenMap.begin(); it != childrenMap.end(); ++it) {
        QString parentId = it.key();
        QStringList childIds = it.value();
        
        if (cameraView) {
            QUuid parentUuid(parentId);
            
            // 각 자식 패턴의 parentId를 설정
            for (const QString& childId : childIds) {
                QUuid childUuid(childId);
                PatternInfo* childPattern = cameraView->getPatternById(childUuid);
                if (childPattern && childPattern->parentId.isNull()) {
                    // parentId가 설정되지 않은 경우에만 기존 시스템으로 설정
                    childPattern->parentId = parentUuid;
                }
            }
        }
    }
}

QTreeWidgetItem* RecipeManager::createPatternTreeItem(const PatternInfo& pattern) {
    QTreeWidgetItem* item = new QTreeWidgetItem();
    
    QString name = pattern.name.isEmpty() ? QString("패턴 %1").arg(pattern.id.toString().left(8)) : pattern.name;
    item->setText(0, name);
    
    QString typeText;
    switch (pattern.type) {
        case PatternType::ROI: typeText = "ROI"; break;
        case PatternType::FID: typeText = "FID"; break;
        case PatternType::INS: typeText = "INS"; break;
        case PatternType::FIL: typeText = "FIL"; break;
    }
    item->setText(1, typeText);
    
    item->setText(2, pattern.enabled ? "활성" : "비활성");
    item->setData(0, Qt::UserRole, pattern.id.toString());
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(0, pattern.enabled ? Qt::Checked : Qt::Unchecked);
    
    return item;
}

bool RecipeManager::saveSimulationRecipe(const QString& fileName,
                                         const QString& projectName,
                                         const QStringList& imagePaths,
                                         int currentIndex) {
    QFile file(fileName);
    
    // 기존 파일 읽기
    QDomDocument doc;
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        doc.setContent(&file);
        file.close();
    } else {
        // 새 XML 문서 생성
        QDomElement root = doc.createElement("Recipes");
        doc.appendChild(root);
    }
    
    QDomElement root = doc.documentElement();
    if (root.isNull()) {
        root = doc.createElement("Recipes");
        doc.appendChild(root);
    }
    
    // 기존 시뮬레이션 카메라가 있는지 확인하고 제거
    QString simulationCameraUuid = projectName;  // projectName을 그대로 사용
    QDomNodeList cameras = root.childNodes();
    for (int i = cameras.count() - 1; i >= 0; i--) {
        QDomElement camera = cameras.at(i).toElement();
        if (!camera.isNull() && camera.tagName() == "Camera") {
            QString uuid = camera.attribute("uuid");
            if (uuid == simulationCameraUuid) {
                root.removeChild(camera);
                break;
            }
        }
    }
    
    // 새 시뮬레이션 카메라 엘리먼트 생성
    QDomElement cameraElement = doc.createElement("Camera");
    QString cameraSerial = projectName;  // projectName을 serialNumber로 사용
    cameraElement.setAttribute("serialNumber", cameraSerial);
    cameraElement.setAttribute("uuid", simulationCameraUuid);
    cameraElement.setAttribute("type", "simulation");
    cameraElement.setAttribute("imageIndex", "0");  // 기본 이미지 인덱스
    
    // 시뮬레이션 카메라 설정
    // videoDeviceIndex 제거됨 (Spinnaker SDK만 사용)
    
    QDomElement deviceId = doc.createElement("deviceId");
    deviceId.appendChild(doc.createTextNode("SIMULATION"));
    cameraElement.appendChild(deviceId);
    
    QDomElement uniqueId = doc.createElement("uniqueId");
    uniqueId.appendChild(doc.createTextNode(simulationCameraUuid));
    cameraElement.appendChild(uniqueId);
    
    // JSON 형태의 이미지 정보 저장
    QJsonObject projectData;
    projectData["projectName"] = projectName;
    projectData["createdTime"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    projectData["imageCount"] = imagePaths.size();
    projectData["currentIndex"] = currentIndex;
    
    QJsonArray imageArray;
    for (const QString& imagePath : imagePaths) {
        imageArray.append(imagePath);
    }
    projectData["imagePaths"] = imageArray;
    
    QJsonDocument jsonDoc(projectData);
    
    QDomElement simulationData = doc.createElement("simulationData");
    simulationData.appendChild(doc.createTextNode(QString::fromUtf8(jsonDoc.toJson(QJsonDocument::Compact))));
    cameraElement.appendChild(simulationData);
    
    // 카메라 엘리먼트를 루트에 추가
    root.appendChild(cameraElement);
    
    if (!file.open(QIODevice::WriteOnly)) {
        setError(QString("파일을 열 수 없습니다: %1").arg(fileName));
        return false;
    }
    
    QTextStream stream(&file);
    stream << doc.toString(4);
    file.close();
    
    return true;
}

bool RecipeManager::loadSimulationRecipe(const QString& fileName,
                                         const QString& projectName,
                                         QStringList& imagePaths,
                                         int& currentIndex) {
    QFile file(fileName);
    if (!file.exists()) {
        setError(QString("파일이 존재하지 않습니다: %1").arg(fileName));
        return false;
    }
    
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QString("파일을 열 수 없습니다: %1").arg(fileName));
        return false;
    }
    
    QDomDocument doc;
    if (!doc.setContent(&file)) {
        file.close();
        setError("XML 파싱 오류");
        return false;
    }
    file.close();
    
    QDomElement root = doc.documentElement();
    QDomNodeList cameras = root.elementsByTagName("Camera");
    
    QString targetCameraName = QString("SIM_%1").arg(projectName);
    
    for (int i = 0; i < cameras.count(); i++) {
        QDomElement camera = cameras.at(i).toElement();
        QString cameraName = camera.attribute("name");
        
        if (cameraName == targetCameraName) {
            // simulationData 엘리먼트에서 JSON 데이터 읽기
            QDomElement simulationDataElement = camera.firstChildElement("simulationData");
            if (!simulationDataElement.isNull()) {
                QString jsonData = simulationDataElement.text();
                QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonData.toUtf8());
                
                if (!jsonDoc.isNull() && jsonDoc.isObject()) {
                    QJsonObject projectData = jsonDoc.object();
                    
                    // 이미지 경로들 로드
                    imagePaths.clear();
                    QJsonArray imageArray = projectData["imagePaths"].toArray();
                    for (const QJsonValue& value : imageArray) {
                        QString imagePath = value.toString();
                        if (QFile::exists(imagePath)) {
                            imagePaths.append(imagePath);
                        }
                    }
                    
                    // 현재 인덱스 복원
                    currentIndex = projectData["currentIndex"].toInt();
                    if (currentIndex >= imagePaths.size()) {
                        currentIndex = imagePaths.size() - 1;
                    }
                    
                    return true;
                }
            }
            break;
        }
    }
    
    setError(QString("시뮬레이션 프로젝트 '%1'을 찾을 수 없습니다").arg(projectName));
    return false;
}

// === 새로운 개별 레시피 관리 함수들 구현 ===

QString RecipeManager::getRecipesDirectory() {
    QString appDir = QCoreApplication::applicationDirPath();
    return QDir(appDir).absoluteFilePath("recipes");
}

bool RecipeManager::createRecipesDirectory() {
    QString recipesDir = getRecipesDirectory();
    QDir dir;
    if (!dir.exists(recipesDir)) {
        if (!dir.mkpath(recipesDir)) {
            setError(QString("레시피 디렉토리를 생성할 수 없습니다: %1").arg(recipesDir));
            return false;
        }
    }
    return true;
}

QStringList RecipeManager::getAvailableRecipes() {
    if (!createRecipesDirectory()) {
        return QStringList();
    }
    
    QDir recipesDir(getRecipesDirectory());
    QStringList recipeNames;
    
    // 새로운 구조만 검색: recipes/레시피명/ 폴더들
    QFileInfoList dirList = recipesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& dirInfo : dirList) {
        QString recipeName = dirInfo.baseName();
        QString xmlFile = QDir(dirInfo.absoluteFilePath()).absoluteFilePath(recipeName + ".xml");
        if (QFile::exists(xmlFile)) {
            recipeNames << recipeName;
        }
    }
    
    return recipeNames;
}

bool RecipeManager::saveRecipeByName(const QString& recipeName, const QVector<PatternInfo>& patterns) {
    if (recipeName.isEmpty()) {
        setError("레시피 이름이 비어있습니다");
        return false;
    }
    
    if (!createRecipesDirectory()) {
        return false;
    }
    
    // 레시피별 폴더 생성: recipes/레시피명/
    QString recipeDir = QDir(getRecipesDirectory()).absoluteFilePath(recipeName);
    QDir dir;
    if (!dir.exists(recipeDir)) {
        if (!dir.mkpath(recipeDir)) {
            setError(QString("레시피 폴더를 생성할 수 없습니다: %1").arg(recipeDir));
            return false;
        }
    }
    
    // 레시피 파일 경로: recipes/레시피명/레시피명.xml
    QString fileName = QDir(recipeDir).absoluteFilePath(recipeName + ".xml");
    
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(QString("레시피 파일을 생성할 수 없습니다: %1").arg(fileName));
        return false;
    }
    
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    
    // 레시피 루트 엘리먼트
    xml.writeStartElement("Recipe");
    xml.writeAttribute("name", recipeName);
    xml.writeAttribute("version", "1.0");
    xml.writeAttribute("createdTime", QDateTime::currentDateTime().toString(Qt::ISODate));
    
    // 패턴들을 카메라별로 그룹화
    xml.writeStartElement("Cameras");
    
    // 기존 레시피가 있으면 기존 카메라 정보를 먼저 읽어서 유지
    QSet<QString> existingCameras;
    QString existingRecipeFile = QDir(recipeDir).absoluteFilePath(recipeName + ".xml");
    if (QFile::exists(existingRecipeFile)) {
        QStringList existingCameraUuids = getRecipeCameraUuids(recipeName);
        for (const QString& uuid : existingCameraUuids) {
            existingCameras.insert(uuid);
        }
    }
    
    // 카메라 UUID별로 패턴들을 그룹화
    QMap<QString, QVector<PatternInfo>> cameraPatterns;
    for (const PatternInfo& pattern : patterns) {
        QString cameraUuid = pattern.cameraUuid.isEmpty() ? "default" : pattern.cameraUuid;
        cameraPatterns[cameraUuid].append(pattern);
        existingCameras.insert(cameraUuid); // 패턴이 있는 카메라도 추가
    }
    
    // 모든 카메라(기존 + 새로운)를 저장
    for (const QString& cameraUuid : existingCameras) {
        const QVector<PatternInfo>& cameraPatternList = cameraPatterns.value(cameraUuid); // 패턴이 없으면 빈 벡터
        
        xml.writeStartElement("Camera");
        xml.writeAttribute("uuid", cameraUuid);
        xml.writeAttribute("serialNumber", cameraUuid); // UUID를 serialNumber로 사용
        
        // 카메라별 티칭 이미지 정보 추가
        QString teachingImageName = QString("%1.jpg").arg(cameraUuid);
        xml.writeAttribute("teachingImage", teachingImageName);
        // 주의: 이 함수에서는 base64 처리하지 않음 (패턴만 저장하는 용도)
        
        // 디버그: 레시피에 카메라별 티칭 이미지 정보 저장
        qDebug() << QString("레시피에 카메라 '%1'의 티칭 이미지 '%2' 저장").arg(cameraUuid).arg(teachingImageName);
        
        xml.writeStartElement("Patterns");
        
        for (const PatternInfo& pattern : cameraPatternList) {
            writePatternHeader(xml, pattern);
            writePatternRect(xml, pattern);
            
            xml.writeStartElement("Details");
            switch (pattern.type) {
            case PatternType::ROI:
                writeROIDetails(xml, pattern);
                break;
            case PatternType::FID:
                writeFIDDetails(xml, pattern);
                break;
            case PatternType::INS:
                writeINSDetails(xml, pattern);
                break;
            case PatternType::FIL:
                // FIL 타입은 별도 세부정보 없음
                break;
        }
        xml.writeEndElement(); // Details
        
        // 필터 정보 저장
        if (!pattern.filters.isEmpty()) {
            writePatternFilters(xml, pattern);
        }
        
        xml.writeEndElement(); // Pattern
        }
        
        xml.writeEndElement(); // Patterns (카메라별)
        xml.writeEndElement(); // Camera
    }
    
    xml.writeEndElement(); // Cameras
    xml.writeEndElement(); // Recipe
    xml.writeEndDocument();
    
    file.close();
    return true;
}

bool RecipeManager::loadRecipeByName(const QString& recipeName, QVector<PatternInfo>& patterns) {
    if (recipeName.isEmpty()) {
        setError("레시피 이름이 비어있습니다");
        return false;
    }
    
    // 새로운 구조만 지원: recipes/레시피명/레시피명.xml
    QString fileName = QDir(getRecipesDirectory()).absoluteFilePath(recipeName + "/" + recipeName + ".xml");
    
    QFile file(fileName);
    if (!file.exists()) {
        setError(QString("레시피 파일이 존재하지 않습니다: %1").arg(recipeName));
        return false;
    }
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(QString("레시피 파일을 열 수 없습니다: %1").arg(fileName));
        return false;
    }
    
    patterns.clear();
    
    QXmlStreamReader xml(&file);
    
    try {
        // XML 루트 요소 확인 - 새 구조는 Recipe
        if (!xml.readNextStartElement() || xml.name() != QLatin1String("Recipe")) {
            throw QString("유효하지 않은 레시피 파일 형식입니다.");
        }
        
        // Recipe 내부에서 Cameras 태그 찾기
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("Cameras")) {
                // 각 카메라별 레시피 읽기
                while (xml.readNextStartElement()) {
                    if (xml.name() == QLatin1String("Camera")) {
                        QString cameraUuid = xml.attributes().value("uuid").toString();
                        
                        // 카메라 내부의 Patterns 태그 찾기
                        while (xml.readNextStartElement()) {
                            if (xml.name() == QLatin1String("Patterns")) {
                                // Patterns 내부의 Pattern들 처리
                                while (xml.readNextStartElement()) {
                                    if (xml.name() == QLatin1String("Pattern")) {
                                        PatternInfo pattern = readPattern(xml, cameraUuid);
                                        if (!pattern.id.isNull()) {
                                            patterns.append(pattern);
                                        }
                                    } else {
                                        xml.skipCurrentElement();
                                    }
                                }
                            } else {
                                xml.skipCurrentElement();
                            }
                        }
                    } else {
                        xml.skipCurrentElement();
                    }
                }
            } else {
                xml.skipCurrentElement();
            }
        }
        
    } catch (const QString& error) {
        setError(error);
        file.close();
        return false;
    }
    
    if (xml.hasError()) {
        setError(QString("XML 파싱 오류: %1").arg(xml.errorString()));
        file.close();
        return false;
    }
    
    file.close();
    return true;
}

bool RecipeManager::deleteRecipe(const QString& recipeName) {
    if (recipeName.isEmpty()) {
        setError("레시피 이름이 비어있습니다");
        return false;
    }
    
    // 새로운 구조만 지원: recipes/레시피명/ 폴더
    QString recipeDir = QDir(getRecipesDirectory()).absoluteFilePath(recipeName);
    
    if (!QDir(recipeDir).exists()) {
        setError(QString("삭제할 레시피가 존재하지 않습니다: %1").arg(recipeName));
        return false;
    }
    
    // 전체 폴더 삭제 (XML 파일, teach 폴더, weights 폴더 포함)
    QDir dir(recipeDir);
    if (!dir.removeRecursively()) {
        setError(QString("레시피 폴더를 삭제할 수 없습니다: %1").arg(recipeDir));
        return false;
    }
    
    qDebug() << "[RecipeManager] 레시피 폴더 삭제됨:" << recipeName;
    return true;
}

bool RecipeManager::renameRecipe(const QString& oldName, const QString& newName) {
    if (oldName.isEmpty() || newName.isEmpty()) {
        setError("레시피 이름이 비어있습니다");
        return false;
    }
    
    if (oldName == newName) {
        return true; // 같은 이름이면 변경할 필요 없음
    }
    
    QString oldFileName = QDir(getRecipesDirectory()).absoluteFilePath(oldName + "/" + oldName + ".xml");
    QString newFileName = QDir(getRecipesDirectory()).absoluteFilePath(newName + "/" + newName + ".xml");
    
    QFile oldFile(oldFileName);
    if (!oldFile.exists()) {
        setError(QString("변경할 레시피가 존재하지 않습니다: %1").arg(oldName));
        return false;
    }
    
    QFile newFile(newFileName);
    if (newFile.exists()) {
        setError(QString("새 레시피 이름이 이미 존재합니다: %1").arg(newName));
        return false;
    }
    
    // 파일 이름 변경 및 내용의 레시피 이름도 업데이트
    QVector<PatternInfo> patterns;
    if (!loadRecipeByName(oldName, patterns)) {
        return false;
    }
    
    if (!saveRecipeByName(newName, patterns)) {
        return false;
    }
    
    if (!deleteRecipe(oldName)) {
        // 새 파일은 생성되었지만 기존 파일 삭제 실패
        setError(QString("기존 레시피 파일 삭제 실패: %1").arg(oldName));
        return false;
    }
    
    return true;
}

bool RecipeManager::copyRecipe(const QString& sourceName, const QString& targetName, const QString& newCameraName) {
    if (sourceName.isEmpty() || targetName.isEmpty()) {
        setError("레시피 이름이 비어있습니다");
        return false;
    }
    
    if (sourceName == targetName) {
        setError("원본과 복사본의 이름이 같습니다");
        return false;
    }
    
    QString sourceFileName = QDir(getRecipesDirectory()).absoluteFilePath(sourceName + "/" + sourceName + ".xml");
    QString targetFileName = QDir(getRecipesDirectory()).absoluteFilePath(targetName + "/" + targetName + ".xml");
    
    QFile sourceFile(sourceFileName);
    if (!sourceFile.exists()) {
        setError(QString("복사할 레시피가 존재하지 않습니다: %1").arg(sourceName));
        return false;
    }
    
    QFile targetFile(targetFileName);
    if (targetFile.exists()) {
        setError(QString("대상 레시피 이름이 이미 존재합니다: %1").arg(targetName));
        return false;
    }
    
    // 대상 레시피 디렉토리 생성
    QString targetDir = QDir(getRecipesDirectory()).absoluteFilePath(targetName);
    QDir dir;
    if (!dir.exists(targetDir)) {
        if (!dir.mkpath(targetDir)) {
            setError(QString("레시피 디렉토리 생성 실패: %1").arg(targetDir));
            return false;
        }
    }
    
    // XML 파일 읽기
    if (!sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(QString("원본 레시피 파일을 열 수 없습니다: %1").arg(sourceFileName));
        return false;
    }
    
    QByteArray xmlData = sourceFile.readAll();
    sourceFile.close();
    
    // 카메라 이름 변경이 필요한 경우 XML 수정
    if (!newCameraName.isEmpty()) {
        QDomDocument doc;
        if (!doc.setContent(xmlData)) {
            setError("XML 파싱 실패");
            return false;
        }
        
        // 모든 Camera 요소의 serialNumber 속성 변경
        QDomNodeList cameraNodes = doc.elementsByTagName("Camera");
        for (int i = 0; i < cameraNodes.count(); ++i) {
            QDomElement cameraElement = cameraNodes.at(i).toElement();
            if (!cameraElement.isNull()) {
                cameraElement.setAttribute("serialNumber", newCameraName);
            }
        }
        
        xmlData = doc.toByteArray();
    }
    
    // 대상 파일에 쓰기
    if (!targetFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(QString("대상 레시피 파일을 생성할 수 없습니다: %1").arg(targetFileName));
        return false;
    }
    
    targetFile.write(xmlData);
    targetFile.close();
    
    // 이미지 폴더 복사 (teach, strip, crimp 등)
    QString sourceDir = QDir(getRecipesDirectory()).absoluteFilePath(sourceName);
    QStringList subDirs = {"teach", "strip", "crimp"};
    
    for (const QString& subDir : subDirs) {
        QString sourceSubDir = sourceDir + "/" + subDir;
        QString targetSubDir = targetDir + "/" + subDir;
        
        if (QDir(sourceSubDir).exists()) {
            if (!dir.mkpath(targetSubDir)) {
                qWarning() << "서브 디렉토리 생성 실패:" << targetSubDir;
                continue;
            }
            
            // 모든 파일 복사
            QDir srcDir(sourceSubDir);
            QStringList files = srcDir.entryList(QDir::Files);
            for (const QString& fileName : files) {
                QString srcFile = sourceSubDir + "/" + fileName;
                QString dstFile = targetSubDir + "/" + fileName;
                if (!QFile::copy(srcFile, dstFile)) {
                    qWarning() << "파일 복사 실패:" << srcFile << "->" << dstFile;
                }
            }
        }
    }
    
    return true;
}

// 연결된 카메라 정보와 함께 레시피 저장




QStringList RecipeManager::getRecipeCameraUuids(const QString& recipeName)
{
    QStringList cameraUuids;
    
    if (recipeName.isEmpty()) {
        setError("레시피 이름이 비어있습니다");
        return cameraUuids;
    }
    
    QString fileName = QDir(getRecipesDirectory()).absoluteFilePath(recipeName + "/" + recipeName + ".xml");
    QFile file(fileName);
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(QString("레시피 파일을 열 수 없습니다: %1").arg(fileName));
        return cameraUuids;
    }
    
    QXmlStreamReader xml(&file);
    
    while (!xml.atEnd()) {
        xml.readNext();
        
        if (xml.isStartElement()) {
            if (xml.name() == QLatin1String("Cameras")) {
                // Cameras 섹션에서 카메라 UUID들 읽기
                while (!xml.atEnd()) {
                    xml.readNext();
                    
                    if (xml.isEndElement() && xml.name() == QLatin1String("Cameras")) {
                        break;
                    }
                    
                    if (xml.isStartElement() && xml.name() == QLatin1String("Camera")) {
                        QXmlStreamAttributes attributes = xml.attributes();
                        QString uuid = attributes.value("uuid").toString();
                        if (!uuid.isEmpty() && !cameraUuids.contains(uuid)) {
                            cameraUuids.append(uuid);
                        }
                    }
                }
            }
        }
    }
    
    if (xml.hasError()) {
        setError(QString("XML 파싱 오류: %1").arg(xml.errorString()));
        return QStringList();
    }
    
    file.close();
    return cameraUuids;
}

QString RecipeManager::getRecipeCameraName(const QString& recipeName) {
    if (recipeName.isEmpty()) {
        setError("레시피 이름이 비어있습니다");
        return QString();
    }
    
    QString fileName = QDir(getRecipesDirectory()).absoluteFilePath(recipeName + "/" + recipeName + ".xml");
    QFile file(fileName);
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(QString("레시피 파일을 열 수 없습니다: %1").arg(fileName));
        return QString();
    }
    
    QXmlStreamReader xml(&file);
    
    while (!xml.atEnd()) {
        xml.readNext();
        
        if (xml.isStartElement() && xml.name() == QLatin1String("Camera")) {
            QXmlStreamAttributes attributes = xml.attributes();
            QString cameraName = attributes.value("name").toString();
            file.close();
            return cameraName;
        }
    }
    
    if (xml.hasError()) {
        setError(QString("XML 파싱 오류: %1").arg(xml.errorString()));
    }
    
    file.close();
    return QString();
}

// 기존 레시피에서 메인 카메라 티칭 이미지 불러오기
bool RecipeManager::loadMainCameraImage(const QString& recipeName, cv::Mat& outImage, QString& outCameraName) {
    QString recipePath = QDir(getRecipesDirectory()).absoluteFilePath(recipeName + "/" + recipeName + ".xml");
    QFile file(recipePath);
    
    if (!file.exists()) {
        setError(QString("레시피 파일을 찾을 수 없음: %1").arg(recipePath));
        return false;
    }
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(QString("레시피 파일을 열 수 없음: %1").arg(recipePath));
        return false;
    }
    
    QXmlStreamReader xml(&file);
    bool found = false;
    
    try {
        while (!xml.atEnd() && !found) {
            xml.readNext();
            
            if (xml.isStartElement()) {
                // 첫 번째 Camera 섹션 찾기
                if (xml.name() == QLatin1String("Camera")) {
                    QXmlStreamAttributes attributes = xml.attributes();
                    outCameraName = attributes.value("name").toString();
                    
                    // teachingImage 속성에서 base64 데이터 읽기
                    QString base64Data = attributes.value("teachingImage").toString();
                    
                    qDebug() << "[RecipeManager] 카메라 찾음:" << outCameraName;
                    qDebug() << "[RecipeManager] teachingImage 속성 길이:" << base64Data.length();
                    
                    if (!base64Data.isEmpty()) {
                        // base64 디코딩
                        QByteArray imageData = QByteArray::fromBase64(base64Data.toLatin1());
                        
                        if (!imageData.isEmpty()) {
                            qDebug() << "[RecipeManager] base64 디코딩 완료, 크기:" << imageData.size();
                            
                            // QImage로 로드
                            QImage qImage;
                            if (qImage.loadFromData(imageData)) {
                                qDebug() << "[RecipeManager] 이미지 로드 성공:" << qImage.width() << "x" << qImage.height();
                                
                                // cv::Mat으로 변환
                                QImage rgbImage = qImage.convertToFormat(QImage::Format_RGB888);
                                outImage = cv::Mat(rgbImage.height(), rgbImage.width(), CV_8UC3,
                                                  (void*)rgbImage.constBits(), rgbImage.bytesPerLine()).clone();
                                cv::cvtColor(outImage, outImage, cv::COLOR_RGB2BGR);
                                
                                found = true;
                                qDebug() << "[RecipeManager] cv::Mat 변환 완료:" << outImage.cols << "x" << outImage.rows;
                            } else {
                                qDebug() << "[RecipeManager] QImage 로드 실패";
                            }
                        } else {
                            qDebug() << "[RecipeManager] base64 디코딩 실패 - 데이터가 비었음";
                        }
                    } else {
                        qDebug() << "[RecipeManager] teachingImage 속성이 비어있음";
                        // TeachingImage 요소 찾기 (속성이 아닌 경우)
                        while (!xml.atEnd()) {
                            xml.readNext();
                            
                            if (xml.isEndElement() && xml.name() == QLatin1String("Camera")) {
                                break;
                            }
                            
                            if (xml.isStartElement() && xml.name() == QLatin1String("TeachingImage")) {
                                base64Data = xml.readElementText();
                                
                                if (!base64Data.isEmpty()) {
                                    qDebug() << "[RecipeManager] TeachingImage 요소에서 base64 데이터 길이:" << base64Data.length();
                                    
                                    QByteArray imageData = QByteArray::fromBase64(base64Data.toLatin1());
                                    
                                    if (!imageData.isEmpty()) {
                                        QImage qImage;
                                        if (qImage.loadFromData(imageData)) {
                                            qDebug() << "[RecipeManager] 이미지 로드 성공:" << qImage.width() << "x" << qImage.height();
                                            
                                            QImage rgbImage = qImage.convertToFormat(QImage::Format_RGB888);
                                            outImage = cv::Mat(rgbImage.height(), rgbImage.width(), CV_8UC3,
                                                              (void*)rgbImage.constBits(), rgbImage.bytesPerLine()).clone();
                                            cv::cvtColor(outImage, outImage, cv::COLOR_RGB2BGR);
                                            
                                            found = true;
                                            qDebug() << "[RecipeManager] cv::Mat 변환 완료:" << outImage.cols << "x" << outImage.rows;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        file.close();
        setError(QString("예외 발생: %1").arg(e.what()));
        return false;
    }
    
    if (xml.hasError()) {
        file.close();
        setError(QString("XML 파싱 오류: %1").arg(xml.errorString()));
        return false;
    }
    
    file.close();
    
    if (!found) {
        setError("레시피에서 TeachingImage를 찾을 수 없음");
        return false;
    }
    
    return true;
}