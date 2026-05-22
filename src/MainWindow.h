#pragma once

#include "DataStore.h"

#include <QColor>
#include <QMainWindow>
#include <QMap>
#include <QTableWidget>
#include <QVector>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &accountingFolder = QString(), QWidget *parent = nullptr);

private:
    enum Column {
        DateColumn,
        SourceColumn,
        AmountColumn,
        PartyColumn,
        TargetsColumn,
        MemoColumn,
        FixedColumnCount
    };

    DataStore m_store;
    QTableWidget *m_table = nullptr;
    QVector<int> m_rowToTransaction;
    bool m_refreshing = false;
    QString m_initialAccountingFolder;

    void buildUi();
    void loadInitialData();
    void updateWindowTitle();
    void refreshTable();
    void updateAccountColumns();
    void updateComputedCells(int row);
    void addOpeningBalanceRow();
    void addMonthBalanceRow(const QString &month, const QMap<QString, double> &balances);
    void setGeneratedRowBackground(int row, const QColor &color);
    int transactionIndexForRow(int row) const;
    void openSplitEditor(int row);
    void saveRowIfValid(int row);
    void saveOpeningBalances();
    void addTransaction();
    void removeSelectedTransaction();
    void editAccounts();
    void editParties();
    void openDataFolder();
    void saveTransactions();
    void showError(const QString &message);
    Transaction transactionFromRow(int row, bool *ok) const;
    bool targetsMatchAmount(const Transaction &transaction) const;
    double postingForAccount(const Transaction &transaction, const QString &accountName) const;
};
