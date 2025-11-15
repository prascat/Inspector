#include "ConfigManager.h"
#include "CommonDefs.h"
#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QDebug>
#include <QCoreApplication>
#include <QDir>

ConfigManager* ConfigManager::m_instance = nullptr;

ConfigManager* ConfigManager::instance() {
    if (!m_instance) {
        m_instance = new ConfigManager();
    }
    return m_instance;
}

ConfigManager::ConfigManager(QObject* parent) : QObject(parent) {
    // 기본값 설정
    m_language = "ko";  // 기본 언어는 한국어
    m_autoSave = true;
    m_lastRecipePath = "";
    m_serialPort = "";
    m_serialBaudRate = 115200;  // 기본 보드레이트
    m_serverIp = "127.0.0.1";  // 기본 서버 IP
    m_serverPort = 5000;  // 기본 서버 포트
    m_autoConnect = false;  // 기본 자동 연결 비활성화
    m_reconnectInterval = 10;  // 기본 재연결 간격 10초
}

ConfigManager::~ConfigManager() {
    // 소멸자에서 설정 저장
    saveConfig();
}

QString ConfigManager::getConfigFilePath() const {
    // 실행 파일과 같은 디렉토리에 CONFIG_FILE 생성
    return QCoreApplication::applicationDirPath() + "/" + CONFIG_FILE;
}

bool ConfigManager::loadConfig() {
    QString configPath = getConfigFilePath();
    
    QFile file(configPath);
    if (!file.exists()) {
        qDebug() << "[ConfigManager] 설정 파일이 없습니다. 기본값 사용:" << configPath;
        return true; // 파일이 없어도 기본값으로 정상 작동
    }
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "[ConfigManager] 설정 파일 열기 실패:" << configPath;
        return false;
    }
    
    QXmlStreamReader xml(&file);
    
    try {
        // XML 시작 확인
        if (!xml.readNextStartElement()) {
            throw QString("XML 시작 요소를 읽을 수 없습니다.");
        }
        
        if (xml.name() != QLatin1String("Config")) {
            throw QString("'Config' 요소를 찾을 수 없습니다.");
        }
        
        // 설정 읽기
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("Language")) {
                m_language = xml.readElementText();
                qDebug() << "[ConfigManager] 언어 설정 로드됨:" << m_language;
            } else if (xml.name() == QLatin1String("AutoSave")) {
                QString value = xml.readElementText();
                m_autoSave = (value.toLower() == "true");
                qDebug() << "[ConfigManager] 자동저장 설정 로드됨:" << m_autoSave;
            } else if (xml.name() == QLatin1String("LastRecipePath")) {
                m_lastRecipePath = xml.readElementText();
                qDebug() << "[ConfigManager] 마지막 레시피 경로 로드됨:" << m_lastRecipePath;
            } else if (xml.name() == QLatin1String("SerialPort")) {
                m_serialPort = xml.readElementText();
                qDebug() << "[ConfigManager] 시리얼 포트 로드됨:" << m_serialPort;
            } else if (xml.name() == QLatin1String("SerialBaudRate")) {
                m_serialBaudRate = xml.readElementText().toInt();
                qDebug() << "[ConfigManager] 시리얼 보드레이트 로드됨:" << m_serialBaudRate;
            } else if (xml.name() == QLatin1String("ServerIp")) {
                m_serverIp = xml.readElementText();
                qDebug() << "[ConfigManager] 서버 IP 로드됨:" << m_serverIp;
            } else if (xml.name() == QLatin1String("ServerPort")) {
                m_serverPort = xml.readElementText().toInt();
                qDebug() << "[ConfigManager] 서버 포트 로드됨:" << m_serverPort;
            } else if (xml.name() == QLatin1String("AutoConnect")) {
                QString value = xml.readElementText();
                m_autoConnect = (value.toLower() == "true");
                qDebug() << "[ConfigManager] 자동 연결 설정 로드됨:" << m_autoConnect;
            } else if (xml.name() == QLatin1String("ReconnectInterval")) {
                m_reconnectInterval = xml.readElementText().toInt();
                if (m_reconnectInterval < 1) m_reconnectInterval = 10;
                qDebug() << "[ConfigManager] 재연결 간격 로드됨:" << m_reconnectInterval << "초";
            } else {
                xml.skipCurrentElement();
            }
            
            // XML 에러 체크
            if (xml.hasError()) {
                break;
            }
        }
        
    } catch (const QString& error) {
        qDebug() << "[ConfigManager] 설정 파일 로드 중 오류:" << error;
        file.close();
        return false;
    }
    
    if (xml.hasError()) {
        qDebug() << "[ConfigManager] XML 파싱 오류:" << xml.errorString();
        file.close();
        return false;
    }
    
    file.close();
    qDebug() << "[ConfigManager] 설정 파일 로드 완료";
    return true;
}

