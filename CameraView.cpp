#include "CameraView.h"
#include "InsProcessor.h"
#include "TeachingWidget.h"
#include <QPainter> 
#include <QPainterPath>
#include <QRandomGenerator>
#include <QPen>   
#include <QMouseEvent>
#include <QQueue>
#include <QDebug>    
#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QScrollBar>
#include <cmath>
#include <algorithm>

CameraView::CameraView(QWidget *parent) : QGraphicsView(parent) {
    // QGraphicsScene 생성
    scene = new QGraphicsScene(this);
    setScene(scene);
    
    // 배경 설정
    bgPixmapItem = nullptr;
    setBackgroundBrush(QBrush(Qt::black));
    
    // 뷰 설정
    setMinimumSize(640, 480);
    setStyleSheet("border: 2px solid gray; background-color: black;");
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    // 스크롤바 숨기기
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    // macOS 트랙패드 핀치 제스처 활성화
    grabGesture(Qt::PinchGesture);
    
    statusInfo = "";
    selectedInspectionPatternId = QUuid(); // 검사 결과 필터 초기화
}

void CameraView::updateUITexts() {
    // CameraView에 표시되는 텍스트 요소 업데이트

    // 그룹 이름 업데이트
    for (auto it = groupNames.begin(); it != groupNames.end(); ++it) {
        QString originalName = it.value();
        if (originalName.startsWith("GRP ")) {
            // 그룹 번호만 있는 경우
            int groupNum = originalName.mid(4).toInt();
            it.value() = QString("%1 %2").arg(TR("PATTERN_GROUP")).arg(groupNum);
        } else if (originalName.startsWith("GRP: ")) {
            // 그룹 이름이 있는 경우
            QString groupName = originalName.mid(5);
            it.value() = QString("%1: %2").arg(TR("PATTERN_GROUP_WITH_NAME")).arg(groupName);
        }
    }

    // 상태 정보 업데이트
    if (m_statusText.contains("CAM")) {
        // CAM 텍스트는 그대로 유지
    }

    // 위젯 다시 그리기
    viewport()->update();
}

void CameraView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { 
        // 엔터 키가 눌렸고, 현재 유효한 사각형이 그려져 있는 경우
        if (!currentRect.isNull() && currentRect.width() > 10 && currentRect.height() > 10) {
            // 엔터키 시그널 발생
            emit enterKeyPressed(currentRect);
            event->accept();
            return;
        }
    }
    
    // 부모 클래스의 기본 처리
    QGraphicsView::keyPressEvent(event);
}

bool CameraView::event(QEvent* event) {
    // macOS 트랙패드 핀치 제스처 처리 (두 손가락으로 줌)
    if (event->type() == QEvent::Gesture) {
        QGestureEvent* gestureEvent = static_cast<QGestureEvent*>(event);
        if (QPinchGesture* pinch = static_cast<QPinchGesture*>(
                gestureEvent->gesture(Qt::PinchGesture))) {
            
            // 핀치 스케일 팩터 얻기 (> 1.0: 확대, < 1.0: 축소)
            qreal scaleFactor = pinch->scaleFactor();
            
            if (backgroundPixmap.isNull() || !bgPixmapItem) {
                event->accept();
                return true;
            }
            
            // 현재 스케일 확인
            QTransform t = transform();
            double currentScale = std::sqrt(t.m11() * t.m11() + t.m12() * t.m12());
            double newScale = currentScale * scaleFactor;
            
            // 0.2배 ~ 5.0배 범위로 제한
            double factor = scaleFactor;
            if (newScale < 0.2) {
                factor = 0.2 / currentScale;
            } else if (newScale > 5.0) {
                factor = 5.0 / currentScale;
            }
            
            // 핀치 중심점 기준으로 확대/축소
            QPointF centerPos = pinch->centerPoint();
            QPointF scenePos = mapToScene(centerPos.toPoint());
            
            scale(factor, factor);
            
            // 중심이 같은 위치에 남도록 보정
            QPointF newScenePos = mapToScene(centerPos.toPoint());
            centerOn(mapToScene(rect().center()) + (scenePos - newScenePos));
            
            viewport()->update();
            event->accept();
            return true;
        }
    }
    
    // 기본 이벤트 처리
    return QGraphicsView::event(event);
}

void CameraView::setScalingInfo(const QSize& origSize, const QSize& displaySize) {
    originalImageSize = origSize;
    
    if (origSize.width() > 0 && origSize.height() > 0) {
        // 실제 표시될 이미지 크기 계산 (AspectRatio 유지)
        double aspectRatio = (double)origSize.width() / origSize.height();
        QSize scaledSize;
        
        if (displaySize.width() / aspectRatio <= displaySize.height()) {
            // 너비 기준 스케일링
            scaledSize = QSize(displaySize.width(), (int)(displaySize.width() / aspectRatio));
        } else {
            // 높이 기준 스케일링
            scaledSize = QSize((int)(displaySize.height() * aspectRatio), displaySize.height());
        }
        
        scaleX = (double)origSize.width() / scaledSize.width();
        scaleY = (double)origSize.height() / scaledSize.height();
        
    }
}

QPoint CameraView::displayToOriginal(const QPoint& displayPos) const {
    // QGraphicsView의 mapToScene() 사용
    QPointF scenePos = const_cast<CameraView*>(this)->mapToScene(displayPos);
    return scenePos.toPoint();
}

QPoint CameraView::originalToDisplay(const QPoint& originalPos) const {
    // QGraphicsView의 mapFromScene() 사용 (const 버전)
    QPoint viewportPos = const_cast<CameraView*>(this)->mapFromScene(QPointF(originalPos));
    return viewportPos;
}

QRect CameraView::originalRectToDisplay(const QRect& origRect) const {
    // Scene 좌표를 viewport 좌표로 변환
    QPoint topLeft = const_cast<CameraView*>(this)->mapFromScene(origRect.topLeft());
    QPoint bottomRight = const_cast<CameraView*>(this)->mapFromScene(origRect.bottomRight());
    return QRect(topLeft, bottomRight);
}

