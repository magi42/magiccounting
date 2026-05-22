#include "LanguageManager.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace
{
QTranslator *appTranslator = nullptr;
QTranslator *qtTranslator = nullptr;

QString translationsDir()
{
    return QStringLiteral(MAGICCOUNTING_TRANSLATIONS_DIR);
}
}

QString LanguageManager::normalizeLanguageCode(const QString &languageCode)
{
    if (languageCode == QStringLiteral("fi") || languageCode == QStringLiteral("en")) {
        return languageCode;
    }
    return QStringLiteral("system");
}

QString LanguageManager::effectiveLanguageCode(const QString &languageCode)
{
    const QString normalized = normalizeLanguageCode(languageCode);
    if (normalized != QStringLiteral("system")) {
        return normalized;
    }

    return QLocale::system().language() == QLocale::Finnish ? QStringLiteral("fi") : QStringLiteral("en");
}

bool LanguageManager::install(QApplication *app, const QString &languageCode)
{
    if (appTranslator) {
        app->removeTranslator(appTranslator);
        delete appTranslator;
        appTranslator = nullptr;
    }
    if (qtTranslator) {
        app->removeTranslator(qtTranslator);
        delete qtTranslator;
        qtTranslator = nullptr;
    }

    const QString effectiveCode = effectiveLanguageCode(languageCode);
    QLocale::setDefault(effectiveCode == QStringLiteral("fi") ? QLocale(QLocale::Finnish, QLocale::Finland) : QLocale::system());

    bool loaded = true;
    if (effectiveCode == QStringLiteral("fi")) {
        appTranslator = new QTranslator(app);
        loaded = appTranslator->load(QStringLiteral("magiccounting_fi"), translationsDir());
        if (loaded) {
            app->installTranslator(appTranslator);
        } else {
            delete appTranslator;
            appTranslator = nullptr;
        }

        qtTranslator = new QTranslator(app);
        if (qtTranslator->load(QLocale(QLocale::Finnish, QLocale::Finland),
                               QStringLiteral("qtbase"),
                               QStringLiteral("_"),
                               QLibraryInfo::location(QLibraryInfo::TranslationsPath))) {
            app->installTranslator(qtTranslator);
        } else {
            delete qtTranslator;
            qtTranslator = nullptr;
        }
    }

    return loaded;
}
