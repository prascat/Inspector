#ifndef CUSTOMMESSAGEBOX_H
#define CUSTOMMESSAGEBOX_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QLineEdit>

class CustomMessageBox : public QDialog {
    Q_OBJECT

public:
    enum IconType {
        NoIcon,
        Information,
        Warning,
        Critical,
        Question
    };

    explicit CustomMessageBox(QWidget* parent = nullptr);
    
    // 편의 생성자 - 타이틀, 메시지, 아이콘, 버튼을 한 번에 설정
    CustomMessageBox(QWidget* parent, IconType iconType, const QString& title, 
                     const QString& message, QMessageBox::StandardButtons buttons = QMessageBox::Ok);
    
    void setTitle(const QString& title);
    void setMessage(const QString& message);
    void setIcon(IconType iconType);
    void setButtons(QMessageBox::StandardButtons buttons);
    
    // 입력 필드 관련 함수
    void setInputField(bool enabled, const QString& defaultText = "");
    QString getInputText() const;
    
    int exec();

private:
    void setupUI();
    void centerOnScreen();
    
    QLabel* iconLabel;
    QLabel* titleLabel;
    QLabel* messageLabel;
    QLineEdit* inputEdit;
    QPushButton* okButton;
    QPushButton* yesButton;
    QPushButton* noButton;
    QPushButton* cancelButton;
    
    QString titleText;
    QString messageText;
    IconType currentIcon;
    QMessageBox::StandardButtons buttonFlags;
    QMessageBox::StandardButton result;
    bool hasInputField;
};

#endif // CUSTOMMESSAGEBOX_H
