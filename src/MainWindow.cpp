#include "MainWindow.h"

#include "AccountListDialog.h"
#include "SplitEditorDialog.h"

#include <QComboBox>
#include <QDate>
#include <QBrush>
#include <QColor>
#include <QFileDialog>
#include <QFont>
#include <QHeaderView>
#include <QMap>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QToolBar>

namespace
{
qint64 moneyCents(double value)
{
    return qRound64(value * 100.0);
}

QString moneyText(double value)
{
    return QString::number(value, 'f', 2);
}

constexpr int kOpeningBalanceRow = -1;
constexpr int kMonthBalanceRow = -2;

QTableWidgetItem *readOnlyItem(const QString &text = QString())
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    loadInitialData();
}

void MainWindow::buildUi()
{
    m_table = new QTableWidget(this);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    setCentralWidget(m_table);

    auto *fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction("Open Data Folder...", this, &MainWindow::openDataFolder);
    fileMenu->addAction("Save", this, &MainWindow::saveTransactions);
    fileMenu->addSeparator();
    fileMenu->addAction("Quit", this, &QWidget::close);

    auto *editMenu = menuBar()->addMenu("Edit");
    editMenu->addAction("Add Transaction", this, &MainWindow::addTransaction);
    editMenu->addAction("Remove Transaction", this, &MainWindow::removeSelectedTransaction);
    editMenu->addSeparator();
    editMenu->addAction("Accounts...", this, &MainWindow::editAccounts);
    editMenu->addAction("Parties...", this, &MainWindow::editParties);

    auto *toolbar = addToolBar("Transactions");
    toolbar->addAction("Add", this, &MainWindow::addTransaction);
    toolbar->addAction("Remove", this, &MainWindow::removeSelectedTransaction);
    toolbar->addSeparator();
    toolbar->addAction("Accounts", this, &MainWindow::editAccounts);
    toolbar->addAction("Parties", this, &MainWindow::editParties);

    connect(m_table, &QTableWidget::cellChanged, this, [this](int row, int) {
        if (m_refreshing) {
            return;
        }

        const int transactionIndex = transactionIndexForRow(row);
        if (transactionIndex == kOpeningBalanceRow) {
            saveOpeningBalances();
            return;
        }
        if (transactionIndex < 0) {
            return;
        }
        saveRowIfValid(row);
    });

    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column == TargetsColumn) {
            openSplitEditor(row);
        }
    });

    setWindowTitle("Magic Counting");
    resize(1100, 620);
}

void MainWindow::loadInitialData()
{
    QString error;
    if (!m_store.load(DataStore::defaultDataFolder(), &error)) {
        showError(error);
    }
    refreshTable();
    updateWindowTitle();
    statusBar()->showMessage(QString("Data folder: %1").arg(m_store.folderPath()));
}

void MainWindow::updateWindowTitle()
{
    setWindowTitle(QString("Magic Counting - %1").arg(QDir(m_store.folderPath()).filePath("transactions.json")));
}

