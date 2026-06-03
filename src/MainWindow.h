#pragma once

#include "DataStore.h"

#include <QAction>
#include <QActionGroup>
#include <QColor>
#include <QEvent>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QTableWidget>
#include <QToolBar>
#include <QVector>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &accountingFile = QString(), QWidget *parent = nullptr);

private:
    enum Column {
        BookingDateColumn,
        PaymentDateColumn,
        SourceColumn,
        AmountColumn,
        PartyColumn,
        ReceiptColumn,
        TargetsColumn,
        FixedColumnCount
    };

    enum class TransactionSortMode {
        BookingDate,
        PaymentDate
    };

    DataStore m_store;
    QTableWidget *m_table = nullptr;
    QVector<int> m_rowToTransaction;
    bool m_refreshing = false;
    TransactionSortMode m_transactionSortMode = TransactionSortMode::BookingDate;
    QString m_initialAccountingFile;
    QMenu *m_fileMenu = nullptr;
    QMenu *m_editMenu = nullptr;
    QMenu *m_languageMenu = nullptr;
    QToolBar *m_toolbar = nullptr;
    QAction *m_openAccountingFileAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_saveAsAction = nullptr;
    QAction *m_importBankStatementAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_addTransactionAction = nullptr;
    QAction *m_removeTransactionAction = nullptr;
    QAction *m_editAccountsAction = nullptr;
    QAction *m_editPartiesAction = nullptr;
    QAction *m_editImportClassificationRulesAction = nullptr;
    QAction *m_classifyUnclassifiedAction = nullptr;
    QAction *m_toolbarAddAction = nullptr;
    QAction *m_toolbarRemoveAction = nullptr;
    QAction *m_toolbarAccountsAction = nullptr;
    QAction *m_toolbarPartiesAction = nullptr;
    QActionGroup *m_languageActionGroup = nullptr;
    QAction *m_systemLanguageAction = nullptr;
    QAction *m_englishLanguageAction = nullptr;
    QAction *m_finnishLanguageAction = nullptr;

    void buildUi();
    void buildMenus();
    void retranslateUi();
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void loadInitialData();
    void updateWindowTitle();
    void refreshTable();
    void refreshTransactionRow(int row, int transactionIndex);
    void refreshGeneratedBalanceRows();
    void refreshRowsAfterTransactionChange(int row);
    void sortTransactions();
    QDate sortDateForTransaction(const Transaction &transaction) const;
    QDate monthEndDate(const QString &month) const;
    void updateAccountColumns();
    void updateComputedCells(int row);
    void addOpeningBalanceRow();
    void addMonthBalanceRow(const QString &month, const QMap<QString, double> &balances, int insertRow = -1);
    void setGeneratedRowBackground(int row, const QColor &color);
    int transactionIndexForRow(int row) const;
    void openTransactionDetailsEditor(int row);
    void saveRowIfValid(int row);
    void saveOpeningBalances();
    void addTransaction();
    void removeSelectedTransaction();
    void editAccounts();
    void editParties();
    void editImportClassificationRules();
    void classifyUnclassifiedTransactions();
    void changeLanguage(const QString &languageCode);
    void openAccountingFile();
    void importBankStatement();
    void saveTransactions();
    void saveAccountingFileAs();
    void showError(const QString &message);
    Transaction transactionFromRow(int row, bool *ok) const;
    bool targetsMatchAmount(const Transaction &transaction) const;
    double postingForAccount(const Transaction &transaction, const QString &accountName) const;
    bool accountExists(const QString &accountName) const;
    bool partyExists(const QString &partyName) const;
    void ensureAccount(const QString &accountName, const QString &kind);
    void ensureParty(const QString &partyName);
    void renameAccountReferences(const QMap<QString, QString> &renamedAccounts);
    QString classifiedAccountForParty(const QString &partyName) const;
    QString storedReceiptPath(const QString &filePath) const;
    bool setReceiptForRow(int row, const QString &filePath);
    void saveAccountOrderFromHeader();
};
