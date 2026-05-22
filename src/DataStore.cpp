#include "DataStore.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTextStream>

namespace
{
const char *kAccountsFile = "accounts.json";
const char *kPartiesFile = "parties.json";
const char *kTransactionsFile = "transactions.json";

QString filePath(const QString &folder, const char *fileName)
{
    return QDir(folder).filePath(QString::fromLatin1(fileName));
}

bool readJsonArray(const QString &path, QJsonArray *array, QString *errorMessage)
{
    QFile file(path);
    if (!file.exists()) {
        *array = QJsonArray();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate("DataStore", "Could not open %1: %2")
                                .arg(path, file.errorString());
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate("DataStore", "Could not parse %1 as a JSON array: %2")
                                .arg(path, parseError.errorString());
        }
        return false;
    }

    *array = document.array();
    return true;
}

bool writeJsonArray(const QString &path, const QJsonArray &array, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate("DataStore", "Could not write %1: %2")
                                .arg(path, file.errorString());
        }
        return false;
    }

    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    return true;
}
}

DataStore::DataStore(const QString &folderPath)
    : m_folderPath(folderPath.isEmpty() ? defaultDataFolder() : folderPath)
{
}

const QString &DataStore::folderPath() const
{
    return m_folderPath;
}

QString DataStore::defaultDataFolder()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!base.isEmpty()) {
        return base;
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath("data");
}

bool DataStore::load(const QString &folderPath, QString *errorMessage)
{
    m_folderPath = folderPath;
    if (!ensureFolder(errorMessage)) {
        return false;
    }

    QJsonArray accountArray;
    QJsonArray partyArray;
    QJsonArray transactionArray;
    if (!readJsonArray(filePath(m_folderPath, kAccountsFile), &accountArray, errorMessage)
        || !readJsonArray(filePath(m_folderPath, kPartiesFile), &partyArray, errorMessage)
        || !readJsonArray(filePath(m_folderPath, kTransactionsFile), &transactionArray, errorMessage)) {
        return false;
    }

    accounts.clear();
    for (const QJsonValue &value : accountArray) {
        const QJsonObject object = value.toObject();
        accounts.append({object.value("name").toString(),
                         object.value("kind").toString("category"),
                         object.value("openingBalance").toDouble()});
    }

    parties.clear();
    for (const QJsonValue &value : partyArray) {
        const QJsonObject object = value.toObject();
        parties.append({object.value("name").toString()});
    }

    transactions.clear();
    for (const QJsonValue &value : transactionArray) {
        const QJsonObject object = value.toObject();
        Transaction transaction;
        transaction.date = QDate::fromString(object.value("date").toString(), Qt::ISODate);
        if (!transaction.date.isValid()) {
            transaction.date = QDate::currentDate();
        }
        transaction.sourceAccount = object.value("sourceAccount").toString();
        transaction.amount = object.value("amount").toDouble();
        transaction.party = object.value("party").toString();
        transaction.memo = object.value("memo").toString();

        const QJsonArray targetArray = object.value("targets").toArray();
        for (const QJsonValue &targetValue : targetArray) {
            const QJsonObject target = targetValue.toObject();
            transaction.targets.append({target.value("account").toString(), target.value("amount").toDouble()});
        }
        if (transaction.amount == 0.0) {
            for (const Split &split : transaction.targets) {
                transaction.amount += split.amount;
            }
        }
        transactions.append(transaction);
    }

    if (accounts.isEmpty() && parties.isEmpty() && transactions.isEmpty()) {
        seedDefaults();
        return saveAll(errorMessage);
    }

    return true;
}

bool DataStore::saveAll(QString *errorMessage) const
{
    return saveAccounts(errorMessage) && saveParties(errorMessage) && saveTransactions(errorMessage);
}

bool DataStore::saveAccounts(QString *errorMessage) const
{
    if (!ensureFolder(errorMessage)) {
        return false;
    }

    QJsonArray array;
    for (const Account &account : accounts) {
        QJsonObject object;
        object["name"] = account.name;
        object["kind"] = account.kind;
        object["openingBalance"] = account.openingBalance;
        array.append(object);
    }
    return writeJsonArray(filePath(m_folderPath, kAccountsFile), array, errorMessage);
}

bool DataStore::saveParties(QString *errorMessage) const
{
    if (!ensureFolder(errorMessage)) {
        return false;
    }

    QJsonArray array;
    for (const Party &party : parties) {
        QJsonObject object;
        object["name"] = party.name;
        array.append(object);
    }
    return writeJsonArray(filePath(m_folderPath, kPartiesFile), array, errorMessage);
}

bool DataStore::saveTransactions(QString *errorMessage) const
{
    if (!ensureFolder(errorMessage)) {
        return false;
    }

    QJsonArray array;
    for (const Transaction &transaction : transactions) {
        QJsonObject object;
        object["date"] = transaction.date.toString(Qt::ISODate);
        object["sourceAccount"] = transaction.sourceAccount;
        object["amount"] = transaction.amount;
        object["party"] = transaction.party;
        object["memo"] = transaction.memo;

        QJsonArray targets;
        for (const Split &split : transaction.targets) {
            QJsonObject target;
            target["account"] = split.account;
            target["amount"] = split.amount;
            targets.append(target);
        }
        object["targets"] = targets;
        array.append(object);
    }
    return writeJsonArray(filePath(m_folderPath, kTransactionsFile), array, errorMessage);
}

QList<Split> DataStore::parseSplits(const QString &text, bool *ok)
{
    QList<Split> splits;
    bool parsed = true;

    const QStringList parts = text.split(';', Qt::SkipEmptyParts);
    for (const QString &rawPart : parts) {
        const QString part = rawPart.trimmed();
        const int separator = part.lastIndexOf(':');
        if (separator <= 0 || separator == part.size() - 1) {
            parsed = false;
            continue;
        }

        bool amountOk = false;
        const QString account = part.left(separator).trimmed();
        const double amount = part.mid(separator + 1).trimmed().toDouble(&amountOk);
        if (account.isEmpty() || !amountOk) {
            parsed = false;
            continue;
        }
        splits.append({account, amount});
    }

    if (ok) {
        *ok = parsed && !splits.isEmpty();
    }
    return splits;
}

QString DataStore::formatSplits(const QList<Split> &splits)
{
    QStringList parts;
    for (const Split &split : splits) {
        parts.append(QString("%1:%2").arg(split.account, QString::number(split.amount, 'f', 2)));
    }
    return parts.join("; ");
}

bool DataStore::ensureFolder(QString *errorMessage) const
{
    QDir dir(m_folderPath);
    if (dir.exists()) {
        return true;
    }
    if (dir.mkpath(".")) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QCoreApplication::translate("DataStore", "Could not create data folder %1").arg(m_folderPath);
    }
    return false;
}

void DataStore::seedDefaults()
{
    accounts = {
        {"bank", "source", 0.0},
        {"cash", "source", 0.0},
        {"home", "category", 0.0},
        {"car", "category", 0.0},
        {"food", "category", 0.0},
        {"income", "category", 0.0},
    };
    parties = {
        {"Grocery shop"},
        {"Fuel station"},
        {"Employer"},
    };
    transactions = {
        {QDate::currentDate(), "bank", 24.90, "Grocery shop", {{"food", 24.90}}, "Weekly groceries"},
        {QDate::currentDate(), "cash", 15.70, "Fuel station", {{"car", 12.50}, {"food", 3.20}}, "Fuel and snack"},
    };
}
