#include "MainWindow.h"

#include "AccountListDialog.h"
#include "AppConfig.h"
#include "BankStatementImporter.h"
#include "LanguageManager.h"
#include "SplitEditorDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDate>
#include <QBrush>
#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QInputDialog>
#include <QMap>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QToolBar>

#include <algorithm>
#include <cmath>

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
const char *kUnclassifiedAccount = "Luokittelemattomat";

QTableWidgetItem *readOnlyItem(const QString &text = QString())
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}
}

MainWindow::MainWindow(const QString &accountingFile, QWidget *parent)
    : QMainWindow(parent)
    , m_initialAccountingFile(accountingFile)
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

    buildMenus();

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

    setWindowTitle(tr("Magic Counting"));
    resize(1100, 620);
}

void MainWindow::buildMenus()
{
    m_fileMenu = menuBar()->addMenu(QString());
    m_openAccountingFileAction = m_fileMenu->addAction(QString(), this, &MainWindow::openAccountingFile);
    m_saveAction = m_fileMenu->addAction(QString(), this, &MainWindow::saveTransactions);
    m_saveAsAction = m_fileMenu->addAction(QString(), this, &MainWindow::saveAccountingFileAs);
    m_fileMenu->addSeparator();
    m_importBankStatementAction = m_fileMenu->addAction(QString(), this, &MainWindow::importBankStatement);
    m_fileMenu->addSeparator();
    m_quitAction = m_fileMenu->addAction(QString(), this, &QWidget::close);

    m_editMenu = menuBar()->addMenu(QString());
    m_addTransactionAction = m_editMenu->addAction(QString(), this, &MainWindow::addTransaction);
    m_removeTransactionAction = m_editMenu->addAction(QString(), this, &MainWindow::removeSelectedTransaction);
    m_editMenu->addSeparator();
    m_editAccountsAction = m_editMenu->addAction(QString(), this, &MainWindow::editAccounts);
    m_editPartiesAction = m_editMenu->addAction(QString(), this, &MainWindow::editParties);
    m_editImportClassificationRulesAction = m_editMenu->addAction(QString(), this, &MainWindow::editImportClassificationRules);

    m_languageMenu = menuBar()->addMenu(QString());
    m_languageActionGroup = new QActionGroup(this);
    m_languageActionGroup->setExclusive(true);
    m_systemLanguageAction = m_languageMenu->addAction(QString());
    m_englishLanguageAction = m_languageMenu->addAction(QString());
    m_finnishLanguageAction = m_languageMenu->addAction(QString());
    for (QAction *action : {m_systemLanguageAction, m_englishLanguageAction, m_finnishLanguageAction}) {
        action->setCheckable(true);
        m_languageActionGroup->addAction(action);
    }
    m_systemLanguageAction->setData(QStringLiteral("system"));
    m_englishLanguageAction->setData(QStringLiteral("en"));
    m_finnishLanguageAction->setData(QStringLiteral("fi"));
    connect(m_languageActionGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        changeLanguage(action->data().toString());
    });

    m_toolbar = addToolBar(QString());
    m_toolbarAddAction = m_toolbar->addAction(QString(), this, &MainWindow::addTransaction);
    m_toolbarRemoveAction = m_toolbar->addAction(QString(), this, &MainWindow::removeSelectedTransaction);
    m_toolbar->addSeparator();
    m_toolbarAccountsAction = m_toolbar->addAction(QString(), this, &MainWindow::editAccounts);
    m_toolbarPartiesAction = m_toolbar->addAction(QString(), this, &MainWindow::editParties);

    retranslateUi();
}

