#include "MainWindow.h"

#include "AccountListDialog.h"
#include "AppConfig.h"
#include "BankStatementImporter.h"
#include "LanguageManager.h"
#include "TransactionDetailsDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDate>
#include <QBrush>
#include <QColor>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QInputDialog>
#include <QMap>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QUrl>

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
    m_table->horizontalHeader()->setSectionsMovable(true);
    m_table->horizontalHeader()->setDropIndicatorShown(true);
    m_table->setAcceptDrops(true);
    m_table->viewport()->setAcceptDrops(true);
    m_table->viewport()->installEventFilter(this);
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
        Q_UNUSED(column);
        if (transactionIndexForRow(row) >= 0) {
            openTransactionDetailsEditor(row);
        }
    });

    connect(m_table->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int logicalIndex) {
        if (logicalIndex == BookingDateColumn) {
            m_transactionSortMode = TransactionSortMode::BookingDate;
            sortTransactions();
            refreshTable();
        } else if (logicalIndex == PaymentDateColumn) {
            m_transactionSortMode = TransactionSortMode::PaymentDate;
            sortTransactions();
            refreshTable();
        }
    });

    connect(m_table->horizontalHeader(), &QHeaderView::sectionMoved, this, [this](int logicalIndex, int oldVisualIndex, int newVisualIndex) {
        if (m_refreshing) {
            return;
        }
        if (logicalIndex < FixedColumnCount || newVisualIndex < FixedColumnCount) {
            m_refreshing = true;
            m_table->horizontalHeader()->moveSection(newVisualIndex, oldVisualIndex);
            m_refreshing = false;
            return;
        }
        QTimer::singleShot(0, this, [this]() {
            if (!m_refreshing) {
                saveAccountOrderFromHeader();
                refreshTable();
            }
        });
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
    m_classifyUnclassifiedAction = m_editMenu->addAction(QString(), this, &MainWindow::classifyUnclassifiedTransactions);

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
        m_classifyUnclassifiedAction->setText(tr("Classify Unclassified"));
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

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_table->viewport()) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto *dragEvent = static_cast<QDragMoveEvent *>(event);
            if (dragEvent->mimeData()->hasUrls() && transactionIndexForRow(m_table->rowAt(dragEvent->pos().y())) >= 0) {
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::Drop) {
            auto *dropEvent = static_cast<QDropEvent *>(event);
            const QList<QUrl> urls = dropEvent->mimeData()->urls();
            if (!urls.isEmpty()) {
                const int row = m_table->rowAt(dropEvent->pos().y());
                if (setReceiptForRow(row, urls.first().toLocalFile())) {
                    dropEvent->acceptProposedAction();
                    return true;
                }
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
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

    for (int transactionIndex = 0; transactionIndex < m_store.transactions.size(); ++transactionIndex) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_rowToTransaction.append(transactionIndex);
        refreshTransactionRow(row, transactionIndex);
    }

    m_refreshing = false;
    refreshGeneratedBalanceRows();
}

void MainWindow::refreshTransactionRow(int row, int transactionIndex)
{
    if (row < 0 || transactionIndex < 0 || transactionIndex >= m_store.transactions.size()) {
        return;
    }

    const Transaction &transaction = m_store.transactions.at(transactionIndex);
    m_table->setItem(row, BookingDateColumn, new QTableWidgetItem(transaction.date.toString(Qt::ISODate)));
    m_table->setItem(row, PaymentDateColumn, new QTableWidgetItem(transaction.paymentDate.isValid()
                                                                     ? transaction.paymentDate.toString(Qt::ISODate)
                                                                     : transaction.date.toString(Qt::ISODate)));
    auto *amountItem = new QTableWidgetItem(moneyText(transaction.amount));
    amountItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_table->setItem(row, AmountColumn, amountItem);
    m_table->setItem(row, PartyColumn, new QTableWidgetItem(transaction.party));
    m_table->setItem(row, ReceiptColumn, readOnlyItem(transaction.receiptPath.isEmpty() ? QString() : tr("Receipt")));

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

    connect(sourceCombo, &QComboBox::currentTextChanged, this, [this, sourceCombo](const QString &) {
        if (m_refreshing) {
            return;
        }
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (m_table->cellWidget(row, SourceColumn) == sourceCombo && transactionIndexForRow(row) >= 0) {
                saveRowIfValid(row);
                break;
            }
        }
    });

    m_table->setCellWidget(row, SourceColumn, sourceCombo);

    auto *targetsButton = new QPushButton(DataStore::formatSplits(transaction.targets), m_table);
    targetsButton->setFlat(true);
    targetsButton->setStyleSheet(targetsMatchAmount(transaction) ? QString() : "color: #b00020;");
    connect(targetsButton, &QPushButton::clicked, this, [this, targetsButton]() {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (m_table->cellWidget(row, TargetsColumn) == targetsButton) {
                openTransactionDetailsEditor(row);
                break;
            }
        }
    });
    m_table->setCellWidget(row, TargetsColumn, targetsButton);

    updateComputedCells(row);
}

