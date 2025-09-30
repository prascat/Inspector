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
#include <cmath>

CameraView::CameraView(QWidget *parent) : QLabel(parent) {
    setMinimumSize(640, 480);
    setAlignment(Qt::AlignCenter);
    setStyleSheet("border: 2px solid gray; background-color: black; color: white;");
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    statusInfo = "";
    selectedInspectionPatternId = QUuid(); // 검사 결과 필터 초기화
    setText(TR("NO_CONNECTION"));
}

void CameraView::updateUITexts() {
    // CameraView에 표시되는 텍스트 요소 업데이트
    if (m_calibrationMode) {
        m_calibrationText = TR("CALIBRATION_IN_PROGRESS");
    }

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
    } else {
        // 다른 상태 메시지는 번역
        m_statusText = TR("CALIBRATION_IN_PROGRESS");
        // 필요한 다른 상태 메시지 번역 추가
    }

    // 위젯 다시 그리기
    update();
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
    QLabel::keyPressEvent(event);
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

QPoint CameraView::displayToOriginal(const QPoint& displayPos) {
    if (backgroundPixmap.isNull()) return displayPos;
    
    QSize viewportSize = size();
    QSize imgSize = backgroundPixmap.size();
    
    // 모든 계산을 정수로 수행
    int scaledWidth = qRound(imgSize.width() * zoomFactor);
    int scaledHeight = qRound(imgSize.height() * zoomFactor);
    
    int offsetX = (viewportSize.width() - scaledWidth) / 2 + panOffset.x();
    int offsetY = (viewportSize.height() - scaledHeight) / 2 + panOffset.y();
    
    int imgX = displayPos.x() - offsetX;
    int imgY = displayPos.y() - offsetY;
    
    // 역변환도 정수 연산
    int origX = qRound((double)imgX / zoomFactor);
    int origY = qRound((double)imgY / zoomFactor);
    
    // 범위 제한
    origX = qBound(0, origX, imgSize.width() - 1);
    origY = qBound(0, origY, imgSize.height() - 1);
    
    return QPoint(origX, origY);
}

QPoint CameraView::originalToDisplay(const QPoint& originalPos) {
    if (backgroundPixmap.isNull()) return originalPos;
    
    QSize viewportSize = size();
    QSize imgSize = backgroundPixmap.size();
    
    // 모든 계산을 정수로 수행
    int scaledWidth = qRound(imgSize.width() * zoomFactor);
    int scaledHeight = qRound(imgSize.height() * zoomFactor);
    
    int offsetX = (viewportSize.width() - scaledWidth) / 2 + panOffset.x();
    int offsetY = (viewportSize.height() - scaledHeight) / 2 + panOffset.y();
    
    int dispX = qRound(originalPos.x() * zoomFactor) + offsetX;
    int dispY = qRound(originalPos.y() * zoomFactor) + offsetY;
    
    return QPoint(dispX, dispY);
}

