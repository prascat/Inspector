#ifndef CAMERASETTINGSDIALOG_H
#define CAMERASETTINGSDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QGroupBox>
#include <QMessageBox>
#include <QRadioButton>
#include "CommonDefs.h"

class CameraSettingsDialog : public QDialog {
    Q_OBJECT
    
public:
    CameraSettingsDialog(QVector<CameraInfo>& cameraInfos, QWidget* parent = nullptr);
    ~CameraSettingsDialog();
    
    // 레시피에 맞는 카메라로 변경
    void setRecipeCamera(const QString& recipeCameraUuid);
    
signals:
    // 레시피 재할당 시그널
    // methodCode - 0: 복사, 1: 교환, 2: 이동
    void recipeReassigned(int sourceIndex, int targetIndex, int methodCode, 
                         const QString& sourceUuid, const QString& targetUuid);
    
private slots:
    void onApplyClicked();
    
private:
    void setupUI();
    
    QVector<CameraInfo>& cameraInfos; // 참조로 카메라 정보 접근
    int currentCameraIndex;
    
    // UI 요소
    QComboBox* sourceComboBox;
    QComboBox* targetComboBox;
    QRadioButton* copyRadio;
    QRadioButton* swapRadio;
    QRadioButton* moveRadio;
    QPushButton* applyButton;
};

#endif // CAMERASETTINGSDIALOG_H