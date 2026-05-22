#pragma once

#include <QString>

class AppConfig
{
public:
    static QString configFilePath();
    static QString accountingFolder();
    static QString languageCode();
    static bool saveAccountingFolder(const QString &folderPath, QString *errorMessage = nullptr);
    static bool saveLanguageCode(const QString &languageCode, QString *errorMessage = nullptr);
};
