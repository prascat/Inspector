#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QString>
#include <QVariant>

class ConfigManager : public QObject
{
    Q_OBJECT

public:
    static ConfigManager* instance();
    
    // 설정 파일 로드/저장
    bool loadConfig();
    bool saveConfig();
    
    // 설정값 접근
    QString getLanguage() const;
    void setLanguage(const QString& language);
    
    // 기타 설정값들 (필요에 따라 추가)
    bool getAutoSave() const;
    void setAutoSave(bool enabled);
    
    QString getLastRecipePath() const;
    void setLastRecipePath(const QString& path);
    
    // 시리얼 통신 설정
    QString getSerialPort() const;
    void setSerialPort(const QString& port);
    
    int getSerialBaudRate() const;
    void setSerialBaudRate(int baudRate);
    
    // 서버 연결 설정
    QString getServerIp() const;
    void setServerIp(const QString& ip);
    
    int getServerPort() const;
    void setServerPort(int port);
    
    bool getAutoConnect() const;
    void setAutoConnect(bool enable);
    
    int getReconnectInterval() const;
    void setReconnectInterval(int seconds);
    
    // 카메라 자동 연결 설정
    bool getCameraAutoConnect() const;
    void setCameraAutoConnect(bool enable);
    
signals:
    void configChanged();
    void languageChanged(const QString& newLanguage);

private:
    ConfigManager(QObject* parent = nullptr);
    ~ConfigManager();
    
    static ConfigManager* m_instance;
    
    // 설정값들
    QString m_language;
    bool m_autoSave;
    QString m_lastRecipePath;
    QString m_serialPort;
    int m_serialBaudRate;
    QString m_serverIp;
    int m_serverPort;
    bool m_autoConnect;
    int m_reconnectInterval;
    bool m_cameraAutoConnect;
    
    // 설정 파일 경로
    QString getConfigFilePath() const;
};

#endif // CONFIGMANAGER_H
