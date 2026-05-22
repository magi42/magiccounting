#include "SplitEditorDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QVBoxLayout>

namespace
{
qint64 moneyCents(double value)
{
    return qRound64(value * 100.0);
}
}

SplitEditorDialog::SplitEditorDialog(const QList<Account> &accounts, double requiredTotal, QWidget *parent)
    : QDialog(parent)
    , m_accounts(accounts)
    , m_requiredTotal(requiredTotal)
    , m_table(new QTableWidget(this))
    , m_totalLabel(new QLabel(this))
{
    setWindowTitle("Target Accounts");

    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({"Target account", "Amount"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttonBox->button(QDialogButtonBox::Ok);
    auto *addButton = buttonBox->addButton("Add", QDialogButtonBox::ActionRole);
    auto *removeButton = buttonBox->addButton("Remove", QDialogButtonBox::ActionRole);

    connect(addButton, &QPushButton::clicked, this, [this]() {
        addRow();
        updateValidity();
    });
    connect(removeButton, &QPushButton::clicked, this, [this]() {
        const int row = m_table->currentRow();
        if (row >= 0) {
            m_table->removeRow(row);
            updateValidity();
        }
    });
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_table);
    layout->addWidget(m_totalLabel);
    layout->addWidget(buttonBox);

    resize(520, 320);
}

void SplitEditorDialog::setSplits(const QList<Split> &splits)
{
    m_table->setRowCount(0);
    for (const Split &split : splits) {
        addRow(split);
    }
    if (splits.isEmpty()) {
        addRow();
    }
    updateValidity();
}

QList<Split> SplitEditorDialog::splits() const
{
    QList<Split> result;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto *accountCombo = qobject_cast<QComboBox *>(m_table->cellWidget(row, 0));
        auto *amountSpin = qobject_cast<QDoubleSpinBox *>(m_table->cellWidget(row, 1));
        if (!accountCombo || !amountSpin || accountCombo->currentText().trimmed().isEmpty()) {
            continue;
        }
        result.append({accountCombo->currentText().trimmed(), amountSpin->value()});
    }
    return result;
}

void SplitEditorDialog::addRow(const Split &split)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    auto *accountCombo = new QComboBox(m_table);
    accountCombo->setEditable(true);
    for (const Account &account : m_accounts) {
        if (account.kind != "source") {
            accountCombo->addItem(account.name);
        }
    }
    if (accountCombo->count() == 0) {
        for (const Account &account : m_accounts) {
            accountCombo->addItem(account.name);
        }
    }
    if (!split.account.isEmpty()) {
        accountCombo->setCurrentText(split.account);
    }

    auto *amountSpin = new QDoubleSpinBox(m_table);
    amountSpin->setRange(0.0, 999999999.99);
    amountSpin->setDecimals(2);
    amountSpin->setSingleStep(1.0);
    amountSpin->setValue(split.amount);

    connect(accountCombo, &QComboBox::currentTextChanged, this, &SplitEditorDialog::updateValidity);
    connect(amountSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &SplitEditorDialog::updateValidity);

    m_table->setCellWidget(row, 0, accountCombo);
    m_table->setCellWidget(row, 1, amountSpin);
}

void SplitEditorDialog::updateValidity()
{
    const double total = currentTotal();
    const bool valid = totalsMatch();
    m_totalLabel->setText(QString("Target total: %1 / Amount: %2")
                              .arg(QString::number(total, 'f', 2), QString::number(m_requiredTotal, 'f', 2)));
    m_totalLabel->setStyleSheet(valid ? QString() : "color: #b00020;");
    m_okButton->setEnabled(valid);
}

double SplitEditorDialog::currentTotal() const
{
    double total = 0.0;
    for (const Split &split : splits()) {
        total += split.amount;
    }
    return total;
}

bool SplitEditorDialog::totalsMatch() const
{
    return moneyCents(currentTotal()) == moneyCents(m_requiredTotal);
}
