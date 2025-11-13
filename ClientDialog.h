#ifndef CLIENTDIALOG_H
#define CLIENTDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTcpSocket>
#include <QTimer>

class ClientDialog : public QDialog {
    Q_OBJECT

public:
    explicit ClientDialog(QWidget* parent = nullptr);
    ~ClientDialog();

    // 설정 값 가져오기
    QString getServerIp() const { return serverIp; }
    int getServerPort() const { return serverPort; }
    bool isAutoConnect() const { return autoConnect; }
    
    // 설정 값 설정하기
    void setServerIp(const QString& ip);
    void setServerPort(int port);
    void setAutoConnect(bool enable);

signals:
    void settingsChanged();

private slots:
    void onTestConnection();
    void onSaveSettings();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void updateConnectionStatus();

private:
    void setupUI();
    void loadSettings();
    void saveSettings();
    void updateLanguage();

    // UI 요소
    QLineEdit* ipEdit;
    QLineEdit* portEdit;
    QPushButton* testButton;
    QPushButton* saveButton;
    QPushButton* cancelButton;
    QLabel* statusLabel;
    QLabel* connectionStatusLabel;
    
    // 설정 값
    QString serverIp;
    int serverPort;
    bool autoConnect;
    
    // TCP 소켓
    QTcpSocket* testSocket;
    QTimer* statusTimer;
    
    // 연결 상태
    bool isConnected;
};

#endif // CLIENTDIALOG_H
