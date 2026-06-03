#include "AccountListDialog.h"

#include <QDialogButtonBox>
#include <QComboBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QMap>
#include <QPushButton>
#include <QVBoxLayout>

AccountListDialog::AccountListDialog(QWidget *parent)
    : QDialog(parent)
    , m_table(new QTableWidget(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_table);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *addButton = buttonBox->addButton(tr("Add"), QDialogButtonBox::ActionRole);
    auto *removeButton = buttonBox->addButton(tr("Remove"), QDialogButtonBox::ActionRole);

    connect(addButton, &QPushButton::clicked, this, [this]() {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        for (int column = 0; column < m_table->columnCount(); ++column) {
            m_table->setItem(row, column, new QTableWidgetItem());
        }
        m_table->setCurrentCell(row, 0);
        m_table->editItem(m_table->item(row, 0));
    });

    connect(removeButton, &QPushButton::clicked, this, [this]() {
        const int row = m_table->currentRow();
        if (row >= 0) {
            m_table->removeRow(row);
        }
    });

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}

bool AccountListDialog::editAccounts(QWidget *parent, QList<Account> *accounts, QMap<QString, QString> *renamedAccounts)
{
    AccountListDialog dialog(parent);
    dialog.setWindowTitle(dialog.tr("Accounts"));
    dialog.m_table->setColumnCount(2);
    dialog.m_table->setHorizontalHeaderLabels({dialog.tr("Name"), dialog.tr("Kind")});
    dialog.m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    dialog.m_table->setRowCount(accounts->size());

    for (int row = 0; row < accounts->size(); ++row) {
        auto *nameItem = new QTableWidgetItem(accounts->at(row).name);
        nameItem->setData(Qt::UserRole, accounts->at(row).name);
        dialog.m_table->setItem(row, 0, nameItem);
        dialog.m_table->setItem(row, 1, new QTableWidgetItem(accounts->at(row).kind));
    }

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    QMap<QString, double> originalBalances;
    for (const Account &account : *accounts) {
        originalBalances.insert(account.name, account.openingBalance);
    }

    if (renamedAccounts) {
        renamedAccounts->clear();
    }
    accounts->clear();
    for (int row = 0; row < dialog.m_table->rowCount(); ++row) {
        QTableWidgetItem *nameItem = dialog.m_table->item(row, 0);
        const QString name = nameItem ? nameItem->text().trimmed() : QString();
        const QString originalName = nameItem ? nameItem->data(Qt::UserRole).toString() : QString();
        const QString kind = dialog.m_table->item(row, 1) ? dialog.m_table->item(row, 1)->text().trimmed() : QString();
        if (!name.isEmpty()) {
            accounts->append({name,
                              kind.isEmpty() ? "category" : kind,
                              originalName.isEmpty() ? 0.0 : originalBalances.value(originalName, 0.0)});
            if (renamedAccounts && originalName != name && !originalName.isEmpty()) {
                renamedAccounts->insert(originalName, name);
            }
        }
    }
    return true;
}

bool AccountListDialog::editParties(QWidget *parent, QList<Party> *parties, const QList<Account> &accounts)
{
    AccountListDialog dialog(parent);
    dialog.setWindowTitle(dialog.tr("Parties"));
    dialog.m_table->setColumnCount(2);
    dialog.m_table->setHorizontalHeaderLabels({dialog.tr("Name"), dialog.tr("Default account")});
    dialog.m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    dialog.m_table->setRowCount(parties->size());

    QStringList accountNames;
    for (const Account &account : accounts) {
        accountNames.append(account.name);
    }

    for (int row = 0; row < parties->size(); ++row) {
        dialog.m_table->setItem(row, 0, new QTableWidgetItem(parties->at(row).name));
        auto *accountCombo = new QComboBox(&dialog);
        accountCombo->setEditable(true);
        accountCombo->addItem(QString());
        accountCombo->addItems(accountNames);
        accountCombo->setCurrentText(parties->at(row).defaultAccount);
        dialog.m_table->setCellWidget(row, 1, accountCombo);
    }

    connect(dialog.m_table, &QTableWidget::cellChanged, &dialog, [&dialog, accountNames](int row, int) {
        if (!dialog.m_table->cellWidget(row, 1)) {
            auto *accountCombo = new QComboBox(&dialog);
            accountCombo->setEditable(true);
            accountCombo->addItem(QString());
            accountCombo->addItems(accountNames);
            dialog.m_table->setCellWidget(row, 1, accountCombo);
        }
    });

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    parties->clear();
    for (int row = 0; row < dialog.m_table->rowCount(); ++row) {
        const QString name = dialog.m_table->item(row, 0) ? dialog.m_table->item(row, 0)->text().trimmed() : QString();
        const auto *accountCombo = qobject_cast<QComboBox *>(dialog.m_table->cellWidget(row, 1));
        const QString defaultAccount = accountCombo
                                           ? accountCombo->currentText().trimmed()
                                           : (dialog.m_table->item(row, 1) ? dialog.m_table->item(row, 1)->text().trimmed() : QString());
        if (!name.isEmpty()) {
            parties->append({name, defaultAccount});
        }
    }
    return true;
}

bool AccountListDialog::editImportClassificationRules(QWidget *parent,
                                                      QList<ImportClassificationRule> *rules,
                                                      const QList<Account> &accounts)
{
    AccountListDialog dialog(parent);
    dialog.setWindowTitle(dialog.tr("Import Classification Rules"));
    dialog.m_table->setColumnCount(2);
    dialog.m_table->setHorizontalHeaderLabels({dialog.tr("Other party contains"), dialog.tr("Target account")});
    dialog.m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    dialog.m_table->setRowCount(rules->size());

    QStringList accountNames;
    for (const Account &account : accounts) {
        accountNames.append(account.name);
    }

    for (int row = 0; row < rules->size(); ++row) {
        dialog.m_table->setItem(row, 0, new QTableWidgetItem(rules->at(row).partyPattern));

        auto *accountCombo = new QComboBox(&dialog);
        accountCombo->setEditable(true);
        accountCombo->addItems(accountNames);
        accountCombo->setCurrentText(rules->at(row).account);
        dialog.m_table->setCellWidget(row, 1, accountCombo);
    }

    connect(dialog.m_table, &QTableWidget::cellChanged, &dialog, [&dialog, accountNames](int row, int) {
        if (!dialog.m_table->cellWidget(row, 1)) {
            auto *accountCombo = new QComboBox(&dialog);
            accountCombo->setEditable(true);
            accountCombo->addItems(accountNames);
            dialog.m_table->setCellWidget(row, 1, accountCombo);
        }
    });

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    rules->clear();
    for (int row = 0; row < dialog.m_table->rowCount(); ++row) {
        const QString partyPattern = dialog.m_table->item(row, 0)
                                         ? dialog.m_table->item(row, 0)->text().trimmed()
                                         : QString();
        const auto *accountCombo = qobject_cast<QComboBox *>(dialog.m_table->cellWidget(row, 1));
        const QString account = accountCombo
                                    ? accountCombo->currentText().trimmed()
                                    : (dialog.m_table->item(row, 1) ? dialog.m_table->item(row, 1)->text().trimmed() : QString());
        if (!partyPattern.isEmpty() && !account.isEmpty()) {
            rules->append({partyPattern, account});
        }
    }
    return true;
}
