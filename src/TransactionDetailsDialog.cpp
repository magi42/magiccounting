#include "TransactionDetailsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace
{
qint64 moneyCents(double value)
{
    return qRound64(value * 100.0);
}
}

TransactionDetailsDialog::TransactionDetailsDialog(const QList<Account> &accounts, QWidget *parent)
    : QDialog(parent)
    , m_accounts(accounts)
    , m_dateEdit(new QDateEdit(this))
    , m_sourceAccountCombo(new QComboBox(this))
    , m_amountSpin(new QDoubleSpinBox(this))
    , m_partyEdit(new QLineEdit(this))
    , m_memoEdit(new QTextEdit(this))
    , m_importSourceEdit(new QLineEdit(this))
    , m_importIdEdit(new QLineEdit(this))
    , m_targetsTable(new QTableWidget(this))
    , m_totalLabel(new QLabel(this))
{
    setWindowTitle(tr("Transaction Details"));

    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));

    for (const Account &account : m_accounts) {
        if (account.kind == "source") {
            m_sourceAccountCombo->addItem(account.name);
        }
    }
    if (m_sourceAccountCombo->count() == 0) {
        for (const Account &account : m_accounts) {
            m_sourceAccountCombo->addItem(account.name);
        }
    }
    m_sourceAccountCombo->setEditable(true);

    m_amountSpin->setRange(0.0, 999999999.99);
    m_amountSpin->setDecimals(2);
    m_amountSpin->setSingleStep(1.0);

    m_memoEdit->setAcceptRichText(false);
    m_memoEdit->setMinimumHeight(90);

    auto *formLayout = new QFormLayout();
    formLayout->addRow(tr("Date"), m_dateEdit);
    formLayout->addRow(tr("Source account"), m_sourceAccountCombo);
    formLayout->addRow(tr("Amount"), m_amountSpin);
    formLayout->addRow(tr("Other party"), m_partyEdit);
    formLayout->addRow(tr("Memo"), m_memoEdit);
    formLayout->addRow(tr("Import source"), m_importSourceEdit);
    formLayout->addRow(tr("Import ID"), m_importIdEdit);

    m_targetsTable->setColumnCount(2);
    m_targetsTable->setHorizontalHeaderLabels({tr("Counterpart account"), tr("Amount")});
    m_targetsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_targetsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_targetsTable->verticalHeader()->setVisible(false);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttonBox->button(QDialogButtonBox::Ok);
    auto *addSplitButton = buttonBox->addButton(tr("Add counterpart"), QDialogButtonBox::ActionRole);
    auto *removeSplitButton = buttonBox->addButton(tr("Remove counterpart"), QDialogButtonBox::ActionRole);

    connect(addSplitButton, &QPushButton::clicked, this, [this]() {
        addSplitRow();
        updateValidity();
    });
    connect(removeSplitButton, &QPushButton::clicked, this, [this]() {
        const int row = m_targetsTable->currentRow();
        if (row >= 0) {
            m_targetsTable->removeRow(row);
            updateValidity();
        }
    });
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_sourceAccountCombo, &QComboBox::currentTextChanged, this, &TransactionDetailsDialog::updateValidity);
    connect(m_amountSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &TransactionDetailsDialog::updateValidity);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(formLayout);
    layout->addWidget(m_targetsTable);
    layout->addWidget(m_totalLabel);
    layout->addWidget(buttonBox);

    resize(700, 620);
}

void TransactionDetailsDialog::setTransaction(const Transaction &transaction)
{
    m_transactionId = transaction.id;
    m_dateEdit->setDate(transaction.date.isValid() ? transaction.date : QDate::currentDate());
    m_sourceAccountCombo->setCurrentText(transaction.sourceAccount);
    m_amountSpin->setValue(transaction.amount);
    m_partyEdit->setText(transaction.party);
    m_memoEdit->setPlainText(transaction.memo);
    m_importSourceEdit->setText(transaction.importSource);
    m_importIdEdit->setText(transaction.importId);

    m_targetsTable->setRowCount(0);
    for (const Split &split : transaction.targets) {
        addSplitRow(split);
    }
    if (transaction.targets.isEmpty()) {
        addSplitRow();
    }
    updateValidity();
}

Transaction TransactionDetailsDialog::transaction() const
{
    Transaction transaction;
    transaction.id = m_transactionId;
    transaction.date = m_dateEdit->date();
    transaction.sourceAccount = m_sourceAccountCombo->currentText().trimmed();
    transaction.amount = m_amountSpin->value();
    transaction.party = m_partyEdit->text().trimmed();
    transaction.memo = m_memoEdit->toPlainText();
    transaction.importSource = m_importSourceEdit->text().trimmed();
    transaction.importId = m_importIdEdit->text().trimmed();
    transaction.targets = splits();
    return transaction;
}

void TransactionDetailsDialog::addSplitRow(const Split &split)
{
    const int row = m_targetsTable->rowCount();
    m_targetsTable->insertRow(row);

    auto *accountCombo = new QComboBox(m_targetsTable);
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

    auto *amountSpin = new QDoubleSpinBox(m_targetsTable);
    amountSpin->setRange(0.0, 999999999.99);
    amountSpin->setDecimals(2);
    amountSpin->setSingleStep(1.0);
    amountSpin->setValue(split.amount);

    connect(accountCombo, &QComboBox::currentTextChanged, this, &TransactionDetailsDialog::updateValidity);
    connect(amountSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &TransactionDetailsDialog::updateValidity);

    m_targetsTable->setCellWidget(row, 0, accountCombo);
    m_targetsTable->setCellWidget(row, 1, amountSpin);
}

void TransactionDetailsDialog::updateValidity()
{
    const double total = splitTotal();
    const bool valid = !m_sourceAccountCombo->currentText().trimmed().isEmpty()
        && m_amountSpin->value() > 0.0
        && totalsMatch();
    m_totalLabel->setText(tr("Counterpart total: %1 / Amount: %2")
                              .arg(QString::number(total, 'f', 2),
                                   QString::number(m_amountSpin->value(), 'f', 2)));
    m_totalLabel->setStyleSheet(valid ? QString() : QStringLiteral("color: #b00020;"));
    m_okButton->setEnabled(valid);
}

QList<Split> TransactionDetailsDialog::splits() const
{
    QList<Split> result;
    for (int row = 0; row < m_targetsTable->rowCount(); ++row) {
        auto *accountCombo = qobject_cast<QComboBox *>(m_targetsTable->cellWidget(row, 0));
        auto *amountSpin = qobject_cast<QDoubleSpinBox *>(m_targetsTable->cellWidget(row, 1));
        if (!accountCombo || !amountSpin || accountCombo->currentText().trimmed().isEmpty()) {
            continue;
        }
        result.append({accountCombo->currentText().trimmed(), amountSpin->value()});
    }
    return result;
}

double TransactionDetailsDialog::splitTotal() const
{
    double total = 0.0;
    for (const Split &split : splits()) {
        total += split.amount;
    }
    return total;
}

bool TransactionDetailsDialog::totalsMatch() const
{
    return moneyCents(splitTotal()) == moneyCents(m_amountSpin->value());
}
