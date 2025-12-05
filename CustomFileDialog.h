#ifndef CUSTOMFILEDIALOG_H
#define CUSTOMFILEDIALOG_H

#include <QFileDialog>
#include <QString>

class CustomFileDialog
{
public:
    enum Mode {
        OpenFile,
        SaveFile,
        OpenDirectory
    };

    // 파일 열기 다이얼로그
    static QString getOpenFileName(QWidget *parent = nullptr,
                                   const QString &caption = QString(),
                                   const QString &dir = QString(),
                                   const QString &filter = QString());

    // 파일 저장 다이얼로그
    static QString getSaveFileName(QWidget *parent = nullptr,
                                   const QString &caption = QString(),
                                   const QString &dir = QString(),
                                   const QString &filter = QString());

    // 디렉토리 선택 다이얼로그
    static QString getExistingDirectory(QWidget *parent = nullptr,
                                       const QString &caption = QString(),
                                       const QString &dir = QString());

private:
    static void applyBlackTheme(QFileDialog &dialog);
};

#endif // CUSTOMFILEDIALOG_H
