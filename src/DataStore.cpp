#include "DataStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QUuid>
#include <QVariant>

namespace
{
const char *kAccountsFile = "accounts.json";
const char *kPartiesFile = "parties.json";
const char *kTransactionsFile = "transactions.json";
const char *kDefaultAccountingFile = "accounting.maccd";
const char *kDatabaseSuffix = "maccd";

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
    if (!party.defaultAccount.isEmpty()) {
        object["defaultAccount"] = party.defaultAccount;
    }
    return object;
}

QJsonObject transactionToJson(const Transaction &transaction)
{
    QJsonObject object;
    if (transaction.id > 0) {
        object["id"] = QString::number(transaction.id);
    }
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

bool isDatabasePath(const QString &path)
{
    return QFileInfo(path).suffix().compare(QString::fromLatin1(kDatabaseSuffix), Qt::CaseInsensitive) == 0;
}

QString sqlError(const QSqlQuery &query)
{
    return query.lastError().text();
}

QString sqlError(const QSqlDatabase &database)
{
    return database.lastError().text();
}

class ScopedSqlConnection
{
public:
    ScopedSqlConnection(const QString &filePath, QString *errorMessage)
        : name(QStringLiteral("magiccounting-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
        , database(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name))
    {
        database.setDatabaseName(filePath);
        if (!database.open()) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not open database %1: %2")
                                    .arg(filePath, sqlError(database));
            }
            opened = false;
        }
    }

    ~ScopedSqlConnection()
    {
        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }

    bool opened = true;
    QString name;
    QSqlDatabase database;
};

bool execSql(QSqlDatabase &database, const QString &sql, QString *errorMessage)
{
    QSqlQuery query(database);
    if (query.exec(sql)) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QCoreApplication::translate("DataStore", "Database error: %1").arg(sqlError(query));
    }
    return false;
}

bool columnExists(QSqlDatabase &database, const QString &table, const QString &column, QString *errorMessage)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate("DataStore", "Database error: %1").arg(sqlError(query));
        }
        return false;
    }
    while (query.next()) {
        if (query.value(1).toString() == column) {
            return true;
        }
    }
    return false;
}

bool createSchema(QSqlDatabase &database, QString *errorMessage)
{
    if (!(execSql(database, QStringLiteral("PRAGMA foreign_keys = ON"), errorMessage)
        && execSql(database, QStringLiteral("CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL)"), errorMessage)
        && execSql(database, QStringLiteral("CREATE TABLE IF NOT EXISTS accounts (name TEXT PRIMARY KEY, kind TEXT NOT NULL, opening_balance REAL NOT NULL DEFAULT 0)"), errorMessage)
        && execSql(database, QStringLiteral("CREATE TABLE IF NOT EXISTS parties (name TEXT PRIMARY KEY, default_account TEXT)"), errorMessage)
        && execSql(database, QStringLiteral("CREATE TABLE IF NOT EXISTS transactions (id INTEGER PRIMARY KEY AUTOINCREMENT, date TEXT NOT NULL, source_account TEXT NOT NULL, amount REAL NOT NULL, party TEXT, memo TEXT, import_source TEXT, import_id TEXT)"), errorMessage)
        && execSql(database, QStringLiteral("CREATE TABLE IF NOT EXISTS splits (transaction_id INTEGER NOT NULL, position INTEGER NOT NULL, account TEXT NOT NULL, amount REAL NOT NULL, PRIMARY KEY (transaction_id, position), FOREIGN KEY (transaction_id) REFERENCES transactions(id) ON DELETE CASCADE)"), errorMessage)
        && execSql(database, QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS transactions_import_unique ON transactions(import_source, import_id) WHERE import_source <> '' AND import_id <> ''"), errorMessage))) {
        return false;
    }
    if (!columnExists(database, QStringLiteral("parties"), QStringLiteral("default_account"), errorMessage)) {
        return execSql(database, QStringLiteral("ALTER TABLE parties ADD COLUMN default_account TEXT"), errorMessage);
    }
    return true;
}

bool insertAccount(QSqlDatabase &database, const Account &account, QString *errorMessage)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral("INSERT INTO accounts (name, kind, opening_balance) VALUES (?, ?, ?)"));
    query.addBindValue(account.name);
    query.addBindValue(account.kind);
    query.addBindValue(account.openingBalance);
    if (query.exec()) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QCoreApplication::translate("DataStore", "Could not save account %1: %2")
                            .arg(account.name, sqlError(query));
    }
    return false;
}

