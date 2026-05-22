#pragma once

#include <QString>

class QApplication;

class LanguageManager
{
public:
    static QString normalizeLanguageCode(const QString &languageCode);
    static QString effectiveLanguageCode(const QString &languageCode);
    static bool install(QApplication *app, const QString &languageCode);
};
