#include "InsProcessor.h"
#include "ImageProcessor.h"
#include "ConfigManager.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QCoreApplication>
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QPen>
#include <QFontMetrics>
#include <numeric>
#include <chrono>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

// ===== 플랫폼별 PatchCore 인터페이스 래퍼 =====
namespace {
    bool initPatchCoreModel(const QString& modelPath) {
#ifdef USE_TENSORRT
        return ImageProcessor::initPatchCoreTensorRT(modelPath);
#elif defined(USE_ONNX)
        // ONNX는 .onnx 확장자 사용
        QString onnxPath = modelPath;
        onnxPath.replace(".trt", ".onnx");
        return ImageProcessor::initPatchCoreONNX(onnxPath);
#else
        qCritical() << "PatchCore: TensorRT 또는 ONNX Runtime이 필요합니다";
        return false;
#endif
    }
}

InsProcessor::InsProcessor(QObject *parent) : QObject(parent)
{
    logDebug("InsProcessor 초기화됨");
}

InsProcessor::~InsProcessor()
{
    logDebug("InsProcessor 소멸됨");
}

void InsProcessor::warmupAnomalyModels(const QList<PatternInfo>& patterns, const QString& recipeName)
{
    // ANOMALY 타입 패턴에서 사용하는 모든 고유 모델 경로와 패턴 크기 수집
    QMap<QString, QSize> modelSizes;  // 모델 경로 -> 패턴 크기
    for (const PatternInfo& pattern : patterns) {
        if (pattern.type == PatternType::INS && pattern.enabled) {
            QString weightsDir = QCoreApplication::applicationDirPath() + "/recipes/" + recipeName + "/weights";
            QString modelPath;
            
            if (pattern.inspectionMethod == static_cast<int>(InspectionMethod::A_PC)) {
#ifdef USE_TENSORRT
                modelPath = weightsDir + "/" + pattern.name + "/" + pattern.name + ".trt";
#else
                modelPath = weightsDir + "/" + pattern.name + "/" + pattern.name + ".onnx";
#endif
                QSize patternSize(static_cast<int>(pattern.rect.width()), 
                                static_cast<int>(pattern.rect.height()));
                modelSizes[modelPath] = patternSize;
            }
            else if (pattern.inspectionMethod == static_cast<int>(InspectionMethod::A_PD)) {
#ifdef USE_TENSORRT
                modelPath = weightsDir + "/" + pattern.name + "/" + pattern.name + "_padim.trt";
#else
                modelPath = weightsDir + "/" + pattern.name + "/" + pattern.name + "_padim.onnx";
#endif
                QSize patternSize(static_cast<int>(pattern.rect.width()), 
                                static_cast<int>(pattern.rect.height()));
                modelSizes[modelPath] = patternSize;
            }
        }
    }
    
    if (modelSizes.isEmpty()) {
        logDebug("워밍업할 AI 모델이 없습니다.");
        return;
    }
    
    logDebug(QString("%1개 AI 모델 초기화 시작...").arg(modelSizes.size()));
    
    int loadedCount = 0;
    for (auto it = modelSizes.begin(); it != modelSizes.end(); ++it) {
        const QString& modelPath = it.key();
        const QSize& patternSize = it.value();
        
        try {
            // 모델 파일 존재 확인
            if (!QFile::exists(modelPath)) {
                logDebug(QString("모델 파일이 존재하지 않음: %1").arg(modelPath));
                continue;
            }
            
            // 패턴 크기 유효성 검증
            if (patternSize.width() <= 0 || patternSize.height() <= 0) {
                logDebug(QString("잘못된 패턴 크기: %1x%2").arg(patternSize.width()).arg(patternSize.height()));
                continue;
            }
            
            // 실제 패턴 크기의 더미 이미지 생성
            cv::Mat dummyImage = cv::Mat(patternSize.height(), patternSize.width(), CV_8UC3, cv::Scalar(128, 128, 128));
            
            // 모델 타입 판별 (_padim 접미사로 구분)
            bool isPaDiM = modelPath.contains("_padim");
            
            // 모델 초기화
            bool initSuccess = false;
            if (isPaDiM) {
#ifdef USE_TENSORRT
                initSuccess = ImageProcessor::initPaDiMTensorRT(modelPath);
#elif defined(USE_ONNX)
                initSuccess = ImageProcessor::initPaDiMONNX(modelPath);
#endif
            } else {
                initSuccess = initPatchCoreModel(modelPath);
            }
            
            if (initSuccess) {
                // 멀티모델 추론으로 워밍업
                QMap<QString, std::vector<cv::Mat>> modelImages;
                modelImages[modelPath] = {dummyImage};
                QMap<QString, std::vector<float>> modelScores;
                QMap<QString, std::vector<cv::Mat>> modelMaps;
                
                if (isPaDiM) {
#ifdef USE_TENSORRT
                    ImageProcessor::runPaDiMTensorRTMultiModelInference(modelImages, modelScores, modelMaps);
#elif defined(USE_ONNX)
                    ImageProcessor::runPaDiMONNXInference(modelPath, {dummyImage}, modelScores[modelPath], modelMaps[modelPath]);
#endif
                } else {
#ifdef USE_TENSORRT
                    ImageProcessor::runPatchCoreTensorRTMultiModelInference(modelImages, modelScores, modelMaps);
#elif defined(USE_ONNX)
                    ImageProcessor::runPatchCoreONNXInference(modelPath, {dummyImage}, modelScores[modelPath], modelMaps[modelPath]);
#endif
                }
                
                loadedCount++;
                logDebug(QString("%1 (%2x%3) 완료").arg(modelPath.section('/', -1)).arg(patternSize.width()).arg(patternSize.height()));
            } else {
                logDebug(QString("모델 초기화 실패: %1").arg(modelPath));
            }
        } catch (const std::exception& e) {
            logDebug(QString("모델 워밍업 중 예외 발생: %1 - %2").arg(modelPath).arg(e.what()));
        } catch (...) {
            logDebug(QString("모델 워밍업 중 알 수 없는 오류: %1").arg(modelPath));
        }
    }
    
    logDebug(QString("완료: %1/%2개 모델 준비됨").arg(loadedCount).arg(modelSizes.size()));
}

