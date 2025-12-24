#ifndef CAMERASETTINGSDIALOG_H
#define CAMERASETTINGSDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QMessageBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QShowEvent>
#include <QSettings>
#include <QLineEdit>
#include <QFileDialog>
#include <QCheckBox>
#include <chrono>
#include <thread>
#include <atomic>
#include "CommonDefs.h"
#include "ConfigManager.h"

#ifdef USE_SPINNAKER
#include <Spinnaker.h>
#endif

class CameraSettingsDialog : public QDialog {
    Q_OBJECT
    
public:
    CameraSettingsDialog(QWidget* parent = nullptr);
    ~CameraSettingsDialog();
    
    int exec() override;
    
    int getSelectedCameraIndex() const;
    
#ifdef USE_SPINNAKER
    void setSpinnakerCameras(const std::vector<Spinnaker::CameraPtr>& cameras);
    void updateLiveImageDisplay(const cv::Mat& frame);  // 라이브 영상 표시
#endif

signals:
    void camerasSwapped();

public slots:

private slots:
    void onLoadUserSet0();
    void onLoadUserSet1();
    void onStartTriggerTest();
    void onStopTriggerTest();
    void onTriggerTestTimeout();
    
private:
    void setupUI();
    void setupUserSetSettings();
    void setupTriggerTestUI();
    
#ifdef USE_SPINNAKER
    void loadUserSet(const QString& userSetName, const QString& modeName);
    void updateCurrentUserSetLabel();
    void printCameraParameters(Spinnaker::GenApi::INodeMap& nodeMap, const QString& stage);
    void updateTriggerTestStatus();
#endif
    
protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    
#ifdef USE_SPINNAKER
    void readCameraSettings(Spinnaker::CameraPtr camera);
#endif
    
#ifdef USE_SPINNAKER
    bool checkHardwareTrigger(Spinnaker::CameraPtr camera);
    std::vector<Spinnaker::CameraPtr> m_spinCameras;
#endif
    
    // UI 컨트롤들
    QComboBox* cameraCombo;             // 카메라 선택 콤보박스
    QLabel* statusLabel;                // 상태 표시 레이블
    
    // UserSet 컨트롤
    QGroupBox* userSetGroup;            // UserSet 설정 그룹박스
    QPushButton* loadUserSet0Btn;       // LIVE UserSet (UserSet0) 로드 버튼
    QPushButton* loadUserSet1Btn;       // TRIGGER UserSet (UserSet1) 로드 버튼
    QLabel* currentUserSetLabel;        // 현재 UserSet 상태 표시 레이블
    QCheckBox* cameraAutoConnectCheckBox;  // 카메라 자동 연결 체크박스
    
    // 트리거 테스트 컨트롤
    QGroupBox* triggerTestGroup;        // 트리거 테스트 그룹박스
    QPushButton* triggerToggleBtn;      // 트리거 테스트 토글 버튼 (시작/중지)
    QLabel* triggerIndicatorLabel;      // 트리거 상태 표시기 (초록색 사각형)
    QLabel* triggerStatusLabel;         // 트리거 상태 텍스트
    QLabel* triggerLineLabel;           // 트리거 라인 정보
    QLabel* triggerEdgeLabel;           // 트리거 엣지 정보 (RisingEdge/FallingEdge)
    QLabel* triggerActivationLabel;     // 트리거 활성화 정보
    QLabel* triggerCountLabel;          // 트리거 감지 횟수
    QLabel* triggerImageLabel;          // 트리거/라이브 영상 표시 라벨
    QLabel* cam0TriggerCountLabel;      // 카메라0 트리거 횟수
    QLabel* cam1TriggerCountLabel;      // 카메라1 트리거 횟수
    std::atomic<bool> isTriggerTesting;              // 트리거 테스트 실행 여부
    std::atomic<bool> liveImageThreadRunning;  // 라이브 영상 스레드 실행 여부
    std::atomic<bool> triggerTestThreadRunning;  // 트리거 테스트 스레드 실행 여부
    std::thread* liveImageThread;       // 라이브 영상 업데이트 스레드
    std::thread* triggerTestThread;     // 트리거 테스트 스레드
    std::atomic<int> triggerDetectionCount;     // 트리거 감지 횟수
    std::atomic<int> cam0TriggerCount;  // 카메라0 트리거 횟수
    std::atomic<int> cam1TriggerCount;  // 카메라1 트리거 횟수
    int lastExposureCount;              // 마지막 노출 카운트
    Spinnaker::ImagePtr lastCapturedImage;  // 마지막 캡처 이미지
    
    // 카메라 인덱스
    int currentCameraIndex;
    
    // ConfigManager 인스턴스
    ConfigManager* m_configManager;
};

#endif // CAMERASETTINGSDIALOG_H