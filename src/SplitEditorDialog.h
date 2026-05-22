#pragma once

#include "DataStore.h"

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>

class SplitEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SplitEditorDialog(const QList<Account> &accounts, double requiredTotal, QWidget *parent = nullptr);

    void setSplits(const QList<Split> &splits);
    QList<Split> splits() const;

private:
    QList<Account> m_accounts;
    double m_requiredTotal = 0.0;
    QTableWidget *m_table = nullptr;
    QLabel *m_totalLabel = nullptr;
    QPushButton *m_okButton = nullptr;

    void addRow(const Split &split = Split());
    void updateValidity();
    double currentTotal() const;
    bool totalsMatch() const;
};