void MainWindow::refreshTable()
{
    m_refreshing = true;
    m_table->clearContents();
    updateAccountColumns();
    m_table->setRowCount(0);
    m_rowToTransaction.clear();

    addOpeningBalanceRow();

    QMap<QString, double> balances;
    for (const Account &account : m_store.accounts) {
        balances.insert(account.name, account.openingBalance);
    }

    for (int transactionIndex = 0; transactionIndex < m_store.transactions.size(); ++transactionIndex) {
        const Transaction &transaction = m_store.transactions.at(transactionIndex);
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_rowToTransaction.append(transactionIndex);
        m_table->setItem(row, DateColumn, new QTableWidgetItem(transaction.date.toString(Qt::ISODate)));
        auto *amountItem = new QTableWidgetItem(moneyText(transaction.amount));
        amountItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, AmountColumn, amountItem);
        m_table->setItem(row, PartyColumn, new QTableWidgetItem(transaction.party));
        m_table->setItem(row, MemoColumn, new QTableWidgetItem(transaction.memo));

        auto *sourceCombo = new QComboBox(m_table);
        for (const Account &account : m_store.accounts) {
            if (account.kind == "source") {
                sourceCombo->addItem(account.name);
            }
        }
        if (sourceCombo->count() == 0) {
            for (const Account &account : m_store.accounts) {
                sourceCombo->addItem(account.name);
            }
        }
        sourceCombo->setEditable(true);
        sourceCombo->setCurrentText(transaction.sourceAccount);

        connect(sourceCombo, &QComboBox::currentTextChanged, this, [this, row](const QString &) {
            if (!m_refreshing && transactionIndexForRow(row) >= 0) {
                saveRowIfValid(row);
            }
        });

        m_table->setCellWidget(row, SourceColumn, sourceCombo);

        auto *targetsButton = new QPushButton(DataStore::formatSplits(transaction.targets), m_table);
        targetsButton->setFlat(true);
        targetsButton->setStyleSheet(targetsMatchAmount(transaction) ? QString() : "color: #b00020;");
        connect(targetsButton, &QPushButton::clicked, this, [this, row]() {
            openSplitEditor(row);
        });
        m_table->setCellWidget(row, TargetsColumn, targetsButton);

        updateComputedCells(row);

        for (const Account &account : m_store.accounts) {
            balances[account.name] += postingForAccount(transaction, account.name);
        }

        const QString month = transaction.date.toString("yyyy-MM");
        const bool lastTransaction = transactionIndex + 1 == m_store.transactions.size();
        const QString nextMonth = lastTransaction ? QString() : m_store.transactions.at(transactionIndex + 1).date.toString("yyyy-MM");
        if (lastTransaction || nextMonth != month) {
            addMonthBalanceRow(month, balances);
        }
    }

    m_refreshing = false;
}

