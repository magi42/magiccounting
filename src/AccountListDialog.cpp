#include "AccountListDialog.h"

#include <QDialogButtonBox>
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

bool AccountListDialog::editAccounts(QWidget *parent, QList<Account> *accounts)
{
    AccountListDialog dialog(parent);
    dialog.setWindowTitle(dialog.tr("Accounts"));
    dialog.m_table->setColumnCount(2);
    dialog.m_table->setHorizontalHeaderLabels({dialog.tr("Name"), dialog.tr("Kind")});
    dialog.m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    dialog.m_table->setRowCount(accounts->size());

    for (int row = 0; row < accounts->size(); ++row) {
        dialog.m_table->setItem(row, 0, new QTableWidgetItem(accounts->at(row).name));
        dialog.m_table->setItem(row, 1, new QTableWidgetItem(accounts->at(row).kind));
    }

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    QMap<QString, double> existingBalances;
    for (const Account &account : *accounts) {
        existingBalances.insert(account.name, account.openingBalance);
    }

    accounts->clear();
    for (int row = 0; row < dialog.m_table->rowCount(); ++row) {
        const QString name = dialog.m_table->item(row, 0) ? dialog.m_table->item(row, 0)->text().trimmed() : QString();
        const QString kind = dialog.m_table->item(row, 1) ? dialog.m_table->item(row, 1)->text().trimmed() : QString();
        if (!name.isEmpty()) {
            accounts->append({name, kind.isEmpty() ? "category" : kind, existingBalances.value(name, 0.0)});
        }
    }
    return true;
}

bool AccountListDialog::editParties(QWidget *parent, QList<Party> *parties)
{
    AccountListDialog dialog(parent);
    dialog.setWindowTitle(dialog.tr("Parties"));
    dialog.m_table->setColumnCount(1);
    dialog.m_table->setHorizontalHeaderLabels({dialog.tr("Name")});
    dialog.m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    dialog.m_table->setRowCount(parties->size());

    for (int row = 0; row < parties->size(); ++row) {
        dialog.m_table->setItem(row, 0, new QTableWidgetItem(parties->at(row).name));
    }

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    parties->clear();
    for (int row = 0; row < dialog.m_table->rowCount(); ++row) {
        const QString name = dialog.m_table->item(row, 0) ? dialog.m_table->item(row, 0)->text().trimmed() : QString();
        if (!name.isEmpty()) {
            parties->append({name});
        }
    }
    return true;
}
