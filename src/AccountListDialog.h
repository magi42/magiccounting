#pragma once

#include "DataStore.h"
#include "AppConfig.h"

#include <QDialog>
#include <QMap>
#include <QTableWidget>
#include <QTreeWidget>

class AccountListDialog : public QDialog
{
    Q_OBJECT

public:
    static bool editAccounts(QWidget *parent,
                             QList<Account> *accounts,
                             const QList<Transaction> &transactions,
                             QMap<QString, QString> *renamedAccounts = nullptr);
    static bool editParties(QWidget *parent, QList<Party> *parties, const QList<Account> &accounts);
    static bool editImportClassificationRules(QWidget *parent,
                                              QList<ImportClassificationRule> *rules,
                                              const QList<Account> &accounts);

private:
    explicit AccountListDialog(QWidget *parent = nullptr, bool treeMode = false);

    QTableWidget *m_table = nullptr;
    QTreeWidget *m_tree = nullptr;
};
