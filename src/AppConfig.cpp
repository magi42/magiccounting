#include "AppConfig.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace
{
const char *kConfigFileName = "config.json";
const char *kAccountingFolderKey = "accountingFolder";

QString configFolder()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!path.isEmpty()) {
        return path;
    }
    return QDir::home().filePath(".config/magiccounting");
}
}

QString AppConfig::configFilePath()
{
    return QDir(configFolder()).filePath(QString::fromLatin1(kConfigFileName));
}

QString AppConfig::accountingFolder()
{
    QFile file(configFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return QString();
    }
    return document.object().value(kAccountingFolderKey).toString();
}

bool AppConfig::saveAccountingFolder(const QString &folderPath, QString *errorMessage)
{
    QDir dir(configFolder());
    if (!dir.exists() && !dir.mkpath(".")) {
        if (errorMessage) {
            *errorMessage = QString("Could not create configuration folder %1").arg(dir.absolutePath());
        }
        return false;
    }

    QJsonObject object;
    object[kAccountingFolderKey] = folderPath;

    QFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QString("Could not write configuration file %1: %2").arg(configFilePath(), file.errorString());
        }
        return false;
    }

    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return true;
}
