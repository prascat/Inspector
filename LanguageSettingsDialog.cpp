#include "LanguageSettingsDialog.h"
#include "LanguageManager.h"
#include "ConfigManager.h"
#include "CustomMessageBox.h"
#include <QMessageBox>
#include <QHBoxLayout>
#include <QDebug>

LanguageSettingsDialog::LanguageSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(TR("LANGUAGE_SETTINGS"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    resize(400, 200);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // 언어 선택에 대한 설명 레이블
    infoLabel = new QLabel(TR("SELECT_LANGUAGE_INFO"));
    infoLabel->setWordWrap(true);
    mainLayout->addWidget(infoLabel);
    
    // 언어 선택 콤보박스
    languageComboBox = new QComboBox(this);
    loadAvailableLanguages();
    mainLayout->addWidget(languageComboBox);
    
    // 현재 선택된 언어 표시
    QString currentLang = LanguageManager::instance()->currentLanguage();
    for (int i = 0; i < languageComboBox->count(); i++) {
        if (languageComboBox->itemData(i).toString() == currentLang) {
            languageComboBox->setCurrentIndex(i);
            break;
        }
    }
    
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    // 버튼 생성
    applyButton = new QPushButton(TR("APPLY"), this);
    cancelButton = new QPushButton(TR("CANCEL"), this);
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(cancelButton);
    
    mainLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
    
    // 시그널 연결
    connect(languageComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LanguageSettingsDialog::onLanguageSelected);
    connect(applyButton, &QPushButton::clicked, this, &LanguageSettingsDialog::onApplyClicked);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
     // 언어 변경 이벤트에 연결
    connect(LanguageManager::instance(), &LanguageManager::languageChanged, 
            this, &LanguageSettingsDialog::updateUITexts);
}

LanguageSettingsDialog::~LanguageSettingsDialog() {
}

int LanguageSettingsDialog::exec() {
    if (parentWidget()) {
        QWidget* topWindow = parentWidget()->window();
        QRect parentRect = topWindow->frameGeometry();
        
        int x = parentRect.x() + (parentRect.width() - width()) / 2;
        int y = parentRect.y() + (parentRect.height() - height()) / 2;
        
        int titleBarHeight = topWindow->frameGeometry().height() - topWindow->geometry().height();
        y -= titleBarHeight / 2;
        
        move(x, y);
    }
    
    return QDialog::exec();
}

void LanguageSettingsDialog::updateUITexts() {
    setWindowTitle(TR("LANGUAGE_SETTINGS"));
    infoLabel->setText(TR("SELECT_LANGUAGE_INFO"));
    applyButton->setText(TR("APPLY"));
    cancelButton->setText(TR("CANCEL"));
}

void LanguageSettingsDialog::loadAvailableLanguages() {
    languageComboBox->clear();
    
    QStringList languages = LanguageManager::instance()->availableLanguages();
    
    // 각 언어 코드에 대한 표시 이름 설정
    QMap<QString, QString> langDisplayNames;
    langDisplayNames["ko"] = "한국어 (Korean)";
    langDisplayNames["en"] = "English";
    langDisplayNames["ja"] = "日本語 (Japanese)";
    langDisplayNames["zh"] = "中文 (Chinese)";
    
    // 사용 가능한 언어를 콤보 박스에 추가
    for (const QString& langCode : languages) {
        QString displayName = langDisplayNames.value(langCode, langCode);
        languageComboBox->addItem(displayName, langCode);
    }
    
    // 현재 없는 경우 기본 언어 추가
    if (languageComboBox->count() == 0) {
        languageComboBox->addItem("한국어 (Korean)", "ko");
    }
}

void LanguageSettingsDialog::onLanguageSelected(int index) {
    // 미리보기 구현을 위한 함수 (필요 시)
}

void LanguageSettingsDialog::onApplyClicked() {
    int index = languageComboBox->currentIndex();
    if (index >= 0) {
        QString langCode = languageComboBox->itemData(index).toString();
        
        qDebug() << "[LanguageSettingsDialog] 언어 변경 요청:" << langCode;
        
        // 언어 변경 전에 번역 맵에 해당 언어가 있는지 확인
        bool containsLanguage = LanguageManager::instance()->containsLanguage(langCode);
        qDebug() << "[LanguageSettingsDialog] 언어 지원 여부:" << containsLanguage;
        
        // ConfigManager를 통해 언어 설정 저장 (자동으로 파일에 저장됨)
        ConfigManager::instance()->setLanguage(langCode);
        
        // LanguageManager에 언어 변경 적용
        LanguageManager::instance()->setCurrentLanguage(langCode);
        
        // 현재 언어 확인
        qDebug() << "[LanguageSettingsDialog] 현재 설정된 언어:" << LanguageManager::instance()->currentLanguage();
        
        // 현재 다이얼로그 UI 갱신
        updateUITexts();
        
        // 부모 위젯 즉시 업데이트 (TeachingWidget)
        if (parentWidget()) {
            QMetaObject::invokeMethod(parentWidget(), "updateUITexts", Qt::DirectConnection);
        }
        
        // 메시지 박스 표시
        CustomMessageBox msgBox(this);
        msgBox.setIcon(CustomMessageBox::Information);
        msgBox.setTitle(TR("LANGUAGE_CHANGED"));
        msgBox.setMessage(TR("LANGUAGE_CHANGE_RESTART_INFO"));
        msgBox.setButtons(QMessageBox::Ok);
        msgBox.exec();
    }
}