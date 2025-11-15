#ifndef CLIENTDIALOG_H
#define CLIENTDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTcpSocket>
#include <QTimer>
#include <QCheckBox>
#include <QThread>

class ClientDialog : public QDialog {
    Q_OBJECT

public:
    static ClientDialog* instance(QWidget* parent = nullptr);
    ~ClientDialog();
    
    int exec() override;
    
    // 연결 관리
    void initialize();  // 프로그램 시작 시 호출
    bool isServerConnected() const { return isConnected; }

    // 설정 값 가져오기
    QString getServerIp() const { return serverIp; }
    int getServerPort() const { return serverPort; }
    bool isAutoConnect() const { return autoConnect; }
    int getReconnectInterval() const { return reconnectInterval; }
    
    // 설정 값 설정하기
    void setServerIp(const QString& ip);
    void setServerPort(int port);
    void setAutoConnect(bool enable);
    void setReconnectInterval(int seconds);
    
    // 재연결 스레드 제어
    void startReconnectThread();
    void stopReconnectThread();

signals:
    void settingsChanged();
    void stripCrimpModeChanged(int mode);  // 0: STRIP, 1: CRIMP

private slots:
    void onTestConnection();
    void onSaveSettings();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onDataReceived();
    void updateConnectionStatus();
    void tryReconnect();

private:
    explicit ClientDialog(QWidget* parent = nullptr);
    static ClientDialog* m_instance;
    
    void setupUI();
    void loadSettings();
    void saveSettings();
    void updateLanguage();

    // UI 요소
    QLineEdit* ipEdit;
    QLineEdit* portEdit;
    QLineEdit* reconnectIntervalEdit;
    QCheckBox* autoConnectCheckBox;
    QPushButton* testButton;
    QPushButton* saveButton;
    QPushButton* cancelButton;
    QLabel* statusLabel;
    QLabel* connectionStatusLabel;
    
    // 설정 값
    QString serverIp;
    int serverPort;
    bool autoConnect;
    int reconnectInterval;
    
    // TCP 소켓
    QTcpSocket* testSocket;
    QTimer* statusTimer;
    
    // 재연결 스레드
    QThread* reconnectThread;
    bool shouldReconnect;
    
    // 연결 상태
    bool isConnected;
};

#endif // CLIENTDIALOG_H
