#pragma once

#include "DataStore.h"

#include <QDateEdit>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTextEdit>
#include <QTemporaryDir>

class TransactionDetailsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TransactionDetailsDialog(const QList<Account> &accounts,
                                      const QString &accountingFilePath,
                                      QWidget *parent = nullptr);

    void setTransaction(const Transaction &transaction);
    Transaction transaction() const;

private:
    QList<Account> m_accounts;
    qint64 m_transactionId = 0;
    QDateEdit *m_bookingDateEdit = nullptr;
    QDateEdit *m_paymentDateEdit = nullptr;
    QComboBox *m_sourceAccountCombo = nullptr;
    QDoubleSpinBox *m_amountSpin = nullptr;
    QLineEdit *m_partyEdit = nullptr;
    QTextEdit *m_memoEdit = nullptr;
    QLineEdit *m_importSourceEdit = nullptr;
    QLineEdit *m_importIdEdit = nullptr;
    QString m_accountingFilePath;
    QString m_receiptPath;
    QLabel *m_receiptImageLabel = nullptr;
    QLabel *m_receiptPathLabel = nullptr;
    QScrollArea *m_receiptScrollArea = nullptr;
    QPushButton *m_openReceiptButton = nullptr;
    QTemporaryDir m_pdfPreviewDir;
    QTableWidget *m_targetsTable = nullptr;
    QLabel *m_totalLabel = nullptr;
    QPushButton *m_okButton = nullptr;

    void addSplitRow(const Split &split = Split());
    void updateValidity();
    QList<Split> splits() const;
    double splitTotal() const;
    bool totalsMatch() const;
    bool eventFilter(QObject *watched, QEvent *event) override;
    QString absoluteReceiptPath() const;
    void setReceiptPath(const QString &receiptPath);
    void updateReceiptPreview();
    bool receiptIsPdf() const;
    QString renderPdfPreview(const QString &path, QString *errorMessage) const;
    void showReceiptImage(const QImage &image);
    double imageWidthMillimeters(const QImage &image) const;
};
