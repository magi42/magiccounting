#pragma once

#include "DataStore.h"

#include <QDateEdit>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>

class TransactionDetailsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TransactionDetailsDialog(const QList<Account> &accounts, QWidget *parent = nullptr);

    void setTransaction(const Transaction &transaction);
    Transaction transaction() const;

private:
    QList<Account> m_accounts;
    qint64 m_transactionId = 0;
    QDateEdit *m_dateEdit = nullptr;
    QComboBox *m_sourceAccountCombo = nullptr;
    QDoubleSpinBox *m_amountSpin = nullptr;
    QLineEdit *m_partyEdit = nullptr;
    QTextEdit *m_memoEdit = nullptr;
    QLineEdit *m_importSourceEdit = nullptr;
    QLineEdit *m_importIdEdit = nullptr;
    QTableWidget *m_targetsTable = nullptr;
    QLabel *m_totalLabel = nullptr;
    QPushButton *m_okButton = nullptr;

    void addSplitRow(const Split &split = Split());
    void updateValidity();
    QList<Split> splits() const;
    double splitTotal() const;
    bool totalsMatch() const;
};