void MainWindow::updateAccountColumns()
{
    QStringList headers = {"Date", "Source account", "Amount", "Other party", "Target accounts", "Memo"};
    for (const Account &account : m_store.accounts) {
        headers.append(account.name);
    }

    m_table->setColumnCount(FixedColumnCount + m_store.accounts.size());
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(DateColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(SourceColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(AmountColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(PartyColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(TargetsColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(MemoColumn, QHeaderView::Stretch);
    for (int column = FixedColumnCount; column < m_table->columnCount(); ++column) {
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
}

void MainWindow::updateComputedCells(int row)
{
    const int transactionIndex = transactionIndexForRow(row);
    if (transactionIndex < 0) {
        return;
    }

    const Transaction &transaction = m_store.transactions.at(transactionIndex);
    for (int accountIndex = 0; accountIndex < m_store.accounts.size(); ++accountIndex) {
        const int column = FixedColumnCount + accountIndex;
        const double posting = postingForAccount(transaction, m_store.accounts.at(accountIndex).name);
        auto *item = new QTableWidgetItem(posting == 0.0 ? QString() : moneyText(posting));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        if (posting < 0.0) {
            item->setForeground(QBrush(Qt::red));
        }
        m_table->setItem(row, column, item);
    }
}

void MainWindow::addOpeningBalanceRow()
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_rowToTransaction.append(kOpeningBalanceRow);

    m_table->setItem(row, DateColumn, readOnlyItem("Opening"));
    m_table->setItem(row, SourceColumn, readOnlyItem());
    m_table->setItem(row, AmountColumn, readOnlyItem());
    m_table->setItem(row, PartyColumn, readOnlyItem());
    m_table->setItem(row, TargetsColumn, readOnlyItem());
    m_table->setItem(row, MemoColumn, readOnlyItem("Initial balances"));

    for (int accountIndex = 0; accountIndex < m_store.accounts.size(); ++accountIndex) {
        auto *item = new QTableWidgetItem(moneyText(m_store.accounts.at(accountIndex).openingBalance));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, FixedColumnCount + accountIndex, item);
    }

    setGeneratedRowBackground(row, QColor(255, 248, 220));
}

void MainWindow::addMonthBalanceRow(const QString &month, const QMap<QString, double> &balances)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_rowToTransaction.append(kMonthBalanceRow);

    m_table->setItem(row, DateColumn, readOnlyItem(month));
    m_table->setItem(row, SourceColumn, readOnlyItem());
    m_table->setItem(row, AmountColumn, readOnlyItem());
    m_table->setItem(row, PartyColumn, readOnlyItem());
    m_table->setItem(row, TargetsColumn, readOnlyItem());
    m_table->setItem(row, MemoColumn, readOnlyItem("Month balance"));

    for (int accountIndex = 0; accountIndex < m_store.accounts.size(); ++accountIndex) {
        const Account &account = m_store.accounts.at(accountIndex);
        auto *item = readOnlyItem(moneyText(balances.value(account.name, 0.0)));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (balances.value(account.name, 0.0) < 0.0) {
            item->setForeground(QBrush(Qt::red));
        }
        m_table->setItem(row, FixedColumnCount + accountIndex, item);
    }

    setGeneratedRowBackground(row, QColor(226, 239, 255));
}

void MainWindow::setGeneratedRowBackground(int row, const QColor &color)
{
    QFont font = m_table->font();
    font.setBold(true);
    const QBrush textBrush(QColor(20, 24, 28));
    for (int column = 0; column < m_table->columnCount(); ++column) {
        QTableWidgetItem *item = m_table->item(row, column);
        if (!item) {
            item = readOnlyItem();
            m_table->setItem(row, column, item);
        }
        item->setBackground(QBrush(color));
        if (item->foreground().color() != Qt::red) {
            item->setForeground(textBrush);
        }
        item->setFont(font);
    }
}

int MainWindow::transactionIndexForRow(int row) const
{
    if (row < 0 || row >= m_rowToTransaction.size()) {
        return kMonthBalanceRow;
    }
    return m_rowToTransaction.at(row);
}

void MainWindow::openSplitEditor(int row)
{
    const int transactionIndex = transactionIndexForRow(row);
    if (transactionIndex < 0) {
        return;
    }

    bool ok = false;
    Transaction transaction = transactionFromRow(row, &ok);
    if (transaction.amount <= 0.0) {
        statusBar()->showMessage("Enter a positive Amount before editing target accounts", 5000);
        return;
    }

    SplitEditorDialog dialog(m_store.accounts, transaction.amount, this);
    dialog.setSplits(m_store.transactions.at(transactionIndex).targets);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    transaction.targets = dialog.splits();
    m_store.transactions[transactionIndex] = transaction;
    refreshTable();
    saveTransactions();
}

void MainWindow::saveRowIfValid(int row)
{
    bool ok = false;
    const Transaction transaction = transactionFromRow(row, &ok);
    if (!ok) {
        const int transactionIndex = transactionIndexForRow(row);
        if (transactionIndex >= 0) {
            m_store.transactions[transactionIndex] = transaction;
        }
        m_refreshing = true;
        if (auto *targetsButton = qobject_cast<QPushButton *>(m_table->cellWidget(row, TargetsColumn))) {
            targetsButton->setText(DataStore::formatSplits(transaction.targets));
            targetsButton->setStyleSheet("color: #b00020;");
        }
        statusBar()->showMessage("Rows need a source account, positive amount, and matching target total", 5000);
        updateComputedCells(row);
        m_refreshing = false;
        return;
    }

    const int transactionIndex = transactionIndexForRow(row);
    if (transactionIndex < 0) {
        return;
    }
    m_store.transactions[transactionIndex] = transaction;
    m_refreshing = true;
    updateComputedCells(row);

    if (auto *targetsButton = qobject_cast<QPushButton *>(m_table->cellWidget(row, TargetsColumn))) {
        targetsButton->setText(DataStore::formatSplits(transaction.targets));
        targetsButton->setStyleSheet(QString());
    }
    m_refreshing = false;
    saveTransactions();
}

void MainWindow::saveOpeningBalances()
{
    for (int accountIndex = 0; accountIndex < m_store.accounts.size(); ++accountIndex) {
        bool ok = false;
        const int column = FixedColumnCount + accountIndex;
        const double balance = m_table->item(0, column) ? m_table->item(0, column)->text().trimmed().toDouble(&ok) : 0.0;
        if (ok) {
            m_store.accounts[accountIndex].openingBalance = balance;
        }
    }

    QString error;
    if (!m_store.saveAccounts(&error)) {
        showError(error);
        return;
    }
    refreshTable();
    statusBar()->showMessage("Opening balances saved", 2000);
}

void MainWindow::addTransaction()
{
    QString defaultSource = m_store.accounts.isEmpty() ? QString() : m_store.accounts.first().name;
    for (const Account &account : m_store.accounts) {
        if (account.kind == "source") {
            defaultSource = account.name;
            break;
        }
    }
    const QString defaultParty;
    QString defaultTarget = "food";
    for (const Account &account : m_store.accounts) {
        if (account.kind != "source") {
            defaultTarget = account.name;
            break;
        }
    }
    m_store.transactions.append({QDate::currentDate(), defaultSource, 0.0, defaultParty, {{defaultTarget, 0.0}}, QString()});
    refreshTable();
    const int transactionIndex = m_store.transactions.size() - 1;
    for (int row = 0; row < m_rowToTransaction.size(); ++row) {
        if (m_rowToTransaction.at(row) == transactionIndex) {
            m_table->selectRow(row);
            break;
        }
    }
    saveTransactions();
}

void MainWindow::removeSelectedTransaction()
{
    const int row = m_table->currentRow();
    const int transactionIndex = transactionIndexForRow(row);
    if (transactionIndex < 0) {
        return;
    }
    m_store.transactions.removeAt(transactionIndex);
    refreshTable();
    saveTransactions();
}

void MainWindow::editAccounts()
{
    if (!AccountListDialog::editAccounts(this, &m_store.accounts)) {
        return;
    }

    QString error;
    if (!m_store.saveAccounts(&error)) {
        showError(error);
    }
    refreshTable();
}

void MainWindow::editParties()
{
    if (!AccountListDialog::editParties(this, &m_store.parties)) {
        return;
    }

    QString error;
    if (!m_store.saveParties(&error)) {
        showError(error);
    }
    refreshTable();
}

void MainWindow::openDataFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, "Open Data Folder", m_store.folderPath());
    if (folder.isEmpty()) {
        return;
    }

    QString error;
    if (!m_store.load(folder, &error)) {
        showError(error);
        return;
    }
    refreshTable();
    updateWindowTitle();
    statusBar()->showMessage(QString("Data folder: %1").arg(m_store.folderPath()));
}

void MainWindow::saveTransactions()
{
    QString error;
    if (!m_store.saveTransactions(&error)) {
        showError(error);
        return;
    }
    statusBar()->showMessage("Transactions saved", 2000);
}

void MainWindow::showError(const QString &message)
{
    QMessageBox::critical(this, "Magic Counting", message);
}

Transaction MainWindow::transactionFromRow(int row, bool *ok) const
{
    Transaction transaction;
    const int transactionIndex = transactionIndexForRow(row);
    bool valid = transactionIndex >= 0 && row < m_table->rowCount();
    transaction.date = QDate::fromString(m_table->item(row, DateColumn) ? m_table->item(row, DateColumn)->text().trimmed() : QString(),
                                         Qt::ISODate);
    if (!transaction.date.isValid()) {
        transaction.date = QDate::currentDate();
    }

    auto *sourceCombo = qobject_cast<QComboBox *>(m_table->cellWidget(row, SourceColumn));
    transaction.sourceAccount = sourceCombo ? sourceCombo->currentText().trimmed() : QString();
    bool amountOk = false;
    transaction.amount = m_table->item(row, AmountColumn) ? m_table->item(row, AmountColumn)->text().trimmed().toDouble(&amountOk) : 0.0;
    transaction.party = m_table->item(row, PartyColumn) ? m_table->item(row, PartyColumn)->text().trimmed() : QString();
    transaction.memo = m_table->item(row, MemoColumn) ? m_table->item(row, MemoColumn)->text() : QString();
    transaction.targets = transactionIndex >= 0 ? m_store.transactions.at(transactionIndex).targets : QList<Split>();
    valid = valid && amountOk && transaction.amount > 0.0 && !transaction.sourceAccount.isEmpty() && targetsMatchAmount(transaction);

    if (ok) {
        *ok = valid;
    }
    return transaction;
}

bool MainWindow::targetsMatchAmount(const Transaction &transaction) const
{
    double total = 0.0;
    for (const Split &split : transaction.targets) {
        total += split.amount;
    }
    return moneyCents(total) == moneyCents(transaction.amount);
}

double MainWindow::postingForAccount(const Transaction &transaction, const QString &accountName) const
{
    double posting = 0.0;
    if (transaction.sourceAccount == accountName) {
        posting -= transaction.amount;
    }
    for (const Split &split : transaction.targets) {
        if (split.account == accountName) {
            posting += split.amount;
        }
    }
    return posting;
}
