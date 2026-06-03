#pragma once

#include "DataStore.h"
#include "AppConfig.h"

#include <QDialog>
#include <QMap>
#include <QTableWidget>

class AccountListDialog : public QDialog
{
    Q_OBJECT

public:
    static bool editAccounts(QWidget *parent, QList<Account> *accounts, QMap<QString, QString> *renamedAccounts = nullptr);
    static bool editParties(QWidget *parent, QList<Party> *parties, const QList<Account> &accounts);
    static bool editImportClassificationRules(QWidget *parent,
                                              QList<ImportClassificationRule> *rules,
                                              const QList<Account> &accounts);

private:
    explicit AccountListDialog(QWidget *parent = nullptr);

    QTableWidget *m_table = nullptr;
};
