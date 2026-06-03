#pragma once

#include <QDate>
#include <QList>
#include <QString>

struct ImportedBankTransaction
{
    QDate bookingDate;
    QDate paymentDate;
    double signedAmount = 0.0;
    QString party;
    QString memo;
    QString externalId;
};

class BankStatementImporter
{
public:
    virtual ~BankStatementImporter() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QString fileFilter() const = 0;
    virtual bool importFile(const QString &filePath,
                            QList<ImportedBankTransaction> *transactions,
                            QString *errorMessage) const = 0;
};

class SBankCsvImporter final : public BankStatementImporter
{
public:
    QString id() const override;
    QString displayName() const override;
    QString fileFilter() const override;
    bool importFile(const QString &filePath,
                    QList<ImportedBankTransaction> *transactions,
                    QString *errorMessage) const override;
};