bool insertParty(QSqlDatabase &database, const Party &party, QString *errorMessage)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral("INSERT INTO parties (name, default_account) VALUES (?, ?)"));
    query.addBindValue(party.name);
    query.addBindValue(party.defaultAccount);
    if (query.exec()) {
        return true;
    }
    if (errorMessage) {
        *errorMessage = QCoreApplication::translate("DataStore", "Could not save party %1: %2")
                            .arg(party.name, sqlError(query));
    }
    return false;
}

bool saveSplits(QSqlDatabase &database, qint64 transactionId, const QList<Split> &splits, QString *errorMessage)
{
    QSqlQuery deleteQuery(database);
    deleteQuery.prepare(QStringLiteral("DELETE FROM splits WHERE transaction_id = ?"));
    deleteQuery.addBindValue(transactionId);
    if (!deleteQuery.exec()) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate("DataStore", "Could not replace transaction splits: %1")
                                .arg(sqlError(deleteQuery));
        }
        return false;
    }

    QSqlQuery insertQuery(database);
    insertQuery.prepare(QStringLiteral("INSERT INTO splits (transaction_id, position, account, amount) VALUES (?, ?, ?, ?)"));
    for (int index = 0; index < splits.size(); ++index) {
        insertQuery.addBindValue(transactionId);
        insertQuery.addBindValue(index);
        insertQuery.addBindValue(splits.at(index).account);
        insertQuery.addBindValue(splits.at(index).amount);
        if (!insertQuery.exec()) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not save transaction split: %1")
                                    .arg(sqlError(insertQuery));
            }
            return false;
        }
    }
    return true;
}