bool ConfigManager::saveConfig() {
    QString configPath = getConfigFilePath();
    
    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "[ConfigManager] 설정 파일 저장 실패:" << configPath;
        return false;
    }
    
    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    
    xml.writeStartElement("Config");
    
    // 언어 설정 저장
    xml.writeTextElement("Language", m_language);
    
    // 자동저장 설정 저장
    xml.writeTextElement("AutoSave", m_autoSave ? "true" : "false");
    
    // 마지막 레시피 경로 저장
    if (!m_lastRecipePath.isEmpty()) {
        xml.writeTextElement("LastRecipePath", m_lastRecipePath);
    }
    
    // 시리얼 통신 설정 저장
    if (!m_serialPort.isEmpty()) {
        xml.writeTextElement("SerialPort", m_serialPort);
    }
    xml.writeTextElement("SerialBaudRate", QString::number(m_serialBaudRate));
    
    // 서버 연결 설정 저장
    xml.writeTextElement("ServerIp", m_serverIp);
    xml.writeTextElement("ServerPort", QString::number(m_serverPort));
    xml.writeTextElement("AutoConnect", m_autoConnect ? "true" : "false");
    xml.writeTextElement("ReconnectInterval", QString::number(m_reconnectInterval));
    
    xml.writeEndElement(); // Config
    xml.writeEndDocument();
    
    file.close();
    
    qDebug() << "[ConfigManager] 설정 파일 저장 완료:" << configPath;
    return true;
}

QString ConfigManager::getLanguage() const {
    return m_language;
}

void ConfigManager::setLanguage(const QString& language) {
    if (m_language != language) {
        m_language = language;
        saveConfig(); // 즉시 저장
        emit languageChanged(language);
        emit configChanged();
        qDebug() << "[ConfigManager] 언어 설정 변경됨:" << language;
    }
}

bool ConfigManager::getAutoSave() const {
    return m_autoSave;
}

void ConfigManager::setAutoSave(bool enabled) {
    if (m_autoSave != enabled) {
        m_autoSave = enabled;
        saveConfig(); // 즉시 저장
        emit configChanged();
        qDebug() << "[ConfigManager] 자동저장 설정 변경됨:" << enabled;
    }
}

QString ConfigManager::getLastRecipePath() const {
    return m_lastRecipePath;
}

void ConfigManager::setLastRecipePath(const QString& path) {
    if (m_lastRecipePath != path) {
        m_lastRecipePath = path;
        saveConfig(); // 즉시 저장
        emit configChanged();
        qDebug() << "[ConfigManager] 마지막 레시피 경로 변경됨:" << path;
    }
}

QString ConfigManager::getSerialPort() const {
    return m_serialPort;
}

void ConfigManager::setSerialPort(const QString& port) {
    if (m_serialPort != port) {
        m_serialPort = port;
        saveConfig(); // 즉시 저장
        emit configChanged();
        qDebug() << "[ConfigManager] 시리얼 포트 변경됨:" << port;
    }
}

int ConfigManager::getSerialBaudRate() const {
    return m_serialBaudRate;
}

void ConfigManager::setSerialBaudRate(int baudRate) {
    if (m_serialBaudRate != baudRate) {
        m_serialBaudRate = baudRate;
        saveConfig(); // 즉시 저장
        emit configChanged();
        qDebug() << "[ConfigManager] 시리얼 보드레이트 변경됨:" << baudRate;
    }
}

QString ConfigManager::getServerIp() const {
    return m_serverIp;
}

void ConfigManager::setServerIp(const QString& ip) {
    if (m_serverIp != ip) {
        m_serverIp = ip;
        emit configChanged();
        qDebug() << "[ConfigManager] 서버 IP 변경됨:" << ip;
    }
}

int ConfigManager::getServerPort() const {
    return m_serverPort;
}

void ConfigManager::setServerPort(int port) {
    if (m_serverPort != port) {
        m_serverPort = port;
        emit configChanged();
        qDebug() << "[ConfigManager] 서버 포트 변경됨:" << port;
    }
}

bool ConfigManager::getAutoConnect() const {
    return m_autoConnect;
}

void ConfigManager::setAutoConnect(bool enable) {
    if (m_autoConnect != enable) {
        m_autoConnect = enable;
        emit configChanged();
        qDebug() << "[ConfigManager] 자동 연결 설정 변경됨:" << enable;
    }
}

int ConfigManager::getReconnectInterval() const {
    return m_reconnectInterval;
}

void ConfigManager::setReconnectInterval(int seconds) {
    if (m_reconnectInterval != seconds && seconds >= 1) {
        m_reconnectInterval = seconds;
        emit configChanged();
        qDebug() << "[ConfigManager] 재연결 간격 변경됨:" << seconds << "초";
    }
}
