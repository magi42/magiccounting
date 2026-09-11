#include "AccountListDialog.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QMap>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <functional>

namespace
{
const char *kSourceKind = "source";
const char *kCategoryKind = "category";
const char *kGroupKind = "group";

class NoWheelComboBox : public QComboBox
{
public:
    explicit NoWheelComboBox(QWidget *parent = nullptr)
        : QComboBox(parent)
    {
    }

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        event->ignore();
    }
};

class AccountTreeWidget : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;
    std::function<void()> afterDrop;

protected:
    void dropEvent(QDropEvent *event) override
    {
        QTreeWidget::dropEvent(event);
        if (afterDrop) {
            afterDrop();
        }
    }
};

QString accountKindLabel(const QString &kind)
{
    if (kind == QString::fromLatin1(kSourceKind)) {
        return QCoreApplication::translate("AccountListDialog", "Asset account");
    }
    if (kind == QString::fromLatin1(kGroupKind)) {
        return QCoreApplication::translate("AccountListDialog", "Account group");
    }
    return QCoreApplication::translate("AccountListDialog", "Income/expense account");
}

QComboBox *accountKindCombo(QWidget *parent, const QString &kind)
{
    auto *combo = new NoWheelComboBox(parent);
    combo->setEditable(false);
    combo->addItem(accountKindLabel(QString::fromLatin1(kSourceKind)), QString::fromLatin1(kSourceKind));
    combo->addItem(accountKindLabel(QString::fromLatin1(kCategoryKind)), QString::fromLatin1(kCategoryKind));
    combo->addItem(accountKindLabel(QString::fromLatin1(kGroupKind)), QString::fromLatin1(kGroupKind));
    const int index = combo->findData(kind.isEmpty() ? QString::fromLatin1(kCategoryKind) : kind);
    combo->setCurrentIndex(index >= 0 ? index : 1);
    return combo;
}

QComboBox *accountCombo(QWidget *parent, const QStringList &accountNames, bool includeEmpty = false)
{
    auto *combo = new NoWheelComboBox(parent);
    combo->setEditable(true);
    if (includeEmpty) {
        combo->addItem(QString());
    }
    combo->addItems(accountNames);
    return combo;
}

QComboBox *parentAccountCombo(QWidget *parent, const QStringList &accountNames)
{
    auto *combo = new NoWheelComboBox(parent);
    combo->setEditable(false);
    combo->addItem(QString(), QString());
    for (const QString &accountName : accountNames) {
        combo->addItem(accountName, accountName);
    }
    return combo;
}

bool accountNameLess(const Account &left, const Account &right)
{
    return QString::localeAwareCompare(left.name, right.name) < 0;
}

QTreeWidgetItem *createAccountItem(QTreeWidget *tree, QDialog *dialog, const Account &account)
{
    auto *item = new QTreeWidgetItem();
    item->setText(0, account.name);
    item->setData(0, Qt::UserRole, account.name);
    item->setData(0, Qt::UserRole + 2, account.kind);
    item->setData(0, Qt::UserRole + 1, account.openingBalance);
    item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
    return item;
}

QString itemAccountName(const QTreeWidgetItem *item)
{
    return item ? item->text(0).trimmed() : QString();
}

QString itemOriginalName(const QTreeWidgetItem *item)
{
    return item ? item->data(0, Qt::UserRole).toString() : QString();
}

bool itemHasChildren(const QTreeWidgetItem *item)
{
    return item && item->childCount() > 0;
}

QString effectiveItemKind(const QTreeWidgetItem *item)
{
    if (itemHasChildren(item)) {
        return QString::fromLatin1(kGroupKind);
    }
    if (item && item->treeWidget()) {
        const auto *kindCombo = qobject_cast<QComboBox *>(item->treeWidget()->itemWidget(const_cast<QTreeWidgetItem *>(item), 1));
        if (kindCombo) {
            return kindCombo->currentData().toString();
        }
    }
    return item ? item->data(0, Qt::UserRole + 2).toString() : QString::fromLatin1(kCategoryKind);
}

bool accountHasTraffic(const QString &accountName, const QList<Account> &originalAccounts, const QList<Transaction> &transactions)
{
    if (accountName.isEmpty()) {
        return false;
    }
    for (const Account &account : originalAccounts) {
        if (account.name == accountName && !qFuzzyIsNull(account.openingBalance)) {
            return true;
        }
    }
    for (const Transaction &transaction : transactions) {
        if (transaction.sourceAccount == accountName) {
            return true;
        }
        for (const Split &split : transaction.targets) {
            if (split.account == accountName) {
                return true;
            }
        }
    }
    return false;
}

QString trafficProblemText()
{
    return QCoreApplication::translate("AccountListDialog",
                                       "This account is an account group but has direct money postings. Create a new account and move this account's child accounts under it.");
}