void CameraView::mousePressEvent(QMouseEvent* event) {
    qDebug() << "[mousePressEvent] 클릭 발생 - 버튼:" << event->button() << "모드:" << static_cast<int>(m_editMode) << "검사모드:" << isInspectionMode;
    
    // Shift+클릭: 패닝 모드 (모든 모드에서 가능)
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier)) {
        isPanning = true;
        panStartPos = event->pos();
        panStartOffset = panOffset;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    
    // 검사 결과 모드: 거리 측정 (왼쪽 클릭 드래그)
    if (event->button() == Qt::LeftButton && isInspectionMode) {
        QPoint originalPos = displayToOriginal(event->pos());
        isMeasuring = true;
        measureStartPoint = originalPos;
        measureEndPoint = originalPos;
        viewport()->update();
        return;
    }
    
    if (event->button() == Qt::LeftButton) {
        QPoint pos = event->pos();
        QPoint originalPos = displayToOriginal(pos);
        
        qDebug() << "[mousePressEvent] 왼쪽 버튼 클릭 - pos:" << pos << "originalPos:" << originalPos;
        
        // 검사 결과 모드: 패턴 클릭 처리 - 이 모드에서는 이것만 처리하고 끝! (View 모드 상관없이)
        if (isInspectionMode) {
            qDebug() << "[mousePressEvent] 검사 결과 모드 - 패턴 클릭 처리";
            QUuid clickedPatternId;
            
            // FID 패턴 클릭 확인 (ROI는 제외)
            for (auto it = lastInspectionResult.fidResults.begin(); it != lastInspectionResult.fidResults.end(); ++it) {
                QUuid patternId = it.key();
                
                if (!lastInspectionResult.locations.contains(patternId)) continue;
                
                cv::Point matchLoc = lastInspectionResult.locations[patternId];
                const PatternInfo* patternInfo = nullptr;
                for (const PatternInfo& pattern : patterns) {
                    if (pattern.id == patternId) {
                        patternInfo = &pattern;
                        break;
                    }
                }
                
                if (!patternInfo || patternInfo->type != PatternType::FID) continue;
                
                int width = patternInfo->rect.width();
                int height = patternInfo->rect.height();
                QRect matchRect(matchLoc.x - width/2, matchLoc.y - height/2, width, height);
                
                if (matchRect.contains(originalPos)) {
                    clickedPatternId = patternId;
                    break;
                }
            }
            
            // INS 패턴 클릭 확인 (ROI는 제외)
            if (clickedPatternId.isNull()) {
                for (auto it = lastInspectionResult.insResults.begin(); it != lastInspectionResult.insResults.end(); ++it) {
                    QUuid patternId = it.key();
                    
                    const PatternInfo* patternInfo = nullptr;
                    for (const PatternInfo& pattern : patterns) {
                        if (pattern.id == patternId) {
                            patternInfo = &pattern;
                            break;
                        }
                    }
                    
                    if (!patternInfo || patternInfo->type != PatternType::INS) continue;
                    
                    QRectF inspRectScene;
                    if (lastInspectionResult.adjustedRects.contains(patternId)) {
                        inspRectScene = lastInspectionResult.adjustedRects[patternId];
                    } else {
                        inspRectScene = patternInfo->rect;
                    }
                    
                    if (inspRectScene.toRect().contains(originalPos)) {
                        clickedPatternId = patternId;
                        break;
                    }
                }
            }
            
            // FID/INS 클릭 또는 빈 공간 클릭 처리
            if (clickedPatternId.isNull()) {
                // FID/INS 패턴이 없는 곳 클릭 - 모든 패턴 표시
                selectedInspectionPatternId = QUuid();
                viewport()->update();
                emit selectedInspectionPatternCleared();
            } else {
                // FID/INS 패턴 클릭 - 해당 패턴만 필터링
                selectedInspectionPatternId = clickedPatternId;
                viewport()->update();
                emit patternSelected(clickedPatternId);
            }
            // 검사 결과 모드에서는 여기서 반드시 return! 다른 처리 금지!
            return;
        }
        
        // View 모드에서는 편집 기능만 차단
        if (m_editMode == EditMode::View) {
            // View 모드에서는 패턴 선택만 가능
            QUuid hitPatternId = hitTest(pos);
            
            if (!hitPatternId.isNull()) {
                setSelectedPatternId(hitPatternId);
            } else {
                setSelectedPatternId(QUuid());
            }
            
            QGraphicsView::mousePressEvent(event);
            return;
        }
        
        // MOVE 모드일 때는 Draw 기능 비활성화
        if (m_editMode == EditMode::Move) {
            // MOVE 모드에서 회전 핸들 클릭 체크 (가장 먼저)
            if (!selectedPatternId.isNull() && getRotateHandleAt(pos) == 1) {
                isRotating = true;
                rotateStartPos = pos;
                PatternInfo* pattern = getPatternById(selectedPatternId);
                if (pattern) {
                    initialAngle = pattern->angle;
                    QPointF centerScene = pattern->rect.center();
                    rotationCenter = mapFromScene(centerScene.toPoint());
                }
                setCursor(Qt::OpenHandCursor);
                return;
            }
            
            // 리사이즈 핸들 클릭 체크
            int handleIdx = getCornerHandleAt(pos);
            if (handleIdx >= 0 && !selectedPatternId.isNull()) {
                isResizing = true;
                activeHandleIdx = handleIdx;
                
                PatternInfo* pattern = getPatternById(selectedPatternId);
                if (pattern) {
                    QVector<QPoint> rotatedCorners = getRotatedCorners();
                    
                    if (rotatedCorners.size() == 4) {
                        int fixedHandleIdx = (handleIdx + 2) % 4;
                        fixedScreenPos = rotatedCorners[fixedHandleIdx];
                    } else {
                        QPointF tl = pattern->rect.topLeft();
                        QPointF tr = pattern->rect.topRight();
                        QPointF br = pattern->rect.bottomRight();
                        QPointF bl = pattern->rect.bottomLeft();
                    
                        QPointF fixedOriginal;
                        if (handleIdx == 0) fixedOriginal = br;
                        else if (handleIdx == 1) fixedOriginal = bl;
                        else if (handleIdx == 2) fixedOriginal = tl;
                        else if (handleIdx == 3) fixedOriginal = tr;
                    
                        fixedScreenPos = originalToDisplay(QPoint(static_cast<int>(fixedOriginal.x()), static_cast<int>(fixedOriginal.y())));
                    }
                }
                
                setCursor(Qt::SizeAllCursor);
                return;
            }
            
            // 패턴 클릭 체크 (핸들이 아닐 때만)
            QUuid hitPatternId = hitTest(pos);
            
            if (!hitPatternId.isNull()) {
                setSelectedPatternId(hitPatternId);
                isDragging = true;
                PatternInfo* pattern = getPatternById(hitPatternId);
                if (pattern) {
                    QPoint patternTopLeft = originalToDisplay(pattern->rect.topLeft().toPoint());
                    dragOffset = pos - patternTopLeft;
                }
                return;
            } else {
                // 빈 공간 클릭 - 패턴 선택 해제
                setSelectedPatternId(QUuid());
                isDragging = false;
                isResizing = false;
                isRotating = false;
                activeHandle = ResizeHandle::None;
                update();
                return;
            }
        } else if (m_editMode == EditMode::Draw) {
            isDrawing = true;
            startPoint = originalPos;
            currentRect = QRect();
            setCursor(Qt::ArrowCursor);
            update();
            return;
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void CameraView::mouseMoveEvent(QMouseEvent* event) {
    QPoint pos = event->pos();
    QPoint originalPos = displayToOriginal(pos);
    
    // 픽셀 정보 업데이트 (항상 먼저 실행)
    if (!backgroundPixmap.isNull()) {
        QImage bgImage = backgroundPixmap.toImage();
        if (originalPos.x() >= 0 && originalPos.x() < bgImage.width() &&
            originalPos.y() >= 0 && originalPos.y() < bgImage.height()) {
            QRgb pixel = bgImage.pixel(originalPos.x(), originalPos.y());
            emit pixelInfoChanged(originalPos.x(), originalPos.y(), 
                                qRed(pixel), qGreen(pixel), qBlue(pixel));
        }
    }
    
    // 패닝 모드일 때 처리
    if (isPanning) {
        QPoint delta = event->pos() - panStartPos;
        
        // 델타가 0이면 처리하지 않음 (불필요한 연산 방지)
        if (delta.manhattanLength() == 0) {
            return;
        }
        
        // 수평/수직 스크롤바를 직접 조정 (크로스 플랫폼에서 더 안정적)
        QScrollBar* hBar = horizontalScrollBar();
        QScrollBar* vBar = verticalScrollBar();
        
        if (hBar) {
            hBar->setValue(hBar->value() - delta.x());
        }
        if (vBar) {
            vBar->setValue(vBar->value() - delta.y());
        }
        
        panStartPos = event->pos();
        return;
    }

    // 거리 측정 모드일 때 처리
    if (isMeasuring) {
        QPoint originalPos = displayToOriginal(event->pos());
        measureEndPoint = originalPos;
        viewport()->update();
        return;
    }

    // View 모드에서는 패닝만 허용하고 다른 모든 편집 기능 차단
    if (m_editMode == EditMode::View) {
        setCursor(Qt::ArrowCursor);
        QGraphicsView::mouseMoveEvent(event);
        return;
    }

    // DRAW 모드에서만 그리기 처리
    if (m_editMode == EditMode::Draw && isDrawing) {
        setCursor(Qt::ArrowCursor);
        dragEndPoint = originalPos;
        QRect newRect = QRect(startPoint, originalPos).normalized();
        if (newRect.width() > 5 || newRect.height() > 5) {
            currentRect = newRect;
            update();
        }
        QGraphicsView::mouseMoveEvent(event);
        return;
    }
    
    // MOVE 모드가 아니면 기본 처리
    if (m_editMode != EditMode::Move) {
        QGraphicsView::mouseMoveEvent(event);
        return;
    }
    
    // MOVE 모드에서는 그리기 차단
    if (m_editMode == EditMode::Move && isDrawing) {
        isDrawing = false;
        currentRect = QRect();
        update();
    }

    // MOVE 모드에서 커서 처리
    if (m_editMode == EditMode::Move && !isRotating && !isResizing && !isDragging) {
        if (!selectedPatternId.isNull()) {
            // 회전 핸들 위에 있는지 확인
            if (getRotateHandleAt(pos) == 1) {
                setCursor(Qt::OpenHandCursor);
                QGraphicsView::mouseMoveEvent(event);
                return;
            }
            // 크기 조절 핸들 위에 있는지 확인
            if (getCornerHandleAt(pos) != -1) {
                setCursor(Qt::SizeFDiagCursor);
                QGraphicsView::mouseMoveEvent(event);
                return;
            }
        }
        setCursor(Qt::ArrowCursor);
    }

    // 회전 모드 처리
    if (isRotating && !selectedPatternId.isNull()) {
        PatternInfo* pattern = getPatternById(selectedPatternId);
        if (!pattern) return;

        // 회전 시작 시점에 고정된 중심점 사용
        QPoint center = rotationCenter;
        
        // 각도 계산을 라디안으로 수행하여 더 정확하게 처리
        double dx1 = rotateStartPos.x() - center.x();
        double dy1 = rotateStartPos.y() - center.y();
        double dx2 = event->pos().x() - center.x();
        double dy2 = event->pos().y() - center.y();

        double angle1 = std::atan2(dy1, dx1);
        double angle2 = std::atan2(dy2, dx2);
        double deltaAngle = angle2 - angle1;
        
        // 각도 차이가 180도를 넘으면 보정 (각도 점프 방지)
        if (deltaAngle > M_PI) deltaAngle -= 2.0 * M_PI;
        else if (deltaAngle < -M_PI) deltaAngle += 2.0 * M_PI;
        
        // 라디안을 도(degree)로 변환하고 각도 정규화
        double newAngle = initialAngle + deltaAngle * 180.0 / M_PI;
        newAngle = std::fmod(newAngle, 360.0);
        if (newAngle < 0) newAngle += 360.0;
        
        // 패턴 각도 업데이트 (개별 회전)
        pattern->angle = newAngle;
        
        // 각도 변경 신호 발송
        emit patternAngleChanged(selectedPatternId, newAngle);
        
        update();
        return;
    }

    if (isResizing && !selectedPatternId.isNull()) {
        PatternInfo* pat = getPatternById(selectedPatternId);
        if (!pat) return;

        // Viewport 좌표 기반 리사이징 (고정점은 이미 viewport 좌표)
        double fx = fixedScreenPos.x();
        double fy = fixedScreenPos.y();
        double mx = event->position().x();
        double my = event->position().y();

        // 중심점 (viewport 좌표)
        double cx = (fx + mx) / 2.0;
        double cy = (fy + my) / 2.0;

        // 회전 각도를 라디안으로 변환
        double rad = pat->angle * M_PI / 180.0;
        double cos_a = std::cos(rad);
        double sin_a = std::sin(rad);

        // 벡터 계산
        double dx = mx - fx;
        double dy = my - fy;

        // 로컬 좌표계에서 너비와 높이 계산 (회전 고려)
        double local_dx = dx * cos_a + dy * sin_a;
        double local_dy = -dx * sin_a + dy * cos_a;

        // Viewport 좌표에서의 새로운 크기 (절대값)
        double screenWidth = std::abs(local_dx);
        double screenHeight = std::abs(local_dy);
        
        // 최소 크기 제한
        screenWidth = std::max(screenWidth, 10.0);
        screenHeight = std::max(screenHeight, 10.0);

        // Scene 좌표로 변환
        QPointF centerScene = mapToScene(QPoint(qRound(cx), qRound(cy)));
        double sceneWidth = screenWidth / transform().m11();  // 현재 스케일 고려
        double sceneHeight = screenHeight / transform().m22();  // 현재 스케일 고려
        
        // QRectF로 정밀도 유지하여 저장
        pat->rect = QRectF(
            centerScene.x() - sceneWidth / 2.0,
            centerScene.y() - sceneHeight / 2.0,
            sceneWidth,
            sceneHeight
        );
        
        // 리사이징 중 실시간 템플릿 업데이트를 위한 시그널 발생
        emit patternRectChanged(selectedPatternId, QRect(static_cast<int>(pat->rect.x()), static_cast<int>(pat->rect.y()),
                                                        static_cast<int>(pat->rect.width()), static_cast<int>(pat->rect.height())));
        
        update();
        return;
    }

    // 드래깅 처리 - 패턴 이동
    if (isDragging && !selectedPatternId.isNull()) {
        PatternInfo* pattern = getPatternById(selectedPatternId);
        if (!pattern) return;

        // Viewport 좌표를 Scene 좌표로 변환
        QPointF currentScenePos = mapToScene(pos);
        QPointF dragStartScenePos = mapToScene(pos - dragOffset);
        
        QRectF newRect = pattern->rect;
        newRect.moveTopLeft(dragStartScenePos);

        if (newRect.left() >= 0 && newRect.top() >= 0 &&
            newRect.right() < 1440 &&  // 원본 이미지 너비
            newRect.bottom() < 1080) {  // 원본 이미지 높이

            pattern->rect = newRect;
            emit patternRectChanged(selectedPatternId, QRect(static_cast<int>(newRect.x()), static_cast<int>(newRect.y()), 
                                                           static_cast<int>(newRect.width()), static_cast<int>(newRect.height())));

            update();
        }
        return;
    }

    // 일반 마우스 이동 시 커서 변경
    QUuid hitPatternId = hitTest(pos);
    if (!hitPatternId.isNull()) {
        ResizeHandle handle = getResizeHandle(pos, hitPatternId);
        if (handle != ResizeHandle::None) {
            setCursor(getResizeCursor(handle));
        } else if (getRotateHandleAt(pos) == 1) {
            setCursor(Qt::OpenHandCursor);
        } else {
            setCursor(Qt::SizeAllCursor);
        }
    } else {
        setCursor(Qt::ArrowCursor);
    }
    
    QGraphicsView::mouseMoveEvent(event);
}

void CameraView::mouseReleaseEvent(QMouseEvent* event) {
    if (isPanning && event->button() == Qt::LeftButton) {
        isPanning = false;
        setCursor(Qt::ArrowCursor);
        return;
    }

    // 거리 측정 모드 해제
    if (isMeasuring && event->button() == Qt::LeftButton) {
        isMeasuring = false;
        measureStartPoint = QPoint();
        measureEndPoint = QPoint();
        viewport()->update();
        return;
    }

    // View 모드에서는 패닝 해제만 허용하고 다른 모든 편집 기능 차단
    if (m_editMode == EditMode::View) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }

    QPoint pos = event->pos();
    QPoint originalPos = displayToOriginal(pos);

    // DRAW 모드에서만 사각형 그리기 완료 처리
    if (isDrawing && m_editMode == EditMode::Draw) {
        isDrawing = false;
        dragEndPoint = originalPos;
        QRect rect = QRect(
            std::min(startPoint.x(), dragEndPoint.x()),
            std::min(startPoint.y(), dragEndPoint.y()),
            std::abs(dragEndPoint.x() - startPoint.x()),
            std::abs(dragEndPoint.y() - startPoint.y())
        );
        if (rect.width() < 10 || rect.height() < 10) {
            currentRect = QRect();
            update();
            return;
        }
        currentRect = rect;
        emit rectDrawn(rect);
        update();
        return;
    }
    
    // MOVE 모드에서 isDrawing이 true가 된 경우 강제로 차단
    if (isDrawing && m_editMode == EditMode::Move) {
        isDrawing = false;
        currentRect = QRect();
        update();
        return;
    }

    // MOVE 모드에서 회전 상태 해제
    if (isRotating && event->button() == Qt::LeftButton) {
        isRotating = false;
        rotationCenter = QPoint(); // 회전 중심점 초기화
        setCursor(Qt::ArrowCursor);
        return;
    }

     if (event->button() == Qt::LeftButton) {
        if (isResizing || isDragging) {
            isResizing = false;
            isDragging = false;
            activeHandle = ResizeHandle::None;
            activeHandleIdx = -1;
            fixedScreenPos = QPoint();
            setCursor(Qt::ArrowCursor);
        }
    }

    else if (event->button() == Qt::RightButton) {
        QPoint pos = event->pos();
        QUuid hitPatternId = hitTest(pos);

        if (m_editMode == EditMode::Move) {
            if (!hitPatternId.isNull()) {
                setSelectedPatternId(hitPatternId);
                showContextMenu(pos);
            }
            else if (!selectedPatternId.isNull()) {
                showContextMenu(pos);
            }
        }
        else if (m_editMode == EditMode::Draw) {
            if (!currentRect.isNull()) {
                showContextMenu(pos);
            }
        }
    }
    QGraphicsView::mouseReleaseEvent(event);
}

// 선택 영역 내의 패턴들 찾기
QList<QUuid> CameraView::findPatternsInSelection() const {
    QList<QUuid> result;
    if (currentRect.isNull()) return result;

    for (const PatternInfo& pattern : patterns) {
        // 시뮬레이션 모드에서는 currentCameraUuid과 비교, 일반 모드에서는 currentCameraUuid와 비교
        bool patternVisible = false;
        if (!currentCameraUuid.isEmpty()) {
            // 시뮬레이션 모드: 모든 패턴을 표시
            patternVisible = true;
        } else {
            // 일반 모드: currentCameraUuid와 비교
            patternVisible = (currentCameraUuid.isEmpty() || 
                            pattern.cameraUuid == currentCameraUuid ||
                            pattern.cameraUuid.isEmpty());
        }
        
        if (!patternVisible) {
            continue;
        }
        
        // 패턴의 중심점이 선택 영역 안에 있는지 확인
        QPoint center = QPoint(static_cast<int>(pattern.rect.center().x()), static_cast<int>(pattern.rect.center().y()));
        if (currentRect.contains(center)) {
            result.append(pattern.id);
        }
    }
    
    return result;
}

void CameraView::showContextMenu(const QPoint& pos) {
    QMenu contextMenu(this);
    
    // 라즈베리파이 호환성을 위한 메뉴 스타일 적용
    contextMenu.setStyleSheet(UIColors::contextMenuStyle());
    
    // 1. MOVE 모드에서 선택된 패턴이 있는 경우
    if (m_editMode == EditMode::Move && !selectedPatternId.isNull()) {
        PatternInfo* pattern = getPatternById(selectedPatternId);
        if (!pattern) return;

        // 패턴 이름을 메뉴 상단에 표시
        QAction* titleAction = contextMenu.addAction(pattern->name);
        titleAction->setEnabled(false);
        titleAction->setFont(QFont("Arial", 10, QFont::Bold));
        
        contextMenu.addSeparator();
        
        // ROI 패턴인 경우 같은 크기의 FID 생성 메뉴 추가
        if (pattern->type == PatternType::ROI) {
            QAction* createFidAction = contextMenu.addAction("같은 크기 FID 생성");
            connect(createFidAction, &QAction::triggered, this, [=]() {
                // ROI와 같은 크기의 FID 패턴 생성
                PatternInfo fidPattern;
                fidPattern.id = QUuid::createUuid();
                fidPattern.type = PatternType::FID;
                // 랜덤한 이름 생성
                const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
                QString randomStr;
                for (int i = 0; i < 6; i++) {
                    randomStr += chars.at(QRandomGenerator::global()->bounded(chars.length()));
                }
                fidPattern.name = QString("F_%1").arg(randomStr);
                fidPattern.rect = pattern->rect; // ROI와 동일한 크기와 위치
                fidPattern.color = UIColors::FIDUCIAL_COLOR;
                fidPattern.enabled = true;
                fidPattern.cameraUuid = pattern->cameraUuid;
                fidPattern.runInspection = true;
                fidPattern.fidMatchMethod = 0;
                fidPattern.matchThreshold = 75.0;  // 75%
                
                // 부모-자식 관계 설정
                fidPattern.parentId = pattern->id;  // FID의 부모를 ROI로 설정
                
                // ROI의 자식 목록에 새 FID 추가
                PatternInfo* roiPattern = getPatternById(pattern->id);
                if (roiPattern) {
                    roiPattern->childIds.append(fidPattern.id);
                }
                
                // 패턴 추가
                addPattern(fidPattern);
                setSelectedPatternId(fidPattern.id);
                
                // 트리 업데이트를 위한 시그널 발생
                emit patternsGrouped();
                
                update();
            });
            contextMenu.addSeparator();
        }
        
        // 패턴 삭제 메뉴
        QAction* deleteAction = contextMenu.addAction("패턴 삭제");
        
        // INS 패턴인 경우 패턴 복사 메뉴 추가
        QAction* copyAction = nullptr;
        if (pattern->type == PatternType::INS) {
            copyAction = contextMenu.addAction("패턴 복사");
        }

        // 필터 추가 메뉴
        QAction* addFilterAction = contextMenu.addAction("필터 추가");
        
        // 그룹 해제 메뉴 추가
        QAction* ungroupAction = nullptr;
        if (pattern->type == PatternType::FID && !pattern->childIds.isEmpty()) {
            contextMenu.addSeparator();
            ungroupAction = contextMenu.addAction("그룹 해제");
        }
        else if (pattern->type == PatternType::INS && !pattern->parentId.isNull()) {
            contextMenu.addSeparator();
            ungroupAction = contextMenu.addAction("그룹에서 제거");
        }
        
        // FID 패턴에서 INS 그룹화 메뉴 추가
        QAction* fidGroupAction = nullptr;
        if (pattern->type == PatternType::FID && pattern->childIds.isEmpty()) {
            // 현재 FID와 같은 영역에 있는 INS 패턴들 찾기
            QList<QUuid> nearbyInsPatterns;
            for (const auto& patternInfo : patterns) {
                if (patternInfo.type == PatternType::INS && 
                    patternInfo.parentId.isNull() && 
                    patternInfo.cameraUuid == pattern->cameraUuid) {
                    nearbyInsPatterns.append(patternInfo.id);
                }
            }
            
            if (!nearbyInsPatterns.isEmpty()) {
                contextMenu.addSeparator();
                QString groupText = QString("INS 패턴 그룹화 (%1개 패턴)").arg(nearbyInsPatterns.size());
                fidGroupAction = contextMenu.addAction(groupText);
            }
        }
        
        // 메뉴 표시 및 처리
        QAction* selectedAction = contextMenu.exec(mapToGlobal(pos));
        
        if (selectedAction == deleteAction) {  
            emit requestRemovePattern(selectedPatternId);
        }
        else if (selectedAction == addFilterAction) {
            emit requestAddFilter(selectedPatternId);
        }
        else if (selectedAction == copyAction && pattern->type == PatternType::INS) {
            // 선택된 패턴 복사
            PatternInfo newPattern = *pattern;
            newPattern.id = QUuid::createUuid();
            newPattern.name = pattern->name + " (복사)";
            
            // 부모-자식 관계 제거
            newPattern.parentId = QUuid();
            newPattern.childIds.clear();
            
            // 패턴 추가
            addPattern(newPattern);
            setSelectedPatternId(newPattern.id);
            
            emit patternsGrouped();
            update();
        }
        else if (selectedAction == ungroupAction) {
            if (pattern->type == PatternType::FID && !pattern->childIds.isEmpty()) {
                // 그룹 해제 확인
                CustomMessageBox msgBox(this);
                msgBox.setIcon(CustomMessageBox::Question);
                msgBox.setTitle("그룹 해제 확인");
                msgBox.setMessage(QString("'%1' 그룹을 해제하시겠습니까?\n그룹 내 모든 패턴이 독립적으로 변경됩니다.").arg(pattern->name));
                msgBox.setButtons(QMessageBox::Yes | QMessageBox::No);
                QMessageBox::StandardButton reply = static_cast<QMessageBox::StandardButton>(msgBox.exec());
                
                if (reply == QMessageBox::Yes) {
                    QList<QUuid> childIds = pattern->childIds;
                    
                    // 모든 자식 패턴의 부모 관계 해제
                    for (const QUuid& childId : childIds) {
                        PatternInfo* child = getPatternById(childId);
                        if (child) {
                            child->parentId = QUuid();
                        }
                    }
                    
                    // 부모 패턴의 자식 목록 비우기
                    pattern->childIds.clear();
                    
                    emit patternsGrouped();
                    update();
                }
            }
            else if (pattern->type == PatternType::INS && !pattern->parentId.isNull()) {
                PatternInfo* parentPattern = getPatternById(pattern->parentId);
                if (parentPattern) {
                    // 부모의 자식 목록에서 현재 패턴 제거
                    parentPattern->childIds.removeAll(pattern->id);
                    // 현재 패턴의 부모 ID 제거
                    pattern->parentId = QUuid();
                    
                    emit patternsGrouped();
                    update();
                }
            }
        }
        else if (selectedAction == fidGroupAction) {
            // FID와 INS 패턴들을 그룹화
            QList<QUuid> ungroupedInsPatterns;
            for (const auto& patternInfo : patterns) {
                if (patternInfo.type == PatternType::INS && 
                    patternInfo.parentId.isNull() && 
                    patternInfo.cameraUuid == pattern->cameraUuid) {
                    ungroupedInsPatterns.append(patternInfo.id);
                }
            }
            
            if (!ungroupedInsPatterns.isEmpty()) {
                // FID를 부모로, INS들을 자식으로 설정
                for (const QUuid& insId : ungroupedInsPatterns) {
                    PatternInfo* insPattern = getPatternById(insId);
                    if (insPattern) {
                        insPattern->parentId = pattern->id;
                        pattern->childIds.append(insId);
                    }
                }
                
                emit patternsGrouped();
                update();
            }
        }
    }
    // 2. DRAW 모드에서 선택 영역이 있는 경우
    else if (!currentRect.isNull()) {
        QList<QUuid> selectedPatterns = findPatternsInSelection();
        
        // 선택된 패턴이 없는 경우 새 패턴 생성 메뉴 추가
        if (selectedPatterns.isEmpty()) {
            // 영역 크기 정보 표시
            QString sizeInfo = QString("영역 크기: %1 x %2 픽셀")
                               .arg(currentRect.width())
                               .arg(currentRect.height());
            QAction* sizeAction = contextMenu.addAction(sizeInfo);
            sizeAction->setEnabled(false);
            QFont sizeFont("Arial", 9);
            sizeFont.setItalic(true);
            sizeAction->setFont(sizeFont);
            
            contextMenu.addSeparator();
            
            // ROI 생성 메뉴
            QAction* createRoiAction = contextMenu.addAction("ROI 패턴 생성");
            
            // FID 생성 메뉴
            QAction* createFidAction = contextMenu.addAction("FID 패턴 생성");
            
            // INS 생성 메뉴
            QAction* createInsAction = contextMenu.addAction("INS 패턴 생성");
            
            // 메뉴 표시
            QAction* selectedAction = contextMenu.exec(mapToGlobal(pos));
            
            if (selectedAction == createRoiAction) {
                // ROI 패턴 생성
                PatternInfo newPattern;
                newPattern.id = QUuid::createUuid();
                newPattern.type = PatternType::ROI;
                newPattern.name = QString("ROI_%1").arg(newPattern.id.toString().left(8));
                newPattern.rect = currentRect;
                newPattern.color = UIColors::ROI_COLOR;
                newPattern.enabled = true;
                newPattern.cameraUuid = currentCameraUuid;
                newPattern.includeAllCamera = false;
                newPattern.stripCrimpMode = currentStripCrimpMode;
                
                addPattern(newPattern);
                setSelectedPatternId(newPattern.id);
                currentRect = QRect();
                update();
            }
            else if (selectedAction == createFidAction) {
                // FID 패턴 생성
                PatternInfo newPattern;
                newPattern.id = QUuid::createUuid();
                newPattern.type = PatternType::FID;
                newPattern.name = QString("FID_%1").arg(newPattern.id.toString().left(8));
                newPattern.rect = currentRect;
                newPattern.color = UIColors::FIDUCIAL_COLOR;
                newPattern.enabled = true;
                newPattern.cameraUuid = currentCameraUuid;
                newPattern.runInspection = true;
                newPattern.fidMatchMethod = 0;
                newPattern.matchThreshold = 75.0;  // 75%
                newPattern.useRotation = false;
                newPattern.minAngle = 0.0;
                newPattern.angle = 0.0;  // 각도 명시적으로 0으로 설정
                
                newPattern.maxAngle = 360.0;
                newPattern.angleStep = 1.0;
                newPattern.stripCrimpMode = currentStripCrimpMode;
                
                addPattern(newPattern);
                setSelectedPatternId(newPattern.id);
                emit fidTemplateUpdateRequired(newPattern.id);
                currentRect = QRect();
                update();
            }
            else if (selectedAction == createInsAction) {
                // INS 패턴 생성
                PatternInfo newPattern;
                newPattern.id = QUuid::createUuid();
                newPattern.type = PatternType::INS;
                newPattern.name = QString("INS_%1").arg(newPattern.id.toString().left(8));
                newPattern.rect = currentRect;
                newPattern.color = UIColors::INSPECTION_COLOR;
                newPattern.enabled = true;
                newPattern.cameraUuid = currentCameraUuid;
                newPattern.runInspection = true;
                newPattern.inspectionMethod = InspectionMethod::DIFF;
                newPattern.passThreshold = 80.0;  // 80%
                newPattern.angle = 0.0;  // 각도 명시적으로 0으로 설정
                newPattern.stripCrimpMode = currentStripCrimpMode;
                
                addPattern(newPattern);
                setSelectedPatternId(newPattern.id);
                emit insTemplateUpdateRequired(newPattern.id);
                currentRect = QRect();
                update();
            }
            
            return;
        }
        
        // 패턴 분류 (현재 Strip/Crimp 모드의 패턴만)
        QList<QUuid> roiPatternIds;
        QList<QUuid> fidPatternIds;
        QList<QUuid> groupedFidPatternIds;
        QList<QUuid> ungroupedInsPatternIds;
        QList<QUuid> groupedInsPatternIds;
        QList<QUuid> groupedRoiPatternIds;
        
        for (const QUuid& id : selectedPatterns) {
            PatternInfo* pattern = getPatternById(id);
            if (!pattern) continue;
            
            // 현재 Strip/Crimp 모드와 다른 패턴은 제외
            if (pattern->stripCrimpMode != currentStripCrimpMode) continue;
            
            if (pattern->type == PatternType::ROI) {
                roiPatternIds.append(id);
                
                // 자식이 있으면 그룹화된 ROI
                if (!pattern->childIds.isEmpty()) {
                    groupedRoiPatternIds.append(id);
                }
            }
            else if (pattern->type == PatternType::FID) {
                fidPatternIds.append(id);
                
                // 자식이 있으면 그룹화된 FID
                if (!pattern->childIds.isEmpty()) {
                    groupedFidPatternIds.append(id);
                }
            } 
            else if (pattern->type == PatternType::INS) {
                // 부모가 있으면 그룹화된 INS, 없으면 그룹화되지 않은 INS
                if (!pattern->parentId.isNull()) {
                    groupedInsPatternIds.append(id);
                } else {
                    ungroupedInsPatternIds.append(id);
                }
            }
        }
        
        // 메뉴 구성
        
        // 디버그 로그 추가
        
        // 1. 그룹 해제 메뉴 추가
        if (!groupedRoiPatternIds.isEmpty() || !groupedFidPatternIds.isEmpty() || !groupedInsPatternIds.isEmpty()) {
            QString ungroupText;
            
            if (!groupedRoiPatternIds.isEmpty()) {
                ungroupText = groupedRoiPatternIds.size() == 1 ? 
                    "ROI 그룹 해제 (1개)" : 
                    QString("ROI 그룹 해제 (%1개)").arg(groupedRoiPatternIds.size());
            }
            else if (!groupedFidPatternIds.isEmpty()) {
                ungroupText = groupedFidPatternIds.size() == 1 ? 
                    "FID 그룹 해제 (1개)" : 
                    QString("FID 그룹 해제 (%1개)").arg(groupedFidPatternIds.size());
            } else {
                ungroupText = groupedInsPatternIds.size() == 1 ?
                    "그룹에서 제거 (INS 1개)" :
                    QString("그룹에서 제거 (INS %1개)").arg(groupedInsPatternIds.size());
            }
            
            QAction* ungroupAction = contextMenu.addAction(ungroupText);
            
            connect(ungroupAction, &QAction::triggered, this, [=]() {
                ungroupPatternsInSelection(selectedPatterns);
            });
        }
        
        // 2. ROI 기반 그룹화 메뉴 추가 (ROI 1개 + FID 1개, INS가 없을 때만)
        if (roiPatternIds.size() == 1 && fidPatternIds.size() == 1 && ungroupedInsPatternIds.isEmpty()) {
            QString groupText = "ROI 기반 그룹화 (ROI:1, FID:1)";
            QAction* groupAction = contextMenu.addAction(groupText);
            
            connect(groupAction, &QAction::triggered, this, [=]() {
                QList<QUuid> patternsToGroup;
                patternsToGroup.append(roiPatternIds.first());
                patternsToGroup.append(fidPatternIds.first());
                
                groupPatternsInSelection(patternsToGroup);
            });
        }
        // 3. FID 기반 그룹화 메뉴 추가 (ROI가 있어도 FID+INS 그룹화는 가능)
        if (fidPatternIds.size() == 1 && !ungroupedInsPatternIds.isEmpty()) {
            QString groupText = QString("FID 기반 그룹화 (FID:1, INS:%1)").arg(ungroupedInsPatternIds.size());
            QAction* groupAction = contextMenu.addAction(groupText);
            
            connect(groupAction, &QAction::triggered, this, [=]() {
                QList<QUuid> patternsToGroup;
                patternsToGroup.append(fidPatternIds.first());
                
                for (const QUuid& insId : ungroupedInsPatternIds) {
                    patternsToGroup.append(insId);
                }
                
                groupPatternsInSelection(patternsToGroup);
            });
        } 
        // 그룹화 불가 사유 메시지
        else if (!roiPatternIds.isEmpty() && fidPatternIds.isEmpty()) {
            QAction* infoAction = contextMenu.addAction("ROI 기반 그룹화를 위해 FID 패턴이 필요합니다");
            infoAction->setEnabled(false);
        }
        else if (roiPatternIds.size() > 1) {
            QAction* infoAction = contextMenu.addAction(QString("ROI 패턴이 %1개 있습니다 (1개만 허용)").arg(roiPatternIds.size()));
            infoAction->setEnabled(false);
        }
        else if (fidPatternIds.size() > 1) {
            QAction* infoAction = contextMenu.addAction(QString("FID 패턴이 %1개 있습니다 (1개만 허용)").arg(fidPatternIds.size()));
            infoAction->setEnabled(false);
        }
        else if (fidPatternIds.isEmpty() && !ungroupedInsPatternIds.isEmpty()) {
            QAction* infoAction = contextMenu.addAction("그룹화를 위해 FID 패턴이 필요합니다");
            infoAction->setEnabled(false);
        }
        
        // 메뉴 표시
        if (!contextMenu.isEmpty()) {
            contextMenu.exec(mapToGlobal(pos));
        }
    }
}

// 선택된 패턴을 그룹화
void CameraView::groupPatternsInSelection(const QList<QUuid>& patternIds) {
    // 1. 패턴 분류
    QUuid roiPatternId;
    QUuid fidPatternId;
    QList<QUuid> insPatternIds;
    int roiCount = 0;
    int fidCount = 0;
    
    for (const QUuid& id : patternIds) {
        PatternInfo* pattern = getPatternById(id);
        if (!pattern) continue;
        
        if (pattern->type == PatternType::ROI) {
            roiPatternId = id;
            roiCount++;
        }
        else if (pattern->type == PatternType::FID) {
            fidPatternId = id;
            fidCount++;
        }
        else if (pattern->type == PatternType::INS) {
            insPatternIds.append(id);
        }
    }
    
    // 2. 그룹화 가능성 검증
    
    // ROI가 있는 경우: ROI->FID->INS 구조
    if (!roiPatternId.isNull()) {
        // ROI가 1개보다 많으면 그룹화 불가
        if (roiCount > 1) {
            CustomMessageBox msgBox(this);
            msgBox.setIcon(CustomMessageBox::Warning);
            msgBox.setTitle("그룹화 실패");
            msgBox.setMessage("선택 영역 내에 ROI 패턴이 여러 개 있습니다.\n"
                              "그룹화를 위해서는 정확히 하나의 ROI 패턴만 선택해야 합니다.");
            msgBox.setButtons(QMessageBox::Ok);
            msgBox.exec();
            return;
        }
        
        // FID가 없으면 그룹화 불가
        if (fidPatternId.isNull()) {
            CustomMessageBox msgBox(this);
            msgBox.setIcon(CustomMessageBox::Warning);
            msgBox.setTitle("그룹화 실패");
            msgBox.setMessage("ROI 기반 그룹화를 위해서는 FID 패턴이 필요합니다.\n"
                              "선택 영역에 FID 패턴을 포함시켜 주세요.");
            msgBox.setButtons(QMessageBox::Ok);
            msgBox.exec();
            return;
        }
        
        // FID가 1개보다 많으면 그룹화 불가
        if (fidCount > 1) {
            CustomMessageBox msgBox(this);
            msgBox.setIcon(CustomMessageBox::Warning);
            msgBox.setTitle("그룹화 실패");
            msgBox.setMessage("선택 영역 내에 FID 패턴이 여러 개 있습니다.\n"
                              "ROI 기반 그룹화를 위해서는 정확히 하나의 FID 패턴만 선택해야 합니다.");
            msgBox.setButtons(QMessageBox::Ok);
            msgBox.exec();
            return;
        }
        
        // 3. ROI 기반 그룹화 수행
        PatternInfo* roiPattern = getPatternById(roiPatternId);
        PatternInfo* fidPattern = getPatternById(fidPatternId);
        
        // 기존 부모-자식 관계 정리
        
        // FID가 이미 다른 부모가 있는 경우 기존 부모에서 제거
        if (!fidPattern->parentId.isNull()) {
            PatternInfo* oldParent = getPatternById(fidPattern->parentId);
            if (oldParent) {
                oldParent->childIds.removeAll(fidPatternId);
            }
        }
        
        // FID를 ROI의 자식으로 설정
        fidPattern->parentId = roiPatternId;
        
        // ROI의 자식 목록에 FID 추가 (중복 방지)
        if (!roiPattern->childIds.contains(fidPatternId)) {
            roiPattern->childIds.append(fidPatternId);
        }
        
        // INS 패턴들을 FID의 자식으로 설정
        for (const QUuid& insId : insPatternIds) {
            PatternInfo* insPattern = getPatternById(insId);
            if (!insPattern) continue;
            
            // 이미 다른 부모가 있는 경우 기존 부모에서 제거
            if (!insPattern->parentId.isNull()) {
                PatternInfo* oldParent = getPatternById(insPattern->parentId);
                if (oldParent) {
                    oldParent->childIds.removeAll(insId);
                }
            }
            
            // 새로운 부모-자식 관계 설정
            insPattern->parentId = fidPatternId;
            
            // 중복 방지
            if (!fidPattern->childIds.contains(insId)) {
                fidPattern->childIds.append(insId);
            }
        }
        
        // 그룹화 완료 알림 (ROI 기반)
        QString message = QString("ROI 기반 그룹화 완료:\n- ROI: 1개\n- FID: 1개\n- INS: %1개")
                                .arg(insPatternIds.size());
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Information);
        msgBox.setTitle("그룹화 완료");
        msgBox.setMessage(message);
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
    }
    // ROI가 없는 경우: 기존 FID->INS 구조 유지
    else {
        // FID 패턴이 없으면 그룹화 불가
        if (fidPatternId.isNull()) {
            CustomMessageBox msgBox(this);
            msgBox.setIcon(CustomMessageBox::Warning);
            msgBox.setTitle("그룹화 실패");
            msgBox.setMessage("선택 영역 내에 FID 패턴이 없습니다.\n"
                              "그룹화를 위해서는 하나의 FID 패턴이 필요합니다.");
            msgBox.setButtons(QMessageBox::Ok);
            msgBox.exec();
            return;
        }
        
        // FID 패턴이 1개보다 많으면 그룹화 불가
        if (fidCount > 1) {
            CustomMessageBox msgBox(this);
            msgBox.setIcon(CustomMessageBox::Warning);
            msgBox.setTitle("그룹화 실패");
            msgBox.setMessage("선택 영역 내에 FID 패턴이 여러 개 있습니다.\n"
                              "그룹화를 위해서는 정확히 하나의 FID 패턴만 선택해야 합니다.");
            msgBox.setButtons(QMessageBox::Ok);
            msgBox.exec();
            return;
        }
        
        // INS 패턴이 없으면 경고
        if (insPatternIds.isEmpty()) {
            CustomMessageBox msgBox(this);
            msgBox.setIcon(CustomMessageBox::Information);
            msgBox.setTitle("그룹화 완료");
            msgBox.setMessage("FID 패턴이 그룹 헤더로 설정되었지만 추가된 INS 패턴이 없습니다.");
            msgBox.setButtons(QMessageBox::Ok);
            msgBox.exec();
        }
        
        // 기존 FID->INS 그룹화 수행
        PatternInfo* fidPattern = getPatternById(fidPatternId);
        
        for (const QUuid& insId : insPatternIds) {
            PatternInfo* insPattern = getPatternById(insId);
            if (!insPattern) continue;
            
            // 이미 다른 부모가 있는 경우 기존 부모에서 제거
            if (!insPattern->parentId.isNull()) {
                PatternInfo* oldParent = getPatternById(insPattern->parentId);
                if (oldParent) {
                    oldParent->childIds.removeAll(insId);
                }
            }
            
            // 새로운 부모-자식 관계 설정
            insPattern->parentId = fidPatternId;
            
            // 중복 방지
            if (!fidPattern->childIds.contains(insId)) {
                fidPattern->childIds.append(insId);
            }
        }
        
        // 그룹화 완료 알림 (FID 기반)
        if (!insPatternIds.isEmpty()) {
            CustomMessageBox msgBox(this);
            msgBox.setIcon(CustomMessageBox::Information);
            msgBox.setTitle("그룹화 완료");
            msgBox.setMessage(QString("FID 기반 그룹화 완료: FID 패턴과 %1개의 INS 패턴이 그룹화되었습니다.")
                                  .arg(insPatternIds.size()));
            msgBox.setButtons(QMessageBox::Ok);
            msgBox.exec();
        }
    }
    
    // UI 갱신
    update();
    
    // 패턴 테이블 갱신을 위한 시그널 발생
    emit patternsGrouped();
}

// 선택된 패턴의 그룹해제
void CameraView::ungroupPatternsInSelection(const QList<QUuid>& patternIds) {
    // ROI 패턴들과 FID 패턴들 분류
    QList<QUuid> roiPatternIds;
    QList<QUuid> fidPatternIds;
    
    for (const QUuid& id : patternIds) {
        PatternInfo* pattern = getPatternById(id);
        if (!pattern) continue;
        
        // ROI 패턴이고 자식이 있는 경우 (ROI 그룹 헤더)
        if (pattern->type == PatternType::ROI && !pattern->childIds.isEmpty()) {
            roiPatternIds.append(id);
        }
        // FID 패턴이고 자식이 있는 경우 (FID 그룹 헤더)
        else if (pattern->type == PatternType::FID && !pattern->childIds.isEmpty()) {
            fidPatternIds.append(id);
        }
    }
    
    int totalChildrenCount = 0;
    int totalGroupsCount = 0;
    
    // 1. ROI 기반 그룹 해제 처리
    for (const QUuid& roiId : roiPatternIds) {
        PatternInfo* roiPattern = getPatternById(roiId);
        if (!roiPattern) continue;
        
        totalGroupsCount++;
        
        // ROI의 모든 자식들 (FID와 그 하위 INS들) 처리
        QList<QUuid> directChildren = roiPattern->childIds; // 복사본 생성
        
        for (const QUuid& childId : directChildren) {
            PatternInfo* childPattern = getPatternById(childId);
            if (!childPattern) continue;
            
            // FID 자식인 경우, 그 하위 INS들도 모두 해제
            if (childPattern->type == PatternType::FID) {
                // FID의 모든 INS 자식들 해제
                QList<QUuid> fidChildren = childPattern->childIds; // 복사본 생성
                
                for (const QUuid& insId : fidChildren) {
                    PatternInfo* insPattern = getPatternById(insId);
                    if (insPattern) {
                        insPattern->parentId = QUuid(); // INS의 부모 관계 해제
                        totalChildrenCount++;
                    }
                }
                
                // FID의 자식 목록 비우기
                childPattern->childIds.clear();
                totalChildrenCount += fidChildren.size();
            }
            
            // 직접 자식의 부모 관계 해제
            childPattern->parentId = QUuid();
            totalChildrenCount++;
        }
        
        // ROI의 자식 목록 비우기
        roiPattern->childIds.clear();
    }
    
    // 2. 남은 FID 기반 그룹 해제 처리 (ROI에 속하지 않은 독립 FID 그룹)
    for (const QUuid& fidId : fidPatternIds) {
        PatternInfo* fidPattern = getPatternById(fidId);
        if (!fidPattern) continue;
        
        // 이미 ROI 그룹 해제 과정에서 처리된 FID는 건너뛰기
        if (!fidPattern->parentId.isNull()) {
            continue; // 이 FID는 ROI의 자식이므로 이미 처리됨
        }
        
        totalGroupsCount++;
        
        QList<QUuid> fidChildren = fidPattern->childIds; // 복사본 생성
        totalChildrenCount += fidChildren.size();
        
        // 모든 자식 패턴의 부모 ID 초기화
        for (const QUuid& childId : fidChildren) {
            PatternInfo* child = getPatternById(childId);
            if (child) {
                child->parentId = QUuid(); // 부모 ID 제거
            }
        }
        
        // 부모 패턴의 자식 목록 비우기
        fidPattern->childIds.clear();
    }
    
    // 그룹 해제할 패턴이 없으면 종료
    if (totalGroupsCount == 0) {
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Information);
        msgBox.setTitle("그룹 해제 실패");
        msgBox.setMessage("선택 영역 내에 그룹화된 패턴이 없습니다.");
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }
    
    // UI 갱신
    update();
    
    // 그룹 해제 완료 알림
    QString message;
    if (roiPatternIds.size() > 0 && fidPatternIds.size() > 0) {
        message = QString("그룹 해제 완료:\n- ROI 그룹: %1개\n- FID 그룹: %2개\n- 총 해제된 패턴: %3개")
                          .arg(roiPatternIds.size())
                          .arg(fidPatternIds.size())
                          .arg(totalChildrenCount);
    }
    else if (roiPatternIds.size() > 0) {
        message = QString("ROI 그룹 해제 완료:\n- 해제된 ROI 그룹: %1개\n- 총 해제된 패턴: %2개")
                          .arg(roiPatternIds.size())
                          .arg(totalChildrenCount);
    }
    else {
        message = QString("FID 그룹 해제 완료:\n- 해제된 FID 그룹: %1개\n- 총 해제된 패턴: %2개")
                          .arg(fidPatternIds.size())
                          .arg(totalChildrenCount);
    }
    
    CustomMessageBox msgBox(this);
    msgBox.setIcon(CustomMessageBox::Information);
    msgBox.setTitle("그룹 해제 완료");
    msgBox.setMessage(message);
    msgBox.setButtons(QMessageBox::Ok);
    msgBox.exec();
    
    // 패턴 테이블 갱신을 위한 시그널 발생
    emit patternsGrouped();
}

