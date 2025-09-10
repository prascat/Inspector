#ifndef FILTERDIALOG_H
#define FILTERDIALOG_H

#include <QDialog>
#include <QMap>
#include <QVector>
#include <QCheckBox>
#include <QLabel>
#include <QDebug>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include "CameraView.h"
#include "ImageProcessor.h"
#include "FilterPropertyWidget.h"

class FilterDialog : public QDialog {
    Q_OBJECT

public:
    FilterDialog(CameraView* cameraView, int patternIndex, QWidget* parent = nullptr);
    void setPatternIndex(int index);
    void setPatternId(const QUuid& id);
    
private slots:
    void onCancelClicked();
    void onApplyClicked();
    void onFilterCheckStateChanged(int state);
    void onFilterParamChanged(const QString& paramName, int value);

private:
    void setupUI();
    void createFilterControls(QWidget* filtersWidget);
    void addFilterWidget(int filterType, QGroupBox* groupBox);
    QMap<QString, int> getFilterParams(int filterType);
    
    // 인덱스를 UUID로 변환하는 헬퍼 메서드
    QUuid getPatternId(int index) const;
    void updateUIFromFilters();
    void updateFilterParam(int filterType, const QString& paramName, int value);

    CameraView* cameraView;
    int patternIndex;
    QUuid patternId; // 패턴 ID
    
    // 필터 관련 데이터
    QVector<int> filterTypes;
    QMap<int, QString> filterNames;
    QMap<int, QCheckBox*> filterCheckboxes;
    QMap<int, FilterPropertyWidget*> filterWidgets;
    QMap<int, FilterInfo> appliedFilters;
    QMap<int, QMap<QString, int>> defaultParams;
};

#endif // FILTERDIALOG_H