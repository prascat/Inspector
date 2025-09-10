// LanguageManager.cpp
#include "LanguageManager.h"
#include <QFile>
#include <QXmlStreamReader>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>

LanguageManager* LanguageManager::m_instance = nullptr;

LanguageManager* LanguageManager::instance() {
    if (!m_instance) {
        m_instance = new LanguageManager();
    }
    return m_instance;
}

LanguageManager::LanguageManager(QObject* parent) : QObject(parent) {
    // 기본 언어를 한국어로 설정
    m_currentLanguage = "ko";
    
    // 번역 파일 경로 설정
    m_translationPath = QCoreApplication::applicationDirPath();
}

LanguageManager::~LanguageManager() {
    // 클린업 코드
}

bool LanguageManager::loadLanguage(const QString& languageFile) {
    
    QFile file(languageFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    
    QXmlStreamReader xml(&file);
    
    // 언어 파일 초기화
    m_translations.clear();
    m_languageNames.clear();
    
    // 지원하는 언어 코드들
    QStringList supportedLanguages = {"ko", "en", "ja", "zh"};
    
    // 언어 이름 초기화
    m_languageNames["ko"] = "한국어";
    m_languageNames["en"] = "English";
    m_languageNames["ja"] = "日本語";
    m_languageNames["zh"] = "中文";
    
    // 각 언어별 번역 맵 초기화
    for (const QString& lang : supportedLanguages) {
        m_translations[lang] = QMap<QString, QString>();
    }
    
    try {
        // XML 시작 확인
        if (!xml.readNextStartElement()) {
            throw QString("XML 시작 요소를 읽을 수 없습니다.");
        }
        
        if (xml.name() != "LanguageStrings") {
            throw QString("'LanguageStrings' 요소를 찾을 수 없습니다.");
        }

        // 언어별 문자열 읽기
        while (xml.readNextStartElement()) {
            if (xml.name() == "String") {
                QString key = xml.attributes().value("key").toString();
                
                if (key.isEmpty()) {
                    xml.skipCurrentElement();
                    continue;
                }
                
                // 이 키에 대한 각 언어별 번역 읽기
                while (xml.readNextStartElement()) {
                    QString langCode = xml.name().toString();
                    QString value = xml.readElementText();
                    
                    // 지원하는 언어인 경우에만 맵에 추가
                    if (supportedLanguages.contains(langCode)) {
                        m_translations[langCode][key] = value;
                    }
                }
                
            } else {
                xml.skipCurrentElement();
            }
            
            // XML 에러 체크
            if (xml.hasError()) {
                break;
            }
        }
        
    } catch (const QString& error) {
        file.close();
        return false;
    }
    
    if (xml.hasError()) {
        file.close();
        return false;
    }
    
    file.close();
    
    // 언어가 로드되었고 현재 언어가 유효하지 않으면 첫 번째 언어로 설정
    if (!m_translations.isEmpty() && !m_translations.contains(m_currentLanguage)) {
        m_currentLanguage = m_translations.keys().first();
        emit languageChanged();
    }

    // 호환성: DRAW_MODE/MOVE_MODE 키가 존재하면 DRAW/MOVE로도 사용 가능하도록 alias 추가
    for (auto it = m_translations.begin(); it != m_translations.end(); ++it) {
        QMap<QString, QString>& map = it.value();
        if (map.contains("DRAW_MODE") && !map.contains("DRAW")) {
            map["DRAW"] = map["DRAW_MODE"];
        }
        if (map.contains("MOVE_MODE") && !map.contains("MOVE")) {
            map["MOVE"] = map["MOVE_MODE"];
        }
    }
    
    return !m_translations.isEmpty();
}

void LanguageManager::setCurrentLanguage(const QString& languageCode) {
    
    if (m_translations.contains(languageCode) && m_currentLanguage != languageCode) {
        m_currentLanguage = languageCode;
        emit languageChanged();
    } else {
        
        // 기본 언어인 "ko"가 맵에 없는 경우 강제로 설정
        if (languageCode == "ko" && !m_translations.contains("ko") && !m_translations.isEmpty()) {
            m_currentLanguage = m_translations.keys().first();
            emit languageChanged();
        }
    }
}

QString LanguageManager::getText(const QString& key) const {
    // 현재 언어에서 키 찾기
    if (m_translations.contains(m_currentLanguage)) {
        const QMap<QString, QString>& strings = m_translations[m_currentLanguage];
        if (strings.contains(key)) {
            QString result = strings[key];
            // \n을 실제 개행 문자로 변환
            result.replace("\\n", "\n");
            return result;
        }
    }
    
    // 현재 언어에 없으면 한국어(ko)에서 찾기
    if (m_currentLanguage != "ko" && m_translations.contains("ko")) {
        const QMap<QString, QString>& koStrings = m_translations["ko"];
        if (koStrings.contains(key)) {
            QString result = koStrings[key];
            // \n을 실제 개행 문자로 변환
            result.replace("\\n", "\n");
            return result;
        }
    }
    
    // 키를 찾을 수 없으면 키 그대로 반환
    return key;
}

QStringList LanguageManager::availableLanguages() const {
    // 직접 언어 목록 반환
    QStringList languages;
    for (auto it = m_translations.begin(); it != m_translations.end(); ++it) {
        if (!it.value().isEmpty()) {
            languages << it.key();
        }
    }
    
    // 만약 비어있다면 기본값 제공
    if (languages.isEmpty()) {
        languages << "ko" << "en" << "ja" << "zh";
    }
    
    return languages;
}

QString LanguageManager::currentLanguage() const {
    return m_currentLanguage;
}