InspectionResult InsProcessor::performInspection(const cv::Mat &image, const QList<PatternInfo> &patterns, const QString& cameraName)
{
    InspectionResult result;

    if (image.empty() || patterns.isEmpty())
    {
        logDebug("검사 실패: 이미지가 비어있거나 패턴이 없음");
        return result;
    }

    // 활성화된 INS 패턴 개수 카운트
    int insCount = 0;
    for (const PatternInfo &p : patterns) {
        if (p.type == PatternType::INS && p.enabled) {
            insCount++;
        }
    }
    
    // 검사 시작 시간 측정
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // 패턴 프레임 인덱스 추출 (로그용)
    int frameIndex = -1;
    if (!patterns.isEmpty()) {
        frameIndex = patterns.first().frameIndex;
    }
    
    // 프레임별 레이블 (CommonDefs.h의 FRAME_LABELS 사용)
    QString stageLabel = (frameIndex >= 0 && frameIndex < FRAME_LABELS.size()) ? FRAME_LABELS[frameIndex] : QString("Frame %1").arg(frameIndex);
    
    // 검사 시작 로그 (옵션 2 형식)
    QString cameraInfo = cameraName.isEmpty() ? "" : QString(" (Cam %1)").arg(cameraName);
    logDebug(QString("[Inspect Start] %1%2").arg(stageLabel).arg(cameraInfo));

    result.isPassed = true;
    
    // PatchCore는 각 ANOMALY 패턴별로 개별 추론 (전체 영상 추론 제거)

    // 1. 활성화된 패턴들을 유형별로 분류
    QList<PatternInfo> roiPatterns, fidPatterns, insPatterns;
    for (const PatternInfo &pattern : patterns)
    {
        if (!pattern.enabled)
        {
            logDebug(QString("패턴 '%1' 비활성화됨 - 검사 건너뜀").arg(pattern.name));
            continue;
        }

        switch (pattern.type)
        {
        case PatternType::ROI:
            roiPatterns.append(pattern);
            break;

        case PatternType::FID:
            fidPatterns.append(pattern);
            break;

        case PatternType::INS:
            insPatterns.append(pattern);
            break;

        case PatternType::FIL:
            logDebug(QString("필터 패턴 발견 (무시됨): '%1'").arg(pattern.name));
            break;
        }
    }

    // 2. ROI 그룹 분석 및 매핑
    QMap<QUuid, QUuid> patternToRoiMap; // 패턴 ID -> 속한 ROI ID 매핑
    QMap<QUuid, QRect> roiGroupAreas;   // ROI ID -> ROI 영역 매핑
    QList<QRect> activeRoiRects;        // 전체 검사 영역 목록 (기존 호환성)

    if (roiPatterns.isEmpty())
    {
        // ROI가 없으면 전체 이미지 영역을 활성 영역으로 간주
        activeRoiRects.append(QRect(0, 0, image.cols, image.rows));
        logDebug("활성화된 ROI 패턴이 없음, 전체 영역 검사");
    }
    else
    {
        // ROI 기반 그룹 분석
        for (const PatternInfo &roiPattern : roiPatterns)
        {
            activeRoiRects.append(QRect(
                static_cast<int>(roiPattern.rect.x()),
                static_cast<int>(roiPattern.rect.y()),
                static_cast<int>(roiPattern.rect.width()),
                static_cast<int>(roiPattern.rect.height())));
            roiGroupAreas[roiPattern.id] = QRect(
                static_cast<int>(roiPattern.rect.x()),
                static_cast<int>(roiPattern.rect.y()),
                static_cast<int>(roiPattern.rect.width()),
                static_cast<int>(roiPattern.rect.height()));

            // ROI의 직접 자식들 (FID들) 매핑
            for (const QUuid &childId : roiPattern.childIds)
            {
                patternToRoiMap[childId] = roiPattern.id;

                // FID의 자식들 (INS들)도 같은 ROI에 매핑
                for (const PatternInfo &fidPattern : fidPatterns)
                {
                    if (fidPattern.id == childId)
                    {
                        for (const QUuid &insId : fidPattern.childIds)
                        {
                            patternToRoiMap[insId] = roiPattern.id;
                        }
                        break;
                    }
                }
            }
        }
    }

    // 3. 패턴이 특정 ROI 그룹 내에 있는지 확인하는 헬퍼 함수 (FID 매칭 영역 제한용)
    auto isInGroupROI = [&patternToRoiMap, &roiGroupAreas, &roiPatterns, this](const QUuid &patternId, const QPoint &center, bool isFidPattern) -> bool
    {
        // ROI가 없으면 항상 true 반환 (기존 동작 유지)
        if (roiPatterns.isEmpty())
            return true;

        // INS 패턴은 ROI 제한 없이 항상 검사 (ROI는 FID 매칭 영역만 제한)
        if (!isFidPattern)
            return true;

        // FID 패턴인 경우: 패턴이 특정 ROI 그룹에 속하는지 확인
        if (patternToRoiMap.contains(patternId))
        {
            QUuid roiId = patternToRoiMap[patternId];
            if (roiGroupAreas.contains(roiId))
            {
                QRect roiRect = roiGroupAreas[roiId];
                bool inGroup = roiRect.contains(center);

                if (!inGroup)
                {
                    logDebug(QString("FID 패턴 '%1': 그룹 ROI 영역 외부에 위치하여 매칭 제외")
                                 .arg(patternId.toString().left(8)));
                }

                return inGroup;
            }
        }

        // 그룹에 속하지 않은 FID 패턴인 경우 ROI 영역 내에서만 매칭
        for (const QRect &roiRect : roiGroupAreas.values())
        {
            if (roiRect.contains(center))
                return true;
        }

        return false;
    };

    // 4. 기존 ROI 영역 확인 함수 (기존 코드 호환성 유지)
    auto isInROI = [&activeRoiRects, &roiPatterns](const QPoint &center) -> bool
    {
        if (roiPatterns.isEmpty())
            return true;

        for (const QRect &roiRect : activeRoiRects)
        {
            if (roiRect.contains(center))
                return true;
        }

        return false;
    };

    // 5. FID 패턴 매칭 수행 (그룹 ROI 제한 적용)
    if (!fidPatterns.isEmpty())
    {
        for (const PatternInfo &pattern : fidPatterns)
        {
            // 매칭 검사가 활성화된 경우에만 수행
            if (!pattern.runInspection)
            {
                logDebug(QString("FID 패턴 '%1': 검사 비활성화됨, 건너뜀").arg(pattern.name));
                continue;
            }

            // **중요**: ROI 영역은 FID 매칭 영역을 제한 (패턴 매칭 최적화)
            QPoint patternCenter = QPoint(
                static_cast<int>(pattern.rect.center().x()),
                static_cast<int>(pattern.rect.center().y()));
            if (!isInGroupROI(pattern.id, patternCenter, true))  // true = FID 패턴
            {
                logDebug(QString("FID 패턴 '%1': ROI 영역 외부에 있어 매칭에서 제외됨").arg(pattern.name));
                continue;
            }

            // 기존 ROI 검사도 유지 (하위 호환성)
            if (!isInROI(patternCenter))
            {
                logDebug(QString("FID 패턴 '%1': ROI 영역 외부에 있어 검사에서 제외됨").arg(pattern.name));
                continue;
            }

            double matchScore = 0.0;
            cv::Point matchLoc;
            double matchAngle = 0.0;

            // **중요**: matchFiducial에 그룹 ROI 정보도 전달
            auto fidStart = std::chrono::high_resolution_clock::now();
            bool fidMatched = matchFiducial(image, pattern, matchScore, matchLoc, matchAngle, patterns);
            auto fidEnd = std::chrono::high_resolution_clock::now();
            auto fidDuration = std::chrono::duration_cast<std::chrono::milliseconds>(fidEnd - fidStart).count();
            
            // FID 패턴 로그 (점수 포함)
            logDebug(QString("  └─ <font color='#7094DB'>FID: %1</font> [%2%/%3%] (%4ms)")
                .arg(pattern.name)
                .arg(matchScore * 100.0, 0, 'f', 1)
                .arg(pattern.matchThreshold, 0, 'f', 1)
                .arg(fidDuration));

            // 매칭 성공 시 검출된 각도를 FID 그룹 전체에 적용
            if (fidMatched)
            {
                // FID 패턴의 각도를 검출된 각도로 업데이트
                double oldAngle = pattern.angle;
                const_cast<PatternInfo &>(pattern).angle = matchAngle;
                result.angles[pattern.id] = matchAngle;
            }
            else
            {
                // 매칭 실패 시에도 검출된 각도는 적용
                logDebug(QString("FID pattern '%1' match failed - but detected angle applied: %2°")
                             .arg(pattern.name)
                             .arg(matchAngle, 0, 'f', 2));

                // FID 패턴의 각도를 검출된 각도로 업데이트 (매칭 실패와 무관)
                double oldAngle = pattern.angle;
                const_cast<PatternInfo &>(pattern).angle = matchAngle;
                result.angles[pattern.id] = matchAngle;

                // 마찬가지로 매칭 실패 시에도 자식 INS의 티칭 각도는 유지합니다.
            }

            // 결과 기록
            result.fidResults[pattern.id] = fidMatched;
            result.matchScores[pattern.id] = matchScore;
            result.locations[pattern.id] = matchLoc;    
            result.isPassed = result.isPassed && fidMatched;
        }
    }

    // 6. INS 패턴 검사 수행 (그룹 ROI 제한 적용)
    if (!insPatterns.isEmpty())
    {
        // ===== ANOMALY 패턴 배치 처리 =====
        // 같은 모델을 사용하는 ANOMALY 패턴들을 그룹화
        QMap<QString, QList<PatternInfo>> anomalyGroupsAPC; // key: 모델 경로 (A-PC)
        QMap<QString, QList<PatternInfo>> anomalyGroupsAPD; // key: 모델 경로 (A-PD)
        QMap<QUuid, int> anomalyPatternIndexMap; // 패턴 ID -> 배치 내 인덱스
        
        int totalAnomalyCount = 0;
        for (const PatternInfo &pattern : insPatterns)
        {
            if (pattern.inspectionMethod == InspectionMethod::A_PC)
            {
                totalAnomalyCount++;
                // 현재 레시피명 가져오기
                QString recipeName = ConfigManager::instance()->getLastRecipePath();
                if (recipeName.isEmpty()) {
                    recipeName = "default";
                }
                
                // 모델 파일 경로 (레시피별)
                QString weightsDir = QCoreApplication::applicationDirPath() + "/recipes/" + recipeName + "/weights";
#ifdef USE_TENSORRT
                QString modelPath = weightsDir + "/" + pattern.name + "/" + pattern.name + ".trt";
#else
                QString modelPath = weightsDir + "/" + pattern.name + "/" + pattern.name + ".onnx";
#endif
                anomalyGroupsAPC[modelPath].append(pattern);
            }
            else if (pattern.inspectionMethod == InspectionMethod::A_PD)
            {
                totalAnomalyCount++;
                // 현재 레시피명 가져오기
                QString recipeName = ConfigManager::instance()->getLastRecipePath();
                if (recipeName.isEmpty()) {
                    recipeName = "default";
                }
                
                // 모델 파일 경로 (레시피별, _padim suffix)
                QString weightsDir = QCoreApplication::applicationDirPath() + "/recipes/" + recipeName + "/weights";
#ifdef USE_TENSORRT
                QString modelPath = weightsDir + "/" + pattern.name + "/" + pattern.name + "_padim.trt";
#else
                QString modelPath = weightsDir + "/" + pattern.name + "/" + pattern.name + "_padim.onnx";
#endif
                anomalyGroupsAPD[modelPath].append(pattern);
            }
        }
        
        // ANOMALY 전체 처리 시작
        auto anomalyBatchStart = std::chrono::high_resolution_clock::now();
        int anomalyGroupCount = anomalyGroupsAPC.size() + anomalyGroupsAPD.size();
        
#ifdef USE_TENSORRT
        // ===== A-PC TensorRT 멀티모델 병렬 처리 =====
        auto apcBatchStart = std::chrono::high_resolution_clock::now();
        int apcPatternCount = 0;
        if (anomalyGroupsAPC.size() >= 1) {
            // 모든 모델 로드
            QMap<QString, std::vector<cv::Mat>> modelImages;
            QMap<QString, QList<PatternInfo>> modelValidPatterns;
            
            int loadedModelCount = 0;
            for (auto groupIt = anomalyGroupsAPC.begin(); groupIt != anomalyGroupsAPC.end(); ++groupIt) {
                const QString& modelPath = groupIt.key();
                const QList<PatternInfo>& group = groupIt.value();
                
                if (group.isEmpty()) continue;
                
                // 모델 로드
                if (!initPatchCoreModel(modelPath)) {
                    logDebug(QString("ANOMALY: 모델 로드 실패 - %1").arg(modelPath));
                    for (const PatternInfo& pattern : group) {
                        result.insResults[pattern.id] = false;
                        result.insScores[pattern.id] = 0.0;
                        result.insMethodTypes[pattern.id] = InspectionMethod::A_PC;
                        result.isPassed = false;
                    }
                    continue;
                }
                loadedModelCount++;
                
                std::vector<cv::Mat> roiImages;
                QList<PatternInfo> validPatterns;
                
                // ROI 추출 (기존 로직과 동일)
                for (const PatternInfo &pattern : group) {
                    QRect adjustedRect = QRect(
                        static_cast<int>(pattern.rect.x()),
                        static_cast<int>(pattern.rect.y()),
                        static_cast<int>(pattern.rect.width()),
                        static_cast<int>(pattern.rect.height()));
                    
                    // 부모 FID 정보 확인 및 위치 조정
                    if (!pattern.parentId.isNull())
                    {
                        if (result.fidResults.contains(pattern.parentId))
                        {
                            if (!result.fidResults[pattern.parentId])
                            {
                                // 부모 FID 매칭 실패
                                result.insResults[pattern.id] = false;
                                result.insScores[pattern.id] = 0.0;
                                result.insMethodTypes[pattern.id] = InspectionMethod::A_PC;
                                result.isPassed = false;
                                continue;
                            }
                            
                            // FID 기반 위치 조정
                            double fidScore = result.matchScores.value(pattern.parentId, 0.0);
                            if (fidScore < 0.999 && result.locations.contains(pattern.parentId))
                            {
                                cv::Point fidLoc = result.locations[pattern.parentId];
                                double fidAngle = result.angles[pattern.parentId];
                                
                                // FID 원래 정보 찾기
                                QPoint originalFidCenter;
                                for (const PatternInfo &fid : fidPatterns)
                                {
                                    if (fid.id == pattern.parentId)
                                    {
                                        originalFidCenter = QPoint(
                                            static_cast<int>(fid.rect.center().x()),
                                            static_cast<int>(fid.rect.center().y()));
                                        break;
                                    }
                                }
                                
                                // 위치 오프셋 계산
                                cv::Point parentOffset(
                                    fidLoc.x - originalFidCenter.x(),
                                    fidLoc.y - originalFidCenter.y());
                                
                                // 회전 각도 차이 계산
                                double parentFidTeachingAngle = 0.0;
                                for (const PatternInfo &p : patterns)
                                {
                                    if (p.id == pattern.parentId)
                                    {
                                        parentFidTeachingAngle = p.angle;
                                        break;
                                    }
                                }
                                double fidAngleDiff = fidAngle - parentFidTeachingAngle;
                                
                                // INS 패턴 중심점 계산
                                QPointF insOriginalCenter = pattern.rect.center();
                                QPointF relativePos(
                                    insOriginalCenter.x() - originalFidCenter.x(),
                                    insOriginalCenter.y() - originalFidCenter.y());
                                
                                // 회전 적용
                                double rad = fidAngleDiff * M_PI / 180.0;
                                double rotatedX = relativePos.x() * cos(rad) - relativePos.y() * sin(rad);
                                double rotatedY = relativePos.x() * sin(rad) + relativePos.y() * cos(rad);
                                
                                // 새로운 중심점 계산
                                int newCenterX = static_cast<int>(std::lround(fidLoc.x + rotatedX));
                                int newCenterY = static_cast<int>(std::lround(fidLoc.y + rotatedY));
                                
                                adjustedRect = QRect(
                                    newCenterX - pattern.rect.width() / 2,
                                    newCenterY - pattern.rect.height() / 2,
                                    pattern.rect.width(),
                                    pattern.rect.height());
                            }
                        }
                    }
                    
                    // ROI 유효성 검사 제거: INS 패턴은 ROI 영역 밖에서도 검사
                    // (ROI는 FID 매칭 영역만 제한하는 용도)
                    
                    // 경계 조정
                    if (adjustedRect.x() < 0 || adjustedRect.y() < 0 ||
                        adjustedRect.x() + adjustedRect.width() > image.cols ||
                        adjustedRect.y() + adjustedRect.height() > image.rows) {
                        int x = std::max(0, adjustedRect.x());
                        int y = std::max(0, adjustedRect.y());
                        int width = std::min(image.cols - x, adjustedRect.width());
                        int height = std::min(image.rows - y, adjustedRect.height());
                        if (width < 10 || height < 10) {
                            result.insResults[pattern.id] = false;
                            result.insScores[pattern.id] = 0.0;
                            result.insMethodTypes[pattern.id] = InspectionMethod::A_PC;
                            result.isPassed = false;
                            continue;
                        }
                        adjustedRect = QRect(x, y, width, height);
                    }
                    
                    cv::Rect roiRect(adjustedRect.x(), adjustedRect.y(), adjustedRect.width(), adjustedRect.height());
                    cv::Mat roiImage = image(roiRect).clone();
                    
                    roiImages.push_back(roiImage);
                    validPatterns.append(pattern);
                    result.adjustedRects[pattern.id] = adjustedRect;
                }
                
                if (!roiImages.empty()) {
                    modelImages[modelPath] = roiImages;
                    modelValidPatterns[modelPath] = validPatterns;
                }
            }
            
            // 멀티모델 병렬 추론 실행
            QMap<QString, std::vector<float>> modelScores;
            QMap<QString, std::vector<cv::Mat>> modelMaps;
            
            auto inferenceStart = std::chrono::high_resolution_clock::now();
            bool multiSuccess = ImageProcessor::runPatchCoreTensorRTMultiModelInference(
                modelImages, modelScores, modelMaps);
            auto inferenceEnd = std::chrono::high_resolution_clock::now();
            auto inferenceDuration = std::chrono::duration_cast<std::chrono::milliseconds>(inferenceEnd - inferenceStart).count();
            
            // 전체 패턴 수 계산
            int totalPatterns = 0;
            for (auto it = modelValidPatterns.begin(); it != modelValidPatterns.end(); ++it) {
                totalPatterns += it.value().size();
            }
            int avgPatternTime = (totalPatterns > 0) ? (inferenceDuration / totalPatterns) : 0;
            
            if (multiSuccess) {
                // 결과 처리 (기존 로직과 동일)
                for (auto it = modelValidPatterns.begin(); it != modelValidPatterns.end(); ++it) {
                    const QString& modelPath = it.key();
                    const QList<PatternInfo>& validPatterns = it.value();
                    
                    if (!modelScores.contains(modelPath) || !modelMaps.contains(modelPath)) {
                        continue;
                    }
                    
                    const std::vector<float>& anomalyScores = modelScores[modelPath];
                    const std::vector<cv::Mat>& anomalyMaps = modelMaps[modelPath];
                    
                    for (int i = 0; i < validPatterns.size(); i++) {
                        const PatternInfo& pattern = validPatterns[i];
                        
                        if (i >= anomalyScores.size() || i >= anomalyMaps.size()) {
                            break;
                        }
                        
                        float roiAnomalyScore = anomalyScores[i];
                        cv::Mat anomalyMap = anomalyMaps[i];
                        roiAnomalyScore = std::max(0.0f, std::min(100.0f, roiAnomalyScore));
                        
                        // Threshold 처리 (기존 로직과 동일)
                        cv::Mat binaryMask;
                        cv::threshold(anomalyMap, binaryMask, pattern.passThreshold, 255, cv::THRESH_BINARY);
                        binaryMask.convertTo(binaryMask, CV_8U);
                        
                        std::vector<std::vector<cv::Point>> contours;
                        cv::findContours(binaryMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
                        
                        bool hasDefect = false;
                        std::vector<std::vector<cv::Point>> defectContours;
                        QRectF adjustedRectF = result.adjustedRects[pattern.id];
                        int adjustedX = static_cast<int>(adjustedRectF.x());
                        int adjustedY = static_cast<int>(adjustedRectF.y());
                        
                        for (const auto& contour : contours) {
                            int blobSize = static_cast<int>(cv::contourArea(contour));
                            cv::Rect bbox = cv::boundingRect(contour);
                            
                            bool sizeCheck = (blobSize >= pattern.anomalyMinBlobSize);
                            bool widthCheck = (bbox.width >= pattern.anomalyMinDefectWidth);
                            bool heightCheck = (bbox.height >= pattern.anomalyMinDefectHeight);
                            
                            if (sizeCheck || (widthCheck && heightCheck)) {
                                hasDefect = true;
                                std::vector<cv::Point> absoluteContour;
                                for (const auto& pt : contour) {
                                    absoluteContour.push_back(cv::Point(pt.x + adjustedX, pt.y + adjustedY));
                                }
                                defectContours.push_back(absoluteContour);
                            }
                        }
                        
                        result.insScores[pattern.id] = static_cast<double>(roiAnomalyScore);
                        result.insResults[pattern.id] = !hasDefect;
                        result.insMethodTypes[pattern.id] = InspectionMethod::A_PC;
                        result.anomalyDefectContours[pattern.id] = defectContours;
                        result.anomalyRawMap[pattern.id] = anomalyMap.clone();
                        
                        // 히트맵 생성
                        cv::Mat normalized;
                        anomalyMap.convertTo(normalized, CV_8U, 255.0 / 100.0);
                        cv::Mat colorHeatmap;
                        cv::applyColorMap(normalized, colorHeatmap, cv::COLORMAP_JET);
                        result.anomalyHeatmap[pattern.id] = colorHeatmap.clone();
                        result.anomalyHeatmapRect[pattern.id] = pattern.rect;
                        
                        result.isPassed = result.isPassed && !hasDefect;
                        apcPatternCount++;
                        
                        QString insResultText = !hasDefect ? "PASS" : "NG";
                        QString resultColor = !hasDefect ? "<font color='#00FF00'>" : "<font color='#FF0000'>";
                        int defectCount = defectContours.size();
                        
                        QString methodName = "A-PC";
                        if (defectCount > 0)
                        {
                            int maxW = 0, maxH = 0;
                            for (const auto& contour : defectContours)
                            {
                                cv::Rect bbox = cv::boundingRect(contour);
                                if (bbox.width > maxW) maxW = bbox.width;
                                if (bbox.height > maxH) maxH = bbox.height;
                            }
                            logDebug(QString("  └─ <font color='#8BCB8B'>%1(%2)</font>: W:%3 H:%4 Detects:%5 (score=%6, thr=%7) [%8ms]")
                                .arg(pattern.name).arg(methodName).arg(maxW).arg(maxH).arg(defectCount)
                                .arg(roiAnomalyScore, 0, 'f', 2).arg(pattern.passThreshold, 0, 'f', 2).arg(avgPatternTime));
                        }
                        else
                        {
                            logDebug(QString("  └─ <font color='#8BCB8B'>%1(%2)</font>: %3%4</font> (score=%5, thr=%6) [%7ms]")
                                .arg(pattern.name).arg(methodName).arg(resultColor).arg(insResultText)
                                .arg(roiAnomalyScore, 0, 'f', 2).arg(pattern.passThreshold, 0, 'f', 2).arg(avgPatternTime));
                        }
                    }
                }
            }
        }
        
        // ===== A-PD TensorRT 멀티모델 병렬 처리 =====
        auto apdBatchStart = std::chrono::high_resolution_clock::now();
        int apdPatternCount = 0;
        if (anomalyGroupsAPD.size() >= 1) {
            // 모든 모델 로드
            QMap<QString, std::vector<cv::Mat>> modelImages;
            QMap<QString, QList<PatternInfo>> modelValidPatterns;
            
            int loadedModelCount = 0;
            for (auto groupIt = anomalyGroupsAPD.begin(); groupIt != anomalyGroupsAPD.end(); ++groupIt) {
                const QString& modelPath = groupIt.key();
                const QList<PatternInfo>& group = groupIt.value();
                
                if (group.isEmpty()) continue;
                
                // PaDiM 모델 로드
                if (!ImageProcessor::initPaDiMTensorRT(modelPath)) {
                    logDebug(QString("A-PD: 모델 로드 실패 - %1").arg(modelPath));
                    for (const PatternInfo& pattern : group) {
                        result.insResults[pattern.id] = false;
                        result.insScores[pattern.id] = 0.0;
                        result.insMethodTypes[pattern.id] = InspectionMethod::A_PD;
                        result.isPassed = false;
                    }
                    continue;
                }
                loadedModelCount++;
                
                std::vector<cv::Mat> roiImages;
                QList<PatternInfo> validPatterns;
                
                // ROI 추출 (A-PC와 동일한 로직)
                for (const PatternInfo &pattern : group) {
                    QRect adjustedRect = QRect(
                        static_cast<int>(pattern.rect.x()),
                        static_cast<int>(pattern.rect.y()),
                        static_cast<int>(pattern.rect.width()),
                        static_cast<int>(pattern.rect.height()));
                    
                    // 부모 FID 정보 확인 및 위치 조정
                    if (!pattern.parentId.isNull())
                    {
                        if (result.fidResults.contains(pattern.parentId))
                        {
                            if (!result.fidResults[pattern.parentId])
                            {
                                // 부모 FID 매칭 실패
                                result.insResults[pattern.id] = false;
                                result.insScores[pattern.id] = 0.0;
                                result.insMethodTypes[pattern.id] = InspectionMethod::A_PD;
                                result.isPassed = false;
                                continue;
                            }
                            
                            // FID 기반 위치 조정
                            double fidScore = result.matchScores.value(pattern.parentId, 0.0);
                            if (fidScore < 0.999 && result.locations.contains(pattern.parentId))
                            {
                                cv::Point fidLoc = result.locations[pattern.parentId];
                                double fidAngle = result.angles[pattern.parentId];
                                
                                // FID 원래 정보 찾기
                                QPoint originalFidCenter;
                                for (const PatternInfo &fid : fidPatterns)
                                {
                                    if (fid.id == pattern.parentId)
                                    {
                                        originalFidCenter = QPoint(
                                            static_cast<int>(fid.rect.center().x()),
                                            static_cast<int>(fid.rect.center().y()));
                                        break;
                                    }
                                }
                                
                                // 위치 오프셋 계산
                                cv::Point parentOffset(
                                    fidLoc.x - originalFidCenter.x(),
                                    fidLoc.y - originalFidCenter.y());
                                
                                // 회전 각도 차이 계산
                                double parentFidTeachingAngle = 0.0;
                                for (const PatternInfo &p : patterns)
                                {
                                    if (p.id == pattern.parentId)
                                    {
                                        parentFidTeachingAngle = p.angle;
                                        break;
                                    }
                                }
                                double fidAngleDiff = fidAngle - parentFidTeachingAngle;
                                
                                // INS 패턴 중심점 계산
                                QPointF insOriginalCenter = pattern.rect.center();
                                QPointF relativePos(
                                    insOriginalCenter.x() - originalFidCenter.x(),
                                    insOriginalCenter.y() - originalFidCenter.y());
                                
                                // 회전 적용
                                double rad = fidAngleDiff * M_PI / 180.0;
                                double rotatedX = relativePos.x() * cos(rad) - relativePos.y() * sin(rad);
                                double rotatedY = relativePos.x() * sin(rad) + relativePos.y() * cos(rad);
                                
                                // 새로운 중심점 계산
                                int newCenterX = static_cast<int>(std::lround(fidLoc.x + rotatedX));
                                int newCenterY = static_cast<int>(std::lround(fidLoc.y + rotatedY));
                                
                                adjustedRect = QRect(
                                    newCenterX - pattern.rect.width() / 2,
                                    newCenterY - pattern.rect.height() / 2,
                                    pattern.rect.width(),
                                    pattern.rect.height());
                            }
                        }
                    }
                    
                    // 경계 조정
                    if (adjustedRect.x() < 0 || adjustedRect.y() < 0 ||
                        adjustedRect.x() + adjustedRect.width() > image.cols ||
                        adjustedRect.y() + adjustedRect.height() > image.rows) {
                        int x = std::max(0, adjustedRect.x());
                        int y = std::max(0, adjustedRect.y());
                        int width = std::min(image.cols - x, adjustedRect.width());
                        int height = std::min(image.rows - y, adjustedRect.height());
                        if (width < 10 || height < 10) {
                            result.insResults[pattern.id] = false;
                            result.insScores[pattern.id] = 0.0;
                            result.insMethodTypes[pattern.id] = InspectionMethod::A_PD;
                            result.isPassed = false;
                            continue;
                        }
                        adjustedRect = QRect(x, y, width, height);
                    }
                    
                    cv::Rect roiRect(adjustedRect.x(), adjustedRect.y(), adjustedRect.width(), adjustedRect.height());
                    cv::Mat roiImage = image(roiRect).clone();
                    
                    roiImages.push_back(roiImage);
                    validPatterns.append(pattern);
                    result.adjustedRects[pattern.id] = adjustedRect;
                }
                
                if (!roiImages.empty()) {
                    modelImages[modelPath] = roiImages;
                    modelValidPatterns[modelPath] = validPatterns;
                }
            }
            
            // 멀티모델 병렬 추론 실행
            QMap<QString, std::vector<float>> modelScores;
            QMap<QString, std::vector<cv::Mat>> modelMaps;
            
            auto inferenceStart = std::chrono::high_resolution_clock::now();
            bool multiSuccess = ImageProcessor::runPaDiMTensorRTMultiModelInference(
                modelImages, modelScores, modelMaps);
            auto inferenceEnd = std::chrono::high_resolution_clock::now();
            auto inferenceDuration = std::chrono::duration_cast<std::chrono::milliseconds>(inferenceEnd - inferenceStart).count();
            
            // 전체 패턴 수 계산
            int totalPatterns = 0;
            for (auto it = modelValidPatterns.begin(); it != modelValidPatterns.end(); ++it) {
                totalPatterns += it.value().size();
            }
            int avgPatternTime = (totalPatterns > 0) ? (inferenceDuration / totalPatterns) : 0;
            
            if (multiSuccess) {
                // 결과 처리
                for (auto it = modelValidPatterns.begin(); it != modelValidPatterns.end(); ++it) {
                    const QString& modelPath = it.key();
                    const QList<PatternInfo>& validPatterns = it.value();
                    
                    if (!modelScores.contains(modelPath) || !modelMaps.contains(modelPath)) {
                        continue;
                    }
                    
                    const std::vector<float>& anomalyScores = modelScores[modelPath];
                    const std::vector<cv::Mat>& anomalyMaps = modelMaps[modelPath];
                    
                    for (int i = 0; i < validPatterns.size(); i++) {
                        const PatternInfo& pattern = validPatterns[i];
                        
                        if (i >= anomalyScores.size() || i >= anomalyMaps.size()) {
                            break;
                        }
                        
                        float roiAnomalyScore = anomalyScores[i];
                        cv::Mat anomalyMap = anomalyMaps[i];
                        roiAnomalyScore = std::max(0.0f, std::min(100.0f, roiAnomalyScore));
                        
                        // Threshold 처리
                        cv::Mat binaryMask;
                        cv::threshold(anomalyMap, binaryMask, pattern.passThreshold, 255, cv::THRESH_BINARY);
                        binaryMask.convertTo(binaryMask, CV_8U);
                        
                        std::vector<std::vector<cv::Point>> contours;
                        cv::findContours(binaryMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
                        
                        bool hasDefect = false;
                        std::vector<std::vector<cv::Point>> defectContours;
                        QRectF adjustedRectF = result.adjustedRects[pattern.id];
                        int adjustedX = static_cast<int>(adjustedRectF.x());
                        int adjustedY = static_cast<int>(adjustedRectF.y());
                        
                        for (const auto& contour : contours) {
                            int blobSize = static_cast<int>(cv::contourArea(contour));
                            cv::Rect bbox = cv::boundingRect(contour);
                            
                            bool sizeCheck = (blobSize >= pattern.anomalyMinBlobSize);
                            bool widthCheck = (bbox.width >= pattern.anomalyMinDefectWidth);
                            bool heightCheck = (bbox.height >= pattern.anomalyMinDefectHeight);
                            
                            if (sizeCheck || (widthCheck && heightCheck)) {
                                hasDefect = true;
                                std::vector<cv::Point> absoluteContour;
                                for (const auto& pt : contour) {
                                    absoluteContour.push_back(cv::Point(pt.x + adjustedX, pt.y + adjustedY));
                                }
                                defectContours.push_back(absoluteContour);
                            }
                        }
                        
                        result.insScores[pattern.id] = static_cast<double>(roiAnomalyScore);
                        result.insResults[pattern.id] = !hasDefect;
                        result.insMethodTypes[pattern.id] = InspectionMethod::A_PD;
                        result.anomalyDefectContours[pattern.id] = defectContours;
                        result.anomalyRawMap[pattern.id] = anomalyMap.clone();
                        
                        // 히트맵 생성
                        cv::Mat normalized;
                        anomalyMap.convertTo(normalized, CV_8U, 255.0 / 100.0);
                        cv::Mat colorHeatmap;
                        cv::applyColorMap(normalized, colorHeatmap, cv::COLORMAP_JET);
                        result.anomalyHeatmap[pattern.id] = colorHeatmap.clone();
                        result.anomalyHeatmapRect[pattern.id] = pattern.rect;
                        apdPatternCount++;
                        
                        result.isPassed = result.isPassed && !hasDefect;
                        
                        QString insResultText = !hasDefect ? "PASS" : "NG";
                        QString resultColor = !hasDefect ? "<font color='#00FF00'>" : "<font color='#FF0000'>";
                        int defectCount = defectContours.size();
                        
                        if (defectCount > 0)
                        {
                            int maxW = 0, maxH = 0;
                            for (const auto& contour : defectContours)
                            {
                                cv::Rect bbox = cv::boundingRect(contour);
                                if (bbox.width > maxW) maxW = bbox.width;
                                if (bbox.height > maxH) maxH = bbox.height;
                            }
                            logDebug(QString("  └─ <font color='#8BCB8B'>%1(A-PD)</font>: W:%2 H:%3 Detects:%4 (score=%5, thr=%6) [%7ms]")
                                .arg(pattern.name).arg(maxW).arg(maxH).arg(defectCount)
                                .arg(roiAnomalyScore, 0, 'f', 2).arg(pattern.passThreshold, 0, 'f', 2).arg(avgPatternTime));
                        }
                        else
                        {
                            logDebug(QString("  └─ <font color='#8BCB8B'>%1(A-PD)</font>: %2%3</font> (score=%4, thr=%5) [%6ms]")
                                .arg(pattern.name).arg(resultColor).arg(insResultText)
                                .arg(roiAnomalyScore, 0, 'f', 2).arg(pattern.passThreshold, 0, 'f', 2).arg(avgPatternTime));
                        }
                    }
                }
            }
        }
#endif
        
        // Anomaly 전체 처리 완료 요약
        if (totalAnomalyCount > 0) {
            auto anomalyBatchEnd = std::chrono::high_resolution_clock::now();
            
            long long apcDuration = 0;
            long long apdDuration = 0;
            if (apcPatternCount > 0) {
                apcDuration = std::chrono::duration_cast<std::chrono::milliseconds>(apdBatchStart - apcBatchStart).count();
            }
            if (apdPatternCount > 0) {
                apdDuration = std::chrono::duration_cast<std::chrono::milliseconds>(anomalyBatchEnd - apdBatchStart).count();
            }
        }
        
        // ===== 일반 INS 패턴 처리 (A-PC, A-PD 제외) =====
        for (const PatternInfo &pattern : insPatterns)
        {
            // A-PC와 A-PD는 이미 배치로 처리됨
            if (pattern.inspectionMethod == InspectionMethod::A_PC ||
                pattern.inspectionMethod == InspectionMethod::A_PD)
            {
                continue;
            }
            
            // 마스크 필터가 활성화되어 있으면 검사 PASS 처리
            bool hasMaskFilter = false;
            for (const FilterInfo &filter : pattern.filters)
            {
                if (filter.enabled && filter.type == FILTER_MASK)
                {
                    hasMaskFilter = true;
                    break;
                }
            }
            if (hasMaskFilter)
            {
                logDebug(QString("INS 패턴 '%1': 마스크 필터 활성화 → 검사 PASS 처리").arg(pattern.name));
                result.insResults[pattern.id] = true;
                result.insScores[pattern.id] = 1.0;
                result.adjustedRects[pattern.id] = pattern.rect;
                result.insMethodTypes[pattern.id] = InspectionMethod::DIFF;
                continue;
            }

            // 기본 검사 영역은 패턴의 원본 영역
            QRect originalRect = QRect(
                static_cast<int>(pattern.rect.x()),
                static_cast<int>(pattern.rect.y()),
                static_cast<int>(pattern.rect.width()),
                static_cast<int>(pattern.rect.height()));
            QRect adjustedRect = originalRect;

            // ===== INS 패턴 매칭 (Fine Alignment) =====
            // 부모 FID와 무관하게 독립적으로 동작
            if (pattern.patternMatchEnabled && !pattern.matchTemplate.isNull())
            {
                // 검색 범위: 부모 ROI 전체 영역 (현재 STRIP/CRIMP 모드에 맞는 ROI)
                cv::Rect searchROI;
                
                // 부모 ROI 찾기
                QUuid parentRoiId;
                for (const PatternInfo& p : patterns) {
                    if (p.type == PatternType::ROI) {
                        parentRoiId = p.id;
                        searchROI = cv::Rect(
                            static_cast<int>(p.rect.x()),
                            static_cast<int>(p.rect.y()),
                            static_cast<int>(p.rect.width()),
                            static_cast<int>(p.rect.height())
                        );
                        break;
                    }
                }
                
                // ROI를 못 찾으면 전체 이미지 사용
                if (searchROI.area() == 0) {
                    searchROI = cv::Rect(0, 0, image.cols, image.rows);
                }

                // matchTemplate을 cv::Mat으로 변환
                cv::Mat templateMat;
                cv::Mat maskMat;  // 마스크 추가
                
                if (pattern.matchTemplate.format() == QImage::Format_RGB888)
                {
                    templateMat = cv::Mat(
                        pattern.matchTemplate.height(),
                        pattern.matchTemplate.width(),
                        CV_8UC3,
                        const_cast<uchar*>(pattern.matchTemplate.bits()),
                        pattern.matchTemplate.bytesPerLine()
                    ).clone();
                    cv::cvtColor(templateMat, templateMat, cv::COLOR_RGB2BGR);
                }
                else if (pattern.matchTemplate.format() == QImage::Format_RGB32 ||
                         pattern.matchTemplate.format() == QImage::Format_ARGB32)
                {
                    templateMat = cv::Mat(
                        pattern.matchTemplate.height(),
                        pattern.matchTemplate.width(),
                        CV_8UC4,
                        const_cast<uchar*>(pattern.matchTemplate.bits()),
                        pattern.matchTemplate.bytesPerLine()
                    ).clone();
                    cv::cvtColor(templateMat, templateMat, cv::COLOR_RGBA2BGR);
                }
                else if (pattern.matchTemplate.format() == QImage::Format_Grayscale8)
                {
                    templateMat = cv::Mat(
                        pattern.matchTemplate.height(),
                        pattern.matchTemplate.width(),
                        CV_8UC1,
                        const_cast<uchar*>(pattern.matchTemplate.bits()),
                        pattern.matchTemplate.bytesPerLine()
                    ).clone();
                }
                
                // 마스크 변환 (있는 경우)
                if (!pattern.matchTemplateMask.isNull())
                {
                    maskMat = cv::Mat(
                        pattern.matchTemplateMask.height(),
                        pattern.matchTemplateMask.width(),
                        CV_8UC1,
                        const_cast<uchar*>(pattern.matchTemplateMask.bits()),
                        pattern.matchTemplateMask.bytesPerLine()
                    ).clone();
                }

                if (!templateMat.empty() && searchROI.width > 0 && searchROI.height > 0)
                {
                    // 검색 영역 추출
                    cv::Mat searchRegion = image(searchROI).clone();

                    // 패턴 매칭 수행
                    cv::Point matchLoc;
                    double matchScore = 0.0;
                    double matchAngle = pattern.angle;

                    auto insMatchStart = std::chrono::high_resolution_clock::now();
                    bool matched = performTemplateMatching(
                        searchRegion,
                        templateMat,
                        matchLoc,
                        matchScore,
                        matchAngle,
                        pattern,
                        pattern.patternMatchUseRotation ? pattern.patternMatchMinAngle : 0.0,
                        pattern.patternMatchUseRotation ? pattern.patternMatchMaxAngle : 0.0,
                        pattern.patternMatchUseRotation ? pattern.patternMatchAngleStep : 1.0,
                        maskMat  // 마스크 전달
                    );
                    // 패턴 매칭 완료

                    if (matched && (matchScore * 100.0) >= pattern.patternMatchThreshold)
                    {
                        // 패턴 매칭 성공 - 찾은 위치로 검사 영역 업데이트
                        int matchedCenterX = searchROI.x + matchLoc.x;
                        int matchedCenterY = searchROI.y + matchLoc.y;

                        // 패턴 매칭 성공

                        // 검사 영역을 매칭된 위치로 업데이트
                        adjustedRect = QRect(
                            matchedCenterX - pattern.rect.width() / 2,
                            matchedCenterY - pattern.rect.height() / 2,
                            pattern.rect.width(),
                            pattern.rect.height()
                        );

                        // 각도도 업데이트 (검사 시 사용)
                        const_cast<PatternInfo&>(pattern).angle = matchAngle;
                    }
                    else
                    {
                        logDebug(QString("INS 패턴 '%1': 패턴 매칭 실패 (Score=%2% < Threshold=%3%), 원본 위치 사용")
                                     .arg(pattern.name)
                                     .arg(matchScore * 100.0, 0, 'f', 1)
                                     .arg(pattern.patternMatchThreshold, 0, 'f', 1));
                    }
                }
                else
                {
                    logDebug(QString("INS 패턴 '%1': 패턴 매칭 실패 - templateMat.empty()=%2, searchROI 유효=%3")
                                 .arg(pattern.name)
                                 .arg(templateMat.empty())
                                 .arg(searchROI.width > 0 && searchROI.height > 0));
                }
            }

            // 부모 FID 정보가 있는 경우 처리 (패턴 매칭 후에 추가 조정)
            cv::Point parentOffset(0, 0);
            double parentAngle = 0.0;
            double parentFidTeachingAngle = 0.0;
            bool hasParentInfo = false;

            // 부모 FID가 있는지 확인
            if (!pattern.parentId.isNull())
            {
                // 부모 FID의 매칭 결과가 있는지 확인
                if (result.fidResults.contains(pattern.parentId))
                {
                    // 부모 FID가 매칭에 실패했을 경우 이 INS 패턴은 FAIL (검사 불가)
                    if (!result.fidResults[pattern.parentId])
                    {
                        logDebug(QString("INS pattern '%1': FAIL - Cannot inspect (parent FID match failed)")
                                     .arg(pattern.name));
                        result.insResults[pattern.id] = false;
                        result.insScores[pattern.id] = 0.0;
                        result.isPassed = false;
                        continue;
                    }

                    // FID 점수 확인 - 1.0이면 위치 조정 생략
                    double fidScore = result.matchScores.value(pattern.parentId, 0.0);
                    if (fidScore >= 0.999)
                    {

                        // 위치 조정하지 않고 원래 패턴 위치 그대로 사용
                        adjustedRect = originalRect;
                    }
                    else
                    {

                        // 부모 FID의 위치 정보가 있는 경우 조정
                        if (result.locations.contains(pattern.parentId))
                        {
                            cv::Point fidLoc = result.locations[pattern.parentId];
                            double fidAngle = result.angles[pattern.parentId];

                            // FID의 원래 정보 찾기
                            QPoint originalFidCenter;
                            // double parentFidTeachingAngle = 0.0; // 제거: 위에서 이미 선언됨
                            PatternInfo parentFidInfo;
                            bool foundFid = false;

                            for (const PatternInfo &fid : fidPatterns)
                            {
                                if (fid.id == pattern.parentId)
                                {
                                    originalFidCenter = QPoint(
                                        static_cast<int>(fid.rect.center().x()),
                                        static_cast<int>(fid.rect.center().y()));
                                    // 백업된 패턴에서 원본 티칭 각도 가져오기 (검출 각도가 아닌 원본 각도)
                                    auto backupIt = std::find_if(patterns.begin(), patterns.end(),
                                                                 [&](const PatternInfo &p)
                                                                 { return p.id == pattern.parentId; });
                                    if (backupIt != patterns.end())
                                    {
                                        parentFidTeachingAngle = backupIt->angle; // 원본 티칭 각도 사용
                                    }
                                    else
                                    {
                                        parentFidTeachingAngle = 0.0; // 백업이 없으면 0도로 가정
                                    }
                                    parentFidInfo = fid;
                                    foundFid = true;
                                    break;
                                }
                            }

                            if (!foundFid)
                            {
                                logDebug(QString("INS 패턴 '%1': 부모 FID 정보를 찾을 수 없음")
                                             .arg(pattern.name));
                                continue;
                            }

                            // 부모 FID의 위치와 각도 정보를 저장
                            parentOffset = cv::Point(
                                fidLoc.x - originalFidCenter.x(),
                                fidLoc.y - originalFidCenter.y());

                            // **중요**: INS 패턴의 회전 각도는 티칭 각도 + FID 회전 차이
                            // FID 티칭 각도와 검출 각도의 차이를 INS 패턴에 적용
                            double fidAngleDiff = fidAngle - parentFidTeachingAngle;
                            parentAngle = pattern.angle + fidAngleDiff;
                            hasParentInfo = true;

                            // ===== 패턴 매칭 (Fine Alignment) 디버그 =====
                            // ===== 패턴 매칭 (Fine Alignment) 수행 =====
                            if (pattern.patternMatchEnabled && !pattern.matchTemplate.isNull())
                            {
                                // 1. Coarse Alignment: FID 기반 대략 위치
                                cv::Point coarseCenter(fidLoc.x, fidLoc.y);
                                double coarseAngle = fidAngle;

                                // 2. Fine Alignment: 부모 ROI 전체 영역에서 패턴 매칭
                                cv::Rect searchROI;
                                
                                // 부모 ROI 찾기
                                for (const PatternInfo& p : patterns) {
                                    if (p.type == PatternType::ROI) {
                                        searchROI = cv::Rect(
                                            static_cast<int>(p.rect.x()),
                                            static_cast<int>(p.rect.y()),
                                            static_cast<int>(p.rect.width()),
                                            static_cast<int>(p.rect.height())
                                        );
                                        break;
                                    }
                                }
                                
                                // ROI를 못 찾으면 전체 이미지 사용
                                if (searchROI.area() == 0) {
                                    searchROI = cv::Rect(0, 0, image.cols, image.rows);
                                }

                                // matchTemplate을 cv::Mat으로 변환
                                cv::Mat templateMat;
                                if (pattern.matchTemplate.format() == QImage::Format_RGB888)
                                {
                                    templateMat = cv::Mat(
                                        pattern.matchTemplate.height(),
                                        pattern.matchTemplate.width(),
                                        CV_8UC3,
                                        const_cast<uchar*>(pattern.matchTemplate.bits()),
                                        pattern.matchTemplate.bytesPerLine()
                                    ).clone();
                                    cv::cvtColor(templateMat, templateMat, cv::COLOR_RGB2BGR);
                                }
                                else if (pattern.matchTemplate.format() == QImage::Format_RGB32 ||
                                         pattern.matchTemplate.format() == QImage::Format_ARGB32)
                                {
                                    templateMat = cv::Mat(
                                        pattern.matchTemplate.height(),
                                        pattern.matchTemplate.width(),
                                        CV_8UC4,
                                        const_cast<uchar*>(pattern.matchTemplate.bits()),
                                        pattern.matchTemplate.bytesPerLine()
                                    ).clone();
                                    cv::cvtColor(templateMat, templateMat, cv::COLOR_RGBA2BGR);
                                }
                                else if (pattern.matchTemplate.format() == QImage::Format_Grayscale8)
                                {
                                    templateMat = cv::Mat(
                                        pattern.matchTemplate.height(),
                                        pattern.matchTemplate.width(),
                                        CV_8UC1,
                                        const_cast<uchar*>(pattern.matchTemplate.bits()),
                                        pattern.matchTemplate.bytesPerLine()
                                    ).clone();
                                }

                                if (templateMat.empty())
                                {
                                    // 변환 실패
                                }
                                else
                                {
                                    // ★ 마스크 변환 (FID 기반도 마스크 지원)
                                    cv::Mat maskMat;
                                    if (!pattern.matchTemplateMask.isNull())
                                    {
                                        maskMat = cv::Mat(
                                            pattern.matchTemplateMask.height(),
                                            pattern.matchTemplateMask.width(),
                                            CV_8UC1,
                                            const_cast<uchar*>(pattern.matchTemplateMask.bits()),
                                            pattern.matchTemplateMask.bytesPerLine()
                                        ).clone();
                                    }
                                    
                                    // 검색 영역 추출
                                    cv::Mat searchRegion = image(searchROI).clone();

                                    // 패턴 매칭 수행 (FID와 동일한 함수 사용)
                                    cv::Point matchLoc;
                                    double matchScore = 0.0;
                                    double matchAngle = coarseAngle;

                                    bool matched = performTemplateMatching(
                                        searchRegion,
                                        templateMat,
                                        matchLoc,
                                        matchScore,
                                        matchAngle,
                                        pattern,
                                        pattern.patternMatchUseRotation ? pattern.patternMatchMinAngle : 0.0,
                                        pattern.patternMatchUseRotation ? pattern.patternMatchMaxAngle : 0.0,
                                        pattern.patternMatchUseRotation ? pattern.patternMatchAngleStep : 1.0,
                                        maskMat  // ★ 마스크 전달
                                    );

                                    if (matched && (matchScore * 100.0) >= pattern.patternMatchThreshold)
                                    {
                                        // 패턴 매칭 성공 - Fine 위치/각도로 업데이트
                                        int fineX = searchROI.x + matchLoc.x;
                                        int fineY = searchROI.y + matchLoc.y;
                                        
                                        fidLoc.x = fineX;
                                        fidLoc.y = fineY;
                                        parentAngle = matchAngle;

                                        // 오프셋 재계산
                                        parentOffset = cv::Point(
                                            fidLoc.x - originalFidCenter.x(),
                                            fidLoc.y - originalFidCenter.y()
                                        );
                                    }
                                    else
                                    {
                                        // 매칭 실패 - Coarse 위치 사용
                                    }
                                }
                            }

                            // 부모 FID 위치에 따른 검사 영역 조정 구현
                            // 1. 티칭 시점의 FID/INS 중심을 부동소수점으로 사용하여 상대 벡터 계산
                            QPointF fidTeachingCenterF = parentFidInfo.rect.center();
                            QPointF insTeachingCenterF = pattern.rect.center();
                            double relX = insTeachingCenterF.x() - fidTeachingCenterF.x();
                            double relY = insTeachingCenterF.y() - fidTeachingCenterF.y();

                            // 2. 각도 차이 계산 (FID 검출 각도 - FID 티칭 각도)
                            double angleDiff = fidAngle - parentFidTeachingAngle;
                            double radians = angleDiff * M_PI / 180.0;

                            // 3. 상대 벡터를 각도만큼 회전 (부동소수점 정밀도 유지)
                            // 이미지 좌표계(Y 증가 방향이 아래)를 고려해 회전 방향을 반전
                            double cosA = std::cos(radians);
                            double sinA = std::sin(radians);
                            // 회전 부호를 반대로 하여 화면에서 기대하는 회전 방향으로 맞춤
                            double rotatedX = relX * cosA + relY * sinA; // using -radians
                            double rotatedY = -relX * sinA + relY * cosA;

                            // 4. FID 검출 위치(정확한 픽셀 좌표) + 회전된 상대 벡터 = INS 새 중심 (부동소수점)
                            double newCenterX_d = static_cast<double>(fidLoc.x) + rotatedX;
                            double newCenterY_d = static_cast<double>(fidLoc.y) + rotatedY;

                            // 정수 좌표로 반올림하여 QRect 계산
                            int newCenterX = static_cast<int>(std::lround(newCenterX_d));
                            int newCenterY = static_cast<int>(std::lround(newCenterY_d));

                            int width = pattern.rect.width();
                            int height = pattern.rect.height();

                            // 중심점 기준으로 새로운 조정된 검사 영역 계산
                            adjustedRect = QRect(
                                newCenterX - width / 2,  // 회전된 새 x 좌표
                                newCenterY - height / 2, // 회전된 새 y 좌표
                                width,                   // 너비 동일
                                height                   // 높이 동일
                            );
                        }
                    }
                }
            }

            // INS 패턴은 ROI 체크를 하지 않음 (전체 이미지에서 검사)
            QPoint adjustedCenter = adjustedRect.center();

            // 조정된 영역이 이미지 경계를 벗어나는지 확인
            if (adjustedRect.x() < 0 || adjustedRect.y() < 0 ||
                adjustedRect.x() + adjustedRect.width() > image.cols ||
                adjustedRect.y() + adjustedRect.height() > image.rows)
            {

                logDebug(QString("INS 패턴 '%1': 조정된 영역이 이미지 경계를 벗어남, 영역 조정").arg(pattern.name));

                // 이미지 경계 내로 영역 조정
                int x = std::max(0, adjustedRect.x());
                int y = std::max(0, adjustedRect.y());
                int width = std::min(image.cols - x, adjustedRect.width());
                int height = std::min(image.rows - y, adjustedRect.height());

                // 조정된 영역이 너무 작으면 검사 실패
                if (width < 10 || height < 10)
                {
                    logDebug(QString("INS 패턴 '%1': 조정된 영역이 너무 작음, 검사 실패").arg(pattern.name));
                    result.insResults[pattern.id] = false;
                    result.insScores[pattern.id] = 0.0;
                    if (hasParentInfo)
                    {
                        result.parentOffsets[pattern.id] = parentOffset;
                        result.parentAngles[pattern.id] = parentAngle;
                    }
                    result.isPassed = false;
                    continue;
                }

                // 유효한 영역으로 조정
                adjustedRect = QRect(x, y, width, height);
                logDebug(QString("INS 패턴 '%1': 영역 조정됨 - (%2,%3,%4,%5)")
                             .arg(pattern.name)
                             .arg(adjustedRect.x())
                             .arg(adjustedRect.y())
                             .arg(adjustedRect.width())
                             .arg(adjustedRect.height()));
            }

            double inspScore = 0.0;
            bool inspPassed = false;

            // 검사 방법에 따른 분기 - 패턴 복제하여 조정된 영역으로 설정
            PatternInfo adjustedPattern = pattern;
            adjustedPattern.rect = adjustedRect;

            // 최종 계산된 각도 설정 (INS 원본 각도 + FID 회전 차이)

            if (hasParentInfo)
            {
                // FID 회전 차이만큼 INS 원본 각도에 추가
                double fidAngleDiff = parentAngle - parentFidTeachingAngle;
                adjustedPattern.angle = pattern.angle + fidAngleDiff; // INS 원본 각도 + FID 회전 차이
            }
            else
            {
                adjustedPattern.angle = pattern.angle; // 패턴 각도만
            }

            // 검사 시간 측정
            auto insStart = std::chrono::high_resolution_clock::now();
            
            switch (pattern.inspectionMethod)
            {
            case InspectionMethod::DIFF:
                inspPassed = checkDiff(image, adjustedPattern, inspScore, result);
                logDebug(QString("DIFF 검사 수행: %1 (method=%2)").arg(pattern.name).arg(pattern.inspectionMethod));
                break;

            case InspectionMethod::STRIP:
            {
                // 디버그: STRIP 검사 직전 각도 확인

                inspPassed = checkStrip(image, adjustedPattern, inspScore, result, patterns);

                break;
            }

            case InspectionMethod::CRIMP:
            {
                inspPassed = checkCrimp(image, adjustedPattern, inspScore, result, patterns);
                break;
            }

            case InspectionMethod::SSIM:
            {
                inspPassed = checkSSIM(image, adjustedPattern, inspScore, result);
                break;
            }

            case InspectionMethod::A_PC:
            case InspectionMethod::A_PD:
            {
                // A-PC와 A-PD는 배치로 이미 처리됨 - 로그만 출력하고 continue
                // 결과는 배치 처리 단계에서 이미 result에 저장됨
                inspPassed = result.insResults.value(pattern.id, false);
                inspScore = result.insScores.value(pattern.id, 0.0);
                
                // 검사 시간은 0으로 설정 (배치 처리에서 전체 시간 측정됨)
                auto insEnd = std::chrono::high_resolution_clock::now();
                auto insDuration = 0; // 배치 처리에서 이미 측정
                
                // 결과 로그 출력
                QString insResultText = inspPassed ? "PASS" : "NG";
                QString resultColor = inspPassed ? "<font color='#00FF00'>" : "<font color='#FF0000'>";
                QString colorGreen = "<font color='#8BCB8B'>";
                QString colorEnd = "</font>";
                
                // 이미 배치 처리에서 로그 출력했으므로 여기서는 건너뜀
                // (중복 로그 방지)
                
                continue; // 다음 패턴으로
            }

            default:
                // 이전 PATTERN 타입은 DIFF로 처리
                inspPassed = checkDiff(image, adjustedPattern, inspScore, result);
                logDebug(QString("알 수 없는 검사 방법 %1, DIFF 검사로 수행: %2")
                             .arg(pattern.inspectionMethod)
                             .arg(pattern.name));
                break;
            }
            
            // 검사 시간 종료
            auto insEnd = std::chrono::high_resolution_clock::now();
            auto insDuration = std::chrono::duration_cast<std::chrono::milliseconds>(insEnd - insStart).count();

            // 결과 기록
            result.insResults[pattern.id] = inspPassed;
            result.insScores[pattern.id] = inspScore;
            result.adjustedRects[pattern.id] = adjustedRect;

            // 부모 FID 위치 정보가 있으면 저장
            if (hasParentInfo)
            {
                result.parentOffsets[pattern.id] = parentOffset;
                result.parentAngles[pattern.id] = parentAngle;
            }
            else
            {
                // 그룹화되지 않은 INS 패턴은 티칭한 각도 그대로 사용
                result.parentAngles[pattern.id] = pattern.angle;
            }

            // INS 검사 결과 로그 (개별 출력)
            QString insResultText;
            if (!result.fidResults.value(pattern.parentId, true) && !pattern.parentId.isNull())
            {
                // 부모 FID 매칭 실패로 검사 불가
                insResultText = "FAIL";
            }
            else if (inspPassed)
            {
                insResultText = "PASS";
            }
            else
            {
                insResultText = "NG";
            }

            // HTML 색상 태그 (Qt 로그용)
            QString colorGreen = "<font color='#8BCB8B'>";    // 연한 초록색 (INS 패턴)
            QString colorPass = "<font color='#00FF00'>";     // 초록색 (PASS)
            QString colorNG = "<font color='#FF0000'>";       // 빨간색 (NG, FAIL)
            QString colorEnd = "</font>";                      // 색상 종료
            
            // 결과 색상 선택
            QString resultColor = inspPassed ? colorPass : colorNG;

            // 검사 방법별 결과 포맷팅
            QString resultDetail;
            if (pattern.inspectionMethod == InspectionMethod::A_PC ||
                pattern.inspectionMethod == InspectionMethod::A_PD)
            {
                // A-PC/A-PD: 불량 개수 및 최대 W/H 표시
                QString methodName = InspectionMethod::getName(pattern.inspectionMethod);
                int defectCount = result.anomalyDefectContours.value(pattern.id).size();
                if (defectCount > 0) {
                    // 최대 컨투어 W/H 찾기
                    int maxW = 0, maxH = 0;
                    for (const auto& contour : result.anomalyDefectContours.value(pattern.id)) {
                        cv::Rect bbox = cv::boundingRect(contour);
                        if (bbox.width > maxW) maxW = bbox.width;
                        if (bbox.height > maxH) maxH = bbox.height;
                    }
                    resultDetail = QString("  └─ %1%2(%3)%4: W:%5 H:%6 Detects:%7 (%8ms)")
                                       .arg(colorGreen).arg(pattern.name)
                                       .arg(methodName).arg(colorEnd)
                                       .arg(maxW)
                                       .arg(maxH)
                                       .arg(defectCount)
                                       .arg(insDuration);
                } else {
                    resultDetail = QString("  └─ %1%2(%3)%4: %5%6%7 (%8ms)")
                                       .arg(colorGreen).arg(pattern.name)
                                       .arg(methodName).arg(colorEnd)
                                       .arg(resultColor).arg(insResultText).arg(colorEnd)
                                       .arg(insDuration);
                }
            }
            else if (pattern.inspectionMethod == InspectionMethod::STRIP)
            {
                // STRIP: 세부 결과 한 줄로
                QStringList stripDetails;
                
                // gradient points 부족으로 검사 실패한 경우 (score = 0.0)
                if (inspScore == 0.0 && !result.stripPointsValid.value(pattern.id, false)) {
                    resultDetail = QString("  └─ %1%2(%3)%4: %5%6%7 (insufficient gradient points) (%8ms)")
                                       .arg(colorGreen).arg(pattern.name)
                                       .arg(InspectionMethod::getName(pattern.inspectionMethod)).arg(colorEnd)
                                       .arg(resultColor).arg(insResultText).arg(colorEnd)
                                       .arg(insDuration);
                } else {
                    if (result.frontResult != "PASS") stripDetails << QString("FRONT:%1").arg(result.frontDetail);
                    if (result.rearResult != "PASS") stripDetails << QString("REAR:%1").arg(result.rearDetail);
                    if (result.edgeResult != "PASS") stripDetails << QString("EDGE:%1").arg(result.edgeDetail);
                    
                    if (stripDetails.isEmpty()) {
                        resultDetail = QString("  └─ %1%2(%3)%4: %5%6%7 (%8ms)")
                                           .arg(colorGreen).arg(pattern.name)
                                           .arg(InspectionMethod::getName(pattern.inspectionMethod)).arg(colorEnd)
                                           .arg(resultColor).arg(insResultText).arg(colorEnd)
                                           .arg(insDuration);
                    } else {
                        resultDetail = QString("  └─ %1%2(%3)%4: %5%6%7 (%8) (%9ms)")
                                           .arg(colorGreen).arg(pattern.name)
                                           .arg(InspectionMethod::getName(pattern.inspectionMethod)).arg(colorEnd)
                                           .arg(resultColor).arg(insResultText).arg(colorEnd)
                                           .arg(stripDetails.join(", "))
                                           .arg(insDuration);
                    }
                }
            }
            else if (pattern.inspectionMethod == InspectionMethod::CRIMP)
            {
                // CRIMP: YOLO 검출 정보
                bool crimpLeft = result.barrelLeftResults.value(pattern.id, false);
                bool crimpRight = result.barrelRightResults.value(pattern.id, false);
                
                QStringList crimpDetails;
                if (!crimpLeft) crimpDetails << "L:FAIL";
                if (!crimpRight) crimpDetails << "R:FAIL";
                
                if (crimpDetails.isEmpty()) {
                    resultDetail = QString("  └─ %1%2(%3)%4: %5%6%7 (%8ms)")
                                       .arg(colorGreen).arg(pattern.name)
                                       .arg(InspectionMethod::getName(pattern.inspectionMethod)).arg(colorEnd)
                                       .arg(resultColor).arg(insResultText).arg(colorEnd)
                                       .arg(insDuration);
                } else {
                    resultDetail = QString("  └─ %1%2(%3)%4: %5%6%7 (%8) (%9ms)")
                                       .arg(colorGreen).arg(pattern.name)
                                       .arg(InspectionMethod::getName(pattern.inspectionMethod)).arg(colorEnd)
                                       .arg(resultColor).arg(insResultText).arg(colorEnd)
                                       .arg(crimpDetails.join(", "))
                                       .arg(insDuration);
                }
            }
            else
            {
                // 기본 형식 (DIFF, SSIM 등)
                resultDetail = QString("  └─ %1%2(%3)%4: %5%6%7 (%8ms)")
                                   .arg(colorGreen).arg(pattern.name)
                                   .arg(InspectionMethod::getName(pattern.inspectionMethod)).arg(colorEnd)
                                   .arg(resultColor).arg(insResultText).arg(colorEnd)
                                   .arg(insDuration);
            }
            
            logDebug(resultDetail);

            // 전체 결과 갱신
            result.isPassed = result.isPassed && inspPassed;
            // qDebug() << "[INS 검사]" << pattern.name << "결과:" << inspPassed << "→ 전체 result.isPassed =" << result.isPassed;
        }
    }

    // 전체 검사 결과 로그
    // FID 매칭 실패가 있는지 확인
    bool hasFidFailure = false;
    for (auto it = result.fidResults.begin(); it != result.fidResults.end(); ++it) {
        if (!it.value()) {
            hasFidFailure = true;
            break;
        }
    }
    
    // 결과 메시지 결정
    QString resultText;
    if (hasFidFailure) {
        resultText = "FAIL";  // FID 매칭 실패 = 검사 실패
    } else {
        resultText = result.isPassed ? "PASS" : "NG";  // 불량 검출 = NG
    }
    
    // 검사 종료 시간 측정 및 소요 시간 계산
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    // 검사 결과에 시간 저장
    result.inspectionTimeMs = duration.count();
    
    // 검사 종료 로그 (옵션 2 형식) - PASS/FAIL 색상 적용
    QString coloredResult = result.isPassed 
        ? QString("<font color='#4CAF50'>%1</font>").arg(resultText)
        : QString("<font color='#f44336'>%1</font>").arg(resultText);
    logDebug(QString("  └─ Result: %1 (%2ms)").arg(coloredResult).arg(duration.count()));

    // 이미지 저장 (NG/OK 상관없이 모두 저장)
    // frameIndex는 이미 위에서 선언됨 (line 49)
    if (!patterns.isEmpty())
    {
        frameIndex = patterns.first().frameIndex;
    }
    
    QString dataDir = QCoreApplication::applicationDirPath() + "/data";
    QString dateFolder = QDateTime::currentDateTime().toString("yyyyMMdd");
    QString frameFolder = QString::number(frameIndex);
    QString savePath = dataDir + "/" + dateFolder + "/" + frameFolder;
    
    // 디렉토리 생성 (날짜/프레임인덱스)
    QDir dir;
    if (!dir.exists(savePath))
    {
        dir.mkpath(savePath);
    }
    
    // 파일명: 타임스탬프.jpg (결과 텍스트 제거)
    QString timestamp = QDateTime::currentDateTime().toString("HHmmss_zzz");
    QString fileName = QString("%1.jpg").arg(timestamp);
    QString filePath = savePath + "/" + fileName;
    
    // 이미지 저장
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 95};
    cv::imwrite(filePath.toStdString(), image, params);

    return result;
}

bool InsProcessor::matchFiducial(const cv::Mat &image, const PatternInfo &pattern,
                                 double &score, cv::Point &matchLoc, double &matchAngle, const QList<PatternInfo> &allPatterns)
{

    // 초기 점수 설정
    score = 0.0;
    matchAngle = 0.0;

    // 입력 이미지 및 템플릿 유효성 검사
    if (image.empty())
    {
        logDebug("FID 매칭 실패: 입력 이미지가 비어있음");
        return false;
    }

    if (pattern.templateImage.isNull())
    {
        logDebug(QString("❌ FID 패턴 '%1': 템플릿 이미지가 없음 (NULL)").arg(pattern.name));
        return false;
    }

    try
    {
        // ★ matchTemplate 사용 (RGB32 포맷 - INS와 동일)
        QImage qimg = pattern.matchTemplate.isNull() ? pattern.templateImage : pattern.matchTemplate;
        cv::Mat templateMat;

        // QImage를 cv::Mat로 정확하게 변환 (RGB32 포맷 처리)
        if (qimg.format() == QImage::Format_RGB32)
        {
            // RGB32 포맷인 경우 직접 변환 (4채널 -> 3채널)
            cv::Mat temp(qimg.height(), qimg.width(), CV_8UC4,
                        (void *)qimg.constBits(), qimg.bytesPerLine());
            cv::cvtColor(temp, templateMat, cv::COLOR_RGBA2BGR);
        }
        else if (qimg.format() == QImage::Format_RGB888)
        {
            // RGB888 포맷인 경우 직접 변환
            templateMat = cv::Mat(qimg.height(), qimg.width(), CV_8UC3,
                                  (void *)qimg.constBits(), qimg.bytesPerLine())
                              .clone();
            // RGB를 BGR로 변환 (OpenCV는 BGR 사용)
            cv::cvtColor(templateMat, templateMat, cv::COLOR_RGB2BGR);
        }
        else
        {
            // 다른 포맷인 경우 RGB888로 변환 후 처리
            qimg = qimg.convertToFormat(QImage::Format_RGB888);
            templateMat = cv::Mat(qimg.height(), qimg.width(), CV_8UC3,
                                  (void *)qimg.constBits(), qimg.bytesPerLine())
                              .clone();
            cv::cvtColor(templateMat, templateMat, cv::COLOR_RGB2BGR);
        }

        // 템플릿 이미지 유효성 확인
        if (templateMat.empty())
        {
            logDebug(QString("FID 패턴 '%1': 템플릿 이미지가 비어있음").arg(pattern.name));
            return false;
        }

        // 템플릿 크기가 너무 작은지 확인
        if (templateMat.rows < 10 || templateMat.cols < 10)
        {
            logDebug(QString("FID 패턴 '%1': 템플릿 크기가 너무 작음 (%2x%3)")
                         .arg(pattern.name)
                         .arg(templateMat.cols)
                         .arg(templateMat.rows));
            return false;
        }

        // 검색 영역(ROI) 결정
        cv::Rect searchRoi;
        bool roiDefined = false;
        QRectF roiRect;  // ROI 영역 저장

        // ★★★ 수정: ROI가 있으면 ROI 전체 영역을 검색, 없으면 패턴 주변만 검색 ★★★
        // 모든 ROI 패턴 검색
        for (const PatternInfo &roi : allPatterns)
        {
            // ROI 패턴인지 확인하고 활성화된 상태인지 확인
            if (roi.type == PatternType::ROI && roi.enabled)
            {
                // FID 패턴이 이 ROI 내부에 있는지 확인 (중심점 기준)
                QPoint fidCenter = QPoint(
                    static_cast<int>(pattern.rect.center().x()),
                    static_cast<int>(pattern.rect.center().y()));
                if (roi.rect.contains(fidCenter))
                {
                    roiDefined = true;
                    roiRect = roi.rect;  // ROI 영역 저장
                    break; // 첫 번째로 찾은 포함하는 ROI 사용
                }
            }
        }

        // ★★★ ROI가 있으면 ROI 전체 영역 사용, 없으면 패턴 주변만 사용 ★★★
        if (roiDefined)
        {
            // ROI 전체 영역을 검색 영역으로 사용
            searchRoi = cv::Rect(
                std::max(0, static_cast<int>(roiRect.x())),
                std::max(0, static_cast<int>(roiRect.y())),
                std::min(image.cols - std::max(0, static_cast<int>(roiRect.x())),
                         static_cast<int>(roiRect.width())),
                std::min(image.rows - std::max(0, static_cast<int>(roiRect.y())),
                         static_cast<int>(roiRect.height())));
        }
        else
        {
            // ROI 없음: 패턴 중심 기반으로 검색 영역 결정 (패턴 크기의 2배 정도 마진)
            int margin = std::max(static_cast<int>(pattern.rect.width()), static_cast<int>(pattern.rect.height()));
            searchRoi = cv::Rect(
                std::max(0, static_cast<int>(pattern.rect.x()) - margin),
                std::max(0, static_cast<int>(pattern.rect.y()) - margin),
                std::min(image.cols - std::max(0, static_cast<int>(pattern.rect.x()) - margin),
                         static_cast<int>(pattern.rect.width()) + 2 * margin),
                std::min(image.rows - std::max(0, static_cast<int>(pattern.rect.y()) - margin),
                         static_cast<int>(pattern.rect.height()) + 2 * margin));
        }

        // 검색 영역 유효성 확인 및 로그
        if (searchRoi.width <= 0 || searchRoi.height <= 0 ||
            searchRoi.x + searchRoi.width > image.cols ||
            searchRoi.y + searchRoi.height > image.rows)
        {
            logDebug(QString("FID 패턴 '%1': 유효하지 않은 검색 영역 (%2,%3,%4,%5)")
                         .arg(pattern.name)
                         .arg(searchRoi.x)
                         .arg(searchRoi.y)
                         .arg(searchRoi.width)
                         .arg(searchRoi.height));
            return false;
        }
        
        // (FID 검색 영역 로그는 제거 - 너무 상세함)

        // 템플릿이 검색 영역보다 큰지 확인
        if (templateMat.rows > searchRoi.height || templateMat.cols > searchRoi.width)
        {
            logDebug(QString("FID 패턴 '%1': 템플릿(%2x%3)이 검색 영역(%4x%5)보다 큼")
                         .arg(pattern.name)
                         .arg(templateMat.cols)
                         .arg(templateMat.rows)
                         .arg(searchRoi.width)
                         .arg(searchRoi.height));
            return false;
        }

        // 검색 영역 추출
        cv::Mat roi = image(searchRoi).clone();

        // **수정**: 템플릿은 티칭할 때 저장된 원본 그대로 사용 (검사 시 갱신하지 않음)
        cv::Mat processedTemplate = templateMat.clone();

        // FID는 마스크를 사용하지 않음 (속도 최적화)
        cv::Mat maskMat; // 빈 마스크

        // FID는 필터를 사용하지 않음 (원본 이미지로만 매칭)

        // 매칭 수행
        bool matched = false;
        cv::Point localMatchLoc; // ROI 내에서의 매칭 위치
        double tempAngle = 0.0;  // 임시 각도 변수 (switch 밖에서 선언)

        // 회전 매칭 파라미터 설정
        double tmplMinA = 0.0, tmplMaxA = 0.0, tmplStep = 1.0;
        if (pattern.useRotation)
        {
            tmplMinA = pattern.minAngle;
            tmplMaxA = pattern.maxAngle;
            tmplStep = pattern.angleStep;
        }

        // fidMatchMethod: 0=Coefficient (TM_CCOEFF_NORMED), 1=Correlation (TM_CCORR_NORMED)
        // FID는 마스크 없이 매칭 (속도 최적화)
        matched = performTemplateMatching(roi, processedTemplate, localMatchLoc, score, tempAngle,
                                          pattern, tmplMinA, tmplMaxA, tmplStep, maskMat);

        // 회전 매칭이 적용된 경우 탐지된 각도 사용
        if (pattern.useRotation && matched)
        {
            matchAngle = tempAngle;
            // logDebug(QString("FID 패턴 '%1': 회전 매칭 적용됨, tempAngle=%2°, matchAngle=%3°")
            //         .arg(pattern.name).arg(tempAngle).arg(matchAngle));
        }
        else if (matched)
        {
            matchAngle = pattern.angle; // 기본 각도 사용
        }

        // ROI 좌표를 원본 이미지 좌표로 변환
        if (matched)
        {
            // 수정: ROI 내에서의 상대 위치를 전체 이미지 좌표로 변환
            matchLoc.x = searchRoi.x + localMatchLoc.x;
            matchLoc.y = searchRoi.y + localMatchLoc.y;

            // 회전 매칭이 활성화된 경우 검출된 각도를 사용
            matchAngle = tempAngle;
        }

        // 매칭 임계값과 비교
        if (matched && (score * 100.0) >= pattern.matchThreshold)
        { // score(0-1)를 100% 단위로 변환하여 비교
            return true;
        }
        else
        {
            // 매칭은 실패했지만 검출된 각도는 적용 (matchAngle은 이미 설정됨)
            return false;
        }
    }
    catch (const cv::Exception &e)
    {
        logDebug(QString("FID 매칭 중 OpenCV 예외 발생: %1").arg(e.what()));
        return false;
    }
    catch (const std::exception &e)
    {
        logDebug(QString("FID 매칭 중 일반 예외 발생: %1").arg(e.what()));
        return false;
    }
    catch (...)
    {
        logDebug("FID 매칭 중 알 수 없는 예외 발생");
        return false;
    }
}

// 패턴 정보를 받는 템플릿 매칭 함수 (헤더에 선언된 버전)
bool InsProcessor::performTemplateMatching(const cv::Mat &image, const cv::Mat &templ,
                                           cv::Point &matchLoc, double &score, double &angle,
                                           const PatternInfo &pattern,
                                           double minAngle, double maxAngle, double angleStep,
                                           const cv::Mat &mask)
{

    if (image.empty() || templ.empty())
    {
        logDebug("템플릿 매칭 실패: 입력 이미지 또는 템플릿이 비어있음");
        score = 0.0;
        return false;
    }

    // 그레이스케일로 변환
    cv::Mat imageGray, templGray;
    if (image.channels() == 3)
        cv::cvtColor(image, imageGray, cv::COLOR_BGR2GRAY);
    else
        image.copyTo(imageGray);

    if (templ.channels() == 3)
        cv::cvtColor(templ, templGray, cv::COLOR_BGR2GRAY);
    else
        templ.copyTo(templGray);

    if (!pattern.useRotation)
    {
        // 회전 허용 안함: 원본 템플릿 그대로 매칭

        cv::Mat templateForMatching = templGray.clone();

        // 매칭 메트릭 선택: 0=Coefficient, 1=Correlation
        // FID: fidMatchMethod, INS: patternMatchMethod 사용 (통합)
        int methodValue = (pattern.type == PatternType::FID) ? pattern.fidMatchMethod : pattern.patternMatchMethod;
        int matchMethod = (methodValue == 0) ? cv::TM_CCOEFF_NORMED : cv::TM_CCORR_NORMED;

        cv::Mat result;
        
        // ★ 마스크가 있으면 마스크를 사용한 매칭
        if (!mask.empty() && mask.size() == templGray.size())
        {
            cv::matchTemplate(imageGray, templateForMatching, result, matchMethod, mask);
        }
        else
        {
            cv::matchTemplate(imageGray, templateForMatching, result, matchMethod);
        }

        double minVal, maxVal;
        cv::Point minLoc, maxLoc;
        cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);

        // 템플릿 매칭은 왼쪽 상단 좌표를 반환하므로, 중심점을 계산
        matchLoc.x = static_cast<int>(maxLoc.x + templateForMatching.cols / 2.0 + 0.5);
        matchLoc.y = static_cast<int>(maxLoc.y + templateForMatching.rows / 2.0 + 0.5);
        score = maxVal;
        angle = pattern.angle; // 패턴에 저장된 각도 그대로 반환

        return true;
    }

    // 회전 허용: 티칭 각도를 기준으로 +/- 범위에서 모든 각도 시도

    // 티칭 각도 기준으로 상대 범위 적용
    double adjustedMinAngle = pattern.angle + minAngle;
    double adjustedMaxAngle = pattern.angle + maxAngle;

    // 템플릿의 원본 크기 저장
    int originalWidth = templGray.cols;
    int originalHeight = templGray.rows;

    // 회전 시 잘림 방지를 위해 여백이 있는 더 큰 이미지 생성
    int diagonal = static_cast<int>(std::sqrt(originalWidth * originalWidth +
                                              originalHeight * originalHeight));
    int offsetX = (diagonal - originalWidth) / 2;
    int offsetY = (diagonal - originalHeight) / 2;

    cv::Mat paddedTempl = cv::Mat::zeros(diagonal, diagonal, templGray.type());
    cv::Rect roi(offsetX, offsetY, originalWidth, originalHeight);
    templGray.copyTo(paddedTempl(roi));
    
    // ★ 마스크도 동일하게 패딩 처리
    cv::Mat paddedMask;
    if (!mask.empty() && mask.size() == templGray.size())
    {
        paddedMask = cv::Mat::zeros(diagonal, diagonal, CV_8UC1);
        mask.copyTo(paddedMask(roi));
    }

    // 최적 매칭을 위한 변수들
    double bestScore = -1.0;
    double bestAngle = adjustedMinAngle;
    cv::Point bestLocation;
    cv::Mat bestTemplate; // 최고 점수를 얻은 템플릿 저장용
    cv::Mat bestMask;     // 최고 점수를 얻은 마스크 저장용

    // 각도 범위 내에서 최적 매칭 검색
    // 각도 리스트 생성: 2단계 적응적 검색 + 조기 종료
    std::vector<double> angleList;

    // 1. 티칭 각도를 첫 번째로 추가 (가장 우선)
    angleList.push_back(pattern.angle);

    // 2. 1단계: 큰 스텝(5도)으로 빠른 검색을 위한 각도 추가
    std::vector<double> coarseAngles;
    for (double currentAngle = adjustedMinAngle; currentAngle <= adjustedMaxAngle; currentAngle += 5.0)
    {
        if (std::abs(currentAngle - pattern.angle) >= 2.5)
        { // 티칭 각도와 충분히 떨어진 경우만
            coarseAngles.push_back(currentAngle);
        }
    }

    // === 1단계: 티칭 각도 + 5도 간격 빠른 검색 ===

    double bestCoarseScore = -1.0;
    double bestCoarseAngle = pattern.angle;
    cv::Point bestCoarseLocation;

    // 첫 번째로 티칭 각도 시도
    angleList.clear();
    angleList.push_back(pattern.angle);

    // 1단계 각도들 추가
    for (double angle : coarseAngles)
    {
        angleList.push_back(angle);
    }

    for (double currentAngle : angleList)
    {
        // 티칭 각도인지 확인
        bool isTeachingAngle = (std::abs(currentAngle - pattern.angle) < 0.01);

        // 템플릿 회전
        cv::Mat templateForMatching;
        cv::Mat maskForMatching;  // 회전된 마스크

        if (isTeachingAngle)
        {
            // 티칭 각도인 경우: 원본 템플릿 그대로 사용
            templateForMatching = templGray.clone();
            if (!mask.empty() && mask.size() == templGray.size())
            {
                maskForMatching = mask.clone();
            }
        }
        else
        {
            // 다른 각도인 경우: 패딩된 템플릿을 회전

            cv::Mat rotMatrix = cv::getRotationMatrix2D(
                cv::Point2f(paddedTempl.cols / 2.0, paddedTempl.rows / 2.0),
                -(currentAngle - pattern.angle), 1.0); // 상대 회전각

            cv::Mat rotatedTempl;
            cv::warpAffine(paddedTempl, rotatedTempl, rotMatrix, paddedTempl.size(),
                           cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));

            // 회전된 템플릿을 그레이스케일로 변환
            if (rotatedTempl.channels() == 3)
            {
                cv::cvtColor(rotatedTempl, templateForMatching, cv::COLOR_BGR2GRAY);
            }
            else
            {
                templateForMatching = rotatedTempl.clone();
            }
            
            // ★ 마스크도 동일하게 회전
            if (!paddedMask.empty())
            {
                cv::warpAffine(paddedMask, maskForMatching, rotMatrix, paddedMask.size(),
                              cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
                
                // ★ 마스크를 이진화 (보간으로 생긴 중간값 제거)
                cv::threshold(maskForMatching, maskForMatching, 127, 255, cv::THRESH_BINARY);
            }
        }

        // 타입을 CV_8U로 통일
        templateForMatching.convertTo(templateForMatching, CV_8U);

        // 템플릿 크기와 이미지 크기 검증
        if (templateForMatching.empty() || imageGray.empty())
        {
            logDebug(QString("각도 %1°: 템플릿 또는 이미지가 비어있음").arg(currentAngle));
            continue;
        }

        if (templateForMatching.cols > imageGray.cols || templateForMatching.rows > imageGray.rows)
        {
            logDebug(QString("각도 %1°: 템플릿이 이미지보다 큼 (템플릿:%2x%3, 이미지:%4x%5)")
                         .arg(currentAngle)
                         .arg(templateForMatching.cols)
                         .arg(templateForMatching.rows)
                         .arg(imageGray.cols)
                         .arg(imageGray.rows));
            continue;
        }

        // 매칭 메트릭 선택: 0=Coefficient, 1=Correlation
        int methodValue = (pattern.type == PatternType::FID) ? pattern.fidMatchMethod : pattern.patternMatchMethod;
        int matchMethod = (methodValue == 0) ? cv::TM_CCOEFF_NORMED : cv::TM_CCORR_NORMED;

        cv::Mat result;
        try
        {
            // ★ 마스크가 있으면 마스크 사용
            if (!maskForMatching.empty() && maskForMatching.size() == templateForMatching.size())
            {
                // 마스크 통계 확인
                int nonZeroPixels = cv::countNonZero(maskForMatching);
                int totalPixels = maskForMatching.rows * maskForMatching.cols;
                double maskRatio = (double)nonZeroPixels / totalPixels * 100.0;
                
                // 마스크 사용 템플릿 매칭
                cv::matchTemplate(imageGray, templateForMatching, result, matchMethod, maskForMatching);
            }
            else
            {
                // 마스크 없이 템플릿 매칭
                cv::matchTemplate(imageGray, templateForMatching, result, matchMethod);
            }
        }
        catch (const cv::Exception &e)
        {
            logDebug(QString("각도 %1°: 템플릿 매칭 오류 - %2").arg(currentAngle).arg(e.what()));
            continue;
        }

        if (result.empty())
        {
            logDebug(QString("각도 %1°: 매칭 결과가 비어있음").arg(currentAngle));
            continue;
        }

        // 최대값과 위치 찾기
        double minVal, maxVal;
        cv::Point minLoc, maxLoc;
        cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);

        // 1단계 최고 점수 업데이트
        if (maxVal > bestCoarseScore)
        {
            bestCoarseScore = maxVal;
            bestCoarseAngle = currentAngle;
            bestCoarseLocation.x = static_cast<int>(maxLoc.x + templateForMatching.cols / 2.0 + 0.5);
            bestCoarseLocation.y = static_cast<int>(maxLoc.y + templateForMatching.rows / 2.0 + 0.5);
        }

        // 조기 종료: 95% 이상 점수면 검색 중단
        if (maxVal >= 0.95)
        {
            matchLoc.x = static_cast<int>(maxLoc.x + templateForMatching.cols / 2.0 + 0.5);
            matchLoc.y = static_cast<int>(maxLoc.y + templateForMatching.rows / 2.0 + 0.5);
            score = maxVal;
            angle = currentAngle;
            return true;
        }

        // 전체 최고 점수도 업데이트 (2단계에서 사용)
        if (maxVal > bestScore)
        {
            bestScore = maxVal;
            bestAngle = currentAngle;
            bestLocation.x = static_cast<int>(maxLoc.x + templateForMatching.cols / 2.0 + 0.5);
            bestLocation.y = static_cast<int>(maxLoc.y + templateForMatching.rows / 2.0 + 0.5);
            bestTemplate = templateForMatching.clone();
        }
    }

    // === 2단계: 1단계 최적 각도 주변에서 1도 간격 정밀 검색 ===
    double fineSearchMin = bestCoarseAngle - 3.0;
    double fineSearchMax = bestCoarseAngle + 3.0;

    // 원래 범위를 벗어나지 않도록 제한
    fineSearchMin = std::max(fineSearchMin, adjustedMinAngle);
    fineSearchMax = std::min(fineSearchMax, adjustedMaxAngle);

    // 2단계 각도 리스트 생성 (1단계에서 이미 시도한 각도 제외)
    std::vector<double> fineAngles;
    for (double currentAngle = fineSearchMin; currentAngle <= fineSearchMax; currentAngle += 1.0)
    {
        // 1단계에서 이미 시도한 각도가 아닌 경우만 추가
        bool alreadyTested = false;
        for (double testedAngle : angleList)
        {
            if (std::abs(currentAngle - testedAngle) < 0.1)
            {
                alreadyTested = true;
                break;
            }
        }
        if (!alreadyTested)
        {
            fineAngles.push_back(currentAngle);
        }
    }

    for (double currentAngle : fineAngles)
    {
        // 티칭 각도인지 확인 (거의 없겠지만)
        bool isTeachingAngle = (std::abs(currentAngle - pattern.angle) < 0.01);

        // 템플릿 회전
        cv::Mat templateForMatching;

        if (isTeachingAngle)
        {
            // 티칭 각도인 경우: 원본 템플릿 그대로 사용
            templateForMatching = templGray.clone();
        }
        else
        {
            // 다른 각도인 경우: 패딩된 템플릿을 회전
            cv::Mat rotMatrix = cv::getRotationMatrix2D(
                cv::Point2f(paddedTempl.cols / 2.0, paddedTempl.rows / 2.0),
                -(currentAngle - pattern.angle), 1.0); // 상대 회전각

            cv::Mat rotatedTempl;
            cv::warpAffine(paddedTempl, rotatedTempl, rotMatrix, paddedTempl.size(),
                           cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));

            // 회전된 템플릿을 그레이스케일로 변환
            if (rotatedTempl.channels() == 3)
            {
                cv::cvtColor(rotatedTempl, templateForMatching, cv::COLOR_BGR2GRAY);
            }
            else
            {
                templateForMatching = rotatedTempl.clone();
            }
        }

        // 타입을 CV_8U로 통일
        templateForMatching.convertTo(templateForMatching, CV_8U);

        // 템플릿 크기와 이미지 크기 검증
        if (templateForMatching.empty() || imageGray.empty())
        {
            continue;
        }

        if (templateForMatching.cols > imageGray.cols || templateForMatching.rows > imageGray.rows)
        {
            continue;
        }

        // 매칭 메트릭 선택: 0=Coefficient, 1=Correlation
        int methodValue = (pattern.type == PatternType::FID) ? pattern.fidMatchMethod : pattern.patternMatchMethod;
        int matchMethod = (methodValue == 0) ? cv::TM_CCOEFF_NORMED : cv::TM_CCORR_NORMED;

        cv::Mat result;
        try
        {
            // FID 패턴 매칭은 마스크 없이 수행
            cv::matchTemplate(imageGray, templateForMatching, result, matchMethod);
        }
        catch (const cv::Exception &e)
        {
            continue;
        }

        if (result.empty())
        {
            continue;
        }

        // 최대값과 위치 찾기
        double minVal, maxVal;
        cv::Point minLoc, maxLoc;
        cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);

        // 가장 높은 점수 업데이트
        if (maxVal > bestScore)
        {
            bestScore = maxVal;
            bestAngle = currentAngle;
            bestLocation.x = static_cast<int>(maxLoc.x + templateForMatching.cols / 2.0 + 0.5);
            bestLocation.y = static_cast<int>(maxLoc.y + templateForMatching.rows / 2.0 + 0.5);
            bestTemplate = templateForMatching.clone();
        }
    }

    // 결과 반환
    matchLoc = bestLocation;
    score = bestScore;
    angle = bestAngle;

    return bestScore > 0.0;
}