void MainWindow::retranslateUi()
{
    if (m_fileMenu) {
        m_fileMenu->setTitle(tr("File"));
        m_openAccountingFileAction->setText(tr("Open Accounting File..."));
        m_saveAction->setText(tr("Save"));
        m_saveAsAction->setText(tr("Save As..."));
        m_importBankStatementAction->setText(tr("Import Bank Statement..."));
        m_quitAction->setText(tr("Quit"));
    }
    if (m_editMenu) {
        m_editMenu->setTitle(tr("Edit"));
        m_addTransactionAction->setText(tr("Add Transaction"));
        m_removeTransactionAction->setText(tr("Remove Transaction"));
        m_editAccountsAction->setText(tr("Accounts..."));
        m_editPartiesAction->setText(tr("Parties..."));
        m_editImportClassificationRulesAction->setText(tr("Import Classification Rules..."));
    }
    if (m_languageMenu) {
        m_languageMenu->setTitle(tr("Language"));
        m_systemLanguageAction->setText(tr("System default"));
        m_englishLanguageAction->setText(tr("English"));
        m_finnishLanguageAction->setText(tr("Finnish"));

        const QString languageCode = AppConfig::languageCode();
        m_systemLanguageAction->setChecked(languageCode == QStringLiteral("system"));
        m_englishLanguageAction->setChecked(languageCode == QStringLiteral("en"));
        m_finnishLanguageAction->setChecked(languageCode == QStringLiteral("fi"));
    }
    if (m_toolbar) {
        m_toolbar->setWindowTitle(tr("Transactions"));
        m_toolbarAddAction->setText(tr("Add"));
        m_toolbarRemoveAction->setText(tr("Remove"));
        m_toolbarAccountsAction->setText(tr("Accounts"));
        m_toolbarPartiesAction->setText(tr("Parties"));
    }

    updateWindowTitle();
    updateAccountColumns();
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
        refreshTable();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::loadInitialData()
{
    QString filePath = m_initialAccountingFile;
    if (filePath.isEmpty()) {
        filePath = QFileDialog::getSaveFileName(this,
                                                tr("Choose Accounting File"),
                                                DataStore::defaultAccountingFile(),
                                                DataStore::fileFilter());
    }
    if (filePath.isEmpty()) {
        filePath = DataStore::defaultAccountingFile();
    }

    QString error;
    if (!m_store.load(filePath, &error)) {
        showError(error);
    } else if (!AppConfig::saveAccountingFile(m_store.filePath(), &error)) {
        showError(error);
    }
    refreshTable();
    updateWindowTitle();
    statusBar()->showMessage(tr("Accounting file: %1").arg(m_store.filePath()));
}

void MainWindow::updateWindowTitle()
{
    setWindowTitle(tr("Magic Counting - %1").arg(m_store.filePath()));
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
    QStringList headers = {tr("Date"), tr("Source account"), tr("Amount"), tr("Other party"), tr("Target accounts"), tr("Memo")};
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

    m_table->setItem(row, DateColumn, readOnlyItem(tr("Opening")));
    m_table->setItem(row, SourceColumn, readOnlyItem());
    m_table->setItem(row, AmountColumn, readOnlyItem());
    m_table->setItem(row, PartyColumn, readOnlyItem());
    m_table->setItem(row, TargetsColumn, readOnlyItem());
    m_table->setItem(row, MemoColumn, readOnlyItem(tr("Initial balances")));

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
    m_table->setItem(row, MemoColumn, readOnlyItem(tr("Month balance")));

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
        statusBar()->showMessage(tr("Enter a positive Amount before editing target accounts"), 5000);
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
    QString error;
    if (!m_store.saveTransaction(transactionIndex, &error)) {
        showError(error);
    }
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
        statusBar()->showMessage(tr("Rows need a source account, positive amount, and matching target total"), 5000);
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
    QString error;
    if (!m_store.saveTransaction(transactionIndex, &error)) {
        showError(error);
        return;
    }
    statusBar()->showMessage(tr("Transaction saved"), 2000);
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
    statusBar()->showMessage(tr("Opening balances saved"), 2000);
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
    m_store.transactions.append({0, QDate::currentDate(), defaultSource, 0.0, defaultParty, {{defaultTarget, 0.0}}, QString()});
    refreshTable();
    const int transactionIndex = m_store.transactions.size() - 1;
    for (int row = 0; row < m_rowToTransaction.size(); ++row) {
        if (m_rowToTransaction.at(row) == transactionIndex) {
            m_table->selectRow(row);
            break;
        }
    }
    QString error;
    if (!m_store.saveTransaction(transactionIndex, &error)) {
        showError(error);
    }
}

void MainWindow::removeSelectedTransaction()
{
    const int row = m_table->currentRow();
    const int transactionIndex = transactionIndexForRow(row);
    if (transactionIndex < 0) {
        return;
    }
    QString error;
    if (!m_store.removeTransaction(transactionIndex, &error)) {
        showError(error);
        return;
    }
    refreshTable();
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

void MainWindow::editImportClassificationRules()
{
    QList<ImportClassificationRule> rules = AppConfig::importClassificationRules();
    if (!AccountListDialog::editImportClassificationRules(this, &rules, m_store.accounts)) {
        return;
    }

    QString error;
    if (!AppConfig::saveImportClassificationRules(rules, &error)) {
        showError(error);
    }
}

void MainWindow::changeLanguage(const QString &languageCode)
{
    const QString normalizedCode = LanguageManager::normalizeLanguageCode(languageCode);
    QString error;
    if (!AppConfig::saveLanguageCode(normalizedCode, &error)) {
        showError(error);
        return;
    }
    LanguageManager::install(qApp, normalizedCode);
    retranslateUi();
    refreshTable();
}

void MainWindow::openAccountingFile()
{
    const QString filePath = QFileDialog::getOpenFileName(this,
                                                          tr("Open Accounting File"),
                                                          QFileInfo(m_store.filePath()).absolutePath(),
                                                          DataStore::fileFilter());
    if (filePath.isEmpty()) {
        return;
    }

    QString error;
    if (!m_store.load(filePath, &error)) {
        showError(error);
        return;
    }
    if (!AppConfig::saveAccountingFile(m_store.filePath(), &error)) {
        showError(error);
        return;
    }
    refreshTable();
    updateWindowTitle();
    statusBar()->showMessage(tr("Accounting file: %1").arg(m_store.filePath()));
}

void MainWindow::importBankStatement()
{
    SBankCsvImporter importer;
    const QString filePath = QFileDialog::getOpenFileName(this,
                                                          tr("Import Bank Statement"),
                                                          QFileInfo(m_store.filePath()).absolutePath(),
                                                          importer.fileFilter());
    if (filePath.isEmpty()) {
        return;
    }

    QStringList sourceAccounts;
    for (const Account &account : m_store.accounts) {
        if (account.kind == "source") {
            sourceAccounts.append(account.name);
        }
    }
    if (sourceAccounts.isEmpty()) {
        for (const Account &account : m_store.accounts) {
            sourceAccounts.append(account.name);
        }
    }
    if (sourceAccounts.isEmpty()) {
        sourceAccounts.append(QStringLiteral("S-Pankki"));
    }

    bool accepted = false;
    const QString bankAccount = QInputDialog::getItem(this,
                                                      tr("Import Bank Statement"),
                                                      tr("Bank account for these transactions:"),
                                                      sourceAccounts,
                                                      0,
                                                      true,
                                                      &accepted)
                                    .trimmed();
    if (!accepted || bankAccount.isEmpty()) {
        return;
    }

    QList<ImportedBankTransaction> importedRows;
    QString error;
    if (!importer.importFile(filePath, &importedRows, &error)) {
        showError(error);
        return;
    }

    ensureAccount(bankAccount, QStringLiteral("source"));
    ensureAccount(QString::fromUtf8(kUnclassifiedAccount), QStringLiteral("category"));

    int importedCount = 0;
    int skippedCount = 0;
    for (const ImportedBankTransaction &row : importedRows) {
        if (m_store.hasImportedTransaction(importer.id(), row.externalId)) {
            ++skippedCount;
            continue;
        }

        const double amount = std::abs(row.signedAmount);
        if (amount == 0.0) {
            ++skippedCount;
            continue;
        }

        const QString party = row.party.isEmpty() ? importer.displayName() : row.party;
        const QString counterAccount = classifiedAccountForParty(party);
        ensureAccount(counterAccount, QStringLiteral("category"));
        ensureParty(party);

        Transaction transaction;
        transaction.date = row.bookingDate;
        transaction.amount = amount;
        transaction.party = party;
        transaction.memo = row.memo;
        transaction.importSource = importer.id();
        transaction.importId = row.externalId;
        if (row.signedAmount < 0.0) {
            transaction.sourceAccount = bankAccount;
            transaction.targets = {{counterAccount, amount}};
        } else {
            transaction.sourceAccount = counterAccount;
            transaction.targets = {{bankAccount, amount}};
        }
        m_store.transactions.append(transaction);
        ++importedCount;
    }

    std::stable_sort(m_store.transactions.begin(), m_store.transactions.end(), [](const Transaction &left, const Transaction &right) {
        return left.date < right.date;
    });

    if (!m_store.saveAll(&error)) {
        showError(error);
        return;
    }

    refreshTable();
    statusBar()->showMessage(tr("Imported %1 transactions, skipped %2 duplicates").arg(importedCount).arg(skippedCount), 5000);
}

void MainWindow::saveTransactions()
{
    QString error;
    if (!m_store.saveTransactions(&error)) {
        showError(error);
        return;
    }
    statusBar()->showMessage(tr("Transactions saved"), 2000);
}

void MainWindow::saveAccountingFileAs()
{
    QString selectedPath = QFileDialog::getSaveFileName(this,
                                                        tr("Save Accounting File As"),
                                                        m_store.filePath(),
                                                        DataStore::fileFilter());
    if (selectedPath.isEmpty()) {
        return;
    }
    if (QFileInfo(selectedPath).suffix().isEmpty()) {
        selectedPath.append(".macc");
    }

    QString error;
    if (!m_store.saveAs(selectedPath, &error)) {
        showError(error);
        return;
    }
    if (!AppConfig::saveAccountingFile(m_store.filePath(), &error)) {
        showError(error);
        return;
    }

    updateWindowTitle();
    statusBar()->showMessage(tr("Accounting file saved as: %1").arg(m_store.filePath()), 3000);
}

void MainWindow::showError(const QString &message)
{
    QMessageBox::critical(this, tr("Magic Counting"), message);
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

bool MainWindow::accountExists(const QString &accountName) const
{
    for (const Account &account : m_store.accounts) {
        if (account.name == accountName) {
            return true;
        }
    }
    return false;
}

bool MainWindow::partyExists(const QString &partyName) const
{
    for (const Party &party : m_store.parties) {
        if (party.name == partyName) {
            return true;
        }
    }
    return false;
}

void MainWindow::ensureAccount(const QString &accountName, const QString &kind)
{
    if (!accountName.isEmpty() && !accountExists(accountName)) {
        m_store.accounts.append({accountName, kind, 0.0});
    }
}

void MainWindow::ensureParty(const QString &partyName)
{
    if (!partyName.isEmpty() && !partyExists(partyName)) {
        m_store.parties.append({partyName});
    }
}

QString MainWindow::classifiedAccountForParty(const QString &partyName) const
{
    const QList<ImportClassificationRule> rules = AppConfig::importClassificationRules();
    for (const ImportClassificationRule &rule : rules) {
        if (partyName.contains(rule.partyPattern, Qt::CaseInsensitive)) {
            return rule.account;
        }
    }
    return QString::fromUtf8(kUnclassifiedAccount);
}
