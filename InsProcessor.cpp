#include "InsProcessor.h"
#include "ImageProcessor.h"
#include <QDebug>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QPen>
#include <QFontMetrics>
#include <numeric>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp> 

InsProcessor::InsProcessor(QObject* parent) : QObject(parent) {
    logDebug("InsProcessor 초기화됨");
}

InsProcessor::~InsProcessor() {
    logDebug("InsProcessor 소멸됨");
}

InspectionResult InsProcessor::performInspection(const cv::Mat& image, const QList<PatternInfo>& patterns) {
    InspectionResult result;
    
    if (image.empty() || patterns.isEmpty()) {
        logDebug("검사 실패: 이미지가 비어있거나 패턴이 없음");
        return result;
    }
    
    logDebug(QString("검사 시작: %1개 패턴").arg(patterns.size()));
    result.isPassed = true;
    
    // 1. 활성화된 패턴들을 유형별로 분류
    QList<PatternInfo> roiPatterns, fidPatterns, insPatterns;
    for (const PatternInfo& pattern : patterns) {
        if (!pattern.enabled) continue;
        
        switch (pattern.type) {
            case PatternType::ROI:
                roiPatterns.append(pattern);
                logDebug(QString("활성화된 ROI 패턴 발견: '%1'").arg(pattern.name));
                break;
                
            case PatternType::FID:
                fidPatterns.append(pattern);
                logDebug(QString("활성화된 FID 패턴 발견: '%1'").arg(pattern.name));
                break;
                
            case PatternType::INS:
                insPatterns.append(pattern);
                logDebug(QString("활성화된 INS 패턴 발견: '%1'").arg(pattern.name));
                break;
                
            case PatternType::FIL:
                logDebug(QString("필터 패턴 발견 (무시됨): '%1'").arg(pattern.name));
                break;
        }
    }
    
    // 2. ROI 그룹 분석 및 매핑
    QMap<QUuid, QUuid> patternToRoiMap; // 패턴 ID -> 속한 ROI ID 매핑
    QMap<QUuid, QRect> roiGroupAreas; // ROI ID -> ROI 영역 매핑
    QList<QRect> activeRoiRects; // 전체 검사 영역 목록 (기존 호환성)
    
    if (roiPatterns.isEmpty()) {
        // ROI가 없으면 전체 이미지 영역을 활성 영역으로 간주
        activeRoiRects.append(QRect(0, 0, image.cols, image.rows));
        logDebug("활성화된 ROI 패턴이 없음, 전체 영역 검사");
    } else {
        // ROI 기반 그룹 분석
        for (const PatternInfo& roiPattern : roiPatterns) {
            activeRoiRects.append(QRect(
                static_cast<int>(roiPattern.rect.x()),
                static_cast<int>(roiPattern.rect.y()),
                static_cast<int>(roiPattern.rect.width()),
                static_cast<int>(roiPattern.rect.height())
            ));
            roiGroupAreas[roiPattern.id] = QRect(
                static_cast<int>(roiPattern.rect.x()),
                static_cast<int>(roiPattern.rect.y()),
                static_cast<int>(roiPattern.rect.width()),
                static_cast<int>(roiPattern.rect.height())
            );
            
            logDebug(QString("ROI 패턴 '%1': 검사 영역 (%2,%3,%4,%5) 추가")
                    .arg(roiPattern.name)
                    .arg(static_cast<int>(roiPattern.rect.x()))
                    .arg(static_cast<int>(roiPattern.rect.y()))
                    .arg(roiPattern.rect.width())
                    .arg(roiPattern.rect.height()));
            
            // ROI의 직접 자식들 (FID들) 매핑
            for (const QUuid& childId : roiPattern.childIds) {
                patternToRoiMap[childId] = roiPattern.id;
                
                // FID의 자식들 (INS들)도 같은 ROI에 매핑
                for (const PatternInfo& fidPattern : fidPatterns) {
                    if (fidPattern.id == childId) {
                        for (const QUuid& insId : fidPattern.childIds) {
                            patternToRoiMap[insId] = roiPattern.id;
                        }
                        break;
                    }
                }
            }
        }
    }
    
    // 3. 패턴이 특정 ROI 그룹 내에 있는지 확인하는 헬퍼 함수
    auto isInGroupROI = [&patternToRoiMap, &roiGroupAreas, &roiPatterns, this](const QUuid& patternId, const QPoint& center) -> bool {
        // ROI가 없으면 항상 true 반환 (기존 동작 유지)
        if (roiPatterns.isEmpty()) return true;
        
        // 패턴이 특정 ROI 그룹에 속하는지 확인
        if (patternToRoiMap.contains(patternId)) {
            QUuid roiId = patternToRoiMap[patternId];
            if (roiGroupAreas.contains(roiId)) {
                QRect roiRect = roiGroupAreas[roiId];
                bool inGroup = roiRect.contains(center);
                
                if (!inGroup) {
                    logDebug(QString("패턴 '%1': 그룹 ROI 영역 외부에 위치하여 검사 제외")
                            .arg(patternId.toString().left(8)));
                }
                
                return inGroup;
            }
        }
        
        // 그룹에 속하지 않은 독립 패턴인 경우 모든 ROI 영역에서 검사 가능
        for (const QRect& roiRect : roiGroupAreas.values()) {
            if (roiRect.contains(center)) return true;
        }
        
        return false;
    };
    
    // 4. 기존 ROI 영역 확인 함수 (기존 코드 호환성 유지)
    auto isInROI = [&activeRoiRects, &roiPatterns](const QPoint& center) -> bool {
        if (roiPatterns.isEmpty()) return true;
        
        for (const QRect& roiRect : activeRoiRects) {
            if (roiRect.contains(center)) return true;
        }
        
        return false;
    };
    
    // 5. FID 패턴 매칭 수행 (그룹 ROI 제한 적용)
    if (!fidPatterns.isEmpty()) {
        logDebug(QString("FID 패턴 매칭 처리 중: %1개").arg(fidPatterns.size()));
        
        for (const PatternInfo& pattern : fidPatterns) {
            // 매칭 검사가 활성화된 경우에만 수행
            if (!pattern.runInspection) {
                logDebug(QString("FID 패턴 '%1': 검사 비활성화됨, 건너뜀").arg(pattern.name));
                continue;
            }
            
            // **중요**: 그룹 ROI 영역 내에 있는지 확인
            QPoint patternCenter = QPoint(
                static_cast<int>(pattern.rect.center().x()),
                static_cast<int>(pattern.rect.center().y())
            );
            if (!isInGroupROI(pattern.id, patternCenter)) {
                logDebug(QString("FID 패턴 '%1': 그룹 ROI 영역 외부에 있어 검사에서 제외됨").arg(pattern.name));
                continue;
            }
            
            // 기존 ROI 검사도 유지 (하위 호환성)
            if (!isInROI(patternCenter)) {
                logDebug(QString("FID 패턴 '%1': ROI 영역 외부에 있어 검사에서 제외됨").arg(pattern.name));
                continue;
            }
            
            double matchScore = 0.0;
            cv::Point matchLoc;
            double matchAngle = 0.0;

            // **중요**: matchFiducial에 그룹 ROI 정보도 전달
            bool fidMatched = matchFiducial(image, pattern, matchScore, matchLoc, matchAngle, patterns);
            
            // 매칭 성공 시 검출된 각도를 FID 그룹 전체에 적용
            if (fidMatched) {
                logDebug(QString("FID 패턴 '%1' 매칭 성공 - 검출 각도: %2°")
                        .arg(pattern.name).arg(matchAngle, 0, 'f', 2));
                
                // FID 패턴의 각도를 검출된 각도로 업데이트
                double oldAngle = pattern.angle;
                const_cast<PatternInfo&>(pattern).angle = matchAngle;
                result.angles[pattern.id] = matchAngle;
                
                logDebug(QString("FID 패턴 '%1' 각도 업데이트: %2° → %3°")
                        .arg(pattern.name).arg(oldAngle).arg(matchAngle));
                
        // 주의: 자식 INS의 티칭 각도는 변경하지 않습니다.
        // INS들은 검사 시점에 FID의 검출 각도 차이(fidAngle - fidTeach)를
        // 티칭 각도에 더해 최종 각도를 계산하도록 처리됩니다.
        logDebug(QString("FID '%1' 검출 각도 적용됨 - 자식 INS의 티칭 각도는 유지됨(검사 시 조정됨)")
            .arg(pattern.name));
            } else {
                // 매칭 실패 시에도 검출된 각도는 적용
                logDebug(QString("FID 패턴 '%1' 매칭 실패 - 하지만 검출 각도는 적용: %2°")
                        .arg(pattern.name).arg(matchAngle, 0, 'f', 2));
                
                // FID 패턴의 각도를 검출된 각도로 업데이트 (매칭 실패와 무관)
                double oldAngle = pattern.angle;
                const_cast<PatternInfo&>(pattern).angle = matchAngle;
                result.angles[pattern.id] = matchAngle;
                
                logDebug(QString("FID 패턴 '%1' 각도 업데이트: %2° → %3°")
                        .arg(pattern.name).arg(oldAngle).arg(matchAngle));
                
        // 마찬가지로 매칭 실패 시에도 자식 INS의 티칭 각도는 유지합니다.
        logDebug(QString("FID '%1' (실패) - 검출 각도 적용됨, 자식 INS의 티칭 각도는 유지됨")
            .arg(pattern.name));
            }
            
            // 결과 기록
            result.fidResults[pattern.id] = fidMatched;
            result.matchScores[pattern.id] = matchScore;
            result.locations[pattern.id] = matchLoc;
            // angles는 위에서 이미 설정됨
            
            // 전체 결과 갱신
            result.isPassed = result.isPassed && fidMatched;
            
            logDebug(QString("FID 패턴 '%1' 매칭 결과: %2 (점수: %3, 위치: %4,%5, 최종각도: %6°)")
                    .arg(pattern.name)
                    .arg(fidMatched ? "성공" : "실패")
                    .arg(matchScore, 0, 'f', 6)
                    .arg(matchLoc.x)
                    .arg(matchLoc.y)
                    .arg(result.angles[pattern.id], 0, 'f', 2));
        }
    }
    
    // 6. INS 패턴 검사 수행 (그룹 ROI 제한 적용)
    if (!insPatterns.isEmpty()) {
        logDebug(QString("INS 패턴 검사 처리 중: %1개").arg(insPatterns.size()));
        
        for (const PatternInfo& pattern : insPatterns) {
            // 마스크 필터가 활성화되어 있으면 검사 PASS 처리
            bool hasMaskFilter = false;
            for (const FilterInfo& filter : pattern.filters) {
                if (filter.enabled && filter.type == FILTER_MASK) {
                    hasMaskFilter = true;
                    break;
                }
            }
            if (hasMaskFilter) {
                logDebug(QString("INS 패턴 '%1': 마스크 필터 활성화 → 검사 PASS 처리").arg(pattern.name));
                result.insResults[pattern.id] = true;
                result.insScores[pattern.id] = 1.0;
                result.adjustedRects[pattern.id] = pattern.rect;
                result.insMethodTypes[pattern.id] = InspectionMethod::BINARY;
                continue;
            }

            // 기본 검사 영역은 패턴의 원본 영역
            QRect originalRect = QRect(
                static_cast<int>(pattern.rect.x()),
                static_cast<int>(pattern.rect.y()),
                static_cast<int>(pattern.rect.width()),
                static_cast<int>(pattern.rect.height())
            );
            QRect adjustedRect = originalRect;
            
            // 부모 FID 정보가 있는 경우 처리
            cv::Point parentOffset(0, 0);
            double parentAngle = 0.0;
            double parentFidTeachingAngle = 0.0; // 스코프 확장
            bool hasParentInfo = false;
            
            // 부모 FID가 있는지 확인
            if (!pattern.parentId.isNull()) {
                // 부모 FID의 매칭 결과가 있는지 확인
                if (result.fidResults.contains(pattern.parentId)) {
                    // 부모 FID가 매칭에 실패했을 경우 이 INS 패턴 검사 건너뛰기
                    if (!result.fidResults[pattern.parentId]) {
                        logDebug(QString("INS 패턴 '%1': 부모 FID '%2'가 매칭에 실패하여 검사 건너뜀")
                                .arg(pattern.name).arg(pattern.parentId.toString()));
                        continue;
                    }
                    
                    // FID 점수 확인 - 1.0이면 위치 조정 생략
                    double fidScore = result.matchScores.value(pattern.parentId, 0.0);
                    if (fidScore >= 0.999) {
                        logDebug(QString("INS 패턴 '%1': 부모 FID 점수가 %.6f이므로 원래 위치에서 검사")
                                .arg(pattern.name).arg(fidScore));
                        // 위치 조정하지 않고 원래 패턴 위치 그대로 사용
                        adjustedRect = originalRect;
                    } else {
                        logDebug(QString("INS 패턴 '%1': 부모 FID 점수가 %.6f이므로 위치 조정 수행")
                                .arg(pattern.name).arg(fidScore));
                    
                    // 부모 FID의 위치 정보가 있는 경우 조정
                    if (result.locations.contains(pattern.parentId)) {
                        cv::Point fidLoc = result.locations[pattern.parentId];
                        double fidAngle = result.angles[pattern.parentId];
                        
                        // FID의 원래 정보 찾기
                        QPoint originalFidCenter;
                        // double parentFidTeachingAngle = 0.0; // 제거: 위에서 이미 선언됨
                        PatternInfo parentFidInfo;
                        bool foundFid = false;
                        
                        for (const PatternInfo& fid : fidPatterns) {
                            if (fid.id == pattern.parentId) {
                                originalFidCenter = QPoint(
                                    static_cast<int>(fid.rect.center().x()),
                                    static_cast<int>(fid.rect.center().y())
                                );
                                // 백업된 패턴에서 원본 티칭 각도 가져오기 (검출 각도가 아닌 원본 각도)
                                auto backupIt = std::find_if(patterns.begin(), patterns.end(),
                                    [&](const PatternInfo& p) { return p.id == pattern.parentId; });
                                if (backupIt != patterns.end()) {
                                    parentFidTeachingAngle = backupIt->angle; // 원본 티칭 각도 사용
                                } else {
                                    parentFidTeachingAngle = 0.0; // 백업이 없으면 0도로 가정
                                }
                                parentFidInfo = fid;
                                foundFid = true;
                                break;
                            }
                        }
                        
                        if (!foundFid) {
                            logDebug(QString("INS 패턴 '%1': 부모 FID 정보를 찾을 수 없음")
                                    .arg(pattern.name));
                            continue;
                        }
                        
                        // 부모 FID의 위치와 각도 정보를 저장
                        parentOffset = cv::Point(
                            fidLoc.x - originalFidCenter.x(),
                            fidLoc.y - originalFidCenter.y()
                        );
                        
                        // **중요**: FID 그룹으로 회전하는 경우 FID 검출 각도를 그대로 사용
                        // INS 개별 각도는 무시하고 FID 매칭 각도로 덩어리째 회전
                        parentAngle = fidAngle;
                        hasParentInfo = true;
                        
                        // 각도 계산 디버그 로그 추가
                        logDebug(QString("INS 패턴 '%1' FID 그룹 회전:")
                                .arg(pattern.name));
                        logDebug(QString("  - INS 개별 각도 (무시됨): %1°")
                                .arg(pattern.angle, 0, 'f', 2));
                        logDebug(QString("  - FID 검출 각도 (적용됨): %1°")
                                .arg(fidAngle, 0, 'f', 2));
                        logDebug(QString("  - 최종 사용 각도: %1°")
                                .arg(parentAngle, 0, 'f', 2));
                        
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
                            newCenterX - width/2,   // 회전된 새 x 좌표
                            newCenterY - height/2,  // 회전된 새 y 좌표
                            width,                  // 너비 동일
                            height                  // 높이 동일
                        );
                        
            // 로그: FID/INS 티칭-검출 중심 및 회전 정보 (부동소수점 기반)
            logDebug(QString("INS 패턴 '%1': FID 중심 덩어리 회전 적용")
                .arg(pattern.name));
            logDebug(QString("  - FID 티칭 중심: (%1,%2)")
                .arg(fidTeachingCenterF.x(), 0, 'f', 1).arg(fidTeachingCenterF.y(), 0, 'f', 1));
            logDebug(QString("  - FID 검출 중심: (%1,%2)")
                .arg(fidLoc.x).arg(fidLoc.y));
            logDebug(QString("  - INS 티칭 중심: (%1,%2)")
                .arg(insTeachingCenterF.x(), 0, 'f', 1).arg(insTeachingCenterF.y(), 0, 'f', 1));
            logDebug(QString("  - FID로부터 INS 상대위치 (티칭): (%1,%2)")
                .arg(relX, 0, 'f', 1).arg(relY, 0, 'f', 1));
            logDebug(QString("  - 회전 각도: %1° (FID검출:%2° - FID티칭:%3°)")
                .arg(angleDiff, 0, 'f', 2).arg(fidAngle, 0, 'f', 2).arg(parentFidTeachingAngle, 0, 'f', 2));
            logDebug(QString("  - 회전된 상대위치: (%1,%2)")
                .arg(rotatedX, 0, 'f', 1).arg(rotatedY, 0, 'f', 1));
            logDebug(QString("  - INS 최종 중심: (%1,%2)")
                .arg(newCenterX).arg(newCenterY));
            logDebug(QString("  - 전체 그룹이 FID 중심 (%1,%2) 기준으로 %3° 덩어리 회전됨")
                .arg(fidLoc.x).arg(fidLoc.y).arg(angleDiff, 0, 'f', 2));
                        
                        logDebug(QString("INS 패턴 '%1': 부모 FID '%2'의 위치(%3,%4) 각도(%5°) 기반으로 조정")
                                .arg(pattern.name)
                                .arg(pattern.parentId.toString())
                                .arg(fidLoc.x)
                                .arg(fidLoc.y)
                                .arg(fidAngle, 0, 'f', 2));
                                
                        logDebug(QString("INS 패턴 '%1': 원본 영역(%2,%3,%4,%5) -> 조정된 영역(%6,%7,%8,%9)")
                                .arg(pattern.name)
                                .arg(originalRect.x())
                                .arg(originalRect.y())
                                .arg(originalRect.width())
                                .arg(originalRect.height())
                                .arg(adjustedRect.x())
                                .arg(adjustedRect.y())
                                .arg(adjustedRect.width())
                                .arg(adjustedRect.height()));
                        }
                    }
                }
            }
            
            // **중요**: 조정된 영역의 중심점이 그룹 ROI 내에 있는지 확인
            QPoint adjustedCenter = adjustedRect.center();
            if (!isInGroupROI(pattern.id, adjustedCenter)) {
                logDebug(QString("INS 패턴 '%1': 조정된 위치가 그룹 ROI 영역 외부에 있어 검사에서 제외됨").arg(pattern.name));
                continue;
            }
            
            // 기존 ROI 검사도 유지 (하위 호환성)
            if (!isInROI(adjustedCenter)) {
                logDebug(QString("INS 패턴 '%1': 조정된 위치가 ROI 영역 외부에 있어 검사에서 제외됨").arg(pattern.name));
                continue;
            }
            
            // 조정된 영역이 이미지 경계를 벗어나는지 확인
            if (adjustedRect.x() < 0 || adjustedRect.y() < 0 ||
                adjustedRect.x() + adjustedRect.width() > image.cols ||
                adjustedRect.y() + adjustedRect.height() > image.rows) {
                
                logDebug(QString("INS 패턴 '%1': 조정된 영역이 이미지 경계를 벗어남, 영역 조정").arg(pattern.name));
                
                // 이미지 경계 내로 영역 조정
                int x = std::max(0, adjustedRect.x());
                int y = std::max(0, adjustedRect.y());
                int width = std::min(image.cols - x, adjustedRect.width());
                int height = std::min(image.rows - y, adjustedRect.height());
                
                // 조정된 영역이 너무 작으면 검사 실패
                if (width < 10 || height < 10) {
                    logDebug(QString("INS 패턴 '%1': 조정된 영역이 너무 작음, 검사 실패").arg(pattern.name));
                    result.insResults[pattern.id] = false;
                    result.insScores[pattern.id] = 0.0;
                    if (hasParentInfo) {
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
            
            // 디버그: 검사 방법 확인
            logDebug(QString("INS 패턴 '%1': 검사 방법=%2 (%3)")
                    .arg(pattern.name)
                    .arg(pattern.inspectionMethod)
                    .arg(InspectionMethod::getName(pattern.inspectionMethod)));
            
            // 최종 계산된 각도 설정 (INS 원본 각도 + FID 회전 차이)
            logDebug(QString("=== adjustedPattern.angle 설정 전: hasParentInfo=%1, pattern.angle=%2도 ===")
                    .arg(hasParentInfo ? "true" : "false")
                    .arg(pattern.angle, 0, 'f', 2));
                    
            if (hasParentInfo) {
                // FID 회전 차이만큼 INS 원본 각도에 추가
                double fidAngleDiff = parentAngle - parentFidTeachingAngle;
                adjustedPattern.angle = pattern.angle + fidAngleDiff; // INS 원본 각도 + FID 회전 차이
                
                logDebug(QString("INS 패턴 '%1' 최종 각도 계산:")
                        .arg(pattern.name));
                logDebug(QString("  - INS 원본 각도: %1°")
                        .arg(pattern.angle, 0, 'f', 2));
                logDebug(QString("  - FID 회전 차이: %1° (검출:%2° - 티칭:%3°)")
                        .arg(fidAngleDiff, 0, 'f', 2)
                        .arg(parentAngle, 0, 'f', 2)
                        .arg(parentFidTeachingAngle, 0, 'f', 2));
                logDebug(QString("  - 최종 검사 각도: %1°")
                        .arg(adjustedPattern.angle, 0, 'f', 2));
            } else {
                adjustedPattern.angle = pattern.angle; // 패턴 각도만 
                logDebug(QString("=== hasParentInfo=false이므로 pattern.angle=%1도 그대로 사용 ===")
                        .arg(adjustedPattern.angle, 0, 'f', 2));
            }
             
            switch (pattern.inspectionMethod) {
                case InspectionMethod::COLOR: 
                    inspPassed = checkColor(image, adjustedPattern, inspScore, result);
                    logDebug(QString("색상 검사 수행: %1 (method=%2)").arg(pattern.name).arg(pattern.inspectionMethod));
                    break;
                    
                case InspectionMethod::EDGE: 
                    inspPassed = checkEdge(image, adjustedPattern, inspScore, result);
                    logDebug(QString("엣지 검사 수행: %1 (method=%2)").arg(pattern.name).arg(pattern.inspectionMethod));
                    break;
                    
                case InspectionMethod::BINARY: 
                    inspPassed = checkBinary(image, adjustedPattern, inspScore, result);
                    logDebug(QString("이진화 검사 수행: %1 (method=%2)").arg(pattern.name).arg(pattern.inspectionMethod));
                    break;

                case InspectionMethod::AI_MATCH1:
                    inspPassed = checkAiMatch1(image, adjustedPattern, inspScore, result);
                    logDebug(QString("AI_MATCH1 검사 수행: %1 (method=%2)").arg(pattern.name).arg(pattern.inspectionMethod));
                    break;
                    
                case InspectionMethod::STRIP:
                {
                    // 디버그: STRIP 검사 직전 각도 확인
                    logDebug(QString("=== STRIP 검사 직전: adjustedPattern.angle = %1도 ===")
                            .arg(adjustedPattern.angle, 0, 'f', 2));
                    
                    inspPassed = checkStrip(image, adjustedPattern, inspScore, result);
                    logDebug(QString("STRIP 검사 수행: %1 (점수: %.3f, method=%2)").arg(pattern.name).arg(pattern.inspectionMethod).arg(inspScore));
                    break;
                }
                    
                default:
                    // 이전 PATTERN 타입은 COLOR로 처리
                    inspPassed = checkColor(image, adjustedPattern, inspScore, result);
                    logDebug(QString("알 수 없는 검사 방법 %1, 색상 검사로 수행: %2")
                            .arg(pattern.inspectionMethod).arg(pattern.name));
                    break;
            }
            
            // 결과 반전이 활성화된 경우 결과를 반전
            if (pattern.invertResult) {
                inspPassed = !inspPassed;
                logDebug(QString("INS 패턴 '%1': 결과 반전 적용됨 (%2 -> %3)")
                        .arg(pattern.name)
                        .arg(!inspPassed ? "합격" : "불합격")
                        .arg(inspPassed ? "합격" : "불합격"));
            }
            
            // 결과 기록
            result.insResults[pattern.id] = inspPassed;
            result.insScores[pattern.id] = inspScore;
            result.adjustedRects[pattern.id] = adjustedRect;
            
            // 부모 FID 위치 정보가 있으면 저장
            if (hasParentInfo) {
                result.parentOffsets[pattern.id] = parentOffset;
                result.parentAngles[pattern.id] = parentAngle;
            } else {
                // 그룹화되지 않은 INS 패턴은 티칭한 각도 그대로 사용
                result.parentAngles[pattern.id] = pattern.angle;
            }
            
            // 전체 결과 갱신
            result.isPassed = result.isPassed && inspPassed;
            
            logDebug(QString("INS 패턴 '%1' 검사 결과: %2 (점수: %3)")
                    .arg(pattern.name)
                    .arg(inspPassed ? "합격" : "불합격")
                    .arg(inspScore, 0, 'f', 2));
        }
    }
    
    logDebug(QString("검사 결과: %1 (FID: %2개, INS: %3개)")
            .arg(result.isPassed ? "합격" : "불합격")
            .arg(result.fidResults.size())
            .arg(result.insResults.size()));
            
    return result;
}

bool InsProcessor::matchFiducial(const cv::Mat& image, const PatternInfo& pattern, 
    double& score, cv::Point& matchLoc, double& matchAngle, const QList<PatternInfo>& allPatterns) {
        
    // 초기 점수 설정
    score = 0.0;
    matchAngle = 0.0;
    
    // 입력 이미지 및 템플릿 유효성 검사
    if (image.empty()) {
        logDebug("FID 매칭 실패: 입력 이미지가 비어있음");
        return false;
    }
    
    if (pattern.templateImage.isNull()) {
        logDebug(QString("FID 패턴 '%1': 템플릿 이미지가 없음").arg(pattern.name));
        return false;
    }
    
    try {
        // **수정**: 저장된 템플릿 이미지를 그대로 사용 (0도 상태의 사각형 이미지)
        QImage qimg = pattern.templateImage;
        cv::Mat templateMat;
        
        // QImage를 cv::Mat로 정확하게 변환 (데이터 손실 최소화)
        if (qimg.format() == QImage::Format_RGB888) {
            // RGB888 포맷인 경우 직접 변환
            templateMat = cv::Mat(qimg.height(), qimg.width(), CV_8UC3, 
                                 (void*)qimg.constBits(), qimg.bytesPerLine()).clone();
            // RGB를 BGR로 변환 (OpenCV는 BGR 사용)
            cv::cvtColor(templateMat, templateMat, cv::COLOR_RGB2BGR);
        } else {
            // 다른 포맷인 경우 RGB888로 변환 후 처리
            qimg = qimg.convertToFormat(QImage::Format_RGB888);
            templateMat = cv::Mat(qimg.height(), qimg.width(), CV_8UC3, 
                                 (void*)qimg.constBits(), qimg.bytesPerLine()).clone();
            cv::cvtColor(templateMat, templateMat, cv::COLOR_RGB2BGR);
        }
        
        // 템플릿 이미지 유효성 확인
        if (templateMat.empty()) {
            logDebug(QString("FID 패턴 '%1': 템플릿 이미지가 비어있음").arg(pattern.name));
            return false;
        }
        
        // 템플릿 크기가 너무 작은지 확인
        if (templateMat.rows < 10 || templateMat.cols < 10) {
            logDebug(QString("FID 패턴 '%1': 템플릿 크기가 너무 작음 (%2x%3)")
                    .arg(pattern.name).arg(templateMat.cols).arg(templateMat.rows));
            return false;
        }
        
        // 검색 영역(ROI) 결정
        cv::Rect searchRoi;
        bool roiDefined = false;
        
        // 모든 ROI 패턴 검색
        for (const PatternInfo& roi : allPatterns) {
            // ROI 패턴인지 확인하고 활성화된 상태인지 확인
            if (roi.type == PatternType::ROI && roi.enabled) {
                // "전체 카메라 영역 포함" 체크 여부 확인
                if (roi.includeAllCamera) {
                    // 전체 이미지 영역 사용
                    searchRoi = cv::Rect(0, 0, image.cols, image.rows);
                    roiDefined = true;
                    logDebug(QString("ROI 패턴 '%1': 전체 카메라 영역 포함 옵션 활성화됨")
                            .arg(roi.name));
                    break; // 전체 영역 사용하는 ROI가 있으면 더 이상 검색하지 않음
                } else {
                    // FID 패턴이 이 ROI 내부에 있는지 확인 (중심점 기준)
                    QPoint fidCenter = QPoint(
                        static_cast<int>(pattern.rect.center().x()),
                        static_cast<int>(pattern.rect.center().y())
                    );
                    if (roi.rect.contains(fidCenter)) {
                        // 해당 ROI 영역 내에서만 검색
                        searchRoi = cv::Rect(
                            roi.rect.x(),
                            roi.rect.y(),
                            roi.rect.width(),
                            roi.rect.height()
                        );
                        roiDefined = true;
                        logDebug(QString("FID 패턴 '%1'이(가) ROI 패턴 '%2' 내에 있음")
                                .arg(pattern.name).arg(roi.name));
                        break; // 첫 번째로 찾은 포함하는 ROI 사용
                    }
                }
            }
        }
        
        // 유효한 ROI가 없는 경우 FID 패턴 주변 영역 사용
        if (!roiDefined) {
            logDebug(QString("FID 패턴 '%1': 적절한 ROI를 찾지 못함, 패턴 주변 영역 사용")
                    .arg(pattern.name));
                    
            // 패턴 주변으로 검색 영역 확장 (패턴 크기의 2배 정도)
            int margin = std::max(static_cast<int>(pattern.rect.width()), static_cast<int>(pattern.rect.height()));
            searchRoi = cv::Rect(
                std::max(0, static_cast<int>(pattern.rect.x()) - margin),
                std::max(0, static_cast<int>(pattern.rect.y()) - margin),
                std::min(image.cols - static_cast<int>(pattern.rect.x()) + margin, static_cast<int>(pattern.rect.width()) + 2 * margin),
                std::min(image.rows - static_cast<int>(pattern.rect.y()) + margin, static_cast<int>(pattern.rect.height()) + 2 * margin)
            );
        }
        
        // 검색 영역 유효성 확인 및 로그
        if (searchRoi.width <= 0 || searchRoi.height <= 0 || 
            searchRoi.x + searchRoi.width > image.cols || 
            searchRoi.y + searchRoi.height > image.rows) {
            logDebug(QString("FID 패턴 '%1': 유효하지 않은 검색 영역 (%2,%3,%4,%5)")
                    .arg(pattern.name)
                    .arg(searchRoi.x).arg(searchRoi.y)
                    .arg(searchRoi.width).arg(searchRoi.height));
            return false;
        }
        
        // 템플릿이 검색 영역보다 큰지 확인
        if (templateMat.rows > searchRoi.height || templateMat.cols > searchRoi.width) {
            logDebug(QString("FID 패턴 '%1': 템플릿(%2x%3)이 검색 영역(%4x%5)보다 큼")
                    .arg(pattern.name)
                    .arg(templateMat.cols).arg(templateMat.rows)
                    .arg(searchRoi.width).arg(searchRoi.height));
            return false;
        }
        
        // 검색 영역 추출
        cv::Mat roi = image(searchRoi).clone();
        
        // **수정**: 템플릿은 티칭할 때 저장된 원본 그대로 사용 (검사 시 갱신하지 않음)
        cv::Mat processedTemplate = templateMat.clone();
        
        // 검색 영역(ROI)에만 필터 적용 (템플릿은 그대로 유지)
        if (!pattern.filters.isEmpty()) {
            logDebug(QString("FID 패턴 '%1': %2개의 필터를 검색 영역에만 적용")
                    .arg(pattern.name)
                    .arg(pattern.filters.size()));
                    
            // 검색 영역에만 필터 적용 (템플릿은 건드리지 않음)
            cv::Mat filteredRoi = roi.clone();
            ImageProcessor processor;
            
            for (const FilterInfo& filter : pattern.filters) {
                if (filter.enabled) {
                    try {
                        cv::Mat tempRoi;
                        processor.applyFilter(filteredRoi, tempRoi, filter);
                        
                        if (!tempRoi.empty()) {
                            filteredRoi = tempRoi.clone();
                            logDebug(QString("FID 패턴 '%1': 필터 %2 검색 영역에 적용됨")
                                    .arg(pattern.name)
                                    .arg(filter.type));
                        }
                    } catch (const cv::Exception& e) {
                        logDebug(QString("FID 패턴 '%1': 필터 적용 중 예외 발생 - %2")
                                .arg(pattern.name)
                                .arg(e.what()));
                    }
                }
            }
            
            // 필터 적용된 검색 영역으로 교체 (템플릿은 그대로)
            roi = filteredRoi;
        }
        
        // 매칭 수행
        bool matched = false;
        cv::Point localMatchLoc; // ROI 내에서의 매칭 위치
        double tempAngle = 0.0; // 임시 각도 변수 (switch 밖에서 선언)
        
        // 회전 매칭 파라미터 설정
        double tmplMinA = 0.0, tmplMaxA = 0.0, tmplStep = 1.0;
        if (pattern.useRotation) {
            tmplMinA = pattern.minAngle;
            tmplMaxA = pattern.maxAngle;
            tmplStep = pattern.angleStep;
        }
        
        switch (pattern.fidMatchMethod) {
            case 0: // 템플릿 매칭
                logDebug(QString("FID 패턴 '%1' 템플릿 매칭 호출: useRotation=%2, min=%3°, max=%4°, step=%5°, ROI=%6x%7, templ=%8x%9")
                        .arg(pattern.name)
                        .arg(pattern.useRotation ? "true" : "false")
                        .arg(tmplMinA).arg(tmplMaxA).arg(tmplStep)
                        .arg(roi.cols).arg(roi.rows)
                        .arg(processedTemplate.cols).arg(processedTemplate.rows));
                        
                matched = performTemplateMatching(roi, processedTemplate, localMatchLoc, score, tempAngle,
                                               pattern, tmplMinA, tmplMaxA, tmplStep);
                
                // 회전 매칭이 적용된 경우 탐지된 각도 사용
                if (pattern.useRotation && matched) {
                    matchAngle = tempAngle;
                    logDebug(QString("FID 패턴 '%1': 회전 매칭 적용됨, tempAngle=%2°, matchAngle=%3°")
                            .arg(pattern.name).arg(tempAngle).arg(matchAngle));
                } else if (matched) {
                    matchAngle = pattern.angle; // 기본 각도 사용
                    logDebug(QString("FID 패턴 '%1': 기본 각도 사용, pattern.angle=%2°, matchAngle=%3°")
                            .arg(pattern.name).arg(pattern.angle).arg(matchAngle));
                }
                break;
                
            case 1: // 특징점 매칭
                logDebug(QString("특징점 매칭 수행 중: 패턴 '%1', 검색 영역 (%2,%3,%4,%5)")
                        .arg(pattern.name)
                        .arg(searchRoi.x).arg(searchRoi.y)
                        .arg(searchRoi.width).arg(searchRoi.height));
                // **수정**: 처리된 템플릿 사용, 각도 무시
                matched = performFeatureMatching(roi, processedTemplate, localMatchLoc, score, tempAngle);
                break;
                
            default:
                logDebug("알 수 없는 매칭 방법");
                return false;
        }
        
        // ROI 좌표를 원본 이미지 좌표로 변환
        if (matched) {
            // 수정: ROI 내에서의 상대 위치를 전체 이미지 좌표로 변환
            matchLoc.x = searchRoi.x + localMatchLoc.x;
            matchLoc.y = searchRoi.y + localMatchLoc.y;
            
            // 회전 매칭이 활성화된 경우 검출된 각도를 사용
            matchAngle = tempAngle;
            
            logDebug(QString("매칭 위치: ROI 내 (%1,%2) -> 전체 이미지 (%3,%4)")
                    .arg(localMatchLoc.x)
                    .arg(localMatchLoc.y)
                    .arg(matchLoc.x)
                    .arg(matchLoc.y));
        }
        
        // 매칭 임계값과 비교
        if (matched && score >= pattern.matchThreshold) {
            logDebug(QString("매칭 성공: 점수 %1 (임계값: %2)")
                    .arg(score, 0, 'f', 2).arg(pattern.matchThreshold, 0, 'f', 2));
            return true;
        } else {
            logDebug(QString("매칭 실패: 점수 %1 < 임계값 %2, 하지만 검출된 각도는 적용됨")
                    .arg(score, 0, 'f', 2).arg(pattern.matchThreshold, 0, 'f', 2));
            // 매칭은 실패했지만 검출된 각도는 적용 (matchAngle은 이미 설정됨)
            return false;
        }
    } catch (const cv::Exception& e) {
        logDebug(QString("FID 매칭 중 OpenCV 예외 발생: %1").arg(e.what()));
        return false;
    } catch (const std::exception& e) {
        logDebug(QString("FID 매칭 중 일반 예외 발생: %1").arg(e.what()));
        return false;
    } catch (...) {
        logDebug("FID 매칭 중 알 수 없는 예외 발생");
        return false;
    }
}

// 패턴 정보를 받는 템플릿 매칭 함수 (헤더에 선언된 버전)
bool InsProcessor::performTemplateMatching(const cv::Mat& image, const cv::Mat& templ, 
                                           cv::Point& matchLoc, double& score, double& angle,
                                           const PatternInfo& pattern,
                                           double minAngle, double maxAngle, double angleStep) {
    
    if (image.empty() || templ.empty()) {
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
    
    if (!pattern.useRotation) {
        // 회전 허용 안함: 원본 템플릿 그대로 매칭 (패턴 각도는 이미 티칭 시 반영됨)
        logDebug(QString("회전 비허용: 원본 템플릿 그대로 매칭 (패턴 각도 %1°는 이미 반영됨)").arg(pattern.angle));
        
        // 원본 템플릿 그대로 사용
        cv::Mat templateForMatching = templGray.clone();
        cv::Mat templateMask;
        
        // 검은색 패턴 부분만 매칭에 사용하는 마스크 생성
        cv::threshold(templGray, templateMask, 127, 255, cv::THRESH_BINARY_INV); // 검은색 부분만 255
        
        cv::Mat result;
        cv::matchTemplate(imageGray, templateForMatching, result, cv::TM_CCOEFF_NORMED, templateMask);
        
        double minVal, maxVal;
        cv::Point minLoc, maxLoc;
        cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
        
        // 템플릿 매칭은 왼쪽 상단 좌표를 반환하므로, 중심점을 계산
        matchLoc.x = static_cast<int>(maxLoc.x + templateForMatching.cols / 2.0 + 0.5);
        matchLoc.y = static_cast<int>(maxLoc.y + templateForMatching.rows / 2.0 + 0.5);
        score = maxVal;
        angle = pattern.angle; // 패턴에 저장된 각도 그대로 반환
        
        logDebug(QString("원본 템플릿 매칭 완료: 점수=%1, 각도=%2°, 위치=(%3,%4)")
                .arg(score, 0, 'f', 4).arg(angle).arg(matchLoc.x).arg(matchLoc.y));
                
        return true;
    }
    
    // 회전 허용: 티칭 각도를 기준으로 +/- 범위에서 모든 각도 시도
    logDebug(QString("회전 허용: 티칭각도 %1°를 기준으로 %2°~%3° 범위에서 1도 간격으로 매칭")
            .arg(pattern.angle).arg(minAngle).arg(maxAngle));
    
    // 티칭 각도 기준으로 상대 범위 적용
    double adjustedMinAngle = pattern.angle + minAngle;
    double adjustedMaxAngle = pattern.angle + maxAngle;
    
    // 템플릿의 원본 크기 저장
    int originalWidth = templGray.cols;
    int originalHeight = templGray.rows;
    
    // 회전 시 잘림 방지를 위해 여백이 있는 더 큰 이미지 생성
    int diagonal = static_cast<int>(std::sqrt(originalWidth * originalWidth + 
                                             originalHeight * originalHeight)) + 10;
    int offsetX = (diagonal - originalWidth) / 2;
    int offsetY = (diagonal - originalHeight) / 2;
    
    cv::Mat paddedTempl = cv::Mat::zeros(diagonal, diagonal, templGray.type());
    cv::Rect roi(offsetX, offsetY, originalWidth, originalHeight);
    templGray.copyTo(paddedTempl(roi));
    
    // 최적 매칭을 위한 변수들
    double bestScore = -1.0;
    double bestAngle = adjustedMinAngle;
    cv::Point bestLocation;
    cv::Mat bestTemplate; // 최고 점수를 얻은 템플릿 저장용
    cv::Mat bestMask; // 최고 점수를 얻은 마스크 저장용
    
    // 각도 범위 내에서 최적 매칭 검색
    logDebug(QString("회전 템플릿 매칭 시작: 각도 범위=%1° ~ %2°, 스텝=%3°")
            .arg(adjustedMinAngle).arg(adjustedMaxAngle).arg(angleStep));
            
    // 원본 템플릿에서 검은색 패턴 부분만 매칭에 사용하는 마스크 생성
    cv::Mat originalMask = cv::Mat::zeros(diagonal, diagonal, CV_8U);
    cv::Mat templMask;
    cv::threshold(templGray, templMask, 127, 255, cv::THRESH_BINARY_INV); // 검은색 부분만 255
    templMask.copyTo(originalMask(roi));
            
    // 각도 리스트 생성: 2단계 적응적 검색 + 조기 종료
    std::vector<double> angleList;
    
    // 1. 티칭 각도를 첫 번째로 추가 (가장 우선)
    angleList.push_back(pattern.angle);
    
    // 2. 1단계: 큰 스텝(5도)으로 빠른 검색을 위한 각도 추가
    std::vector<double> coarseAngles;
    for (double currentAngle = adjustedMinAngle; currentAngle <= adjustedMaxAngle; currentAngle += 5.0) {
        if (std::abs(currentAngle - pattern.angle) >= 2.5) { // 티칭 각도와 충분히 떨어진 경우만
            coarseAngles.push_back(currentAngle);
        }
    }
    
    // 디버깅용: 검색 전략 출력
    logDebug(QString("=== 2단계 적응적 회전 매칭 ==="));
    logDebug(QString("티칭 각도: %1° (1순위)").arg(pattern.angle));
    logDebug(QString("상대 범위: %1° ~ %2°").arg(minAngle).arg(maxAngle));
    logDebug(QString("절대 범위: %1° ~ %2°").arg(adjustedMinAngle).arg(adjustedMaxAngle));
    logDebug(QString("1단계 검색: 5도 간격, %1개 각도").arg(coarseAngles.size()));
    
    QString coarseAngleStr;
    for (size_t i = 0; i < std::min(coarseAngles.size(), size_t(10)); ++i) {
        if (i > 0) coarseAngleStr += ", ";
        coarseAngleStr += QString::number(coarseAngles[i], 'f', 0) + "°";
    }
    if (coarseAngles.size() > 10) {
        coarseAngleStr += QString(" ... (총 %1개)").arg(coarseAngles.size());
    }
    logDebug(QString("1단계 각도 리스트: %1").arg(coarseAngleStr));
    
    // === 1단계: 티칭 각도 + 5도 간격 빠른 검색 ===
    logDebug(QString("1단계 시작: 티칭각도 + %1개 각도에서 빠른 검색").arg(coarseAngles.size()));
    
    double bestCoarseScore = -1.0;
    double bestCoarseAngle = pattern.angle;
    cv::Point bestCoarseLocation;
    
    // 첫 번째로 티칭 각도 시도
    angleList.clear();
    angleList.push_back(pattern.angle);
    
    // 1단계 각도들 추가
    for (double angle : coarseAngles) {
        angleList.push_back(angle);
    }
    
    for (double currentAngle : angleList) {
        // 티칭 각도인지 확인
        bool isTeachingAngle = (std::abs(currentAngle - pattern.angle) < 0.01);
        
        // 템플릿 회전
        cv::Mat templateForMatching, rotatedMask;
        
        if (isTeachingAngle) {
            // 티칭 각도인 경우: 원본 템플릿 그대로 사용
            logDebug(QString("티칭 각도 %1°: 원본 템플릿 사용").arg(currentAngle));
            templateForMatching = templGray.clone();
            cv::threshold(templGray, rotatedMask, 127, 255, cv::THRESH_BINARY_INV);
        } else {
            // 다른 각도인 경우: 패딩된 템플릿을 회전
            logDebug(QString("각도 %1°: 템플릿 회전 (상대각도: %2°)")
                    .arg(currentAngle).arg(currentAngle - pattern.angle));
            
            cv::Mat rotMatrix = cv::getRotationMatrix2D(
                cv::Point2f(paddedTempl.cols / 2.0, paddedTempl.rows / 2.0), 
                -(currentAngle - pattern.angle), 1.0); // 상대 회전각
            
            cv::Mat rotatedTempl;
            cv::warpAffine(paddedTempl, rotatedTempl, rotMatrix, paddedTempl.size(), 
                         cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
            cv::warpAffine(originalMask, rotatedMask, rotMatrix, originalMask.size(), 
                         cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
            
            // 마스크 이진화 (회전으로 인한 보간 오류 제거)
            cv::threshold(rotatedMask, rotatedMask, 128, 255, cv::THRESH_BINARY);
            
            // 회전된 템플릿을 그레이스케일로 변환
            if (rotatedTempl.channels() == 3) {
                cv::cvtColor(rotatedTempl, templateForMatching, cv::COLOR_BGR2GRAY);
            } else {
                templateForMatching = rotatedTempl.clone();
            }
        }
        
        // 타입을 CV_8U로 통일
        templateForMatching.convertTo(templateForMatching, CV_8U);
        rotatedMask.convertTo(rotatedMask, CV_8U);
        
        // 템플릿 크기와 이미지 크기 검증
        if (templateForMatching.empty() || imageGray.empty()) {
            logDebug(QString("각도 %1°: 템플릿 또는 이미지가 비어있음").arg(currentAngle));
            continue;
        }
        
        if (templateForMatching.cols > imageGray.cols || templateForMatching.rows > imageGray.rows) {
            logDebug(QString("각도 %1°: 템플릿이 이미지보다 큼 (템플릿:%2x%3, 이미지:%4x%5)")
                    .arg(currentAngle)
                    .arg(templateForMatching.cols).arg(templateForMatching.rows)
                    .arg(imageGray.cols).arg(imageGray.rows));
            continue;
        }
        
        // 마스크 영역이 너무 작으면 스킬
        int maskArea = cv::countNonZero(rotatedMask);
        int originalArea = originalWidth * originalHeight;
        if (maskArea < originalArea * 0.2) { // 원본의 20% 미만이면 스킵
            logDebug(QString("각도 %1°: 마스크 영역이 너무 작음 (%2/%3, %4%)")
                    .arg(currentAngle).arg(maskArea).arg(originalArea)
                    .arg(maskArea * 100.0 / originalArea, 0, 'f', 1));
            continue;
        }
        
        cv::Mat result;
        try {
            cv::matchTemplate(imageGray, templateForMatching, result, cv::TM_CCOEFF_NORMED, rotatedMask);
        } catch (const cv::Exception& e) {
            logDebug(QString("각도 %1°: 템플릿 매칭 오류 - %2").arg(currentAngle).arg(e.what()));
            continue;
        }
        
        if (result.empty()) {
            logDebug(QString("각도 %1°: 매칭 결과가 비어있음").arg(currentAngle));
            continue;
        }
        
        // 최대값과 위치 찾기
        double minVal, maxVal;
        cv::Point minLoc, maxLoc;
        cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
        
        logDebug(QString("1단계 - 각도 %1°%2: 매칭 점수=%3, 마스크 영역=%4/%5 (%6%), 위치=(%7,%8)")
                .arg(currentAngle, 0, 'f', 0)
                .arg(isTeachingAngle ? " (티칭)" : "")
                .arg(maxVal, 0, 'f', 4)
                .arg(maskArea).arg(originalArea)
                .arg(maskArea * 100.0 / originalArea, 0, 'f', 1)
                .arg(maxLoc.x).arg(maxLoc.y));
        
        // 1단계 최고 점수 업데이트
        if (maxVal > bestCoarseScore) {
            bestCoarseScore = maxVal;
            bestCoarseAngle = currentAngle;
            bestCoarseLocation.x = static_cast<int>(maxLoc.x + templateForMatching.cols / 2.0 + 0.5);
            bestCoarseLocation.y = static_cast<int>(maxLoc.y + templateForMatching.rows / 2.0 + 0.5);
            logDebug(QString("1단계 새로운 최고! 각도 %1°%2: %3")
                    .arg(currentAngle, 0, 'f', 0)
                    .arg(isTeachingAngle ? " (티칭)" : "")
                    .arg(maxVal, 0, 'f', 4));
        }
        
        // 조기 종료: 95% 이상 점수면 검색 중단
        if (maxVal >= 0.95) {
            logDebug(QString("조기 종료: 충분히 높은 점수(%1) 달성, 각도 %2°")
                    .arg(maxVal, 0, 'f', 4).arg(currentAngle));
            matchLoc.x = static_cast<int>(maxLoc.x + templateForMatching.cols / 2.0 + 0.5);
            matchLoc.y = static_cast<int>(maxLoc.y + templateForMatching.rows / 2.0 + 0.5);
            score = maxVal;
            angle = currentAngle;
            return true;
        }
        
        // 전체 최고 점수도 업데이트 (2단계에서 사용)
        if (maxVal > bestScore) {
            bestScore = maxVal;
            bestAngle = currentAngle;
            bestLocation.x = static_cast<int>(maxLoc.x + templateForMatching.cols / 2.0 + 0.5);
            bestLocation.y = static_cast<int>(maxLoc.y + templateForMatching.rows / 2.0 + 0.5);
            bestTemplate = templateForMatching.clone();
            bestMask = rotatedMask.clone();
        }
    }
    
    logDebug(QString("1단계 완료: 최고 점수=%1, 각도=%2°")
            .arg(bestCoarseScore, 0, 'f', 4).arg(bestCoarseAngle, 0, 'f', 0));
    
    // === 2단계: 1단계 최적 각도 주변에서 1도 간격 정밀 검색 ===
    double fineSearchMin = bestCoarseAngle - 3.0;
    double fineSearchMax = bestCoarseAngle + 3.0;
    
    // 원래 범위를 벗어나지 않도록 제한
    fineSearchMin = std::max(fineSearchMin, adjustedMinAngle);
    fineSearchMax = std::min(fineSearchMax, adjustedMaxAngle);
    
    logDebug(QString("2단계 시작: %1° 주변 %2°~%3° 범위에서 1도 간격 정밀 검색")
            .arg(bestCoarseAngle, 0, 'f', 0).arg(fineSearchMin, 0, 'f', 0).arg(fineSearchMax, 0, 'f', 0));
    
    // 2단계 각도 리스트 생성 (1단계에서 이미 시도한 각도 제외)
    std::vector<double> fineAngles;
    for (double currentAngle = fineSearchMin; currentAngle <= fineSearchMax; currentAngle += 1.0) {
        // 1단계에서 이미 시도한 각도가 아닌 경우만 추가
        bool alreadyTested = false;
        for (double testedAngle : angleList) {
            if (std::abs(currentAngle - testedAngle) < 0.1) {
                alreadyTested = true;
                break;
            }
        }
        if (!alreadyTested) {
            fineAngles.push_back(currentAngle);
        }
    }
    
    logDebug(QString("2단계 각도 개수: %1개").arg(fineAngles.size()));
    
    for (double currentAngle : fineAngles) {
        // 티칭 각도인지 확인 (거의 없겠지만)
        bool isTeachingAngle = (std::abs(currentAngle - pattern.angle) < 0.01);
        
        // 템플릿 회전
        cv::Mat templateForMatching, rotatedMask;
        
        if (isTeachingAngle) {
            // 티칭 각도인 경우: 원본 템플릿 그대로 사용
            templateForMatching = templGray.clone();
            cv::threshold(templGray, rotatedMask, 127, 255, cv::THRESH_BINARY_INV);
        } else {
            // 다른 각도인 경우: 패딩된 템플릿을 회전
            cv::Mat rotMatrix = cv::getRotationMatrix2D(
                cv::Point2f(paddedTempl.cols / 2.0, paddedTempl.rows / 2.0), 
                -(currentAngle - pattern.angle), 1.0); // 상대 회전각
            
            cv::Mat rotatedTempl;
            cv::warpAffine(paddedTempl, rotatedTempl, rotMatrix, paddedTempl.size(), 
                         cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
            cv::warpAffine(originalMask, rotatedMask, rotMatrix, originalMask.size(), 
                         cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
            
            // 마스크 이진화 (회전으로 인한 보간 오류 제거)
            cv::threshold(rotatedMask, rotatedMask, 128, 255, cv::THRESH_BINARY);
            
            // 회전된 템플릿을 그레이스케일로 변환
            if (rotatedTempl.channels() == 3) {
                cv::cvtColor(rotatedTempl, templateForMatching, cv::COLOR_BGR2GRAY);
            } else {
                templateForMatching = rotatedTempl.clone();
            }
        }
        
        // 타입을 CV_8U로 통일
        templateForMatching.convertTo(templateForMatching, CV_8U);
        rotatedMask.convertTo(rotatedMask, CV_8U);
        
        // 템플릿 크기와 이미지 크기 검증
        if (templateForMatching.empty() || imageGray.empty()) {
            continue;
        }
        
        if (templateForMatching.cols > imageGray.cols || templateForMatching.rows > imageGray.rows) {
            continue;
        }
        
        // 마스크 영역이 너무 작으면 스킬
        int maskArea = cv::countNonZero(rotatedMask);
        int originalArea = originalWidth * originalHeight;
        if (maskArea < originalArea * 0.2) {
            continue;
        }
        
        cv::Mat result;
        try {
            cv::matchTemplate(imageGray, templateForMatching, result, cv::TM_CCOEFF_NORMED, rotatedMask);
        } catch (const cv::Exception& e) {
            continue;
        }
        
        if (result.empty()) {
            continue;
        }
        
        // 최대값과 위치 찾기
        double minVal, maxVal;
        cv::Point minLoc, maxLoc;
        cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
        
        logDebug(QString("2단계 - 각도 %1°: 매칭 점수=%2, 위치=(%3,%4)")
                .arg(currentAngle, 0, 'f', 0)
                .arg(maxVal, 0, 'f', 4)
                .arg(maxLoc.x).arg(maxLoc.y));
        
        // 가장 높은 점수 업데이트
        if (maxVal > bestScore) {
            bestScore = maxVal;
            bestAngle = currentAngle;
            bestLocation.x = static_cast<int>(maxLoc.x + templateForMatching.cols / 2.0 + 0.5);
            bestLocation.y = static_cast<int>(maxLoc.y + templateForMatching.rows / 2.0 + 0.5);
            bestTemplate = templateForMatching.clone();
            bestMask = rotatedMask.clone();
            logDebug(QString("2단계 새로운 최고! 각도 %1°: %2 -> 중심점(%3,%4)")
                    .arg(currentAngle, 0, 'f', 0)
                    .arg(maxVal, 0, 'f', 4)
                    .arg(bestLocation.x).arg(bestLocation.y));
        }
    }
    
    // 2단계 완료
    logDebug(QString("2단계 완료: 최종 최고 점수=%1, 각도=%2°, 위치=(%3,%4)")
            .arg(bestScore, 0, 'f', 4).arg(bestAngle, 0, 'f', 0)
            .arg(bestLocation.x).arg(bestLocation.y));

    // 결과 반환
    matchLoc = bestLocation;
    score = bestScore;
    angle = bestAngle;

    return bestScore > 0.0;
}

bool InsProcessor::performFeatureMatching(const cv::Mat& image, const cv::Mat& templ, 
                                          cv::Point& matchLoc, double& score, double& angle) {
    try {
        // 입력 이미지 및 템플릿 유효성 검사
        if (image.empty() || templ.empty()) {
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
        try {
            feature_detector = cv::SIFT::create(0, 3, 0.04, 10, 1.6);
        } catch (const cv::Exception& e) {
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
        if (keypoints_templ.size() < 4) {
            logDebug(QString("특징점 매칭 실패: 템플릿의 특징점이 부족함 (%1개)")
                    .arg(keypoints_templ.size()));
            score = 0.0;
            angle = 0.0;
            return false;
        }
        
        // 이미지에서 특징점 검출
        feature_detector->detectAndCompute(imageGray, cv::noArray(), keypoints_image, descriptors_image);
        
        // 이미지에 충분한 특징점이 없으면 조기 종료
        if (keypoints_image.size() < 4) {
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
        if (descriptors_templ.type() == CV_8U) {
            // ORB, BRISK, AKAZE는 이진 기술자를 사용
            matcher = cv::DescriptorMatcher::create(cv::DescriptorMatcher::BRUTEFORCE_HAMMING);
        } else {
            // SIFT, SURF는 부동소수점 기술자를 사용
            matcher = cv::DescriptorMatcher::create(cv::DescriptorMatcher::FLANNBASED);
        }
        
        // KNN 매칭 수행 (각 특징점에 대해 2개의 최근접 매칭 찾기)
        std::vector<std::vector<cv::DMatch>> knn_matches;
        try {
            matcher->knnMatch(descriptors_templ, descriptors_image, knn_matches, 2);
        } catch (const cv::Exception& e) {
            logDebug(QString("KNN 매칭 실패: %1").arg(e.what()));
            
            // FLANN 실패 시 브루트포스 방식으로 재시도
            matcher = cv::DescriptorMatcher::create(cv::DescriptorMatcher::BRUTEFORCE);
            matcher->knnMatch(descriptors_templ, descriptors_image, knn_matches, 2);
        }
        
        // 좋은 매칭만 필터링 (Lowe's ratio test)
        float ratio_thresh = 0.75f;
        std::vector<cv::DMatch> good_matches;
        for (const auto& match : knn_matches) {
            if (match.size() >= 2 && match[0].distance < ratio_thresh * match[1].distance) {
                good_matches.push_back(match[0]);
            }
        }
        
        // 매칭이 부족한 경우 - 임계값 조정해서 재시도
        if (good_matches.size() < 4 && !knn_matches.empty()) {
            logDebug(QString("첫 번째 매칭 시도 실패: 좋은 매칭이 부족함 (%1개), 임계값 완화 시도")
                    .arg(good_matches.size()));
            
            good_matches.clear();
            ratio_thresh = 0.85f;  // 더 관대한 임계값으로 재시도
            
            for (const auto& match : knn_matches) {
                if (match.size() >= 2 && match[0].distance < ratio_thresh * match[1].distance) {
                    good_matches.push_back(match[0]);
                }
            }
        }
        
        // 매칭이 여전히 부족하면 실패
        if (good_matches.size() < 4) {
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
        for (const auto& match : good_matches) {
            src_pts.push_back(keypoints_templ[match.queryIdx].pt);
            dst_pts.push_back(keypoints_image[match.trainIdx].pt);
        }
        
        // 호모그래피 계산 (RANSAC 사용)
        std::vector<uchar> inliers;  // RANSAC 인라이어 마스크
        double ransacReprojThreshold = 3.0;  // 재투영 오차 임계값
        cv::Mat H = cv::findHomography(src_pts, dst_pts, cv::RANSAC, 
                                     ransacReprojThreshold, inliers);
        
        // 호모그래피 실패하면 매칭 실패
        if (H.empty()) {
            logDebug("특징점 매칭 실패: 호모그래피 계산 실패");
            score = 0.0;
            angle = 0.0;
            return false;
        }
        
        // RANSAC 인라이어 수 계산
        int inlierCount = 0;
        for (const auto& status : inliers) {
            if (status) inlierCount++;
        }
        
        // 인라이어 비율이 너무 낮으면 실패
        double inlierRatio = static_cast<double>(inlierCount) / good_matches.size();
        if (inlierRatio < 0.4) {  // 40% 이하면 신뢰도가 낮음
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
        for (const auto& corner : scene_corners) {
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
    catch (const cv::Exception& e) {
        logDebug(QString("특징점 매칭 중 OpenCV 예외 발생: %1").arg(e.what()));
        score = 0.0;
        angle = 0.0;
        return false;
    }
    catch (const std::exception& e) {
        logDebug(QString("특징점 매칭 중 일반 예외 발생: %1").arg(e.what()));
        score = 0.0;
        angle = 0.0;
        return false;
    }
    catch (...) {
        logDebug("특징점 매칭 중 알 수 없는 예외 발생");
        score = 0.0;
        angle = 0.0;
        return false;
    }
}

bool InsProcessor::checkColor(const cv::Mat& image, const PatternInfo& pattern, double& score, InspectionResult& result) {
    cv::Mat roi = extractROI(image, pattern.rect, pattern.angle);
    
    if (roi.empty()) {
        logDebug(QString("색상 검사 실패: ROI 추출 실패 - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        return false;
    }
    
    if (pattern.templateImage.isNull()) {
        logDebug(QString("색상 검사 실패: 템플릿 이미지가 없음 - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        return false;
    }
    
    // 템플릿 이미지 정보 로그
    logDebug(QString("색상 검사 - 패턴 '%1': 템플릿 크기 %2x%3, ROI 크기 %4x%5")
            .arg(pattern.name)
            .arg(pattern.templateImage.width())
            .arg(pattern.templateImage.height())
            .arg(roi.cols)
            .arg(roi.rows));
    
    try {
        // ROI에 패턴에 적용된 필터 적용 (임시로 비활성화)
        cv::Mat processedRoi = roi.clone(); // 처리할 ROI 이미지 초기화
        
        /*
        if (!pattern.filters.isEmpty()) {
            logDebug(QString("ROI에 %1개 필터 적용").arg(pattern.filters.size()));
            
            // 필터 순차 적용
            ImageProcessor processor;
            for (const FilterInfo& filter : pattern.filters) {
                if (filter.enabled) {
                    cv::Mat filteredRoi;
                    processor.applyFilter(processedRoi, filteredRoi, filter);
                    if (!filteredRoi.empty()) {
                        processedRoi = filteredRoi.clone();
                        logDebug(QString("필터 %1 적용 완료").arg(filter.type));
                    }
                }
            }
        }
        */
        
        // 템플릿 이미지를 cv::Mat으로 변환
        QImage qTemplateImage = pattern.templateImage.convertToFormat(QImage::Format_RGB888);
        cv::Mat templateMat;

        // QImage 데이터를 복사하여 연속된 메모리에 저장
        if (qTemplateImage.format() == QImage::Format_RGB888) {
            templateMat = cv::Mat(qTemplateImage.height(), qTemplateImage.width(), 
                            CV_8UC3, const_cast<uchar*>(qTemplateImage.bits()), qTemplateImage.bytesPerLine());
            templateMat = templateMat.clone(); // 연속된 메모리 보장을 위해 복사
            cv::cvtColor(templateMat, templateMat, cv::COLOR_RGB2BGR); // RGB -> BGR 변환
        } else {
            logDebug(QString("색상 검사 실패: 이미지 형식 변환 실패 - 패턴 '%1'").arg(pattern.name));
            score = 0.0;
            return false;
        }
        
        // 크기 확인 (이제 동일한 방식으로 추출되므로 크기가 같아야 함)
        if (templateMat.size() != processedRoi.size()) {
            logDebug(QString("색상 검사 경고: ROI(%1x%2)와 템플릿(%3x%4) 크기 불일치 - 패턴 '%5'")
                    .arg(processedRoi.cols).arg(processedRoi.rows)
                    .arg(templateMat.cols).arg(templateMat.rows)
                    .arg(pattern.name));
        }
        
        // HSV 색 공간으로 변환하여 히스토그램 비교
        cv::Mat hsvRoi, hsvTemplate;
        cv::cvtColor(processedRoi, hsvRoi, cv::COLOR_BGR2HSV);
        cv::cvtColor(templateMat, hsvTemplate, cv::COLOR_BGR2HSV);
        
        // 히스토그램 계산
        int histSize[] = {30, 32, 32};  // H, S, V 각각의 빈 개수
        float hRanges[] = {0, 180};     // H는 0-180
        float sRanges[] = {0, 256};     // S, V는 0-255
        float vRanges[] = {0, 256};
        const float* ranges[] = {hRanges, sRanges, vRanges};
        int channels[] = {0, 1, 2};     // 모든 채널 사용
        
        cv::Mat roiHist, templateHist;
        cv::calcHist(&hsvRoi, 1, channels, cv::Mat(), roiHist, 3, histSize, ranges);
        cv::calcHist(&hsvTemplate, 1, channels, cv::Mat(), templateHist, 3, histSize, ranges);
        
        // 히스토그램 정규화
        cv::normalize(roiHist, roiHist, 0, 1, cv::NORM_MINMAX);
        cv::normalize(templateHist, templateHist, 0, 1, cv::NORM_MINMAX);
        
        // 히스토그램 비교
        score = cv::compareHist(roiHist, templateHist, cv::HISTCMP_CORREL);
        
        // 상관계수는 -1에서 1 사이의 값이므로 0에서 1로 변환
        score = (score + 1.0) / 2.0;
        
        // 시각화를 위한 차이 이미지 생성
        cv::Mat diffImage;
        cv::absdiff(processedRoi, templateMat, diffImage);
        cv::cvtColor(diffImage, diffImage, cv::COLOR_BGR2GRAY);
        cv::normalize(diffImage, diffImage, 0, 255, cv::NORM_MINMAX);
        cv::bitwise_not(diffImage, diffImage); // 반전해서 유사할수록 밝게 보이도록
        
        // 비교 방식에 따른 결과 판단
        bool passed = false;
        switch (pattern.compareMethod) {
            case 0:  // 이상 (>=)
                passed = (score >= pattern.passThreshold);
                break;
            case 1:  // 이하 (<=)
                passed = (score <= pattern.passThreshold);
                break;
            case 2:  // 범위 내 (lowerThreshold <= score <= upperThreshold)
                passed = (score >= pattern.lowerThreshold && score <= pattern.upperThreshold);
                break;
            default:
                passed = (score >= pattern.passThreshold);
                break;
        }
        
        // 결과 이미지 저장 - InspectionResult에 직접 저장
        result.insProcessedImages[pattern.id] = diffImage;
        result.insMethodTypes[pattern.id] = InspectionMethod::COLOR;
        
        logDebug(QString("색상 검사 결과 - 패턴: '%1', 유사도: %2, 임계값: %3, 결과: %4")
                .arg(pattern.name)
                .arg(score, 0, 'f', 2)
                .arg(pattern.passThreshold, 0, 'f', 2)
                .arg(passed ? "합격" : "불합격"));
        
        return passed;
        
    } catch (const cv::Exception& e) {
        logDebug(QString("색상 검사 중 OpenCV 예외 발생: '%1' - 패턴 '%2'").arg(e.what()).arg(pattern.name));
        score = 0.0;
        return false;
    } catch (const std::exception& e) {
        logDebug(QString("색상 검사 중 일반 예외 발생: '%1' - 패턴 '%2'").arg(e.what()).arg(pattern.name));
        score = 0.0;
        return false;
    } catch (...) {
        logDebug(QString("색상 검사 중 알 수 없는 예외 발생 - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        return false;
    }
}

bool InsProcessor::checkBinary(const cv::Mat& image, const PatternInfo& pattern, double& score, InspectionResult& result) {
    cv::Mat roi = extractROI(image, pattern.rect, pattern.angle);
    if (roi.empty()) {
        logDebug(QString("이진화 검사 실패: ROI 추출 실패 - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        return false;
    }
    
    try {
        // ROI 복사본 생성 (필터 적용용)
        cv::Mat processedRoi = roi.clone();
        
        // 이진화 타입과 파라미터 결정
        int thresholdType = cv::THRESH_BINARY;
        int threshold = pattern.binaryThreshold;
        int blockSize = 11;  // 기본값
        int C = 2;           // 기본값
        
        // 패턴의 필터에서 이진화 필터가 있는지 확인
        for (const FilterInfo& filter : pattern.filters) {
            if (filter.enabled && filter.type == FILTER_THRESHOLD) {
                // 필터에 설정된 이진화 파라미터 사용
                thresholdType = filter.params.value("thresholdType", cv::THRESH_BINARY);
                threshold = filter.params.value("threshold", pattern.binaryThreshold);
                blockSize = filter.params.value("blockSize", 11);
                C = filter.params.value("C", 2);
                
                logDebug(QString("필터에서 이진화 타입 %1 사용, 임계값: %2")
                        .arg(thresholdType).arg(threshold));
                break;
            }
        }
        
        // 필터에 이진화 타입이 없으면 ratioType에 따라 설정
        if (thresholdType == cv::THRESH_BINARY && pattern.ratioType == 1) {
            thresholdType = cv::THRESH_BINARY_INV;
            logDebug(QString("ratioType에 따른 역이진화(BINARY_INV) 사용"));
        }
        
        // 그레이스케일 변환
        cv::Mat gray;
        if (processedRoi.channels() == 3) {
            cv::cvtColor(processedRoi, gray, cv::COLOR_BGR2GRAY);
        } else {
            processedRoi.copyTo(gray);
        }
        
        // 템플릿 이미지가 없는 경우 - 단순 비율 계산
        if (pattern.templateImage.isNull()) {
            cv::Mat binary;
            
            // ImageProcessor 클래스의 thresholdFilter 활용
            ImageProcessor processor;
            cv::Mat tempResult;  // 임시 결과 (RGB 형식)
            processor.applyThresholdFilter(
                processedRoi, tempResult, 
                threshold, thresholdType, blockSize, C
            );
            
            // RGB에서 그레이스케일로 다시 변환 (결과가 RGB로 반환되므로)
            cv::cvtColor(tempResult, binary, cv::COLOR_RGB2GRAY);
            
            // 여기를 수정: 0이 아닌 모든 픽셀을 255로 설정 (특수 이진화 타입 보존)
            cv::threshold(binary, binary, 0, 255, cv::THRESH_BINARY);
            
            int totalPixels = binary.rows * binary.cols;
            int whitePixels = cv::countNonZero(binary);
            
            // 관심 비율 계산 (흰색 픽셀 비율)
            double ratio = (double)whitePixels / totalPixels;
            score = ratio;
            
            // 결과 비교
            bool passed = false;
            switch (pattern.compareMethod) {
                case 0: // 이상 (>=)
                    passed = (score >= pattern.passThreshold);
                    break;
                case 1: // 이하 (<=)
                    passed = (score <= pattern.passThreshold);
                    break;
                case 2: // 범위 내
                    passed = (score >= pattern.lowerThreshold && score <= pattern.upperThreshold);
                    break;
                default:
                    passed = (score >= pattern.passThreshold);
            }
            
            // 결과 이미지 저장
            result.insProcessedImages[pattern.id] = tempResult.clone();  // RGB 형식 그대로 저장
            result.insMethodTypes[pattern.id] = InspectionMethod::BINARY;
            
            logDebug(QString("이진화 검사 결과 (비율): 패턴 '%1', 비율: %2, 임계값: %3, 타입: %4, 결과: %5")
                    .arg(pattern.name)
                    .arg(ratio, 0, 'f', 4)
                    .arg(pattern.passThreshold, 0, 'f', 4)
                    .arg(thresholdType)
                    .arg(passed ? "합격" : "불합격"));
                    
            return passed;
        }
        
        // 템플릿 이미지 로드 및 처리
        QImage qTemplateImage = pattern.templateImage.convertToFormat(QImage::Format_RGB888);
        cv::Mat templateMat;
        
        if (qTemplateImage.format() == QImage::Format_RGB888) {
            templateMat = cv::Mat(qTemplateImage.height(), qTemplateImage.width(), 
                              CV_8UC3, const_cast<uchar*>(qTemplateImage.bits()), qTemplateImage.bytesPerLine());
            templateMat = templateMat.clone();
            cv::cvtColor(templateMat, templateMat, cv::COLOR_RGB2BGR);
        } else {
            logDebug(QString("이진화 검사 실패: 템플릿 이미지 형식 변환 실패 - 패턴 '%1'").arg(pattern.name));
            score = 0.0;
            return false;
        }
        
        // 크기 확인 (이제 동일한 방식으로 추출되므로 크기가 같아야 함)
        if (templateMat.size() != processedRoi.size()) {
            logDebug(QString("이진화 검사 경고: ROI(%1x%2)와 템플릿(%3x%4) 크기 불일치 - 패턴 '%5'")
                    .arg(processedRoi.cols).arg(processedRoi.rows)
                    .arg(templateMat.cols).arg(templateMat.rows)
                    .arg(pattern.name));
        }
        
        // 그레이스케일로 변환
        cv::Mat grayRoi, grayTemplate;
        cv::cvtColor(processedRoi, grayRoi, cv::COLOR_BGR2GRAY);
        cv::cvtColor(templateMat, grayTemplate, cv::COLOR_BGR2GRAY);
        
        // 특수 이진화 타입인 경우 처리 방식 변경
        cv::Mat binary, templateBinary;
        bool isSpecialThresholdType = (thresholdType == cv::THRESH_TRUNC || 
                                      thresholdType == cv::THRESH_TOZERO || 
                                      thresholdType == cv::THRESH_TOZERO_INV);
        
        // 적응형 이진화 타입인 경우
        bool isAdaptiveThreshold = (thresholdType == THRESH_ADAPTIVE_MEAN || 
                                  thresholdType == THRESH_ADAPTIVE_GAUSSIAN);
        
        // 이진화 처리
        if (isAdaptiveThreshold) {
            // 적응형 이진화 처리
            int adaptiveMethod = (thresholdType == THRESH_ADAPTIVE_MEAN) ? 
                cv::ADAPTIVE_THRESH_MEAN_C : cv::ADAPTIVE_THRESH_GAUSSIAN_C;
            
            cv::adaptiveThreshold(grayRoi, binary, 255, adaptiveMethod,
                               cv::THRESH_BINARY, blockSize, C);
            cv::adaptiveThreshold(grayTemplate, templateBinary, 255, adaptiveMethod,
                               cv::THRESH_BINARY, blockSize, C);
        }
        else if (isSpecialThresholdType) {
            // 특수 이진화 타입 처리 (TRUNC, TOZERO, TOZERO_INV)
            cv::threshold(grayRoi, binary, threshold, 255, thresholdType);
            cv::threshold(grayTemplate, templateBinary, threshold, 255, thresholdType);
            
            // 픽셀값 기반 비교를 위해 다시 일반 이진화로 변환
            // 0이 아닌 픽셀을 모두 흰색(255)으로, 0인 픽셀은 그대로 0으로 변환
            cv::Mat tempBinary, tempTemplateBinary;
            cv::threshold(binary, tempBinary, 0, 255, cv::THRESH_BINARY);
            cv::threshold(templateBinary, tempTemplateBinary, 0, 255, cv::THRESH_BINARY);
            binary = tempBinary;
            templateBinary = tempTemplateBinary;
        }
        else {
            // 일반 이진화 (BINARY, BINARY_INV)
            cv::threshold(grayRoi, binary, threshold, 255, thresholdType);
            cv::threshold(grayTemplate, templateBinary, threshold, 255, thresholdType);
        }
        
        // 디버깅을 위한 이진화 결과 로그
        int binaryWhitePixels = cv::countNonZero(binary);
        int templateWhitePixels = cv::countNonZero(templateBinary);
        int totalPixels = binary.rows * binary.cols;
        
        logDebug(QString("이진화 결과 - 현재: %1% 흰색, 템플릿: %2% 흰색, 임계값: %3, 타입: %4")
                .arg((double)binaryWhitePixels / totalPixels * 100.0, 0, 'f', 1)
                .arg((double)templateWhitePixels / totalPixels * 100.0, 0, 'f', 1)
                .arg(threshold)
                .arg(thresholdType));
        
        // 일치 및 불일치 픽셀 계산
        cv::Mat matchingPixels, diffPixels;
        cv::bitwise_and(binary, templateBinary, matchingPixels);  // 둘 다 1인 픽셀 (일치하는 흰색)
        cv::bitwise_xor(binary, templateBinary, diffPixels);      // 서로 다른 픽셀 (불일치)
        
        // 일치율 계산 방법 1: 전체 픽셀 대비 동일한 픽셀 비율
        int samePixels = totalPixels - cv::countNonZero(diffPixels);
        double matchRatio = (double)samePixels / totalPixels;
        
        // 일치율 계산 방법 2: 흰색 픽셀 일치도
        double whiteMatchRatio = 0.0;
        if (templateWhitePixels > 0) {
            int matchingWhitePixels = cv::countNonZero(matchingPixels);
            whiteMatchRatio = (double)matchingWhitePixels / templateWhitePixels;
        }
        
        // 최종 점수 계산 (두 방법의 가중 평균)
        score = 0.7 * matchRatio + 0.3 * whiteMatchRatio;
        
        // 결과 시각화 이미지 생성
        cv::Mat resultImage = cv::Mat::zeros(binary.size(), CV_8UC3);
        
        // 색상 코드 정의 (BGR 순서)
        cv::Vec3b colorMatch(0, 255, 0);          // 녹색: 일치하는 흰색 픽셀
        cv::Vec3b colorOnlyTemplate(0, 0, 255);   // 빨간색: 템플릿에만 있는 흰색 픽셀
        cv::Vec3b colorOnlyCurrent(255, 0, 0);    // 파란색: 현재 이미지에만 있는 흰색 픽셀
        cv::Vec3b colorBlack(30, 30, 30);         // 어두운 회색: 모두 검은색인 픽셀
        
        for (int y = 0; y < binary.rows; y++) {
            for (int x = 0; x < binary.cols; x++) {
                bool isBinaryWhite = binary.at<uchar>(y, x) > 0;
                bool isTemplateWhite = templateBinary.at<uchar>(y, x) > 0;
                
                if (isBinaryWhite && isTemplateWhite) {
                    // 둘 다 흰색 - 일치
                    resultImage.at<cv::Vec3b>(y, x) = colorMatch;
                } else if (!isBinaryWhite && !isTemplateWhite) {
                    // 둘 다 검은색 - 일치
                    resultImage.at<cv::Vec3b>(y, x) = colorBlack;
                } else if (isTemplateWhite) {
                    // 템플릿만 흰색 - 불일치
                    resultImage.at<cv::Vec3b>(y, x) = colorOnlyTemplate;
                } else {
                    // 현재만 흰색 - 불일치
                    resultImage.at<cv::Vec3b>(y, x) = colorOnlyCurrent;
                }
            }
        }
        
        // 결과 저장
        result.insProcessedImages[pattern.id] = resultImage;
        result.insMethodTypes[pattern.id] = InspectionMethod::BINARY;
        
        // 비교 방식에 따른 결과 판단
        bool passed = false;
        switch (pattern.compareMethod) {
            case 0:  // 이상 (>=)
                passed = (score >= pattern.passThreshold);
                break;
            case 1:  // 이하 (<=)
                passed = (score <= pattern.passThreshold);
                break;
            case 2:  // 범위 내 (lowerThreshold <= score <= upperThreshold)
                passed = (score >= pattern.lowerThreshold && score <= pattern.upperThreshold);
                break;
            default:
                passed = (score >= pattern.passThreshold);
                break;
        }
        
        logDebug(QString("이진화 검사 결과 - 패턴: '%1', 전체 일치율: %2, 흰색 일치율: %3, 최종 점수: %4, 임계값: %5, 타입: %6, 결과: %7")
        .arg(pattern.name)
        .arg(matchRatio, 0, 'f', 4)
        .arg(whiteMatchRatio, 0, 'f', 4)
        .arg(score, 0, 'f', 4)
        .arg(pattern.passThreshold, 0, 'f', 4)
        .arg(thresholdType)
        .arg(passed ? "합격" : "불합격"));

        // 색상 코드 설명 추가
        logDebug(QString("이진화 결과 시각화 - 패턴: '%1', 녹색: 일치 흰색 픽셀, 빨간색: 템플릿에만 있는 픽셀, 파란색: 현재에만 있는 픽셀")
                .arg(pattern.name));
                
        return passed;
    } catch (const cv::Exception& e) {
        logDebug(QString("이진화 검사 중 OpenCV 예외 발생: '%1' - 패턴 '%2'").arg(e.what()).arg(pattern.name));
        score = 0.0;
        return false;
    } catch (const std::exception& e) {
        logDebug(QString("이진화 검사 중 일반 예외 발생: '%1' - 패턴 '%2'").arg(e.what()).arg(pattern.name));
        score = 0.0;
        return false;
    } catch (...) {
        logDebug(QString("이진화 검사 중 알 수 없는 예외 발생 - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        return false;
    }
}

bool InsProcessor::checkEdge(const cv::Mat& image, const PatternInfo& pattern, double& score, InspectionResult& result) {
    cv::Mat roi = extractROI(image, pattern.rect, pattern.angle);
    if (roi.empty()) {
        logDebug(QString("엣지 검사 실패: ROI 추출 실패 - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        return false;
    }
    
    try {
        // ROI 복사본 생성 (필터 적용용)
        cv::Mat processedRoi = roi.clone();
        
        // ROI에 패턴에 적용된 필터 적용
        if (!pattern.filters.isEmpty()) {
            logDebug(QString("ROI에 %1개 필터 적용").arg(pattern.filters.size()));
            
            // 필터 순차 적용
            ImageProcessor processor;
            for (const FilterInfo& filter : pattern.filters) {
                if (filter.enabled) {
                    cv::Mat filteredRoi;
                    processor.applyFilter(processedRoi, filteredRoi, filter);
                    if (!filteredRoi.empty()) {
                        processedRoi = filteredRoi.clone();
                        logDebug(QString("필터 %1 적용 완료").arg(filter.type));
                    }
                }
            }
        }
        
        // 그레이스케일 변환
        cv::Mat gray;
        if (processedRoi.channels() == 3) {
            cv::cvtColor(processedRoi, gray, cv::COLOR_BGR2GRAY);
        } else {
            processedRoi.copyTo(gray);
        }
        
        // 템플릿 이미지가 있는지 확인
        if (pattern.templateImage.isNull()) {
            logDebug(QString("엣지 검사 실패: 템플릿 이미지가 없음 - 패턴 '%1'").arg(pattern.name));
            
            // 템플릿 없이 기존 방식으로 엣지 비율 계산
            cv::Mat edges;
            int threshold1 = pattern.binaryThreshold / 2;
            int threshold2 = pattern.binaryThreshold;
            
            cv::Canny(gray, edges, threshold1, threshold2);
            
            int totalPixels = edges.rows * edges.cols;
            int edgePixels = cv::countNonZero(edges);
            double ratio = (double)edgePixels / totalPixels;
            score = ratio;
            
            // 비교 방식에 따른 결과 판단
            bool passed = (score >= pattern.passThreshold);
            if (pattern.compareMethod == 1) passed = (score <= pattern.passThreshold);
            else if (pattern.compareMethod == 2) passed = (score >= pattern.lowerThreshold && score <= pattern.upperThreshold);
            
            result.insProcessedImages[pattern.id] = edges.clone();
            result.insMethodTypes[pattern.id] = InspectionMethod::EDGE;
            
            return passed;
        }
        
        // 템플릿 이미지를 Mat으로 변환
        QImage qTemplateImage = pattern.templateImage.convertToFormat(QImage::Format_RGB888);
        cv::Mat templateMat;
        
        if (qTemplateImage.format() == QImage::Format_RGB888) {
            templateMat = cv::Mat(qTemplateImage.height(), qTemplateImage.width(), 
                              CV_8UC3, const_cast<uchar*>(qTemplateImage.bits()), qTemplateImage.bytesPerLine());
            templateMat = templateMat.clone();
            cv::cvtColor(templateMat, templateMat, cv::COLOR_RGB2BGR);
        } else {
            logDebug(QString("엣지 검사 실패: 이미지 형식 변환 실패 - 패턴 '%1'").arg(pattern.name));
            score = 0.0;
            return false;
        }
        
        // 템플릿 이미지 그레이스케일 변환
        cv::Mat templateGray;
        if (templateMat.channels() == 3) {
            cv::cvtColor(templateMat, templateGray, cv::COLOR_BGR2GRAY);
        } else {
            templateMat.copyTo(templateGray);
        }
        
        // 크기 확인 (이제 동일한 방식으로 추출되므로 크기가 같아야 함)
        if (templateGray.size() != gray.size()) {
            logDebug(QString("엣지 검사 경고: ROI(%1x%2)와 템플릿(%3x%4) 크기 불일치 - 패턴 '%5'")
                    .arg(gray.cols).arg(gray.rows)
                    .arg(templateGray.cols).arg(templateGray.rows)
                    .arg(pattern.name));
        }
        
        // 엣지 추출 (현재 이미지와 템플릿 모두)
        cv::Mat edges, templateEdges;
        int threshold1 = pattern.binaryThreshold / 2;
        int threshold2 = pattern.binaryThreshold;
        
        cv::Canny(gray, edges, threshold1, threshold2);
        cv::Canny(templateGray, templateEdges, threshold1, threshold2);
        
        // 엣지 좌표 추출
        std::vector<cv::Point> edgePoints, templateEdgePoints;
        
        for (int y = 0; y < edges.rows; y++) {
            for (int x = 0; x < edges.cols; x++) {
                if (edges.at<uchar>(y, x) > 0) {
                    edgePoints.push_back(cv::Point(x, y));
                }
                if (templateEdges.at<uchar>(y, x) > 0) {
                    templateEdgePoints.push_back(cv::Point(x, y));
                }
            }
        }
        
        // 충분한 엣지 포인트가 있는지 확인
        if (edgePoints.empty() || templateEdgePoints.empty()) {
            logDebug(QString("엣지 검사 실패: 엣지 포인트가 충분하지 않음 - 현재(%1), 템플릿(%2)")
                    .arg(edgePoints.size())
                    .arg(templateEdgePoints.size()));
            score = 0.0;
            
            // 결과 이미지 저장
            cv::Mat resultEdges;
            if (edgePoints.empty()) {
                templateEdges.copyTo(resultEdges);
            } else {
                edges.copyTo(resultEdges);
            }
            result.insProcessedImages[pattern.id] = resultEdges;
            result.insMethodTypes[pattern.id] = InspectionMethod::EDGE;
            
            return false;
        }
        
        // 방법 1: 엣지 맵 비교 - 두 엣지 맵의 유사도 계산
        cv::Mat diffEdges;
        cv::bitwise_xor(edges, templateEdges, diffEdges);  // XOR로 차이 계산
        int diffPixels = cv::countNonZero(diffEdges);      // 차이 픽셀 수
        int totalPixels = edges.rows * edges.cols;         // 전체 픽셀 수
        
        // 유사도 점수 계산 (1에서 차이 비율 빼기)
        double similarityScore = 1.0 - (double)diffPixels / totalPixels;
        
        // 방법 2: Chamfer 매칭 - 엣지 간의 거리 변환 맵 기반 매칭
        cv::Mat distanceMap;
        cv::distanceTransform(~templateEdges, distanceMap, cv::DIST_L2, 3);
        
        // 평균 거리 계산
        double totalDistance = 0.0;
        for (const cv::Point& pt : edgePoints) {
            if (pt.x >= 0 && pt.x < distanceMap.cols && pt.y >= 0 && pt.y < distanceMap.rows) {
                totalDistance += distanceMap.at<float>(pt.y, pt.x);
            }
        }
        
        // 정규화된 Chamfer 거리 점수 (낮을수록 더 유사)
        double maxDistance = sqrt(edges.rows * edges.rows + edges.cols * edges.cols);
        double chamferScore = 1.0 - (totalDistance / edgePoints.size()) / maxDistance;
        
        // 두 방법의 점수 조합
        score = 0.7 * similarityScore + 0.3 * chamferScore;
        
        // 비교 방식에 따른 결과 판단
        bool passed = false;
        switch (pattern.compareMethod) {
            case 0:  // 이상 (>=)
                passed = (score >= pattern.passThreshold);
                break;
            case 1:  // 이하 (<=)
                passed = (score <= pattern.passThreshold);
                break;
            case 2:  // 범위 내
                passed = (score >= pattern.lowerThreshold && score <= pattern.upperThreshold);
                break;
            default:
                passed = (score >= pattern.passThreshold);
                break;
        }
        
        // 결과 시각화 이미지 생성
        cv::Mat visualEdges;
        cv::cvtColor(edges, visualEdges, cv::COLOR_GRAY2BGR);
        
        // 현재 엣지는 빨간색, 템플릿 엣지는 녹색으로 표시
        for (int y = 0; y < edges.rows; y++) {
            for (int x = 0; x < edges.cols; x++) {
                if (edges.at<uchar>(y, x) > 0) {
                    visualEdges.at<cv::Vec3b>(y, x) = cv::Vec3b(0, 0, 255);  // 빨간색
                }
                if (templateEdges.at<uchar>(y, x) > 0) {
                    visualEdges.at<cv::Vec3b>(y, x) = cv::Vec3b(0, 255, 0);  // 녹색
                }
                if (edges.at<uchar>(y, x) > 0 && templateEdges.at<uchar>(y, x) > 0) {
                    visualEdges.at<cv::Vec3b>(y, x) = cv::Vec3b(255, 255, 0); // 노란색 (일치)
                }
            }
        }
        
        // 결과 이미지 저장
        result.insProcessedImages[pattern.id] = visualEdges;
        result.insMethodTypes[pattern.id] = InspectionMethod::EDGE;
        
        // 디버그 출력
        logDebug(QString("엣지 검사 결과 - 패턴: '%1', 유사도: %2, XOR 점수: %3, Chamfer 점수: %4, 임계값: %5, 결과: %6")
                .arg(pattern.name)
                .arg(score, 0, 'f', 4)
                .arg(similarityScore, 0, 'f', 4)
                .arg(chamferScore, 0, 'f', 4)
                .arg(pattern.passThreshold, 0, 'f', 4)
                .arg(passed ? "합격" : "불합격"));
        
        return passed;
        
    } catch (const cv::Exception& e) {
        logDebug(QString("엣지 검사 중 OpenCV 예외 발생: '%1' - 패턴 '%2'").arg(e.what()).arg(pattern.name));
        score = 0.0;
        return false;
    } catch (const std::exception& e) {
        logDebug(QString("엣지 검사 중 일반 예외 발생: '%1' - 패턴 '%2'").arg(e.what()).arg(pattern.name));
        score = 0.0;
        return false;
    } catch (...) {
        logDebug(QString("엣지 검사 중 알 수 없는 예외 발생 - 패턴 '%1'").arg(pattern.name));
        score = 0.0;
        return false;
    }
}

// 간단한 AI 기반 매칭 스텁 (확장 포인트)
bool InsProcessor::checkAiMatch1(const cv::Mat& image, const PatternInfo& pattern, double& score, InspectionResult& result) {
    // AI_MATCH1 다중 영역 검사 호출 구현
    logDebug(QString("checkAiMatch1: AI_MATCH1 다중 검사 시작 (스캔용 패턴: %1)").arg(pattern.name));

    // 이 함수는 단일 패턴 호출을 허용하지만, 여기서는 performInspection에서
    // 모든 AI_MATCH1 패턴을 모아 한 번에 호출하도록 되어 있으므로, 기본 동작은
    // 단일 패턴에 대해서는 로컬 API를 호출하지 않고 실패를 반환합니다.
    // 대신 performInspection에서 일괄 호출을 수행하도록 구현되어야 합니다.

    // 표시: 현재 위치에서는 단일 패턴 검사 요청일 경우에는 stub 동작.
    result.insMethodTypes[pattern.id] = InspectionMethod::AI_MATCH1;
    score = 0.0;
    return false;
}

void InsProcessor::logDebug(const QString& message) {
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString formattedMessage = QString("\"%1\" - \"%2\"").arg(timestamp).arg(message);
    emit logMessage(formattedMessage);
}

cv::Mat InsProcessor::extractROI(const cv::Mat& image, const QRectF& rect, double angle, bool isTemplate) {
    try {
        cv::Mat roiMat;
        
        // 중심점 계산
        cv::Point2f center(rect.x() + rect.width()/2.0f, rect.y() + rect.height()/2.0f);
        
        // 회전각에 따른 최소 필요 사각형 크기 계산
        double angleRad = std::abs(angle) * M_PI / 180.0;
        double width = rect.width();
        double height = rect.height();
        
        // 회전된 사각형의 경계 상자 크기 계산
        double rotatedWidth = std::abs(width * std::cos(angleRad)) + std::abs(height * std::sin(angleRad));
        double rotatedHeight = std::abs(width * std::sin(angleRad)) + std::abs(height * std::cos(angleRad));
        
        // 정사각형 크기는 회전된 경계 상자 중 더 큰 값 + 여유분
        int maxSize = static_cast<int>(std::max(rotatedWidth, rotatedHeight)) + 10;
        
        // 정사각형 ROI 영역 계산 (중심점 기준)
        int halfSize = maxSize / 2;
        cv::Rect squareRoi(
            static_cast<int>(center.x) - halfSize,
            static_cast<int>(center.y) - halfSize,
            maxSize,
            maxSize
        );
        
        // 이미지 경계와 교집합 구하기
        cv::Rect imageBounds(0, 0, image.cols, image.rows);
        cv::Rect validRoi = squareRoi & imageBounds;
        
        if (validRoi.width > 0 && validRoi.height > 0) {
            // 정사각형 결과 이미지 생성 (검은색 배경 - 티칭과 동일)
            roiMat = cv::Mat::zeros(maxSize, maxSize, image.type());
            
            // 유효한 영역만 복사
            int offsetX = validRoi.x - squareRoi.x;
            int offsetY = validRoi.y - squareRoi.y;
            
            cv::Mat validImage = image(validRoi);
            cv::Rect resultRect(offsetX, offsetY, validRoi.width, validRoi.height);
            validImage.copyTo(roiMat(resultRect));
            
            // 패턴 영역 외부 마스킹 (패턴 영역만 보이도록)
            cv::Mat mask = cv::Mat::zeros(maxSize, maxSize, CV_8UC1);
            
            // 정사각형 중심을 기준으로 패턴 영역 계산
            cv::Point2f patternCenter(maxSize / 2.0f, maxSize / 2.0f);
            cv::Size2f patternSize(rect.width(), rect.height());
            
            if (std::abs(angle) > 0.1) {
                // 회전된 패턴의 경우: 회전된 사각형 마스크
                cv::Point2f vertices[4];
                cv::RotatedRect rotatedRect(patternCenter, patternSize, angle);
                rotatedRect.points(vertices);
                
                std::vector<cv::Point> points;
                for (int i = 0; i < 4; i++) {
                    points.push_back(cv::Point(static_cast<int>(vertices[i].x), 
                                             static_cast<int>(vertices[i].y)));
                }
                
                cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{points}, cv::Scalar(255));
            } else {
                // 회전 없는 경우: 일반 사각형 마스크
                cv::Rect patternRect(
                    static_cast<int>(patternCenter.x - patternSize.width / 2),
                    static_cast<int>(patternCenter.y - patternSize.height / 2),
                    static_cast<int>(patternSize.width),
                    static_cast<int>(patternSize.height)
                );
                cv::rectangle(mask, patternRect, cv::Scalar(255), -1);
            }
            
            // 마스크 반전: 패턴 영역 외부를 검은색으로 설정
            cv::Mat invertedMask;
            cv::bitwise_not(mask, invertedMask);
            
            // 패턴 영역 외부를 검은색으로 마스킹 (티칭과 동일)
            roiMat.setTo(cv::Scalar(0, 0, 0), invertedMask);
            
            return roiMat;
        }
        
        return cv::Mat();
        
    } catch (const cv::Exception& e) {
        logDebug(QString("ROI 추출 중 OpenCV 예외 발생: %1").arg(e.what()));
        return cv::Mat();
    } catch (...) {
        logDebug("ROI 추출 중 알 수 없는 예외 발생");
        return cv::Mat();
    }
}

QImage InsProcessor::matToQImage(const cv::Mat& mat) {
    if (mat.empty()) {
        return QImage();
    }

    switch (mat.type()) {
    case CV_8UC4: {
        // BGRA
        QImage qimg(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_ARGB32);
        return qimg.rgbSwapped();
    }
    case CV_8UC3: {
        // BGR
        QImage qimg(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_RGB888);
        return qimg.rgbSwapped();
    }
    case CV_8UC1: {
        // 그레이스케일
        return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8);
    }
    default:
        // 다른 형식은 BGR로 변환
        cv::Mat convertedMat;
        if (mat.channels() == 1) {
            cv::cvtColor(mat, convertedMat, cv::COLOR_GRAY2BGR);
        } else {
            mat.convertTo(convertedMat, CV_8UC3);
        }
        QImage qimg(convertedMat.data, convertedMat.cols, convertedMat.rows, 
                   convertedMat.step, QImage::Format_RGB888);
        return qimg.rgbSwapped();
    }
}

bool InsProcessor::checkStrip(const cv::Mat& image, const PatternInfo& pattern, double& score, InspectionResult& result) {
    try {
        // ROI 영역 추출
        cv::Mat roiImage = extractROI(image, pattern.rect, pattern.angle);
        if (roiImage.empty()) {
            logDebug(QString("STRIP 길이 검사 실패: ROI 추출 실패 - %1").arg(pattern.name));
            score = 0.0;
            result.insMethodTypes[pattern.id] = InspectionMethod::STRIP;
            return false;
        }
        
        // 템플릿 이미지 로드 (패턴에 저장된 템플릿 이미지 사용)
        cv::Mat templateImage;
        if (!pattern.templateImage.isNull()) {
            // QImage를 cv::Mat으로 변환
            QImage qImg = pattern.templateImage.convertToFormat(QImage::Format_RGB888);
            templateImage = cv::Mat(qImg.height(), qImg.width(), CV_8UC3, (void*)qImg.constBits(), qImg.bytesPerLine());
            templateImage = templateImage.clone(); // 데이터 복사
            cv::cvtColor(templateImage, templateImage, cv::COLOR_RGB2BGR);
        } else {
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
        
        // 디버그: STRIP 검사 임계값 확인
        logDebug(QString("=== STRIP 검사 임계값: pattern.passThreshold = %1 ===")
                .arg(pattern.passThreshold, 0, 'f', 3));
                
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
                
    // performStripInspection 호출을 간소화: PatternInfo 전체를 전달
    bool isPassed = ImageProcessor::performStripInspection(roiImage, templateImage,
                                  pattern,
                                  score, startPoint, maxGradientPoint, gradientPoints, resultImage);
    
    // ROI 좌표를 원본 이미지 좌표로 변환
    cv::Point2f patternCenter(pattern.rect.x() + pattern.rect.width()/2.0f, 
                            pattern.rect.y() + pattern.rect.height()/2.0f);
    cv::Point2f roiCenter(roiImage.cols / 2.0f, roiImage.rows / 2.0f);
    cv::Point2f offset = patternCenter - roiCenter;
    
    // OpenCV에서 검출된 gradientPoints를 사용 (4개 포인트)
    QPoint absPoint1, absPoint2, absPoint3, absPoint4;
    
    if (gradientPoints.size() >= 4) {
        // OpenCV gradientPoints 순서 재정렬 - 올바른 위치로 매핑
        std::vector<cv::Point> orderedPoints = {
            gradientPoints[0],  // Point 1: 왼쪽 위
            gradientPoints[2],  // Point 2: 왼쪽 아래 
            gradientPoints[1],  // Point 3: 오른쪽 위
            gradientPoints[3]   // Point 4: 오른쪽 아래
        };
        
        qDebug() << "OpenCV 검출 STRIP 4점 (회전 전) - P1:" << QPoint(orderedPoints[0].x, orderedPoints[0].y) 
                 << "P2:" << QPoint(orderedPoints[1].x, orderedPoints[1].y) 
                 << "P3:" << QPoint(orderedPoints[2].x, orderedPoints[2].y) 
                 << "P4:" << QPoint(orderedPoints[3].x, orderedPoints[3].y);
        
        // 유틸리티 함수를 사용하여 점들을 변환
        QList<QPoint> transformedPoints = InsProcessor::transformPatternPoints(
            orderedPoints, 
            roiImage.size(), 
            pattern.angle, 
            offset
        );
        
        // 변환된 점들을 할당
        if (transformedPoints.size() >= 4) {
            absPoint1 = transformedPoints[0];
            absPoint2 = transformedPoints[1];
            absPoint3 = transformedPoints[2];
            absPoint4 = transformedPoints[3];
        }
    } else {
        qDebug() << "경고: gradientPoints 개수 부족 (" << gradientPoints.size() << "/4)";
        return true; // 검출 실패시 early return
    }
    
    // InspectionResult에 저장
    result.stripPoint1[pattern.id] = absPoint1;
    result.stripPoint2[pattern.id] = absPoint2;
    result.stripPoint3[pattern.id] = absPoint3;
    result.stripPoint4[pattern.id] = absPoint4;
    result.stripPointsValid[pattern.id] = true;
    
    // 기울기 계산 (Point 1-3, Point 2-4)
    double slope13 = static_cast<double>(absPoint3.y() - absPoint1.y()) / static_cast<double>(absPoint3.x() - absPoint1.x());
    double slope24 = static_cast<double>(absPoint4.y() - absPoint2.y()) / static_cast<double>(absPoint4.x() - absPoint2.x());
    result.stripSlope13[pattern.id] = slope13;
    result.stripSlope24[pattern.id] = slope24;
    
    qDebug() << "=== STRIP 4점 좌표 변환 ===";
    qDebug() << "패턴 중심:" << QPointF(patternCenter.x, patternCenter.y);
    qDebug() << "ROI 중심:" << QPointF(roiCenter.x, roiCenter.y); 
    qDebug() << "오프셋:" << QPointF(offset.x, offset.y);
    qDebug() << "변환된 절대좌표 - P1:" << absPoint1 << "P2:" << absPoint2 << "P3:" << absPoint3 << "P4:" << absPoint4;
    qDebug() << "기울기 - S13:" << slope13 << "S24:" << slope24;
    
    if (isPassed) {
        // 좌표 변환 적용
        startPoint.x += static_cast<int>(offset.x);
        startPoint.y += static_cast<int>(offset.y);
        maxGradientPoint.x += static_cast<int>(offset.x);
        maxGradientPoint.y += static_cast<int>(offset.y);
        
        for (auto& point : gradientPoints) {
            point.x += static_cast<int>(offset.x);
            point.y += static_cast<int>(offset.y);
        }
    }
                                                              
        // 측정된 두께를 검사 결과에 저장 (FRONT + REAR)
        result.stripMeasuredThicknessMin[pattern.id] = measuredMinThickness;
        result.stripMeasuredThicknessMax[pattern.id] = measuredMaxThickness;
        result.stripMeasuredThicknessAvg[pattern.id] = measuredAvgThickness;
        result.stripThicknessMeasured[pattern.id] = (measuredAvgThickness > 0);
        
        result.stripRearMeasuredThicknessMin[pattern.id] = rearMeasuredMinThickness;
        result.stripRearMeasuredThicknessMax[pattern.id] = rearMeasuredMaxThickness;
        result.stripRearMeasuredThicknessAvg[pattern.id] = rearMeasuredAvgThickness;
        result.stripRearThicknessMeasured[pattern.id] = (rearMeasuredAvgThickness > 0);
        
        // 박스 위치를 패턴 중심 기준 상대좌표로 저장
        QPointF patternCenterForBox = pattern.rect.center();
        
        // FRONT 박스 상대좌표 계산 (패턴 왼쪽 끝에서부터의 퍼센트 위치)
        float startPercent = pattern.stripGradientStartPercent / 100.0f;
        float frontBoxOffsetX = (startPercent - 0.5f) * pattern.rect.width(); // 왼쪽 끝에서의 위치를 중심 기준으로 변환
        QPointF frontBoxRelativeCenter(frontBoxOffsetX, 0); // Y는 패턴 중심과 동일
        result.stripFrontBoxCenter[pattern.id] = frontBoxRelativeCenter;
        result.stripFrontBoxSize[pattern.id] = QSizeF(pattern.stripThicknessBoxWidth, pattern.stripThicknessBoxHeight);
        
        // REAR 박스 상대좌표 계산 (패턴 왼쪽 끝에서부터의 퍼센트 위치)
        float endPercent = pattern.stripGradientEndPercent / 100.0f;
        float rearBoxOffsetX = (endPercent - 0.5f) * pattern.rect.width(); // 왼쪽 끝에서의 위치를 중심 기준으로 변환
        QPointF rearBoxRelativeCenter(rearBoxOffsetX, 0); // Y는 패턴 중심과 동일
        result.stripRearBoxCenter[pattern.id] = rearBoxRelativeCenter;
        result.stripRearBoxSize[pattern.id] = QSizeF(pattern.stripRearThicknessBoxWidth, pattern.stripRearThicknessBoxHeight);
        
        // EDGE 검사 결과 저장
        result.edgeResults[pattern.id] = edgePassed;
        result.edgeIrregularityCount[pattern.id] = edgeIrregularityCount;
        result.edgeMaxDeviation[pattern.id] = edgeMaxDeviation;
        
        // EDGE 박스 상대좌표 계산 (패턴 왼쪽에서 edgeOffsetX만큼 떨어진 위치)
        float edgeOffsetX = (-pattern.rect.width()/2.0f) + pattern.edgeOffsetX; // 중심 기준 오프셋
        QPointF edgeBoxRelativeCenter(edgeOffsetX, 0); // Y는 패턴 중심과 동일
        result.edgeBoxCenter[pattern.id] = edgeBoxRelativeCenter;
        result.edgeBoxSize[pattern.id] = QSizeF(pattern.edgeBoxWidth, pattern.edgeBoxHeight);
        
        result.edgeMeasured[pattern.id] = pattern.edgeEnabled;
        result.edgeAverageX[pattern.id] = edgeAverageX;
        
        // Qt로 시각화 추가 (시작점, 끝점, Local Max Gradient 지점들)
        if (!resultImage.empty()) {
            // 결과 이미지를 QImage로 변환
            QImage qResultImage = matToQImage(resultImage);
            QPainter painter(&qResultImage);
            painter.setRenderHint(QPainter::Antialiasing);
            
            int verticalHeight = 15;
            
            // 가장 오른쪽 Local Max Gradient 지점만 표시 (빨간색 세로선)
            if (!gradientPoints.empty()) {
                // gradientPoints에서 X좌표가 가장 큰(오른쪽) 지점 찾기
                cv::Point rightmostPoint = gradientPoints[0];
                for (const cv::Point& gradPoint : gradientPoints) {
                    if (gradPoint.x > rightmostPoint.x) {
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
                if (gradientPoints.size() > 747) { // 상단 + 하단이 있다면
                    // gradientPoints의 후반부 (하단 컨투어)에서 Y가 가장 큰 점 찾기
                    cv::Point bottomMaxGrad = gradientPoints[747]; // 하단 컨투어 시작점
                    for (size_t i = 747; i < gradientPoints.size(); i++) {
                        if (gradientPoints[i].y > bottomMaxGrad.y) {
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
                int pixelDistance = static_cast<int>(sqrt(dx*dx + dy*dy));
                int midX = (rotatedStart.x + rotatedMaxGrad.x) / 2;
                int midY = (rotatedStart.y + rotatedMaxGrad.y) / 2 - 20; // 선 위쪽에 표시
                
                painter.setPen(QPen(QColor(0, 255, 0), 1)); // 초록색 텍스트
                painter.setFont(QFont("Arial", 12, QFont::Bold));
                QString distanceText = QString("길이: %1px").arg(pixelDistance);
                painter.drawText(midX - 35, midY, distanceText);
                
                // 두께 측정 시각화 (Max Gradient 지점 기준 좌우 100px 위치에서)
                if (leftThickness > 0 || rightThickness > 0) {
                    // 길이 방향 벡터 계산
                    double lengthVecX = actualMaxGradient.x - startPoint.x;
                    double lengthVecY = actualMaxGradient.y - startPoint.y;
                    double lengthMag = sqrt(lengthVecX * lengthVecX + lengthVecY * lengthVecY);
                    
                    if (lengthMag > 0) {
                        // 정규화된 길이 방향 벡터
                        double normLengthX = lengthVecX / lengthMag;
                        double normLengthY = lengthVecY / lengthMag;
                        
                        // 두께 측정을 위한 수직 방향 벡터 (FID 각도 적용)
                        double perpX, perpY;
                        
                        // FID 각도가 있으면 적용, 없으면 길이 방향에 수직으로 계산
                        if (std::abs(pattern.angle) > 0.1) {
                            // FID 각도를 라디안으로 변환
                            double angleRad = pattern.angle * CV_PI / 180.0;
                            // FID 각도에서 90도 회전한 방향 (두께 측정 방향)
                            double thicknessAngle = angleRad + CV_PI/2;
                            perpX = cos(thicknessAngle);
                            perpY = sin(thicknessAngle);
                        } else {
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
                        if (roiImage.channels() == 3) {
                            cv::cvtColor(roiImage, grayForMeasure, cv::COLOR_BGR2GRAY);
                        } else {
                            roiImage.copyTo(grayForMeasure);
                        }
                        
                        const int maxSearchDistance = 100;
                        const int thresholdDiff = 30;
                        
                        // 좌측 위치에서 두께 측정 (20픽셀 범위로 평균 계산)
                        int actualLeftThickness = 0;
                        
                        if (leftMeasureX >= 10 && leftMeasureX < grayForMeasure.cols - 10 && 
                            leftMeasureY >= 0 && leftMeasureY < grayForMeasure.rows) {
                            
                            std::vector<int> thicknessMeasurements;
                            std::vector<std::pair<cv::Point, cv::Point>> thicknessPoints; // 각 측정의 상하 끝점들
                            
                            // 좌우 10픽셀씩, 총 20픽셀 범위에서 측정
                            for (int offsetX = -10; offsetX <= 10; offsetX++) {
                                int measureX = leftMeasureX + offsetX;
                                int measureY = leftMeasureY;
                                
                                if (measureX < 0 || measureX >= grayForMeasure.cols) continue;
                                
                                int centerIntensity = grayForMeasure.at<uchar>(measureY, measureX);
                                
                                // 위아래로 스캔해서 검은색을 만날 때까지 픽셀 수 계산
                                int upThickness = 0, downThickness = 0;
                                cv::Point topPoint(measureX, measureY);
                                cv::Point bottomPoint(measureX, measureY);
                                
                                // 위쪽으로 탐색
                                for (int i = 1; i <= maxSearchDistance; i++) {
                                    int searchX = measureX + static_cast<int>(perpX * (-i));
                                    int searchY = measureY + static_cast<int>(perpY * (-i));
                                    
                                    if (searchX < 0 || searchX >= grayForMeasure.cols || 
                                        searchY < 0 || searchY >= grayForMeasure.rows) {
                                        break;
                                    }
                                    
                                    int intensity = grayForMeasure.at<uchar>(searchY, searchX);
                                    if (abs(intensity - centerIntensity) > thresholdDiff) {
                                        upThickness = i;
                                        topPoint = cv::Point(searchX, searchY);
                                        break;
                                    }
                                }
                                
                                // 아래쪽으로 탐색
                                for (int i = 1; i <= maxSearchDistance; i++) {
                                    int searchX = measureX + static_cast<int>(perpX * i);
                                    int searchY = measureY + static_cast<int>(perpY * i);
                                    
                                    if (searchX < 0 || searchX >= grayForMeasure.cols || 
                                        searchY < 0 || searchY >= grayForMeasure.rows) {
                                        break;
                                    }
                                    
                                    int intensity = grayForMeasure.at<uchar>(searchY, searchX);
                                    if (abs(intensity - centerIntensity) > thresholdDiff) {
                                        downThickness = i;
                                        bottomPoint = cv::Point(searchX, searchY);
                                        break;
                                    }
                                }
                                
                                int totalThickness = upThickness + downThickness;
                                if (totalThickness > 0) {
                                    thicknessMeasurements.push_back(totalThickness);
                                    thicknessPoints.push_back(std::make_pair(topPoint, bottomPoint));
                                }
                            }
                            
                            // 평균 두께 계산 (이상치 제거)
                            if (!thicknessMeasurements.empty()) {
                                // 정렬된 인덱스 생성
                                std::vector<std::pair<int, size_t>> indexedMeasurements;
                                for (size_t i = 0; i < thicknessMeasurements.size(); i++) {
                                    indexedMeasurements.push_back({thicknessMeasurements[i], i});
                                }
                                std::sort(indexedMeasurements.begin(), indexedMeasurements.end());
                                
                                int removeCount = static_cast<int>(indexedMeasurements.size() * 0.2);
                                int startIdx = removeCount;
                                int endIdx = indexedMeasurements.size() - removeCount;
                                
                                if (endIdx > startIdx) {
                                    int sum = 0;
                                    cv::Point avgTop(0, 0), avgBottom(0, 0);
                                    int validCount = 0;
                                    
                                    for (int i = startIdx; i < endIdx; i++) {
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
                                } else {
                                    // 데이터가 적으면 단순 평균
                                    int sum = std::accumulate(thicknessMeasurements.begin(), thicknessMeasurements.end(), 0);
                                    actualLeftThickness = sum / thicknessMeasurements.size();
                                    
                                    cv::Point avgTop(0, 0), avgBottom(0, 0);
                                    for (const auto& points : thicknessPoints) {
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
                            rightMeasureY >= 0 && rightMeasureY < grayForMeasure.rows) {
                            
                            std::vector<int> thicknessMeasurements;
                            std::vector<std::pair<cv::Point, cv::Point>> thicknessPoints; // 각 측정의 상하 끝점들
                            
                            // 좌우 10픽셀씩, 총 20픽셀 범위에서 측정
                            for (int offsetX = -10; offsetX <= 10; offsetX++) {
                                int measureX = rightMeasureX + offsetX;
                                int measureY = rightMeasureY;
                                
                                if (measureX < 0 || measureX >= grayForMeasure.cols) continue;
                                
                                int centerIntensity = grayForMeasure.at<uchar>(measureY, measureX);
                                
                                // 위아래로 스캔해서 검은색을 만날 때까지 픽셀 수 계산
                                int upThickness = 0, downThickness = 0;
                                cv::Point topPoint(measureX, measureY);
                                cv::Point bottomPoint(measureX, measureY);
                                
                                // 위쪽으로 탐색
                                for (int i = 1; i <= maxSearchDistance; i++) {
                                    int searchX = measureX + static_cast<int>(perpX * (-i));
                                    int searchY = measureY + static_cast<int>(perpY * (-i));
                                    
                                    if (searchX < 0 || searchX >= grayForMeasure.cols || 
                                        searchY < 0 || searchY >= grayForMeasure.rows) {
                                        break;
                                    }
                                    
                                    int intensity = grayForMeasure.at<uchar>(searchY, searchX);
                                    if (abs(intensity - centerIntensity) > thresholdDiff) {
                                        upThickness = i;
                                        topPoint = cv::Point(searchX, searchY);
                                        break;
                                    }
                                }
                                
                                // 아래쪽으로 탐색
                                for (int i = 1; i <= maxSearchDistance; i++) {
                                    int searchX = measureX + static_cast<int>(perpX * i);
                                    int searchY = measureY + static_cast<int>(perpY * i);
                                    
                                    if (searchX < 0 || searchX >= grayForMeasure.cols || 
                                        searchY < 0 || searchY >= grayForMeasure.rows) {
                                        break;
                                    }
                                    
                                    int intensity = grayForMeasure.at<uchar>(searchY, searchX);
                                    if (abs(intensity - centerIntensity) > thresholdDiff) {
                                        downThickness = i;
                                        bottomPoint = cv::Point(searchX, searchY);
                                        break;
                                    }
                                }
                                
                                int totalThickness = upThickness + downThickness;
                                if (totalThickness > 0) {
                                    thicknessMeasurements.push_back(totalThickness);
                                    thicknessPoints.push_back(std::make_pair(topPoint, bottomPoint));
                                }
                            }
                            
                            // 평균 두께 계산 (이상치 제거)
                            if (!thicknessMeasurements.empty()) {
                                // 정렬된 인덱스 생성
                                std::vector<std::pair<int, size_t>> indexedMeasurements;
                                for (size_t i = 0; i < thicknessMeasurements.size(); i++) {
                                    indexedMeasurements.push_back({thicknessMeasurements[i], i});
                                }
                                std::sort(indexedMeasurements.begin(), indexedMeasurements.end());
                                
                                int removeCount = static_cast<int>(indexedMeasurements.size() * 0.2);
                                int startIdx = removeCount;
                                int endIdx = indexedMeasurements.size() - removeCount;
                                
                                if (endIdx > startIdx) {
                                    int sum = 0;
                                    cv::Point avgTop(0, 0), avgBottom(0, 0);
                                    int validCount = 0;
                                    
                                    for (int i = startIdx; i < endIdx; i++) {
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
                                } else {
                                    // 데이터가 적으면 단순 평균
                                    int sum = std::accumulate(thicknessMeasurements.begin(), thicknessMeasurements.end(), 0);
                                    actualRightThickness = sum / thicknessMeasurements.size();
                                    
                                    cv::Point avgTop(0, 0), avgBottom(0, 0);
                                    for (const auto& points : thicknessPoints) {
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
                        if (actualLeftThickness > 0) {
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
                        if (actualRightThickness > 0) {
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
                
                logDebug(QString("두께 측정 결과: 좌측=%1px, 우측=%2px, 길이=%3px")
                        .arg(leftThickness).arg(rightThickness).arg(pixelDistance));
                
                // 측정된 두께 정보를 검사 결과에 추가하여 Qt로 표시
                if (result.stripMeasuredThicknessAvg.contains(pattern.id) && 
                    result.stripThicknessMeasured[pattern.id]) {
                    
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
                                 (void*)rgbImage.constBits(), rgbImage.bytesPerLine()).clone();
            cv::cvtColor(resultImage, resultImage, cv::COLOR_RGB2BGR);
        }
        
        // 결과 저장
        result.insProcessedImages[pattern.id] = resultImage.clone();
        result.insMethodTypes[pattern.id] = InspectionMethod::STRIP;
        result.insScores[pattern.id] = score;
        result.insResults[pattern.id] = isPassed;
        
        // 두께 측정 결과 저장
        if (leftThickness > 0 || rightThickness > 0) {
            result.stripThicknessCenters[pattern.id] = maxGradientPoint;
            
            // 실제 두께 측정 위치 계산 (Max Gradient 기준 좌우 100px)
            double lengthVecX = maxGradientPoint.x - startPoint.x;
            double lengthVecY = maxGradientPoint.y - startPoint.y;
            double lengthMag = sqrt(lengthVecX * lengthVecX + lengthVecY * lengthVecY);
            
            if (lengthMag > 0) {
                // 정규화된 길이 방향 벡터
                double normLengthX = lengthVecX / lengthMag;
                double normLengthY = lengthVecY / lengthMag;
                
                // 좌측 측정 중심점 (Max Gradient에서 좌측으로 100px)
                cv::Point leftCenter(
                    static_cast<int>(maxGradientPoint.x - normLengthX * 100),
                    static_cast<int>(maxGradientPoint.y - normLengthY * 100)
                );
                
                // 우측 측정 중심점 (Max Gradient에서 우측으로 100px)
                cv::Point rightCenter(
                    static_cast<int>(maxGradientPoint.x + normLengthX * 100),
                    static_cast<int>(maxGradientPoint.y + normLengthY * 100)
                );
                
                result.stripThicknessLines[pattern.id] = std::make_pair(leftCenter, rightCenter);
                
                // 상세 두께 측정 좌표 저장 (좌측 상하점, 우측 상하점)
                std::vector<std::pair<cv::Point, cv::Point>> thicknessDetails;
                if (leftThickness > 0) {
                    thicknessDetails.push_back(std::make_pair(leftTopPoint, leftBottomPoint));
                }
                if (rightThickness > 0) {
                    thicknessDetails.push_back(std::make_pair(rightTopPoint, rightBottomPoint));
                }
                result.stripThicknessDetails[pattern.id] = thicknessDetails;
            }
        }
        
        logDebug(QString("STRIP 검사 완료 - %1: 점수=%.3f, 통과=%2, 좌측두께=%3px, 우측두께=%4px")
                .arg(pattern.name).arg(isPassed ? "예" : "아니오").arg(score).arg(leftThickness).arg(rightThickness));
        
        return isPassed;
        
    } catch (const cv::Exception& e) {
        logDebug(QString("STRIP 길이 검사 중 OpenCV 예외 발생 - %1: %2").arg(pattern.name).arg(e.what()));
        score = 0.0;
        result.insMethodTypes[pattern.id] = InspectionMethod::STRIP;
        return false;
    } catch (...) {
        logDebug(QString("STRIP 길이 검사 중 알 수 없는 예외 발생 - %1").arg(pattern.name));
        score = 0.0;
        result.insMethodTypes[pattern.id] = InspectionMethod::STRIP;
        return false;
    }
}

// INS 패턴 내부 좌표점들을 역회전시켜 고정 위치로 변환하는 유틸리티 함수
QList<QPoint> InsProcessor::transformPatternPoints(const std::vector<cv::Point>& roiPoints, 
                                                  const cv::Size& roiSize, 
                                                  double patternAngle,
                                                  const cv::Point2f& offset) 
{
    QList<QPoint> transformedPoints;
    
    if (roiPoints.empty()) {
        return transformedPoints;
    }
    
    // ROI 중심점
    cv::Point2f roiCenter(roiSize.width / 2.0f, roiSize.height / 2.0f);
    
    // 역회전각 (라디안)
    double reverseAngle = -patternAngle * CV_PI / 180.0;
    
    // 회전 변환 함수
    auto rotatePoint = [&](cv::Point p) -> cv::Point {
        cv::Point2f pt(p.x - roiCenter.x, p.y - roiCenter.y);  // 중심 기준으로 이동
        float cos_a = cos(reverseAngle);
        float sin_a = sin(reverseAngle);
        cv::Point2f rotated;
        rotated.x = pt.x * cos_a - pt.y * sin_a;
        rotated.y = pt.x * sin_a + pt.y * cos_a;
        return cv::Point(rotated.x + roiCenter.x, rotated.y + roiCenter.y);  // 원점 복원
    };
    
    // 각 점을 역회전시키고 절대좌표로 변환
    for (const cv::Point& roiPoint : roiPoints) {
        // 역회전 적용
        cv::Point fixedPoint = rotatePoint(roiPoint);
        
        // 절대좌표로 변환
        QPoint absPoint(fixedPoint.x + static_cast<int>(offset.x), 
                       fixedPoint.y + static_cast<int>(offset.y));
        
        transformedPoints.append(absPoint);
    }
    
    qDebug() << "transformPatternPoints: 입력점수" << roiPoints.size() 
             << "패턴각도" << patternAngle << "도 -> 변환완료" << transformedPoints.size() << "점";
    
    return transformedPoints;
}

