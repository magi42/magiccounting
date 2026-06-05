#pragma once

#include <QDate>
#include <QList>
#include <QtGlobal>
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
    QString defaultAccount;
};

struct Split
{
    QString account;
    double amount = 0.0;
};

struct Transaction
{
    qint64 id = 0;
    QDate date;
    QDate paymentDate;
    QString sourceAccount;
    double amount = 0.0;
    QString party;
    QList<Split> targets;
    QString memo;
    QString importSource;
    QString importId;
    QString receiptPath;
    QString reviewStatus;
};

class DataStore
{
public:
    explicit DataStore(const QString &filePath = QString());

    const QString &filePath() const;
    bool load(const QString &filePath, QString *errorMessage = nullptr);
    bool saveAs(const QString &filePath, QString *errorMessage = nullptr);
    bool saveAll(QString *errorMessage = nullptr);
    bool saveAccounts(QString *errorMessage = nullptr);
    bool saveParties(QString *errorMessage = nullptr);
    bool saveTransactions(QString *errorMessage = nullptr);
    bool saveTransaction(int transactionIndex, QString *errorMessage = nullptr);
    bool removeTransaction(int transactionIndex, QString *errorMessage = nullptr);

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
