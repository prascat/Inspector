#ifndef TESTDIALOG_H
#define TESTDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFileDialog>
#include <QDir>
#include <QRadioButton>
#include <QButtonGroup>
#include <QMouseEvent>
#include <opencv2/opencv.hpp>
#include "CommonDefs.h"

class TeachingWidget;

class TestDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TestDialog(TeachingWidget *parent = nullptr);
    ~TestDialog();
    
    void syncStripCrimpMode(int mode);  // 외부에서 모드 동기화용

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void onLoadImages();
    void onImageSelected(QListWidgetItem *item);
    void onRunTest();
    void onClearResults();
    void onStripCrimpModeChanged(int mode);

private:
    void setupUI();
    void loadImageThumbnails(const QStringList &imagePaths);
    void addResultToTable(const QString &timestamp, const QString &imageName,
                         const QString &patternName, const QString &inspectionMethod,
                         const QString &result, const QString &value);
    void runInspectionOnImage(const QString &imagePath);

    TeachingWidget *teachingWidget;
    
    // UI Components
    QListWidget *imageListWidget;
    QTableWidget *resultTableWidget;
    QPushButton *loadButton;
    QPushButton *runButton;
    QPushButton *clearButton;
    QPushButton *closeButton;
    QLabel *statusLabel;
    QRadioButton *stripRadio;
    QRadioButton *crimpRadio;
    
    // Data
    QStringList imagePathList;
    int currentStripCrimpMode; // 0: STRIP, 1: CRIMP
    
    // Mouse drag
    QPoint dragPosition;
    bool isDragging;
};

#endif // TESTDIALOG_H