bool saveTransactionToDatabase(QSqlDatabase &database, Transaction *transaction, QString *errorMessage)
{
    if (transaction->id > 0) {
        QSqlQuery query(database);
        query.prepare(QStringLiteral("UPDATE transactions SET date = ?, source_account = ?, amount = ?, party = ?, memo = ?, import_source = ?, import_id = ? WHERE id = ?"));
        query.addBindValue(transaction->date.toString(Qt::ISODate));
        query.addBindValue(transaction->sourceAccount);
        query.addBindValue(transaction->amount);
        query.addBindValue(transaction->party);
        query.addBindValue(transaction->memo);
        query.addBindValue(transaction->importSource);
        query.addBindValue(transaction->importId);
        query.addBindValue(transaction->id);
        if (!query.exec()) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not update transaction: %1")
                                    .arg(sqlError(query));
            }
            return false;
        }
    } else {
        QSqlQuery query(database);
        query.prepare(QStringLiteral("INSERT INTO transactions (date, source_account, amount, party, memo, import_source, import_id) VALUES (?, ?, ?, ?, ?, ?, ?)"));
        query.addBindValue(transaction->date.toString(Qt::ISODate));
        query.addBindValue(transaction->sourceAccount);
        query.addBindValue(transaction->amount);
        query.addBindValue(transaction->party);
        query.addBindValue(transaction->memo);
        query.addBindValue(transaction->importSource);
        query.addBindValue(transaction->importId);
        if (!query.exec()) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not insert transaction: %1")
                                    .arg(sqlError(query));
            }
            return false;
        }
        transaction->id = query.lastInsertId().toLongLong();
    }

    return saveSplits(database, transaction->id, transaction->targets, errorMessage);
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
    return QCoreApplication::translate("DataStore", "Magic Counting database (*.maccd);;Magic Counting JSON (*.macc);;All files (*)");
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
        normalizedPath.append(".maccd");
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
    } else if (isDatabasePath(m_filePath)) {
        if (!info.exists()) {
            seedDefaults();
            return saveAll(errorMessage);
        }

        ScopedSqlConnection connection(m_filePath, errorMessage);
        if (!connection.opened || !createSchema(connection.database, errorMessage)) {
            return false;
        }

        accounts.clear();
        QSqlQuery accountQuery(connection.database);
        if (!accountQuery.exec(QStringLiteral("SELECT name, kind, opening_balance FROM accounts ORDER BY rowid"))) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not load accounts: %1")
                                    .arg(sqlError(accountQuery));
            }
            return false;
        }
        while (accountQuery.next()) {
            accounts.append({accountQuery.value(0).toString(),
                             accountQuery.value(1).toString(),
                             accountQuery.value(2).toDouble()});
        }

        parties.clear();
        QSqlQuery partyQuery(connection.database);
        if (!partyQuery.exec(QStringLiteral("SELECT name, default_account FROM parties ORDER BY rowid"))) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not load parties: %1")
                                    .arg(sqlError(partyQuery));
            }
            return false;
        }
        while (partyQuery.next()) {
            parties.append({partyQuery.value(0).toString(), partyQuery.value(1).toString()});
        }

        transactions.clear();
        QSqlQuery transactionQuery(connection.database);
        if (!transactionQuery.exec(QStringLiteral("SELECT id, date, source_account, amount, party, memo, import_source, import_id FROM transactions ORDER BY date, id"))) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not load transactions: %1")
                                    .arg(sqlError(transactionQuery));
            }
            return false;
        }
        while (transactionQuery.next()) {
            Transaction transaction;
            transaction.id = transactionQuery.value(0).toLongLong();
            transaction.date = QDate::fromString(transactionQuery.value(1).toString(), Qt::ISODate);
            if (!transaction.date.isValid()) {
                transaction.date = QDate::currentDate();
            }
            transaction.sourceAccount = transactionQuery.value(2).toString();
            transaction.amount = transactionQuery.value(3).toDouble();
            transaction.party = transactionQuery.value(4).toString();
            transaction.memo = transactionQuery.value(5).toString();
            transaction.importSource = transactionQuery.value(6).toString();
            transaction.importId = transactionQuery.value(7).toString();

            QSqlQuery splitQuery(connection.database);
            splitQuery.prepare(QStringLiteral("SELECT account, amount FROM splits WHERE transaction_id = ? ORDER BY position"));
            splitQuery.addBindValue(transaction.id);
            if (!splitQuery.exec()) {
                if (errorMessage) {
                    *errorMessage = QCoreApplication::translate("DataStore", "Could not load transaction splits: %1")
                                        .arg(sqlError(splitQuery));
                }
                return false;
            }
            while (splitQuery.next()) {
                transaction.targets.append({splitQuery.value(0).toString(), splitQuery.value(1).toDouble()});
            }
            transactions.append(transaction);
        }

        if (accounts.isEmpty() && parties.isEmpty() && transactions.isEmpty()) {
            seedDefaults();
            return saveAll(errorMessage);
        }
        return true;
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
        parties.append({object.value("name").toString(), object.value("defaultAccount").toString()});
    }

    transactions.clear();
    for (const QJsonValue &value : transactionArray) {
        const QJsonObject object = value.toObject();
        Transaction transaction;
        transaction.id = object.value("id").toVariant().toLongLong();
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

bool DataStore::saveAll(QString *errorMessage)
{
    if (!ensureParentFolder(errorMessage)) {
        return false;
    }

    if (isDatabasePath(m_filePath)) {
        ScopedSqlConnection connection(m_filePath, errorMessage);
        if (!connection.opened || !createSchema(connection.database, errorMessage)) {
            return false;
        }
        if (!connection.database.transaction()) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not start database transaction: %1")
                                    .arg(sqlError(connection.database));
            }
            return false;
        }

        const QStringList deleteStatements = {
            QStringLiteral("DELETE FROM splits"),
            QStringLiteral("DELETE FROM transactions"),
            QStringLiteral("DELETE FROM parties"),
            QStringLiteral("DELETE FROM accounts"),
        };
        for (const QString &statement : deleteStatements) {
            if (!execSql(connection.database, statement, errorMessage)) {
                connection.database.rollback();
                return false;
            }
        }

        for (const Account &account : accounts) {
            if (!insertAccount(connection.database, account, errorMessage)) {
                connection.database.rollback();
                return false;
            }
        }
        for (const Party &party : parties) {
            if (!insertParty(connection.database, party, errorMessage)) {
                connection.database.rollback();
                return false;
            }
        }
        for (Transaction &transaction : transactions) {
            transaction.id = 0;
            if (!saveTransactionToDatabase(connection.database, &transaction, errorMessage)) {
                connection.database.rollback();
                return false;
            }
        }

        if (!connection.database.commit()) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not commit database transaction: %1")
                                    .arg(sqlError(connection.database));
            }
            connection.database.rollback();
            return false;
        }
        return true;
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

bool DataStore::saveAccounts(QString *errorMessage)
{
    if (isDatabasePath(m_filePath)) {
        ScopedSqlConnection connection(m_filePath, errorMessage);
        if (!connection.opened || !createSchema(connection.database, errorMessage)) {
            return false;
        }
        if (!connection.database.transaction()) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not start database transaction: %1")
                                    .arg(sqlError(connection.database));
            }
            return false;
        }
        if (!execSql(connection.database, QStringLiteral("DELETE FROM accounts"), errorMessage)) {
            connection.database.rollback();
            return false;
        }
        for (const Account &account : accounts) {
            if (!insertAccount(connection.database, account, errorMessage)) {
                connection.database.rollback();
                return false;
            }
        }
        if (!connection.database.commit()) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not commit database transaction: %1")
                                    .arg(sqlError(connection.database));
            }
            connection.database.rollback();
            return false;
        }
        return true;
    }
    return saveAll(errorMessage);
}

