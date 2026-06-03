#pragma once

#include <QString>
#include <QList>

struct ImportClassificationRule
{
    QString partyPattern;
    QString account;
};

class AppConfig
{
public:
    static QString configFilePath();
    static QString accountingFile();
    static QString accountingFolder();
    static QString languageCode();
    static QList<ImportClassificationRule> importClassificationRules();
    static bool saveAccountingFile(const QString &filePath, QString *errorMessage = nullptr);
    static bool saveAccountingFolder(const QString &folderPath, QString *errorMessage = nullptr);
    static bool saveLanguageCode(const QString &languageCode, QString *errorMessage = nullptr);
    static bool saveImportClassificationRules(const QList<ImportClassificationRule> &rules,
                                              QString *errorMessage = nullptr);
};
