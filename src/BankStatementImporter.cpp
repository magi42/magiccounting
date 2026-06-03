#include "BankStatementImporter.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

namespace
{
enum SBankColumn {
    BookingDateColumn = 0,
    PaymentDateColumn = 1,
    AmountColumn = 2,
    TransactionTypeColumn = 3,
    PayerColumn = 4,
    RecipientColumn = 5,
    ReferenceColumn = 8,
    MessageColumn = 9,
    ArchiveIdColumn = 10,
    RequiredColumnCount = 11
};

QString cleanField(QString text)
{
    text = text.trimmed();
    if (text == QStringLiteral("-")) {
        return QString();
    }
    if (text.size() >= 2 && text.startsWith('\'') && text.endsWith('\'')) {
        text = text.mid(1, text.size() - 2).trimmed();
    }
    return text == QStringLiteral("-") ? QString() : text;
}

QStringList parseCsvLine(const QString &line)
{
    QStringList fields;
    QString field;
    bool inQuotes = false;

    for (int index = 0; index < line.size(); ++index) {
        const QChar ch = line.at(index);
        if (ch == '"') {
            if (inQuotes && index + 1 < line.size() && line.at(index + 1) == '"') {
                field.append('"');
                ++index;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (ch == ';' && !inQuotes) {
            fields.append(field);
            field.clear();
        } else {
            field.append(ch);
        }
    }

    fields.append(field);
    return fields;
}

bool parseSBankAmount(QString text, double *amount)
{
    text = text.trimmed();
    text.remove(' ');
    text.replace(',', '.');

    bool ok = false;
    const double parsed = text.toDouble(&ok);
    if (!ok) {
        return false;
    }
    *amount = parsed;
    return true;
}

QString transactionMemo(const QStringList &fields)
{
    QStringList parts;
    const QString type = cleanField(fields.value(TransactionTypeColumn));
    const QString reference = cleanField(fields.value(ReferenceColumn));
    const QString message = cleanField(fields.value(MessageColumn));
    if (!type.isEmpty()) {
        parts.append(type);
    }
    if (!reference.isEmpty()) {
        parts.append(QCoreApplication::translate("SBankCsvImporter", "Reference: %1").arg(reference));
    }
    if (!message.isEmpty()) {
        parts.append(message);
    }
    return parts.join(QStringLiteral(" | "));
}
}

QString SBankCsvImporter::id() const
{
    return QStringLiteral("s-pankki-csv");
}

QString SBankCsvImporter::displayName() const
{
    return QCoreApplication::translate("SBankCsvImporter", "S-Pankki CSV");
}

QString SBankCsvImporter::fileFilter() const
{
    return QCoreApplication::translate("SBankCsvImporter", "S-Pankki CSV files (*.csv);;All files (*)");
}

bool SBankCsvImporter::importFile(const QString &filePath,
                                  QList<ImportedBankTransaction> *transactions,
                                  QString *errorMessage) const
{
    transactions->clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate("SBankCsvImporter", "Could not open %1: %2")
                                .arg(filePath, file.errorString());
        }
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");

    if (stream.atEnd()) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate("SBankCsvImporter", "The CSV file is empty");
        }
        return false;
    }

    const QStringList headers = parseCsvLine(stream.readLine());
    if (headers.size() < RequiredColumnCount
        || cleanField(headers.value(BookingDateColumn)) != QStringLiteral("Kirjauspäivä")
        || cleanField(headers.value(AmountColumn)) != QStringLiteral("Summa")
        || cleanField(headers.value(ArchiveIdColumn)) != QStringLiteral("Arkistointitunnus")) {
        if (errorMessage) {
            *errorMessage = QCoreApplication::translate("SBankCsvImporter", "The CSV file does not look like an S-Pankki statement");
        }
        return false;
    }

    int lineNumber = 1;
    while (!stream.atEnd()) {
        ++lineNumber;
        const QString line = stream.readLine();
        if (line.trimmed().isEmpty()) {
            continue;
        }

        const QStringList fields = parseCsvLine(line);
        if (fields.size() < RequiredColumnCount) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("SBankCsvImporter", "Line %1 has too few columns").arg(lineNumber);
            }
            return false;
        }

        const QDate bookingDate = QDate::fromString(cleanField(fields.value(BookingDateColumn)), QStringLiteral("dd.MM.yyyy"));
        QDate paymentDate = QDate::fromString(cleanField(fields.value(PaymentDateColumn)), QStringLiteral("dd.MM.yyyy"));
        if (!paymentDate.isValid()) {
            paymentDate = bookingDate;
        }
        double amount = 0.0;
        if (!bookingDate.isValid() || !parseSBankAmount(fields.value(AmountColumn), &amount)) {
            if (errorMessage) {
                *errorMessage = QCoreApplication::translate("SBankCsvImporter", "Line %1 has an invalid date or amount").arg(lineNumber);
            }
            return false;
        }

        ImportedBankTransaction transaction;
        transaction.bookingDate = bookingDate;
        transaction.paymentDate = paymentDate;
        transaction.signedAmount = amount;
        transaction.party = amount < 0.0 ? cleanField(fields.value(RecipientColumn)) : cleanField(fields.value(PayerColumn));
        transaction.memo = transactionMemo(fields);
        transaction.externalId = cleanField(fields.value(ArchiveIdColumn));
        if (transaction.externalId.isEmpty()) {
            transaction.externalId = QStringLiteral("%1|%2|%3|%4")
                                         .arg(bookingDate.toString(Qt::ISODate),
                                              QString::number(amount, 'f', 2),
                                              transaction.party,
                                              transaction.memo);
        }

        transactions->append(transaction);
    }

    return true;
}