bool DataStore::saveParties(QString *errorMessage)
{
    if (isDatabasePath(m_filePath)) {
        ScopedSqlConnection connection(m_filePath, errorMessage);
        if (!connection.opened || !createSchema(connection.database, errorMessage)) {
            return false;
        }
        if (!connection.database.transaction()) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not start database transaction: %1")
                                    .arg(sqlError(connection.database));
            }
            return false;
        }
        if (!execSql(connection.database, QStringLiteral("DELETE FROM parties"), errorMessage)) {
            connection.database.rollback();
            return false;
        }
        for (const Party &party : parties) {
            if (!insertParty(connection.database, party, errorMessage)) {
                connection.database.rollback();
                return false;
            }
        }
        if (!connection.database.commit()) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not commit database transaction: %1")
                                    .arg(sqlError(connection.database));
            }
            connection.database.rollback();
            return false;
        }
        return true;
    }
    return saveAll(errorMessage);
}

bool DataStore::saveTransactions(QString *errorMessage)
{
    if (isDatabasePath(m_filePath)) {
        ScopedSqlConnection connection(m_filePath, errorMessage);
        if (!connection.opened || !createSchema(connection.database, errorMessage)) {
            return false;
        }
        if (!connection.database.transaction()) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not start database transaction: %1")
                                    .arg(sqlError(connection.database));
            }
            return false;
        }
        if (!execSql(connection.database, QStringLiteral("DELETE FROM splits"), errorMessage)
            || !execSql(connection.database, QStringLiteral("DELETE FROM transactions"), errorMessage)) {
            connection.database.rollback();
            return false;
        }
        for (Transaction &transaction : transactions) {
            transaction.id = 0;
            if (!saveTransactionToDatabase(connection.database, &transaction, errorMessage)) {
                connection.database.rollback();
                return false;
            }
        }
        if (!connection.database.commit()) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("DataStore", "Could not commit database transaction: %1")
                                    .arg(sqlError(connection.database));
            }
            connection.database.rollback();
            return false;
        }
        return true;
    }
    return saveAll(errorMessage);
}

bool DataStore::saveTransaction(int transactionIndex, QString *errorMessage)
{
    if (transactionIndex < 0 || transactionIndex >= transactions.size()) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate("DataStore", "Invalid transaction row");
        }
        return false;
    }

    if (!isDatabasePath(m_filePath)) {
        return saveTransactions(errorMessage);
    }

    if (!ensureParentFolder(errorMessage)) {
        return false;
    }
    ScopedSqlConnection connection(m_filePath, errorMessage);
    if (!connection.opened || !createSchema(connection.database, errorMessage)) {
        return false;
    }
    if (!connection.database.transaction()) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate("DataStore", "Could not start database transaction: %1")
                                .arg(sqlError(connection.database));
        }
        return false;
    }
    if (!saveTransactionToDatabase(connection.database, &transactions[transactionIndex], errorMessage)) {
        connection.database.rollback();
        return false;
    }
    if (!connection.database.commit()) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate("DataStore", "Could not commit database transaction: %1")
                                .arg(sqlError(connection.database));
        }
        connection.database.rollback();
        return false;
    }
    return true;
}

bool DataStore::removeTransaction(int transactionIndex, QString *errorMessage)
{
    if (transactionIndex < 0 || transactionIndex >= transactions.size()) {
        return false;
    }

    const qint64 transactionId = transactions.at(transactionIndex).id;

    if (!isDatabasePath(m_filePath)) {
        transactions.removeAt(transactionIndex);
        return saveTransactions(errorMessage);
    }
    if (transactionId <= 0) {
        transactions.removeAt(transactionIndex);
        return true;
    }

    ScopedSqlConnection connection(m_filePath, errorMessage);
    if (!connection.opened || !createSchema(connection.database, errorMessage)) {
        return false;
    }
    QSqlQuery query(connection.database);
    query.prepare(QStringLiteral("DELETE FROM transactions WHERE id = ?"));
    query.addBindValue(transactionId);
    if (query.exec()) {
        transactions.removeAt(transactionIndex);
        return true;
    }
    if (errorMessage) {
        *errorMessage = QCoreApplication::translate("DataStore", "Could not remove transaction: %1")
                            .arg(sqlError(query));
    }
    return false;
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
        {"Grocery shop", "food"},
        {"Fuel station", "car"},
        {"Employer", "income"},
    };
    transactions = {
        {0, QDate::currentDate(), "bank", 24.90, "Grocery shop", {{"food", 24.90}}, "Weekly groceries"},
        {0, QDate::currentDate(), "cash", 15.70, "Fuel station", {{"car", 12.50}, {"food", 3.20}}, "Fuel and snack"},
    };
}
