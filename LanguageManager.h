// LanguageManager.h
#ifndef LANGUAGEMANAGER_H
#define LANGUAGEMANAGER_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QStringList>

class LanguageManager : public QObject {
    Q_OBJECT
    
public:
    static LanguageManager* instance();
    const QMap<QString, QMap<QString, QString>>& getAllTranslations() const {
        return m_translations;
    }
    // 언어 코드가 번역 맵에 있는지 확인
    bool containsLanguage(const QString& langCode) const {
        return m_translations.contains(langCode);
    }

    // 언어 파일 로드
    bool loadLanguage(const QString& languageFile);
    
    // 현재 언어 설정
    void setCurrentLanguage(const QString& languageCode);
    
    // 번역 문자열 얻기
    QString getText(const QString& key) const;
    
    // 사용 가능한 언어 목록 가져오기
    QStringList availableLanguages() const;
    
    // 현재 언어 코드 얻기
    QString currentLanguage() const;
    
signals:
    void languageChanged();
    
private:
    LanguageManager(QObject* parent = nullptr);
    ~LanguageManager();
    
    static LanguageManager* m_instance;
    
    // 언어 코드별 번역 맵
    QMap<QString, QMap<QString, QString>> m_translations;
    
    // 현재 활성 언어 코드
    QString m_currentLanguage;
    
    // 각 언어 코드별 이름
    QMap<QString, QString> m_languageNames;

    // 번역 파일 경로
    QString m_translationPath;
};

// 편의를 위한 번역 매크로
#define TR(key) LanguageManager::instance()->getText(key)

#endif // LANGUAGEMANAGER_H