bool InsProcessor::performFeatureMatching(const cv::Mat &image, const cv::Mat &templ,
                                          cv::Point &matchLoc, double &score, double &angle)
{
    try
    {
        // 입력 이미지 및 템플릿 유효성 검사
        if (image.empty() || templ.empty())
        {
            logDebug("특징점 매칭 실패: 입력 이미지 또는 템플릿이 비어있음");
            score = 0.0;
            angle = 0.0;
            return false;
        }

        // 그레이스케일 변환 (특징점 검출을 위해)
        cv::Mat imageGray, templGray;
        if (image.channels() == 3)
            cv::cvtColor(image, imageGray, cv::COLOR_BGR2GRAY);
        else
            image.copyTo(imageGray);

        if (templ.channels() == 3)
            cv::cvtColor(templ, templGray, cv::COLOR_BGR2GRAY);
        else
            templ.copyTo(templGray);

        // 특징점 검출 및 기술자 계산에 사용할 알고리즘 선택
        // SIFT가 성능이 좋지만 계산이 느릴 수 있음 (ORB는 속도가 빠른 대안)
        cv::Ptr<cv::Feature2D> feature_detector;

        // 더 많은 특징점 검출을 위한 파라미터 조정
        try
        {
            feature_detector = cv::SIFT::create(0, 3, 0.04, 10, 1.6);
        }
        catch (const cv::Exception &e)
        {
            // SIFT를 사용할 수 없는 경우 ORB로 대체
            logDebug("SIFT 초기화 실패, ORB 사용");
            feature_detector = cv::ORB::create(500);
        }

        // 특징점 및 기술자 추출
        std::vector<cv::KeyPoint> keypoints_image, keypoints_templ;
        cv::Mat descriptors_image, descriptors_templ;

        // 템플릿에서 흰색 영역을 제외한 마스크 생성
        cv::Mat templateMask;
        cv::threshold(templGray, templateMask, 250, 255, cv::THRESH_BINARY_INV);

        // 템플릿에서 먼저 특징점 검출 (마스크 적용하여 흰색 영역 제외)
        feature_detector->detectAndCompute(templGray, templateMask, keypoints_templ, descriptors_templ);

        // 템플릿에 충분한 특징점이 없으면 조기 종료
        if (keypoints_templ.size() < 4)
        {
            logDebug(QString("특징점 매칭 실패: 템플릿의 특징점이 부족함 (%1개)")
                         .arg(keypoints_templ.size()));
            score = 0.0;
            angle = 0.0;
            return false;
        }

        // 이미지에서 특징점 검출
        feature_detector->detectAndCompute(imageGray, cv::noArray(), keypoints_image, descriptors_image);

        // 이미지에 충분한 특징점이 없으면 조기 종료
        if (keypoints_image.size() < 4)
        {
            logDebug(QString("특징점 매칭 실패: 이미지의 특징점이 부족함 (%1개)")
                         .arg(keypoints_image.size()));
            score = 0.0;
            angle = 0.0;
            return false;
        }

        // 디버깅용 로그
        logDebug(QString("특징점 검출됨: 템플릿(%1개), 이미지(%2개)")
                     .arg(keypoints_templ.size())
                     .arg(keypoints_image.size()));

        // 매칭기 선택 - 기술자 유형에 따라 적절한 매칭기 선택
        cv::Ptr<cv::DescriptorMatcher> matcher;
        if (descriptors_templ.type() == CV_8U)
        {
            // ORB, BRISK, AKAZE는 이진 기술자를 사용
            matcher = cv::DescriptorMatcher::create(cv::DescriptorMatcher::BRUTEFORCE_HAMMING);
        }
        else
        {
            // SIFT, SURF는 부동소수점 기술자를 사용
            matcher = cv::DescriptorMatcher::create(cv::DescriptorMatcher::FLANNBASED);
        }

        // KNN 매칭 수행 (각 특징점에 대해 2개의 최근접 매칭 찾기)
        std::vector<std::vector<cv::DMatch>> knn_matches;
        try
        {
            matcher->knnMatch(descriptors_templ, descriptors_image, knn_matches, 2);
        }
        catch (const cv::Exception &e)
        {
            logDebug(QString("KNN 매칭 실패: %1").arg(e.what()));

            // FLANN 실패 시 브루트포스 방식으로 재시도
            matcher = cv::DescriptorMatcher::create(cv::DescriptorMatcher::BRUTEFORCE);
            matcher->knnMatch(descriptors_templ, descriptors_image, knn_matches, 2);
        }

        // 좋은 매칭만 필터링 (Lowe's ratio test)
        float ratio_thresh = 0.75f;
        std::vector<cv::DMatch> good_matches;
        for (const auto &match : knn_matches)
        {
            if (match.size() >= 2 && match[0].distance < ratio_thresh * match[1].distance)
            {
                good_matches.push_back(match[0]);
            }
        }

        // 매칭이 부족한 경우 - 임계값 조정해서 재시도
        if (good_matches.size() < 4 && !knn_matches.empty())
        {
            logDebug(QString("첫 번째 매칭 시도 실패: 좋은 매칭이 부족함 (%1개), 임계값 완화 시도")
                         .arg(good_matches.size()));

            good_matches.clear();
            ratio_thresh = 0.85f; // 더 관대한 임계값으로 재시도

            for (const auto &match : knn_matches)
            {
                if (match.size() >= 2 && match[0].distance < ratio_thresh * match[1].distance)
                {
                    good_matches.push_back(match[0]);
                }
            }
        }

        // 매칭이 여전히 부족하면 실패
        if (good_matches.size() < 4)
        {
            logDebug(QString("최종 특징점 매칭 실패: 좋은 매칭이 부족함 (%1개)")
                         .arg(good_matches.size()));
            score = 0.0;
            angle = 0.0;
            return false;
        }

        // 매칭 수 디버깅 출력
        logDebug(QString("좋은 매칭 발견: %1개").arg(good_matches.size()));

        // 매칭된 지점들의 좌표 추출
        std::vector<cv::Point2f> src_pts;
        std::vector<cv::Point2f> dst_pts;
        for (const auto &match : good_matches)
        {
            src_pts.push_back(keypoints_templ[match.queryIdx].pt);
            dst_pts.push_back(keypoints_image[match.trainIdx].pt);
        }

        // 호모그래피 계산 (RANSAC 사용)
        std::vector<uchar> inliers;         // RANSAC 인라이어 마스크
        double ransacReprojThreshold = 3.0; // 재투영 오차 임계값
        cv::Mat H = cv::findHomography(src_pts, dst_pts, cv::RANSAC,
                                       ransacReprojThreshold, inliers);

        // 호모그래피 실패하면 매칭 실패
        if (H.empty())
        {
            logDebug("특징점 매칭 실패: 호모그래피 계산 실패");
            score = 0.0;
            angle = 0.0;
            return false;
        }

        // RANSAC 인라이어 수 계산
        int inlierCount = 0;
        for (const auto &status : inliers)
        {
            if (status)
                inlierCount++;
        }

        // 인라이어 비율이 너무 낮으면 실패
        double inlierRatio = static_cast<double>(inlierCount) / good_matches.size();
        if (inlierRatio < 0.4)
        { // 40% 이하면 신뢰도가 낮음
            logDebug(QString("특징점 매칭 실패: 인라이어 비율이 낮음 (%1%)")
                         .arg(inlierRatio * 100.0, 0, 'f', 1));
            score = 0.0;
            angle = 0.0;
            return false;
        }

        // 템플릿 코너 변환 - 원본 템플릿 크기 사용
        std::vector<cv::Point2f> templ_corners(4);
        templ_corners[0] = cv::Point2f(0, 0);
        templ_corners[1] = cv::Point2f((float)(templ.cols - 1), 0);
        templ_corners[2] = cv::Point2f((float)(templ.cols - 1), (float)(templ.rows - 1));
        templ_corners[3] = cv::Point2f(0, (float)(templ.rows - 1));

        std::vector<cv::Point2f> scene_corners(4);
        cv::perspectiveTransform(templ_corners, scene_corners, H);

        // 매칭 위치 계산 (변환된 코너의 중심)
        float centerX = 0.0f, centerY = 0.0f;
        for (const auto &corner : scene_corners)
        {
            centerX += corner.x;
            centerY += corner.y;
        }
        matchLoc.x = static_cast<int>(centerX / 4.0f + 0.5f);
        matchLoc.y = static_cast<int>(centerY / 4.0f + 0.5f);

        // 회전 각도 계산
        // 원본 템플릿의 상단 가로 벡터
        cv::Point2f templVector = cv::Point2f(templ.cols, 0);
        // 변환된 벡터
        cv::Point2f transformedVector = scene_corners[1] - scene_corners[0];
        // 벡터 사이의 각도 계산 (라디안)
        float dot = templVector.x * transformedVector.x + templVector.y * transformedVector.y;
        float det = templVector.x * transformedVector.y - templVector.y * transformedVector.x;
        float angleRad = atan2(det, dot);
        // 라디안에서 각도로 변환 (-180 ~ +180 범위로 보정)
        angle = angleRad * 180.0 / CV_PI;

        // 매칭 점수 계산 (RANSAC 인라이어 비율)
        score = inlierRatio;

        // 디버깅 출력
        logDebug(QString("특징점 매칭 성공: 매칭=%1개, 인라이어=%2개(%3%), 위치=(%4,%5), 각도=%6°")
                     .arg(good_matches.size())
                     .arg(inlierCount)
                     .arg(inlierRatio * 100.0, 0, 'f', 1)
                     .arg(matchLoc.x)
                     .arg(matchLoc.y)
                     .arg(angle, 0, 'f', 1));

        return true;
    }
    catch (const cv::Exception &e)
    {
        logDebug(QString("특징점 매칭 중 OpenCV 예외 발생: %1").arg(e.what()));
        score = 0.0;
        angle = 0.0;
        return false;
    }
    catch (const std::exception &e)
    {
        logDebug(QString("특징점 매칭 중 일반 예외 발생: %1").arg(e.what()));
        score = 0.0;
        angle = 0.0;
        return false;
    }
    catch (...)
    {
        logDebug("특징점 매칭 중 알 수 없는 예외 발생");
        score = 0.0;
        angle = 0.0;
        return false;
    }
}

