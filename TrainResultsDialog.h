#ifndef TRAINRESULTSDIALOG_H
#define TRAINRESULTSDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QPixmap>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

class TrainResultsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TrainResultsDialog(const QString& recipeName, QWidget *parent = nullptr);

private slots:
    void previousImage();
    void nextImage();
    void onImageClicked();

private:
    void setupUI();
    void loadImages();
    void updateImageDisplay();
    
    QString recipeName;
    QString resultsPath;
    QStringList imagePaths;
    int currentIndex;
    
    QLabel* imageLabel;
    QLabel* imageInfoLabel;
    QPushButton* prevButton;
    QPushButton* nextButton;
    QPushButton* deleteButton;
    QScrollArea* scrollArea;
};

#endif // TRAINRESULTSDIALOG_H