bool CameraView::updatePatternById(const QUuid& id, const PatternInfo& pattern) {
    for (int i = 0; i < patterns.size(); i++) {
        if (patterns[i].id == id) {
            // ★★★ 각도 정보 보존 확인 디버그 ★★★
            double oldAngle = patterns[i].angle;
            double newAngle = pattern.angle;
            if (oldAngle != newAngle) {
                // 각도 변경 신호 발송
                emit patternAngleChanged(id, newAngle);
            }
            
            patterns[i] = pattern;
            update();
            return true;
        }
    }
    return false;
}

void CameraView::updateInspectionResult(bool passed, const InspectionResult& result) {
    isInspectionMode = true;
    hasInspectionResult = true;
    lastInspectionPassed = passed;
    lastInspectionResult = result;
    
    // 처음 검사 결과가 나올 때는 필터를 해제하여 모든 결과를 보여줌
    if (selectedInspectionPatternId.isNull()) {
        // 이미 null이므로 그대로 유지 (모든 검사 결과 표시)
    }
    
    // 매칭 결과에서 업데이트된 각도 정보를 패턴에 반영
    for (auto it = result.angles.begin(); it != result.angles.end(); ++it) {
        QUuid patternId = it.key();
        double detectedAngle = it.value();
        
        // 해당 패턴의 각도 업데이트
        for (int i = 0; i < patterns.size(); i++) {
            if (patterns[i].id == patternId) {
                double oldAngle = patterns[i].angle;
                patterns[i].angle = detectedAngle;

                
                // 각도 변경 신호 발송
                emit patternAngleChanged(patternId, detectedAngle);
                
                // 같은 FID 그룹의 INS 패턴들은 덩어리로 이동/회전(리짓 바디 변환)합니다.
                if (patterns[i].type == PatternType::FID) {
                    // 티칭 시점의 FID 중심과 각도를 보존
                    QPointF fidTeachingCenter = patterns[i].rect.center();
                    double fidTeachingAngle = oldAngle; // 이전(티칭) 각도

                    // 결과에 FID 위치가 있으면 그 위치를 사용하여 전체 그룹 이동
                    cv::Point fidDetectedLoc(0,0);
                    bool haveFidDetectedLoc = false;
                    if (result.locations.contains(patternId)) {
                        fidDetectedLoc = result.locations[patternId];
                        haveFidDetectedLoc = true;
                    }

                    double angleDiff = detectedAngle - fidTeachingAngle;
                    double radians = angleDiff * M_PI / 180.0;
                    double cosA = std::cos(radians);
                    double sinA = std::sin(radians);

                    for (int j = 0; j < patterns.size(); j++) {
                        if (patterns[j].type == PatternType::INS && 
                            patterns[j].parentId == patterns[i].id) {
                            // 티칭 시점의 자식 중심
                            QPointF childTeachingCenter = patterns[j].rect.center();
                            double relX = childTeachingCenter.x() - fidTeachingCenter.x();
                            double relY = childTeachingCenter.y() - fidTeachingCenter.y();

                            // 회전된 상대 벡터 계산
                            double rotatedX = relX * cosA - relY * sinA;
                            double rotatedY = relX * sinA + relY * cosA;

                            // 새 중심 = (FID 검출 위치) + 회전된 상대벡터
                            double newCx_d, newCy_d;
                            if (haveFidDetectedLoc) {
                                newCx_d = fidDetectedLoc.x + rotatedX;
                                newCy_d = fidDetectedLoc.y + rotatedY;
                            } else {
                                // 검출 위치가 없으면 티칭 FID 중심을 기준으로 회전만 적용
                                newCx_d = fidTeachingCenter.x() + rotatedX;
                                newCy_d = fidTeachingCenter.y() + rotatedY;
                            }

                            int newCx = qRound(newCx_d);
                            int newCy = qRound(newCy_d);

                            int w = patterns[j].rect.width();
                            int h = patterns[j].rect.height();
                            patterns[j].rect = QRect(newCx - w/2, newCy - h/2, w, h);

                            // 각도는 티칭 각도에 FID 회전 차이를 더한 값
                            double childOldAngle = patterns[j].angle;
                            patterns[j].angle = childOldAngle + angleDiff;



                            // 각도 변경 신호 발송 (UI 업데이트용)
                            emit patternAngleChanged(patterns[j].id, patterns[j].angle);

                            // 화면 그리기에 사용되는 검사 결과(lastInspectionResult)도 갱신
                            // adjustedRects는 QRectF 타입일 수 있으므로 QRect -> QRectF로 저장
                            lastInspectionResult.adjustedRects[patterns[j].id] = patterns[j].rect;
                            lastInspectionResult.parentAngles[patterns[j].id] = patterns[j].angle;
                        }
                    }
                }
                break;
            }
        }
    }
    
    update();
}

void CameraView::drawInspectionResults(QPainter& painter, const InspectionResult& result) {
    drawROIPatterns(painter, result);
    drawFIDPatterns(painter, result);
    drawINSPatterns(painter, result);
}

void CameraView::drawROIPatterns(QPainter& painter, const InspectionResult& result) {
    for (const PatternInfo& pattern : patterns) {
        if (pattern.type != PatternType::ROI || !pattern.enabled) continue;
        
        // 패턴 표시 조건: 패턴에 카메라 지정이 없거나, 현재 카메라가 비어있거나, 카메라가 일치하면 표시
        if (!pattern.cameraUuid.isEmpty() && !currentCameraUuid.isEmpty() && pattern.cameraUuid != currentCameraUuid) {
            continue;
        }
        
        // STRIP/CRIMP 모드 체크 (검사 결과에서는 항상 현재 모드만)
        if (pattern.stripCrimpMode != currentStripCrimpMode) continue;
        
        QPointF topLeft = mapFromScene(pattern.rect.topLeft());
        QPointF bottomRight = mapFromScene(pattern.rect.bottomRight());
        QRectF displayRect(topLeft, bottomRight);
        
        QColor color = UIColors::getPatternColor(pattern.type);
        
        // 회전 적용 (paintEvent와 동일)
        painter.save();
        QPointF center = displayRect.center();
        painter.translate(center);
        painter.rotate(pattern.angle);
        painter.translate(-center);
        
        painter.setPen(QPen(color, 2));
        painter.drawRect(displayRect);
        
        painter.restore();
        
        // 패턴 이름 (회전 적용)
        QFont font(NAMEPLATE_FONT_FAMILY, NAMEPLATE_FONT_SIZE, NAMEPLATE_FONT_WEIGHT);
        painter.setFont(font);
        QFontMetrics fm(font);
        int textWidth = fm.horizontalAdvance(pattern.name);
        int textHeight = fm.height();
        
        painter.save();
        painter.translate(center);
        painter.rotate(pattern.angle);
        painter.translate(-center);
        
        QRectF textRect(displayRect.center().x() - textWidth/2, displayRect.top() - textHeight - 2, textWidth + 6, textHeight);
        painter.fillRect(textRect, QBrush(QColor(0, 0, 0, 180)));
        painter.setPen(color);
        painter.drawText(textRect, Qt::AlignCenter, pattern.name);
        
        painter.restore();
    }
}

void CameraView::drawFIDPatterns(QPainter& painter, const InspectionResult& result) {
    bool hasSelectedPattern = !selectedInspectionPatternId.isNull();
    
    for (auto it = result.fidResults.begin(); it != result.fidResults.end(); ++it) {
        QUuid patternId = it.key();
        bool passed = it.value();
        
        if (hasSelectedPattern && patternId != selectedInspectionPatternId) continue;
        if (!result.locations.contains(patternId)) continue;
        
        cv::Point matchLoc = result.locations[patternId];
        double score = result.matchScores.value(patternId, 0.0);
        double detectedAngle = result.angles.value(patternId, 0.0);
        
        const PatternInfo* patternInfo = nullptr;
        for (const PatternInfo& pattern : patterns) {
            if (pattern.id == patternId) {
                patternInfo = &pattern;
                break;
            }
        }
        
        if (!patternInfo || patternInfo->type != PatternType::FID) continue;
        
        bool patternVisible = (patternInfo->cameraUuid.isEmpty() || patternInfo->cameraUuid == currentCameraUuid || currentCameraUuid.isEmpty());
        if (!patternVisible) continue;
        
        // STRIP/CRIMP 모드 체크 (검사 결과에서는 항상 현재 모드만)
        if (patternInfo->stripCrimpMode != currentStripCrimpMode) continue;
        
        // FID 박스 그리기 (검출된 위치 기준)
        int width = patternInfo->rect.width();
        int height = patternInfo->rect.height();
        QRectF matchRectScene(matchLoc.x - width/2.0, matchLoc.y - height/2.0, width, height);
        QPointF centerScene = matchRectScene.center();
        QPointF centerViewport = mapFromScene(centerScene);
        QPointF topLeftViewport = mapFromScene(matchRectScene.topLeft());
        QPointF bottomRightViewport = mapFromScene(matchRectScene.bottomRight());
        QRectF matchRect(topLeftViewport, bottomRightViewport);
        
        QColor borderColor = passed ? UIColors::FIDUCIAL_COLOR : QColor(200, 0, 0);
        
        // 회전하여 그리기 (paintEvent와 동일)
        painter.save();
        painter.translate(centerViewport);
        painter.rotate(detectedAngle);
        painter.translate(-centerViewport);
        
        painter.setPen(QPen(borderColor, 2));
        painter.drawRect(matchRect);
        
        painter.restore();
        
        // FID 라벨 (점수를 퍼센트로 표시)
        QString label = QString("%1: %2%").arg(patternInfo->name).arg(score * 100.0, 0, 'f', 1);
        QFont font(NAMEPLATE_FONT_FAMILY, NAMEPLATE_FONT_SIZE, NAMEPLATE_FONT_WEIGHT);
        painter.setFont(font);
        QFontMetrics fm(font);
        int textW = fm.horizontalAdvance(label);
        int textH = fm.height();
        
        painter.save();
        painter.translate(centerViewport);
        painter.rotate(detectedAngle);
        painter.translate(-centerViewport);
        
        QRectF labelRect(matchRect.center().x() - textW/2, matchRect.top() - textH - 2, textW + 6, textH);
        painter.fillRect(labelRect, QBrush(QColor(0, 0, 0, 180)));
        
        painter.setPen(UIColors::FIDUCIAL_COLOR);
        painter.drawText(labelRect, Qt::AlignCenter, label);
        
        painter.restore();
        
        // ===== FID 패턴 노란색 박스 (축정렬, 회전 투영 고려) =====
        // 현재 줌 스케일 계산
        QTransform t = transform();
        double currentScale = std::sqrt(t.m11() * t.m11() + t.m12() * t.m12());
        
        // 원본 크기
        double fidWidth = patternInfo->rect.width();
        double fidHeight = patternInfo->rect.height();
        
        // 회전 투영 적용
        double radians = detectedAngle * M_PI / 180.0;
        double cosA = std::cos(radians);
        double sinA = std::sin(radians);
        double projX = std::abs(fidWidth * cosA) + std::abs(fidHeight * sinA);
        double projY = std::abs(fidWidth * sinA) + std::abs(fidHeight * cosA);
        
        // 노란색 박스 크기
        double yellowWidth = projX * currentScale;
        double yellowHeight = projY * currentScale;
        
        // 노란색 박스 (축정렬)
        painter.setPen(QPen(QColor(255, 255, 0), 1.5));
        painter.setBrush(Qt::NoBrush);
        
        QPointF fidTopLeft(centerViewport.x() - yellowWidth/2, centerViewport.y() - yellowHeight/2);
        QPointF fidTopRight(centerViewport.x() + yellowWidth/2, centerViewport.y() - yellowHeight/2);
        QPointF fidBottomLeft(centerViewport.x() - yellowWidth/2, centerViewport.y() + yellowHeight/2);
        QPointF fidBottomRight(centerViewport.x() + yellowWidth/2, centerViewport.y() + yellowHeight/2);
        
        QPolygonF fidYellowPolygon;
        fidYellowPolygon << fidTopLeft << fidTopRight << fidBottomRight << fidBottomLeft;
        painter.drawPolygon(fidYellowPolygon);
    }
}

void CameraView::drawINSPatterns(QPainter& painter, const InspectionResult& result) {
    bool hasSelectedPattern = !selectedInspectionPatternId.isNull();
    
    for (auto it = result.insResults.begin(); it != result.insResults.end(); ++it) {
        QUuid patternId = it.key();
        bool passed = it.value();
        
        // 선택된 패턴이 있으면 그 패턴만 보여주되, STRIP 데이터는 항상 보여줘야 함
        // (STRIP은 INS 하위 검사이므로 INS가 필터링되어도 데이터는 표시되어야 함)
        if (hasSelectedPattern && patternId != selectedInspectionPatternId) {
            // 선택된 패턴이 현재 패턴이 아니면 INS 박스는 건너뜀
            // 하지만 STRIP 데이터는 나중에 확인하기 위해 일단 계속 진행
        }
        
        const PatternInfo* patternInfo = nullptr;
        for (const PatternInfo& pattern : patterns) {
            if (pattern.id == patternId) {
                patternInfo = &pattern;
                break;
            }
        }
        
        if (!patternInfo) {
            printf("[CameraView] INS 패턴 정보를 찾을 수 없음: %s\n", patternId.toString().toStdString().c_str());
            fflush(stdout);
            continue;
        }
        
        if (patternInfo->type != PatternType::INS) {
            printf("[CameraView] 패턴 타입이 INS가 아님: %s (type=%d)\n", 
                   patternInfo->name.toStdString().c_str(), static_cast<int>(patternInfo->type));
            fflush(stdout);
            continue;
        }
        
        bool patternVisible = (patternInfo->cameraUuid.isEmpty() || patternInfo->cameraUuid == currentCameraUuid || currentCameraUuid.isEmpty());
        if (!patternVisible) continue;
        
        // STRIP/CRIMP 모드 체크 (검사 결과에서는 항상 현재 모드만)
        if (patternInfo->stripCrimpMode != currentStripCrimpMode) continue;
        
        // 선택된 패턴이 있으면 그 패턴만 그리기 (INS 박스)
        bool drawINSBox = true;
        if (hasSelectedPattern && patternId != selectedInspectionPatternId) {
            drawINSBox = false;
        }
        
        // INS 검사 영역
        QRectF inspRectScene;
        if (result.adjustedRects.contains(patternId)) {
            inspRectScene = result.adjustedRects[patternId];
        } else {
            inspRectScene = patternInfo->rect;
        }
        
        QPointF topLeftViewport = mapFromScene(inspRectScene.topLeft());
        QPointF bottomRightViewport = mapFromScene(inspRectScene.bottomRight());
        QRectF inspRect(topLeftViewport, bottomRightViewport);
        
        double insAngle = result.parentAngles.value(patternId, 0.0);
        QPointF centerViewport = inspRect.center();
        double score = result.insScores.value(patternId, 0.0);
        
        QColor borderColor = passed ? UIColors::INSPECTION_COLOR : QColor(200, 0, 0);
        
        // INS 박스를 필터링에 따라 그리기
        if (drawINSBox) {
            // 회전하여 박스 그리기 (paintEvent와 동일)
            painter.save();
            painter.translate(centerViewport);
            painter.rotate(insAngle);
            painter.translate(-centerViewport);
            
            painter.setPen(QPen(borderColor, 2));
            painter.drawRect(inspRect);
            
            painter.restore();
            
            // INS 라벨 (패턴이름: 검사방법)
            // CRIMP는 점수 표시 안함, DIFF/STRIP은 점수 표시
            QString methodName = InspectionMethod::getName(patternInfo->inspectionMethod);
            QString label;
            if (patternInfo->inspectionMethod == InspectionMethod::CRIMP) {
                label = QString("%1: %2").arg(patternInfo->name).arg(methodName);
            } else {
                label = QString("%1: %2(%3%)").arg(patternInfo->name)
                                              .arg(methodName)
                                              .arg(score * 100.0, 0, 'f', 1);
            }
            QFont font(NAMEPLATE_FONT_FAMILY, NAMEPLATE_FONT_SIZE, NAMEPLATE_FONT_WEIGHT);
            painter.setFont(font);
            QFontMetrics fm(font);
            int textW = fm.horizontalAdvance(label);
            int textH = fm.height();
            
            // PASS/NG 텍스트
            QString passText = passed ? "PASS" : "NG";
            QColor passColor = passed ? QColor(0, 255, 0) : QColor(255, 0, 0);
            int passTextW = fm.horizontalAdvance(passText);
            
            painter.save();
            painter.translate(centerViewport);
            painter.rotate(insAngle);
            painter.translate(-centerViewport);
            
            // PASS/NG 배경 및 텍스트 (라벨 위)
            QRectF passRect(inspRect.center().x() - passTextW/2, inspRect.top() - textH * 2 - 4, passTextW + 6, textH);
            painter.fillRect(passRect, QBrush(QColor(0, 0, 0, 180)));
            painter.setPen(passColor);
            painter.drawText(passRect, Qt::AlignCenter, passText);
            
            // 패턴 라벨 배경 및 텍스트
            QRectF labelRect(inspRect.center().x() - textW/2, inspRect.top() - textH - 2, textW + 6, textH);
            painter.fillRect(labelRect, QBrush(QColor(0, 0, 0, 180)));
            
            painter.setPen(UIColors::INSPECTION_COLOR);
            painter.drawText(labelRect, Qt::AlignCenter, label);
            
            painter.restore();
        }
        
        // 검사 방법별 시각화 호출
        switch (patternInfo->inspectionMethod) {
            case InspectionMethod::STRIP:
                drawINSStripVisualization(painter, result, patternId, patternInfo, inspRectScene, insAngle);
                break;
            case InspectionMethod::DIFF:
                drawINSDiffVisualization(painter, result, patternId, patternInfo, inspRectScene, insAngle);
                break;
            case InspectionMethod::CRIMP:
                drawINSCrimpVisualization(painter, result, patternId, patternInfo, inspRectScene, insAngle);
                break;
        }
    }
}

