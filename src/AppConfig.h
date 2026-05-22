#pragma once

#include <QString>

class AppConfig
{
public:
    static QString configFilePath();
    static QString accountingFolder();
    static bool saveAccountingFolder(const QString &folderPath, QString *errorMessage = nullptr);
};
