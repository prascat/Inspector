#ifndef FILTERPROPERTYWIDGET_H
#define FILTERPROPERTYWIDGET_H

#include <QWidget>
#include <QMap>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include "CommonDefs.h"

class FilterPropertyWidget : public QWidget {
    Q_OBJECT

public:
    FilterPropertyWidget(int filterType, QWidget* parent = nullptr);
    
    void setFilterType(int type);
    int getFilterType() const;
    
    // 파라미터 가져오기/설정하기
    QMap<QString, int> getParams() const;
    void setParams(const QMap<QString, int>& params);
    int getParamValue(const QString& paramName, int defaultValue) const;
    void setEnabled(bool enabled);
    
signals:
    void paramChanged(const QString& name, int value);
    void enableStateChanged(bool enabled);

private slots:
    void handleSliderValueChanged(int value);
    void handleComboIndexChanged(int index);
    
private:
    void setupUI();
    void setupThresholdUI();
    void setupBlurUI();
    void setupCannyUI();
    void setupSobelUI();
    void setupLaplacianUI();
    void setupSharpenUI();
    void setupBrightnessUI();
    void setupContrastUI();
    void setupContourUI();
    void setupMaskUI();
    
    QSlider* addSlider(const QString& name, const QString& labelText, int min, int max, int value, int step = 1);
    QComboBox* addComboBox(const QString& name, const QString& labelText);

    int filterType;
    QMap<QString, QSlider*> sliders;
    QMap<QString, QLabel*> valueLabels;
    QMap<QString, QComboBox*> combos;
    QVBoxLayout* mainLayout;
    QFormLayout* formLayout;
    QGroupBox* adaptiveGroup; // 이진화 필터의 적응형 파라미터용
};

#endif // FILTERPROPERTYWIDGET_H