// ===== STRIP 검사 시각화 =====
void CameraView::drawINSStripVisualization(QPainter& painter, const InspectionResult& result,
                                            const QUuid& patternId, const PatternInfo* patternInfo,
                                            const QRectF& inspRectScene, double insAngle) {
    // 현재 줌 스케일 계산
    QTransform t = transform();
    double currentScale = std::sqrt(t.m11() * t.m11() + t.m12() * t.m12());
    
    // 회전 각도를 라디안으로 변환
    double radians = insAngle * M_PI / 180.0;
    double cosA = std::cos(radians);
    double sinA = std::sin(radians);
    
    QPointF centerViewport = mapFromScene(inspRectScene.center());
    QPointF patternCenterScene = inspRectScene.center();
    
    // 공통 컨텍스트 생성
    StripDrawContext ctx(painter, result, patternId, patternInfo, inspRectScene,
                        insAngle, currentScale, centerViewport, cosA, sinA);
    
    // 원본 크기 및 중심
    double insWidth = inspRectScene.width();
    double insHeight = inspRectScene.height();
    QPointF insCenter = centerViewport;
    
    // ===== 1. INS 패턴 노란색 바운딩 박스 (축정렬, 회전 투영 고려) =====
    drawYellowBoundingBox(painter, QSizeF(insWidth, insHeight), insCenter, insAngle, currentScale);
    
    // INS 박스의 회전된 좌표들을 얻기 위해 4개 모서리점 변환
    QPointF topLeftScene = inspRectScene.topLeft();
    QPointF topRightScene(inspRectScene.right(), inspRectScene.top());
    QPointF bottomLeftScene(inspRectScene.left(), inspRectScene.bottom());
    QPointF bottomRightScene = inspRectScene.bottomRight();
    
    // Viewport로 변환
    QPointF topLeftVP = mapFromScene(topLeftScene);
    QPointF topRightVP = mapFromScene(topRightScene);
    QPointF bottomLeftVP = mapFromScene(bottomLeftScene);
    QPointF bottomRightVP = mapFromScene(bottomRightScene);
    
    // 회전 변환 함수: 중심 기준 회전
    auto rotatePoint = [&](QPointF pt, QPointF center) -> QPointF {
        double dx = pt.x() - center.x();
        double dy = pt.y() - center.y();
        double newX = center.x() + dx * cosA - dy * sinA;
        double newY = center.y() + dx * sinA + dy * cosA;
        return QPointF(newX, newY);
            };
            
            // 회전 후의 실제 좌표 계산
            QPointF rotTopLeftVP = rotatePoint(topLeftVP, centerViewport);
            QPointF rotTopRightVP = rotatePoint(topRightVP, centerViewport);
            QPointF rotBottomLeftVP = rotatePoint(bottomLeftVP, centerViewport);
            QPointF rotBottomRightVP = rotatePoint(bottomRightVP, centerViewport);
            
            // 가로 방향 벡터 (회전 후)
            double widthVectorX = rotTopRightVP.x() - rotTopLeftVP.x();
            double widthVectorY = rotTopRightVP.y() - rotTopLeftVP.y();
            double vectorLen = std::sqrt(widthVectorX * widthVectorX + widthVectorY * widthVectorY);
            
            if (vectorLen > 0.01) {
                // ===== 실제 측정 지점들을 그리기 (FRONT/REAR) =====
                // 시작점과 Max Gradient 지점으로부터 FRONT/REAR 위치 결정
                if (result.stripStartPoint.contains(patternId) && result.stripMaxGradientPoint.contains(patternId)) {
                    QPoint startPointImgInt = result.stripStartPoint[patternId];
                    QPoint maxGradPointImgInt = result.stripMaxGradientPoint[patternId];
                    
                    // ROI 패턴을 찾아서 이미지 좌표 -> 씬 좌표로 변환
                    QPointF roiPatternTopLeftScene;
                    double roiWidth = 0, roiHeight = 0;
                    
                    for (const PatternInfo& p : patterns) {
                        if (p.type == PatternType::ROI && p.cameraUuid == currentCameraUuid) {
                            roiPatternTopLeftScene = p.rect.topLeft();
                            roiWidth = p.rect.width();
                            roiHeight = p.rect.height();
                            break;
                        }
                    }
                    
                    // 이미지 좌표를 ROI 기준 비율로 변환
                    double normalizedX = roiWidth > 0 ? startPointImgInt.x() / roiWidth : 0;
                    double normalizedY = roiHeight > 0 ? startPointImgInt.y() / roiHeight : 0;
                    double normalizedMaxX = roiWidth > 0 ? maxGradPointImgInt.x() / roiWidth : 0;
                    double normalizedMaxY = roiHeight > 0 ? maxGradPointImgInt.y() / roiHeight : 0;
                    
                    // 씬 좌표로 변환
                    QPointF startPointScene(roiPatternTopLeftScene.x() + normalizedX * roiWidth,
                                           roiPatternTopLeftScene.y() + normalizedY * roiHeight);
                    QPointF maxGradPointScene(roiPatternTopLeftScene.x() + normalizedMaxX * roiWidth,
                                             roiPatternTopLeftScene.y() + normalizedMaxY * roiHeight);
                    
                    // 씬 좌표 -> 뷰포트 좌표 변환
                    QPointF startPointVP = mapFromScene(startPointScene);
                    QPointF maxGradPointVP = mapFromScene(maxGradPointScene);
                    
                    // 회전 적용
                    QPointF rotStartPoint = rotatePoint(startPointVP, centerViewport);
                    QPointF rotMaxGradPoint = rotatePoint(maxGradPointVP, centerViewport);
                    
                    // stripStartPoint와 stripMaxGradientPoint 데이터는 있지만 표시하지 않음
                }
            }
            
    // ===== 2. REAR 박스 시각화 (stripGradientEndPercent 위치) =====
            if (result.stripRearBoxSize.contains(patternId)) {
                // INS 박스의 회전된 좌표들을 얻기 위해 4개 모서리점 변환
                QPointF topLeftScene = inspRectScene.topLeft();
                QPointF topRightScene(inspRectScene.right(), inspRectScene.top());
                QPointF bottomLeftScene(inspRectScene.left(), inspRectScene.bottom());
                QPointF bottomRightScene = inspRectScene.bottomRight();
                
                // Viewport로 변환
                QPointF topLeftVP = mapFromScene(topLeftScene);
                QPointF topRightVP = mapFromScene(topRightScene);
                QPointF bottomLeftVP = mapFromScene(bottomLeftScene);
                QPointF bottomRightVP = mapFromScene(bottomRightScene);
                
                // 회전 각도를 라디안으로 변환
                double radians = insAngle * M_PI / 180.0;
                double cosA = std::cos(radians);
                double sinA = std::sin(radians);
                
                // 회전 변환 함수
                auto rotatePointFunc = [&](QPointF pt, QPointF center) -> QPointF {
                    double dx = pt.x() - center.x();
                    double dy = pt.y() - center.y();
                    double newX = center.x() + dx * cosA - dy * sinA;
                    double newY = center.y() + dx * sinA + dy * cosA;
                    return QPointF(newX, newY);
                };
                
                QPointF rotTopLeftVP = rotatePointFunc(topLeftVP, centerViewport);
                QPointF rotTopRightVP = rotatePointFunc(topRightVP, centerViewport);
                QPointF rotBottomLeftVP = rotatePointFunc(bottomLeftVP, centerViewport);
                QPointF rotBottomRightVP = rotatePointFunc(bottomRightVP, centerViewport);
                
                // 가로 방향 벡터 (회전 후)
                double widthVectorX = rotTopRightVP.x() - rotTopLeftVP.x();
                double widthVectorY = rotTopRightVP.y() - rotTopLeftVP.y();
                double vectorLen = std::sqrt(widthVectorX * widthVectorX + widthVectorY * widthVectorY);
                
                // REAR 박스 중심 (Viewport 좌표) - Yellow 박스에서도 재사용
                QPointF rearBoxCenterVP;
                
                if (vectorLen > 0.01) {
                    // gradient 끝점 (REAR 검사 중심) - stripGradientEndPercent (보통 85%)
                    float endPercent = patternInfo->stripGradientEndPercent / 100.0f;
                    QPointF posEndTop(
                        rotTopLeftVP.x() + widthVectorX * endPercent,
                        rotTopLeftVP.y() + widthVectorY * endPercent
                    );
                    QPointF posEndBottom(
                        rotBottomLeftVP.x() + widthVectorX * endPercent,
                        rotBottomLeftVP.y() + widthVectorY * endPercent
                    );
                    rearBoxCenterVP = QPointF(
                        (posEndTop.x() + posEndBottom.x()) / 2.0f,
                        (posEndTop.y() + posEndBottom.y()) / 2.0f
                    );
                    

                    
                    // zoom scale 적용
                    QTransform t = transform();
                    double currentScale = std::sqrt(t.m11() * t.m11() + t.m12() * t.m12());
                    
                    // ImageProcessor가 계산한 바운딩 박스 크기 사용 (Yellow 박스와 동일)
                    double boxWidth = 0, boxHeight = 0;

                    if (result.stripRearBoxSize.contains(patternId)) {
                        QSizeF rearBoxSize = result.stripRearBoxSize[patternId];
                        boxWidth = rearBoxSize.width() * currentScale;
                        boxHeight = rearBoxSize.height() * currentScale;

                    } else {

                        // fallback: 원본 크기에서 바운딩 박스 계산
                        if (std::abs(patternInfo->angle) < 0.1) {
                            boxWidth = patternInfo->stripRearThicknessBoxWidth * currentScale;
                            boxHeight = patternInfo->stripRearThicknessBoxHeight * currentScale;
                        } else {
                            double angleRad = patternInfo->angle * M_PI / 180.0;
                            double cosA = std::abs(std::cos(angleRad));
                            double sinA = std::abs(std::sin(angleRad));
                            double boundingWidth = patternInfo->stripRearThicknessBoxWidth * cosA + patternInfo->stripRearThicknessBoxHeight * sinA;
                            double boundingHeight = patternInfo->stripRearThicknessBoxWidth * sinA + patternInfo->stripRearThicknessBoxHeight * cosA;
                            boxWidth = boundingWidth * currentScale;
                            boxHeight = boundingHeight * currentScale;
                        }
                    }
                    

                    
                    painter.save();
                    painter.translate(rearBoxCenterVP);
                    painter.rotate(insAngle);
                    
                    QPen rearPen(QColor(0, 191, 255), 2);
                    rearPen.setStyle(Qt::DashLine);
                    painter.setPen(rearPen);
                    painter.setBrush(QColor(0, 255, 0, 80));  // 녹색 반투명으로 채우기
                    painter.drawRect(-boxWidth/2, -boxHeight/2, boxWidth, boxHeight);
                    
                    // ===== REAR 스캔 라인 그리기 (옵션) =====
                    if (result.stripRearThicknessPoints.contains(patternId)) {
                        const QList<QPoint>& scanLines = result.stripRearThicknessPoints[patternId];
                        
                        // scanLines는 2개씩 쌍으로 저장됨 (라인시작, 라인끝)
                        painter.setPen(QPen(QColor(0, 180, 0), 0.5));  // 더 어두운 초록색
                        painter.setBrush(Qt::NoBrush);
                        
                        double rad = -insAngle * M_PI / 180.0;
                        double cosA = std::cos(rad);
                        double sinA = std::sin(rad);
                        
                        for (int i = 0; i < scanLines.size(); i += 2) {
                            if (i + 1 < scanLines.size()) {
                                QPoint pt1Scene = scanLines[i];
                                QPoint pt2Scene = scanLines[i + 1];
                                
                                QPointF pt1VP = mapFromScene(QPointF(pt1Scene.x(), pt1Scene.y()));
                                QPointF pt2VP = mapFromScene(QPointF(pt2Scene.x(), pt2Scene.y()));
                                
                                // 박스 중심 기준으로 상대 좌표 계산
                                QPointF rel1 = pt1VP - rearBoxCenterVP;
                                QPointF rel2 = pt2VP - rearBoxCenterVP;
                                
                                // 역회전 적용
                                double rot1X = rel1.x() * cosA - rel1.y() * sinA;
                                double rot1Y = rel1.x() * sinA + rel1.y() * cosA;
                                double rot2X = rel2.x() * cosA - rel2.y() * sinA;
                                double rot2Y = rel2.x() * sinA + rel2.y() * cosA;
                                
                                // REAR 검사는 역방향이므로 Y를 반전
                                rot1Y = -rot1Y;
                                rot2Y = -rot2Y;
                                
                                painter.drawLine(QPointF(rot1X, rot1Y), QPointF(rot2X, rot2Y));
                            }
                        }
                    }
                    
                    // REAR 라벨에 최소/최대/평균 값 표시
                    int rearMeasuredMin = result.stripRearMeasuredThicknessMin.value(patternId, 0);
                    int rearMeasuredMax = result.stripRearMeasuredThicknessMax.value(patternId, 0);
                    int rearMeasuredAvg = result.stripRearMeasuredThicknessAvg.value(patternId, 0);
                    
                    QString rearLabel;
                    // 픽셀을 mm로 변환하여 표시
                    if (patternInfo->stripLengthCalibrationPx > 0) {
                        double pixelToMm = patternInfo->stripLengthConversionMm / patternInfo->stripLengthCalibrationPx;
                        double minMm = rearMeasuredMin * pixelToMm;
                        double maxMm = rearMeasuredMax * pixelToMm;
                        double avgMm = rearMeasuredAvg * pixelToMm;
                        
                        // 1mm 이하는 μm 단위로 표시
                        if (minMm < 1.0 && maxMm < 1.0 && avgMm < 1.0) {
                            rearLabel = QString("REAR Min:%1 Max:%2 Avg:%3μm")
                                .arg(minMm * 1000, 0, 'f', 0)
                                .arg(maxMm * 1000, 0, 'f', 0)
                                .arg(avgMm * 1000, 0, 'f', 0);
                        } else {
                            rearLabel = QString("REAR Min:%1 Max:%2 Avg:%3mm")
                                .arg(minMm, 0, 'f', 2)
                                .arg(maxMm, 0, 'f', 2)
                                .arg(avgMm, 0, 'f', 2);
                        }
                    } else {
                        // calibration 없으면 픽셀값 표시
                        rearLabel = QString("REAR Min:%1 Max:%2 Avg:%3px")
                            .arg(rearMeasuredMin)
                            .arg(rearMeasuredMax)
                            .arg(rearMeasuredAvg);
                    }
                    
                    QFont boxFont(NAMEPLATE_FONT_FAMILY, NAMEPLATE_FONT_SIZE, NAMEPLATE_FONT_WEIGHT);
                    painter.setFont(boxFont);
                    QFontMetrics boxFm(boxFont);
                    int rearTextW = boxFm.horizontalAdvance(rearLabel);
                    int rearTextH = boxFm.height();
                    
                    QRect rearTextRect(-rearTextW/2 - 2, -boxHeight/2 - rearTextH - 2, rearTextW + 4, rearTextH);
                    painter.fillRect(rearTextRect, QBrush(QColor(0, 0, 0, 180)));
                    painter.setPen(QColor(255, 255, 255));  // 흰색
                    painter.drawText(rearTextRect, Qt::AlignCenter, rearLabel);
                    
                    // PASS/NG 표시 (이름표 위) - 판정 다시 계산
                    bool rearPassed = true;
                    if (patternInfo->stripLengthCalibrationPx > 0) {
                        int rearMeasuredMin = result.stripRearMeasuredThicknessMin.value(patternId, 0);
                        int rearMeasuredMax = result.stripRearMeasuredThicknessMax.value(patternId, 0);
                        double pixelToMm = patternInfo->stripLengthConversionMm / patternInfo->stripLengthCalibrationPx;
                        double minMm = rearMeasuredMin * pixelToMm;
                        double maxMm = rearMeasuredMax * pixelToMm;
                        rearPassed = (minMm >= patternInfo->stripRearThicknessMin && 
                                     maxMm <= patternInfo->stripRearThicknessMax);
                    }
                    
                    QString rearPassText = rearPassed ? "PASS" : "NG";
                    QColor rearPassColor = rearPassed ? QColor(0, 255, 0) : QColor(255, 0, 0);
                    int passTextW = boxFm.horizontalAdvance(rearPassText);
                    
                    QRect rearPassRect(-passTextW/2 - 2, -boxHeight/2 - rearTextH*2 - 4, passTextW + 4, rearTextH);
                    painter.fillRect(rearPassRect, QBrush(QColor(0, 0, 0, 180)));
                    painter.setPen(rearPassColor);
                    painter.drawText(rearPassRect, Qt::AlignCenter, rearPassText);
                    
                    painter.restore();
                }
            }
            
    // ===== 스캔 라인 시각화 (디버그) =====
    // FRONT 스캔 라인
    if (result.stripFrontScanLines.contains(patternId)) {
        const QList<QPair<QPoint, QPoint>>& scanLines = result.stripFrontScanLines[patternId];
        painter.setPen(QPen(QColor(0, 255, 255, 100), 1));  // 반투명 시안색
        
        for (const auto& line : scanLines) {
            QPointF pt1VP = mapFromScene(QPointF(line.first.x(), line.first.y()));
            QPointF pt2VP = mapFromScene(QPointF(line.second.x(), line.second.y()));
            painter.drawLine(pt1VP, pt2VP);
        }
    }
    
    // REAR 스캔 라인
    if (result.stripRearScanLines.contains(patternId)) {
        const QList<QPair<QPoint, QPoint>>& scanLines = result.stripRearScanLines[patternId];
        painter.setPen(QPen(QColor(255, 255, 0, 100), 1));  // 반투명 노란색
        
        for (const auto& line : scanLines) {
            QPointF pt1VP = mapFromScene(QPointF(line.first.x(), line.first.y()));
            QPointF pt2VP = mapFromScene(QPointF(line.second.x(), line.second.y()));
            painter.drawLine(pt1VP, pt2VP);
        }
    }
            
    // ===== 4. STRIP 4개 컨투어 포인트 그리기 =====
            if (result.stripPointsValid.value(patternId, false)) {
                QVector<QPoint> stripPoints;
                QPointF topRightScene(inspRectScene.right(), inspRectScene.top());
                QPointF bottomLeftScene(inspRectScene.left(), inspRectScene.bottom());
                QPointF bottomRightScene = inspRectScene.bottomRight();
                
                // Viewport로 변환
                QPointF topLeftVP = mapFromScene(topLeftScene);
                QPointF topRightVP = mapFromScene(topRightScene);
                QPointF bottomLeftVP = mapFromScene(bottomLeftScene);
                QPointF bottomRightVP = mapFromScene(bottomRightScene);
                
                // 회전 각도를 라디안으로 변환
                double radians = insAngle * M_PI / 180.0;
                double cosA = std::cos(radians);
                double sinA = std::sin(radians);
                
                // 회전 변환 함수
                auto rotatePointFunc = [&](QPointF pt, QPointF center) -> QPointF {
                    double dx = pt.x() - center.x();
                    double dy = pt.y() - center.y();
                    double newX = center.x() + dx * cosA - dy * sinA;
                    double newY = center.y() + dx * sinA + dy * cosA;
                    return QPointF(newX, newY);
                };
                
                QPointF rotTopLeftVP = rotatePointFunc(topLeftVP, centerViewport);
                QPointF rotTopRightVP = rotatePointFunc(topRightVP, centerViewport);
                QPointF rotBottomLeftVP = rotatePointFunc(bottomLeftVP, centerViewport);
                QPointF rotBottomRightVP = rotatePointFunc(bottomRightVP, centerViewport);
                
                // 가로 방향 벡터 (회전 후)
                double widthVectorX = rotTopRightVP.x() - rotTopLeftVP.x();
                double widthVectorY = rotTopRightVP.y() - rotTopLeftVP.y();
                double vectorLen = std::sqrt(widthVectorX * widthVectorX + widthVectorY * widthVectorY);
                
                // FRONT 박스 중심 (Viewport 좌표) - Yellow 박스에서도 재사용
                QPointF frontBoxCenterVP;
                
                if (vectorLen > 0.01) {
                    // gradient 시작점 (FRONT 검사 중심) - stripGradientStartPercent (보통 20%)
                    float startPercent = patternInfo->stripGradientStartPercent / 100.0f;
                    QPointF posStartTop(
                        rotTopLeftVP.x() + widthVectorX * startPercent,
                        rotTopLeftVP.y() + widthVectorY * startPercent
                    );
                    QPointF posStartBottom(
                        rotBottomLeftVP.x() + widthVectorX * startPercent,
                        rotBottomLeftVP.y() + widthVectorY * startPercent
                    );
                    frontBoxCenterVP = QPointF(
                        (posStartTop.x() + posStartBottom.x()) / 2.0f,
                        (posStartTop.y() + posStartBottom.y()) / 2.0f
                    );
                    
                    // zoom scale 적용
                    QTransform t = transform();
                    double currentScale = std::sqrt(t.m11() * t.m11() + t.m12() * t.m12());
                    
                    // ImageProcessor가 계산한 바운딩 박스 크기 사용 (Yellow 박스와 동일)
                    double boxWidth = 0, boxHeight = 0;

                    if (result.stripFrontBoxSize.contains(patternId)) {
                        QSizeF frontBoxSize = result.stripFrontBoxSize[patternId];
                        boxWidth = frontBoxSize.width() * currentScale;
                        boxHeight = frontBoxSize.height() * currentScale;

                    } else {

                        // fallback: 원본 크기에서 바운딩 박스 계산
                        if (std::abs(patternInfo->angle) < 0.1) {
                            boxWidth = patternInfo->stripThicknessBoxWidth * currentScale;
                            boxHeight = patternInfo->stripThicknessBoxHeight * currentScale;
                        } else {
                            double angleRad = patternInfo->angle * M_PI / 180.0;
                            double cosA = std::abs(std::cos(angleRad));
                            double sinA = std::abs(std::sin(angleRad));
                            double boundingWidth = patternInfo->stripThicknessBoxWidth * cosA + patternInfo->stripThicknessBoxHeight * sinA;
                            double boundingHeight = patternInfo->stripThicknessBoxWidth * sinA + patternInfo->stripThicknessBoxHeight * cosA;
                            boxWidth = boundingWidth * currentScale;
                            boxHeight = boundingHeight * currentScale;
                        }
                    }
                    
                    painter.save();
                    painter.translate(frontBoxCenterVP);
                    painter.rotate(insAngle);
                    
                    QPen frontPen(Qt::cyan, 2);
                    frontPen.setStyle(Qt::DashLine);
                    painter.setPen(frontPen);
                    painter.setBrush(QColor(0, 255, 0, 80));  // 녹색 반투명으로 채우기
                    painter.drawRect(-boxWidth/2, -boxHeight/2, boxWidth, boxHeight);
                    
                    // ===== FRONT 스캔 라인 그리기 (옵션) =====
                    if (result.stripFrontThicknessPoints.contains(patternId)) {
                        const QList<QPoint>& scanLines = result.stripFrontThicknessPoints[patternId];
                        
                        // scanLines는 2개씩 쌍으로 저장됨 (라인시작, 라인끝)
                        painter.setPen(QPen(QColor(0, 180, 0), 0.5));  // 더 어두운 초록색
                        painter.setBrush(Qt::NoBrush);
                        
                        double rad = -insAngle * M_PI / 180.0;
                        double cosA = std::cos(rad);
                        double sinA = std::sin(rad);
                        
                        for (int i = 0; i < scanLines.size(); i += 2) {
                            if (i + 1 < scanLines.size()) {
                                QPoint pt1Scene = scanLines[i];
                                QPoint pt2Scene = scanLines[i + 1];
                                
                                QPointF pt1VP = mapFromScene(QPointF(pt1Scene.x(), pt1Scene.y()));
                                QPointF pt2VP = mapFromScene(QPointF(pt2Scene.x(), pt2Scene.y()));
                                
                                // 박스 중심 기준으로 상대 좌표 계산
                                QPointF rel1 = pt1VP - frontBoxCenterVP;
                                QPointF rel2 = pt2VP - frontBoxCenterVP;
                                
                                // 역회전 적용
                                double rot1X = rel1.x() * cosA - rel1.y() * sinA;
                                double rot1Y = rel1.x() * sinA + rel1.y() * cosA;
                                double rot2X = rel2.x() * cosA - rel2.y() * sinA;
                                double rot2Y = rel2.x() * sinA + rel2.y() * cosA;
                                
                                painter.drawLine(QPointF(rot1X, rot1Y), QPointF(rot2X, rot2Y));
                            }
                        }
                    }

                    // FRONT 라벨에 최소/최대/평균 값 표시
                    int frontMeasuredMin = result.stripMeasuredThicknessMin.value(patternId, 0);
                    int frontMeasuredMax = result.stripMeasuredThicknessMax.value(patternId, 0);
                    int frontMeasuredAvg = result.stripMeasuredThicknessAvg.value(patternId, 0);
                    
                    QString frontLabel;
                    // 픽셀을 mm로 변환하여 표시
                    if (patternInfo->stripLengthCalibrationPx > 0) {
                        double pixelToMm = patternInfo->stripLengthConversionMm / patternInfo->stripLengthCalibrationPx;
                        double minMm = frontMeasuredMin * pixelToMm;
                        double maxMm = frontMeasuredMax * pixelToMm;
                        double avgMm = frontMeasuredAvg * pixelToMm;
                        
                        // 1mm 이하는 μm 단위로 표시
                        if (minMm < 1.0 && maxMm < 1.0 && avgMm < 1.0) {
                            frontLabel = QString("FRONT Min:%1 Max:%2 Avg:%3μm")
                                .arg(minMm * 1000, 0, 'f', 0)
                                .arg(maxMm * 1000, 0, 'f', 0)
                                .arg(avgMm * 1000, 0, 'f', 0);
                        } else {
                            frontLabel = QString("FRONT Min:%1 Max:%2 Avg:%3mm")
                                .arg(minMm, 0, 'f', 2)
                                .arg(maxMm, 0, 'f', 2)
                                .arg(avgMm, 0, 'f', 2);
                        }
                    } else {
                        // calibration 없으면 픽셀값 표시
                        frontLabel = QString("FRONT Min:%1 Max:%2 Avg:%3px")
                            .arg(frontMeasuredMin)
                            .arg(frontMeasuredMax)
                            .arg(frontMeasuredAvg);
                    }
                    
                    QFont boxFont(NAMEPLATE_FONT_FAMILY, NAMEPLATE_FONT_SIZE, NAMEPLATE_FONT_WEIGHT);
                    painter.setFont(boxFont);
                    QFontMetrics boxFm(boxFont);
                    int frontTextW = boxFm.horizontalAdvance(frontLabel);
                    int frontTextH = boxFm.height();
                    
                    QRect frontTextRect(-frontTextW/2 - 2, -boxHeight/2 - frontTextH - 2, frontTextW + 4, frontTextH);
                    painter.fillRect(frontTextRect, QBrush(QColor(0, 0, 0, 180)));
                    painter.setPen(QColor(255, 255, 255));  // 흰색
                    painter.drawText(frontTextRect, Qt::AlignCenter, frontLabel);
                    
                    // PASS/NG 표시 (이름표 위) - 판정 다시 계산
                    bool frontPassed = true;
                    if (patternInfo->stripLengthCalibrationPx > 0) {
                        int frontMeasuredMin = result.stripMeasuredThicknessMin.value(patternId, 0);
                        int frontMeasuredMax = result.stripMeasuredThicknessMax.value(patternId, 0);
                        double pixelToMm = patternInfo->stripLengthConversionMm / patternInfo->stripLengthCalibrationPx;
                        double minMm = frontMeasuredMin * pixelToMm;
                        double maxMm = frontMeasuredMax * pixelToMm;
                        frontPassed = (minMm >= patternInfo->stripThicknessMin && 
                                      maxMm <= patternInfo->stripThicknessMax);
                    }
                    
                    QString frontPassText = frontPassed ? "PASS" : "NG";
                    QColor frontPassColor = frontPassed ? QColor(0, 255, 0) : QColor(255, 0, 0);
                    int passTextW = boxFm.horizontalAdvance(frontPassText);
                    
                    QRect frontPassRect(-passTextW/2 - 2, -boxHeight/2 - frontTextH*2 - 4, passTextW + 4, frontTextH);
                    painter.fillRect(frontPassRect, QBrush(QColor(0, 0, 0, 180)));
                    painter.setPen(frontPassColor);
                    painter.drawText(frontPassRect, Qt::AlignCenter, frontPassText);
                    
                    painter.restore();
                }
            }
            
    // ===== 4. STRIP 4개 컨투어 포인트 그리기 =====
            if (result.stripPointsValid.value(patternId, false)) {
                QVector<QPoint> stripPoints;
                if (result.stripPoint1.contains(patternId)) stripPoints.push_back(result.stripPoint1[patternId]);
                if (result.stripPoint2.contains(patternId)) stripPoints.push_back(result.stripPoint2[patternId]);
                if (result.stripPoint3.contains(patternId)) stripPoints.push_back(result.stripPoint3[patternId]);
                if (result.stripPoint4.contains(patternId)) stripPoints.push_back(result.stripPoint4[patternId]);
                
                if (stripPoints.size() == 4) {
                    // stripPoints는 절대좌표이므로 직접 뷰포트 좌표로 변환만 수행
                    // (이미 검사 결과에서 패턴 각도가 반영되어 있으므로 추가 회전 금지!)
                    QVector<QPointF> vpPoints;
                    for (int i = 0; i < 4; i++) {
                        QPoint absPoint = stripPoints[i];
                        
                        // 절대좌표를 씬 좌표로 변환 (이미 절대좌표)
                        QPointF pointScene(absPoint.x(), absPoint.y());
                        
                        // 씬 좌표 -> 뷰포트 좌표 변환 (이것만 적용!)
                        QPointF pointVP = mapFromScene(pointScene);
                        vpPoints.push_back(pointVP);
                    }
                    
                    // 각 포인트에 번호 표시
                    QFont pointFont(NAMEPLATE_FONT_FAMILY, NAMEPLATE_FONT_SIZE, NAMEPLATE_FONT_WEIGHT);
                    painter.setFont(pointFont);
                    
                    QStringList pointLabels = {"P1", "P2", "P3", "P4"};
                    QVector<QColor> pointColors = {
                        QColor(255, 255, 0),   // P1: 노랑
                        QColor(0, 255, 255),   // P2: 시안
                        QColor(255, 0, 255),   // P3: 마젠타
                        QColor(0, 255, 0)      // P4: 초록
                    };
                    
                    for (int i = 0; i < 4; i++) {
                        // 포인트 마커 (원)
                        painter.setBrush(pointColors[i]);
                        painter.setPen(QPen(pointColors[i], 2));
                        painter.drawEllipse(vpPoints[i], 6, 6);
                        
                        // 포인트 라벨
                        QFontMetrics fm(pointFont);
                        int textW = fm.horizontalAdvance(pointLabels[i]);
                        int textH = fm.height();
                        
                        QRectF labelRect(vpPoints[i].x() + 8,
                                       vpPoints[i].y() - textH/2,
                                       textW + 4, textH);
                        painter.fillRect(labelRect, QBrush(QColor(0, 0, 0, 180)));
                        painter.setPen(pointColors[i]);
                        painter.drawText(labelRect, Qt::AlignCenter, pointLabels[i]);
                    }
                }
            }
            
    // ===== 스캔 라인 시각화 (디버그) =====
    // FRONT 스캔 라인
    if (result.stripFrontScanLines.contains(patternId)) {
        const QList<QPair<QPoint, QPoint>>& scanLines = result.stripFrontScanLines[patternId];
        painter.setPen(QPen(QColor(0, 255, 255, 100), 1));  // 반투명 시안색
        
        for (const auto& line : scanLines) {
            QPointF pt1VP = mapFromScene(QPointF(line.first.x(), line.first.y()));
            QPointF pt2VP = mapFromScene(QPointF(line.second.x(), line.second.y()));
            painter.drawLine(pt1VP, pt2VP);
        }
    }
    
    // REAR 스캔 라인
    if (result.stripRearScanLines.contains(patternId)) {
        const QList<QPair<QPoint, QPoint>>& scanLines = result.stripRearScanLines[patternId];
        painter.setPen(QPen(QColor(255, 255, 0, 100), 1));  // 반투명 노란색
        
        for (const auto& line : scanLines) {
            QPointF pt1VP = mapFromScene(QPointF(line.first.x(), line.first.y()));
            QPointF pt2VP = mapFromScene(QPointF(line.second.x(), line.second.y()));
            painter.drawLine(pt1VP, pt2VP);
        }
    }
            
            // 두께 검사 결과는 박스 채우기로 표현 (아래 FRONT/REAR 박스 시각화에서 처리)
            
    // ===== 5. EDGE 박스 시각화 (심선 끝 절단면 품질 검사) =====
            if (result.edgeBoxCenter.contains(patternId) && result.edgeBoxSize.contains(patternId)) {
                QPointF edgeBoxCenterRel = result.edgeBoxCenter[patternId];
                QSizeF edgeBoxSize = result.edgeBoxSize[patternId];
                
                QPointF edgeBoxCenterScene = patternCenterScene + edgeBoxCenterRel;
                QPointF edgeCenterViewport = mapFromScene(edgeBoxCenterScene);
                QPointF edgeRotatedCenter = rotatePoint(edgeCenterViewport, centerViewport);
                
                // ===== EDGE 노란색 박스 (축정렬, 회전 투영 고려) =====
                QTransform t = transform();
                double currentScale = std::sqrt(t.m11() * t.m11() + t.m12() * t.m12());
                
                // 회전 투영을 적용하여 노란색 박스 크기 계산 (REAR/FRONT와 동일)
                double w = edgeBoxSize.width();
                double h = edgeBoxSize.height();
                double projX = std::abs(w * cosA) + std::abs(h * sinA);
                double projY = std::abs(w * sinA) + std::abs(h * cosA);
                double edgeYellowWidth = projX * currentScale;
                double edgeYellowHeight = projY * currentScale;
                
                // 노란색 박스 (축정렬) - edgeCenterViewport 기준으로 그리기
                painter.setPen(QPen(QColor(255, 255, 0), 1.5));
                painter.setBrush(Qt::NoBrush);
                
                QPointF edgeTopLeft(edgeCenterViewport.x() - edgeYellowWidth/2, edgeCenterViewport.y() - edgeYellowHeight/2);
                QPointF edgeTopRight(edgeCenterViewport.x() + edgeYellowWidth/2, edgeCenterViewport.y() - edgeYellowHeight/2);
                QPointF edgeBottomLeft(edgeCenterViewport.x() - edgeYellowWidth/2, edgeCenterViewport.y() + edgeYellowHeight/2);
                QPointF edgeBottomRight(edgeCenterViewport.x() + edgeYellowWidth/2, edgeCenterViewport.y() + edgeYellowHeight/2);
                
                QPolygonF edgeYellowPolygon;
                edgeYellowPolygon << edgeTopLeft << edgeTopRight << edgeBottomRight << edgeBottomLeft;
                painter.drawPolygon(edgeYellowPolygon);
                
                // ===== EDGE 청록색 박스 (회전) =====
                int edgeBoxWidth = int(edgeBoxSize.width() * currentScale);
                int edgeBoxHeight = int(edgeBoxSize.height() * currentScale);
                
                painter.save();
                painter.translate(edgeCenterViewport);
                painter.rotate(insAngle);
                painter.translate(-edgeCenterViewport);
                
                QPen edgePen(QColor(255, 128, 0), 2);
                edgePen.setStyle(Qt::DashLine);
                painter.setPen(edgePen);
                painter.setBrush(Qt::NoBrush);  // 내부 칠하지 않음
                painter.drawRect(QRectF(edgeCenterViewport.x() - edgeBoxWidth/2,
                                       edgeCenterViewport.y() - edgeBoxHeight/2,
                                       edgeBoxWidth, edgeBoxHeight));
                
                int edgeOutlierCount = result.edgeIrregularityCount.value(patternId, 0);
                double edgeMaxDev = result.edgeMaxDeviation.value(patternId, 0.0);
                double edgeMinDev = result.edgeMinDeviation.value(patternId, 0.0);
                double edgeAvgDev = result.edgeAvgDeviation.value(patternId, 0.0);
                bool edgePassed = result.edgeResults.value(patternId, false);
                
                // 패턴 정보에서 최대 허용 불량 수 가져오기
                int maxOutliers = 5;  // 기본값
                for (const PatternInfo& p : patterns) {
                    if (p.id == patternId) {
                        maxOutliers = p.edgeMaxOutliers;
                        break;
                    }
                }
                
                QString edgeLabel = QString("EDGE: Max:%1 Avg:%2mm [%3/%4]")
                    .arg(edgeMaxDev, 0, 'f', 2)
                    .arg(edgeAvgDev, 0, 'f', 2)
                    .arg(edgeOutlierCount)
                    .arg(maxOutliers);
                
                QFont boxFont(NAMEPLATE_FONT_FAMILY, NAMEPLATE_FONT_SIZE, NAMEPLATE_FONT_WEIGHT);
                painter.setFont(boxFont);
                QFontMetrics boxFm(boxFont);
                int edgeTextW = boxFm.horizontalAdvance(edgeLabel);
                int edgeTextH = boxFm.height();
                
                // PASS/NG에 따라 배경색 변경
                QRectF edgeLabelRect(edgeCenterViewport.x() - edgeTextW/2 - 3,
                                    edgeCenterViewport.y() - edgeYellowHeight/2 - edgeTextH - 5,
                                    edgeTextW + 6, edgeTextH);
                painter.fillRect(edgeLabelRect, QBrush(QColor(0, 0, 0, 180)));
                painter.setPen(QColor(255, 255, 255));  // 흰색
                painter.drawText(edgeLabelRect, Qt::AlignCenter, edgeLabel);
                
                // PASS/NG 표시 (이름표 위)
                QString edgePassText = edgePassed ? "PASS" : "NG";
                QColor edgePassColor = edgePassed ? QColor(0, 255, 0) : QColor(255, 0, 0);
                int passTextW = boxFm.horizontalAdvance(edgePassText);
                
                QRectF edgePassRect(edgeCenterViewport.x() - passTextW/2 - 3,
                                   edgeCenterViewport.y() - edgeYellowHeight/2 - edgeTextH*2 - 7,
                                   passTextW + 6, edgeTextH);
                painter.fillRect(edgePassRect, QBrush(QColor(0, 0, 0, 180)));
                painter.setPen(edgePassColor);
                painter.drawText(edgePassRect, Qt::AlignCenter, edgePassText);
                
                painter.restore();
            }
            
            // ===== EDGE 포인트들 그리기 =====
            if (result.edgeAbsolutePoints.contains(patternId)) {
                const QList<QPoint>& edgePoints = result.edgeAbsolutePoints[patternId];
                
                // EDGE 박스 정보 가져오기
                QPointF edgeBoxCenterRel(0, 0);
                QSizeF edgeBoxSize(0, 0);
                if (result.edgeBoxCenter.contains(patternId) && result.edgeBoxSize.contains(patternId)) {
                    edgeBoxCenterRel = result.edgeBoxCenter[patternId];
                    edgeBoxSize = result.edgeBoxSize[patternId];
                }
                
                // 패턴 정보 가져오기 (거리 파라미터용)
                PatternInfo currentPattern;
                bool patternFound = false;
                for (const PatternInfo& p : patterns) {
                    if (p.id == patternId) {
                        currentPattern = p;
                        patternFound = true;
                        break;
                    }
                }
                
                if (patternFound && !edgePoints.isEmpty()) {
                    // 각 포인트별 거리 정보 가져오기 (InsProcessor에서 계산한 값)
                    QList<double> pointDistances;
                    if (result.edgePointDistances.contains(patternId)) {
                        pointDistances = result.edgePointDistances[patternId];
                    }
                    
                    // 각 포인트를 그리면서 Y 범위 추적
                    int firstDrawnY = -1;
                    int lastDrawnY = -1;
                    
                    for (int i = 0; i < edgePoints.size(); i++) {
                        const QPoint& pt = edgePoints[i];
                        
                        // InsProcessor에서 계산한 거리 사용
                        double distanceMm = 0.0;
                        if (i < pointDistances.size()) {
                            distanceMm = pointDistances[i];
                        }
                        
                        // 거리에 따른 색상 결정 (mm 기준으로 최대값 체크)
                        QColor pointColor;
                        if (distanceMm > currentPattern.edgeDistanceMax) {
                            // 최대 거리 초과 - 빨간색 (불량)
                            pointColor = QColor(255, 0, 0);
                        } else if (distanceMm > currentPattern.edgeDistanceMax * 0.7) {
                            // 최대 거리의 70% 이상 - 주황색 (주의)
                            pointColor = QColor(255, 165, 0);
                        } else {
                            // 정상 - 녹색 (양호)
                            pointColor = QColor(0, 255, 0);
                        }
                        
                        // 포인트 표시
                        QPointF ptScene(pt.x(), pt.y());
                        QPointF ptVP = mapFromScene(ptScene);
                        
                        painter.setPen(QPen(pointColor, 1));
                        painter.setBrush(QBrush(pointColor));
                        painter.drawEllipse(ptVP, 3, 3);
                        
                        // Y 범위 추적
                        if (firstDrawnY == -1) {
                            firstDrawnY = pt.y();
                        }
                        lastDrawnY = pt.y();
                    }
                    
                    // 선형 회귀 직선 그리기 (y = mx + b)
                    QPointF avgLineCenter;  // STRIP 길이 측정용으로 저장
                    bool hasAvgLineCenter = false;
                    
                    if (firstDrawnY != -1 && lastDrawnY != -1 && 
                        result.edgeRegressionSlope.contains(patternId) && 
                        result.edgeRegressionIntercept.contains(patternId)) {
                        
                        double m = result.edgeRegressionSlope[patternId];
                        double b = result.edgeRegressionIntercept[patternId];
                        
                        // Y 범위에 대응하는 X 계산 (y = mx + b => x = (y - b) / m)
                        double x1 = (m != 0) ? (firstDrawnY - b) / m : result.edgeAverageX.value(patternId, 0);
                        double x2 = (m != 0) ? (lastDrawnY - b) / m : result.edgeAverageX.value(patternId, 0);
                        
                        QPointF lineTop(x1, firstDrawnY);
                        QPointF lineBottom(x2, lastDrawnY);
                        
                        // Scene 좌표로 변환
                        QPointF lineTopVP = mapFromScene(lineTop);
                        QPointF lineBottomVP = mapFromScene(lineBottom);
                        
                        // 평균선의 중간점
                        avgLineCenter = (lineTopVP + lineBottomVP) / 2.0;
                        hasAvgLineCenter = true;
                        
                        // EDGE 평균선 그리기 (노란색 점선)
                        QPen avgLinePen(QColor(255, 255, 0), 2);  // 노란색, 2px
                        avgLinePen.setStyle(Qt::DashLine);
                        painter.setPen(avgLinePen);
                        painter.drawLine(lineTopVP, lineBottomVP);
                        
                        // ========== STRIP 탈피 길이 측정 선 ==========
                        // EDGE 평균선 중심점을 시작점으로 사용
                        if (result.stripLengthEndPoint.contains(patternId)) {
                            QPoint endPt = result.stripLengthEndPoint[patternId];
                            
                            // Scene 좌표로 변환
                            QPointF endScene(endPt.x(), endPt.y());
                            QPointF endVP = mapFromScene(endScene);
                            
                            // 마젠타색 실선으로 길이 측정선 그리기 (회전 없이 직선 연결)
                            QPen lengthPen(QColor(255, 0, 255), 2);  // 마젠타색, 2px
                            painter.setPen(lengthPen);
                            painter.drawLine(avgLineCenter, endVP);
                            
                            // 시작점과 끝점에 작은 원 표시
                            painter.setBrush(QBrush(QColor(255, 0, 255)));
                            painter.drawEllipse(avgLineCenter, 4, 4);
                            painter.drawEllipse(endVP, 4, 4);
                            
                            // 측정된 길이 텍스트 표시
                            if (result.stripMeasuredLength.contains(patternId)) {
                                double measuredValue = result.stripMeasuredLength[patternId];
                                
                                // 패턴 정보 가져오기
                                QString lengthText;
                                const PatternInfo* pattern = nullptr;
                                for (const PatternInfo& p : patterns) {
                                    if (p.id == patternId) {
                                        pattern = &p;
                                        break;
                                    }
                                }
                                
                                if (pattern && pattern->stripLengthCalibrated && 
                                    pattern->stripLengthCalibrationPx > 0.0 && 
                                    pattern->stripLengthConversionMm > 0.0 &&
                                    std::isfinite(measuredValue)) {
                                    // 캘리브레이션 완료: mm 값과 픽셀 값 함께 표시
                                    double mmToPixel = pattern->stripLengthCalibrationPx / pattern->stripLengthConversionMm;
                                    double lengthPx = measuredValue * mmToPixel;
                                    lengthText = QString("%1 mm (%2 px)")
                                        .arg(measuredValue, 0, 'f', 2)
                                        .arg(lengthPx, 0, 'f', 1);
                                } else {
                                    // 캘리브레이션 전: 픽셀 값만 표시
                                    if (std::isfinite(measuredValue)) {
                                        lengthText = QString("%1 px").arg(measuredValue, 0, 'f', 1);
                                    } else {
                                        lengthText = "ERROR";  // 무한대 또는 NaN인 경우
                                    }
                                }                                // 선의 중간 지점에 텍스트 표시 (INS 각도 적용)
                                QPointF midPoint = (avgLineCenter + endVP) / 2.0;
                                
                                // INS 각도 적용 (패턴 정보에서)
                                const PatternInfo* patternInfo = nullptr;
                                for (const PatternInfo& p : patterns) {
                                    if (p.id == patternId) {
                                        patternInfo = &p;
                                        break;
                                    }
                                }
                                
                                if (patternInfo) {
                                    painter.save();
                                    
                                    // PASS/NG 판정
                                    bool lengthPassed = result.stripLengthResults.value(patternId, false);
                                    
                                    // midPoint를 중심으로 회전
                                    painter.translate(midPoint);
                                    painter.rotate(patternInfo->angle);
                                    
                                    QFont lengthFont(NAMEPLATE_FONT_FAMILY, NAMEPLATE_FONT_SIZE, NAMEPLATE_FONT_WEIGHT);
                                    painter.setFont(lengthFont);
                                    QFontMetrics fm(lengthFont);
                                    
                                    // "LEN: " 접두어 추가
                                    QString lengthLabelText = QString("LEN: %1").arg(lengthText);
                                    int lengthTextW = fm.horizontalAdvance(lengthLabelText);
                                    int lengthTextH = fm.height();
                                    
                                    // 길이 값 표시 (아래쪽)
                                    QRect lengthRect(-lengthTextW/2 - 5,
                                                    5,
                                                    lengthTextW + 10,
                                                    lengthTextH + 6);
                                    painter.fillRect(lengthRect, QBrush(QColor(0, 0, 0, 180)));
                                    painter.setPen(QColor(255, 255, 255));  // 흰색
                                    painter.drawText(lengthRect, Qt::AlignCenter, lengthLabelText);
                                    
                                    // PASS/NG 표시 (위쪽)
                                    QString lengthPassText = lengthPassed ? "PASS" : "NG";
                                    QColor lengthPassColor = lengthPassed ? QColor(0, 255, 0) : QColor(255, 0, 0);
                                    int passTextW = fm.horizontalAdvance(lengthPassText);
                                    
                                    QRect passRect(-passTextW/2 - 5,
                                                  -lengthTextH - 5,
                                                  passTextW + 10,
                                                  lengthTextH + 6);
                                    painter.fillRect(passRect, QBrush(QColor(0, 0, 0, 180)));
                                    painter.setPen(lengthPassColor);
                                    painter.drawText(passRect, Qt::AlignCenter, lengthPassText);
                                    
                                    painter.restore();
                                }
                            }
                        }
                    }
                
                // ===== INS CRIMP 검사 결과 시각화 =====
                }
            
            // ===== EDGE 검사 결과 시각화 (DIFF MASK) =====
            if (patternInfo->inspectionMethod == InspectionMethod::DIFF && 
                result.diffMask.contains(patternId)) {
                
                cv::Mat diffMaskMat = result.diffMask[patternId];
                if (!diffMaskMat.empty()) {
                    // zoom scale 적용
                    QTransform t = transform();
                    double currentScale = std::sqrt(t.m11() * t.m11() + t.m12() * t.m12());
                    
                    // INS 패턴 정보
                    QRectF rectF = patternInfo->rect;
                    QPointF center(rectF.x() + rectF.width() / 2.0, rectF.y() + rectF.height() / 2.0);
                    QPointF viewCenter = mapFromScene(center);
                    
                    double insWidth = rectF.width() * currentScale;
                    double insHeight = rectF.height() * currentScale;
                    
                    // diffMask 크기 (스케일 적용)
                    int scaledWidth = static_cast<int>(diffMaskMat.cols * currentScale);
                    int scaledHeight = static_cast<int>(diffMaskMat.rows * currentScale);
                    
                    QPointF topLeft(viewCenter.x() - scaledWidth / 2.0, 
                                   viewCenter.y() - scaledHeight / 2.0);
                    
                    // 회전각 (도 단위)
                    double angle = patternInfo->angle;
                    double angleRad = angle * M_PI / 180.0;
                    double cosA = std::cos(angleRad);
                    double sinA = std::sin(angleRad);
                    
                    // diffMask를 픽셀 단위로 검사하면서 그리기
                    for (int py = 0; py < diffMaskMat.rows; py++) {
                        for (int px = 0; px < diffMaskMat.cols; px++) {
                            uchar pixelValue;
                            if (diffMaskMat.channels() == 3) {
                                cv::Vec3b pixel = diffMaskMat.at<cv::Vec3b>(py, px);
                                pixelValue = pixel[0];
                            } else {
                                pixelValue = diffMaskMat.at<uchar>(py, px);
                            }
                            
                            // 뷰 좌표로 변환 (스케일 적용)
                            double vx = topLeft.x() + px * currentScale;
                            double vy = topLeft.y() + py * currentScale;
                            
                            // 중심을 기준으로 상대 좌표 계산
                            double relX = vx - viewCenter.x();
                            double relY = vy - viewCenter.y();
                            
                            // 역회전: 회전된 좌표를 원래 좌표로 변환
                            double unrotatedX = relX * cosA + relY * sinA;
                            double unrotatedY = -relX * sinA + relY * cosA;
                            
                            // INS 영역 범위 확인 (반시계 방향 회전이므로 음수 사용)
                            if (std::abs(unrotatedX) <= insWidth / 2.0 && 
                                std::abs(unrotatedY) <= insHeight / 2.0) {
                                
                                // 색상 결정
                                QColor pixelColor;
                                if (pixelValue > 0) {
                                    pixelColor = QColor(255, 0, 0);  // 빨강 (차이)
                                } else {
                                    pixelColor = QColor(0, 255, 0);  // 초록 (유사)
                                }
                                
                                // 알파 값 적용
                                pixelColor.setAlpha(179);  // 0.7 * 255 ≈ 179
                                
                                // 픽셀 그리기
                                painter.fillRect(QRectF(vx, vy, currentScale, currentScale), pixelColor);
                            }
                        }
                    }
                }
            }
        }
}

