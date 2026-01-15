#ifndef CAMERASETTINGSDIALOG_H
#define CAMERASETTINGSDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QTabWidget>
#include <QMouseEvent>
#include <vector>

#ifdef USE_SPINNAKER
#include <Spinnaker.h>
#include <SpinGenApi/SpinnakerGenApi.h>
#endif

class CameraSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CameraSettingsDialog(QWidget *parent = nullptr);
    ~CameraSettingsDialog();

#ifdef USE_SPINNAKER
    void setCameras(const std::vector<Spinnaker::CameraPtr>& cameras);
#endif

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void onCameraSelected(int index);
    void onLoadUserSet();
    void onSaveUserSet();
    void onApplySettings();
    void onExposureAutoChanged(int state);
    void onGainAutoChanged(int state);
    void onFrameRateEnableChanged(int state);

private:
    void setupUI();
    void applyBlackTheme();
    void loadCameraSettings();
    void updateUIFromCamera();

    // 마우스 드래그 관련
    bool m_dragging;
    QPoint m_dragPosition;

    // UI 위젯
    QWidget* titleBar;
    QLabel* titleLabel;
    QPushButton* closeButtonTop;
    QTabWidget* tabWidget;
    QComboBox* cameraComboBox;
    QComboBox* userSetComboBox;
    QPushButton* loadUserSetButton;
    QPushButton* saveUserSetButton;
    
    // 노출 설정
    QCheckBox* exposureAutoCheckBox;
    QDoubleSpinBox* exposureTimeSpinBox;
    QLabel* exposureRangeLabel;
    
    // 게인 설정
    QCheckBox* gainAutoCheckBox;
    QDoubleSpinBox* gainSpinBox;
    QLabel* gainRangeLabel;
    
    // 화이트 밸런스
    QCheckBox* whiteBalanceAutoCheckBox;
    QDoubleSpinBox* whiteBalanceRedSpinBox;
    QDoubleSpinBox* whiteBalanceBlueSpinBox;
    
    // 감마
    QCheckBox* gammaEnableCheckBox;
    QDoubleSpinBox* gammaSpinBox;
    
    // 이미지 설정
    QSpinBox* widthSpinBox;
    QSpinBox* heightSpinBox;
    QSpinBox* offsetXSpinBox;
    QSpinBox* offsetYSpinBox;
    QComboBox* pixelFormatComboBox;
    QCheckBox* frameRateEnableCheckBox;
    QDoubleSpinBox* frameRateSpinBox;
    QLabel* frameRateRangeLabel;
    
    // 트리거 설정
    QComboBox* triggerModeComboBox;
    QComboBox* triggerSourceComboBox;
    QComboBox* acquisitionModeComboBox;
    QCheckBox* saveTriggerImagesCheckBox;
    
    // 화질 설정
    QDoubleSpinBox* blackLevelSpinBox;
    QDoubleSpinBox* sharpnessSpinBox;
    QCheckBox* sharpnessEnableCheckBox;
    
    QPushButton* applyButton;
    QPushButton* closeButton;

#ifdef USE_SPINNAKER
    std::vector<Spinnaker::CameraPtr> spinCameras;
    Spinnaker::CameraPtr currentCamera;
#endif
    
    int currentCameraIndex;
};

#endif // CAMERASETTINGSDIALOG_H
