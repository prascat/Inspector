#include "CustomFileDialog.h"
#include <QPixmap>
#include <QImageReader>

QString CustomFileDialog::getOpenFileName(QWidget *parent,
                                         const QString &caption,
                                         const QString &dir,
                                         const QString &filter)
{
    QFileDialog dialog(parent, caption, dir, filter);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.resize(1000, 600);
    applyBlackTheme(dialog);
    addImagePreview(dialog);
    
    QString filePath;
    if (dialog.exec() == QDialog::Accepted) {
        QStringList files = dialog.selectedFiles();
        if (!files.isEmpty()) {
            filePath = files.first();
        }
    }
    
    return filePath;
}

QStringList CustomFileDialog::getOpenFileNames(QWidget *parent,
                                               const QString &caption,
                                               const QString &dir,
                                               const QString &filter)
{
    QFileDialog dialog(parent, caption, dir, filter);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
    dialog.resize(1000, 600);
    applyBlackTheme(dialog);
    addImagePreview(dialog);
    
    // 부모 위젯 중앙에 배치
    if (parent) {
        QPoint parentTopLeft = parent->mapToGlobal(QPoint(0, 0));
        int x = parentTopLeft.x() + (parent->width() - dialog.width()) / 2;
        int y = parentTopLeft.y() + (parent->height() - dialog.height()) / 2;
        dialog.move(x, y);
    }
    
    QStringList filePaths;
    if (dialog.exec() == QDialog::Accepted) {
        filePaths = dialog.selectedFiles();
    }
    
    return filePaths;
}

QString CustomFileDialog::getSaveFileName(QWidget *parent,
                                         const QString &caption,
                                         const QString &dir,
                                         const QString &filter)
{
    QFileDialog dialog(parent, caption, dir, filter);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.resize(800, 500);
    applyBlackTheme(dialog);
    
    QString filePath;
    if (dialog.exec() == QDialog::Accepted) {
        QStringList files = dialog.selectedFiles();
        if (!files.isEmpty()) {
            filePath = files.first();
        }
    }
    
    return filePath;
}

QString CustomFileDialog::getExistingDirectory(QWidget *parent,
                                               const QString &caption,
                                               const QString &dir)
{
    QFileDialog dialog(parent, caption, dir);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.resize(800, 500);
    applyBlackTheme(dialog);
    
    QString dirPath;
    if (dialog.exec() == QDialog::Accepted) {
        QStringList dirs = dialog.selectedFiles();
        if (!dirs.isEmpty()) {
            dirPath = dirs.first();
        }
    }
    
    return dirPath;
}

void CustomFileDialog::applyBlackTheme(QFileDialog &dialog)
{
    dialog.setStyleSheet(
        "QFileDialog { background-color: #1e1e1e; color: #ffffff; }"
        "QWidget { background-color: #1e1e1e; color: #ffffff; }"
        "QPushButton { background-color: #2d2d2d; color: #ffffff; border: 1px solid #3d3d3d; padding: 5px; min-width: 80px; }"
        "QPushButton:hover { background-color: #3d3d3d; }"
        "QLineEdit { background-color: #252525; color: #ffffff; border: 1px solid #3d3d3d; padding: 3px; }"
        "QTreeView { background-color: #252525; color: #ffffff; border: 1px solid #3d3d3d; }"
        "QTreeView::item:hover { background-color: #3d3d3d; }"
        "QTreeView::item:selected { background-color: #0d47a1; }"
        "QHeaderView::section { background-color: #2d2d2d; color: #ffffff; border: 1px solid #3d3d3d; padding: 3px; }"
        "QComboBox { background-color: #252525; color: #ffffff; border: 1px solid #3d3d3d; padding: 3px; }"
        "QComboBox:hover { background-color: #3d3d3d; }"
        "QComboBox::drop-down { border: none; }"
        "QLabel { color: #ffffff; }"
    );
}

void CustomFileDialog::addImagePreview(QFileDialog &dialog)
{
    // 미리보기 레이블 생성
    QLabel *previewLabel = new QLabel(&dialog);
    previewLabel->setFixedSize(300, 300);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setStyleSheet(
        "QLabel { "
        "   background-color: #252525; "
        "   border: 2px solid #3d3d3d; "
        "   color: #888888; "
        "}"
    );
    previewLabel->setText("미리보기");
    previewLabel->setScaledContents(false);
    
    // 다이얼로그 레이아웃에 미리보기 추가
    QVBoxLayout *previewLayout = new QVBoxLayout();
    previewLayout->addWidget(previewLabel);
    previewLayout->addStretch();
    
    QGridLayout *mainLayout = qobject_cast<QGridLayout*>(dialog.layout());
    if (mainLayout) {
        mainLayout->addLayout(previewLayout, 0, mainLayout->columnCount(), -1, 1);
    }
    
    // 파일 선택 시그널 연결
    QObject::connect(&dialog, &QFileDialog::currentChanged, [previewLabel](const QString &path) {
        QImageReader reader(path);
        if (reader.canRead()) {
            QPixmap pixmap(path);
            if (!pixmap.isNull()) {
                // 비율 유지하며 크기 조정
                QPixmap scaledPixmap = pixmap.scaled(
                    previewLabel->size(),
                    Qt::KeepAspectRatio,
                    Qt::SmoothTransformation
                );
                previewLabel->setPixmap(scaledPixmap);
            } else {
                previewLabel->clear();
                previewLabel->setText("미리보기\n불가");
            }
        } else {
            previewLabel->clear();
            previewLabel->setText("미리보기");
        }
    });
}