void MainWindow::refreshGeneratedBalanceRows()
{
    m_refreshing = true;
    for (int row = m_rowToTransaction.size() - 1; row >= 0; --row) {
        if (m_rowToTransaction.at(row) == kMonthBalanceRow) {
            m_table->removeRow(row);
            m_rowToTransaction.removeAt(row);
        }
    }

    QStringList months;
    for (const Transaction &transaction : m_store.transactions) {
        const QString month = transaction.date.toString(QStringLiteral("yyyy-MM"));
        if (!month.isEmpty() && !months.contains(month)) {
            months.append(month);
        }
    }
    months.sort();

    QMap<QString, double> balances;
    for (const Account &account : m_store.accounts) {
        balances.insert(account.name, account.openingBalance);
    }

    for (const QString &month : months) {
        for (const Transaction &transaction : m_store.transactions) {
            if (transaction.date.toString(QStringLiteral("yyyy-MM")) == month) {
                for (const Account &account : m_store.accounts) {
                    balances[account.name] += postingForAccount(transaction, account.name);
                }
            }
        }

        const QDate monthEnd = monthEndDate(month);
        int insertRow = 1;
        for (int row = 1; row < m_rowToTransaction.size(); ++row) {
            const int transactionIndex = m_rowToTransaction.at(row);
            if (transactionIndex == kMonthBalanceRow) {
                insertRow = row + 1;
                continue;
            }
            if (transactionIndex >= 0 && sortDateForTransaction(m_store.transactions.at(transactionIndex)) <= monthEnd) {
                insertRow = row + 1;
            }
        }
        addMonthBalanceRow(month, balances, insertRow);
    }
    m_refreshing = false;
}

void MainWindow::refreshRowsAfterTransactionChange(int row)
{
    const int transactionIndex = transactionIndexForRow(row);
    if (transactionIndex < 0) {
        return;
    }
    m_refreshing = true;
    refreshTransactionRow(row, transactionIndex);
    m_refreshing = false;
    refreshGeneratedBalanceRows();
}

void MainWindow::sortTransactions()
{
    std::stable_sort(m_store.transactions.begin(), m_store.transactions.end(), [this](const Transaction &left, const Transaction &right) {
        const QDate leftPrimary = sortDateForTransaction(left);
        const QDate rightPrimary = sortDateForTransaction(right);
        if (leftPrimary != rightPrimary) {
            return leftPrimary < rightPrimary;
        }
        if (left.date != right.date) {
            return left.date < right.date;
        }
        if (left.paymentDate != right.paymentDate) {
            return left.paymentDate < right.paymentDate;
        }
        return left.id < right.id;
    });
}

QDate MainWindow::sortDateForTransaction(const Transaction &transaction) const
{
    if (m_transactionSortMode == TransactionSortMode::PaymentDate && transaction.paymentDate.isValid()) {
        return transaction.paymentDate;
    }
    return transaction.date;
}

QDate MainWindow::monthEndDate(const QString &month) const
{
    const QDate firstDay = QDate::fromString(month + QStringLiteral("-01"), Qt::ISODate);
    if (!firstDay.isValid()) {
        return QDate();
    }
    return QDate(firstDay.year(), firstDay.month(), firstDay.daysInMonth());
}