// ===== DIFF 검사 시각화 =====
void CameraView::drawINSDiffVisualization(QPainter& painter, const InspectionResult& result,
                                           const QUuid& patternId, const PatternInfo* patternInfo,
                                           const QRectF& inspRectScene, double insAngle) {
    if (!result.diffMask.contains(patternId)) return;
    
    cv::Mat diffMaskMat = result.diffMask[patternId];
    if (diffMaskMat.empty()) return;
    
    // zoom scale 적용
    QTransform t = transform();
    double currentScale = std::sqrt(t.m11() * t.m11() + t.m12() * t.m12());
    
    // INS 패턴 정보
    QRectF rectF = patternInfo->rect;
    QPointF center(rectF.x() + rectF.width() / 2.0, rectF.y() + rectF.height() / 2.0);
    QPointF viewCenter = mapFromScene(center);
    
    double insWidth = rectF.width() * currentScale;
    double insHeight = rectF.height() * currentScale;
    
    // diffMask 크기 (스케일 적용)
    int scaledWidth = static_cast<int>(diffMaskMat.cols * currentScale);
    int scaledHeight = static_cast<int>(diffMaskMat.rows * currentScale);
    
    QPointF topLeft(viewCenter.x() - scaledWidth / 2.0, 
                   viewCenter.y() - scaledHeight / 2.0);
    
    // 회전각 (도 단위)
    double angle = patternInfo->angle;
    double angleRad = angle * M_PI / 180.0;
    double cosA = std::cos(angleRad);
    double sinA = std::sin(angleRad);
    
    // diffMask를 픽셀 단위로 검사하면서 그리기
    for (int py = 0; py < diffMaskMat.rows; py++) {
        for (int px = 0; px < diffMaskMat.cols; px++) {
            uchar pixelValue;
            if (diffMaskMat.channels() == 3) {
                cv::Vec3b pixel = diffMaskMat.at<cv::Vec3b>(py, px);
                pixelValue = pixel[0];
            } else {
                pixelValue = diffMaskMat.at<uchar>(py, px);
            }
            
            // 뷰 좌표로 변환 (스케일 적용)
            double vx = topLeft.x() + px * currentScale;
            double vy = topLeft.y() + py * currentScale;
            
            // 중심을 기준으로 상대 좌표 계산
            double relX = vx - viewCenter.x();
            double relY = vy - viewCenter.y();
            
            // 역회전: 회전된 좌표를 원래 좌표로 변환
            double unrotatedX = relX * cosA + relY * sinA;
            double unrotatedY = -relX * sinA + relY * cosA;
            
            // INS 영역 범위 확인
            if (std::abs(unrotatedX) <= insWidth / 2.0 && 
                std::abs(unrotatedY) <= insHeight / 2.0) {
                
                // 색상 결정
                QColor pixelColor;
                if (pixelValue > 0) {
                    pixelColor = QColor(255, 0, 0);  // 빨강 (차이)
                } else {
                    pixelColor = QColor(0, 255, 0);  // 초록 (유사)
                }
                
                // 알파 값 적용
                pixelColor.setAlpha(179);  // 0.7 * 255
                
                // 픽셀 그리기
                painter.fillRect(QRectF(vx, vy, currentScale, currentScale), pixelColor);
            }
        }
    }
}

