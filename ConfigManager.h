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
    
    // 설정 파일 경로
    QString getConfigFilePath() const;
};

#endif // CONFIGMANAGER_H
