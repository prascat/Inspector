#ifndef LANGUAGESETTINGSDIALOG_H
#define LANGUAGESETTINGSDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>

class LanguageSettingsDialog : public QDialog {
    Q_OBJECT
    
public:
    LanguageSettingsDialog(QWidget* parent = nullptr);
    ~LanguageSettingsDialog();
    
    int exec() override;
    
private slots:
    void onLanguageSelected(int index);
    void onApplyClicked();
    void updateUITexts(); 
    
private:
    QComboBox* languageComboBox;
    QPushButton* applyButton;
    QPushButton* cancelButton;
    QLabel* infoLabel;
    
    void loadAvailableLanguages();
};

#endif // LANGUAGESETTINGSDIALOG_H