// SSIM (Structural Similarity Index) 검사
bool InsProcessor::checkSSIM(const cv::Mat &image, const PatternInfo &pattern, double &score, InspectionResult &result)
{
    QRectF rectF = pattern.rect;
    cv::Point2f center(rectF.x() + rectF.width() / 2.0f, rectF.y() + rectF.height() / 2.0f);

    double width = rectF.width();
    double height = rectF.height();

    cv::Mat currentROI;

    // 회전이 있는 경우
    if (std::abs(pattern.angle) > 0.1)
    {
        // 회전 각도에 따른 bounding box 크기 계산 (티칭과 동일)
        double angleRad = std::abs(pattern.angle) * M_PI / 180.0;
        double rotatedWidth = std::abs(width * cos(angleRad)) + std::abs(height * sin(angleRad));
        double rotatedHeight = std::abs(width * sin(angleRad)) + std::abs(height * cos(angleRad));

        int bboxWidth = static_cast<int>(rotatedWidth);
        int bboxHeight = static_cast<int>(rotatedHeight);

        cv::Rect bboxRoi(
            static_cast<int>(center.x - bboxWidth / 2.0),
            static_cast<int>(center.y - bboxHeight / 2.0),
            bboxWidth,
            bboxHeight);

        cv::Rect imageBounds(0, 0, image.cols, image.rows);
        cv::Rect validRoi = bboxRoi & imageBounds;

        if (validRoi.width <= 0 || validRoi.height <= 0)
        {
            logDebug(QString("SSIM 검사 실패: 유효하지 않은 ROI - 패턴 '%1'").arg(pattern.name));
            score = 0.0;
            return false;
        }

        // bounding box 크기의 빈 이미지 생성 (티칭과 동일)
        cv::Mat currentRegion = cv::Mat::zeros(bboxHeight, bboxWidth, image.type());

        // 유효한 영역만 복사
        int offsetX = validRoi.x - bboxRoi.x;
        int offsetY = validRoi.y - bboxRoi.y;
        cv::Mat validImage = image(validRoi);
        cv::Rect resultRect(offsetX, offsetY, validRoi.width, validRoi.height);
        validImage.copyTo(currentRegion(resultRect));

        // 티칭과 동일하게 역회전 없이 그대로 사용
        currentROI = currentRegion;
    }
    else
    {
        // 회전이 없는 경우: INS 영역 직접 추출 (티칭과 동일)
        cv::Rect roi(
            static_cast<int>(rectF.x()),
            static_cast<int>(rectF.y()),
            static_cast<int>(width),
            static_cast<int>(height));

        cv::Rect imageBounds(0, 0, image.cols, image.rows);
        cv::Rect validRoi = roi & imageBounds;

        if (validRoi.width <= 0 || validRoi.height <= 0)
        {
            logDebug(QString("SSIM 검사 실패: 유효하지 않은 ROI - 패턴 '%1'").arg(pattern.name));
            score = 0.0;
            return false;
        }

        currentROI = image(validRoi).clone();
    }

    // 티칭 때 적용한 필터를 검사 대상 이미지에도 순서대로 적용
    if (!pattern.filters.isEmpty())
    {
        cv::Mat processedROI = currentROI.clone();
        ImageProcessor processor;
        for (const FilterInfo &filter : pattern.filters)
        {
            if (filter.enabled)
            {
                cv::Mat tempFiltered;
                processor.applyFilter(processedROI, tempFiltered, filter);
                if (!tempFiltered.empty())
                {
                    processedROI = tempFiltered.clone();
                }
            }
        }
        currentROI = processedROI;
    }

    // 템플릿 이미지 가져오기 (검사용 templateImage 사용)
    QImage templateQImage = pattern.templateImage;
    if (templateQImage.isNull())
    {
        logDebug(QString("SSIM 검사 실패: 검사용 템플릿 이미지 없음 - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        return false;
    }

    // QImage를 cv::Mat으로 변환
    cv::Mat templateMat;
    QImage convertedTemplate = templateQImage.convertToFormat(QImage::Format_RGB888);
    cv::Mat tempMat(convertedTemplate.height(), convertedTemplate.width(), CV_8UC3,
                    const_cast<uchar *>(convertedTemplate.bits()),
                    static_cast<size_t>(convertedTemplate.bytesPerLine()));
    cv::cvtColor(tempMat.clone(), templateMat, cv::COLOR_RGB2BGR);

    // 매칭에서 찾은 각도로 템플릿 회전 (새 이미지는 그대로 두고 템플릿을 회전)
    cv::Mat rotatedTemplate = templateMat;
    if (std::abs(pattern.angle) > 0.1)
    {
        cv::Point2f templateCenter(templateMat.cols / 2.0f, templateMat.rows / 2.0f);
        cv::Mat rotMat = cv::getRotationMatrix2D(templateCenter, pattern.angle, 1.0);
        cv::warpAffine(templateMat, rotatedTemplate, rotMat, templateMat.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        
        logDebug(QString("SSIM: 템플릿을 %1° 회전 - 패턴 '%2'")
                     .arg(pattern.angle, 0, 'f', 2)
                     .arg(pattern.name));
    }

    logDebug(QString("SSIM: 템플릿 크기=%1x%2, 현재ROI 크기=%3x%4, 각도=%5°")
                 .arg(rotatedTemplate.cols).arg(rotatedTemplate.rows)
                 .arg(currentROI.cols).arg(currentROI.rows)
                 .arg(pattern.angle, 0, 'f', 2));

    // 크기가 다르면 템플릿을 현재 ROI 크기에 맞춤
    cv::Mat finalTemplate;
    if (rotatedTemplate.size() != currentROI.size())
    {
        logDebug(QString("SSIM: 크기 조정 필요 - resize 적용 (보간 오차 발생 가능)"));
        cv::resize(rotatedTemplate, finalTemplate, currentROI.size(), 0, 0, cv::INTER_LINEAR);
    }
    else
    {
        finalTemplate = rotatedTemplate;
    }

    // 그레이스케일 변환
    cv::Mat gray1, gray2;
    if (currentROI.channels() == 3)
        cv::cvtColor(currentROI, gray1, cv::COLOR_BGR2GRAY);
    else
        gray1 = currentROI;

    if (finalTemplate.channels() == 3)
        cv::cvtColor(finalTemplate, gray2, cv::COLOR_BGR2GRAY);
    else
        gray2 = finalTemplate;

    logDebug(QString("SSIM: Gray1 크기=%1x%2, Gray2 크기=%3x%4, 채널=%5/%6")
                 .arg(gray1.cols).arg(gray1.rows)
                 .arg(gray2.cols).arg(gray2.rows)
                 .arg(gray1.channels()).arg(gray2.channels()));
    
    // 동일한 이미지인지 먼저 체크 (픽셀 단위 비교)
    cv::Mat diff;
    cv::absdiff(gray1, gray2, diff);
    double maxDiff = 0;
    cv::minMaxLoc(diff, nullptr, &maxDiff);
    
    // 픽셀값 차이가 전혀 없으면 100% 처리
    if (maxDiff == 0) {
        score = 1.0;
        logDebug(QString("SSIM: 동일한 이미지 감지 - 100% 처리 (패턴='%1')").arg(pattern.name));
        
        // 차이 맵도 완전히 0으로 설정
        cv::Mat diffMap = cv::Mat::zeros(gray1.size(), CV_64F);
        cv::Mat colorHeatmap = cv::Mat::zeros(gray1.size(), CV_8UC3);
        
        QRectF rectF(pattern.rect.x(), pattern.rect.y(), 
                     pattern.rect.width(), pattern.rect.height());
        
        result.ssimDiffMap[pattern.id] = diffMap.clone();
        result.ssimHeatmap[pattern.id] = colorHeatmap.clone();
        result.ssimHeatmapRect[pattern.id] = rectF;
        result.insScores[pattern.id] = 1.0;
        result.insMethodTypes[pattern.id] = InspectionMethod::SSIM;
        
        return true;  // 100% 합격
    }

    // SSIM 계산
    const double C1 = 6.5025;   // (0.01 * 255)^2
    const double C2 = 58.5225;  // (0.03 * 255)^2

    cv::Mat I1, I2;
    gray1.convertTo(I1, CV_64F);
    gray2.convertTo(I2, CV_64F);

    cv::Mat I1_2 = I1.mul(I1);   // I1^2
    cv::Mat I2_2 = I2.mul(I2);   // I2^2
    cv::Mat I1_I2 = I1.mul(I2);  // I1 * I2

    cv::Mat mu1, mu2;
    cv::GaussianBlur(I1, mu1, cv::Size(11, 11), 1.5);
    cv::GaussianBlur(I2, mu2, cv::Size(11, 11), 1.5);

    cv::Mat mu1_2 = mu1.mul(mu1);
    cv::Mat mu2_2 = mu2.mul(mu2);
    cv::Mat mu1_mu2 = mu1.mul(mu2);

    cv::Mat sigma1_2, sigma2_2, sigma12;
    cv::GaussianBlur(I1_2, sigma1_2, cv::Size(11, 11), 1.5);
    sigma1_2 -= mu1_2;

    cv::GaussianBlur(I2_2, sigma2_2, cv::Size(11, 11), 1.5);
    sigma2_2 -= mu2_2;

    cv::GaussianBlur(I1_I2, sigma12, cv::Size(11, 11), 1.5);
    sigma12 -= mu1_mu2;

    cv::Mat t1 = 2 * mu1_mu2 + C1;
    cv::Mat t2 = 2 * sigma12 + C2;
    cv::Mat t3 = t1.mul(t2);

    t1 = mu1_2 + mu2_2 + C1;
    t2 = sigma1_2 + sigma2_2 + C2;
    t1 = t1.mul(t2);

    cv::Mat ssim_map;
    cv::divide(t3, t1, ssim_map);

    cv::Scalar mssim = cv::mean(ssim_map);
    double ssimValue = mssim[0];

    // SSIM 값은 0-1 범위, score도 0-1로 저장 (출력 시 100 곱함)
    score = ssimValue;

    logDebug(QString("SSIM: 계산 완료 - 패턴='%1', SSIM=%2%, 각도=%3°, resize=%4, 회전=%5")
                 .arg(pattern.name)
                 .arg(ssimValue * 100.0, 0, 'f', 2)
                 .arg(pattern.angle, 0, 'f', 2)
                 .arg(rotatedTemplate.size() != currentROI.size() ? "O" : "X")
                 .arg(std::abs(pattern.angle) > 0.1 ? "O" : "X"));

    // 차이 히트맵 생성 (1 - SSIM으로 차이가 클수록 밝게)
    cv::Mat diffMap;
    cv::subtract(cv::Scalar(1.0), ssim_map, diffMap);
    
    // ssimNgThreshold는 "차이 임계값" (예: 5% → 차이 5% 이상은 NG)
    // diffMap = (1 - SSIM)이므로, 차이 임계값 = ssimNgThreshold/100
    double ngThreshold = pattern.ssimNgThreshold / 100.0;
    
    // 임계값 미만의 픽셀은 0으로 설정 (정상 영역 제거, 차이 큰 부분만 남김)
    cv::Mat maskedDiffMap = diffMap.clone();
    for (int y = 0; y < maskedDiffMap.rows; y++) {
        double* row = maskedDiffMap.ptr<double>(y);
        for (int x = 0; x < maskedDiffMap.cols; x++) {
            if (row[x] < ngThreshold) {
                row[x] = 0.0;  // 차이가 작은 부분(정상)은 제거
            }
        }
    }
    
    // 0-255 범위로 변환하여 히트맵 생성
    cv::Mat heatmap;
    maskedDiffMap.convertTo(heatmap, CV_8U, 255.0);
    
    // 컬러 히트맵으로 변환 (COLORMAP_JET: 파랑→초록→빨강)
    cv::Mat colorHeatmap;
    cv::applyColorMap(heatmap, colorHeatmap, cv::COLORMAP_JET);
    
    // 원본 diffMap과 히트맵 저장
    result.ssimDiffMap[pattern.id] = diffMap.clone();          // 원본 저장 (실시간 갱신용)
    result.ssimHeatmap[pattern.id] = colorHeatmap.clone();
    result.ssimHeatmapRect[pattern.id] = rectF;  // 패턴 위치 저장

    // 결과 저장
    result.insMethodTypes[pattern.id] = InspectionMethod::SSIM;

    // NG 임계값 판정: 실제 패턴 박스 영역만 계산 (bounding box 전체가 아님)
    // ngThreshold는 위에서 이미 계산됨: (1 - ssimNgThreshold/100)
    
    int ngPixelCount = 0;
    int totalPixels = 0;
    
    // 회전이 있는 경우: 회전된 사각형 마스크 생성하여 내부 픽셀만 계산
    if (std::abs(pattern.angle) > 0.1)
    {
        // diffMap 중심점 (bounding box 중심)
        cv::Point2f diffCenter(diffMap.cols / 2.0f, diffMap.rows / 2.0f);
        cv::Size2f patternSize(width, height);
        
        // 회전된 사각형 생성
        cv::RotatedRect rotatedRect(diffCenter, patternSize, pattern.angle);
        
        // 마스크 생성
        cv::Mat mask = cv::Mat::zeros(diffMap.size(), CV_8UC1);
        cv::Point2f vertices[4];
        rotatedRect.points(vertices);
        
        std::vector<cv::Point> points;
        for (int i = 0; i < 4; i++) {
            points.push_back(cv::Point(static_cast<int>(std::round(vertices[i].x)),
                                      static_cast<int>(std::round(vertices[i].y))));
        }
        cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{points}, cv::Scalar(255));
        
        // 마스크 영역 내의 픽셀만 계산
        for (int y = 0; y < diffMap.rows; y++) {
            const double* diffRow = diffMap.ptr<double>(y);
            const uchar* maskRow = mask.ptr<uchar>(y);
            for (int x = 0; x < diffMap.cols; x++) {
                if (maskRow[x] > 0) {  // 패턴 박스 내부
                    totalPixels++;
                    if (diffRow[x] >= ngThreshold) {
                        ngPixelCount++;
                    }
                }
            }
        }
    }
    else
    {
        // 회전 없는 경우: 전체 diffMap 영역 사용 (패턴 rect 크기와 동일)
        totalPixels = diffMap.rows * diffMap.cols;
        for (int y = 0; y < diffMap.rows; y++) {
            const double* row = diffMap.ptr<double>(y);
            for (int x = 0; x < diffMap.cols; x++) {
                if (row[x] >= ngThreshold) {
                    ngPixelCount++;
                }
            }
        }
    }
    
    // NG 픽셀 비율 계산 (패턴 박스 영역 대비)
    double ngRatio = (totalPixels > 0) ? (static_cast<double>(ngPixelCount) / totalPixels) : 0.0;
    double ngRatioPercent = ngRatio * 100.0;
    
    // allowedNgRatio: NG 픽셀이 이 값(%) 이하면 합격
    bool passed = (ngRatioPercent <= pattern.allowedNgRatio);

    // score는 NG 임계값 이상 픽셀 비율로 설정 (0-1 범위, 낮을수록 좋음)
    score = ngRatio;

    logDebug(QString("SSIM 검사: 패턴 '%1', 차이>%2%인 영역=%3%, 허용=%4%, 결과=%5")
                 .arg(pattern.name)
                 .arg(pattern.ssimNgThreshold, 0, 'f', 0)
                 .arg(ngRatioPercent, 0, 'f', 2)
                 .arg(pattern.allowedNgRatio, 0, 'f', 1)
                 .arg(passed ? "PASS" : "FAIL"));

    return passed;
}

// ANOMALY (PatchCore) 검사
bool InsProcessor::checkAnomaly(const cv::Mat &image, const PatternInfo &pattern, double &score, InspectionResult &result)
{
    // 패턴별 PatchCore 모델 로드
    QString fullModelPath = QCoreApplication::applicationDirPath() + QString("/weights/%1/%1.xml").arg(pattern.name);
    
    QFileInfo modelFile(fullModelPath);
    if (!modelFile.exists()) {
        logDebug(QString("ANOMALY 검사 실패: 모델 파일 없음 - 패턴 '%1', 경로: %2").arg(pattern.name).arg(fullModelPath));
        score = 0.0;
        result.insScores[pattern.id] = score;
        result.insMethodTypes[pattern.id] = InspectionMethod::A_PC;
        return false;
    }
    
    // 모델 로드 (이미 로드된 경우 내부에서 자동 스킵)
    if (!initPatchCoreModel(fullModelPath)) {
        logDebug(QString("ANOMALY 검사 실패: 모델 로드 실패 - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        result.insScores[pattern.id] = score;
        result.insMethodTypes[pattern.id] = InspectionMethod::A_PC;
        return false;
    }
    
    // pattern.rect는 adjustedPattern이므로 이미 원본 영상 좌표
    QRectF rectF = pattern.rect;
    cv::Rect roiRect(static_cast<int>(rectF.x()), 
                     static_cast<int>(rectF.y()), 
                     static_cast<int>(rectF.width()), 
                     static_cast<int>(rectF.height()));
    
    // 범위 체크
    if (roiRect.x < 0 || roiRect.y < 0 ||
        roiRect.x + roiRect.width > image.cols ||
        roiRect.y + roiRect.height > image.rows ||
        roiRect.width <= 0 || roiRect.height <= 0)
    {
        logDebug(QString("ANOMALY 검사 실패: 유효하지 않은 ROI - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        result.insScores[pattern.id] = score;
        result.insMethodTypes[pattern.id] = InspectionMethod::A_PC;
        return false;
    }
    
    // ROI 영역만 추출
    cv::Mat roiImage = image(roiRect).clone();
    
    // PatchCore 추론 (멀티모델 추론 사용)
    cv::Mat anomalyMap;
    float anomalyScore = 0.0f;
    
    auto anomalyStart = std::chrono::high_resolution_clock::now();
    
    QMap<QString, std::vector<cv::Mat>> modelImages;
    modelImages[fullModelPath] = {roiImage};
    QMap<QString, std::vector<float>> modelScores;
    QMap<QString, std::vector<cv::Mat>> modelMaps;
    
    bool inferenceSuccess = ImageProcessor::runPatchCoreTensorRTMultiModelInference(modelImages, modelScores, modelMaps);
    
    if (inferenceSuccess && modelScores.contains(fullModelPath) && !modelScores[fullModelPath].empty()) {
        anomalyScore = modelScores[fullModelPath][0];
        anomalyMap = modelMaps[fullModelPath][0];
    } else {
        inferenceSuccess = false;
    }
    
    auto anomalyEnd = std::chrono::high_resolution_clock::now();
    auto anomalyDuration = std::chrono::duration_cast<std::chrono::milliseconds>(anomalyEnd - anomalyStart).count();
    
    // ANOMALY 추론 로그는 제거 (너무 상세함, 최종 결과에만 표시)
    
    if (!inferenceSuccess)
    {
        logDebug(QString("ANOMALY 검사 실패: PatchCore 추론 실패 - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        result.insScores[pattern.id] = score;
        result.insMethodTypes[pattern.id] = InspectionMethod::A_PC;
        return false;
    }
    
    if (anomalyMap.empty())
    {
        logDebug(QString("ANOMALY 검사 실패: anomaly map이 비어있음 - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        result.insScores[pattern.id] = score;
        result.insMethodTypes[pattern.id] = InspectionMethod::A_PC;
        return false;
    }
    
    // ROI 영역의 max anomaly score 계산 (0~100 범위)
    double minVal, maxVal;
    cv::minMaxLoc(anomalyMap, &minVal, &maxVal);
    float roiAnomalyScore = static_cast<float>(maxVal);
    
    // Score를 0~100 범위로 클리핑
    roiAnomalyScore = std::max(0.0f, std::min(100.0f, roiAnomalyScore));
    
    qDebug() << QString("[Anomaly 검사] 패턴: %1, min:%2 max:%3 threshold:%4")
        .arg(pattern.name).arg(minVal).arg(maxVal).arg(pattern.passThreshold);
    
    // Threshold 이상인 픽셀로 마스크 생성
    cv::Mat binaryMask;
    cv::threshold(anomalyMap, binaryMask, pattern.passThreshold, 255, cv::THRESH_BINARY);
    binaryMask.convertTo(binaryMask, CV_8U);
    
    // Contour로 덩어리 검출
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binaryMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    
    // anomalyMinBlobSize 또는 W/H 기준으로 필터링
    bool hasDefect = false;
    int maxDefectBlobSize = 0;
    std::vector<std::vector<cv::Point>> defectContours;
    
    for (const auto& contour : contours) {
        int blobSize = static_cast<int>(cv::contourArea(contour));
        cv::Rect bbox = cv::boundingRect(contour);
        
        // 면적 또는 W/H 중 하나라도 기준 이상이면 불량
        bool sizeCheck = (blobSize >= pattern.anomalyMinBlobSize);
        bool widthCheck = (bbox.width >= pattern.anomalyMinDefectWidth);
        bool heightCheck = (bbox.height >= pattern.anomalyMinDefectHeight);
        
        if (sizeCheck || (widthCheck && heightCheck)) {
            hasDefect = true;
            
            // ROI 상대좌표를 절대좌표로 변환
            std::vector<cv::Point> absoluteContour;
            for (const auto& pt : contour) {
                absoluteContour.push_back(cv::Point(pt.x + roiRect.x, pt.y + roiRect.y));
            }
            defectContours.push_back(absoluteContour);
            
            if (blobSize > maxDefectBlobSize) {
                maxDefectBlobSize = blobSize;
            }
        }
    }
    
    // Score 저장 (0~100 범위)
    score = static_cast<double>(roiAnomalyScore);
    
    // 불량 contour 저장 (절대좌표)
    result.anomalyDefectContours[pattern.id] = defectContours;
    
    // 원본 anomaly map 저장 (임계값 조절용)
    result.anomalyRawMap[pattern.id] = anomalyMap.clone();
    
    // ROI 영역의 Anomaly Map을 히트맵으로 저장
    cv::Mat normalized;
    anomalyMap.convertTo(normalized, CV_8U, 255.0 / 100.0);  // 0~100 -> 0~255
    
    cv::Mat colorHeatmap;
    cv::applyColorMap(normalized, colorHeatmap, cv::COLORMAP_JET);
    
    result.anomalyHeatmap[pattern.id] = colorHeatmap.clone();
    result.anomalyHeatmapRect[pattern.id] = pattern.rect;
    
    // 불량 이미지 저장 비활성화 (성능 최적화)
    
    // 결과 저장
    result.insScores[pattern.id] = score;
    result.insMethodTypes[pattern.id] = InspectionMethod::A_PC;
    
    return !hasDefect;
}

bool InsProcessor::checkDiff(const cv::Mat &image, const PatternInfo &pattern, double &score, InspectionResult &result)
{
    QRectF rectF = pattern.rect;
    cv::Point2f center(rectF.x() + rectF.width() / 2.0f, rectF.y() + rectF.height() / 2.0f);

    double width = rectF.width();
    double height = rectF.height();

    int extractW = static_cast<int>(width);
    int extractH = static_cast<int>(height);

    // 회전 각도에 따른 bounding box 크기 계산 (회전 없으면 원본 크기)
    double angleRad = pattern.angle * M_PI / 180.0;
    double rotatedWidth = std::abs(width * cos(angleRad)) + std::abs(height * sin(angleRad));
    double rotatedHeight = std::abs(width * sin(angleRad)) + std::abs(height * cos(angleRad));

    // bounding box 크기 (티칭과 동일하게 여유 10픽셀 추가)
    int bboxWidth = static_cast<int>(rotatedWidth);
    int bboxHeight = static_cast<int>(rotatedHeight);

    cv::Rect bboxRoi(
        static_cast<int>(center.x - bboxWidth / 2.0),
        static_cast<int>(center.y - bboxHeight / 2.0),
        bboxWidth,
        bboxHeight);

    cv::Rect imageBounds(0, 0, image.cols, image.rows);
    cv::Rect validRoi = bboxRoi & imageBounds;

    if (validRoi.width <= 0 || validRoi.height <= 0)
    {
        logDebug(QString("엣지 검사 실패: 유효하지 않은 ROI - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        return false;
    }

    // bounding box 크기의 빈 이미지 생성
    cv::Mat templateRegion = cv::Mat::zeros(bboxHeight, bboxWidth, image.type());

    // 유효한 영역만 복사
    int offsetX = validRoi.x - bboxRoi.x;
    int offsetY = validRoi.y - bboxRoi.y;
    cv::Mat validImage = image(validRoi);
    cv::Rect resultRect(offsetX, offsetY, validRoi.width, validRoi.height);
    validImage.copyTo(templateRegion(resultRect));

    // INS 영역 추출 위치 계산 (중앙)
    int startX = (templateRegion.cols - extractW) / 2;
    int startY = (templateRegion.rows - extractH) / 2;

    startX = std::max(0, std::min(startX, templateRegion.cols - extractW));
    startY = std::max(0, std::min(startY, templateRegion.rows - extractH));

    if (startX < 0 || startY < 0 || startX + extractW > templateRegion.cols ||
        startY + extractH > templateRegion.rows)
    {
        logDebug(QString("엣지 검사 실패: 추출 범위 초과 - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        return false;
    }

    // ===== 1. 전체 영역에 필터 순차 적용 =====
    cv::Mat processedRegion = templateRegion.clone();

    if (!pattern.filters.isEmpty())
    {
        logDebug(QString("전체 영역(%1x%2)에 %3개 필터 순차 적용")
                     .arg(templateRegion.cols)
                     .arg(templateRegion.rows)
                     .arg(pattern.filters.size()));

        ImageProcessor processor;
        for (const FilterInfo &filter : pattern.filters)
        {
            if (filter.enabled)
            {
                cv::Mat tempFiltered;
                processor.applyFilter(processedRegion, tempFiltered, filter);
                if (!tempFiltered.empty())
                {
                    processedRegion = tempFiltered.clone();
                }
            }
        }
    }

    // ===== 2. 전체 영역에서 그레이스케일 변환 =====
    cv::Mat processedGray;
    if (processedRegion.channels() == 3)
    {
        cv::cvtColor(processedRegion, processedGray, cv::COLOR_BGR2GRAY);
    }
    else
    {
        processedRegion.copyTo(processedGray);
    }

    // 템플릿 이미지가 있는지 확인
    if (pattern.templateImage.isNull())
    {
        logDebug(QString("엣지 검사 실패: 템플릿 이미지가 없음 - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        return false;
    }

    try
    {

        // 템플릿 이미지를 Mat으로 변환
        QImage qTemplateImage = pattern.templateImage.convertToFormat(QImage::Format_RGB888);
        cv::Mat templateMat;

        if (qTemplateImage.format() == QImage::Format_RGB888)
        {
            templateMat = cv::Mat(qTemplateImage.height(), qTemplateImage.width(),
                                  CV_8UC3, const_cast<uchar *>(qTemplateImage.bits()), qTemplateImage.bytesPerLine());
            templateMat = templateMat.clone();
            cv::cvtColor(templateMat, templateMat, cv::COLOR_RGB2BGR);
        }
        else
        {
            logDebug(QString("엣지 검사 실패: 이미지 형식 변환 실패 - 패턴 '%1'").arg(pattern.name));
            score = 0.0;
            return false;
        }

        // 템플릿 이미지 그레이스케일 변환
        cv::Mat templateGray;
        if (templateMat.channels() == 3)
        {
            cv::cvtColor(templateMat, templateGray, cv::COLOR_BGR2GRAY);
        }
        else
        {
            templateMat.copyTo(templateGray);
        }

        // 크기 확인 (템플릿은 INS 영역 크기, processedGray는 전체 사각형)
        if (templateGray.size().width > processedGray.size().width ||
            templateGray.size().height > processedGray.size().height)
        {
            logDebug(QString("엣지 검사 실패: 템플릿(%1x%2)이 전체 영역(%3x%4)보다 큼 - 패턴 '%5'")
                         .arg(templateGray.cols)
                         .arg(templateGray.rows)
                         .arg(processedGray.cols)
                         .arg(processedGray.rows)
                         .arg(pattern.name));
            score = 0.0;
            return false;
        }

        // ===== 3. 이진 이미지로 변환 후 XOR 비교 =====
        // processedGray를 이진화
        cv::Mat binary;
        cv::threshold(processedGray, binary, 127, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

        // 템플릿을 이진화
        cv::Mat templateBinary;
        cv::threshold(templateGray, templateBinary, 127, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

        // 이진 이미지 크기가 일치하지 않으면 템플릿 크기로 리사이징
        if (binary.size() != templateBinary.size())
        {
            cv::resize(binary, binary, templateBinary.size());
        }

        // XOR로 차이 계산 (DIFF 검사)
        cv::Mat diffMask;
        cv::bitwise_xor(binary, templateBinary, diffMask);

        int diffPixels = cv::countNonZero(diffMask);
        int totalPixels = diffMask.rows * diffMask.cols;

        // 유사도 점수 계산 (1에서 차이 비율 빼기)
        score = 1.0 - (double)diffPixels / totalPixels;

        // 비교 방식에 따른 결과 판단
        // score는 0-1 범위, pattern.passThreshold는 0-100 범위이므로 score를 백분율로 변환해서 비교
        double scorePercentage = score * 100.0;
        bool passed = (scorePercentage >= pattern.passThreshold);

        // ===== 4. 결과 저장: DIFF MASK 생성 =====
        result.insProcessedImages[pattern.id] = binary; // 이진 이미지 저장
        result.insMethodTypes[pattern.id] = InspectionMethod::DIFF;
        result.diffMask[pattern.id] = diffMask.clone(); // diff mask 저장
        result.insScores[pattern.id] = score;           // 점수 저장 (0-1 범위)
        result.insResults[pattern.id] = passed;         // 검사 결과 저장

        logDebug(QString("   └─ %1(DIFF): %2 (score=%3, thr=%4)")
                     .arg(pattern.name)
                     .arg(passed ? "PASS" : "NG")
                     .arg(scorePercentage, 0, 'f', 2)
                     .arg(pattern.passThreshold, 0, 'f', 2));

        return passed;
    }
    catch (const cv::Exception &e)
    {
        logDebug(QString("엣지 검사 중 OpenCV 예외 발생: '%1' - 패턴 '%2'").arg(e.what()).arg(pattern.name));
        score = 0.0;
        return false;
    }
    catch (const std::exception &e)
    {
        logDebug(QString("엣지 검사 중 일반 예외 발생: '%1' - 패턴 '%2'").arg(e.what()).arg(pattern.name));
        score = 0.0;
        return false;
    }
    catch (...)
    {
        logDebug(QString("엣지 검사 중 알 수 없는 예외 발생 - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        return false;
    }
}

// 간단한 AI 기반 매칭 스텁 (확장 포인트)
void InsProcessor::logDebug(const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString formattedMessage = QString("%1 - %2").arg(timestamp).arg(message);
    emit logMessage(formattedMessage);
}

cv::Mat InsProcessor::extractROI(const cv::Mat &image, const QRectF &rect, double angle, bool isTemplate)
{

    try
    {
        cv::Mat roiMat;

        // 중심점 계산
        cv::Point2f center(rect.x() + rect.width() / 2.0f, rect.y() + rect.height() / 2.0f);

        // 회전각에 따른 최소 필요 사각형 크기 계산
        double angleRad = std::abs(angle) * M_PI / 180.0;
        double width = rect.width();
        double height = rect.height();

        // 회전된 사각형의 경계 상자 크기 계산
        double rotatedWidth = std::abs(width * std::cos(angleRad)) + std::abs(height * std::sin(angleRad));
        double rotatedHeight = std::abs(width * std::sin(angleRad)) + std::abs(height * std::cos(angleRad));

        // bounding box 크기 (티칭과 동일하게 여유 없음)
        int bboxWidth = static_cast<int>(rotatedWidth);
        int bboxHeight = static_cast<int>(rotatedHeight);

        // bounding box ROI 영역 계산 (중심점 기준)
        cv::Rect bboxRoi(
            static_cast<int>(std::round(center.x - bboxWidth / 2.0)),
            static_cast<int>(std::round(center.y - bboxHeight / 2.0)),
            bboxWidth,
            bboxHeight);

        // 이미지 경계와 교집합 구하기
        cv::Rect imageBounds(0, 0, image.cols, image.rows);
        cv::Rect validRoi = bboxRoi & imageBounds;

        if (validRoi.width > 0 && validRoi.height > 0)
        {
            // bounding box 결과 이미지 생성 (검은색 배경 - 티칭과 동일)
            roiMat = cv::Mat::zeros(bboxHeight, bboxWidth, image.type());

            // 유효한 영역만 복사
            int offsetX = validRoi.x - bboxRoi.x;
            int offsetY = validRoi.y - bboxRoi.y;

            cv::Mat validImage = image(validRoi);
            cv::Rect resultRect(offsetX, offsetY, validRoi.width, validRoi.height);
            validImage.copyTo(roiMat(resultRect));

            // 패턴 영역 외부 마스킹 (패턴 영역만 보이도록)
            cv::Mat mask = cv::Mat::zeros(bboxHeight, bboxWidth, CV_8UC1);

            // ROI 내에서 패턴의 실제 위치 계산 (중앙 배치 대신 원래 위치 유지)
            cv::Point2f patternCenter(center.x - bboxRoi.x, center.y - bboxRoi.y);
            cv::Size2f patternSize(rect.width(), rect.height());

            if (std::abs(angle) > 0.1)
            {
                // 회전된 패턴의 경우: 회전된 사각형 마스크
                cv::Point2f vertices[4];
                cv::RotatedRect rotatedRect(patternCenter, patternSize, angle);
                rotatedRect.points(vertices);

                std::vector<cv::Point> points;
                for (int i = 0; i < 4; i++)
                {
                    points.push_back(cv::Point(static_cast<int>(std::round(vertices[i].x)),
                                               static_cast<int>(std::round(vertices[i].y)))); // 반올림 사용
                }

                cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{points}, cv::Scalar(255));
            }
            else
            {
                // 회전 없는 경우: 일반 사각형 마스크
                cv::Rect patternRect(
                    static_cast<int>(std::round(patternCenter.x - patternSize.width / 2)),
                    static_cast<int>(std::round(patternCenter.y - patternSize.height / 2)),
                    static_cast<int>(std::round(patternSize.width)),
                    static_cast<int>(std::round(patternSize.height)));
                cv::rectangle(mask, patternRect, cv::Scalar(255), -1);
            }

            // 마스크 반전: 패턴 영역 외부를 흰색으로 설정 (STRIP 검사를 위해)
            // 주석 처리: 화면 표시 시 원본이 보이도록
            // cv::Mat invertedMask;
            // cv::bitwise_not(mask, invertedMask);
            // roiMat.setTo(cv::Scalar(255, 255, 255), invertedMask);

            return roiMat;
        }

        return cv::Mat();
    }
    catch (const cv::Exception &e)
    {
        logDebug(QString("ROI 추출 중 OpenCV 예외 발생: %1").arg(e.what()));
        return cv::Mat();
    }
    catch (...)
    {
        logDebug("ROI 추출 중 알 수 없는 예외 발생");
        return cv::Mat();
    }
}

QImage InsProcessor::matToQImage(const cv::Mat &mat)
{
    if (mat.empty())
    {
        return QImage();
    }

    switch (mat.type())
    {
    case CV_8UC4:
    {
        // BGRA
        QImage qimg(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_ARGB32);
        return qimg.rgbSwapped();
    }
    case CV_8UC3:
    {
        // BGR
        QImage qimg(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_RGB888);
        return qimg.rgbSwapped();
    }
    case CV_8UC1:
    {
        // 그레이스케일
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8);
    }
    default:
        // 다른 형식은 BGR로 변환
        cv::Mat convertedMat;
        if (mat.channels() == 1)
        {
            cv::cvtColor(mat, convertedMat, cv::COLOR_GRAY2BGR);
        }
        else
        {
            mat.convertTo(convertedMat, CV_8UC3);
        }
        QImage qimg(convertedMat.data, convertedMat.cols, convertedMat.rows,
                    convertedMat.step, QImage::Format_RGB888);
        return qimg.rgbSwapped();
    }
}

bool InsProcessor::checkStrip(const cv::Mat &image, const PatternInfo &pattern, double &score, InspectionResult &result, const QList<PatternInfo> &patterns)
{
    try
    {
        // ROI 영역 추출 (회전 고려)
        cv::Mat roiImage = extractROI(image, pattern.rect, pattern.angle);
        if (roiImage.empty())
        {
            logDebug(QString("STRIP 길이 검사 실패: ROI 추출 실패 - %1").arg(pattern.name));
            score = 0.0;
            result.insMethodTypes[pattern.id] = InspectionMethod::STRIP;
            return false;
        }

        // 추출한 ROI 전체에 필터 적용
        if (!pattern.filters.isEmpty())
        {

            ImageProcessor processor;
            for (const FilterInfo &filter : pattern.filters)
            {
                if (filter.enabled)
                {
                    cv::Mat nextFiltered;
                    processor.applyFilter(roiImage, nextFiltered, filter);
                    if (!nextFiltered.empty())
                    {
                        roiImage = nextFiltered.clone();
                    }
                }
            }
        }

        // 템플릿 이미지 로드
        cv::Mat templateImage;

        if (!pattern.templateImage.isNull())
        {
            // QImage를 cv::Mat으로 변환
            QImage qImg = pattern.templateImage.convertToFormat(QImage::Format_RGB888);
            templateImage = cv::Mat(qImg.height(), qImg.width(), CV_8UC3, (void *)qImg.constBits(), qImg.bytesPerLine());
            templateImage = templateImage.clone(); // 데이터 복사
            cv::cvtColor(templateImage, templateImage, cv::COLOR_RGB2BGR);
        }
        else
        {
            logDebug(QString("STRIP 길이 검사 실패: 템플릿 이미지 없음 - %1").arg(pattern.name));
            score = 0.0;
            result.insMethodTypes[pattern.id] = InspectionMethod::STRIP;
            return false;
        }

        // STRIP 검사 수행 (INS 패턴 각도 적용)
        cv::Mat resultImage;
        cv::Point startPoint, maxGradientPoint;
        std::vector<cv::Point> gradientPoints;
        int leftThickness = 0, rightThickness = 0;

        // 두께 측정 상세 좌표를 저장할 변수들 (전체 함수 scope에서 사용)
        cv::Point leftTopPoint, leftBottomPoint, rightTopPoint, rightBottomPoint;

        // 포인트 저장 변수들
        std::vector<cv::Point> frontThicknessPoints, rearThicknessPoints;

        // 디버그: STRIP 검사 임계값 확인

        // 측정된 두께 값들을 저장할 변수 (FRONT + REAR)
        int measuredMinThickness = 0, measuredMaxThickness = 0, measuredAvgThickness = 0;
        int rearMeasuredMinThickness = 0, rearMeasuredMaxThickness = 0, rearMeasuredAvgThickness = 0;
        cv::Point frontBoxTopLeft, rearBoxTopLeft;

        // EDGE 검사 결과 변수들
        int edgeIrregularityCount = 0;
        double edgeMaxDeviation = 0.0;
        cv::Point edgeBoxTopLeft;
        bool edgePassed = true;
        int edgeAverageX = 0;
        std::vector<cv::Point> edgePoints; // EDGE 포인트들을 받을 변수

        // STRIP 길이 검사 결과 변수들
        bool stripLengthPassed = true;
        double stripMeasuredLength = 0.0;
        double stripMeasuredLengthPx = 0.0; // 픽셀 원본값
        cv::Point stripLengthStartPoint;
        cv::Point stripLengthEndPoint;

        // performStripInspection 호출을 간소화: PatternInfo 전체를 전달
        std::vector<cv::Point> frontBlackRegionPoints, rearBlackRegionPoints; // 검은색 구간 포인트 (빨간색 표시용)
        cv::Point frontBoxCenterROI, rearBoxCenterROI, edgeBoxCenterROI;      // ROI 좌표계의 박스 중심
        cv::Size frontBoxSz, rearBoxSz, edgeBoxSz;                            // 박스 크기

        // 최소/최대 스캔 라인
        cv::Point frontMinScanTop, frontMinScanBottom, frontMaxScanTop, frontMaxScanBottom;
        cv::Point rearMinScanTop, rearMinScanBottom, rearMaxScanTop, rearMaxScanBottom;

        // 스캔 라인 (ImageProcessor에서 생성)
        std::vector<std::pair<cv::Point, cv::Point>> frontScanLinesROI, rearScanLinesROI;

        bool isPassed = ImageProcessor::performStripInspection(roiImage, templateImage,
                                                               pattern,
                                                               score, startPoint, maxGradientPoint, gradientPoints, resultImage, &edgePoints,
                                                               &stripLengthPassed, &stripMeasuredLength, &stripLengthStartPoint, &stripLengthEndPoint,
                                                               &frontThicknessPoints, &rearThicknessPoints,
                                                               &frontBlackRegionPoints, &rearBlackRegionPoints,
                                                               &stripMeasuredLengthPx,
                                                               &frontBoxCenterROI, &frontBoxSz,
                                                               &rearBoxCenterROI, &rearBoxSz,
                                                               &edgeBoxCenterROI, &edgeBoxSz,
                                                               &frontMinScanTop, &frontMinScanBottom, &frontMaxScanTop, &frontMaxScanBottom,
                                                               &rearMinScanTop, &rearMinScanBottom, &rearMaxScanTop, &rearMaxScanBottom,
                                                               &frontScanLinesROI, &rearScanLinesROI);

        // FRONT 두께 통계 계산 (ImageProcessor에서 받은 픽셀 데이터로부터)
        if (!frontThicknessPoints.empty())
        {
            std::vector<int> thicknesses;
            for (const cv::Point &pt : frontThicknessPoints)
            {
                thicknesses.push_back(pt.y); // y값이 두께 (픽셀)
            }

            measuredMinThickness = *std::min_element(thicknesses.begin(), thicknesses.end());
            measuredMaxThickness = *std::max_element(thicknesses.begin(), thicknesses.end());

            int sum = std::accumulate(thicknesses.begin(), thicknesses.end(), 0);
            measuredAvgThickness = sum / static_cast<int>(thicknesses.size());
        }

        // REAR 두께 통계 계산 (ImageProcessor에서 받은 픽셀 데이터로부터)
        if (!rearThicknessPoints.empty())
        {
            std::vector<int> thicknesses;
            for (const cv::Point &pt : rearThicknessPoints)
            {
                thicknesses.push_back(pt.y); // y값이 두께 (픽셀)
            }

            rearMeasuredMinThickness = *std::min_element(thicknesses.begin(), thicknesses.end());
            rearMeasuredMaxThickness = *std::max_element(thicknesses.begin(), thicknesses.end());

            int sum = std::accumulate(thicknesses.begin(), thicknesses.end(), 0);
            rearMeasuredAvgThickness = sum / static_cast<int>(thicknesses.size());
        }

        double thicknessPixelToMm = pattern.stripLengthConversionMm / pattern.stripLengthCalibrationPx;

        // ROI 좌표를 원본 이미지 좌표로 변환 (extractROI와 정확히 동일한 방식으로 계산)
        cv::Point2f patternCenter(pattern.rect.x() + pattern.rect.width() / 2.0f,
                                  pattern.rect.y() + pattern.rect.height() / 2.0f);

        // extractROI와 동일한 bounding box 크기 계산
        double angleRad = std::abs(pattern.angle) * M_PI / 180.0;
        double width = pattern.rect.width();
        double height = pattern.rect.height();
        double rotatedWidth = std::abs(width * std::cos(angleRad)) + std::abs(height * std::sin(angleRad));
        double rotatedHeight = std::abs(width * std::sin(angleRad)) + std::abs(height * std::cos(angleRad));

        // bounding box 크기 (extractROI와 동일하게 여유 없음)
        int bboxWidth = static_cast<int>(rotatedWidth);
        int bboxHeight = static_cast<int>(rotatedHeight);

        // extractROI와 정확히 동일한 bboxRoi 계산
        cv::Point2f center(pattern.rect.x() + pattern.rect.width() / 2.0f,
                           pattern.rect.y() + pattern.rect.height() / 2.0f);

        cv::Rect bboxRoi(
            static_cast<int>(std::round(center.x - bboxWidth / 2.0)),
            static_cast<int>(std::round(center.y - bboxHeight / 2.0)),
            bboxWidth,
            bboxHeight);

        cv::Point2f offset(bboxRoi.x, bboxRoi.y);

        // FRONT/REAR 포인트: 패턴 상대좌표로 변환하여 저장 (각도 적용)
        // frontThicknessPoints와 rearThicknessPoints는 (index, thickness) 형태의 데이터이므로
        // 공간 좌표 변환을 수행하지 않습니다. (그래프용 데이터)
        QList<QPoint> frontPointsConverted;
        QList<QPoint> rearPointsConverted;

        // 그래프 데이터로 그대로 사용하기 위해 변환 없이 저장하거나,
        // 필요하다면 별도의 필드에 저장해야 합니다.
        // 현재는 빈 리스트로 두어 잘못된 좌표 표시를 방지합니다.

        // 검은색 구간 포인트들도 절대좌표로 변환 (박스 기준 로컬 좌표 -> 절대좌표)
        // 최소/최대 두께의 라인만 표시
        QList<QPoint> frontBlackPointsConverted;

        // frontThicknessPoints: cv::Point(라인인덱스, 두께)
        // 최소/최대 두께의 라인 인덱스 찾기
        int minThicknessLineIdx = -1, maxThicknessLineIdx = -1;
        int minThickness = INT_MAX, maxThickness = INT_MIN;

        for (size_t i = 0; i < frontThicknessPoints.size(); i++)
        {
            int thickness = frontThicknessPoints[i].y;
            if (thickness < minThickness)
            {
                minThickness = thickness;
                minThicknessLineIdx = i;
            }
            if (thickness > maxThickness)
            {
                maxThickness = thickness;
                maxThicknessLineIdx = i;
            }
        }

        // 최소/최대 라인의 포인트만 필터링해서 표시
        // 검은색 포인트는 검출된 그대로만 절대좌표로 변환 (어떤 회전도 적용 금지)

        for (const cv::Point &pt : frontBlackRegionPoints)
        {
            // pt는 ROI 내 절대좌표 (이미 올바른 위치)
            // 이미지 절대좌표로 변환: 그냥 bboxRoi 오프셋만 더함
            int absX = bboxRoi.x + pt.x;
            int absY = bboxRoi.y + pt.y;

            QPoint ptAbs(absX, absY);
            frontBlackPointsConverted.append(ptAbs);
        }

        QList<QPoint> rearBlackPointsConverted;

        // 검은색 포인트는 검출된 그대로만 절대좌표로 변환 (어떤 회전도 적용 금지)
        for (const cv::Point &pt : rearBlackRegionPoints)
        {
            // pt는 ROI 내 절대좌표 (이미 올바른 위치)
            // 이미지 절대좌표로 변환: 그냥 bboxRoi 오프셋만 더함
            int absX = bboxRoi.x + pt.x;
            int absY = bboxRoi.y + pt.y;

            QPoint ptAbs(absX, absY);
            rearBlackPointsConverted.append(ptAbs);
        }

        // 변환된 포인트 저장 (절대좌표)
        result.stripFrontThicknessPoints[pattern.id] = frontPointsConverted;
        result.stripRearThicknessPoints[pattern.id] = rearPointsConverted;
        result.stripFrontBlackRegionPoints[pattern.id] = frontBlackPointsConverted;
        result.stripRearBlackRegionPoints[pattern.id] = rearBlackPointsConverted;

        // 스캔 라인 생성 및 저장 (시각화용)
        QList<QPair<QPoint, QPoint>> frontScanLinesAbs;
        QList<QPair<QPoint, QPoint>> rearScanLinesAbs;

        // ImageProcessor에서 받은 스캔 라인을 절대 좌표로 변환
        for (const auto &line : frontScanLinesROI)
        {
            QPoint absTop(bboxRoi.x + line.first.x, bboxRoi.y + line.first.y);
            QPoint absBottom(bboxRoi.x + line.second.x, bboxRoi.y + line.second.y);
            frontScanLinesAbs.append(qMakePair(absTop, absBottom));
        }

        for (const auto &line : rearScanLinesROI)
        {
            QPoint absTop(bboxRoi.x + line.first.x, bboxRoi.y + line.first.y);
            QPoint absBottom(bboxRoi.x + line.second.x, bboxRoi.y + line.second.y);
            rearScanLinesAbs.append(qMakePair(absTop, absBottom));
        }

        // 스캔 라인 저장
        result.stripFrontScanLines[pattern.id] = frontScanLinesAbs;
        result.stripRearScanLines[pattern.id] = rearScanLinesAbs;

        // ImageProcessor에서 이미 계산된 두께 값을 그대로 사용
        // (잘못된 Y좌표 범위 계산 제거됨)

        // FRONT 두께 포인트는 (index, thickness) 그래프 데이터이므로 공간 좌표 변환 안함
        // frontPointsConverted는 의도적으로 비어있음 (frontThicknessPoints 사용)

        // REAR 두께 포인트는 (index, thickness) 그래프 데이터이므로 공간 좌표 변환 안함
        // rearPointsConverted는 의도적으로 비어있음 (rearThicknessPoints 사용)

        // OpenCV에서 검출된 gradientPoints를 사용 (4개 포인트)
        QPoint absPoint1, absPoint2, absPoint3, absPoint4;

        if (gradientPoints.size() >= 4)
        {
            // OpenCV gradientPoints 순서 재정렬 - 올바른 위치로 매핑
            std::vector<cv::Point> orderedPoints = {
                gradientPoints[0], // Point 1: 왼쪽 위
                gradientPoints[2], // Point 2: 왼쪽 아래
                gradientPoints[1], // Point 3: 오른쪽 위
                gradientPoints[3]  // Point 4: 오른쪽 아래
            };

            // ROI 좌표를 절대 좌표로 변환 (회전 없음, 오프셋만 적용)
            // extractROI는 이미지를 회전시키지 않고 crop만 하므로,
            // ROI 내의 점들은 원본 이미지에서 offset만큼만 이동하면 됨
            if (orderedPoints.size() >= 4)
            {
                absPoint1 = QPoint(orderedPoints[0].x + offset.x, orderedPoints[0].y + offset.y);
                absPoint2 = QPoint(orderedPoints[1].x + offset.x, orderedPoints[1].y + offset.y);
                absPoint3 = QPoint(orderedPoints[2].x + offset.x, orderedPoints[2].y + offset.y);
                absPoint4 = QPoint(orderedPoints[3].x + offset.x, orderedPoints[3].y + offset.y);
            }
        }
        else
        {
            logDebug("STRIP inspection failed: Insufficient gradient points (" + QString::number(gradientPoints.size()) + "/4)");
            score = 0.0;
            return false; // FAIL 처리
        }

        // InspectionResult에 저장
        result.stripPoint1[pattern.id] = absPoint1;
        result.stripPoint2[pattern.id] = absPoint2;
        result.stripPoint3[pattern.id] = absPoint3;
        result.stripPoint4[pattern.id] = absPoint4;
        result.stripPointsValid[pattern.id] = true;

        // STRIP 길이 검사 결과 저장
        result.stripLengthResults[pattern.id] = stripLengthPassed;
        result.stripMeasuredLength[pattern.id] = stripMeasuredLength;
        result.stripMeasuredLengthPx[pattern.id] = stripMeasuredLengthPx; // 픽셀 원본값 저장

        // STRIP 박스 정보 저장 (ROI 좌표를 scene 좌표로 변환)
        // ImageProcessor에서 반환된 boxCenter는 ROI 이미지 절대 좌표
        // extractROI는 이미지를 회전시키지 않으므로, 오프셋만 더하면 됨 (추가 회전 불필요)

        QPointF frontBoxCenterScene(0, 0);
        QPointF rearBoxCenterScene(0, 0);
        QPointF edgeBoxCenterScene(0, 0);

        frontBoxCenterScene = QPointF(frontBoxCenterROI.x + offset.x, frontBoxCenterROI.y + offset.y);
        rearBoxCenterScene = QPointF(rearBoxCenterROI.x + offset.x, rearBoxCenterROI.y + offset.y);
        edgeBoxCenterScene = QPointF(edgeBoxCenterROI.x + offset.x, edgeBoxCenterROI.y + offset.y);

        result.stripFrontBoxCenter[pattern.id] = frontBoxCenterScene;
        // 원본 박스 크기 저장 (회전 투영은 CameraView에서 처리)
        result.stripFrontBoxSize[pattern.id] = QSizeF(pattern.stripThicknessBoxWidth, pattern.stripThicknessBoxHeight);
        result.stripRearBoxCenter[pattern.id] = rearBoxCenterScene;
        result.stripRearBoxSize[pattern.id] = QSizeF(pattern.stripRearThicknessBoxWidth, pattern.stripRearThicknessBoxHeight);

        // EDGE 박스도 절대좌표로 저장
        result.edgeBoxCenter[pattern.id] = edgeBoxCenterScene;
        result.edgeBoxSize[pattern.id] = QSizeF(pattern.stripEdgeBoxWidth, pattern.stripEdgeBoxHeight);

        // STRIP 길이 측정 점들을 절대 좌표로 변환
        QPoint absStripLengthStart = QPoint(stripLengthStartPoint.x + offset.x, stripLengthStartPoint.y + offset.y);
        QPoint absStripLengthEnd = QPoint(stripLengthEndPoint.x + offset.x, stripLengthEndPoint.y + offset.y);
        result.stripLengthStartPoint[pattern.id] = absStripLengthStart;
        result.stripLengthEndPoint[pattern.id] = absStripLengthEnd;

        // 측정된 두께를 검사 결과에 저장 (측정값이 있으면 항상 저장)
        result.stripMeasuredThicknessMin[pattern.id] = measuredMinThickness;
        result.stripMeasuredThicknessMax[pattern.id] = measuredMaxThickness;
        result.stripMeasuredThicknessAvg[pattern.id] = measuredAvgThickness;
        result.stripThicknessMeasured[pattern.id] = (measuredAvgThickness > 0);

        result.stripRearMeasuredThicknessMin[pattern.id] = rearMeasuredMinThickness;
        result.stripRearMeasuredThicknessMax[pattern.id] = rearMeasuredMaxThickness;
        result.stripRearMeasuredThicknessAvg[pattern.id] = rearMeasuredAvgThickness;
        result.stripRearThicknessMeasured[pattern.id] = (rearMeasuredAvgThickness > 0);

        // 각 검사 항목별 PASS/FAIL 판정
        bool frontThicknessPassed = true;
        bool rearThicknessPassed = true;
        bool edgeTestPassed = edgePassed; // EDGE 검사 결과 사용

        // FRONT 두께 검사 판정 (측정값이 있고 calibration이 있는 경우)
        // 최소, 최대, 평균 모두 설정 범위 내에 있어야 통과
        if (measuredAvgThickness > 0 && pattern.stripLengthCalibrationPx > 0)
        {
            double pixelToMm = pattern.stripLengthConversionMm / pattern.stripLengthCalibrationPx;
            double minMm = measuredMinThickness * pixelToMm;
            double maxMm = measuredMaxThickness * pixelToMm;
            double avgMm = measuredAvgThickness * pixelToMm;

            frontThicknessPassed = (minMm >= pattern.stripThicknessMin &&
                                    maxMm <= pattern.stripThicknessMax);
        }

        // REAR 두께 검사 판정 (측정값이 있고 calibration이 있는 경우)
        // 최소, 최대, 평균 모두 설정 범위 내에 있어야 통과
        if (rearMeasuredAvgThickness > 0 && pattern.stripLengthCalibrationPx > 0)
        {
            double pixelToMm = pattern.stripLengthConversionMm / pattern.stripLengthCalibrationPx;
            double minMm = rearMeasuredMinThickness * pixelToMm;
            double maxMm = rearMeasuredMaxThickness * pixelToMm;
            double avgMm = rearMeasuredAvgThickness * pixelToMm;

            rearThicknessPassed = (minMm >= pattern.stripRearThicknessMin &&
                                   maxMm <= pattern.stripRearThicknessMax);
        }

        // EDGE 검사 결과 확인 (이미 edgePassed 변수에 저장되어 있음)
        if (result.edgeResults.contains(pattern.id))
        {
            edgeTestPassed = result.edgeResults[pattern.id];
        }

        // 모든 검사 항목을 AND 연산으로 최종 판정
        bool allTestsPassed = isPassed && stripLengthPassed && frontThicknessPassed && rearThicknessPassed && edgeTestPassed;

        // qDebug() << "[STRIP 최종 판정]" << pattern.name
        //          << "isPassed:" << isPassed
        //          << "stripLength:" << stripLengthPassed
        //          << "front:" << frontThicknessPassed
        //          << "rear:" << rearThicknessPassed
        //          << "edge:" << edgeTestPassed
        //          << "→ allTestsPassed:" << allTestsPassed;

        if (allTestsPassed)
        {
            // 좌표 변환 적용

            startPoint.x += static_cast<int>(offset.x);
            startPoint.y += static_cast<int>(offset.y);
            maxGradientPoint.x += static_cast<int>(offset.x);
            maxGradientPoint.y += static_cast<int>(offset.y);

            for (auto &point : gradientPoints)
            {
                point.x += static_cast<int>(offset.x);
                point.y += static_cast<int>(offset.y);
            }

            // 실제 측정 지점들을 저장 (절대좌표)
            result.stripStartPoint[pattern.id] = QPoint(startPoint.x, startPoint.y);
            result.stripMaxGradientPoint[pattern.id] = QPoint(maxGradientPoint.x, maxGradientPoint.y);
            result.stripMeasuredThicknessLeft[pattern.id] = leftThickness;   // 좌측 두께
            result.stripMeasuredThicknessRight[pattern.id] = rightThickness; // 우측 두께
        }
        else
        {
            // isPassed가 false인 경우 측정 지점만 0으로 초기화
            result.stripStartPoint[pattern.id] = QPoint(0, 0);
            result.stripMaxGradientPoint[pattern.id] = QPoint(0, 0);
            result.stripMeasuredThicknessLeft[pattern.id] = 0;
            result.stripMeasuredThicknessRight[pattern.id] = 0;
        }

        // 박스 위치를 패턴 중심 기준 상대좌표로 저장
        QPointF patternCenterForBox = pattern.rect.center();

        // 주의: stripFrontBoxCenter와 stripRearBoxCenter는 이미 위에서 설정되었습니다 (2575줄)
        // 여기서 다시 설정하면 안 됩니다!
        // 이 섹션은 REAR 검사가 실패한 경우 fallback으로 상대좌표를 사용했던 legacy 코드입니다.

        // fallback 제거 - 계산된 박스 정보를 신뢰합니다
        // EDGE 박스는 위에서 절대좌표로 이미 저장됨

        // EDGE 포인트들을 절대좌표로 변환 (회전 없이 오프셋만 적용)
        QList<QPoint> absoluteEdgePoints;
        if (!edgePoints.empty())
        {
            // EDGE 포인트 필터링: 시작/끝 퍼센트만큼 제외
            int totalPoints = edgePoints.size();
            int startSkip = (totalPoints * pattern.edgeStartPercent) / 100;
            int endSkip = (totalPoints * pattern.edgeEndPercent) / 100;

            // 유효한 범위 확인
            int validStart = startSkip;
            int validEnd = totalPoints - endSkip;
            if (validStart >= validEnd)
            {
                qDebug() << "EDGE 필터링 오류: 유효한 포인트가 없음 (시작:" << validStart << ", 끝:" << validEnd << ")";
                validStart = 0;
                validEnd = totalPoints;
            }

            for (int i = validStart; i < validEnd; i++)
            {
                const cv::Point &point = edgePoints[i];
                // EDGE는 수직 절단면이므로 회전 적용하지 않고 오프셋만 적용
                QPoint absolutePoint(point.x + static_cast<int>(offset.x),
                                     point.y + static_cast<int>(offset.y));
                absoluteEdgePoints.append(absolutePoint);
            }
            result.edgeAbsolutePoints[pattern.id] = absoluteEdgePoints;
        }

        // EDGE 포인트 통계 계산 (절대 좌표 기준으로 mm 변환)
        double edgeAvgX = 0.0;
        double edgeMaxDeviationMm = 0.0;
        double edgeMinDeviationMm = 0.0;
        double edgeAvgDeviationMm = 0.0;
        int edgeOutlierCount = 0;

        if (!absoluteEdgePoints.empty())
        {
            // 1. 평균 X 좌표 계산 (절대 좌표의 픽셀값)
            double sumX = 0.0;
            for (const QPoint &pt : absoluteEdgePoints)
            {
                sumX += pt.x();
            }
            edgeAvgX = sumX / absoluteEdgePoints.size();

            // 2. mm 변환을 위한 캘리브레이션 확인
            double pixelToMm = 0.0;
            if (pattern.stripLengthCalibrationPx > 0 && pattern.stripLengthConversionMm > 0)
            {
                pixelToMm = pattern.stripLengthConversionMm / pattern.stripLengthCalibrationPx;
            }

            // 3. 각 포인트와 평균선 사이의 거리 계산 (mm 변환)
            double maxDistancePx = 0.0;
            double minX = absoluteEdgePoints[0].x();
            double maxX = absoluteEdgePoints[0].x();

            // OpenCV fitLine을 사용하여 EDGE 포인트들에 최적의 직선 맞추기
            std::vector<cv::Point2f> points;
            for (const QPoint &pt : absoluteEdgePoints)
            {
                points.push_back(cv::Point2f(pt.x(), pt.y()));
            }

            cv::Vec4f lineParams;
            cv::fitLine(points, lineParams, cv::DIST_L2, 0, 0.01, 0.01);

            // lineParams: [vx, vy, x0, y0]
            // vx, vy: 직선의 방향 벡터 (정규화됨)
            // x0, y0: 직선이 지나는 점
            float vx = lineParams[0];
            float vy = lineParams[1];
            float x0 = lineParams[2];
            float y0 = lineParams[3];

            // y = mx + b 형태로 변환
            double m = 0.0, b = 0.0;
            if (std::abs(vx) > 0.001)
            {
                m = vy / vx;     // 기울기
                b = y0 - m * x0; // y절편
            }
            else
            {
                // 완전 수직선인 경우
                m = 1e6; // 매우 큰 기울기
                b = 0;
            }

            double minDistancePx = std::numeric_limits<double>::max();
            double sumDistancePx = 0.0;
            QList<double> pointDistancesMm; // 각 포인트별 거리 mm 저장

            for (int i = 0; i < absoluteEdgePoints.size(); i++)
            {
                const QPoint &pt = absoluteEdgePoints[i];
                minX = std::min(minX, static_cast<double>(pt.x()));
                maxX = std::max(maxX, static_cast<double>(pt.x()));

                // 점 (px, py)와 직선 mx - y + b = 0 사이의 거리
                // 거리 = |m*px - py + b| / sqrt(m^2 + 1)
                double numerator = std::abs(m * pt.x() - pt.y() + b);
                double denominator = std::sqrt(m * m + 1.0);
                double distancePx = numerator / denominator;

                maxDistancePx = std::max(maxDistancePx, distancePx);
                minDistancePx = std::min(minDistancePx, distancePx);
                sumDistancePx += distancePx;

                // mm로 변환하여 저장
                double distanceMm = distancePx * pixelToMm;
                pointDistancesMm.append(distanceMm);

                // 최대 허용 거리와 비교
                if (distanceMm > pattern.edgeDistanceMax)
                {
                    edgeOutlierCount++;
                }
            }

            // 편차를 mm로 변환
            double avgDistancePx = sumDistancePx / absoluteEdgePoints.size();
            edgeMaxDeviationMm = maxDistancePx * pixelToMm;
            edgeMinDeviationMm = minDistancePx * pixelToMm;
            edgeAvgDeviationMm = avgDistancePx * pixelToMm;

            // 각 포인트별 거리와 선형 회귀 정보 저장
            result.edgePointDistances[pattern.id] = pointDistancesMm;
            result.edgeRegressionSlope[pattern.id] = m;
            result.edgeRegressionIntercept[pattern.id] = b;
        }

        // EDGE 불량 판정: edgeOutlierCount가 edgeMaxOutliers 이상이면 불량
        edgePassed = (edgeOutlierCount < pattern.edgeMaxOutliers);

        // EDGE 검사 결과 저장 (불량수와 편차는 mm 기준)
        result.edgeResults[pattern.id] = edgePassed;
        result.edgeIrregularityCount[pattern.id] = edgeOutlierCount;
        result.edgeMaxDeviation[pattern.id] = edgeMaxDeviationMm;
        result.edgeMinDeviation[pattern.id] = edgeMinDeviationMm;
        result.edgeAvgDeviation[pattern.id] = edgeAvgDeviationMm;

        result.edgeMeasured[pattern.id] = pattern.edgeEnabled;
        result.edgeAverageX[pattern.id] = edgeAvgX; // 절대 좌표 평균

        // Qt로 시각화 추가 (시작점, 끝점, Local Max Gradient 지점들)
        if (!resultImage.empty())
        {
            // 결과 이미지를 QImage로 변환
            QImage qResultImage = matToQImage(resultImage);
            QPainter painter(&qResultImage);
            painter.setRenderHint(QPainter::Antialiasing);

            int verticalHeight = 15;

            // 가장 오른쪽 Local Max Gradient 지점만 표시 (빨간색 세로선)
            if (!gradientPoints.empty())
            {
                // gradientPoints에서 X좌표가 가장 큰(오른쪽) 지점 찾기
                cv::Point rightmostPoint = gradientPoints[0];
                for (const cv::Point &gradPoint : gradientPoints)
                {
                    if (gradPoint.x > rightmostPoint.x)
                    {
                        rightmostPoint = gradPoint;
                    }
                }

                // 실제로는 maxGradientPoint를 사용
                cv::Point actualMaxGradient = maxGradientPoint;

                // 두 점의 중심점 계산
                cv::Point centerPoint;
                centerPoint.x = (startPoint.x + actualMaxGradient.x) / 2;
                centerPoint.y = (startPoint.y + actualMaxGradient.y) / 2;

                // 패턴 각도를 라디안으로 변환
                double angleRad = pattern.angle * CV_PI / 180.0;
                double cosA = cos(angleRad);
                double sinA = sin(angleRad);

                // 중심점 기준으로 시작점 회전
                float relX1 = startPoint.x - centerPoint.x;
                float relY1 = startPoint.y - centerPoint.y;
                cv::Point rotatedStart;
                rotatedStart.x = static_cast<int>(relX1 * cosA - relY1 * sinA + centerPoint.x);
                rotatedStart.y = static_cast<int>(relX1 * sinA + relY1 * cosA + centerPoint.y);

                // 중심점 기준으로 Max Gradient 지점 회전
                float relX2 = actualMaxGradient.x - centerPoint.x;
                float relY2 = actualMaxGradient.y - centerPoint.y;
                cv::Point rotatedMaxGrad;
                rotatedMaxGrad.x = static_cast<int>(relX2 * cosA - relY2 * sinA + centerPoint.x);
                rotatedMaxGrad.y = static_cast<int>(relX2 * sinA + relY2 * cosA + centerPoint.y);

                // 회전된 세로선 방향 벡터 계산 (수직 방향을 각도만큼 회전)
                int perpX = static_cast<int>(-sinA * 20 / 2);
                int perpY = static_cast<int>(cosA * 20 / 2);

                // 회전된 시작점에 파란색 세로선
                painter.setPen(QPen(QColor(0, 0, 255), 3)); // 파란색
                painter.drawLine(rotatedStart.x - perpX, rotatedStart.y - perpY,
                                 rotatedStart.x + perpX, rotatedStart.y + perpY);

                // 회전된 Max Gradient 지점에 빨간색 세로선
                painter.setPen(QPen(QColor(255, 0, 0), 3)); // 빨간색
                painter.drawLine(rotatedMaxGrad.x - perpX, rotatedMaxGrad.y - perpY,
                                 rotatedMaxGrad.x + perpX, rotatedMaxGrad.y + perpY);

                // 회전된 두 점을 초록색 선으로 연결 (길이) - 상단 컨투어 (주석 처리)
                // painter.setPen(QPen(QColor(0, 255, 0), 3)); // 초록색 선
                // painter.drawLine(rotatedStart.x, rotatedStart.y,
                //                rotatedMaxGrad.x, rotatedMaxGrad.y);

                // 하단 컨투어의 max gradient 찾기 및 연결선 그리기
                if (gradientPoints.size() > 747)
                { // 상단 + 하단이 있다면
                    // gradientPoints의 후반부 (하단 컨투어)에서 Y가 가장 큰 점 찾기
                    cv::Point bottomMaxGrad = gradientPoints[747]; // 하단 컨투어 시작점
                    for (size_t i = 747; i < gradientPoints.size(); i++)
                    {
                        if (gradientPoints[i].y > bottomMaxGrad.y)
                        {
                            bottomMaxGrad = gradientPoints[i];
                        }
                    }

                    // 하단 max gradient 점도 회전 적용
                    float relX3 = bottomMaxGrad.x - centerPoint.x;
                    float relY3 = bottomMaxGrad.y - centerPoint.y;
                    cv::Point rotatedBottomMaxGrad;
                    rotatedBottomMaxGrad.x = static_cast<int>(relX3 * cosA - relY3 * sinA + centerPoint.x);
                    rotatedBottomMaxGrad.y = static_cast<int>(relX3 * sinA + relY3 * cosA + centerPoint.y);
                }

                // 중간 지점에 길이 표시 (실제 거리 계산)
                double dx = rotatedMaxGrad.x - rotatedStart.x;
                double dy = rotatedMaxGrad.y - rotatedStart.y;
                int pixelDistance = static_cast<int>(sqrt(dx * dx + dy * dy));
                int midX = (rotatedStart.x + rotatedMaxGrad.x) / 2;
                int midY = (rotatedStart.y + rotatedMaxGrad.y) / 2 - 20; // 선 위쪽에 표시

                painter.setPen(QPen(QColor(0, 255, 0), 1)); // 초록색 텍스트
                painter.setFont(QFont("Arial", 12, QFont::Bold));
                QString distanceText = QString("길이: %1mm").arg(pixelDistance);
                painter.drawText(midX - 35, midY, distanceText);

                // 두께 측정 시각화 (Max Gradient 지점 기준 좌우 100px 위치에서)
                if (leftThickness > 0 || rightThickness > 0)
                {
                    // 길이 방향 벡터 계산
                    double lengthVecX = actualMaxGradient.x - startPoint.x;
                    double lengthVecY = actualMaxGradient.y - startPoint.y;
                    double lengthMag = sqrt(lengthVecX * lengthVecX + lengthVecY * lengthVecY);

                    if (lengthMag > 0)
                    {
                        // 정규화된 길이 방향 벡터
                        double normLengthX = lengthVecX / lengthMag;
                        double normLengthY = lengthVecY / lengthMag;

                        // 두께 측정을 위한 수직 방향 벡터 (FID 각도 적용)
                        double perpX, perpY;

                        // FID 각도가 있으면 적용, 없으면 길이 방향에 수직으로 계산
                        if (std::abs(pattern.angle) > 0.1)
                        {
                            // FID 각도를 라디안으로 변환
                            double angleRad = pattern.angle * CV_PI / 180.0;
                            // FID 각도에서 90도 회전한 방향 (두께 측정 방향)
                            double thicknessAngle = angleRad + CV_PI / 2;
                            perpX = cos(thicknessAngle);
                            perpY = sin(thicknessAngle);
                        }
                        else
                        {
                            // FID 각도가 없으면 길이 방향에 수직
                            perpX = -normLengthY;
                            perpY = normLengthX;
                        }

                        // 좌측 측정 위치 (Max Gradient에서 좌측으로 100px)
                        int leftMeasureX = static_cast<int>(actualMaxGradient.x - normLengthX * 100);
                        int leftMeasureY = static_cast<int>(actualMaxGradient.y - normLengthY * 100);

                        // 우측 측정 위치 (Max Gradient에서 우측으로 100px)
                        int rightMeasureX = static_cast<int>(actualMaxGradient.x + normLengthX * 100);
                        int rightMeasureY = static_cast<int>(actualMaxGradient.y + normLengthY * 100);

                        // 실제 두께 측정 (ROI 이미지에서 직접 측정)
                        cv::Mat grayForMeasure;
                        if (roiImage.channels() == 3)
                        {
                            cv::cvtColor(roiImage, grayForMeasure, cv::COLOR_BGR2GRAY);
                        }
                        else
                        {
                            roiImage.copyTo(grayForMeasure);
                        }

                        const int maxSearchDistance = 100;
                        const int thresholdDiff = 30;

                        // 좌측 위치에서 두께 측정 (20픽셀 범위로 평균 계산)
                        int actualLeftThickness = 0;

                        if (leftMeasureX >= 10 && leftMeasureX < grayForMeasure.cols - 10 &&
                            leftMeasureY >= 0 && leftMeasureY < grayForMeasure.rows)
                        {

                            std::vector<int> thicknessMeasurements;
                            std::vector<std::pair<cv::Point, cv::Point>> thicknessPoints; // 각 측정의 상하 끝점들

                            // 좌우 10픽셀씩, 총 20픽셀 범위에서 측정
                            for (int offsetX = -10; offsetX <= 10; offsetX++)
                            {
                                int measureX = leftMeasureX + offsetX;
                                int measureY = leftMeasureY;

                                if (measureX < 0 || measureX >= grayForMeasure.cols)
                                    continue;

                                int centerIntensity = grayForMeasure.at<uchar>(measureY, measureX);

                                // 위아래로 스캔해서 검은색을 만날 때까지 픽셀 수 계산
                                int upThickness = 0, downThickness = 0;
                                cv::Point topPoint(measureX, measureY);
                                cv::Point bottomPoint(measureX, measureY);

                                // 위쪽으로 탐색
                                for (int i = 1; i <= maxSearchDistance; i++)
                                {
                                    int searchX = measureX + static_cast<int>(perpX * (-i));
                                    int searchY = measureY + static_cast<int>(perpY * (-i));

                                    if (searchX < 0 || searchX >= grayForMeasure.cols ||
                                        searchY < 0 || searchY >= grayForMeasure.rows)
                                    {
                                        break;
                                    }

                                    int intensity = grayForMeasure.at<uchar>(searchY, searchX);
                                    if (abs(intensity - centerIntensity) > thresholdDiff)
                                    {
                                        upThickness = i;
                                        topPoint = cv::Point(searchX, searchY);
                                        break;
                                    }
                                }

                                // 아래쪽으로 탐색
                                for (int i = 1; i <= maxSearchDistance; i++)
                                {
                                    int searchX = measureX + static_cast<int>(perpX * i);
                                    int searchY = measureY + static_cast<int>(perpY * i);

                                    if (searchX < 0 || searchX >= grayForMeasure.cols ||
                                        searchY < 0 || searchY >= grayForMeasure.rows)
                                    {
                                        break;
                                    }

                                    int intensity = grayForMeasure.at<uchar>(searchY, searchX);
                                    if (abs(intensity - centerIntensity) > thresholdDiff)
                                    {
                                        downThickness = i;
                                        bottomPoint = cv::Point(searchX, searchY);
                                        break;
                                    }
                                }

                                int totalThickness = upThickness + downThickness;
                                if (totalThickness > 0)
                                {
                                    thicknessMeasurements.push_back(totalThickness);
                                    thicknessPoints.push_back(std::make_pair(topPoint, bottomPoint));
                                }
                            }

                            // 평균 두께 계산 (이상치 제거)
                            if (!thicknessMeasurements.empty())
                            {
                                // 정렬된 인덱스 생성
                                std::vector<std::pair<int, size_t>> indexedMeasurements;
                                for (size_t i = 0; i < thicknessMeasurements.size(); i++)
                                {
                                    indexedMeasurements.push_back({thicknessMeasurements[i], i});
                                }
                                std::sort(indexedMeasurements.begin(), indexedMeasurements.end());

                                int removeCount = static_cast<int>(indexedMeasurements.size() * 0.2);
                                int startIdx = removeCount;
                                int endIdx = indexedMeasurements.size() - removeCount;

                                if (endIdx > startIdx)
                                {
                                    int sum = 0;
                                    cv::Point avgTop(0, 0), avgBottom(0, 0);
                                    int validCount = 0;

                                    for (int i = startIdx; i < endIdx; i++)
                                    {
                                        size_t originalIdx = indexedMeasurements[i].second;
                                        sum += indexedMeasurements[i].first;
                                        avgTop.x += thicknessPoints[originalIdx].first.x;
                                        avgTop.y += thicknessPoints[originalIdx].first.y;
                                        avgBottom.x += thicknessPoints[originalIdx].second.x;
                                        avgBottom.y += thicknessPoints[originalIdx].second.y;
                                        validCount++;
                                    }
                                    actualLeftThickness = sum / validCount;
                                    leftTopPoint = cv::Point(avgTop.x / validCount, avgTop.y / validCount);
                                    leftBottomPoint = cv::Point(avgBottom.x / validCount, avgBottom.y / validCount);
                                }
                                else
                                {
                                    // 데이터가 적으면 단순 평균
                                    int sum = std::accumulate(thicknessMeasurements.begin(), thicknessMeasurements.end(), 0);
                                    actualLeftThickness = sum / thicknessMeasurements.size();

                                    cv::Point avgTop(0, 0), avgBottom(0, 0);
                                    for (const auto &points : thicknessPoints)
                                    {
                                        avgTop.x += points.first.x;
                                        avgTop.y += points.first.y;
                                        avgBottom.x += points.second.x;
                                        avgBottom.y += points.second.y;
                                    }
                                    leftTopPoint = cv::Point(avgTop.x / thicknessPoints.size(), avgTop.y / thicknessPoints.size());
                                    leftBottomPoint = cv::Point(avgBottom.x / thicknessPoints.size(), avgBottom.y / thicknessPoints.size());
                                }
                            }
                        }

                        // 우측 위치에서 두께 측정 (20픽셀 범위로 평균 계산)
                        int actualRightThickness = 0;

                        if (rightMeasureX >= 10 && rightMeasureX < grayForMeasure.cols - 10 &&
                            rightMeasureY >= 0 && rightMeasureY < grayForMeasure.rows)
                        {

                            std::vector<int> thicknessMeasurements;
                            std::vector<std::pair<cv::Point, cv::Point>> thicknessPoints; // 각 측정의 상하 끝점들

                            // 좌우 10픽셀씩, 총 20픽셀 범위에서 측정
                            for (int offsetX = -10; offsetX <= 10; offsetX++)
                            {
                                int measureX = rightMeasureX + offsetX;
                                int measureY = rightMeasureY;

                                if (measureX < 0 || measureX >= grayForMeasure.cols)
                                    continue;

                                int centerIntensity = grayForMeasure.at<uchar>(measureY, measureX);

                                // 위아래로 스캔해서 검은색을 만날 때까지 픽셀 수 계산
                                int upThickness = 0, downThickness = 0;
                                cv::Point topPoint(measureX, measureY);
                                cv::Point bottomPoint(measureX, measureY);

                                // 위쪽으로 탐색
                                for (int i = 1; i <= maxSearchDistance; i++)
                                {
                                    int searchX = measureX + static_cast<int>(perpX * (-i));
                                    int searchY = measureY + static_cast<int>(perpY * (-i));

                                    if (searchX < 0 || searchX >= grayForMeasure.cols ||
                                        searchY < 0 || searchY >= grayForMeasure.rows)
                                    {
                                        break;
                                    }

                                    int intensity = grayForMeasure.at<uchar>(searchY, searchX);
                                    if (abs(intensity - centerIntensity) > thresholdDiff)
                                    {
                                        upThickness = i;
                                        topPoint = cv::Point(searchX, searchY);
                                        break;
                                    }
                                }

                                // 아래쪽으로 탐색
                                for (int i = 1; i <= maxSearchDistance; i++)
                                {
                                    int searchX = measureX + static_cast<int>(perpX * i);
                                    int searchY = measureY + static_cast<int>(perpY * i);

                                    if (searchX < 0 || searchX >= grayForMeasure.cols ||
                                        searchY < 0 || searchY >= grayForMeasure.rows)
                                    {
                                        break;
                                    }

                                    int intensity = grayForMeasure.at<uchar>(searchY, searchX);
                                    if (abs(intensity - centerIntensity) > thresholdDiff)
                                    {
                                        downThickness = i;
                                        bottomPoint = cv::Point(searchX, searchY);
                                        break;
                                    }
                                }

                                int totalThickness = upThickness + downThickness;
                                if (totalThickness > 0)
                                {
                                    thicknessMeasurements.push_back(totalThickness);
                                    thicknessPoints.push_back(std::make_pair(topPoint, bottomPoint));
                                }
                            }

                            // 평균 두께 계산 (이상치 제거)
                            if (!thicknessMeasurements.empty())
                            {
                                // 정렬된 인덱스 생성
                                std::vector<std::pair<int, size_t>> indexedMeasurements;
                                for (size_t i = 0; i < thicknessMeasurements.size(); i++)
                                {
                                    indexedMeasurements.push_back({thicknessMeasurements[i], i});
                                }
                                std::sort(indexedMeasurements.begin(), indexedMeasurements.end());

                                int removeCount = static_cast<int>(indexedMeasurements.size() * 0.2);
                                int startIdx = removeCount;
                                int endIdx = indexedMeasurements.size() - removeCount;

                                if (endIdx > startIdx)
                                {
                                    int sum = 0;
                                    cv::Point avgTop(0, 0), avgBottom(0, 0);
                                    int validCount = 0;

                                    for (int i = startIdx; i < endIdx; i++)
                                    {
                                        size_t originalIdx = indexedMeasurements[i].second;
                                        sum += indexedMeasurements[i].first;
                                        avgTop.x += thicknessPoints[originalIdx].first.x;
                                        avgTop.y += thicknessPoints[originalIdx].first.y;
                                        avgBottom.x += thicknessPoints[originalIdx].second.x;
                                        avgBottom.y += thicknessPoints[originalIdx].second.y;
                                        validCount++;
                                    }
                                    actualRightThickness = sum / validCount;
                                    rightTopPoint = cv::Point(avgTop.x / validCount, avgTop.y / validCount);
                                    rightBottomPoint = cv::Point(avgBottom.x / validCount, avgBottom.y / validCount);
                                }
                                else
                                {
                                    // 데이터가 적으면 단순 평균
                                    int sum = std::accumulate(thicknessMeasurements.begin(), thicknessMeasurements.end(), 0);
                                    actualRightThickness = sum / thicknessMeasurements.size();

                                    cv::Point avgTop(0, 0), avgBottom(0, 0);
                                    for (const auto &points : thicknessPoints)
                                    {
                                        avgTop.x += points.first.x;
                                        avgTop.y += points.first.y;
                                        avgBottom.x += points.second.x;
                                        avgBottom.y += points.second.y;
                                    }
                                    rightTopPoint = cv::Point(avgTop.x / thicknessPoints.size(), avgTop.y / thicknessPoints.size());
                                    rightBottomPoint = cv::Point(avgBottom.x / thicknessPoints.size(), avgBottom.y / thicknessPoints.size());
                                }
                            }
                        }

                        // 좌측 두께 표시 (보라색) - 측정된 실제 두께와 상하 끝점으로 표시
                        if (actualLeftThickness > 0)
                        {
                            painter.setPen(QPen(QColor(128, 0, 128), 3)); // 보라색
                            painter.drawLine(leftTopPoint.x, leftTopPoint.y, leftBottomPoint.x, leftBottomPoint.y);

                            // 상하 끝점에 작은 원 표시
                            painter.setPen(QPen(QColor(128, 0, 128), 2));
                            painter.drawEllipse(leftTopPoint.x - 2, leftTopPoint.y - 2, 4, 4);
                            painter.drawEllipse(leftBottomPoint.x - 2, leftBottomPoint.y - 2, 4, 4);

                            // 좌측 두께 텍스트
                            int leftTextX = leftMeasureX + 10;
                            int leftTextY = leftMeasureY - 10;
                            painter.setPen(QPen(QColor(128, 0, 128), 1));
                            painter.setFont(QFont("Arial", 10, QFont::Bold));
                            painter.drawText(leftTextX, leftTextY, QString("좌: %1px").arg(actualLeftThickness));
                        }

                        // 우측 두께 표시 (주황색) - 측정된 실제 두께와 상하 끝점으로 표시
                        if (actualRightThickness > 0)
                        {
                            painter.setPen(QPen(QColor(255, 165, 0), 3)); // 주황색
                            painter.drawLine(rightTopPoint.x, rightTopPoint.y, rightBottomPoint.x, rightBottomPoint.y);

                            // 상하 끝점에 작은 원 표시
                            painter.setPen(QPen(QColor(255, 165, 0), 2));
                            painter.drawEllipse(rightTopPoint.x - 2, rightTopPoint.y - 2, 4, 4);
                            painter.drawEllipse(rightBottomPoint.x - 2, rightBottomPoint.y - 2, 4, 4);

                            // 우측 두께 텍스트
                            int rightTextX = rightMeasureX + 10;
                            int rightTextY = rightMeasureY - 10;
                            painter.setPen(QPen(QColor(255, 165, 0), 1));
                            painter.setFont(QFont("Arial", 10, QFont::Bold));
                            painter.drawText(rightTextX, rightTextY, QString("우: %1px").arg(actualRightThickness));
                        }

                        // 실제 측정값으로 업데이트
                        leftThickness = actualLeftThickness;
                        rightThickness = actualRightThickness;
                    }
                }

                // 측정된 두께 정보를 검사 결과에 추가하여 Qt로 표시
                if (result.stripMeasuredThicknessAvg.contains(pattern.id) &&
                    result.stripThicknessMeasured[pattern.id])
                {

                    int measuredMin = result.stripMeasuredThicknessMin[pattern.id];
                    int measuredMax = result.stripMeasuredThicknessMax[pattern.id];
                    int measuredAvg = result.stripMeasuredThicknessAvg[pattern.id];

                    // 두께 정보를 오른쪽 상단에 표시
                    int textX = qResultImage.width() - 200;
                    int textY = 30;

                    // 배경 박스 그리기
                    painter.fillRect(textX - 10, textY - 20, 190, 70, QColor(0, 0, 0, 180));

                    // 두께 설정 범위 표시
                    painter.setPen(QPen(QColor(255, 255, 255), 1));
                    painter.setFont(QFont("Arial", 10, QFont::Bold));
                    QString rangeText = QString("설정: %1~%2px").arg(pattern.stripThicknessMin).arg(pattern.stripThicknessMax);
                    painter.drawText(textX, textY, rangeText);

                    // 측정된 두께 표시 (범위 내/외에 따라 색상 변경)
                    bool isInRange = (measuredAvg >= pattern.stripThicknessMin && measuredAvg <= pattern.stripThicknessMax);
                    painter.setPen(QPen(isInRange ? QColor(0, 255, 0) : QColor(255, 0, 0), 1));
                    QString measureText = QString("측정: %1~%2(%3)px").arg(measuredMin).arg(measuredMax).arg(measuredAvg);
                    painter.drawText(textX, textY + 20, measureText);

                    // 판정 결과 표시
                    QString resultText = isInRange ? "PASS" : "FAIL";
                    painter.drawText(textX, textY + 40, QString("판정: %1").arg(resultText));
                }
            }

            // QImage를 다시 cv::Mat으로 변환
            QImage rgbImage = qResultImage.convertToFormat(QImage::Format_RGB888);
            resultImage = cv::Mat(rgbImage.height(), rgbImage.width(), CV_8UC3,
                                  (void *)rgbImage.constBits(), rgbImage.bytesPerLine())
                              .clone();
            cv::cvtColor(resultImage, resultImage, cv::COLOR_RGB2BGR);
        }

        // 결과 저장
        result.insProcessedImages[pattern.id] = resultImage.clone();
        result.insMethodTypes[pattern.id] = InspectionMethod::STRIP;
        result.insScores[pattern.id] = score;
        result.insResults[pattern.id] = allTestsPassed; // 모든 검사 통과 여부

        // 두께 측정 결과 저장
        if (leftThickness > 0 || rightThickness > 0)
        {
            result.stripThicknessCenters[pattern.id] = maxGradientPoint;

            // 실제 두께 측정 위치 계산 (Max Gradient 기준 좌우 100px)
            double lengthVecX = maxGradientPoint.x - startPoint.x;
            double lengthVecY = maxGradientPoint.y - startPoint.y;
            double lengthMag = sqrt(lengthVecX * lengthVecX + lengthVecY * lengthVecY);

            if (lengthMag > 0)
            {
                // 정규화된 길이 방향 벡터
                double normLengthX = lengthVecX / lengthMag;
                double normLengthY = lengthVecY / lengthMag;

                // 좌측 측정 중심점 (Max Gradient에서 좌측으로 100px)
                cv::Point leftCenter(
                    static_cast<int>(maxGradientPoint.x - normLengthX * 100),
                    static_cast<int>(maxGradientPoint.y - normLengthY * 100));

                // 우측 측정 중심점 (Max Gradient에서 우측으로 100px)
                cv::Point rightCenter(
                    static_cast<int>(maxGradientPoint.x + normLengthX * 100),
                    static_cast<int>(maxGradientPoint.y + normLengthY * 100));

                result.stripThicknessLines[pattern.id] = std::make_pair(leftCenter, rightCenter);

                // 상세 두께 측정 좌표 저장 (좌측 상하점, 우측 상하점)
                std::vector<std::pair<cv::Point, cv::Point>> thicknessDetails;
                if (leftThickness > 0)
                {
                    thicknessDetails.push_back(std::make_pair(leftTopPoint, leftBottomPoint));
                }
                if (rightThickness > 0)
                {
                    thicknessDetails.push_back(std::make_pair(rightTopPoint, rightBottomPoint));
                }
                result.stripThicknessDetails[pattern.id] = thicknessDetails;
            }
        }

        // STRIP 검사 결과 로그 출력
        double pixelToMm = pattern.stripLengthConversionMm / pattern.stripLengthCalibrationPx;

        // EDGE 검사 결과
        QString edgeResult = edgeTestPassed ? "PASS" : "NG";
        QString edgeDetail = QString("Max:%1 Avg:%2 [%3/%4]")
                                 .arg(QString::number(edgeMaxDeviationMm, 'f', 2))
                                 .arg(QString::number(edgeAvgDeviationMm, 'f', 2))
                                 .arg(QString::number(edgeOutlierCount))
                                 .arg(QString::number(pattern.edgeMaxOutliers));

        // FRONT 두께 검사 결과
        QString frontResult = frontThicknessPassed ? "PASS" : "NG";
        QString frontDetail = "";
        if (measuredAvgThickness > 0)
        {
            double avgMm = measuredAvgThickness * pixelToMm;
            frontDetail = QString("[%1/%2-%3]").arg(QString::number(avgMm, 'f', 2)).arg(QString::number(pattern.stripThicknessMin, 'f', 2)).arg(QString::number(pattern.stripThicknessMax, 'f', 2));
        }

        // REAR 두께 검사 결과
        QString rearResult = rearThicknessPassed ? "PASS" : "NG";
        QString rearDetail = "";
        if (rearMeasuredAvgThickness > 0)
        {
            double avgMm = rearMeasuredAvgThickness * pixelToMm;
            rearDetail = QString("[%1/%2-%3]").arg(QString::number(avgMm, 'f', 2)).arg(QString::number(pattern.stripRearThicknessMin, 'f', 2)).arg(QString::number(pattern.stripRearThicknessMax, 'f', 2));
        }

        // STRIP LENGTH 검사 결과 - 로그는 result에 저장하고 나중에 출력
        result.stripLengthResult = stripLengthPassed ? "PASS" : "NG";
        if (stripMeasuredLength > 0)
        {
            result.stripLengthDetail = QString("[%1/%2-%3]").arg(QString::number(stripMeasuredLength, 'f', 2)).arg(QString::number(pattern.stripLengthMin, 'f', 2)).arg(QString::number(pattern.stripLengthMax, 'f', 2));
        }

        result.frontResult = frontResult;
        result.frontDetail = frontDetail;
        result.rearResult = rearResult;
        result.rearDetail = rearDetail;
        result.edgeResult = edgeResult;
        result.edgeDetail = edgeDetail;
        result.stripPatternName = pattern.name;

        return allTestsPassed; // 모든 검사 통과 여부 반환
    }
    catch (const cv::Exception &e)
    {
        logDebug(QString("STRIP 길이 검사 중 OpenCV 예외 발생 - %1: %2").arg(pattern.name).arg(e.what()));
        score = 0.0;
        result.insMethodTypes[pattern.id] = InspectionMethod::STRIP;
        return false;
    }
    catch (...)
    {
        logDebug(QString("STRIP 길이 검사 중 알 수 없는 예외 발생 - %1").arg(pattern.name));
        score = 0.0;
        result.insMethodTypes[pattern.id] = InspectionMethod::STRIP;
        return false;
    }
}

// ===== CRIMP 검사는 현재 비활성화됨 =====
bool InsProcessor::checkCrimp(const cv::Mat &image, const PatternInfo &pattern, double &score, InspectionResult &result, const QList<PatternInfo>& patterns)
{
    result.insMethodTypes[pattern.id] = InspectionMethod::CRIMP;
    score = 0.0;  // 기본 점수 (0-1 범위)
    
    // CRIMP 검사는 현재 비활성화 (YOLO 모델 제거됨)
    qDebug() << "[CRIMP] CRIMP 검사는 현재 지원되지 않습니다 (YOLO 모델 제거됨)";
    return false;
}
//     
