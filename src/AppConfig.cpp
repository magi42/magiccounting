#include "AppConfig.h"

#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace
{
const char *kConfigFileName = "config.json";
const char *kAccountingFolderKey = "accountingFolder";
const char *kLanguageKey = "language";

QString configFolder()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!path.isEmpty()) {
        return path;
    }
    return QDir::home().filePath(".config/magiccounting");
}

QJsonObject readConfig()
{
    QFile file(AppConfig::configFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QJsonObject();
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return QJsonObject();
    }
    return document.object();
}

bool writeConfig(const QJsonObject &object, QString *errorMessage)
{
    QDir dir(configFolder());
    if (!dir.exists() && !dir.mkpath(".")) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate("AppConfig", "Could not create configuration folder %1")
                                .arg(dir.absolutePath());
        }
        return false;
    }

    QFile file(AppConfig::configFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate("AppConfig", "Could not write configuration file %1: %2")
                                .arg(AppConfig::configFilePath(), file.errorString());
        }
        return false;
    }

    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return true;
}
}

QString AppConfig::configFilePath()
{
    return QDir(configFolder()).filePath(QString::fromLatin1(kConfigFileName));
}

QString AppConfig::accountingFolder()
{
    return readConfig().value(kAccountingFolderKey).toString();
}

QString AppConfig::languageCode()
{
    const QString code = readConfig().value(kLanguageKey).toString();
    return code.isEmpty() ? QStringLiteral("system") : code;
}

bool AppConfig::saveAccountingFolder(const QString &folderPath, QString *errorMessage)
{
    QJsonObject object = readConfig();
    object[kAccountingFolderKey] = folderPath;
    return writeConfig(object, errorMessage);
}

bool AppConfig::saveLanguageCode(const QString &languageCode, QString *errorMessage)
{
    QJsonObject object = readConfig();
    object[kLanguageKey] = languageCode;
    return writeConfig(object, errorMessage);
}
