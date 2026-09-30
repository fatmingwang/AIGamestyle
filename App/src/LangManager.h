#pragma once
#include <QObject>
#include <QHash>
#include <QString>

enum class AppLang { EN, ZH_TW };

class LangManager : public QObject {
    Q_OBJECT
public:
    static LangManager& instance();
    void    setLang(AppLang l);
    AppLang lang()          const { return lang_; }
    QString get(const char* key)    const;
    QString get(const QString& key) const;
signals:
    void langChanged();
private:
    explicit LangManager(QObject* parent = nullptr);
    AppLang lang_ = AppLang::EN;
    QHash<QString, QString> zh_;
    void initTables();
};

// LS("English text") returns the text in the current language
#define LS(key) LangManager::instance().get(key)
