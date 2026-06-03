#pragma once

#include <QString>

class AppConfig
{
public:
    static QString configFilePath();
    static QString accountingFile();
    static QString accountingFolder();
    static QString languageCode();
    static bool saveAccountingFile(const QString &filePath, QString *errorMessage = nullptr);
    static bool saveAccountingFolder(const QString &folderPath, QString *errorMessage = nullptr);
    static bool saveLanguageCode(const QString &languageCode, QString *errorMessage = nullptr);
};
