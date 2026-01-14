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
#include <QComboBox>
#include <QMouseEvent>
#include <QCloseEvent>
#include <opencv2/opencv.hpp>
#include "CommonDefs.h"

class TeachingWidget;

// 테스트 검사 결과 저장용 구조체
struct TestResultRow {
    QString timestamp;
    QString imageName;
    QMap<QString, QString> patternResults; // 패턴명 -> PASS/NG
};

class TestDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TestDialog(TeachingWidget *parent = nullptr);
    ~TestDialog();
    
    void syncInspectionArea(int area);  // 외부에서 검사 영역 동기화용

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onLoadImages();
    void onImageSelected(QListWidgetItem *item);
    void onRunTest();
    void onClearResults();
    void onInspectionAreaChanged(int index);
    void onSaveResults();
    void onResultTableClicked(int row, int column);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void setupUI();
    void rebuildResultTable();  // 레시피 패턴에 따라 테이블 재구성
    void loadImageThumbnails(const QStringList &imagePaths);
    void addResultToTable(const QString &timestamp, const QString &imageName,
                         const QString &patternName, const QString &inspectionMethod,
                         const QString &result, const QString &value);
    void runInspectionOnImage(const QString &imagePath);
    
    // 결과 저장 관련
    bool hasUnsavedResults() const;
    void saveResultsToTxt(const QString &filePath);
    void saveResultsToXml(const QString &filePath);
    void saveResultsToJson(const QString &filePath);

    TeachingWidget *teachingWidget;
    
    // UI Components
    QListWidget *imageListWidget;
    QTableWidget *resultTableWidget;
    QPushButton *loadButton;
    QPushButton *runButton;
    QPushButton *clearButton;
    QPushButton *closeButton;
    QLabel *statusLabel;
    QComboBox *areaComboBox;
    
    // Data
    QStringList imagePathList;
    int currentInspectionArea; // 0: FRONT-STRIP, 1: FRONT-CRIMP, 2: REAR-STRIP, 3: REAR-CRIMP
    QList<TestResultRow> areaResults[4];  // 각 영역별 결과
    QStringList currentPatternNames;    // 현재 영역의 패턴명 리스트
    
    // Mouse drag
    QPoint dragPosition;
    bool isDragging;
};

#endif // TESTDIALOG_H