// ===== CRIMP 검사 시각화 =====
void CameraView::drawINSCrimpVisualization(QPainter& painter, const InspectionResult& result,
                                            const QUuid& patternId, const PatternInfo* patternInfo,
                                            const QRectF& inspRectScene, double insAngle) {
    // CRIMP 검사는 현재 별도 시각화가 없음 (필요시 추가)
    // 기본 INS 박스는 drawINSPatterns에서 이미 그려짐
}


void CameraView::paintEvent(QPaintEvent *event) {
    // QGraphicsView 기본 렌더링 (배경 이미지 포함)
    QGraphicsView::paintEvent(event);
    
    // 패턴 오버레이 렌더링 (뷰포트 좌표계 - 고정)
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setTransform(QTransform());
    
    // 티칭 모드: 패턴 및 핸들 렌더링
    if (!isInspectionMode) {
        drawTeachingModePatterns(painter);
        drawSelectedPatternHandles(painter);
    }
    
    // 검사 모드: 검사 결과 렌더링
    if (isInspectionMode && hasInspectionResult) {
        drawInspectionResults(painter, lastInspectionResult);
    }
    
    // 거리 측정 라인
    drawMeasurementLine(painter);
    
    // 현재 그리는 사각형
    drawCurrentDrawingRect(painter);
}


void CameraView::wheelEvent(QWheelEvent* event) {
    if (backgroundPixmap.isNull() || !bgPixmapItem) {
        event->accept();
        return;
    }

    // 마우스 위치를 scene 좌표로 변환 (확대/축소 중심점)
    QPointF mouseScenePos = mapToScene(event->position().toPoint());
    
    // 확대/축소 factor 계산
    double scaleFactor = 1.15;
    int numDegrees = event->angleDelta().y() / 8;
    int numSteps = numDegrees / 15;
    double factor = std::pow(scaleFactor, numSteps);
    
    // 현재 스케일 확인
    QTransform t = transform();
    double currentScale = std::sqrt(t.m11() * t.m11() + t.m12() * t.m12());
    double newScale = currentScale * factor;
    
    // 0.2배 ~ 5.0배 범위로 제한
    if (newScale < 0.2) {
        factor = 0.2 / currentScale;
    } else if (newScale > 5.0) {
        factor = 5.0 / currentScale;
    }
    
    // 마우스 위치를 중심으로 scale
    scale(factor, factor);
    
    // 마우스가 같은 scene 좌표에 남도록 보정
    QPointF newMouseScenePos = mapToScene(event->position().toPoint());
    centerOn(mapToScene(rect().center()) + (mouseScenePos - newMouseScenePos));
    
    viewport()->update();
    event->accept();
}

