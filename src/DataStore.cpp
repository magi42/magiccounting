#include "DataStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
const char *kDefaultAccountingFile = "accounting.macc";

QString pathInFolder(const QString &folder, const char *fileName)
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

QJsonObject accountToJson(const Account &account)
{
    QJsonObject object;
    object["name"] = account.name;
    object["kind"] = account.kind;
    object["openingBalance"] = account.openingBalance;
    return object;
}

QJsonObject partyToJson(const Party &party)
{
    QJsonObject object;
    object["name"] = party.name;
    return object;
}

QJsonObject transactionToJson(const Transaction &transaction)
{
    QJsonObject object;
    object["date"] = transaction.date.toString(Qt::ISODate);
    object["sourceAccount"] = transaction.sourceAccount;
    object["amount"] = transaction.amount;
    object["party"] = transaction.party;
    object["memo"] = transaction.memo;
    if (!transaction.importSource.isEmpty() && !transaction.importId.isEmpty()) {
        object["importSource"] = transaction.importSource;
        object["importId"] = transaction.importId;
    }

    QJsonArray targets;
    for (const Split &split : transaction.targets) {
        QJsonObject target;
        target["account"] = split.account;
        target["amount"] = split.amount;
        targets.append(target);
    }
    object["targets"] = targets;
    return object;
}
}

DataStore::DataStore(const QString &filePath)
    : m_filePath(filePath.isEmpty() ? defaultAccountingFile() : filePath)
{
}

const QString &DataStore::filePath() const
{
    return m_filePath;
}

QString DataStore::defaultAccountingFile()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!base.isEmpty()) {
        return QDir(base).filePath(QString::fromLatin1(kDefaultAccountingFile));
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath(QString::fromLatin1(kDefaultAccountingFile));
}

QString DataStore::fileFilter()
{
    return QCoreApplication::translate("DataStore", "Magic Counting files (*.macc);;All files (*)");
}

bool DataStore::saveAs(const QString &filePath, QString *errorMessage)
{
    if (filePath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate("DataStore", "No accounting file selected");
        }
        return false;
    }

    QString normalizedPath = filePath;
    if (QFileInfo(normalizedPath).suffix().isEmpty()) {
        normalizedPath.append(".macc");
    }

    const QString previousPath = m_filePath;
    m_filePath = normalizedPath;
    if (!saveAll(errorMessage)) {
        m_filePath = previousPath;
        return false;
    }
    return true;
}

bool DataStore::load(const QString &filePath, QString *errorMessage)
{
    m_filePath = filePath;
    QJsonArray accountArray;
    QJsonArray partyArray;
    QJsonArray transactionArray;

    const QFileInfo info(m_filePath);
    if (info.isDir()) {
        if (!readJsonArray(pathInFolder(m_filePath, kAccountsFile), &accountArray, errorMessage)
            || !readJsonArray(pathInFolder(m_filePath, kPartiesFile), &partyArray, errorMessage)
            || !readJsonArray(pathInFolder(m_filePath, kTransactionsFile), &transactionArray, errorMessage)) {
            return false;
        }
        m_filePath = QDir(m_filePath).filePath(QString::fromLatin1(kDefaultAccountingFile));
    } else {
        QFile file(m_filePath);
        if (!file.exists()) {
            seedDefaults();
            return saveAll(errorMessage);
        }
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not open %1: %2")
                                    .arg(m_filePath, file.errorString());
            }
            return false;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not parse %1 as a Magic Counting file: %2")
                                    .arg(m_filePath, parseError.errorString());
            }
            return false;
        }

        const QJsonObject root = document.object();
        accountArray = root.value("accounts").toArray();
        partyArray = root.value("parties").toArray();
        transactionArray = root.value("transactions").toArray();
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
        transaction.importSource = object.value("importSource").toString();
        transaction.importId = object.value("importId").toString();

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
    if (!ensureParentFolder(errorMessage)) {
        return false;
    }

    QJsonArray accountArray;
    for (const Account &account : accounts) {
        accountArray.append(accountToJson(account));
    }

    QJsonArray partyArray;
    for (const Party &party : parties) {
        partyArray.append(partyToJson(party));
    }

    QJsonArray transactionArray;
    for (const Transaction &transaction : transactions) {
        transactionArray.append(transactionToJson(transaction));
    }

    QJsonObject root;
    root["format"] = "magiccounting";
    root["version"] = 1;
    root["accounts"] = accountArray;
    root["parties"] = partyArray;
    root["transactions"] = transactionArray;

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate("DataStore", "Could not write %1: %2")
                                .arg(m_filePath, file.errorString());
        }
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool DataStore::saveAccounts(QString *errorMessage) const
{
    return saveAll(errorMessage);
}

bool DataStore::saveParties(QString *errorMessage) const
{
    return saveAll(errorMessage);
}

bool DataStore::saveTransactions(QString *errorMessage) const
{
    return saveAll(errorMessage);
}

bool DataStore::hasImportedTransaction(const QString &importSource, const QString &importId) const
{
    if (importSource.isEmpty() || importId.isEmpty()) {
        return false;
    }
    for (const Transaction &transaction : transactions) {
        if (transaction.importSource == importSource && transaction.importId == importId) {
            return true;
        }
    }
    return false;
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

bool DataStore::ensureParentFolder(QString *errorMessage) const
{
    QDir dir(QFileInfo(m_filePath).absolutePath());
    if (dir.exists()) {
        return true;
    }
    if (dir.mkpath(".")) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QCoreApplication::translate("DataStore", "Could not create data folder %1").arg(dir.absolutePath());
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