void MainWindow::updateAccountColumns()
{
    QStringList headers = {tr("Booked"), tr("Paid"), tr("Source account"), tr("Amount"), tr("Other party"), tr("Receipt"), tr("Target accounts")};
    for (const Account &account : m_store.accounts) {
        headers.append(account.name);
    }

    m_table->setColumnCount(FixedColumnCount + m_store.accounts.size());
    m_table->setHorizontalHeaderLabels(headers);
    {
        const QSignalBlocker blocker(m_table->horizontalHeader());
        for (int logicalColumn = 0; logicalColumn < m_table->columnCount(); ++logicalColumn) {
            const int visualIndex = m_table->horizontalHeader()->visualIndex(logicalColumn);
            if (visualIndex >= 0 && visualIndex != logicalColumn) {
                m_table->horizontalHeader()->moveSection(visualIndex, logicalColumn);
            }
        }
    }
    m_table->horizontalHeader()->setSectionResizeMode(BookingDateColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(PaymentDateColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(SourceColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(AmountColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(PartyColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(TargetsColumn, QHeaderView::Interactive);
    for (int column = FixedColumnCount; column < m_table->columnCount(); ++column) {
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
        if (m_table->columnWidth(column) < 90) {
            m_table->setColumnWidth(column, 110);
        }
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

    m_table->setItem(row, BookingDateColumn, readOnlyItem(tr("Opening")));
    m_table->setItem(row, PaymentDateColumn, readOnlyItem());
    m_table->setItem(row, SourceColumn, readOnlyItem());
    m_table->setItem(row, AmountColumn, readOnlyItem());
    m_table->setItem(row, PartyColumn, readOnlyItem());
    m_table->setItem(row, ReceiptColumn, readOnlyItem());
    m_table->setItem(row, TargetsColumn, readOnlyItem());

    for (int accountIndex = 0; accountIndex < m_store.accounts.size(); ++accountIndex) {
        auto *item = new QTableWidgetItem(moneyText(m_store.accounts.at(accountIndex).openingBalance));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, FixedColumnCount + accountIndex, item);
    }

    setGeneratedRowBackground(row, QColor(255, 248, 220));
}

void MainWindow::addMonthBalanceRow(const QString &month, const QMap<QString, double> &balances, int insertRow)
{
    const int row = insertRow < 0 ? m_table->rowCount() : insertRow;
    m_table->insertRow(row);
    if (insertRow < 0 || insertRow >= m_rowToTransaction.size()) {
        m_rowToTransaction.append(kMonthBalanceRow);
    } else {
        m_rowToTransaction.insert(row, kMonthBalanceRow);
    }

    m_table->setItem(row, BookingDateColumn, readOnlyItem(month));
    m_table->setItem(row, PaymentDateColumn, readOnlyItem());
    m_table->setItem(row, SourceColumn, readOnlyItem());
    m_table->setItem(row, AmountColumn, readOnlyItem());
    m_table->setItem(row, PartyColumn, readOnlyItem());
    m_table->setItem(row, ReceiptColumn, readOnlyItem());
    m_table->setItem(row, TargetsColumn, readOnlyItem());

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

void MainWindow::openTransactionDetailsEditor(int row)
{
    const int transactionIndex = transactionIndexForRow(row);
    if (transactionIndex < 0) {
        return;
    }

    bool ok = false;
    Transaction transaction = transactionFromRow(row, &ok);
    transaction.id = m_store.transactions.at(transactionIndex).id;
    transaction.memo = m_store.transactions.at(transactionIndex).memo;
    transaction.importSource = m_store.transactions.at(transactionIndex).importSource;
    transaction.importId = m_store.transactions.at(transactionIndex).importId;
    transaction.targets = m_store.transactions.at(transactionIndex).targets;

    TransactionDetailsDialog dialog(m_store.accounts, m_store.filePath(), this);
    dialog.setTransaction(transaction);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QDate previousBookingDate = m_store.transactions.at(transactionIndex).date;
    const QDate previousPaymentDate = m_store.transactions.at(transactionIndex).paymentDate;
    m_store.transactions[transactionIndex] = dialog.transaction();
    m_store.transactions[transactionIndex].receiptPath = storedReceiptPath(m_store.transactions.at(transactionIndex).receiptPath);
    QString error;
    if (!m_store.saveTransaction(transactionIndex, &error)) {
        showError(error);
        return;
    }
    if (previousBookingDate != m_store.transactions.at(transactionIndex).date
        || previousPaymentDate != m_store.transactions.at(transactionIndex).paymentDate) {
        sortTransactions();
        refreshTable();
    } else {
        refreshRowsAfterTransactionChange(row);
    }
    statusBar()->showMessage(tr("Transaction saved"), 2000);
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
        refreshGeneratedBalanceRows();
        m_refreshing = false;
        return;
    }

    const int transactionIndex = transactionIndexForRow(row);
    if (transactionIndex < 0) {
        return;
    }
    const QDate previousBookingDate = m_store.transactions.at(transactionIndex).date;
    const QDate previousPaymentDate = m_store.transactions.at(transactionIndex).paymentDate;
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
    if (previousBookingDate != transaction.date || previousPaymentDate != transaction.paymentDate) {
        sortTransactions();
        refreshTable();
    } else {
        refreshGeneratedBalanceRows();
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
    m_store.transactions.append({0, QDate::currentDate(), QDate::currentDate(), defaultSource, 0.0, defaultParty, {{defaultTarget, 0.0}}, QString()});
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
    QMap<QString, QString> renamedAccounts;
    if (!AccountListDialog::editAccounts(this, &m_store.accounts, &renamedAccounts)) {
        return;
    }

    renameAccountReferences(renamedAccounts);

    QString error;
    if (!renamedAccounts.isEmpty()) {
        if (!m_store.saveAll(&error)) {
            showError(error);
        }
    } else if (!m_store.saveAccounts(&error)) {
        showError(error);
    }
    refreshTable();
}

void MainWindow::editParties()
{
    if (!AccountListDialog::editParties(this, &m_store.parties, m_store.accounts)) {
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

void MainWindow::classifyUnclassifiedTransactions()
{
    const QString unclassifiedAccount = QString::fromUtf8(kUnclassifiedAccount);
    int changedCount = 0;

    for (Transaction &transaction : m_store.transactions) {
        const QString classifiedAccount = classifiedAccountForParty(transaction.party);
        if (classifiedAccount.isEmpty() || classifiedAccount == unclassifiedAccount) {
            continue;
        }

        bool changed = false;
        if (transaction.sourceAccount == unclassifiedAccount) {
            transaction.sourceAccount = classifiedAccount;
            changed = true;
        }
        for (Split &split : transaction.targets) {
            if (split.account == unclassifiedAccount) {
                split.account = classifiedAccount;
                changed = true;
            }
        }

        if (changed) {
            ++changedCount;
        }
    }

    if (changedCount == 0) {
        statusBar()->showMessage(tr("No unclassified transactions matched classification rules"), 5000);
        return;
    }

    QString error;
    if (!m_store.saveTransactions(&error)) {
        showError(error);
        return;
    }
    refreshTable();
    statusBar()->showMessage(tr("Classified %1 unclassified transactions").arg(changedCount), 5000);
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
        transaction.paymentDate = row.paymentDate.isValid() ? row.paymentDate : row.bookingDate;
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

    sortTransactions();

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
        selectedPath.append(".maccd");
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
    if (transactionIndex >= 0) {
        const Transaction &storedTransaction = m_store.transactions.at(transactionIndex);
        transaction.id = storedTransaction.id;
        transaction.memo = storedTransaction.memo;
        transaction.importSource = storedTransaction.importSource;
        transaction.importId = storedTransaction.importId;
        transaction.receiptPath = storedTransaction.receiptPath;
        transaction.targets = storedTransaction.targets;
    }
    transaction.date = QDate::fromString(m_table->item(row, BookingDateColumn) ? m_table->item(row, BookingDateColumn)->text().trimmed() : QString(),
                                         Qt::ISODate);
    if (!transaction.date.isValid()) {
        transaction.date = QDate::currentDate();
    }
    transaction.paymentDate = QDate::fromString(m_table->item(row, PaymentDateColumn) ? m_table->item(row, PaymentDateColumn)->text().trimmed() : QString(),
                                                Qt::ISODate);
    if (!transaction.paymentDate.isValid()) {
        transaction.paymentDate = transaction.date;
    }

    auto *sourceCombo = qobject_cast<QComboBox *>(m_table->cellWidget(row, SourceColumn));
    transaction.sourceAccount = sourceCombo ? sourceCombo->currentText().trimmed() : QString();
    bool amountOk = false;
    transaction.amount = m_table->item(row, AmountColumn) ? m_table->item(row, AmountColumn)->text().trimmed().toDouble(&amountOk) : 0.0;
    transaction.party = m_table->item(row, PartyColumn) ? m_table->item(row, PartyColumn)->text().trimmed() : QString();
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
        m_store.parties.append({partyName, QString()});
    }
}

void MainWindow::renameAccountReferences(const QMap<QString, QString> &renamedAccounts)
{
    if (renamedAccounts.isEmpty()) {
        return;
    }

    for (Transaction &transaction : m_store.transactions) {
        if (renamedAccounts.contains(transaction.sourceAccount)) {
            transaction.sourceAccount = renamedAccounts.value(transaction.sourceAccount);
        }
        for (Split &split : transaction.targets) {
            if (renamedAccounts.contains(split.account)) {
                split.account = renamedAccounts.value(split.account);
            }
        }
    }

    for (Party &party : m_store.parties) {
        if (renamedAccounts.contains(party.defaultAccount)) {
            party.defaultAccount = renamedAccounts.value(party.defaultAccount);
        }
    }

    QList<ImportClassificationRule> rules = AppConfig::importClassificationRules();
    bool rulesChanged = false;
    for (ImportClassificationRule &rule : rules) {
        if (renamedAccounts.contains(rule.account)) {
            rule.account = renamedAccounts.value(rule.account);
            rulesChanged = true;
        }
    }
    if (rulesChanged) {
        QString error;
        if (!AppConfig::saveImportClassificationRules(rules, &error)) {
            showError(error);
        }
    }
}

QString MainWindow::classifiedAccountForParty(const QString &partyName) const
{
    for (const Party &party : m_store.parties) {
        if (party.name.compare(partyName, Qt::CaseInsensitive) == 0 && !party.defaultAccount.isEmpty()) {
            return party.defaultAccount;
        }
    }

    const QList<ImportClassificationRule> rules = AppConfig::importClassificationRules();
    for (const ImportClassificationRule &rule : rules) {
        if (partyName.contains(rule.partyPattern, Qt::CaseInsensitive)) {
            return rule.account;
        }
    }
    return QString::fromUtf8(kUnclassifiedAccount);
}

QString MainWindow::storedReceiptPath(const QString &filePath) const
{
    if (filePath.trimmed().isEmpty()) {
        return QString();
    }

    const QFileInfo fileInfo(filePath);
    if (fileInfo.isRelative()) {
        return QDir::cleanPath(filePath);
    }

    const QString absolutePath = fileInfo.absoluteFilePath();
    const QDir baseDir(QFileInfo(m_store.filePath()).absolutePath());
    const QString relativePath = baseDir.relativeFilePath(absolutePath);
    if (!relativePath.startsWith(QStringLiteral("../")) && relativePath != QStringLiteral("..") && !QDir::isAbsolutePath(relativePath)) {
        return relativePath;
    }
    return absolutePath;
}

bool MainWindow::setReceiptForRow(int row, const QString &filePath)
{
    const int transactionIndex = transactionIndexForRow(row);
    if (transactionIndex < 0 || filePath.isEmpty()) {
        return false;
    }

    m_store.transactions[transactionIndex].receiptPath = storedReceiptPath(filePath);
    QString error;
    if (!m_store.saveTransaction(transactionIndex, &error)) {
        showError(error);
        return false;
    }

    m_refreshing = true;
    refreshTransactionRow(row, transactionIndex);
    m_refreshing = false;
    statusBar()->showMessage(tr("Receipt attached"), 2000);
    return true;
}

void MainWindow::saveAccountOrderFromHeader()
{
    if (!m_table || m_store.accounts.size() < 2) {
        return;
    }

    QVector<QPair<int, Account>> visualAccounts;
    visualAccounts.reserve(m_store.accounts.size());
    for (int accountIndex = 0; accountIndex < m_store.accounts.size(); ++accountIndex) {
        const int logicalColumn = FixedColumnCount + accountIndex;
        visualAccounts.append({m_table->horizontalHeader()->visualIndex(logicalColumn), m_store.accounts.at(accountIndex)});
    }
    std::sort(visualAccounts.begin(), visualAccounts.end(), [](const auto &left, const auto &right) {
        return left.first < right.first;
    });

    QList<Account> orderedAccounts;
    for (const auto &entry : visualAccounts) {
        orderedAccounts.append(entry.second);
    }

    bool changed = false;
    for (int index = 0; index < orderedAccounts.size(); ++index) {
        if (orderedAccounts.at(index).name != m_store.accounts.at(index).name) {
            changed = true;
            break;
        }
    }
    if (!changed) {
        return;
    }

    m_store.accounts = orderedAccounts;
    QString error;
    if (!m_store.saveAccounts(&error)) {
        showError(error);
    }
}