QPixmap CameraView::applyZoom(const QPixmap& original) {
    if (original.isNull() || zoomFactor == 1.0) {
        return original;  // 줌이 1.0이면 원본 반환
    }
    
    // 화면 중앙을 기준으로 줌 (또는 마우스 위치)
    QPoint center = zoomCenter.isNull() ? rect().center() : zoomCenter;
    
    // 원본 이미지 크기
    QSize origSize = original.size();
    
    // 확대/축소된 크기 계산
    QSize newSize(origSize.width() * zoomFactor, origSize.height() * zoomFactor);
    
    // 디스플레이에서 확대/축소된 이미지를 보여줄 위치 계산
    QRect targetRect(0, 0, size().width(), size().height());
    
    // 변환된 이미지 생성
    QPixmap zoomed = original.scaled(newSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    
    // 이미지가 뷰보다 크면 특정 부분만 표시
    if (zoomFactor > 1.0) {
        // 마우스 위치를 기반으로 뷰포트 계산 
        double relX = center.x() / (double)width();
        double relY = center.y() / (double)height();
        
        int focusX = static_cast<int>(zoomed.width() * relX - width() / 2.0);
        int focusY = static_cast<int>(zoomed.height() * relY - height() / 2.0);
        
        // 경계 체크
        focusX = qBound(0, focusX, zoomed.width() - width());
        focusY = qBound(0, focusY, zoomed.height() - height());
        
        // 보이는 영역만 자르기
        QRect viewRect(focusX, focusY, 
                      qMin(width(), zoomed.width()), 
                      qMin(height(), zoomed.height()));
        
        zoomed = zoomed.copy(viewRect);
    }
    
    return zoomed;
}

void CameraView::updateZoomedView() {
    // 배경 이미지가 없으면 그냥 리턴
    if (backgroundPixmap.isNull()) {
        return;
    }
    
    // QGraphicsView에서는 scene에 pixmap이 이미 추가되어 있으므로
    // viewport 갱신만 하면 된다
    viewport()->update();
}

void CameraView::setBackgroundPixmap(const QPixmap &pixmap) {
    // 빈 픽스맵(카메라 OFF)인 경우 텍스트 표시
    if (pixmap.isNull()) {
        backgroundPixmap = QPixmap();
        originalImageSize = QSize();
        if (bgPixmapItem) {
            scene->removeItem(bgPixmapItem);
            delete bgPixmapItem;
            bgPixmapItem = nullptr;
        }
        viewport()->update();
        return;
    }
    
    // 처음 이미지가 설정될 때만 초기화 적용
    bool isFirstLoad = backgroundPixmap.isNull();
    
    backgroundPixmap = pixmap;
    originalImageSize = pixmap.size();
    
    // QGraphicsScene에 배경 이미지 추가
    if (bgPixmapItem) {
        scene->removeItem(bgPixmapItem);
        delete bgPixmapItem;
    }
    bgPixmapItem = scene->addPixmap(pixmap);
    scene->setSceneRect(pixmap.rect());
    
    if (isFirstLoad) {
        // 처음 로드될 때만 중앙 정렬 및 자동 리사이징 적용
        zoomFactor = 1.0;
        panOffset = QPoint(0, 0);
        
        // 이미지 비율 계산하여 화면에 맞추기
        QSize viewSize = size();
        if (viewSize.width() <= 0 || viewSize.height() <= 0) {
            viewSize = QSize(640, 480);
        }
        
        double imgRatio = (double)pixmap.width() / pixmap.height();
        double viewRatio = (double)viewSize.width() / viewSize.height();
        
        // 이미지를 화면에 맞추기 위한 초기 줌 계산
        if (imgRatio > viewRatio) {
            // 너비에 맞춤
            zoomFactor = (double)viewSize.width() / pixmap.width() * 0.70; // 70%로 맞추기
        } else {
            // 높이에 맞춤
            zoomFactor = (double)viewSize.height() / pixmap.height() * 0.70;
        }
        
        // 뷰 맞추기
        fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
    }
    
    // 화면 갱신
    viewport()->update();
}

void CameraView::drawResizeHandles(QPainter& painter, const QRect& rect) {
    Q_UNUSED(rect);  // rect 파라미터 사용 안함
    PatternInfo* pattern = getPatternById(selectedPatternId);
    if (!pattern) return;
    
    // 티칭 오프이거나 Move 모드가 아니면 핸들 숨기기
    TeachingWidget* teachingWidget = qobject_cast<TeachingWidget*>(parent());
    if (!teachingWidget || m_editMode != EditMode::Move) {
        return; // 핸들을 그리지 않음
    }
    
    // teachModeButton 상태로 티칭 모드 확인
    QPushButton* teachButton = teachingWidget->findChild<QPushButton*>("teachModeButton");
    if (!teachButton || !teachButton->isChecked()) {
        return; // 티칭 모드가 아니면 핸들 숨기기
    }
    
    // 그룹화된 FID 패턴인지 확인
    bool isGroupedFid = false;
    if (pattern->type == PatternType::FID) {
        for (int i = 0; i < patterns.size(); i++) {
            const PatternInfo& insPattern = patterns[i];
            if (insPattern.parentId == selectedPatternId && insPattern.type == PatternType::INS) {
                isGroupedFid = true;
                break;
            }
        }
    }
    
    QVector<QPoint> corners = getRotatedCorners();
    if (corners.size() < 4) return;
    
    int handleSize = resizeHandleSize;
    QColor handleColor = pattern->color;

    if (isGroupedFid) {
        // 그룹화된 FID: 크기조절 핸들 + 회전 핸들 모두 표시
        painter.setPen(QPen(handleColor.darker(), 1));
        painter.setBrush(handleColor);

        for (const QPoint& pt : corners) {
            painter.drawRect(QRect(pt.x() - handleSize/2, pt.y() - handleSize/2, handleSize, handleSize));
        }

        // 회전 핸들
        QRect rotateRect = rotateHandleRect();
        painter.setPen(QPen(Qt::blue, 2));
        painter.setBrush(Qt::yellow);
        painter.drawEllipse(rotateRect);
    } else {
        // 개별 패턴: 크기 조정 핸들 + 회전 핸들 모두 표시
        painter.setPen(QPen(handleColor.darker(), 1));
        painter.setBrush(handleColor);

        for (const QPoint& pt : corners) {
            painter.drawRect(QRect(pt.x() - handleSize/2, pt.y() - handleSize/2, handleSize, handleSize));
        }

        // 회전 핸들
        QRect rotateRect = rotateHandleRect();
        painter.setPen(QPen(Qt::blue, 2));
        painter.setBrush(Qt::yellow);
        painter.drawEllipse(rotateRect);
    }
}

int CameraView::getRotateHandleAt(const QPoint& pos) const {
    QRect rotateRect = rotateHandleRect();
    if (rotateRect.contains(pos)) return 1; // 1: 회전 핸들
    return -1;
}

int CameraView::getCornerHandleAt(const QPoint& pos) const {
    QVector<QPoint> corners = getRotatedCorners();
    int s = resizeHandleSize;
    if (corners.size() < 4) return -1; // ★ 방어 코드 추가
    for (int i = 0; i < 4; ++i) {
        QRect handleRect(corners[i].x() - s/2, corners[i].y() - s/2, s, s);
        if (handleRect.contains(pos)) return i;
    }
    return -1;
}

QVector<QPoint> CameraView::getRotatedCorners() const {
    const PatternInfo* pattern = getPatternById(selectedPatternId);
    if (!pattern || pattern->rect.isNull()) return QVector<QPoint>();

    QVector<QPoint> corners;
    
    // Viewport 좌표로 직접 계산
    QPointF topLeft = mapFromScene(pattern->rect.topLeft());
    QPointF bottomRight = mapFromScene(pattern->rect.bottomRight());
    QRectF displayRect(topLeft, bottomRight);
    
    // 중심점과 크기 (viewport 좌표)
    double centerX = displayRect.center().x();
    double centerY = displayRect.center().y();
    double halfWidth = displayRect.width() / 2.0;
    double halfHeight = displayRect.height() / 2.0;
    
    // 회전되지 않은 꼭짓점들
    QVector<QPointF> unrotatedCorners = {
        QPointF(centerX - halfWidth, centerY - halfHeight),  // top-left
        QPointF(centerX + halfWidth, centerY - halfHeight),  // top-right
        QPointF(centerX + halfWidth, centerY + halfHeight),  // bottom-right
        QPointF(centerX - halfWidth, centerY + halfHeight)   // bottom-left
    };
    
    corners.resize(4);
    
    // 회전 각도 적용
    double radians = pattern->angle * M_PI / 180.0;
    double cosA = std::cos(radians);
    double sinA = std::sin(radians);
    
    for (int i = 0; i < 4; i++) {
        double dx = unrotatedCorners[i].x() - centerX;
        double dy = unrotatedCorners[i].y() - centerY;
        
        double rotatedX = centerX + dx * cosA - dy * sinA;
        double rotatedY = centerY + dx * sinA + dy * cosA;
        
        corners[i] = QPoint(static_cast<int>(std::round(rotatedX)), 
                           static_cast<int>(std::round(rotatedY)));
    }
    
    return corners;
}

QVector<QPoint> CameraView::getRotatedCornersForPattern(const PatternInfo& pattern) const {
    if (pattern.rect.isNull()) return QVector<QPoint>();

    QVector<QPoint> corners;
    
    // 배경 이미지가 없으면 빈 벡터 반환
    if (backgroundPixmap.isNull()) return corners;
    
    // 패턴 중심을 좌표 변환
    QPointF centerOriginal = pattern.rect.center();
    QPoint centerDisplay = originalToDisplay(centerOriginal.toPoint());
    
    // 너비/높이를 화면 좌표로 변환
    double width = pattern.rect.width() * zoomFactor;
    double height = pattern.rect.height() * zoomFactor;
    
    // 회전되지 않은 꼭짓점들 먼저 계산
    double halfWidth = width / 2.0;
    double halfHeight = height / 2.0;
    
    QVector<QPointF> unrotatedCorners = {
        QPointF(centerDisplay.x() - halfWidth, centerDisplay.y() - halfHeight),  // top-left
        QPointF(centerDisplay.x() + halfWidth, centerDisplay.y() - halfHeight),  // top-right
        QPointF(centerDisplay.x() + halfWidth, centerDisplay.y() + halfHeight),  // bottom-right
        QPointF(centerDisplay.x() - halfWidth, centerDisplay.y() + halfHeight)   // bottom-left
    };
    
    // 회전 적용
    double radians = pattern.angle * M_PI / 180.0;
    double cosA = std::cos(radians);
    double sinA = std::sin(radians);
    
    corners.resize(4);
    for (int i = 0; i < 4; i++) {
        double dx = unrotatedCorners[i].x() - centerDisplay.x();
        double dy = unrotatedCorners[i].y() - centerDisplay.y();
        
        double rotatedX = centerDisplay.x() + dx * cosA - dy * sinA;
        double rotatedY = centerDisplay.y() + dx * sinA + dy * cosA;
        
        // 최종 변환에서만 반올림
        corners[i] = QPoint(static_cast<int>(std::round(rotatedX)), 
                           static_cast<int>(std::round(rotatedY)));
    }
    
    return corners;
}

QPoint CameraView::getRotatedCenter() const {
    const PatternInfo* pattern = getPatternById(selectedPatternId);
    if (!pattern || pattern->rect.isNull()) return QPoint();
    
    // 패턴의 실제 중심점을 화면 좌표로 변환
    QPointF centerOriginal = pattern->rect.center();
    return originalToDisplay(centerOriginal.toPoint());
}

QRect CameraView::rotateHandleRect() const {
    const PatternInfo* pattern = getPatternById(selectedPatternId);
    if (!pattern) return QRect();
    
    QVector<QPoint> corners = getRotatedCorners();
    if (corners.size() < 4) return QRect();
    
    // 상단 중심점 (viewport 좌표)
    QPointF topCenter = QPointF(
        (corners[0].x() + corners[1].x()) / 2.0,
        (corners[0].y() + corners[1].y()) / 2.0
    );
    
    // 패턴의 중심점
    QPointF center = mapFromScene(pattern->rect.center());
    
    // 회전 각도를 고려하여 핸들 위치 계산
    double radians = pattern->angle * M_PI / 180.0;
    double dx = 0;
    double dy = -20; // 상단으로 20픽셀
    
    // 회전 적용
    double rotatedDx = dx * std::cos(radians) - dy * std::sin(radians);
    double rotatedDy = dx * std::sin(radians) + dy * std::cos(radians);
    
    int hx = qRound(topCenter.x() + rotatedDx);
    int hy = qRound(topCenter.y() + rotatedDy);
    
    int s = resizeHandleSize;
    return QRect(hx - s/2, hy - s/2, s, s);
}

const PatternInfo* CameraView::getPatternById(const QUuid& id) const {
    for (int i = 0; i < patterns.size(); i++) {
        if (patterns[i].id == id) {
            return &patterns[i];
        }
    }
    return nullptr;
}

QUuid CameraView::hitTest(const QPoint& pos) {
    // Viewport 좌표로 직접 처리 (paintEvent와 일관성 유지)
    
    QUuid result;
    int minDistSq = std::numeric_limits<int>::max();
 
    // 선택된 패턴 먼저 확인 (최상위에 그려졌으므로 우선 선택)
    if (!selectedPatternId.isNull()) {
        PatternInfo* selectedPattern = getPatternById(selectedPatternId);
        if (selectedPattern && selectedPattern->enabled) {
            bool patternVisible = (selectedPattern->cameraUuid.isEmpty() || selectedPattern->cameraUuid == currentCameraUuid || currentCameraUuid.isEmpty());
            
            if (patternVisible) {
                QPointF topLeft = mapFromScene(selectedPattern->rect.topLeft());
                QPointF bottomRight = mapFromScene(selectedPattern->rect.bottomRight());
                QRectF displayRect(topLeft, bottomRight);
             
                if (displayRect.contains(pos)) {
                    return selectedPattern->id;
                }
            }
        }
    }
    
    // 모든 패턴 검사 (최근 추가된 것이 먼저 선택되도록 뒤에서 검사)
    for (int i = patterns.size() - 1; i >= 0; --i) {
        if (!patterns[i].enabled) continue;
        
        // ROI 패턴은 hitTest에서 제외 (다른 패턴 편집 방해하지 않도록)
        if (patterns[i].type == PatternType::ROI) continue;
        
        bool patternVisible = (patterns[i].cameraUuid.isEmpty() || patterns[i].cameraUuid == currentCameraUuid || currentCameraUuid.isEmpty());
       
        if (!patternVisible) continue;
        
        QPointF topLeft = mapFromScene(patterns[i].rect.topLeft());
        QPointF bottomRight = mapFromScene(patterns[i].rect.bottomRight());
        QRectF displayRect(topLeft, bottomRight);
           
        if (displayRect.contains(pos)) {
            QPointF rectCenter = displayRect.center();
            int centerDistSq = static_cast<int>(
                (pos.x() - rectCenter.x()) * (pos.x() - rectCenter.x()) +
                (pos.y() - rectCenter.y()) * (pos.y() - rectCenter.y())
            );
            if (centerDistSq < minDistSq) {
                result = patterns[i].id;
                minDistSq = centerDistSq;
            }
        }
    }
    
    return result;
}

CameraView::ResizeHandle CameraView::getResizeHandle(const QPoint& pos, const QUuid& patternId) {
    PatternInfo* pattern = getPatternById(patternId);
    if (!pattern) return ResizeHandle::None;
    
    // 원본 좌표계의 패턴 영역을 디스플레이 좌표로 변환
    QRectF r = pattern->rect;
    QPoint topLeft = originalToDisplay(QPoint(qRound(r.left()), qRound(r.top())));
    QPoint bottomRight = originalToDisplay(QPoint(qRound(r.right()), qRound(r.bottom())));
    QRect displayRect(topLeft, bottomRight);
    
    int hs = resizeHandleSize;
    
    // 각 핸들 영역 확인
    if (QRect(displayRect.left() - hs/2, displayRect.top() - hs/2, hs, hs).contains(pos))
        return ResizeHandle::TopLeft;
    if (QRect(displayRect.right() - hs/2, displayRect.top() - hs/2, hs, hs).contains(pos))
        return ResizeHandle::TopRight;
    if (QRect(displayRect.left() - hs/2, displayRect.bottom() - hs/2, hs, hs).contains(pos))
        return ResizeHandle::BottomLeft;
    if (QRect(displayRect.right() - hs/2, displayRect.bottom() - hs/2, hs, hs).contains(pos))
        return ResizeHandle::BottomRight;
    if (QRect(displayRect.left() + displayRect.width()/2 - hs/2, displayRect.top() - hs/2, hs, hs).contains(pos))
        return ResizeHandle::Top;
    if (QRect(displayRect.left() - hs/2, displayRect.top() + displayRect.height()/2 - hs/2, hs, hs).contains(pos))
        return ResizeHandle::Left;
    if (QRect(displayRect.right() - hs/2, displayRect.top() + displayRect.height()/2 - hs/2, hs, hs).contains(pos))
        return ResizeHandle::Right;
    if (QRect(displayRect.left() + displayRect.width()/2 - hs/2, displayRect.bottom() - hs/2, hs, hs).contains(pos))
        return ResizeHandle::Bottom;
        
    return ResizeHandle::None;
}

QCursor CameraView::getResizeCursor(ResizeHandle handle) {
    switch (handle) {
        case ResizeHandle::TopLeft:
        case ResizeHandle::BottomRight:
            return Qt::SizeFDiagCursor;
        case ResizeHandle::TopRight:
        case ResizeHandle::BottomLeft:
            return Qt::SizeBDiagCursor;
        case ResizeHandle::Top:
        case ResizeHandle::Bottom:
            return Qt::SizeVerCursor;
        case ResizeHandle::Left:
        case ResizeHandle::Right:
            return Qt::SizeHorCursor;
        default:
            return Qt::ArrowCursor;
    }
}

QRect CameraView::getResizedRect(const QRect& rect, const QPoint& pos, ResizeHandle handle) {
    QRect newRect = rect;
    
    switch (handle) {
        case ResizeHandle::TopLeft:
            newRect.setTopLeft(pos);
            break;
        case ResizeHandle::TopRight:
            newRect.setTopRight(pos);
            break;
        case ResizeHandle::BottomLeft:
            newRect.setBottomLeft(pos);
            break;
        case ResizeHandle::BottomRight:
            newRect.setBottomRight(pos);
            break;
        case ResizeHandle::Top:
            newRect.setTop(pos.y());
            break;
        case ResizeHandle::Left:
            newRect.setLeft(pos.x());
            break;
        case ResizeHandle::Right:
            newRect.setRight(pos.x());
            break;
        case ResizeHandle::Bottom:
            newRect.setBottom(pos.y());
            break;
        default:
            break;
    }
    
    return newRect.normalized();
}

QUuid CameraView::addPattern(const PatternInfo& pattern) {
    // 새 패턴에 고유 ID 생성(이미 없다면)
    PatternInfo newPattern = pattern;
    if (newPattern.id.isNull()) {
        newPattern.id = QUuid::createUuid();
    }
    
    patterns.append(newPattern);
    
    // 방금 추가한 패턴이 INS 패턴이라면 템플릿 이미지 업데이트 신호 발생
    if (pattern.type == PatternType::INS) {
        emit insTemplateUpdateRequired(pattern.id);
    } else if (pattern.type == PatternType::FID) {
        emit fidTemplateUpdateRequired(pattern.id);
    }

    update();
    emit patternAdded(newPattern.id);
    return newPattern.id;
}

void CameraView::removePattern(const QUuid& patternId) {
    // 1. 삭제할 모든 패턴들을 한 번에 수집 (재귀 없이)
    QSet<QUuid> patternsToDelete;
    QQueue<QUuid> patternQueue;
    patternQueue.enqueue(patternId);
    
    while (!patternQueue.isEmpty()) {
        QUuid currentId = patternQueue.dequeue();
        if (patternsToDelete.contains(currentId)) continue;
        
        patternsToDelete.insert(currentId);
        
        // 자식 패턴들을 큐에 추가
        for (const PatternInfo& pattern : patterns) {
            if (pattern.parentId == currentId && !patternsToDelete.contains(pattern.id)) {
                patternQueue.enqueue(pattern.id);
            }
        }
    }
    
    // 2. 다른 패턴들의 childIds에서 삭제될 패턴들 제거
    for (int i = 0; i < patterns.size(); i++) {
        for (const QUuid& deleteId : patternsToDelete) {
            patterns[i].childIds.removeAll(deleteId);
        }
    }
    
    // 3. 역순으로 패턴 삭제 (인덱스 문제 방지)
    for (int i = patterns.size() - 1; i >= 0; i--) {
        if (patternsToDelete.contains(patterns[i].id)) {
            patterns.removeAt(i);
        }
    }
    
    // 4. 선택된 패턴이 삭제된 경우 선택 해제
    if (patternsToDelete.contains(selectedPatternId)) {
        selectedPatternId = QUuid();
        emit patternSelected(QUuid());
    }
    
    // 5. 삭제 이벤트 발생
    for (const QUuid& deleteId : patternsToDelete) {
        emit patternRemoved(deleteId);
    }
    
    update();
}

int CameraView::getSelectedPatternIndex() const {
    // selectedPatternId로 인덱스 찾기
    for (int i = 0; i < patterns.size(); i++) {
        if (patterns[i].id == selectedPatternId) {
            return i;
        }
    }
    return -1;
}

void CameraView::setSelectedPatternId(const QUuid& id) {
    // 이미 선택된 ID와 같으면 변경 없음
    if (selectedPatternId == id) {
        return;
    }

    // ID로 패턴 인덱스 찾기
    int index = -1;
    for (int i = 0; i < patterns.size(); i++) {
        if (patterns[i].id == id) {
            index = i;
            break;
        }
    }

    // 유효한 ID이거나 빈 ID(선택 해제)인 경우 적용
    bool validId = (index >= 0) || id.isNull();
    
    if (validId) {
        // 선택 시 각도 등 패턴 속성은 절대 건드리지 않음
        selectedPatternId = id;
        
        // 빈 ID일 때는 선택 해제 시그널 emit
        if (id.isNull()) {
            qDebug() << "[setSelectedPatternId] 패턴 선택 해제 - selectedInspectionPatternCleared 시그널 emit";
            emit selectedInspectionPatternCleared();
        } else {
            qDebug() << "[setSelectedPatternId] 패턴 선택:" << id;
            emit patternSelected(id);
        }
        
        update();
    }
}

PatternInfo* CameraView::getPatternById(const QUuid& id) {
    for (int i = 0; i < patterns.size(); i++) {
        if (patterns[i].id == id) {
            return &patterns[i];
        }
    }
    return nullptr;
}

void CameraView::updatePatternRect(const QUuid& id, const QRectF& rect) {
    // ID로 패턴 찾기
    for (int i = 0; i < patterns.size(); i++) {
        if (patterns[i].id == id) {
            patterns[i].rect = rect;
            emit patternRectChanged(id, QRect(static_cast<int>(rect.x()), static_cast<int>(rect.y()),
                                            static_cast<int>(rect.width()), static_cast<int>(rect.height())));
            update();
            break;
        }
    }
}

const QList<FilterInfo>& CameraView::getPatternFilters(const QUuid& patternId) const {
    static QList<FilterInfo> emptyList;
    
    // ID로 패턴 찾기
    for (int i = 0; i < patterns.size(); i++) {
        if (patterns[i].id == patternId) {
            return patterns[i].filters;
        }
    }
    
    return emptyList;
}

void CameraView::clearPatterns() {
    patterns.clear();
    selectedPatternId = QUuid();
    update();
}

void CameraView::addPatternFilter(const QUuid& patternId, int filterType) {
    // ID로 패턴 찾기
    PatternInfo* pattern = getPatternById(patternId);
    if (!pattern) return;
    
    // 새 필터 생성
    FilterInfo filter;
    filter.type = filterType;
    filter.enabled = true;
    
    // 기본 파라미터 설정 - ImageProcessor 클래스에서 가져옴
    filter.params = ImageProcessor::getDefaultParams(filterType);
    
    // 필터 목록에 추가
    pattern->filters.append(filter);
    
    // FID 또는 INS 패턴인 경우 템플릿 이미지 갱신 필요 신호 발생
    if (pattern->type == PatternType::FID) {
        emit fidTemplateUpdateRequired(patternId);
    } else if (pattern->type == PatternType::INS) {
        emit insTemplateUpdateRequired(patternId);
    }
    
    update();
}

void CameraView::removePatternFilter(const QUuid& patternId, int filterIndex) {
    PatternInfo* pattern = getPatternById(patternId);
    if (!pattern) return;
    if (filterIndex < 0 || filterIndex >= pattern->filters.size()) return;
    
    // CONTOUR 필터를 제거하는 경우 윤곽선 UI 삭제
    if (pattern->filters[filterIndex].type == FILTER_CONTOUR) {
        patternContours[patternId].clear();
    }
    
    // 필터 제거
    pattern->filters.removeAt(filterIndex);
    
    // FID 또는 INS 패턴인 경우 템플릿 이미지 갱신 필요 신호 발생
    if (pattern->type == PatternType::FID) {
        emit fidTemplateUpdateRequired(patternId);
    } else if (pattern->type == PatternType::INS) {
        emit insTemplateUpdateRequired(patternId);
    }
    
    update();
}

void CameraView::setPatternFilterEnabled(const QUuid& patternId, int filterIndex, bool enabled) {
    PatternInfo* pattern = getPatternById(patternId);
    if (!pattern) return;
    if (filterIndex < 0 || filterIndex >= pattern->filters.size()) return;
    
    // 이전 상태와 다를 때만 처리
    if (pattern->filters[filterIndex].enabled != enabled) {
        // 필터 활성화/비활성화
        pattern->filters[filterIndex].enabled = enabled;
        
        // CONTOUR 필터를 비활성화하는 경우 윤곽선 UI 삭제
        if (!enabled && pattern->filters[filterIndex].type == FILTER_CONTOUR) {
            patternContours[patternId].clear();
        }
        
        // FID 또는 INS 패턴인 경우 템플릿 이미지 갱신 필요 신호 발생
        if (pattern->type == PatternType::FID) {
            emit fidTemplateUpdateRequired(patternId);
        } else if (pattern->type == PatternType::INS) {
            emit insTemplateUpdateRequired(patternId);
        }
        
        update();
    }
}

void CameraView::setPatternFilterParam(const QUuid& patternId, int filterIndex, const QString& paramName, int value) {
    PatternInfo* pattern = getPatternById(patternId);
    if (!pattern) return;
    if (filterIndex < 0 || filterIndex >= pattern->filters.size()) return;
    
    // 필터 파라미터 설정
    pattern->filters[filterIndex].params[paramName] = value;
    update();
}

void CameraView::movePatternFilterUp(const QUuid& patternId, int filterIndex) {
    PatternInfo* pattern = getPatternById(patternId);
    if (!pattern) return;
    if (filterIndex <= 0 || filterIndex >= pattern->filters.size()) return;
    
    // 필터 위로 이동
    pattern->filters.swapItemsAt(filterIndex, filterIndex - 1);
    update();
}

void CameraView::movePatternFilterDown(const QUuid& patternId, int filterIndex) {
    PatternInfo* pattern = getPatternById(patternId);
    if (!pattern) return;
    if (filterIndex < 0 || filterIndex >= pattern->filters.size() - 1) return;
    
    // 필터 아래로 이동
    pattern->filters.swapItemsAt(filterIndex, filterIndex + 1);
    update();
}

void CameraView::applyFiltersToImage(cv::Mat& image) {
    // 이미지가 비어있으면 처리하지 않음
    if (image.empty()) return;
    
    for (int i = 0; i < patterns.size(); ++i) {
        const PatternInfo& pattern = patterns[i];
        
        // 현재 카메라에 해당하는 패턴만 처리 (시뮬레이션/일반 모드 고려)
        bool patternVisible = false;
        if (!currentCameraUuid.isEmpty()) {
            // 시뮬레이션 모드: 모든 패턴을 처리
            patternVisible = true;
        } else {
            // 일반 모드: currentCameraUuid와 비교
            patternVisible = (currentCameraUuid.isEmpty() || 
                            pattern.cameraUuid == currentCameraUuid ||
                            pattern.cameraUuid.isEmpty());
        }
        
        if (!patternVisible) continue;
        
        // 비활성화된 패턴 무시
        if (!pattern.enabled) continue;
        
        // 필터가 없으면 건너뜀
        if (pattern.filters.isEmpty()) continue;
        
        printf("[CameraView] 필터 적용 중 - 패턴: %s, 필터 수: %lld, 각도: %.1f\n", 
               pattern.name.toStdString().c_str(), (long long)pattern.filters.size(), pattern.angle);
        fflush(stdout);
        
        // 회전이 있는 경우: 회전된 사각형 영역에만 필터 적용
        if (std::abs(pattern.angle) > 0.1) {
            cv::Point2f center(pattern.rect.x() + pattern.rect.width()/2.0f, 
                             pattern.rect.y() + pattern.rect.height()/2.0f);
            
            // 1. 회전된 사각형 마스크 생성
            cv::Mat mask = cv::Mat::zeros(image.size(), CV_8UC1);
            cv::Size2f patternSize(pattern.rect.width(), pattern.rect.height());
            
            cv::Point2f vertices[4];
            cv::RotatedRect rotatedRect(center, patternSize, pattern.angle);
            rotatedRect.points(vertices);
            
            std::vector<cv::Point> points;
            for (int i = 0; i < 4; i++) {
                points.push_back(cv::Point(static_cast<int>(std::round(vertices[i].x)), 
                                         static_cast<int>(std::round(vertices[i].y))));
            }
            cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{points}, cv::Scalar(255));
            
            // 2. 마스크 영역만 복사한 이미지 생성
            cv::Mat maskedImage = cv::Mat::zeros(image.size(), image.type());
            image.copyTo(maskedImage, mask);
            
            // 3. 확장된 ROI 계산
            double angleRad = std::abs(pattern.angle) * M_PI / 180.0;
            double width = pattern.rect.width();
            double height = pattern.rect.height();
            
            double rotatedWidth = std::abs(width * std::cos(angleRad)) + std::abs(height * std::sin(angleRad));
            double rotatedHeight = std::abs(width * std::sin(angleRad)) + std::abs(height * std::cos(angleRad));
            
            int maxSize = static_cast<int>(std::max(rotatedWidth, rotatedHeight));
            int halfSize = maxSize / 2;
            
            cv::Rect expandedRoi(
                qBound(0, static_cast<int>(center.x) - halfSize, image.cols - 1),
                qBound(0, static_cast<int>(center.y) - halfSize, image.rows - 1),
                qBound(1, maxSize, image.cols - (static_cast<int>(center.x) - halfSize)),
                qBound(1, maxSize, image.rows - (static_cast<int>(center.y) - halfSize))
            );
            
            // 4. 확장된 영역에 필터 적용
            if (expandedRoi.width > 0 && expandedRoi.height > 0 && 
                expandedRoi.x + expandedRoi.width <= maskedImage.cols && 
                expandedRoi.y + expandedRoi.height <= maskedImage.rows) {
                
                cv::Mat roiMat = maskedImage(expandedRoi);
                ImageProcessor processor;
                for (const FilterInfo& filter : pattern.filters) {
                    if (filter.enabled) {
                        cv::Mat nextFiltered;
                        processor.applyFilter(roiMat, nextFiltered, filter);
                        if (!nextFiltered.empty()) {
                            nextFiltered.copyTo(roiMat);
                        }
                    }
                }
            }
            
            // 5. 마스크 영역만 필터 적용된 결과로 교체
            maskedImage.copyTo(image, mask);
            
        } else {
            // 회전 없는 경우: rect 영역에만 필터 적용
            QRectF rect = pattern.rect;
            
            int x = qBound(0, qRound(rect.x()), image.cols - 2);
            int y = qBound(0, qRound(rect.y()), image.rows - 2);
            int width = qBound(1, qRound(rect.width()), image.cols - x);
            int height = qBound(1, qRound(rect.height()), image.rows - y);
            
            if (width <= 0 || height <= 0) continue;
            
            cv::Rect roi(x, y, width, height);
            
            try {
                if (roi.x >= 0 && roi.y >= 0 && 
                    roi.x + roi.width <= image.cols && 
                    roi.y + roi.height <= image.rows &&
                    roi.width > 0 && roi.height > 0) {
                    
                    ImageProcessor::applyFilters(image, pattern.filters, roi);
                }
            } catch (const std::exception& e) {
            } catch (...) {
            }
        }
    }
}

// 윤곽선 설정 함수 구현
void CameraView::setPatternContours(const QUuid& patternId, const QList<QVector<QPoint>>& contours) {
    patternContours[patternId] = contours;
    update(); // 화면 갱신
}

