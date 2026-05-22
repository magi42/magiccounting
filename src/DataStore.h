#pragma once

#include <QDate>
#include <QDir>
#include <QList>
#include <QString>

struct Account
{
    QString name;
    QString kind;
    double openingBalance = 0.0;
};

struct Party
{
    QString name;
};

struct Split
{
    QString account;
    double amount = 0.0;
};

struct Transaction
{
    QDate date;
    QString sourceAccount;
    double amount = 0.0;
    QString party;
    QList<Split> targets;
    QString memo;
};

class DataStore
{
public:
    explicit DataStore(const QString &folderPath = QString());

    const QString &folderPath() const;
    bool load(const QString &folderPath, QString *errorMessage = nullptr);
    bool saveAll(QString *errorMessage = nullptr) const;
    bool saveAccounts(QString *errorMessage = nullptr) const;
    bool saveParties(QString *errorMessage = nullptr) const;
    bool saveTransactions(QString *errorMessage = nullptr) const;

    QList<Account> accounts;
    QList<Party> parties;
    QList<Transaction> transactions;

    static QList<Split> parseSplits(const QString &text, bool *ok = nullptr);
    static QString formatSplits(const QList<Split> &splits);
    static QString defaultDataFolder();

private:
    QString m_folderPath;

    bool ensureFolder(QString *errorMessage) const;
    void seedDefaults();
};
