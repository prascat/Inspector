#ifndef SERIALSETTINGSDIALOG_H
#define SERIALSETTINGSDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QTextEdit>
#include <QLineEdit>
#include <QTimer>
#include <QCheckBox>
#include <QSerialPortInfo>
#include "LanguageManager.h"

#ifndef TR
#define TR(key) LanguageManager::instance()->getText(key)
#endif

class SerialCommunication;

class SerialSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SerialSettingsDialog(SerialCommunication* serialComm, QWidget *parent = nullptr);
    
    int exec() override;

private slots:
    void updateUITexts();
    void refreshPortList();
    void connectToPort();
    void disconnectFromPort();
    void onConnectionStatusChanged(bool connected);
    void onCommandReceived(const QString& command);
    void onInspectionCompleted(int cameraNumber, const QString& result);
    void onErrorOccurred(const QString& error);
    void sendTestCommand();
    void clearLog();
    void clearReceiveData();

private:
    void setupUI();
    void connectSignals();
    void updateConnectionStatus();
    void addLogMessage(const QString& message);
    void addReceiveData(const QString& data);
    void loadSettings();        // 설정 로드
    void saveSettings();        // 설정 저장
    void tryAutoConnect();      // 자동 연결 시도
    
    SerialCommunication* serialComm;
    
    // UI 요소들
    QLabel* portLabel;
    QComboBox* portComboBox;
    QPushButton* refreshButton;
    QLabel* baudRateLabel;
    QSpinBox* baudRateSpinBox;
    QCheckBox* autoConnectCheckBox;
    QPushButton* connectButton;
    QPushButton* disconnectButton;
    QLabel* statusLabel;
    
    QLabel* testCommandLabel;
    QLineEdit* testCommandLineEdit;
    QComboBox* sendModeComboBox;
    QCheckBox* sendRealSerialCheckBox;
    QPushButton* sendTestButton;
    
    QTextEdit* receiveTextEdit;
    QPushButton* clearReceiveButton;
    
    QTextEdit* logTextEdit;
    QPushButton* clearLogButton;
    
    QPushButton* saveSettingsButton;
    QPushButton* closeButton;
};

#endif // SERIALSETTINGSDIALOG_H
