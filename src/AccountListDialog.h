#pragma once

#include "DataStore.h"

#include <QDialog>
#include <QTableWidget>

class AccountListDialog : public QDialog
{
    Q_OBJECT

public:
    static bool editAccounts(QWidget *parent, QList<Account> *accounts);
    static bool editParties(QWidget *parent, QList<Party> *parties);

private:
    explicit AccountListDialog(QWidget *parent = nullptr);

    QTableWidget *m_table = nullptr;
};
