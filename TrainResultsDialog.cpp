#include "TrainResultsDialog.h"
#include <QGridLayout>
#include <QApplication>
#include <QScreen>

TrainResultsDialog::TrainResultsDialog(const QString& recipeName, QWidget *parent)
    : QDialog(parent)
    , recipeName(recipeName)
    , currentIndex(0)
{
    resultsPath = QString("results/%1").arg(recipeName);
    
    setWindowTitle(QString("학습 결과 - %1").arg(recipeName));
    setModal(true);
    
    // 화면 크기의 80%로 설정
    QScreen *screen = QApplication::primaryScreen();
    QRect screenGeometry = screen->geometry();
    int width = screenGeometry.width() * 0.8;
    int height = screenGeometry.height() * 0.8;
    resize(width, height);
    
    setupUI();
    loadImages();
    updateImageDisplay();
}

void TrainResultsDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // 이미지 표시 영역
    scrollArea = new QScrollArea();
    imageLabel = new QLabel();
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setScaledContents(false);
    imageLabel->setMinimumSize(400, 300);
    imageLabel->setStyleSheet("border: 1px solid gray;");
    
    scrollArea->setWidget(imageLabel);
    scrollArea->setWidgetResizable(true);
    
    // 이미지 정보 라벨
    imageInfoLabel = new QLabel("이미지 정보");
    imageInfoLabel->setAlignment(Qt::AlignCenter);
    
    // 네비게이션 버튼들
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    prevButton = new QPushButton("← 이전");
    nextButton = new QPushButton("이후 →");
    deleteButton = new QPushButton("삭제");
    
    prevButton->setFixedSize(80, 30);
    nextButton->setFixedSize(80, 30);
    deleteButton->setFixedSize(60, 30);
    
    buttonLayout->addWidget(prevButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(deleteButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(nextButton);
    
    // 닫기 버튼
    QPushButton* closeButton = new QPushButton("닫기");
    closeButton->setFixedSize(80, 30);
    
    QHBoxLayout* closeLayout = new QHBoxLayout();
    closeLayout->addStretch();
    closeLayout->addWidget(closeButton);
    
    // 레이아웃 구성
    mainLayout->addWidget(scrollArea, 1);
    mainLayout->addWidget(imageInfoLabel);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addLayout(closeLayout);
    
    // 시그널 연결
    connect(prevButton, &QPushButton::clicked, this, &TrainResultsDialog::previousImage);
    connect(nextButton, &QPushButton::clicked, this, &TrainResultsDialog::nextImage);
    connect(deleteButton, &QPushButton::clicked, this, &TrainResultsDialog::onImageClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void TrainResultsDialog::loadImages()
{
    imagePaths.clear();
    
    QDir resultsDir(resultsPath);
    if (!resultsDir.exists()) {
        imageInfoLabel->setText("학습 결과 폴더가 존재하지 않습니다.");
        return;
    }
    
    // 이미지 파일 필터
    QStringList nameFilters;
    nameFilters << "*.jpg" << "*.jpeg" << "*.png" << "*.bmp" << "*.tiff" << "*.tif";
    
    QFileInfoList fileList = resultsDir.entryInfoList(nameFilters, QDir::Files, QDir::Name);
    
    for (const QFileInfo& fileInfo : fileList) {
        imagePaths.append(fileInfo.absoluteFilePath());
    }
    
    if (imagePaths.isEmpty()) {
        imageInfoLabel->setText("학습 결과 이미지가 없습니다.");
    }
}

void TrainResultsDialog::updateImageDisplay()
{
    if (imagePaths.isEmpty()) {
        imageLabel->setText("표시할 이미지가 없습니다.");
        prevButton->setEnabled(false);
        nextButton->setEnabled(false);
        deleteButton->setEnabled(false);
        return;
    }
    
    if (currentIndex < 0 || currentIndex >= imagePaths.size()) {
        currentIndex = 0;
    }
    
    QString currentImagePath = imagePaths[currentIndex];
    QPixmap pixmap(currentImagePath);
    
    if (!pixmap.isNull()) {
        // 스크롤 영역에 맞게 이미지 크기 조정
        QSize scrollSize = scrollArea->size();
        if (pixmap.width() > scrollSize.width() - 20 || pixmap.height() > scrollSize.height() - 20) {
            pixmap = pixmap.scaled(scrollSize.width() - 20, scrollSize.height() - 20, 
                                 Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        
        imageLabel->setPixmap(pixmap);
        imageLabel->resize(pixmap.size());
    } else {
        imageLabel->setText("이미지를 불러올 수 없습니다.");
    }
    
    // 이미지 정보 업데이트
    QString fileName = QFileInfo(currentImagePath).fileName();
    imageInfoLabel->setText(QString("학습 결과: %1 (%2/%3)")
                          .arg(fileName)
                          .arg(currentIndex + 1)
                          .arg(imagePaths.size()));
    
    // 버튼 상태 업데이트
    prevButton->setEnabled(currentIndex > 0);
    nextButton->setEnabled(currentIndex < imagePaths.size() - 1);
    deleteButton->setEnabled(true);
}

void TrainResultsDialog::previousImage()
{
    if (currentIndex > 0) {
        currentIndex--;
        updateImageDisplay();
    }
}

void TrainResultsDialog::nextImage()
{
    if (currentIndex < imagePaths.size() - 1) {
        currentIndex++;
        updateImageDisplay();
    }
}

void TrainResultsDialog::onImageClicked()
{
    if (imagePaths.isEmpty() || currentIndex < 0 || currentIndex >= imagePaths.size()) {
        return;
    }
    
    QString currentImagePath = imagePaths[currentIndex];
    QString fileName = QFileInfo(currentImagePath).fileName();
    
    int ret = QMessageBox::question(this, "이미지 삭제", 
                                   QString("'%1'을(를) 삭제하시겠습니까?").arg(fileName),
                                   QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        if (QFile::remove(currentImagePath)) {
            // 리스트에서 제거
            imagePaths.removeAt(currentIndex);
            
            // 인덱스 조정
            if (currentIndex >= imagePaths.size() && !imagePaths.isEmpty()) {
                currentIndex = imagePaths.size() - 1;
            }
            
            // 화면 업데이트
            updateImageDisplay();
            
            QMessageBox::information(this, "삭제 완료", QString("'%1'이(가) 삭제되었습니다.").arg(fileName));
        } else {
            QMessageBox::warning(this, "삭제 실패", QString("'%1'을(를) 삭제할 수 없습니다.").arg(fileName));
        }
    }
}
