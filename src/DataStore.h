#pragma once

#include <QDate>
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
    QString importSource;
    QString importId;
};

class DataStore
{
public:
    explicit DataStore(const QString &filePath = QString());

    const QString &filePath() const;
    bool load(const QString &filePath, QString *errorMessage = nullptr);
    bool saveAs(const QString &filePath, QString *errorMessage = nullptr);
    bool saveAll(QString *errorMessage = nullptr) const;
    bool saveAccounts(QString *errorMessage = nullptr) const;
    bool saveParties(QString *errorMessage = nullptr) const;
    bool saveTransactions(QString *errorMessage = nullptr) const;

    QList<Account> accounts;
    QList<Party> parties;
    QList<Transaction> transactions;

    bool hasImportedTransaction(const QString &importSource, const QString &importId) const;
    static QList<Split> parseSplits(const QString &text, bool *ok = nullptr);
    static QString formatSplits(const QList<Split> &splits);
    static QString defaultAccountingFile();
    static QString fileFilter();

private:
    QString m_filePath;

    bool ensureParentFolder(QString *errorMessage) const;
    void seedDefaults();
};