void attachAccountKindCombo(QTreeWidget *tree, QTreeWidgetItem *item)
{
    const QString kind = itemHasChildren(item) ? QString::fromLatin1(kGroupKind) : item->data(0, Qt::UserRole + 2).toString();
    auto *combo = accountKindCombo(tree, kind);
    if (itemHasChildren(item)) {
        combo->setEnabled(false);
    }
    tree->setItemWidget(item, 1, combo);
}

QSet<QString> descendantNames(const QTreeWidgetItem *item)
{
    QSet<QString> names;
    if (!item) {
        return names;
    }
    for (int index = 0; index < item->childCount(); ++index) {
        const QTreeWidgetItem *child = item->child(index);
        names.insert(itemAccountName(child));
        names.unite(descendantNames(child));
    }
    return names;
}

QList<QTreeWidgetItem *> allAccountItems(QTreeWidget *tree)
{
    QList<QTreeWidgetItem *> items;
    if (!tree) {
        return items;
    }
    std::function<void(QTreeWidgetItem *)> collect = [&](QTreeWidgetItem *item) {
        items.append(item);
        for (int index = 0; index < item->childCount(); ++index) {
            collect(item->child(index));
        }
    };
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        collect(tree->topLevelItem(index));
    }
    return items;
}

QStringList possibleParentNames(QTreeWidget *tree, QTreeWidgetItem *item)
{
    QStringList names;
    const QSet<QString> descendants = descendantNames(item);
    const QString selfName = itemAccountName(item);
    for (QTreeWidgetItem *candidate : allAccountItems(tree)) {
        const QString name = itemAccountName(candidate);
        if (!name.isEmpty() && name != selfName && !descendants.contains(name)) {
            names.append(name);
        }
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

void updateAccountTreeWidgets(QTreeWidget *tree, const QList<Account> &originalAccounts, const QList<Transaction> &transactions);

void attachParentCombo(QTreeWidget *tree, QTreeWidgetItem *item, const QList<Account> &originalAccounts, const QList<Transaction> &transactions)
{
    auto *combo = parentAccountCombo(tree, possibleParentNames(tree, item));
    const QString parentName = item->parent() ? itemAccountName(item->parent()) : QString();
    const int index = combo->findData(parentName);
    combo->setCurrentIndex(index >= 0 ? index : 0);
    QObject::connect(combo, qOverload<int>(&QComboBox::activated), tree, [tree, item, combo, originalAccounts, transactions](int) {
        if (!item) {
            return;
        }
        QTreeWidgetItem *taken = nullptr;
        if (QTreeWidgetItem *oldParent = item->parent()) {
            taken = oldParent->takeChild(oldParent->indexOfChild(item));
        } else {
            taken = tree->takeTopLevelItem(tree->indexOfTopLevelItem(item));
        }
        if (!taken) {
            return;
        }

        const QString parentName = combo->currentData().toString();
        QTreeWidgetItem *newParent = nullptr;
        for (QTreeWidgetItem *candidate : allAccountItems(tree)) {
            if (itemAccountName(candidate) == parentName) {
                newParent = candidate;
                break;
            }
        }
        if (newParent) {
            newParent->addChild(taken);
            newParent->setExpanded(true);
        } else {
            tree->addTopLevelItem(taken);
        }
        updateAccountTreeWidgets(tree, originalAccounts, transactions);
    });
    tree->setItemWidget(item, 2, combo);
}

void updateProblemIndicator(QTreeWidgetItem *item, const QList<Account> &originalAccounts, const QList<Transaction> &transactions)
{
    const bool problem = effectiveItemKind(item) == QString::fromLatin1(kGroupKind)
        && accountHasTraffic(itemOriginalName(item), originalAccounts, transactions);
    item->setText(3, problem ? QStringLiteral("!") : QString());
    item->setToolTip(3, problem ? trafficProblemText() : QString());
}

void updateAccountTreeWidgets(QTreeWidget *tree, const QList<Account> &originalAccounts, const QList<Transaction> &transactions)
{
    for (QTreeWidgetItem *item : allAccountItems(tree)) {
        if (!itemHasChildren(item)) {
            if (const auto *kindCombo = qobject_cast<QComboBox *>(tree->itemWidget(item, 1))) {
                if (kindCombo->isEnabled()) {
                    item->setData(0, Qt::UserRole + 2, kindCombo->currentData().toString());
                }
            }
        }
        attachAccountKindCombo(tree, item);
        attachParentCombo(tree, item, originalAccounts, transactions);
        updateProblemIndicator(item, originalAccounts, transactions);
    }
}

void addSortedAccountChildren(QTreeWidget *tree,
                              QDialog *dialog,
                              QTreeWidgetItem *parentItem,
                              const QString &parentName,
                              const QList<Account> &accounts,
                              QSet<QString> *inserted)
{
    QList<Account> children;
    for (const Account &account : accounts) {
        if (account.parentAccount == parentName && !inserted->contains(account.name)) {
            children.append(account);
        }
    }
    std::sort(children.begin(), children.end(), accountNameLess);

    for (const Account &account : children) {
        auto *item = createAccountItem(tree, dialog, account);
        if (parentItem) {
            parentItem->addChild(item);
        } else {
            tree->addTopLevelItem(item);
        }
        Q_UNUSED(dialog);
        attachAccountKindCombo(tree, item);
        inserted->insert(account.name);
        addSortedAccountChildren(tree, dialog, item, account.name, accounts, inserted);
        item->setExpanded(true);
    }
}

void sortTreeItem(QTreeWidgetItem *item)
{
    item->sortChildren(0, Qt::AscendingOrder);
    for (int index = 0; index < item->childCount(); ++index) {
        sortTreeItem(item->child(index));
    }
}

void collectAccountItems(QTreeWidgetItem *item, const QString &parentName, QList<Account> *accounts)
{
    const QString name = item->text(0).trimmed();
    const auto *kindCombo = qobject_cast<QComboBox *>(item->treeWidget()->itemWidget(item, 1));
    const QString kind = effectiveItemKind(item);
    if (!name.isEmpty()) {
        accounts->append({name,
                          kind.isEmpty() ? QString::fromLatin1(kCategoryKind) : kind,
                          item->data(0, Qt::UserRole + 1).toDouble(),
                          parentName});
    }

    QList<QTreeWidgetItem *> children;
    children.reserve(item->childCount());
    for (int index = 0; index < item->childCount(); ++index) {
        children.append(item->child(index));
    }
    std::sort(children.begin(), children.end(), [](const QTreeWidgetItem *left, const QTreeWidgetItem *right) {
        return QString::localeAwareCompare(left->text(0), right->text(0)) < 0;
    });
    for (QTreeWidgetItem *child : children) {
        collectAccountItems(child, name, accounts);
    }
}

QStringList accountNames(const QList<Account> &accounts, bool includeGroups = false)
{
    QStringList names;
    for (const Account &account : accounts) {
        if (includeGroups || account.kind != QString::fromLatin1(kGroupKind)) {
            names.append(account.name);
        }
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}
}

AccountListDialog::AccountListDialog(QWidget *parent, bool treeMode)
    : QDialog(parent)
{
    auto *layout = new QVBoxLayout(this);
    if (treeMode) {
        m_tree = new AccountTreeWidget(this);
        m_tree->setColumnCount(4);
        m_tree->setHeaderLabels({tr("Name"), tr("Kind"), tr("Parent account"), tr("Problem")});
        m_tree->header()->setSectionResizeMode(QHeaderView::Stretch);
        m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
        m_tree->setDragDropMode(QAbstractItemView::InternalMove);
        m_tree->setDefaultDropAction(Qt::MoveAction);
        m_tree->setDropIndicatorShown(true);
        layout->addWidget(m_tree);
    } else {
        m_table = new QTableWidget(this);
        layout->addWidget(m_table);
    }

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *addButton = buttonBox->addButton(tr("Add"), QDialogButtonBox::ActionRole);
    auto *removeButton = buttonBox->addButton(tr("Remove"), QDialogButtonBox::ActionRole);

    connect(addButton, &QPushButton::clicked, this, [this]() {
        if (m_tree) {
            Account account;
            account.kind = QString::fromLatin1(kCategoryKind);
            auto *item = createAccountItem(m_tree, this, account);
            auto *parentItem = m_tree->currentItem();
            if (parentItem) {
                parentItem->addChild(item);
                parentItem->setExpanded(true);
            } else {
                m_tree->addTopLevelItem(item);
            }
            attachAccountKindCombo(m_tree, item);
            m_tree->setCurrentItem(item);
            m_tree->editItem(item, 0);
            return;
        }

        const int row = m_table->rowCount();
        m_table->insertRow(row);
        for (int column = 0; column < m_table->columnCount(); ++column) {
            m_table->setItem(row, column, new QTableWidgetItem());
        }
        m_table->setCurrentCell(row, 0);
        m_table->editItem(m_table->item(row, 0));
    });

    connect(removeButton, &QPushButton::clicked, this, [this]() {
        if (m_tree) {
            delete m_tree->currentItem();
            return;
        }

        const int row = m_table->currentRow();
        if (row >= 0) {
            m_table->removeRow(row);
        }
    });

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}

bool AccountListDialog::editAccounts(QWidget *parent,
                                     QList<Account> *accounts,
                                     const QList<Transaction> &transactions,
                                     QMap<QString, QString> *renamedAccounts)
{
    AccountListDialog dialog(parent, true);
    dialog.setWindowTitle(dialog.tr("Accounts"));
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect availableGeometry = screen->availableGeometry();
        dialog.resize(qMax(720, availableGeometry.width() / 2), availableGeometry.height());
    }

    QSet<QString> inserted;
    addSortedAccountChildren(dialog.m_tree, &dialog, nullptr, QString(), *accounts, &inserted);
    QList<Account> orphanAccounts;
    for (const Account &account : *accounts) {
        if (!inserted.contains(account.name)) {
            orphanAccounts.append(account);
        }
    }
    std::sort(orphanAccounts.begin(), orphanAccounts.end(), accountNameLess);
    for (const Account &account : orphanAccounts) {
        auto *item = createAccountItem(dialog.m_tree, &dialog, account);
        dialog.m_tree->addTopLevelItem(item);
        attachAccountKindCombo(dialog.m_tree, item);
        inserted.insert(account.name);
        addSortedAccountChildren(dialog.m_tree, &dialog, item, account.name, *accounts, &inserted);
        item->setExpanded(true);
    }
    for (int index = 0; index < dialog.m_tree->topLevelItemCount(); ++index) {
        sortTreeItem(dialog.m_tree->topLevelItem(index));
    }
    dialog.m_tree->sortItems(0, Qt::AscendingOrder);
    if (auto *tree = dynamic_cast<AccountTreeWidget *>(dialog.m_tree)) {
        tree->afterDrop = [&dialog, accounts, transactions]() {
            updateAccountTreeWidgets(dialog.m_tree, *accounts, transactions);
        };
    }
    updateAccountTreeWidgets(dialog.m_tree, *accounts, transactions);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    if (renamedAccounts) {
        renamedAccounts->clear();
    }

    accounts->clear();
    for (int index = 0; index < dialog.m_tree->topLevelItemCount(); ++index) {
        collectAccountItems(dialog.m_tree->topLevelItem(index), QString(), accounts);
    }

    for (const Account &account : *accounts) {
        const QList<QTreeWidgetItem *> items = dialog.m_tree->findItems(account.name, Qt::MatchExactly | Qt::MatchRecursive, 0);
        if (items.isEmpty()) {
            continue;
        }
        const QString originalName = items.first()->data(0, Qt::UserRole).toString();
        if (renamedAccounts && originalName != account.name && !originalName.isEmpty()) {
            renamedAccounts->insert(originalName, account.name);
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

    const QStringList names = accountNames(accounts);
    for (int row = 0; row < parties->size(); ++row) {
        dialog.m_table->setItem(row, 0, new QTableWidgetItem(parties->at(row).name));
        auto *combo = accountCombo(&dialog, names, true);
        combo->setCurrentText(parties->at(row).defaultAccount);
        dialog.m_table->setCellWidget(row, 1, combo);
    }

    connect(dialog.m_table, &QTableWidget::cellChanged, &dialog, [&dialog, names](int row, int) {
        if (!dialog.m_table->cellWidget(row, 1)) {
            dialog.m_table->setCellWidget(row, 1, accountCombo(&dialog, names, true));
        }
    });

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    parties->clear();
    for (int row = 0; row < dialog.m_table->rowCount(); ++row) {
        const QString name = dialog.m_table->item(row, 0) ? dialog.m_table->item(row, 0)->text().trimmed() : QString();
        const auto *combo = qobject_cast<QComboBox *>(dialog.m_table->cellWidget(row, 1));
        const QString defaultAccount = combo
                                           ? combo->currentText().trimmed()
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

    const QStringList names = accountNames(accounts);
    for (int row = 0; row < rules->size(); ++row) {
        dialog.m_table->setItem(row, 0, new QTableWidgetItem(rules->at(row).partyPattern));
        auto *combo = accountCombo(&dialog, names);
        combo->setCurrentText(rules->at(row).account);
        dialog.m_table->setCellWidget(row, 1, combo);
    }

    connect(dialog.m_table, &QTableWidget::cellChanged, &dialog, [&dialog, names](int row, int) {
        if (!dialog.m_table->cellWidget(row, 1)) {
            dialog.m_table->setCellWidget(row, 1, accountCombo(&dialog, names));
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
        const auto *combo = qobject_cast<QComboBox *>(dialog.m_table->cellWidget(row, 1));
        const QString account = combo
                                    ? combo->currentText().trimmed()
                                    : (dialog.m_table->item(row, 1) ? dialog.m_table->item(row, 1)->text().trimmed() : QString());
        if (!partyPattern.isEmpty() && !account.isEmpty()) {
            rules->append({partyPattern, account});
        }
    }
    return true;
}