// 그룹 바운딩 박스 그리기 함수 구현 (회전 지원)
void CameraView::drawGroupBoundingBox(QPainter& painter, const QList<PatternInfo>& groupPatterns) {
    if (groupPatterns.isEmpty()) return;
    
    // 그룹의 FID 패턴 찾기 (회전 각도 확인용)
    const PatternInfo* fidPattern = nullptr;
    for (const PatternInfo& pattern : groupPatterns) {
        if (pattern.type == PatternType::FID) {
            fidPattern = &pattern;
            break;
        }
    }
    
    // 그룹의 전체 바운딩 박스 계산 (회전된 패턴들의 실제 영역 고려)
    QRectF boundingBox;
    bool first = true;
    
    for (const PatternInfo& pattern : groupPatterns) {
        QRectF patternBounds;
        
        if (pattern.angle != 0.0) {
            // 회전된 패턴의 경우: 회전된 4개 꼭짓점을 모두 포함하는 축에 평행한 사각형 계산
            QPointF center = pattern.rect.center();
            double halfWidth = pattern.rect.width() / 2.0;
            double halfHeight = pattern.rect.height() / 2.0;
            
            // 4개 꼭짓점의 원본 좌표 (중심 기준 상대 좌표)
            QPointF corners[4] = {
                QPointF(-halfWidth, -halfHeight),  // top-left
                QPointF(halfWidth, -halfHeight),   // top-right
                QPointF(halfWidth, halfHeight),    // bottom-right
                QPointF(-halfWidth, halfHeight)    // bottom-left
            };
            
            // 회전 변환 적용
            double radians = pattern.angle * M_PI / 180.0;
            double cosA = std::cos(radians);
            double sinA = std::sin(radians);
            
            double minX = DBL_MAX, maxX = -DBL_MAX;
            double minY = DBL_MAX, maxY = -DBL_MAX;
            
            for (int i = 0; i < 4; i++) {
                // 회전된 좌표 계산
                double rotatedX = corners[i].x() * cosA - corners[i].y() * sinA;
                double rotatedY = corners[i].x() * sinA + corners[i].y() * cosA;
                
                // 절대 좌표로 변환
                double absoluteX = center.x() + rotatedX;
                double absoluteY = center.y() + rotatedY;
                
                // 최소/최대값 업데이트
                minX = std::min(minX, absoluteX);
                maxX = std::max(maxX, absoluteX);
                minY = std::min(minY, absoluteY);
                maxY = std::max(maxY, absoluteY);
            }
            
            patternBounds = QRectF(minX, minY, maxX - minX, maxY - minY);
        } else {
            // 회전하지 않은 패턴의 경우: 원본 사각형 사용
            patternBounds = pattern.rect;
        }
        
        if (first) {
            boundingBox = patternBounds;
            first = false;
        } else {
            boundingBox = boundingBox.united(patternBounds);
        }
    }
    
    // 약간 더 크게 확장 (여백 추가)
    int margin = 20;  // 픽셀 단위 여백 (회전 고려해서 더 크게)
    boundingBox.adjust(-margin, -margin, margin, margin);
    
    // 마젠타 점선 펜 설정 - 촘촘하고 얇게
    QPen groupPen(UIColors::GROUP_COLOR, 1);   // CommonDefs의 GROUP_COLOR 사용
    groupPen.setStyle(Qt::DashLine);        // 점선 스타일
    groupPen.setDashPattern({3, 2});        // 촉촘한 점선 패턴 (3픽셀 선, 2픽셀 공백)
    
    painter.setPen(groupPen);
    painter.setBrush(Qt::NoBrush);          // 내부 채우지 않음
    
    // 항상 축에 평행한 사각형으로 그리기 (회전 없음)
    QRect screenRect = QRect(
        originalToDisplay(QPoint(boundingBox.x(), boundingBox.y())),
        originalToDisplay(QPoint(boundingBox.right(), boundingBox.bottom()))
    );
    screenRect = screenRect.normalized();
    
    // 그룹 바운딩 박스를 점선으로 그리기
    painter.drawRect(screenRect);
}

QPixmap CameraView::getBackgroundPixmap() const {
    return backgroundPixmap;
}

// ========== 공통 헬퍼 함수들 ==========

// 회전된 박스 그리기 (중심 기준 회전)
void CameraView::drawRotatedBox(QPainter& painter, const QRectF& rect, const QPointF& center, 
                                 double angle, const QPen& pen, const QBrush& brush) {
    painter.save();
    painter.translate(center);
    painter.rotate(angle);
    painter.translate(-center);
    
    painter.setPen(pen);
    painter.setBrush(brush);
    painter.drawRect(rect);
    
    painter.restore();
}

// 회전된 텍스트 라벨 그리기 (배경 + 텍스트)
void CameraView::drawRotatedLabel(QPainter& painter, const QString& text, const QRectF& rect,
                                   const QPointF& center, double angle, const QColor& bgColor,
                                   const QColor& textColor, const QFont& font) {
    painter.save();
    painter.translate(center);
    painter.rotate(angle);
    painter.translate(-center);
    
    painter.setFont(font);
    painter.fillRect(rect, QBrush(bgColor));
    painter.setPen(textColor);
    painter.drawText(rect, Qt::AlignCenter, text);
    
    painter.restore();
}

// 노란색 바운딩 박스 그리기 (축정렬, 회전 투영 적용)
void CameraView::drawYellowBoundingBox(QPainter& painter, const QSizeF& originalSize, 
                                        const QPointF& center, double angle, double scale) {
    // 회전 투영 적용
    double radians = angle * M_PI / 180.0;
    double cosA = std::abs(std::cos(radians));
    double sinA = std::abs(std::sin(radians));
    double projX = originalSize.width() * cosA + originalSize.height() * sinA;
    double projY = originalSize.width() * sinA + originalSize.height() * cosA;
    
    // 스케일 적용
    double boxWidth = projX * scale;
    double boxHeight = projY * scale;
    
    // 축정렬 박스 그리기
    painter.setPen(QPen(QColor(255, 255, 0), 1.5));
    painter.setBrush(Qt::NoBrush);
    
    QPointF topLeft(center.x() - boxWidth/2, center.y() - boxHeight/2);
    QPointF topRight(center.x() + boxWidth/2, center.y() - boxHeight/2);
    QPointF bottomLeft(center.x() - boxWidth/2, center.y() + boxHeight/2);
    QPointF bottomRight(center.x() + boxWidth/2, center.y() + boxHeight/2);
    
    QPolygonF polygon;
    polygon << topLeft << topRight << bottomRight << bottomLeft;
    painter.drawPolygon(polygon);
}

// PASS/NG 라벨 그리기
void CameraView::drawPassNGLabel(QPainter& painter, bool passed, const QRectF& rect,
                                  const QFont& font) {
    QString text = passed ? "PASS" : "NG";
    QColor color = passed ? QColor(0, 255, 0) : QColor(255, 0, 0);
    
    painter.setFont(font);
    QFontMetrics fm(font);
    int textW = fm.horizontalAdvance(text);
    int textH = fm.height();
    
    QRectF bgRect(rect.center().x() - textW/2 - 2, rect.y(), textW + 4, textH);
    painter.fillRect(bgRect, QBrush(QColor(0, 0, 0, 180)));
    painter.setPen(color);
    painter.drawText(bgRect, Qt::AlignCenter, text);
}
// paintEvent 헬퍼 함수들 - 이 파일은 CameraView.cpp의 끝에 통합될 예정

// 티칭 모드 패턴 그리기 (비선택 패턴)
void CameraView::drawTeachingModePatterns(QPainter& painter) {
    if (isInspectionMode) return;
    
    for (const PatternInfo& pattern : patterns) {
        if (pattern.id == selectedPatternId) continue;
        if (!pattern.enabled) continue;
        
        if (!pattern.cameraUuid.isEmpty() && !currentCameraUuid.isEmpty() && 
            pattern.cameraUuid != currentCameraUuid) {
            continue;
        }
        
        if (pattern.stripCrimpMode != currentStripCrimpMode) {
            continue;
        }
        
        // FID/INS 패턴 필터링
        if ((pattern.type == PatternType::FID || pattern.type == PatternType::INS) && 
            !selectedInspectionPatternId.isNull()) {
            const PatternInfo* selectedPattern = nullptr;
            for (const PatternInfo& p : patterns) {
                if (p.id == selectedInspectionPatternId) {
                    selectedPattern = &p;
                    break;
                }
            }
            
            if (selectedPattern && !selectedPattern->childIds.contains(pattern.id) && 
                pattern.id != selectedInspectionPatternId) {
                continue;
            }
        }
        
        // Scene 좌표를 viewport 좌표로 변환
        QPointF topLeft = mapFromScene(pattern.rect.topLeft());
        QPointF bottomRight = mapFromScene(pattern.rect.bottomRight());
        QRectF displayRect(topLeft, bottomRight);
        
        QColor color = UIColors::getPatternColor(pattern.type);
        
        // 회전 적용하여 박스 그리기
        painter.save();
        QPointF center = displayRect.center();
        painter.translate(center);
        painter.rotate(pattern.angle);
        painter.translate(-center);
        
        painter.setPen(QPen(color, 2));
        painter.drawRect(displayRect);
        
        painter.restore();
        
        // 패턴 이름 (회전 적용)
        QFont font(NAMEPLATE_FONT_FAMILY, NAMEPLATE_FONT_SIZE, NAMEPLATE_FONT_WEIGHT);
        painter.setFont(font);
        QFontMetrics fm(font);
        int textWidth = fm.horizontalAdvance(pattern.name);
        int textHeight = fm.height();
        
        painter.save();
        painter.translate(center);
        painter.rotate(pattern.angle);
        painter.translate(-center);
        
        QRectF textRect(displayRect.center().x() - textWidth/2, displayRect.top() - textHeight - 2, 
                       textWidth + 6, textHeight);
        painter.fillRect(textRect, QBrush(QColor(0, 0, 0, 180)));
        painter.setPen(color);
        painter.drawText(textRect, Qt::AlignCenter, pattern.name);
        
        painter.restore();
    }
}

// 선택된 패턴의 핸들 및 UI 요소 그리기
void CameraView::drawSelectedPatternHandles(QPainter& painter) {
    if (isInspectionMode) return;
    
    for (const PatternInfo& pattern : patterns) {
        if (pattern.id != selectedPatternId) continue;
        
        if (pattern.stripCrimpMode != currentStripCrimpMode) {
            continue;
        }
        
        QPointF topLeft = mapFromScene(pattern.rect.topLeft());
        QPointF bottomRight = mapFromScene(pattern.rect.bottomRight());
        QRectF displayRect(topLeft, bottomRight);
        QColor color = UIColors::getPatternColor(pattern.type);
        
        // 회전 중심점
        QPointF center = displayRect.center();
        
        // 40% opacity로 채우기
        painter.save();
        painter.translate(center);
        painter.rotate(pattern.angle);
        painter.translate(-center);
        
        QColor fillColor = color;
        fillColor.setAlpha(102); // 40% opacity
        
        painter.fillRect(displayRect, QBrush(fillColor));
        painter.setPen(QPen(color, 2));
        painter.drawRect(displayRect);
        
        painter.restore();
        
        // 패턴 이름
        QFont font(NAMEPLATE_FONT_FAMILY, NAMEPLATE_FONT_SIZE, NAMEPLATE_FONT_WEIGHT);
        painter.setFont(font);
        QFontMetrics fm(font);
        int textWidth = fm.horizontalAdvance(pattern.name);
        int textHeight = fm.height();
        
        painter.save();
        painter.translate(center);
        painter.rotate(pattern.angle);
        painter.translate(-center);
        
        QRectF textRect(displayRect.center().x() - textWidth/2, displayRect.top() - textHeight - 2,
                       textWidth + 6, textHeight);
        painter.fillRect(textRect, QBrush(QColor(0, 0, 0, 180)));
        painter.setPen(color);
        painter.drawText(textRect, Qt::AlignCenter, pattern.name);
        
        painter.restore();
        
        // 리사이즈 핸들 (4개 모서리)
        const int handleSize = 8;
        painter.save();
        painter.translate(center);
        painter.rotate(pattern.angle);
        painter.translate(-center);
        
        QPointF corners[] = {
            displayRect.topLeft(),
            displayRect.topRight(),
            displayRect.bottomRight(),
            displayRect.bottomLeft()
        };
        
        painter.setPen(QPen(color.darker(), 1));
        painter.setBrush(QBrush(color));
        for (int i = 0; i < 4; i++) {
            QRectF handleRect(corners[i].x() - handleSize/2, corners[i].y() - handleSize/2, 
                             handleSize, handleSize);
            painter.drawRect(handleRect);
        }
        
        painter.restore();
        
        // 회전 핸들 (상단 중앙)
        painter.save();
        painter.translate(center);
        painter.rotate(pattern.angle);
        painter.translate(-center);
        
        QPointF rotateHandlePos(displayRect.center().x(), displayRect.top() - 20);
        QRectF rotateHandleRect(rotateHandlePos.x() - handleSize/2, rotateHandlePos.y() - handleSize/2,
                               handleSize, handleSize);
        painter.setPen(QPen(Qt::blue, 2));
        painter.setBrush(Qt::yellow);
        painter.drawEllipse(rotateHandleRect);
        
        // 회전 핸들과 패턴 연결선
        painter.setPen(QPen(Qt::blue, 1));
        painter.drawLine(QPointF(displayRect.center().x(), displayRect.top()), rotateHandlePos);
        
        painter.restore();
        
        // INS STRIP 패턴의 추가 UI 요소
        if (pattern.type == PatternType::INS && 
            pattern.inspectionMethod == InspectionMethod::STRIP) {
            drawStripGradientRange(painter, pattern);
            drawStripThicknessBoxes(painter, pattern);
        }
    }
}

// STRIP 그라디언트 범위 표시
void CameraView::drawStripGradientRange(QPainter& painter, const PatternInfo& pattern) {
    QVector<QPoint> rotatedCorners = getRotatedCorners();
    if (rotatedCorners.size() != 4) return;
    
    QPoint topLeft = rotatedCorners[0];
    QPoint topRight = rotatedCorners[1];
    QPoint bottomLeft = rotatedCorners[3];
    QPoint bottomRight = rotatedCorners[2];
    
    // 가로 방향 벡터 계산
    double widthVectorX = topRight.x() - topLeft.x();
    double widthVectorY = topRight.y() - topLeft.y();
    
    float startPercent = pattern.stripGradientStartPercent / 100.0f;
    float endPercent = pattern.stripGradientEndPercent / 100.0f;
    
    // gradient 범위 지점 계산
    QPoint posStartTop(
        qRound(topLeft.x() + widthVectorX * startPercent),
        qRound(topLeft.y() + widthVectorY * startPercent)
    );
    QPoint posStartBottom(
        qRound(bottomLeft.x() + widthVectorX * startPercent),
        qRound(bottomLeft.y() + widthVectorY * startPercent)
    );
    QPoint posEndTop(
        qRound(topLeft.x() + widthVectorX * endPercent),
        qRound(topLeft.y() + widthVectorY * endPercent)
    );
    QPoint posEndBottom(
        qRound(bottomLeft.x() + widthVectorX * endPercent),
        qRound(bottomLeft.y() + widthVectorY * endPercent)
    );
    
    // 점선 스타일 설정 (노란색)
    QPen dashPen(QColor(255, 255, 0), 2);
    dashPen.setStyle(Qt::DashLine);
    painter.setPen(dashPen);
    
    painter.drawLine(posStartTop, posStartBottom);
    painter.drawLine(posEndTop, posEndBottom);
    
    // 범위 텍스트 표시
    QFont rangeFont(NAMEPLATE_FONT_FAMILY, NAMEPLATE_FONT_SIZE, NAMEPLATE_FONT_WEIGHT);
    painter.setFont(rangeFont);
    QFontMetrics rangeFm(rangeFont);
    
    // 시작점 텍스트
    QString startText = QString("%1%").arg(pattern.stripGradientStartPercent);
    int startTextWidth = rangeFm.horizontalAdvance(startText);
    int startTextHeight = rangeFm.height();
    
    QRect startTextRect(
        posStartTop.x() - startTextWidth/2 - 2,
        posStartTop.y() - startTextHeight - 5,
        startTextWidth + 4,
        startTextHeight
    );
    painter.fillRect(startTextRect, QBrush(QColor(0, 0, 0, 180)));
    painter.setPen(Qt::yellow);
    painter.drawText(startTextRect, Qt::AlignCenter, startText);
    
    // 끝점 텍스트
    QString endText = QString("%1%").arg(pattern.stripGradientEndPercent);
    int endTextWidth = rangeFm.horizontalAdvance(endText);
    int endTextHeight = rangeFm.height();
    
    QRect endTextRect(
        posEndTop.x() - endTextWidth/2 - 2,
        posEndTop.y() - endTextHeight - 5,
        endTextWidth + 4,
        endTextHeight
    );
    painter.fillRect(endTextRect, QBrush(QColor(0, 0, 0, 180)));
    painter.setPen(Qt::yellow);
    painter.drawText(endTextRect, Qt::AlignCenter, endText);
}

// STRIP FRONT/REAR 두께 검사 박스 그리기
void CameraView::drawStripThicknessBoxes(QPainter& painter, const PatternInfo& pattern) {
    QTransform t = transform();
    double currentScale = std::sqrt(t.m11() * t.m11() + t.m12() * t.m12());
    
    QVector<QPoint> rotatedCorners = getRotatedCorners();
    if (rotatedCorners.size() != 4) return;
    
    QPoint topLeft = rotatedCorners[0];
    QPoint topRight = rotatedCorners[1];
    QPoint bottomLeft = rotatedCorners[3];
    QPoint bottomRight = rotatedCorners[2];
    
    double widthVectorX = topRight.x() - topLeft.x();
    double widthVectorY = topRight.y() - topLeft.y();
    double vectorLen = std::sqrt(widthVectorX * widthVectorX + widthVectorY * widthVectorY);
    
    if (vectorLen < 0.01) return;
    
    double boxAngle = std::atan2(widthVectorY, widthVectorX) * 180.0 / M_PI;
    double boxWidth = pattern.stripThicknessBoxWidth * currentScale;
    double boxHeight = pattern.stripThicknessBoxHeight * currentScale;
    
    // FRONT 박스
    float startPercent = pattern.stripGradientStartPercent / 100.0f;
    QPoint posStartTop(
        qRound(topLeft.x() + widthVectorX * startPercent),
        qRound(topLeft.y() + widthVectorY * startPercent)
    );
    QPoint posStartBottom(
        qRound(bottomLeft.x() + widthVectorX * startPercent),
        qRound(bottomLeft.y() + widthVectorY * startPercent)
    );
    QPoint frontBoxCenter(
        (posStartTop.x() + posStartBottom.x()) / 2,
        (posStartTop.y() + posStartBottom.y()) / 2
    );
    
    QRectF frontBoxRect(-boxWidth/2, -boxHeight/2, boxWidth, boxHeight);
    QPen frontPen(Qt::cyan, 2);
    frontPen.setStyle(Qt::DashLine);
    drawRotatedBox(painter, frontBoxRect, frontBoxCenter, boxAngle, frontPen);
    
    QString frontLabel = QString("FRONT:%1~%2mm")
                        .arg(pattern.stripThicknessMin)
                        .arg(pattern.stripThicknessMax);
    QFont frontFont(NAMEPLATE_FONT_FAMILY, NAMEPLATE_FONT_SIZE, NAMEPLATE_FONT_WEIGHT);
    QFontMetrics frontFm(frontFont);
    int frontTextW = frontFm.horizontalAdvance(frontLabel);
    int frontTextH = frontFm.height();
    QRectF frontTextRect(-frontTextW/2 - 2, -boxHeight/2 - frontTextH - 2, frontTextW + 4, frontTextH);
    drawRotatedLabel(painter, frontLabel, frontTextRect, frontBoxCenter, boxAngle,
                    QColor(0, 0, 0, 180), Qt::cyan, frontFont);
    
    // REAR 박스
    float endPercent = pattern.stripGradientEndPercent / 100.0f;
    QPoint posEndTop(
        qRound(topLeft.x() + widthVectorX * endPercent),
        qRound(topLeft.y() + widthVectorY * endPercent)
    );
    QPoint posEndBottom(
        qRound(bottomLeft.x() + widthVectorX * endPercent),
        qRound(bottomLeft.y() + widthVectorY * endPercent)
    );
    QPoint rearBoxCenter(
        (posEndTop.x() + posEndBottom.x()) / 2,
        (posEndTop.y() + posEndBottom.y()) / 2
    );
    
    QRectF rearBoxRect(-boxWidth/2, -boxHeight/2, boxWidth, boxHeight);
    QPen rearPen(QColor(135, 206, 250), 2); // 하늘색
    rearPen.setStyle(Qt::DashLine);
    drawRotatedBox(painter, rearBoxRect, rearBoxCenter, boxAngle, rearPen);
    
    QString rearLabel = QString("REAR:%1~%2mm")
                       .arg(pattern.stripThicknessMin)
                       .arg(pattern.stripThicknessMax);
    QFont rearFont(NAMEPLATE_FONT_FAMILY, NAMEPLATE_FONT_SIZE, NAMEPLATE_FONT_WEIGHT);
    QFontMetrics rearFm(rearFont);
    int rearTextW = rearFm.horizontalAdvance(rearLabel);
    int rearTextH = rearFm.height();
    QRectF rearTextRect(-rearTextW/2 - 2, -boxHeight/2 - rearTextH - 2, rearTextW + 4, rearTextH);
    drawRotatedLabel(painter, rearLabel, rearTextRect, rearBoxCenter, boxAngle,
                    QColor(0, 0, 0, 180), QColor(135, 206, 250), rearFont);
}

// 거리 측정 라인 그리기
void CameraView::drawMeasurementLine(QPainter& painter) {
    if (!isMeasuring || measureStartPoint.isNull() || measureEndPoint.isNull()) return;
    
    QPointF startDisplay = mapFromScene(measureStartPoint);
    QPointF endDisplay = mapFromScene(measureEndPoint);
    
    painter.setPen(QPen(Qt::yellow, 2));
    painter.drawLine(startDisplay, endDisplay);
    
    // 거리 계산
    double dx = measureEndPoint.x() - measureStartPoint.x();
    double dy = measureEndPoint.y() - measureStartPoint.y();
    double distancePx = std::sqrt(dx * dx + dy * dy);
    
    QString distanceText;
    bool hasCalibration = false;
    
    // STRIP 검사 패턴에서 calibration 정보 가져오기
    for (const PatternInfo& pattern : patterns) {
        if (pattern.type == PatternType::INS && 
            pattern.inspectionMethod == InspectionMethod::STRIP &&
            pattern.stripLengthCalibrationPx > 0.0 &&
            pattern.stripLengthConversionMm > 0.0) {
            
            double pixelToMm = pattern.stripLengthConversionMm / pattern.stripLengthCalibrationPx;
            double distanceMm = distancePx * pixelToMm;
            distanceText = QString("%1 mm (%2 px)").arg(distanceMm, 0, 'f', 2).arg(distancePx, 0, 'f', 1);
            hasCalibration = true;
            break;
        }
    }
    
    if (!hasCalibration) {
        distanceText = QString("%1 px").arg(distancePx, 0, 'f', 1);
    }
    
    // 텍스트 위치 (선의 중간)
    QPointF midPoint = (startDisplay + endDisplay) / 2.0;
    
    QFont distFont("Arial", 12, QFont::Bold);
    painter.setFont(distFont);
    QFontMetrics distFm(distFont);
    int textWidth = distFm.horizontalAdvance(distanceText);
    int textHeight = distFm.height();
    
    QRectF textRect(midPoint.x() - textWidth/2 - 4, midPoint.y() - textHeight/2 - 2,
                   textWidth + 8, textHeight + 4);
    painter.fillRect(textRect, QColor(0, 0, 0, 180));
    painter.setPen(Qt::yellow);
    painter.drawText(textRect, Qt::AlignCenter, distanceText);
    
    // 시작점과 끝점에 원 그리기
    painter.setBrush(Qt::yellow);
    painter.drawEllipse(startDisplay, 4, 4);
    painter.drawEllipse(endDisplay, 4, 4);
}

// 현재 그리는 사각형 그리기
void CameraView::drawCurrentDrawingRect(QPainter& painter) {
    if (currentRect.isNull()) return;
    
    QPointF topLeft = mapFromScene(currentRect.topLeft());
    QPointF bottomRight = mapFromScene(currentRect.bottomRight());
    QRectF displayRect(topLeft, bottomRight);
    
    painter.setPen(QPen(currentDrawColor, 2, Qt::DashLine));
    painter.drawRect(displayRect);
}
