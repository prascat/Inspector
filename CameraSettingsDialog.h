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
#include <QTimer>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include "CommonDefs.h"

#ifdef USE_SPINNAKER
#include <Spinnaker.h>
#endif

class CameraSettingsDialog : public QDialog {
    Q_OBJECT
    
public:
    CameraSettingsDialog(QWidget* parent = nullptr);
    ~CameraSettingsDialog();
    
    void loadCameraSettings();
    int getSelectedCameraIndex() const;
    
#ifdef USE_SPINNAKER
    void setSpinnakerCameras(const std::vector<Spinnaker::CameraPtr>& cameras);
#endif

public slots:
    void applySettings();
    void startHardwareTriggerDetection();
    void stopHardwareTriggerDetection();
    
private slots:
    void checkHardwareTrigger();
    
private:
    void setupUI();
    void setupTriggerSettings();
    void setupExposureSettings();
    void setupGainSettings();
    void setupTriggerMonitoring();
    void loadCurrentCameraSettings();
    
#ifdef USE_SPINNAKER
    void readCameraSettings(Spinnaker::CameraPtr camera);
#endif
    
#ifdef USE_SPINNAKER
    bool checkHardwareTrigger(Spinnaker::CameraPtr camera);
    std::vector<Spinnaker::CameraPtr> m_spinCameras;
#endif
    
    // UI 컨트롤들
    QComboBox* cameraCombo;             // 카메라 선택 콤보박스
    QComboBox* triggerModeCombo;        // 트리거 모드 (Off/Software/Hardware)
    QComboBox* triggerSourceCombo;      // 트리거 소스 (Line0/Line1/Line2/Line3)
    QComboBox* triggerSelectorCombo;    // 트리거 선택자 (Frame Start, etc.)
    QComboBox* triggerActivationCombo;  // 트리거 활성화 (Rising Edge/Falling Edge)
    QSpinBox* triggerDelaySpinBox;      // 트리거 딜레이
    
    QComboBox* exposureAutoCombo;       // 노출 자동 모드 (Off/Once/Continuous)
    QComboBox* gainAutoCombo;           // 게인 자동 모드 (Off/Once/Continuous)
    QSpinBox* exposureSpinBox;          // 노출 시간
    QSpinBox* gainSpinBox;              // 게인 값
    
    // 공통 컨트롤
    QPushButton* startListeningBtn;
    QPushButton* stopListeningBtn;
    QLabel* statusLabel;
    QLabel* triggerStatusLabel;  // Trigger ON/OFF 표시
    
    // 트리거 모니터링
    QTimer* triggerCheckTimer;
    bool isListening;
    int currentCameraIndex;
};

#endif // CAMERASETTINGSDIALOG_H