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
#include <QShowEvent>
#include <QSettings>
#include <QLineEdit>
#include <QFileDialog>
#include <QCheckBox>
#include <chrono>
#include <thread>
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
    
    int getSelectedCameraIndex() const;
    
#ifdef USE_SPINNAKER
    void setSpinnakerCameras(const std::vector<Spinnaker::CameraPtr>& cameras);
#endif

public slots:

private slots:
    void onBrowseLiveUserSet();
    void onBrowseInspectUserSet();
    void onUploadUserSets();  // LIVE와 INSPECT 둘 다 업로드
    
private:
    void setupUI();
    void setupUserSetSettings();
    
#ifdef USE_SPINNAKER
    bool uploadUserSetFile(Spinnaker::CameraPtr camera, const QString& filePath, const QString& userSetName);
    void printCameraParameters(Spinnaker::GenApi::INodeMap& nodeMap, const QString& stage);
#endif
    
protected:
    void showEvent(QShowEvent* event) override;
    
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
    QLineEdit* liveUserSetPathEdit;     // LIVE UserSet 파일 경로
    QLineEdit* inspectUserSetPathEdit;  // INSPECT UserSet 파일 경로
    QPushButton* browseLiveUserSetBtn;  // LIVE UserSet 파일 선택
    QPushButton* browseInspectUserSetBtn; // INSPECT UserSet 파일 선택
    QPushButton* uploadUserSetsBtn;     // LIVE & INSPECT UserSet 업로드
    
    // 카메라 인덱스
    int currentCameraIndex;
    
    // ConfigManager 인스턴스
    ConfigManager* m_configManager;
};

#endif // CAMERASETTINGSDIALOG_H