void CameraView::mousePressEvent(QMouseEvent* event) {
    // View 모드에서는 모든 편집 기능 차단 (패닝과 줌만 허용)
    if (m_editMode == EditMode::View) {
        // Ctrl 키가 눌렸고 확대 상태면 패닝만 허용
        if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier) && zoomFactor > 0.2) {
            isPanning = true;
            panStartPos = event->pos();
            panStartOffset = panOffset;
            setCursor(Qt::ClosedHandCursor);
            return;
        }
        // View 모드에서는 다른 모든 마우스 이벤트 무시
        QLabel::mousePressEvent(event);
        return;
    }
    
    if (event->button() == Qt::LeftButton) {
        // 모든 마우스 클릭 시 검사 결과 필터 해제
        if (!selectedInspectionPatternId.isNull()) {
            selectedInspectionPatternId = QUuid();
            update();
            emit selectedInspectionPatternCleared();
        }
        
        // Ctrl 키가 눌렸고 확대 상태면 패닝 시작
        if ((event->modifiers() & Qt::ControlModifier) && zoomFactor > 0.2) {
            isPanning = true;
            panStartPos = event->pos();
            panStartOffset = panOffset;
            setCursor(Qt::ClosedHandCursor);
            return;
        }

        QPoint pos = event->pos();
        QPoint originalPos = displayToOriginal(pos);

        // DRAW 모드에서는 무조건 새 패턴 그리기만 허용
        if (m_editMode == EditMode::Draw) {
            isDrawing = true;
            startPoint = originalPos;
            currentRect = QRect();
            setCursor(Qt::ArrowCursor);
            update();
            return;
        }

        // MOVE 모드에서 회전 핸들 클릭: 회전 시작
        if (m_editMode == EditMode::Move && !selectedPatternId.isNull() && getRotateHandleAt(pos) == 1) {
            isRotating = true;
            rotateStartPos = pos;
            PatternInfo* pattern = getPatternById(selectedPatternId);
            if (pattern) {
                initialAngle = pattern->angle;
                // 회전 중심점을 더 정확하게 계산 (부동소수점 좌표 유지)
                QPointF centerOriginal = pattern->rect.center();
                
                // 원본 좌표를 화면 좌표로 변환할 때 정밀도 유지
                QSize viewportSize = size();
                QSize imgSize = backgroundPixmap.size();
                
                if (!backgroundPixmap.isNull() && imgSize.width() > 0 && imgSize.height() > 0) {
                    int scaledWidth = qRound(imgSize.width() * zoomFactor);
                    int scaledHeight = qRound(imgSize.height() * zoomFactor);
                    
                    int offsetX = (viewportSize.width() - scaledWidth) / 2 + panOffset.x();
                    int offsetY = (viewportSize.height() - scaledHeight) / 2 + panOffset.y();
                    
                    double dispX = centerOriginal.x() * zoomFactor + offsetX;
                    double dispY = centerOriginal.y() * zoomFactor + offsetY;
                    
                    rotationCenter = QPoint(qRound(dispX), qRound(dispY));
                } else {
                    rotationCenter = originalToDisplay(centerOriginal.toPoint());
                }
            }
            setCursor(Qt::OpenHandCursor);
            return;
        }

        int handleIdx = getCornerHandleAt(pos);
        // 회전 핸들 영역과 겹치는 경우 크기 조정 차단
        if (m_editMode == EditMode::Move && !selectedPatternId.isNull() && handleIdx != -1 && getRotateHandleAt(pos) == -1) {
            isResizing = true;
            activeHandleIdx = handleIdx;

            // ★★★ 회전된 상태의 실제 꼭짓점을 사용하여 고정점 설정 ★★★
            PatternInfo* pattern = getPatternById(selectedPatternId);
            if (pattern) {
                // 회전된 상태의 실제 꼭짓점들을 가져오기
                QVector<QPoint> rotatedCorners = getRotatedCorners();
                
                if (rotatedCorners.size() == 4) {
                    // 클릭한 핸들의 대각선 반대편 꼭짓점을 고정점으로 설정
                    int fixedHandleIdx = (handleIdx + 2) % 4;  // 대각선 반대편
                    fixedScreenPos = rotatedCorners[fixedHandleIdx];
                    
                } else {
                    // 백업: 원본 방식
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

        // MOVE 모드에서 패턴 내부 클릭: 이동 시작
        QUuid hitPatternId = hitTest(pos);
        
        if (m_editMode == EditMode::Move && !hitPatternId.isNull()) {
            setSelectedPatternId(hitPatternId);
            isDragging = true;
            PatternInfo* pattern = getPatternById(hitPatternId);
            if (pattern) {
                QPoint patternTopLeft = originalToDisplay(pattern->rect.topLeft().toPoint());
                dragOffset = pos - patternTopLeft;
            }
            return;
        }

        // MOVE 모드에서 빈 공간 클릭: 패턴 선택 해제 및 검사 결과 필터 해제
        if (m_editMode == EditMode::Move && hitPatternId.isNull()) {
            selectedPatternId = QUuid();
            selectedInspectionPatternId = QUuid();  // 검사 결과 필터 해제
            isDragging = false;
            isResizing = false;
            isRotating = false;
            activeHandle = ResizeHandle::None;
            update();
            emit selectedInspectionPatternCleared();  // TeachingWidget에 알림
            return;
        }
        
        // 추가 안전장치: 어떤 모드든 빈 공간 클릭 시 검사 결과 필터 해제
        if (hitPatternId.isNull() && !selectedInspectionPatternId.isNull()) {
            selectedInspectionPatternId = QUuid();
            update();
            emit selectedInspectionPatternCleared();
        }
    }
    QLabel::mousePressEvent(event);
}

void CameraView::mouseMoveEvent(QMouseEvent* event) {
    // 패닝 모드일 때 처리
    if (isPanning) {
        QPoint delta = event->pos() - panStartPos;
        panOffset = panStartOffset + delta;
        update();
        return;
    }

    // View 모드에서는 패닝만 허용하고 다른 모든 편집 기능 차단
    if (m_editMode == EditMode::View) {
        setCursor(Qt::ArrowCursor);
        QLabel::mouseMoveEvent(event);
        return;
    }

    QPoint pos = event->pos();
    QPoint originalPos = displayToOriginal(pos);

    // DRAW 모드에서는 항상 기본 커서
    if (m_editMode == EditMode::Draw) {
        setCursor(Qt::ArrowCursor);
        if (isDrawing) {
            dragEndPoint = originalPos;
            QRect newRect = QRect(startPoint, originalPos).normalized();
            if (newRect.width() > 5 || newRect.height() > 5) {
                currentRect = newRect;
                update();
            }
        }
        QLabel::mouseMoveEvent(event);
        return;
    }
    
    // MOVE 모드에서는 그리기 차단
    if (m_editMode == EditMode::Move && isDrawing) {
        isDrawing = false;
        currentRect = QRect();
        update();
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

        // ★★★ 완벽한 코드와 동일한 방식: 회전 고려한 정밀한 계산 ★★★
        double fx = fixedScreenPos.x();
        double fy = fixedScreenPos.y();
        double mx = event->x();
        double my = event->y();

        // 중심점은 마우스와 고정점의 중간 (실수 정밀도 유지)
        double cx = (fx + mx) / 2.0;
        double cy = (fy + my) / 2.0;

        // 회전 각도를 라디안으로 변환
        double rad = pat->angle * M_PI / 180.0;
        double cos_a = std::cos(rad);
        double sin_a = std::sin(rad);

        // 회전 좌표계에서 고정점과 마우스 위치 벡터 계산
        double dx = mx - fx;
        double dy = my - fy;

        // 로컬 좌표계에서 너비와 높이 계산 (회전 고려)
        double local_dx = dx * cos_a + dy * sin_a;
        double local_dy = -dx * sin_a + dy * cos_a;

        // 화면 좌표에서의 새로운 크기 (절대값)
        double screenWidth = std::abs(local_dx);
        double screenHeight = std::abs(local_dy);
        
        // 최소 크기 제한 (10픽셀로 줄임)
        screenWidth = std::max(screenWidth, 10.0);
        screenHeight = std::max(screenHeight, 10.0);

        // 원본 좌표로 변환하여 최종 저장
        QPoint centerOriginal = displayToOriginal(QPoint(qRound(cx), qRound(cy)));
        double originalWidth = screenWidth / zoomFactor;
        double originalHeight = screenHeight / zoomFactor;
        
        // QRectF로 정밀도 유지하여 저장
        pat->rect = QRectF(
            centerOriginal.x() - originalWidth / 2.0,
            centerOriginal.y() - originalHeight / 2.0,
            originalWidth,
            originalHeight
        );
        
        // 리사이징 중 실시간 템플릿 업데이트를 위한 시그널 발생
        emit patternRectChanged(selectedPatternId, QRect(static_cast<int>(pat->rect.x()), static_cast<int>(pat->rect.y()),
                                                        static_cast<int>(pat->rect.width()), static_cast<int>(pat->rect.height())));
        
        update();
        return;
    }

    // 드래깅 처리 - 그룹 이동 지원
    if (isDragging && !selectedPatternId.isNull()) {
        PatternInfo* pattern = getPatternById(selectedPatternId);
        if (!pattern) return;

        QPoint patternTopLeftScreen = originalToDisplay(QPoint(static_cast<int>(pattern->rect.x()), static_cast<int>(pattern->rect.y())));
        QPoint newTopLeftScreen = pos - dragOffset;
        QPoint newTopLeftOriginal = displayToOriginal(newTopLeftScreen);

        double deltaX = newTopLeftOriginal.x() - pattern->rect.x();
        double deltaY = newTopLeftOriginal.y() - pattern->rect.y();

        QRectF newRect = pattern->rect;
        newRect.moveTopLeft(QPointF(newTopLeftOriginal));

        if (newRect.left() >= 0 && newRect.top() >= 0 &&
            newRect.right() < originalImageSize.width() &&
            newRect.bottom() < originalImageSize.height()) {

            if (pattern->type == PatternType::FID && !pattern->childIds.isEmpty()) {
                pattern->rect = newRect;
                emit patternRectChanged(selectedPatternId, QRect(static_cast<int>(newRect.x()), static_cast<int>(newRect.y()), 
                                                               static_cast<int>(newRect.width()), static_cast<int>(newRect.height())));

                for (const QUuid& childId : pattern->childIds) {
                    PatternInfo* child = getPatternById(childId);
                    if (!child) continue;

                    QRectF childRect = child->rect;
                    childRect.translate(deltaX, deltaY);

                    if (childRect.left() >= 0 && childRect.top() >= 0 &&
                        childRect.right() < originalImageSize.width() &&
                        childRect.bottom() < originalImageSize.height() &&
                        childRect.width() > 0 && childRect.height() > 0) {

                        child->rect = childRect;
                        emit patternRectChanged(childId, QRect(static_cast<int>(childRect.x()), static_cast<int>(childRect.y()),
                                                             static_cast<int>(childRect.width()), static_cast<int>(childRect.height())));
                    }
                }
            } else {
                pattern->rect = newRect;
                emit patternRectChanged(selectedPatternId, QRect(static_cast<int>(newRect.x()), static_cast<int>(newRect.y()),
                                                               static_cast<int>(newRect.width()), static_cast<int>(newRect.height())));
            }

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
    QLabel::mouseMoveEvent(event);
}

void CameraView::mouseReleaseEvent(QMouseEvent* event) {
    if (isPanning && event->button() == Qt::LeftButton) {
        isPanning = false;
        setCursor(Qt::ArrowCursor);
        return;
    }

    // View 모드에서는 패닝 해제만 허용하고 다른 모든 편집 기능 차단
    if (m_editMode == EditMode::View) {
        QLabel::mouseReleaseEvent(event);
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
        if (m_calibrationMode) {
            emit calibrationRectDrawn(rect);
        } else {
            emit rectDrawn(rect);
        }
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
    QLabel::mouseReleaseEvent(event);
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
                fidPattern.matchThreshold = 0.8;
                
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
                QMessageBox::StandardButton reply = UIColors::showQuestion(this, "그룹 해제 확인", 
                                                                  QString("'%1' 그룹을 해제하시겠습니까?\n그룹 내 모든 패턴이 독립적으로 변경됩니다.").arg(pattern->name),
                                                                  QMessageBox::Yes | QMessageBox::No,
                                                                  QMessageBox::No);
                
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
                newPattern.matchThreshold = 0.8;
                newPattern.useRotation = false;
                newPattern.minAngle = 0.0;
                newPattern.angle = 0.0;  // 각도 명시적으로 0으로 설정
                
                newPattern.maxAngle = 360.0;
                newPattern.angleStep = 1.0;
                
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
                newPattern.inspectionMethod = InspectionMethod::COLOR;
                newPattern.passThreshold = 0.8;
                newPattern.compareMethod = 0;
                newPattern.angle = 0.0;  // 각도 명시적으로 0으로 설정
                
                newPattern.lowerThreshold = 0.0;
                newPattern.upperThreshold = 1.0;
                
                addPattern(newPattern);
                setSelectedPatternId(newPattern.id);
                emit insTemplateUpdateRequired(newPattern.id);
                currentRect = QRect();
                update();
            }
            
            return;
        }
        
        // 패턴 분류
        QList<QUuid> roiPatternIds;
        QList<QUuid> fidPatternIds;
        QList<QUuid> groupedFidPatternIds;
        QList<QUuid> ungroupedInsPatternIds;
        QList<QUuid> groupedInsPatternIds;
        QList<QUuid> groupedRoiPatternIds;
        
        for (const QUuid& id : selectedPatterns) {
            PatternInfo* pattern = getPatternById(id);
            if (!pattern) continue;
            
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
            UIColors::showWarning(this, "그룹화 실패", 
                              "선택 영역 내에 ROI 패턴이 여러 개 있습니다.\n"
                              "그룹화를 위해서는 정확히 하나의 ROI 패턴만 선택해야 합니다.");
            return;
        }
        
        // FID가 없으면 그룹화 불가
        if (fidPatternId.isNull()) {
            UIColors::showWarning(this, "그룹화 실패", 
                              "ROI 기반 그룹화를 위해서는 FID 패턴이 필요합니다.\n"
                              "선택 영역에 FID 패턴을 포함시켜 주세요.");
            return;
        }
        
        // FID가 1개보다 많으면 그룹화 불가
        if (fidCount > 1) {
            UIColors::showWarning(this, "그룹화 실패", 
                              "선택 영역 내에 FID 패턴이 여러 개 있습니다.\n"
                              "ROI 기반 그룹화를 위해서는 정확히 하나의 FID 패턴만 선택해야 합니다.");
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
        UIColors::showInformation(this, "그룹화 완료", message);
    }
    // ROI가 없는 경우: 기존 FID->INS 구조 유지
    else {
        // FID 패턴이 없으면 그룹화 불가
        if (fidPatternId.isNull()) {
            QMessageBox::warning(this, "그룹화 실패", 
                              "선택 영역 내에 FID 패턴이 없습니다.\n"
                              "그룹화를 위해서는 하나의 FID 패턴이 필요합니다.");
            return;
        }
        
        // FID 패턴이 1개보다 많으면 그룹화 불가
        if (fidCount > 1) {
            QMessageBox::warning(this, "그룹화 실패", 
                              "선택 영역 내에 FID 패턴이 여러 개 있습니다.\n"
                              "그룹화를 위해서는 정확히 하나의 FID 패턴만 선택해야 합니다.");
            return;
        }
        
        // INS 패턴이 없으면 경고
        if (insPatternIds.isEmpty()) {
            QMessageBox::information(this, "그룹화 완료", 
                                  "FID 패턴이 그룹 헤더로 설정되었지만 추가된 INS 패턴이 없습니다.");
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
            QMessageBox::information(this, "그룹화 완료", 
                                  QString("FID 기반 그룹화 완료: FID 패턴과 %1개의 INS 패턴이 그룹화되었습니다.")
                                  .arg(insPatternIds.size()));
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
         UIColors::showInformation(this, "그룹 해제 실패", "선택 영역 내에 그룹화된 패턴이 없습니다.");
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
    
    UIColors::showInformation(this, "그룹 해제 완료", message);
    
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
    
    qDebug() << "=== updateInspectionResult 호출됨 ===";
    qDebug() << "검출된 각도 정보:";
    for (auto it = result.angles.begin(); it != result.angles.end(); ++it) {
        qDebug() << QString("패턴 ID: %1, 각도: %2°").arg(it.key().toString()).arg(it.value());
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
                qDebug() << QString("★ 패턴 '%1' 각도 UI 업데이트: %2° → %3°")
                            .arg(patterns[i].name).arg(oldAngle).arg(detectedAngle);
                
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

                            qDebug() << QString("INS 패턴 '%1' 그룹 변환 적용: 중심 (%2,%3) 각도 %4° -> %5°")
                                        .arg(patterns[j].name)
                                        .arg(QString::number(childTeachingCenter.x(), 'f', 3))
                                        .arg(QString::number(childTeachingCenter.y(), 'f', 3))
                                        .arg(QString::number(childOldAngle, 'f', 2))
                                        .arg(QString::number(patterns[j].angle, 'f', 2));

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

void CameraView::drawInspectionResultsVector(QPainter& painter, const InspectionResult& result) {
    // 티칭 모드와 동일한 폰트 및 스타일 설정
    QFont standardFont("Arial", 10);
    standardFont.setBold(true);
    const int thickness = 2; // 티칭 두께와 동일하게
    
    // 선택된 패턴이 있는 경우 해당 패턴만 표시, 없으면 전체 표시
    bool hasSelectedPattern = !selectedInspectionPatternId.isNull();
    
    // FID 패턴 결과 그리기 (벡터 방식)
    for (auto it = result.fidResults.begin(); it != result.fidResults.end(); ++it) {
        QUuid patternId = it.key();
        bool passed = it.value();
        
        // 선택된 패턴 필터링
        if (hasSelectedPattern && patternId != selectedInspectionPatternId) {
            continue;
        }
        
        if (!result.locations.contains(patternId)) continue;
        
        cv::Point matchLoc = result.locations[patternId];
        double score = result.matchScores.value(patternId, 0.0);
        double detectedAngle = result.angles.value(patternId, 0.0); // 검출된 각도
        
        // 패턴 정보 찾기
        const PatternInfo* patternInfo = nullptr;
        for (const PatternInfo& pattern : patterns) {
            if (pattern.id == patternId) {
                patternInfo = &pattern;
                break;
            }
        }
        
        // ROI 패턴은 검사 결과 표시에서 제외
        if (patternInfo && patternInfo->type == PatternType::ROI) {
            continue;
        }
        
        // **현재 카메라에 해당하는 패턴인지 확인**
        if (patternInfo) {
            bool patternVisible = false;
            if (!currentCameraUuid.isEmpty()) {
                // 시뮬레이션 모드: 현재 시뮬레이션 카메라와 관련된 패턴만
                patternVisible = (patternInfo->cameraUuid == currentCameraUuid || 
                                patternInfo->cameraUuid.isEmpty());
            } else {
                // 일반 모드: 현재 카메라 UUID와 일치하는 패턴만
                // 안전 검사 추가
                if (!currentCameraUuid.isEmpty()) {
                    patternVisible = (patternInfo->cameraUuid == currentCameraUuid ||
                                    patternInfo->cameraUuid.isEmpty());
                } else {
                    // currentCameraUuid가 비어있으면 모든 패턴 표시하지 않음
                    patternVisible = false;
                }
            }
            
            if (!patternVisible) {
                continue; // 현재 카메라의 패턴이 아니면 건너뛰기
            }
        }
        
        // **수정**: 최종 각도는 검출된 각도를 그대로 사용 (이미 티칭 각도가 포함됨)
        double finalAngle = detectedAngle;
        // if (patternInfo) {
        //     finalAngle += patternInfo->angle; // 중복 적용 방지를 위해 제거
        // }
        
        QString patternName = patternInfo ? patternInfo->name : "FID_UNKNOWN";
        QColor patternColor = UIColors::FIDUCIAL_COLOR; // 항상 UIColors::FIDUCIAL_COLOR 사용
        QColor borderColor = patternColor; // 패턴 타입 색상 사용
        
        // 매칭된 위치 계산 (원본 좌표 → 화면 좌표)
        int width = patternInfo ? patternInfo->rect.width() : 40;
        int height = patternInfo ? patternInfo->rect.height() : 40;
        
        QRect matchRectOriginal(
            matchLoc.x - width / 2,
            matchLoc.y - height / 2,
            width,
            height
        );
        
        // 화면 좌표로 변환
        QPoint topLeft = originalToDisplay(matchRectOriginal.topLeft());
        QPoint bottomRight = originalToDisplay(matchRectOriginal.bottomRight());
        QRect matchRect(topLeft, bottomRight);
        
        // 패턴 중심점 계산
        QPointF center = matchRect.center();
        
        // 각도만큼 회전시켜 사각형 그리기
        painter.save();
        painter.translate(center);
        // 검출된 각도 그대로 회전 (부호 반전 제거)
        painter.rotate(finalAngle); 
        
        // 중심을 기준으로 한 상대 좌표로 사각형 그리기
        QRect rotatedRect(-matchRect.width()/2, -matchRect.height()/2, 
                         matchRect.width(), matchRect.height());
        
        painter.setPen(QPen(borderColor, thickness));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rotatedRect);
        
        // 회전 각도가 작을 때 시각적 확인을 위해 작은 마커 추가
        if (std::abs(finalAngle) > 0.1) { // 0.1도 이상일 때만
            painter.setPen(QPen(Qt::red, 1));
            painter.drawLine(-5, 0, 5, 0); // 중심에 빨간 십자가
            painter.drawLine(0, -5, 0, 5);
        }
        
        painter.restore(); // 변환 복원
        
        // 텍스트 그리기 (회전된 패턴 위에)
        QString labelText = QString("%1: %2").arg(patternName).arg(score, 0, 'f', 2);
        if (finalAngle != 0.0) {
            labelText += QString(" A:%1°").arg(finalAngle, 0, 'f', 1);
        }
        
        QFontMetrics fm(standardFont);
        int textWidth = fm.horizontalAdvance(labelText);
        int textHeight = fm.height();
        
        // FID 패턴 텍스트를 회전된 상태에서 그리기
        painter.save();
        painter.translate(center);
        painter.rotate(finalAngle);
        
        // 패턴 위쪽 왼쪽에 텍스트 위치 (상대 좌표, 왼쪽 정렬)
        QRect textRect(-matchRect.width()/2, -matchRect.height()/2 - textHeight - 2, textWidth + 6, textHeight);

        // 배경 및 텍스트 그리기 (티칭 스타일) - 통과/실패에 따라 배경 색상 조정
        QColor bgColor = passed ? patternColor : QColor(200, 0, 0); // 통과시 패턴 색상, 실패시 빨간색
        bgColor.setAlpha(180);
        painter.fillRect(textRect, bgColor);
        
        QColor textColor = UIColors::getTextColor(bgColor);
        painter.setPen(textColor);
        painter.setFont(standardFont);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, labelText);
        
        painter.restore();
    }
    
     // INS 패턴 결과 그리기 (검사 결과 이미지 포함)
    for (auto it = result.insResults.begin(); it != result.insResults.end(); ++it) {
        QUuid patternId = it.key();
        bool passed = it.value();
        
        // 선택된 패턴 필터링
        if (hasSelectedPattern && patternId != selectedInspectionPatternId) {
            continue;
        }
        
        double score = result.insScores.value(patternId, 0.0);
        
        // 패턴 정보 찾기
        const PatternInfo* patternInfo = nullptr;
        for (const PatternInfo& pattern : patterns) {
            if (pattern.id == patternId) {
                patternInfo = &pattern;
                break;
            }
        }
        
        if (!patternInfo) continue;
        
        // ROI 패턴은 검사 결과 표시에서 제외
        if (patternInfo->type == PatternType::ROI) {
            continue;
        }

        // **현재 카메라에 해당하는 패턴인지 확인**
        bool patternVisible = false;
        if (!currentCameraUuid.isEmpty()) {
            // 시뮬레이션 모드: 현재 시뮬레이션 카메라와 관련된 패턴만
            patternVisible = (patternInfo->cameraUuid == currentCameraUuid || 
                            patternInfo->cameraUuid.isEmpty());
        } else {
            // 일반 모드: 현재 카메라 UUID와 일치하는 패턴만
            // 안전 검사 추가
            if (!currentCameraUuid.isEmpty()) {
                patternVisible = (patternInfo->cameraUuid == currentCameraUuid ||
                                patternInfo->cameraUuid.isEmpty());
            } else {
                // currentCameraUuid가 비어있으면 모든 패턴 표시하지 않음
                patternVisible = false;
            }
        }
        
        if (!patternVisible) {
            continue; // 현재 카메라의 패턴이 아니면 건너뛰기
        }

        // 마스크 필터가 있으면 건너뛰기 (검사 결과 없음)
        bool hasMaskFilter = false;
        for (const FilterInfo& filter : patternInfo->filters) {
            if (filter.enabled && filter.type == FILTER_MASK) {
                hasMaskFilter = true;
                break;
            }
        }
        
        if (hasMaskFilter) continue; // 마스크 필터는 그리지 않음

        QString patternName = patternInfo->name;
        QColor patternColor = UIColors::INSPECTION_COLOR; // 항상 UIColors::INSPECTION_COLOR 사용
        QColor borderColor = patternColor; // 패턴 타입 색상 사용
        
        painter.setPen(QPen(borderColor, thickness));
        painter.setBrush(Qt::NoBrush);
        
        // 검사 방법 이름 가져오기
        QString methodName = "UNKNOWN";
        if (result.insMethodTypes.contains(patternId)) {
            int methodType = result.insMethodTypes[patternId];
            methodName = InspectionMethod::getName(methodType);
        }
        
        // 검사 영역 그리기
        QRectF inspRectOriginal;
        if (result.adjustedRects.contains(patternId)) {
            inspRectOriginal = result.adjustedRects[patternId];
        } else {
            inspRectOriginal = patternInfo->rect;
        }
        
        // 화면 좌표로 변환 (QPointF를 QPoint로 변환)
        QPoint topLeft = originalToDisplay(inspRectOriginal.topLeft().toPoint());
        QPoint bottomRight = originalToDisplay(inspRectOriginal.bottomRight().toPoint());
        QRect inspRect(topLeft, bottomRight);
        
        // INS 패턴의 각도 정보 계산
        double insAngle = 0.0;
        if (result.parentAngles.contains(patternId)) {
            insAngle = result.parentAngles.value(patternId, 0.0);
        }
        
        // 패턴 중심점 계산
        QPointF inspCenter = inspRect.center();
        
        // 패턴 박스는 티칭한 각도로 회전시켜 그리기
        painter.save();
        painter.translate(inspCenter);
        painter.rotate(insAngle); // 티칭한 각도로 회전
        
        // 중심을 기준으로 한 상대 좌표로 사각형 그리기
        QRect rotatedRect(-inspRect.width()/2, -inspRect.height()/2, 
                         inspRect.width(), inspRect.height());
        
        // 검사 영역 사각형 그리기 (회전된 박스)
        painter.drawRect(rotatedRect);
        // 회전 변환 복원
        painter.restore();
        
        // 검사 결과 이미지가 있으면 회전된 패턴 박스로 마스킹해서 그리기
        if (result.insProcessedImages.contains(patternId)) {
            cv::Mat resultImage = result.insProcessedImages[patternId];
            if (!resultImage.empty()) {
                QImage qResultImage = InsProcessor::matToQImage(resultImage);
                if (!qResultImage.isNull()) {
                    // 검사 결과 이미지는 정사각형 크기로 추출됨
                    // 원본 패턴 크기에 맞춰 정사각형 크기로 표시 영역 계산
                    int originalWidth = inspRect.width();
                    int originalHeight = inspRect.height();
                    
                    // 회전각에 따른 최소 필요 사각형 크기 계산 (extractROI와 동일한 로직)
                    double angleRad = std::abs(insAngle) * M_PI / 180.0;
                    double rotatedWidth = std::abs(originalWidth * std::cos(angleRad)) + std::abs(originalHeight * std::sin(angleRad));
                    double rotatedHeight = std::abs(originalWidth * std::sin(angleRad)) + std::abs(originalHeight * std::cos(angleRad));
                    int maxSize = static_cast<int>(std::max(rotatedWidth, rotatedHeight)) + 10;
                    
                    // 정사각형 표시 영역 계산 (중심점 기준)
                    QRect squareRect(
                        inspCenter.x() - maxSize/2,
                        inspCenter.y() - maxSize/2,
                        maxSize,
                        maxSize
                    );
                    
                    // 검사 결과 이미지를 정사각형 크기에 맞춰 스케일링
                    QImage scaledResultImage = qResultImage.scaled(
                        squareRect.size(), 
                        Qt::IgnoreAspectRatio, 
                        Qt::SmoothTransformation
                    );
                    
                    // 회전된 패턴 박스 모양으로 클리핑 마스크 설정
                    painter.save();
                    
                    // 회전된 패턴 박스의 4개 꼭짓점 계산
                    QPointF center = inspRect.center();
                    QTransform transform;
                    transform.translate(center.x(), center.y());
                    transform.rotate(insAngle);
                    transform.translate(-center.x(), -center.y());
                    
                    QPolygonF rotatedPolygon = transform.map(QPolygonF(QRectF(inspRect)));
                    
                    // 회전된 다각형으로 클리핑
                    QPainterPath clipPath;
                    clipPath.addPolygon(rotatedPolygon);
                    painter.setClipPath(clipPath);
                    
                    // 1. 먼저 클리핑된 전체 영역을 배경색으로 완전히 채우기
                    painter.setOpacity(1.0);
                    painter.fillPath(clipPath, QColor(200, 200, 200)); // 회색 배경으로 완전히 채움
                    
                    // 2. 그 위에 검사 결과 그리기 (정사각형 영역에)
                    painter.setOpacity(0.8);  // 검사 결과 투명도
                    painter.drawImage(squareRect, scaledResultImage);
                    painter.setOpacity(1.0);  // 투명도 원복
                    
                    painter.restore(); // 클리핑 해제
                }
            }
        }
        
        // 검사 결과 이미지 위에 패턴 박스를 다시 명확하게 그리기
        painter.save();
        painter.translate(inspCenter);
        painter.rotate(insAngle); // 티칭한 각도로 회전
        
        // 중심을 기준으로 한 상대 좌표로 사각형 그리기
        QRect finalRotatedRect(-inspRect.width()/2, -inspRect.height()/2, 
                         inspRect.width(), inspRect.height());
        
        // 패턴 박스를 명확한 색상으로 다시 그리기 (티칭 두께와 동일)
        painter.setPen(QPen(borderColor, thickness)); // 티칭과 동일한 두께
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(finalRotatedRect);
        
        painter.restore(); // 변환 복원
        
        // 간단한 텍스트 형태: "패턴명 (검사방법): 점수"
        QString labelText = QString("%1 (%2): %3")
                        .arg(patternName)
                        .arg(methodName)
                        .arg(score, 0, 'f', 2);

        // INS 패턴의 각도 정보 추가
        if (insAngle != 0.0) {
            labelText += QString(" A:%1°").arg(insAngle, 0, 'f', 1);
        }
        
        // STRIP 검사인 경우 간단한 품질 표시만 추가
        if (result.insMethodTypes.value(patternId, -1) == static_cast<int>(InspectionMethod::STRIP) &&
            result.stripNeckMeasureCount.contains(patternId) && 
            result.stripNeckMeasureCount.value(patternId) > 0) {
            
            double avgWidth = result.stripNeckAvgWidths.value(patternId, 0.0);
            double stdDev = result.stripNeckStdDevs.value(patternId, 0.0);
            
            // 간단한 품질 정보만 추가
            labelText += QString("\n목폭: %1±%2px")
                        .arg(avgWidth, 0, 'f', 1)
                        .arg(stdDev, 0, 'f', 1);
        }
        
        QFontMetrics fm(standardFont);
        QStringList lines = labelText.split('\n');
        int maxWidth = 0;
        for (const QString& line : lines) {
            maxWidth = qMax(maxWidth, fm.horizontalAdvance(line));
        }
        int totalHeight = fm.height() * lines.size();
        
        // INS 패턴 텍스트를 회전된 상태에서 그리기
        painter.save();
        painter.translate(inspCenter);
        painter.rotate(insAngle);
        
        // 패턴 위쪽 왼쪽에 텍스트 위치 (상대 좌표, 왼쪽 정렬)
        QPoint textPos(-inspRect.width()/2, -inspRect.height()/2 - totalHeight - 2);
        QRect textRect(textPos.x(), textPos.y(), maxWidth + 6, totalHeight);
        
        // 배경 및 텍스트 그리기
        QColor bgColor = passed ? patternColor : QColor(200, 0, 0);
        bgColor.setAlpha(180);
        painter.fillRect(textRect, bgColor);
        
        QColor textColor = UIColors::getTextColor(bgColor);
        painter.setPen(textColor);
        painter.setFont(standardFont);
        
        // 여러 줄 텍스트 그리기 (왼쪽 정렬)
        int yOffset = 0;
        for (const QString& line : lines) {
            QPoint linePos(textPos.x() + 3, textPos.y() + yOffset + fm.height());
            painter.drawText(linePos, line);
            yOffset += fm.height();
        }
        
        painter.restore();
        
        // STRIP 검사의 목 폭 측정 위치 시각화 (실제 측정 지점)
        if (result.insMethodTypes.value(patternId, -1) == static_cast<int>(InspectionMethod::STRIP) &&
            result.stripNeckMeasureX.contains(patternId)) {
            
            int measureX = result.stripNeckMeasureX.value(patternId);
            
            // 원본 이미지 좌표를 화면 좌표로 변환
            QPoint topPoint = originalToDisplay(QPoint(measureX, inspRectOriginal.y()));
            QPoint bottomPoint = originalToDisplay(QPoint(measureX, inspRectOriginal.y() + inspRectOriginal.height()));
            
            // 목 폭 측정선 그리기 (붉은색 점선)
            QPen dashPen(QColor(255, 0, 0), 2);  // 붉은색
            dashPen.setStyle(Qt::DashLine);  // 점선
            painter.setPen(dashPen);
            painter.drawLine(topPoint, bottomPoint);
            
            // 측정 경계 표시 (작은 사각형들)
            painter.setBrush(QColor(255, 0, 0));
            painter.setPen(Qt::NoPen);
            
            // 경계를 따라 작은 표시점들 그리기
            for (int y = inspRectOriginal.y(); y < inspRectOriginal.y() + inspRectOriginal.height(); y += 10) {
                QPoint edgePoint = originalToDisplay(QPoint(measureX, y));
                painter.drawRect(edgePoint.x() - 1, edgePoint.y() - 1, 3, 3);
            }
        }
        
        // STRIP 검사 FRONT/REAR 박스 텍스트 표시
        if (result.insMethodTypes.value(patternId, -1) == static_cast<int>(InspectionMethod::STRIP)) {
            // 패턴 정보 가져오기 (활성화 상태 확인용)
            PatternInfo* pattern = getPatternById(patternId);
            
            // FRONT 박스 그리기 (STRIP 검사에서 FRONT 박스 데이터가 있는 경우)
            if (pattern && result.stripFrontBoxCenter.contains(patternId)) {
                
                // 상대좌표에서 절대좌표 계산
                QPointF frontBoxRelativeCenter = result.stripFrontBoxCenter.value(patternId);
                QSizeF frontBoxSize = result.stripFrontBoxSize.value(patternId);
                
                // INS 패턴 중심 좌표 계산
                QRectF inspRectOriginal;
                if (result.adjustedRects.contains(patternId)) {
                    inspRectOriginal = result.adjustedRects[patternId];
                } else {
                    inspRectOriginal = pattern->rect;
                }
                
                QPointF patternCenter = inspRectOriginal.center();
                
                // 상대좌표를 절대좌표로 변환 (검사 전과 동일한 방식)
                double patternAngle = result.parentAngles.value(patternId, 0.0);
                double angleRad = patternAngle * M_PI / 180.0;
                double cos_a = cos(angleRad);
                double sin_a = sin(angleRad);
                
                // 회전 적용된 FRONT 박스 중심점 계산
                QPointF frontBoxAbsoluteCenter = QPointF(
                    patternCenter.x() + frontBoxRelativeCenter.x() * cos_a - frontBoxRelativeCenter.y() * sin_a,
                    patternCenter.y() + frontBoxRelativeCenter.x() * sin_a + frontBoxRelativeCenter.y() * cos_a
                );
                
                // 화면 좌표로 변환
                QPoint frontBoxCenter = originalToDisplay(frontBoxAbsoluteCenter.toPoint());
                
                // FRONT 박스 그리기 (시안색 점선)
                qDebug() << "FRONT 박스 그리기: 중심" << frontBoxCenter << "각도" << patternAngle << "크기" << frontBoxSize;
                painter.save();
                painter.translate(frontBoxCenter);
                painter.rotate(patternAngle);
                
                QPen frontPen(QColor(0, 255, 255), 2);  // 시안색 점선
                frontPen.setStyle(Qt::DashLine);
                painter.setPen(frontPen);
                painter.setBrush(Qt::NoBrush);
                
                int displayBoxWidth = qRound(frontBoxSize.width() * zoomFactor);
                int displayBoxHeight = qRound(frontBoxSize.height() * zoomFactor);
                QRect frontBox(-displayBoxWidth/2, -displayBoxHeight/2, displayBoxWidth, displayBoxHeight);
                painter.drawRect(frontBox);
                qDebug() << "FRONT 박스 그려짐: 표시크기" << displayBoxWidth << "x" << displayBoxHeight;
                
                // FRONT 라벨 그리기
                int frontMin = result.stripMeasuredThicknessMin.value(patternId, 0);
                int frontMax = result.stripMeasuredThicknessMax.value(patternId, 0);
                bool frontPassed = (frontMin > 0 && frontMax > 0); // 간단한 통과 판정
                
                QString frontLabelText = QString("FRONT:%1~%2px")
                                       .arg(pattern->stripThicknessMin)
                                       .arg(pattern->stripThicknessMax);
                
                QFont frontLabelFont("Arial", 10, QFont::Bold);
                painter.setFont(frontLabelFont);
                QFontMetrics frontFm(frontLabelFont);
                int frontTextWidth = frontFm.horizontalAdvance(frontLabelText);
                int frontTextHeight = frontFm.height();
                
                // 배경 사각형 그리기 (시안색)
                QRect frontTextRect(-frontTextWidth/2 - 2, -displayBoxHeight/2 - frontTextHeight - 1, frontTextWidth + 4, frontTextHeight);
                painter.fillRect(frontTextRect, Qt::cyan);
                
                // 검은색 텍스트
                painter.setPen(Qt::black);
                painter.drawText(frontTextRect, Qt::AlignCenter | Qt::AlignVCenter, frontLabelText);
                
                painter.restore();
            }
            
            // REAR 박스 그리기 (STRIP 검사에서 REAR 박스 데이터가 있는 경우)
            if (pattern && result.stripRearBoxCenter.contains(patternId)) {
                
                // 상대좌표에서 절대좌표 계산
                QPointF rearBoxRelativeCenter = result.stripRearBoxCenter.value(patternId);
                QSizeF rearBoxSize = result.stripRearBoxSize.value(patternId);
                
                // INS 패턴 중심 좌표 계산
                QRectF inspRectOriginal;
                if (result.adjustedRects.contains(patternId)) {
                    inspRectOriginal = result.adjustedRects[patternId];
                } else {
                    inspRectOriginal = pattern->rect;
                }
                
                QPointF patternCenter = inspRectOriginal.center();
                
                // 상대좌표를 절대좌표로 변환 (검사 전과 동일한 방식)
                double patternAngle = result.parentAngles.value(patternId, 0.0);
                double angleRad = patternAngle * M_PI / 180.0;
                double cos_a = cos(angleRad);
                double sin_a = sin(angleRad);
                
                // 회전 적용된 REAR 박스 중심점 계산
                QPointF rearBoxAbsoluteCenter = QPointF(
                    patternCenter.x() + rearBoxRelativeCenter.x() * cos_a - rearBoxRelativeCenter.y() * sin_a,
                    patternCenter.y() + rearBoxRelativeCenter.x() * sin_a + rearBoxRelativeCenter.y() * cos_a
                );
                
                // 화면 좌표로 변환
                QPoint rearBoxCenter = originalToDisplay(rearBoxAbsoluteCenter.toPoint());
                
                // REAR 박스 그리기 (하늘색 점선)
                qDebug() << "REAR 박스 그리기: 중심" << rearBoxCenter << "각도" << patternAngle << "크기" << rearBoxSize;
                painter.save();
                painter.translate(rearBoxCenter);
                painter.rotate(patternAngle);
                
                QPen rearPen(QColor(0, 191, 255), 2);  // 하늘색 점선
                rearPen.setStyle(Qt::DashLine);
                painter.setPen(rearPen);
                painter.setBrush(Qt::NoBrush);
                
                int displayBoxWidth = qRound(rearBoxSize.width() * zoomFactor);
                int displayBoxHeight = qRound(rearBoxSize.height() * zoomFactor);
                QRect rearBox(-displayBoxWidth/2, -displayBoxHeight/2, displayBoxWidth, displayBoxHeight);
                painter.drawRect(rearBox);
                qDebug() << "REAR 박스 그려짐: 표시크기" << displayBoxWidth << "x" << displayBoxHeight;
                
                // REAR 라벨 그리기
                int rearMin = result.stripRearMeasuredThicknessMin.value(patternId, 0);
                int rearMax = result.stripRearMeasuredThicknessMax.value(patternId, 0);
                bool rearPassed = (rearMin > 0 && rearMax > 0); // 간단한 통과 판정
                
                QString rearLabelText = QString("REAR:%1~%2px")
                                      .arg(pattern->stripRearThicknessMin)
                                      .arg(pattern->stripRearThicknessMax);
                
                QFont rearLabelFont("Arial", 10, QFont::Bold);
                painter.setFont(rearLabelFont);
                QFontMetrics rearFm(rearLabelFont);
                int rearTextWidth = rearFm.horizontalAdvance(rearLabelText);
                int rearTextHeight = rearFm.height();
                
                // 배경 사각형 그리기 (하늘색)
                QRect rearTextRect(-rearTextWidth/2 - 2, -displayBoxHeight/2 - rearTextHeight - 1, rearTextWidth + 4, rearTextHeight);
                painter.fillRect(rearTextRect, QColor(0, 191, 255));
                
                // 검은색 텍스트
                painter.setPen(Qt::black);
                painter.drawText(rearTextRect, Qt::AlignCenter | Qt::AlignVCenter, rearLabelText);
                
                painter.restore();
            }
            
            // EDGE 박스 그리기 (EDGE 검사가 활성화된 경우에만)
            if (pattern && pattern->edgeEnabled && 
                result.edgeBoxCenter.contains(patternId) && result.edgeMeasured.value(patternId, false)) {
                
                // 상대좌표에서 절대좌표 계산
                QPointF edgeBoxRelativeCenter = result.edgeBoxCenter.value(patternId);
                QSizeF edgeBoxSize = result.edgeBoxSize.value(patternId);
                
                // INS 패턴 중심 좌표 계산
                QRectF inspRectOriginal;
                if (result.adjustedRects.contains(patternId)) {
                    inspRectOriginal = result.adjustedRects[patternId];
                } else {
                    inspRectOriginal = pattern->rect;
                }
                
                QPointF patternCenter = inspRectOriginal.center();
                
                // 상대좌표를 절대좌표로 변환 (검사 전과 동일한 방식)
                double patternAngle = result.parentAngles.value(patternId, 0.0);
                double angleRad = patternAngle * M_PI / 180.0;
                double cos_a = cos(angleRad);
                double sin_a = sin(angleRad);
                
                // 회전 적용된 EDGE 박스 중심점 계산
                QPointF edgeBoxAbsoluteCenter = QPointF(
                    patternCenter.x() + edgeBoxRelativeCenter.x() * cos_a - edgeBoxRelativeCenter.y() * sin_a,
                    patternCenter.y() + edgeBoxRelativeCenter.x() * sin_a + edgeBoxRelativeCenter.y() * cos_a
                );
                
                // 화면 좌표로 변환
                QPoint edgeBoxCenter = originalToDisplay(edgeBoxAbsoluteCenter.toPoint());
                
                // EDGE 박스 그리기 (주황색 점선)
                painter.save();
                painter.translate(edgeBoxCenter);
                painter.rotate(patternAngle);
                
                QPen edgePen(QColor(255, 128, 0), 2);  // 주황색 점선
                edgePen.setStyle(Qt::DashLine);
                painter.setPen(edgePen);
                painter.setBrush(Qt::NoBrush);
                
                int displayBoxWidth = qRound(edgeBoxSize.width() * zoomFactor);
                int displayBoxHeight = qRound(edgeBoxSize.height() * zoomFactor);
                QRect edgeBox(-displayBoxWidth/2, -displayBoxHeight/2, displayBoxWidth, displayBoxHeight);
                painter.drawRect(edgeBox);
                
                // EDGE 라벨 그리기
                bool edgePassed = result.edgeResults.value(patternId, false);
                int edgeIrregularityCount = result.edgeIrregularityCount.value(patternId, 0);
                
                QString edgeLabelText = QString("EDGE:/%1")
                                      .arg(pattern->edgeMaxIrregularities);
                
                QFont edgeLabelFont("Arial", 10, QFont::Bold);
                painter.setFont(edgeLabelFont);
                QFontMetrics edgeFm(edgeLabelFont);
                int edgeTextWidth = edgeFm.horizontalAdvance(edgeLabelText);
                int edgeTextHeight = edgeFm.height();
                
                // 배경 사각형 그리기 (주황색)
                QRect edgeTextRect(-edgeTextWidth/2 - 2, -displayBoxHeight/2 - edgeTextHeight - 1, edgeTextWidth + 4, edgeTextHeight);
                painter.fillRect(edgeTextRect, QColor(255, 128, 0));
                
                // 검은색 텍스트
                painter.setPen(Qt::black);
                painter.drawText(edgeTextRect, Qt::AlignCenter | Qt::AlignVCenter, edgeLabelText);
                
                painter.restore();
            }
        }
    }
    
    // PASS/FAIL 텍스트 추가 (카메라 정보 아래)
    QFont resultFont("Arial", 28, QFont::Bold);
    painter.setFont(resultFont);
    
    QString resultText = lastInspectionPassed ? "PASS" : "FAIL";
    QColor resultColor = lastInspectionPassed ? QColor(0, 255, 0) : QColor(255, 0, 0);
    
    // 텍스트 크기 정확히 측정
    QFontMetrics fm(resultFont);
    QRect textBounds = fm.boundingRect(resultText);
    
    // 배경 박스 크기 설정 (패딩 포함)
    int padding = 10;
    QRect resultTextRect(
        10,  // x 위치 (CAM 정보와 동일)
        60,  // y 위치 (CAM 정보 아래)
        textBounds.width() + padding * 2,  // 너비
        textBounds.height() + padding * 2  // 높이
    );
    
    // 배경을 CAM 정보와 같은 반투명 색상으로 변경
    painter.setBrush(QColor(0, 0, 0, 150));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(resultTextRect, 3, 3);
    
    // 벡터 스타일 텍스트 - 배경 박스 중앙에 정확히 위치
    QPainterPath textPath;
    QPoint textPosition(
        resultTextRect.x() + padding,
        resultTextRect.y() + padding + fm.ascent()  // ascent로 정확한 베이스라인 계산
    );
    textPath.addText(textPosition, resultFont, resultText);
    
    // 테두리 그리기
    painter.setPen(QPen(QColor(0, 0, 0), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(textPath);
    
    // 메인 텍스트 그리기
    painter.setPen(Qt::NoPen);
    painter.setBrush(resultColor);
    painter.drawPath(textPath);
    
    // **추가**: 검사 결과에서도 그룹 바운딩 박스 그리기
    // 검사 모드에서도 ROI를 먼저 그립니다 (티칭 모드와 동일하게 표시)
    for (const PatternInfo& pattern : patterns) {
        if (pattern.type == PatternType::ROI && pattern.enabled) {
            // **현재 카메라에 해당하는 ROI 패턴인지 확인**
            bool patternVisible = false;
            if (!currentCameraUuid.isEmpty()) {
                // 시뮬레이션 모드: 현재 시뮬레이션 카메라와 관련된 패턴만
                patternVisible = (pattern.cameraUuid == currentCameraUuid || 
                                pattern.cameraUuid.isEmpty());
            } else {
                // 일반 모드: 현재 카메라 UUID와 일치하는 패턴만
                // 안전 검사 추가
                if (!currentCameraUuid.isEmpty()) {
                    patternVisible = (pattern.cameraUuid == currentCameraUuid ||
                                    pattern.cameraUuid.isEmpty());
                } else {
                    // currentCameraUuid가 비어있으면 모든 패턴 표시하지 않음
                    patternVisible = false;
                }
            }
            
            if (!patternVisible) {
                continue; // 현재 카메라의 패턴이 아니면 건너뛰기
            }
            
            QRectF roiRect = pattern.rect;
            QPoint tl = originalToDisplay(roiRect.topLeft().toPoint());
            QPoint br = originalToDisplay(roiRect.bottomRight().toPoint());
            QRect drawRoi(tl, br);
            QPen roiPen(UIColors::ROI_COLOR, 2, Qt::SolidLine);  // 실선으로 변경
            painter.setPen(roiPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(drawRoi);
            
            // 라벨 (박스 위쪽에 배경색과 함께 표시) - 티칭 모드와 같은 폰트 크기
            painter.setFont(QFont("Arial", 10, QFont::Bold));
            QFontMetrics fm(painter.font());
            int textWidth = fm.horizontalAdvance(pattern.name);
            int textHeight = fm.height();
            
            // 배경 사각형
            QRect labelRect(drawRoi.topLeft().x() + 4, drawRoi.topLeft().y() - textHeight - 2, 
                           textWidth + 6, textHeight);
            QColor bgColor = UIColors::ROI_COLOR;
            bgColor.setAlpha(180);
            painter.fillRect(labelRect, bgColor);
            
            // 텍스트
            painter.setPen(UIColors::getTextColor(bgColor));
            painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, pattern.name);
        }
    }

    QMap<QUuid, QList<PatternInfo>> groupMap;
    
    // FID별로 그룹핑 (검사된 패턴들만)
    for (const PatternInfo& pattern : patterns) {
        // FID 패턴이고 검사 결과가 있는 경우
        if (pattern.type == PatternType::FID && result.fidResults.contains(pattern.id)) {
            QList<PatternInfo> groupPatterns;
            groupPatterns.append(pattern); // FID 자신 추가
            
            // 해당 FID의 자식 INS 패턴들 추가 (검사 결과가 있는 경우만)
            for (const PatternInfo& otherPattern : patterns) {
                if (otherPattern.type == PatternType::INS && 
                    otherPattern.parentId == pattern.id &&
                    result.insResults.contains(otherPattern.id)) {
                    // 검사된 실제 위치로 패턴 정보 업데이트
                    PatternInfo adjustedPattern = otherPattern;
                    if (result.adjustedRects.contains(otherPattern.id)) {
                        adjustedPattern.rect = result.adjustedRects[otherPattern.id];
                    }
                    groupPatterns.append(adjustedPattern);
                }
            }
            
            // 그룹에 2개 이상 패턴이 있으면 바운딩 박스 그리기
            if (groupPatterns.size() > 1) {
                // FID의 검사된 위치로 조정
                if (result.locations.contains(pattern.id)) {
                    cv::Point fidLoc = result.locations[pattern.id];
                    PatternInfo adjustedFidPattern = pattern;
                    adjustedFidPattern.rect = QRectF(
                        fidLoc.x - pattern.rect.width()/2,
                        fidLoc.y - pattern.rect.height()/2,
                        pattern.rect.width(),
                        pattern.rect.height()
                    );
                    groupPatterns[0] = adjustedFidPattern; // FID 위치 업데이트
                }
                
                groupMap[pattern.id] = groupPatterns;
            }
        }
    }
    
    // 각 그룹의 바운딩 박스 그리기 (선택된 FID 또는 아무것도 선택되지 않은 경우만)
    for (auto it = groupMap.begin(); it != groupMap.end(); ++it) {
        const QUuid& fidId = it.key();
        const QList<PatternInfo>& groupPatterns = it.value();
        
        bool shouldDrawGroup = false;
        if (selectedPatternId.isNull()) {
            // 아무것도 선택되지 않은 경우 (카메라뷰 클릭) - 모든 그룹 표시
            shouldDrawGroup = true;
        } else {
            // FID 패턴이 선택된 경우만 해당 그룹 표시
            PatternInfo* selectedPattern = getPatternById(selectedPatternId);
            if (selectedPattern && selectedPattern->type == PatternType::FID) {
                shouldDrawGroup = (selectedPattern->id == fidId);
            }
        }
        
        if (shouldDrawGroup) {
            drawGroupBoundingBox(painter, groupPatterns);
        }
    }
}

void CameraView::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 배경 이미지 그리기
    if (!backgroundPixmap.isNull()) {
        double safeZoom = zoomFactor;
        if (safeZoom < 0.01) safeZoom = 1.0;

        QSize origSize = backgroundPixmap.size();
        int scaledW = int(origSize.width() * safeZoom);
        int scaledH = int(origSize.height() * safeZoom);

        if (scaledW <= 0) scaledW = 1;
        if (scaledH <= 0) scaledH = 1;

        if (origSize.width() > 0 && origSize.height() > 0) {
            QPixmap scaledPixmap = backgroundPixmap.scaled(
                scaledW, scaledH,
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation
            );

            int x = (width() - scaledPixmap.width()) / 2 + panOffset.x();
            int y = (height() - scaledPixmap.height()) / 2 + panOffset.y();
            painter.drawPixmap(x, y, scaledPixmap);
        }
    } else {
        // 배경 이미지가 없을 때 상태에 따른 표시
        painter.fillRect(rect(), Qt::black);
        
        // 카메라 이름이 설정되지 않은 경우에만 "연결 없음" 표시
        if (currentCameraName.isEmpty()) {
            painter.setPen(Qt::white);
            QFont font = painter.font();
            font.setPointSize(16);
            font.setBold(true);
            painter.setFont(font);
            painter.drawText(rect(), Qt::AlignCenter, TR("NO_CONNECTION"));
        }
        // 카메라 이름이 있으면 (카메라 연결됨) 텍스트 표시 안 함 - 깔끔한 검은 화면
    }
    
    // 패턴 그리기 (티칭 모드에서만)
    // 검사 모드일 때는 기존 티칭 패턴을 그리지 않고, 검사 결과만 표시
    if (!isInspectionMode) {
        for (int i = 0; i < patterns.size(); ++i) {
            const PatternInfo& pattern = patterns[i];
            
            // 시뮬레이션 모드에서는 시뮬레이션 카메라 이름과 비교, 일반 모드에서는 currentCameraUuid와 비교
            bool patternVisible = false;
            if (!currentCameraUuid.isEmpty()) {
                // 시뮬레이션 모드: 현재 시뮬레이션 카메라와 관련된 패턴만 표시
                patternVisible = (pattern.cameraUuid == currentCameraUuid || 
                                pattern.cameraUuid.isEmpty());
            } else {
                // 일반 모드: 현재 카메라 UUID와 비교
                patternVisible = (currentCameraUuid.isEmpty() || 
                                pattern.cameraUuid == currentCameraUuid ||
                                pattern.cameraUuid.isEmpty());
            }
            
            if (!patternVisible) continue;
            if (!pattern.enabled) continue;

            QColor color = UIColors::getPatternColor(pattern.type);

            if (pattern.id == selectedPatternId) {
                QVector<QPoint> corners = getRotatedCorners();
                if (corners.size() == 4) {
                    QPolygon poly;
                    for (const QPoint& pt : corners) poly << pt;
                    painter.setPen(QPen(color, 3));
                    QColor fillColor = color; fillColor.setAlpha(40);
                    painter.setBrush(QBrush(fillColor));
                    painter.drawPolygon(poly);

                    // 패턴 이름 표시 - 회전된 상태에서 그리기
                    QFont font = painter.font();
                    font.setBold(true);
                    font.setPointSize(10);
                    painter.setFont(font);

                    QFontMetrics fm(font);
                    int textWidth = fm.horizontalAdvance(pattern.name);
                    int textHeight = fm.height();

                    // 패턴 중심점과 각도 계산
                    QPointF patternCenter = QPointF((corners[0] + corners[2]) / 2);
                    double patternAngle = pattern.angle;
                    
                    // 패턴 크기를 정확히 계산 (실제 픽셀 크기)
                    QRectF patternRect = pattern.rect;
                    int patternWidth = qRound(patternRect.width() * zoomFactor);
                    int patternHeight = qRound(patternRect.height() * zoomFactor);

                    // 회전된 상태에서 텍스트 그리기 (확대 비율 적용)
                    painter.save();
                    painter.translate(patternCenter);
                    painter.rotate(patternAngle);
                    
                    // INS 패턴의 실제 화면 크기 계산 (확대 비율 적용)
                    int displayPatternWidth = qRound(patternRect.width() * zoomFactor);
                    int displayPatternHeight = qRound(patternRect.height() * zoomFactor);
                    
                    // 패턴 박스 바깥쪽 왼쪽 상단에 텍스트 위치 (상대 좌표, 왼쪽 정렬)
                    QRect textRect(-displayPatternWidth/2, -displayPatternHeight/2 - textHeight - 2, textWidth + 6, textHeight);

                    QColor bgColor = color; // 패턴 타입별 색상 사용
                    bgColor.setAlpha(180);
                    painter.fillRect(textRect, bgColor);

                    painter.setPen(Qt::black);
                    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, pattern.name);
                    
                    painter.restore();
                    
                    // 선택된 INS 패턴에서 STRIP 검사 시에만 그라디언트 범위 표시
                    if (pattern.type == PatternType::INS && 
                        pattern.inspectionMethod == InspectionMethod::STRIP &&
                        pattern.id == selectedPatternId) {  // 선택된 패턴이어야 함
                        QPoint topLeft = corners[0];
                        QPoint topRight = corners[1];
                        QPoint bottomLeft = corners[3];
                        QPoint bottomRight = corners[2];
                        
                        // 가로 방향 벡터 계산
                        QPoint widthVector = topRight - topLeft;
                        
                        // 실제 패턴의 gradient 범위 퍼센트 값 사용
                        float startPercent = pattern.stripGradientStartPercent / 100.0f;
                        float endPercent = pattern.stripGradientEndPercent / 100.0f;
                        
                        // gradient 범위 지점 계산
                        QPoint posStartTop = topLeft + QPoint(widthVector.x() * startPercent, widthVector.y() * startPercent);
                        QPoint posStartBottom = bottomLeft + QPoint(widthVector.x() * startPercent, widthVector.y() * startPercent);
                        QPoint posEndTop = topLeft + QPoint(widthVector.x() * endPercent, widthVector.y() * endPercent);
                        QPoint posEndBottom = bottomLeft + QPoint(widthVector.x() * endPercent, widthVector.y() * endPercent);
                        
                        // 점선 스타일 설정
                        QPen dashPen(QColor(255, 255, 0), 2);  // 노란색 점선 (선택된 패턴용)
                        dashPen.setStyle(Qt::DashLine);
                        painter.setPen(dashPen);
                        
                        // 시작 지점 세로 점선 (위에서 아래로)
                        painter.drawLine(posStartTop, posStartBottom);
                        
                        // 끝 지점 세로 점선 (위에서 아래로)
                        painter.drawLine(posEndTop, posEndBottom);
                        
                        // 범위 텍스트 표시 (배경색 있는 스타일)
                        QFont rangeFont = font;
                        rangeFont.setPointSize(10);
                        rangeFont.setBold(true);
                        painter.setFont(rangeFont);
                        QFontMetrics rangeFm(rangeFont);
                        
                        // 시작점 텍스트 - 회전된 상태에서 그리기
                        QString startText = QString("%1%").arg(pattern.stripGradientStartPercent);
                        int startTextWidth = rangeFm.horizontalAdvance(startText);
                        int startTextHeight = rangeFm.height();
                        
                        painter.save();
                        painter.translate(posStartTop);
                        painter.rotate(pattern.angle);
                        QRect startTextRect(-startTextWidth/2 - 2, -startTextHeight - 5, startTextWidth + 4, startTextHeight);
                        painter.fillRect(startTextRect, Qt::yellow);
                        painter.setPen(Qt::black);
                        painter.drawText(startTextRect, Qt::AlignCenter, startText);
                        painter.restore();
                        
                        // 끝점 텍스트 - 회전된 상태에서 그리기
                        QString endText = QString("%1%").arg(pattern.stripGradientEndPercent);
                        int endTextWidth = rangeFm.horizontalAdvance(endText);
                        int endTextHeight = rangeFm.height();
                        
                        painter.save();
                        painter.translate(posEndTop);
                        painter.rotate(pattern.angle);
                        QRect endTextRect(-endTextWidth/2 - 2, -endTextHeight - 5, endTextWidth + 4, endTextHeight);
                        painter.fillRect(endTextRect, Qt::yellow);
                        painter.setPen(Qt::black);
                        painter.drawText(endTextRect, Qt::AlignCenter, endText);
                        painter.restore();
                        
                        // 검사박스는 검사전 모드에서만 표시 (검사후에는 drawInspectionResultsVector에서 표시)
                        if (!isInspectionMode) {
                            // REAR 검사 영역 사각형 (80% 지점, FRONT와 같은 회전 방식)
                            // gradient end 지점의 중심점 계산 (패턴 각도에 따른 실제 80% 위치)
                            QPointF rearPatternCenterOrig = pattern.rect.center();
                            float rearPatternWidth = pattern.rect.width();
                            float rearActualAngle = pattern.angle;
                        
                        QPointF gradientEndCenterOrig;
                        if (std::abs(rearActualAngle) < 0.1) {
                            // 각도가 거의 0인 경우
                            gradientEndCenterOrig = QPointF(
                                rearPatternCenterOrig.x() - rearPatternWidth/2.0 + rearPatternWidth * pattern.stripGradientEndPercent / 100.0,
                                rearPatternCenterOrig.y()
                            );
                        } else {
                            // 회전된 패턴의 경우 중심축을 따라 80% 지점 계산
                            double angleRad = rearActualAngle * M_PI / 180.0;
                            
                            // 패턴의 너비/2만큼 이동한 후 80% 지점 계산
                            double halfWidth = rearPatternWidth / 2.0;
                            double endDistance = halfWidth * (2.0 * pattern.stripGradientEndPercent / 100.0 - 1.0);
                            
                            gradientEndCenterOrig = QPointF(
                                rearPatternCenterOrig.x() + endDistance * std::cos(angleRad),
                                rearPatternCenterOrig.y() + endDistance * std::sin(angleRad)
                            );
                        }
                        
                        // 화면 좌표로 변환된 중심점
                        QPoint rearBoxCenter = originalToDisplay(gradientEndCenterOrig.toPoint());
                        
                        // 사각형 크기도 originalToDisplay와 동일한 zoomFactor 적용
                        int rearBoxWidth = qRound(pattern.stripRearThicknessBoxWidth * zoomFactor);
                        int rearBoxHeight = qRound(pattern.stripRearThicknessBoxHeight * zoomFactor);
                        
                        // painter의 변환 매트릭스를 사용해 회전된 REAR 사각형 그리기
                        painter.save();
                        painter.translate(rearBoxCenter);
                        painter.rotate(rearActualAngle);
                        
                        QPen rearAreaPen(QColor(0, 191, 255), 2);  // 하늘색 점선 (FRONT와 동일)
                        rearAreaPen.setStyle(Qt::DashLine);
                        painter.setPen(rearAreaPen);
                        painter.setBrush(Qt::NoBrush);
                        
                        QRect rearThicknessBox(-rearBoxWidth/2, -rearBoxHeight/2, rearBoxWidth, rearBoxHeight);
                        painter.drawRect(rearThicknessBox);
                        
                        // REAR 라벨 표시 (검사 박스 위에) - 회전된 상태에서 그리기
                        QString rearLabelText = QString("REAR:%1~%2px")
                                              .arg(pattern.stripRearThicknessMin)
                                              .arg(pattern.stripRearThicknessMax);
                        
                        // 텍스트 크기 계산
                        QFont rearLabelFont("Arial", 10, QFont::Bold);
                        painter.setFont(rearLabelFont);
                        QFontMetrics rearFm(rearLabelFont);
                        int rearTextWidth = rearFm.horizontalAdvance(rearLabelText);
                        int rearTextHeight = rearFm.height();
                        
                        // 배경 사각형 그리기 (하늘색) - 가로는 중심, 세로는 위쪽
                        QRect rearTextRect(-rearTextWidth/2 - 2, -rearBoxHeight/2 - rearTextHeight - 1, rearTextWidth + 4, rearTextHeight);
                        painter.fillRect(rearTextRect, QColor(0, 191, 255));
                        
                        // 검은색 텍스트 (중앙 정렬)
                        painter.setPen(Qt::black);
                        painter.drawText(rearTextRect, Qt::AlignCenter | Qt::AlignVCenter, rearLabelText);
                        
                        painter.restore();
                        
                        } // REAR 검사박스 그리기 끝 (!isInspectionMode)
                        
                        // FRONT 검사박스는 검사전 모드에서만 표시 (검사후에는 drawInspectionResultsVector에서 표시)
                        if (!isInspectionMode) {
                            // 두께 범위 점선 사각형 그리기 (gradient 시작 선의 중앙에서)
                            // 패턴 각도를 반영한 실제 좌표 계산
                            
                            // 검사전 모드에서는 기존 패턴 좌표 사용
                            QPointF patternCenterOrig = QPointF(pattern.rect.center().x(), pattern.rect.center().y());
                            float patternWidth = pattern.rect.width();
                            float patternHeight = pattern.rect.height();
                            float actualAngle = pattern.angle;
                        
                        if (isInspectionMode && hasInspectionResult && lastInspectionResult.adjustedRects.contains(pattern.id)) {
                            // 검사 결과의 실제 좌표와 각도 사용
                            QRectF adjustedRect = lastInspectionResult.adjustedRects[pattern.id];
                            patternCenterOrig = adjustedRect.center();
                            patternWidth = adjustedRect.width();
                            patternHeight = adjustedRect.height();
                            actualAngle = lastInspectionResult.angles.value(pattern.id, pattern.angle);
                        } else {
                            // 검사전 모드에서는 기존 패턴 좌표 사용
                            patternCenterOrig = QPointF(pattern.rect.center().x(), pattern.rect.center().y());
                            patternWidth = pattern.rect.width();
                            patternHeight = pattern.rect.height();
                            actualAngle = pattern.angle;
                        }
                        
                        // 패턴 각도 (라디안)
                        float angleRad = actualAngle * M_PI / 180.0f;
                        float cosAngle = cos(angleRad);
                        float sinAngle = sin(angleRad);
                        
                        // 회전된 패턴의 gradient 시작 위치 계산
                        // gradient 시작점: 패턴 왼쪽 끝에서 startPercent만큼 이동한 지점
                        float gradientOffsetX = (startPercent - 0.5f) * patternWidth;  // 중심 기준 오프셋
                        
                        // 회전 적용된 gradient 시작점 중앙 계산
                        QPointF gradientStartCenterOrig = QPointF(
                            patternCenterOrig.x() + gradientOffsetX * cosAngle,
                            patternCenterOrig.y() + gradientOffsetX * sinAngle
                        );
                        
                        // 두께 범위 점선 사각형 그리기 (패턴 각도에 맞춰 회전)
                        QPen thicknessPen(QColor(0, 255, 255), 2);  // 시안색 점선
                        thicknessPen.setStyle(Qt::DashLine);
                        painter.setPen(thicknessPen);
                        
                        // 화면 좌표로 변환된 중심점
                        QPoint boxCenter = originalToDisplay(gradientStartCenterOrig.toPoint());
                        
                        // 사각형 크기도 originalToDisplay와 동일한 zoomFactor 적용
                        int boxWidth = qRound(pattern.stripThicknessBoxWidth * zoomFactor);
                        int boxHeight = qRound(pattern.stripThicknessBoxHeight * zoomFactor);
                        
                        // painter의 변환 매트릭스를 사용해 회전된 사각형 그리기
                        painter.save();
                        painter.translate(boxCenter);
                        painter.rotate(actualAngle);
                        
                        QRect thicknessBox(-boxWidth/2, -boxHeight/2, boxWidth, boxHeight);
                        painter.drawRect(thicknessBox);
                        
                        // FRONT 라벨 표시 (검사 박스 위에) - 회전된 상태에서 그리기
                        QString frontLabelText = QString("FRONT:%1~%2px")
                                               .arg(pattern.stripThicknessMin)
                                               .arg(pattern.stripThicknessMax);
                        
                        // 텍스트 크기 계산
                        QFont frontLabelFont("Arial", 10, QFont::Bold);
                        painter.setFont(frontLabelFont);
                        QFontMetrics frontFm(frontLabelFont);
                        int frontTextWidth = frontFm.horizontalAdvance(frontLabelText);
                        int frontTextHeight = frontFm.height();
                        
                        // 배경 사각형 그리기 (시안색) - 가로는 중심, 세로는 위쪽
                        QRect frontTextRect(-frontTextWidth/2 - 2, -boxHeight/2 - frontTextHeight - 1, frontTextWidth + 4, frontTextHeight);
                        painter.fillRect(frontTextRect, Qt::cyan);
                        
                        // 검은색 텍스트 (중앙 정렬)
                        painter.setPen(Qt::black);
                        painter.drawText(frontTextRect, Qt::AlignCenter | Qt::AlignVCenter, frontLabelText);
                        
                        painter.restore();
                        
                        } 
                    }
                    
                    // EDGE 검사 박스 그리기 (INS 패턴의 STRIP 검사에서만, 검사전 모드에서만)
                    if (!isInspectionMode && pattern.type == PatternType::INS && 
                        pattern.inspectionMethod == InspectionMethod::STRIP &&
                        pattern.edgeEnabled) {
                        
                        // 검사전 모드에서는 기존 패턴 좌표 사용
                        QPointF edgePatternCenterOrig = QPointF(pattern.rect.center().x(), pattern.rect.center().y());
                        float edgePatternWidth = pattern.rect.width();
                        float edgeActualAngle = pattern.angle;
                        
                        // 패턴 각도 (라디안)
                        float angleRad = edgeActualAngle * M_PI / 180.0f;
                        float cosAngle = cos(angleRad);
                        float sinAngle = sin(angleRad);
                        
                        // 패턴 왼쪽에서 edgeOffsetX만큼 떨어진 위치 계산
                        float edgeOffsetX = (-edgePatternWidth/2.0f) + pattern.edgeOffsetX; // 패턴 중심 기준 오프셋
                        
                        // 회전 적용된 EDGE 검사 중심점 계산
                        QPointF edgeCenterOrig = QPointF(
                            edgePatternCenterOrig.x() + edgeOffsetX * cosAngle,
                            edgePatternCenterOrig.y() + edgeOffsetX * sinAngle
                        );
                        
                        // 화면 좌표로 변환된 중심점
                        QPoint edgeBoxCenter = originalToDisplay(edgeCenterOrig.toPoint());
                        
                        // 사각형 크기도 zoomFactor 적용
                        int edgeBoxWidth = qRound(pattern.edgeBoxWidth * zoomFactor);
                        int edgeBoxHeight = qRound(pattern.edgeBoxHeight * zoomFactor);
                        
                        // painter의 변환 매트릭스를 사용해 회전된 EDGE 사각형 그리기
                        painter.save();
                        painter.translate(edgeBoxCenter);
                        painter.rotate(edgeActualAngle);
                        
                        QPen edgePen(QColor(255, 128, 0), 2);  // 주황색 점선 (다른 검사와 구분)
                        edgePen.setStyle(Qt::DashLine);
                        painter.setPen(edgePen);
                        painter.setBrush(Qt::NoBrush);
                        
                        QRect edgeBox(-edgeBoxWidth/2, -edgeBoxHeight/2, edgeBoxWidth, edgeBoxHeight);
                        painter.drawRect(edgeBox);
                        
                        // EDGE 라벨 표시 (검사 박스 위에) - 회전된 상태에서 그리기
                        QString edgeLabelText = QString("EDGE:/%1")
                                              .arg(pattern.edgeMaxIrregularities);
                        
                        // 텍스트 크기 계산
                        QFont edgeLabelFont("Arial", 10, QFont::Bold);
                        painter.setFont(edgeLabelFont);
                        QFontMetrics edgeFm(edgeLabelFont);
                        int edgeTextWidth = edgeFm.horizontalAdvance(edgeLabelText);
                        int edgeTextHeight = edgeFm.height();
                        
                        // 배경 사각형 그리기 (주황색) - 가로는 중심, 세로는 위쪽
                        QRect edgeTextRect(-edgeTextWidth/2 - 2, -edgeBoxHeight/2 - edgeTextHeight - 1, edgeTextWidth + 4, edgeTextHeight);
                        painter.fillRect(edgeTextRect, QColor(255, 128, 0));
                        
                        // 검은색 텍스트 (중앙 정렬)
                        painter.setPen(Qt::black);
                        painter.drawText(edgeTextRect, Qt::AlignCenter | Qt::AlignVCenter, edgeLabelText);
                        
                        painter.restore();
                        
                        // EDGE 검사 텍스트는 라벨로 이동되어 제거됨
                    }
                }
                // 선택된 패턴은 회전된 폴리곤만 그리고, drawRect(displayRect)는 그리지 않음!
            } else {
                // 선택되지 않은 패턴도 회전된 상태로 그리기
                QVector<QPoint> corners = getRotatedCornersForPattern(pattern);
                
                if (corners.size() == 4) {
                    QPolygon poly;
                    for (const QPoint& pt : corners) poly << pt;
                    painter.setPen(QPen(color, 2));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawPolygon(poly);
                } else {
                    // 폴백: 기존 방식으로 사각형 그리기
                    QPoint topLeft = originalToDisplay(QPoint(pattern.rect.x(), pattern.rect.y()));
                    QPoint bottomRight = originalToDisplay(QPoint(pattern.rect.x() + pattern.rect.width(), 
                                                        pattern.rect.y() + pattern.rect.height()));
                    QRect displayRect(topLeft, bottomRight);
                    painter.setPen(QPen(color, 2));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRect(displayRect);
                }

                // 패턴 이름과 각도 표시
                QFont font = painter.font();
                font.setBold(true);
                font.setPointSize(10);
                painter.setFont(font);

                // 패턴 이름에 각도 정보 추가
                QString displayName = pattern.name;
                if (pattern.angle != 0.0) {
                    displayName += QString(" A:%1°").arg(pattern.angle, 0, 'f', 1);
                }

                QFontMetrics fm(font);
                int textWidth = fm.horizontalAdvance(displayName);
                int textHeight = fm.height();

                // 검사후 모드에서는 검사 결과의 실제 좌표와 각도 사용
                QPoint center;
                double patternAngle;
                
                if (isInspectionMode && hasInspectionResult && lastInspectionResult.adjustedRects.contains(pattern.id)) {
                    // 검사 결과의 실제 중심점과 각도 사용
                    QRectF adjustedRect = lastInspectionResult.adjustedRects[pattern.id];
                    center = originalToDisplay(adjustedRect.center().toPoint());
                    patternAngle = lastInspectionResult.angles.value(pattern.id, pattern.angle);
                } else {
                    // 검사전 모드에서는 기존 패턴 좌표 사용
                    center = originalToDisplay(QPoint(pattern.rect.x() + pattern.rect.width()/2, 
                                                     pattern.rect.y() + pattern.rect.height()/2));
                    patternAngle = pattern.angle;
                }
                
                // 패턴 크기를 디스플레이 좌표로 변환
                int displayWidth = pattern.rect.width() * zoomFactor;
                int displayHeight = pattern.rect.height() * zoomFactor;

                // 회전된 상태에서 텍스트 그리기 (검사전 모드와 동일)
                painter.save();
                painter.translate(center);
                painter.rotate(patternAngle);
                
                // 패턴 위쪽 왼쪽에 텍스트 위치 (상대 좌표, 왼쪽 정렬)
                QRect textRect(-displayWidth/2, -displayHeight/2 - textHeight - 2, textWidth + 6, textHeight);

                QColor bgColor = color; // 패턴 타입별 색상 사용
                bgColor.setAlpha(180);
                painter.fillRect(textRect, bgColor);

                painter.setPen(Qt::black);
                painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, displayName);
                
                painter.restore();
                
                // 선택되지 않은 INS 패턴에는 점선을 그리지 않음 (선택된 패턴에만 표시)
            }
        }
    }  // 티칭 모드 패턴 그리기 루프 끝
    
    // 그룹 바운딩 박스 그리기 (티칭 모드에서만)
    if (!isInspectionMode) {
        // 각 FID 패턴과 그 자식들의 그룹 바운딩 박스 그리기
        QMap<QUuid, QList<PatternInfo>> groups;
        
        // FID 패턴들을 기준으로 그룹 생성
        for (const PatternInfo& pattern : patterns) {
            if (!pattern.enabled) continue;
            
            // 시뮬레이션/일반 모드 가시성 체크
            bool patternVisible = false;
            if (!currentCameraUuid.isEmpty()) {
                // 시뮬레이션 모드: 모든 패턴을 표시
                patternVisible = true;
            } else {
                patternVisible = (currentCameraUuid.isEmpty() || 
                                pattern.cameraUuid == currentCameraUuid ||
                                pattern.cameraUuid.isEmpty());
            }
            if (!patternVisible) continue;
            
            if (pattern.type == PatternType::FID && !pattern.childIds.isEmpty()) {
                QList<PatternInfo> groupPatterns;
                groupPatterns.append(pattern);  // FID 자신 추가
                
                // FID의 자식 패턴들(INS) 추가
                for (const QUuid& childId : pattern.childIds) {
                    for (const PatternInfo& childPattern : patterns) {
                        if (childPattern.id == childId && childPattern.enabled) {
                            // 자식 패턴도 같은 가시성 체크
                            bool childVisible = false;
                            if (!currentCameraUuid.isEmpty()) {
                                childVisible = (childPattern.cameraUuid == currentCameraUuid);
                            } else {
                                childVisible = (currentCameraUuid.isEmpty() || childPattern.cameraUuid == currentCameraUuid);
                            }
                            if (childVisible) {
                                groupPatterns.append(childPattern);
                            }
                        }
                    }
                }
                
                // 그룹에 2개 이상의 패턴이 있으면 바운딩 박스 그리기
                // 단, FID가 선택된 경우나 아무것도 선택되지 않은 경우(카메라뷰 클릭)만
                if (groupPatterns.size() >= 2) {
                    bool shouldDrawGroup = false;
                    if (selectedPatternId.isNull()) {
                        // 아무것도 선택되지 않은 경우 (카메라뷰 클릭) - 모든 그룹 표시
                        shouldDrawGroup = true;
                    } else {
                        // FID 패턴이 선택된 경우만 해당 그룹 표시
                        PatternInfo* selectedPattern = getPatternById(selectedPatternId);
                        if (selectedPattern && selectedPattern->type == PatternType::FID) {
                            shouldDrawGroup = (selectedPattern->id == pattern.id);
                        }
                    }
                    
                    if (shouldDrawGroup) {
                        drawGroupBoundingBox(painter, groupPatterns);
                    }
                }
            }
        }
    }

        // 현재 그리고 있는 사각형 (검사 모드가 아니고 캘리브레이션 모드가 아닐 때만)
        if (!isInspectionMode && !currentRect.isNull() && !isDrawing && !m_calibrationMode) {
            QPoint topLeft = originalToDisplay(QPoint(currentRect.x(), currentRect.y()));
            QPoint bottomRight = originalToDisplay(QPoint(currentRect.x() + currentRect.width(), 
                                                currentRect.y() + currentRect.height()));
            QRect displayRect(topLeft, bottomRight);

            painter.setPen(QPen(currentDrawColor, 2, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(displayRect);
        }

        // 선택된 패턴의 리사이즈/회전 핸들 (티칭 모드에서만)
        if (!isInspectionMode && !selectedPatternId.isNull()) {
            PatternInfo* pattern = getPatternById(selectedPatternId);
            if (pattern && pattern->enabled) {
                // 시뮬레이션 모드에서는 currentCameraUuid과 비교, 일반 모드에서는 currentCameraUuid와 비교
                bool patternVisible = false;
                if (!currentCameraUuid.isEmpty()) {
                    // 시뮬레이션 모드: currentCameraUuid (CAM(...)) 형태와 비교
                    patternVisible = (pattern->cameraUuid == currentCameraUuid);
                } else {
                    // 일반 모드: currentCameraUuid와 비교
                    patternVisible = (currentCameraUuid.isEmpty() || pattern->cameraUuid == currentCameraUuid);
                }
                
                if (patternVisible) {
                    // FID 패턴이고 그룹화된 경우 그룹 전체 바운딩 박스 사용
                    QRectF handleRect;
                    if (pattern->type == PatternType::FID) {
                        // 그룹화된 모든 패턴들 (FID + INS) 찾기
                        QList<PatternInfo*> groupPatterns;
                        groupPatterns.append(pattern); // FID 패턴 추가
                        
                        for (int i = 0; i < patterns.size(); i++) {
                            PatternInfo& insPattern = patterns[i];
                            if (insPattern.parentId == selectedPatternId && insPattern.type == PatternType::INS) {
                                groupPatterns.append(&insPattern);
                            }
                        }
                        
                        // 그룹화된 패턴이 있으면 그룹 전체 바운딩 박스 사용
                        if (groupPatterns.size() > 1) {
                            handleRect = groupPatterns[0]->rect;
                            for (int i = 1; i < groupPatterns.size(); i++) {
                                handleRect = handleRect.united(groupPatterns[i]->rect);
                            }
                        } else {
                            handleRect = pattern->rect;
                        }
                    } else {
                        handleRect = pattern->rect;
                    }
                    
                    QPoint topLeft = originalToDisplay(QPoint(qRound(handleRect.left()), qRound(handleRect.top())));
                    QPoint bottomRight = originalToDisplay(QPoint(qRound(handleRect.right()), qRound(handleRect.bottom())));
                    QRect displayRect(topLeft, bottomRight);

                    drawResizeHandles(painter, displayRect);
                }
            }
        }

    // 캘리브레이션 모드일 때 안내 텍스트 표시
    if (m_calibrationMode) {
        painter.setPen(QPen(Qt::white, 2));
        painter.setFont(QFont("Arial", 14, QFont::Bold));
        
        QString calibText = m_calibrationText.isEmpty() ? "CALIBRATION_IN_PROGRESS" : m_calibrationText;
        QRect textRect = painter.fontMetrics().boundingRect(calibText);
        textRect.adjust(-10, -5, 10, 5);
        textRect.moveCenter(QPoint(width()/2, 50));
        
        painter.fillRect(textRect, QBrush(QColor(255, 0, 0, 150)));
        painter.drawText(textRect, Qt::AlignCenter, calibText);
    }
    
    // 캘리브레이션 사각형 그리기 (캘리브레이션 완료 후)
    const CalibrationInfo& calibInfo = m_calibrationInfo;
    if (calibInfo.isCalibrated && !calibInfo.calibrationRect.isNull()) {
        QPoint topLeft = originalToDisplay(QPoint(calibInfo.calibrationRect.left(), calibInfo.calibrationRect.top()));
        QPoint bottomRight = originalToDisplay(QPoint(calibInfo.calibrationRect.right(), calibInfo.calibrationRect.bottom()));
        QRect displayRect(topLeft, bottomRight);
        
        painter.setPen(QPen(Qt::blue, 2, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(displayRect);
        
        QString calibInfoText = QString("Calibrated: %1 mm/pixel")
                                .arg(calibInfo.pixelToMmRatio, 0, 'f', 4);
        
        QFont infoFont = painter.font();
        infoFont.setPointSize(10);
        painter.setFont(infoFont);
        painter.setPen(Qt::blue);
        
        QRect infoRect = painter.fontMetrics().boundingRect(calibInfoText);
        infoRect.adjust(-5, -2, 5, 2);
        infoRect.moveTopLeft(QPoint(topLeft.x(), topLeft.y() - infoRect.height() - 5));
        
        painter.fillRect(infoRect, QBrush(QColor(255, 255, 255, 200)));
        painter.drawText(infoRect, Qt::AlignCenter, calibInfoText);
    }

    // 물리적 길이 측정 표시 (일반 사각형에)
    if (!currentRect.isNull() && !isDrawing && !m_calibrationMode && calibInfo.isCalibrated) {
        double physicalWidth = currentRect.width() * calibInfo.pixelToMmRatio;
        double physicalHeight = currentRect.height() * calibInfo.pixelToMmRatio;
        
        QString sizeText = QString("W: %1mm, H: %2mm")
                          .arg(physicalWidth, 0, 'f', 1)
                          .arg(physicalHeight, 0, 'f', 1);
        
        QPoint topLeft = originalToDisplay(QPoint(currentRect.left(), currentRect.top()));
        
        painter.setPen(Qt::yellow);
        painter.setFont(QFont("Arial", 10, QFont::Bold));
        
        QRect sizeRect = painter.fontMetrics().boundingRect(sizeText);
        sizeRect.adjust(-3, -2, 3, 2);
        sizeRect.moveTopLeft(QPoint(topLeft.x(), topLeft.y() - sizeRect.height() - 25));
        
        painter.fillRect(sizeRect, QBrush(QColor(0, 0, 0, 180)));
        painter.drawText(sizeRect, Qt::AlignCenter, sizeText);
    }
    
    // 카메라 정보 표시 (한 번만)
    if (!currentCameraUuid.isEmpty()) {
        QFont idFont = painter.font();
        idFont.setBold(true);
        idFont.setPointSize(10);
        painter.setFont(idFont);
        
        QString cameraIdText;
        if (!currentCameraUuid.isEmpty()) {
            QString recipeName = currentCameraUuid;
            cameraIdText = recipeName;
        } else {
            cameraIdText = QString("CAM(%1)").arg(currentCameraUuid);
        }
        QRect idTextRect = painter.fontMetrics().boundingRect(cameraIdText);
        idTextRect.adjust(-5, -5, 5, 5);
        
        int yPos = 10;
        if (!statusInfo.isEmpty()) {
            yPos = 40;
        }
        
        painter.setBrush(QColor(0, 0, 0, 150));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRect(10, yPos, idTextRect.width() + 10, idTextRect.height()), 3, 3);
        
        painter.setPen(Qt::white);
        painter.drawText(QRect(15, yPos, idTextRect.width(), idTextRect.height()), 
                        Qt::AlignLeft | Qt::AlignVCenter, cameraIdText);

        if (isInspectionMode && hasInspectionResult) {
            drawInspectionResultsVector(painter, lastInspectionResult);
        }
    }
    
    // 드래그 중인 사각형 (실시간 mm 표시 포함)
    if (!currentRect.isNull() && (isDrawing || m_calibrationMode)) {
        QPen pen(m_calibrationMode ? Qt::red : currentDrawColor, 2);
        pen.setStyle(Qt::SolidLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        
        QPoint topLeft = originalToDisplay(QPoint(currentRect.left(), currentRect.top()));
        QPoint bottomRight = originalToDisplay(QPoint(currentRect.right(), currentRect.bottom()));
        QRect displayRect(topLeft, bottomRight);
        
        painter.drawRect(displayRect);
        
        const CalibrationInfo& calibInfo = m_calibrationInfo;
        if (isDrawing && !m_calibrationMode && calibInfo.isCalibrated) {
            double physicalWidth = currentRect.width() * calibInfo.pixelToMmRatio;
            double physicalHeight = currentRect.height() * calibInfo.pixelToMmRatio;
            
            painter.setPen(Qt::cyan);
            painter.setFont(QFont("Arial", 10, QFont::Bold));
            
            QString widthText = QString("%1mm").arg(physicalWidth, 0, 'f', 1);
            QRect widthRect = painter.fontMetrics().boundingRect(widthText);
            widthRect.adjust(-3, -2, 3, 2);
            int widthX = (topLeft.x() + bottomRight.x()) / 2 - widthRect.width() / 2;
            int widthY = topLeft.y() - widthRect.height() - 5;
            
            if (widthY >= 0) {
                widthRect.moveTopLeft(QPoint(widthX, widthY));
                painter.fillRect(widthRect, QBrush(QColor(0, 100, 100, 150)));
                painter.drawText(widthRect, Qt::AlignCenter, widthText);
            }
            
            QString heightText = QString("%1mm").arg(physicalHeight, 0, 'f', 1);
            QRect heightRect = painter.fontMetrics().boundingRect(heightText);
            heightRect.adjust(-3, -2, 3, 2);
            int heightX = topLeft.x() - heightRect.width() - 5;
            int heightY = (topLeft.y() + bottomRight.y()) / 2 - heightRect.height() / 2;
            
            if (heightX >= 0) {
                heightRect.moveTopLeft(QPoint(heightX, heightY));
                painter.fillRect(heightRect, QBrush(QColor(0, 100, 100, 150)));
                painter.drawText(heightRect, Qt::AlignCenter, heightText);
            }
        }
    }
}

void CameraView::wheelEvent(QWheelEvent* event) {
    if (backgroundPixmap.isNull()) {
        event->accept();
        return;
    }

    double oldZoom = zoomFactor;
    double zoomStep = 0.1;
    int delta = event->angleDelta().y();
    if (delta > 0)
        zoomFactor += zoomStep;
    else
        zoomFactor -= zoomStep;

    if (zoomFactor < 0.2) zoomFactor = 0.2;
    if (zoomFactor > 5.0) zoomFactor = 5.0;

    if (zoomFactor != oldZoom) {
        QPoint mousePos = event->position().toPoint();
        QSize origSize = backgroundPixmap.size();

        int oldScaledW = int(origSize.width() * oldZoom);
        int oldScaledH = int(origSize.height() * oldZoom);
        int oldImgX = mousePos.x() - ((width() - oldScaledW) / 2 + panOffset.x());
        int oldImgY = mousePos.y() - ((height() - oldScaledH) / 2 + panOffset.y());

        double relX = (oldScaledW > 0) ? double(oldImgX) / oldScaledW : 0.5;
        double relY = (oldScaledH > 0) ? double(oldImgY) / oldScaledH : 0.5;

        int newScaledW = int(origSize.width() * zoomFactor);
        int newScaledH = int(origSize.height() * zoomFactor);
        int newImgX = int(relX * newScaledW);
        int newImgY = int(relY * newScaledH);

        // ★ 제한 없이 바로 적용 ★
        int newPanX = mousePos.x() - (width() - newScaledW) / 2 - newImgX;
        int newPanY = mousePos.y() - (height() - newScaledH) / 2 - newImgY;
        panOffset.setX(newPanX);
        panOffset.setY(newPanY);
    }

    update();
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
    
    // 기존 pixmap을 복사하여 스케일링
    QPixmap originalPixmap = backgroundPixmap;
    
    // 원본 이미지를 직접 화면에 표시 (줌 팩터가 1이거나 그에 가까우면)
    if (qAbs(zoomFactor - 1.0) < 0.05) {
        QLabel::setPixmap(originalPixmap);
        update();
        return;
    }
    
    // 단순한 스케일링으로 구현 (뷰포트 계산 방식 대신)
    QSize viewSize = size();
    QSize pixSize = originalPixmap.size();
    
    // 줌이 적용된 이미지 크기 계산
    double scaleFactor = 1.0;
    
    // 비율 유지하며 화면에 맞추기
    if (pixSize.width() > 0 && pixSize.height() > 0) {
        if (viewSize.width() * pixSize.height() > viewSize.height() * pixSize.width()) {
            // 높이에 맞추기
            scaleFactor = (double)viewSize.height() / pixSize.height();
        } else {
            // 너비에 맞추기
            scaleFactor = (double)viewSize.width() / pixSize.width();
        }
    }
    
    // 줌 팩터 적용
    scaleFactor *= zoomFactor;
    
    // 최종 이미지 크기
    int newWidth = qRound(pixSize.width() * scaleFactor);
    int newHeight = qRound(pixSize.height() * scaleFactor);
    
    // 이미지가 너무 크지 않도록 제한
    if (newWidth > 5000 || newHeight > 5000) {
        double limitFactor = qMin(5000.0 / newWidth, 5000.0 / newHeight);
        newWidth = qRound(newWidth * limitFactor);
        newHeight = qRound(newHeight * limitFactor);
    }
    
    // 크기가 유효한지 확인
    if (newWidth <= 0 || newHeight <= 0) {
        return;
    }
    
    // 스케일된 이미지 생성
    QPixmap scaledPixmap;
    
    try {
        scaledPixmap = originalPixmap.scaled(
            newWidth, newHeight,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        );
    } catch (...) {
        qWarning() << "Failed to scale image to" << newWidth << "x" << newHeight;
        return;
    }
    
    // 스케일링 성공했는지 확인
    if (scaledPixmap.isNull()) {
        return;
    }
    
    // 픽스맵 설정
    QLabel::setPixmap(scaledPixmap);
}

void CameraView::setBackgroundPixmap(const QPixmap &pixmap) {
    // 빈 픽스맵(카메라 OFF)인 경우 텍스트 표시
    if (pixmap.isNull()) {
        backgroundPixmap = QPixmap();
        originalImageSize = QSize();
        setText(TR("NO_CONNECTION"));
        update();
        return;
    }
    
    // 이미지가 있는 경우 텍스트 지우기
    setText("");
    
    // 처음 이미지가 설정될 때만 초기화 적용
    bool isFirstLoad = backgroundPixmap.isNull();
    
    backgroundPixmap = pixmap;
    originalImageSize = pixmap.size();
    
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
            zoomFactor = (double)viewSize.width() / pixmap.width() * 0.95; // 95%로 맞추기
        } else {
            // 높이에 맞춤
            zoomFactor = (double)viewSize.height() / pixmap.height() * 0.95;
        }
        
        // 중앙 정렬 설정
        setAlignment(Qt::AlignCenter);
    }
    
    // 화면 갱신
    update();
}

void CameraView::drawResizeHandles(QPainter& painter, const QRect& /*rect*/) {
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

    // 항상 선택된 패턴의 개별 좌표 사용 (그룹화되어도 개별 회전)
    QRectF effectiveRect = pattern->rect;

    QVector<QPoint> corners;
    
    // ★★★ 리사이징 중일 때는 고정점 기준으로 안정적 계산 ★★★
    if (isResizing && !fixedScreenPos.isNull() && activeHandleIdx != -1) {
        // 현재 마우스 위치를 기반으로 안정적인 꼭짓점 계산
        QPoint mousePos = mapFromGlobal(QCursor::pos());
        double fx = fixedScreenPos.x();
        double fy = fixedScreenPos.y();
        double mx = mousePos.x();
        double my = mousePos.y();
        
        // 중심점
        double cx = (fx + mx) / 2.0;
        double cy = (fy + my) / 2.0;
        
        // 회전 각도
        double radians = pattern->angle * M_PI / 180.0;
        double cosA = std::cos(radians);
        double sinA = std::sin(radians);
        
        // 로컬 좌표계에서 크기 계산
        double dx = mx - fx;
        double dy = my - fy;
        double local_dx = dx * cosA + dy * sinA;
        double local_dy = -dx * sinA + dy * cosA;
        
        double width = std::abs(local_dx);
        double height = std::abs(local_dy);
        
        // 회전되지 않은 꼭짓점들
        double halfWidth = width / 2.0;
        double halfHeight = height / 2.0;
        
        QVector<QPointF> unrotatedCorners = {
            QPointF(cx - halfWidth, cy - halfHeight),  // top-left
            QPointF(cx + halfWidth, cy - halfHeight),  // top-right  
            QPointF(cx + halfWidth, cy + halfHeight),  // bottom-right
            QPointF(cx - halfWidth, cy + halfHeight)   // bottom-left
        };
        
        // 회전 적용
        corners.resize(4);
        for (int i = 0; i < 4; i++) {
            double dx = unrotatedCorners[i].x() - cx;
            double dy = unrotatedCorners[i].y() - cy;
            
            double rotatedX = cx + dx * cosA - dy * sinA;
            double rotatedY = cy + dx * sinA + dy * cosA;
            
            corners[i] = QPoint(static_cast<int>(std::round(rotatedX)), 
                               static_cast<int>(std::round(rotatedY)));
        }
        
        return corners;
    }
    
    // 일반적인 경우: 예제소스 방식으로 정밀 계산
    if (backgroundPixmap.isNull()) return corners;
    
    QSize viewportSize = size();
    QSize imgSize = backgroundPixmap.size();
    
    // 화면 변환 정보 계산 (부동소수점 유지)
    double scaledWidth = imgSize.width() * zoomFactor;
    double scaledHeight = imgSize.height() * zoomFactor;
    double offsetX = (viewportSize.width() - scaledWidth) / 2.0 + panOffset.x();
    double offsetY = (viewportSize.height() - scaledHeight) / 2.0 + panOffset.y();
    
    // 원본 사각형을 화면 좌표로 변환 (부동소수점 정밀도 유지)
    double x1 = effectiveRect.x() * zoomFactor + offsetX;
    double y1 = effectiveRect.y() * zoomFactor + offsetY;
    double x2 = (effectiveRect.x() + effectiveRect.width()) * zoomFactor + offsetX;
    double y2 = (effectiveRect.y() + effectiveRect.height()) * zoomFactor + offsetY;
    
    // 중심점과 크기 계산 (부동소수점)
    double centerX = (x1 + x2) / 2.0;
    double centerY = (y1 + y2) / 2.0;
    double width = x2 - x1;
    double height = y2 - y1;
    
    // 예제소스와 동일한 방식: 회전되지 않은 꼭짓점들 먼저 계산
    double halfWidth = width / 2.0;
    double halfHeight = height / 2.0;
    
    QVector<QPointF> unrotatedCorners = {
        QPointF(centerX - halfWidth, centerY - halfHeight),  // top-left
        QPointF(centerX + halfWidth, centerY - halfHeight),  // top-right
        QPointF(centerX + halfWidth, centerY + halfHeight),  // bottom-right
        QPointF(centerX - halfWidth, centerY + halfHeight)   // bottom-left
    };
    
    corners.resize(4);
    
    // 항상 회전 적용 (개별 패턴 기준)
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
    
    QSize viewportSize = size();
    QSize imgSize = backgroundPixmap.size();
    
    // 화면 변환 정보 계산 (부동소수점 유지)
    double scaledWidth = imgSize.width() * zoomFactor;
    double scaledHeight = imgSize.height() * zoomFactor;
    double offsetX = (viewportSize.width() - scaledWidth) / 2.0 + panOffset.x();
    double offsetY = (viewportSize.height() - scaledHeight) / 2.0 + panOffset.y();
    
    // 원본 사각형을 화면 좌표로 변환 (부동소수점 정밀도 유지)
    double x1 = pattern.rect.x() * zoomFactor + offsetX;
    double y1 = pattern.rect.y() * zoomFactor + offsetY;
    double x2 = (pattern.rect.x() + pattern.rect.width()) * zoomFactor + offsetX;
    double y2 = (pattern.rect.y() + pattern.rect.height()) * zoomFactor + offsetY;
    
    // 중심점과 크기 계산 (부동소수점)
    double centerX = (x1 + x2) / 2.0;
    double centerY = (y1 + y2) / 2.0;
    double width = x2 - x1;
    double height = y2 - y1;
    
    // 회전되지 않은 꼭짓점들 먼저 계산
    double halfWidth = width / 2.0;
    double halfHeight = height / 2.0;
    
    QVector<QPointF> unrotatedCorners = {
        QPointF(centerX - halfWidth, centerY - halfHeight),  // top-left
        QPointF(centerX + halfWidth, centerY - halfHeight),  // top-right
        QPointF(centerX + halfWidth, centerY + halfHeight),  // bottom-right
        QPointF(centerX - halfWidth, centerY + halfHeight)   // bottom-left
    };
    
    // 회전 적용
    double radians = pattern.angle * M_PI / 180.0;
    
    double cosA = std::cos(radians);
    double sinA = std::sin(radians);
    
    corners.resize(4);
    for (int i = 0; i < 4; i++) {
        double dx = unrotatedCorners[i].x() - centerX;
        double dy = unrotatedCorners[i].y() - centerY;
        
        double rotatedX = centerX + dx * cosA - dy * sinA;
        double rotatedY = centerY + dx * sinA + dy * cosA;
        
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
    QVector<QPoint> corners = getRotatedCorners();
    if (corners.size() < 4) return QRect(); // 4개 꼭짓점 모두 필요
    
    // 상단 중심점을 더 정확하게 계산
    QPointF topCenter = QPointF(
        (corners[0].x() + corners[1].x()) / 2.0,
        (corners[0].y() + corners[1].y()) / 2.0
    );
    
    QPoint center = getRotatedCenter();
    int offset = 30;
    
    // 벡터 계산을 부동소수점으로 수행
    double dx = topCenter.x() - center.x();
    double dy = topCenter.y() - center.y();
    double len = std::hypot(dx, dy);
    if (len < 1.0) len = 1.0; // 0 나눗셈 방지
    
    double nx = dx / len;
    double ny = dy / len;
    
    // 핸들 위치 계산 (부동소수점으로 정확히 계산 후 반올림)
    int hx = qRound(topCenter.x() + nx * offset);
    int hy = qRound(topCenter.y() + ny * offset);
    
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

QPoint CameraView::originalToDisplay(const QPoint& originalPos) const {
    if (backgroundPixmap.isNull()) return originalPos;
    QSize viewportSize = size();
    QSize imgSize = backgroundPixmap.size();
    int scaledWidth = qRound(imgSize.width() * zoomFactor);
    int scaledHeight = qRound(imgSize.height() * zoomFactor);
    int offsetX = (viewportSize.width() - scaledWidth) / 2 + panOffset.x();
    int offsetY = (viewportSize.height() - scaledHeight) / 2 + panOffset.y();
    int dispX = qRound(originalPos.x() * zoomFactor) + offsetX;
    int dispY = qRound(originalPos.y() * zoomFactor) + offsetY;
    return QPoint(dispX, dispY);
}

QUuid CameraView::hitTest(const QPoint& pos) {
    // 디스플레이 좌표를 원본 좌표로 변환
    QPoint originalPos = displayToOriginal(pos);
    
    QUuid result;
    int minDistSq = std::numeric_limits<int>::max();
    
    // 1. 자식 패턴 먼저 검사 (자식 패턴이 부모 위에 그려지기 때문)
    for (int i = 0; i < patterns.size(); ++i) {
        if (!patterns[i].enabled) continue;
        
        // 시뮬레이션 모드에서는 currentCameraUuid과 비교, 일반 모드에서는 currentCameraUuid와 비교
        bool patternVisible = false;
        if (!currentCameraUuid.isEmpty()) {
            // 시뮬레이션 모드: currentCameraUuid (CAM(...)) 형태와 비교
            patternVisible = (patterns[i].cameraUuid == currentCameraUuid);
        } else {
            // 일반 모드: currentCameraUuid와 비교
            patternVisible = (currentCameraUuid.isEmpty() || patterns[i].cameraUuid == currentCameraUuid);
        }
        
        if (!patternVisible) {
            continue;
        }
        
        // 자식 패턴 검사 (부모보다 우선 선택)
        if (patterns[i].type == PatternType::FID && !patterns[i].childIds.isEmpty()) {
            for (const QUuid& childId : patterns[i].childIds) {
                for (int j = 0; j < patterns.size(); ++j) {
                    if (patterns[j].id == childId && patterns[j].enabled) {
                        QRectF rect = patterns[j].rect;
                        
                        // 점이 패턴 내부에 있는지 확인
                        if (rect.contains(originalPos)) {
                            QPoint rectCenter = rect.center().toPoint();
                            int centerDistSq = QPoint(originalPos.x() - rectCenter.x(), 
                                                    originalPos.y() - rectCenter.y()).manhattanLength();
                            if (centerDistSq < minDistSq) {
                                result = patterns[j].id;
                                minDistSq = centerDistSq;
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 2. 자식 패턴이 선택되지 않았으면 모든 패턴 검사
    if (result.isNull()) {
        // 최근에 추가된 패턴이 우선 선택되도록 뒤에서부터 검사
        for (int i = patterns.size() - 1; i >= 0; --i) {
            if (!patterns[i].enabled) continue;
            
            // 시뮬레이션 모드에서는 currentCameraUuid과 비교, 일반 모드에서는 currentCameraUuid와 비교
            bool patternVisible = false;
            if (!currentCameraUuid.isEmpty()) {
                // 시뮬레이션 모드: currentCameraUuid (CAM(...)) 형태와 비교
                patternVisible = (patterns[i].cameraUuid == currentCameraUuid);
            } else {
                // 일반 모드: currentCameraUuid와 비교
                patternVisible = (currentCameraUuid.isEmpty() || patterns[i].cameraUuid == currentCameraUuid);
            }
            
            if (!patternVisible) continue;
                
            QRectF rect = patterns[i].rect;
            
            // 점이 패턴 내부에 있는지 확인
            if (rect.contains(originalPos)) {
                QPoint rectCenter = rect.center().toPoint();
                int centerDistSq = QPoint(originalPos.x() - rectCenter.x(), 
                                        originalPos.y() - rectCenter.y()).manhattanLength();
                if (centerDistSq < minDistSq) {
                    result = patterns[i].id;
                    minDistSq = centerDistSq;
                }
            }
        }
    }
    
    // 3. 패턴이 선택되지 않았으면 그룹 경계 검사
    if (result.isNull()) {
        for (int i = 0; i < patterns.size(); ++i) {
            if (!patterns[i].enabled) continue;
            
            // 시뮬레이션 모드에서는 currentCameraUuid과 비교, 일반 모드에서는 currentCameraUuid와 비교
            bool patternVisible = false;
            if (!currentCameraUuid.isEmpty()) {
                // 시뮬레이션 모드: currentCameraUuid (CAM(...)) 형태와 비교
                patternVisible = (patterns[i].cameraUuid == currentCameraUuid);
            } else {
                // 일반 모드: currentCameraUuid와 비교
                patternVisible = (currentCameraUuid.isEmpty() || patterns[i].cameraUuid == currentCameraUuid);
            }
            
            if (!patternVisible) continue;
                
            // FID 패턴이고 자식이 있는 경우
            if (patterns[i].type == PatternType::FID && !patterns[i].childIds.isEmpty()) {
                QRectF groupRect = patterns[i].rect;
                
                // 모든 자식 패턴 포함하는 그룹 경계 계산
                for (const QUuid& childId : patterns[i].childIds) {
                    PatternInfo* child = getPatternById(childId);
                    if (!child || !child->enabled) continue;
                    
                    groupRect = groupRect.united(child->rect);
                }
                
                // 여백 추가
                groupRect.adjust(-10, -10, 10, 10);
                
                // 그룹 경계 테두리 검사 (5픽셀 범위)
                QRect outerRect = groupRect.adjusted(-5, -5, 5, 5).toRect();
                QRect innerRect = groupRect.adjusted(5, 5, -5, -5).toRect();
                
                if (outerRect.contains(originalPos) && !innerRect.contains(originalPos)) {
                    return patterns[i].id; // FID 패턴 ID 반환
                }
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
    if (selectedPatternId == id) return;

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
        emit patternSelected(id);
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
        
        printf("[CameraView] 필터 적용 중 - 패턴: %s, 필터 수: %d\n", 
               pattern.name.toStdString().c_str(), pattern.filters.size());
        fflush(stdout);
        
        // 패턴의 rect는 이미 원본 이미지 좌표계에 있음
        QRectF rect = pattern.rect;
        
        // 유효한 이미지 좌표 범위로 제한
        int x = qBound(0, qRound(rect.x()), image.cols - 2);
        int y = qBound(0, qRound(rect.y()), image.rows - 2);
        int width = qBound(1, qRound(rect.width()), image.cols - x);
        int height = qBound(1, qRound(rect.height()), image.rows - y);
        
        // 유효하지 않은 크기면 건너뜀
        if (width <= 0 || height <= 0) continue;
        
        // OpenCV ROI 생성
        cv::Rect roi(x, y, width, height);
        
        try {
            // ROI가 이미지 범위 안에 있는지 최종 확인
            if (roi.x >= 0 && roi.y >= 0 && 
                roi.x + roi.width <= image.cols && 
                roi.y + roi.height <= image.rows &&
                roi.width > 0 && roi.height > 0) {
                
                printf("[CameraView] ImageProcessor::applyFilters 호출 - ROI: %d,%d,%d,%d\n", 
                       roi.x, roi.y, roi.width, roi.height);
                fflush(stdout);
                ImageProcessor::applyFilters(image, pattern.filters, roi);
                printf("[CameraView] 필터 적용 완료\n");
                fflush(stdout);
            } else {
                printf("[CameraView] ROI 범위 오류 - 이미지 크기: %dx%d, ROI: %d,%d,%d,%d\n", 
                       image.cols, image.rows, roi.x, roi.y, roi.width, roi.height);
                fflush(stdout);
            }
        } catch (const std::exception& e) {
        } catch (...) {
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
    
    // 그룹 라벨은 표시하지 않음 (사용자 요청에 따라 제거)
    // if (!groupPatterns.isEmpty()) {
    //     QString groupLabel = QString("GROUP");
    //     QFont font = painter.font();
    //     font.setPointSize(font.pointSize() - 1);
    //     painter.setFont(font);
    //     
    //     QPen textPen(UIColors::GROUP_COLOR);   // 그룹과 같은 마젠타 색상
    //     painter.setPen(textPen);
    //     
    //     // 바운딩 박스 중심점 위쪽에 라벨 표시
    //     QPoint centerDisplay = originalToDisplay(boundingBox.center().toPoint());
    //     QRect textRect(centerDisplay.x() - 30, centerDisplay.y() - boundingBox.height()/2*zoomFactor - 25, 60, 20);
    //     painter.drawText(textRect, Qt::AlignCenter, groupLabel);
    // }
}

QPixmap CameraView::getBackgroundPixmap() const {
    return backgroundPixmap;
}