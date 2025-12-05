#include "CustomFileDialog.h"

QString CustomFileDialog::getOpenFileName(QWidget *parent,
                                         const QString &caption,
                                         const QString &dir,
                                         const QString &filter)
{
    QFileDialog dialog(parent, caption, dir, filter);
    dialog.setFileMode(QFileDialog::ExistingFile);
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

QStringList CustomFileDialog::getOpenFileNames(QWidget *parent,
                                               const QString &caption,
                                               const QString &dir,
                                               const QString &filter)
{
    QFileDialog dialog(parent, caption, dir, filter);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
    dialog.resize(900, 600);
    applyBlackTheme(dialog);
    
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
