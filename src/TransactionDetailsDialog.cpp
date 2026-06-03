#include "TransactionDetailsDialog.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QPixmap>
#include <QProcess>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
constexpr int kReceiptPreviewMinimumWidth = 312;

qint64 moneyCents(double value)
{
    return qRound64(value * 100.0);
}
}

TransactionDetailsDialog::TransactionDetailsDialog(const QList<Account> &accounts,
                                                   const QString &accountingFilePath,
                                                   QWidget *parent)
    : QDialog(parent)
    , m_accounts(accounts)
    , m_bookingDateEdit(new QDateEdit(this))
    , m_paymentDateEdit(new QDateEdit(this))
    , m_sourceAccountCombo(new QComboBox(this))
    , m_amountSpin(new QDoubleSpinBox(this))
    , m_partyEdit(new QLineEdit(this))
    , m_memoEdit(new QTextEdit(this))
    , m_importSourceEdit(new QLineEdit(this))
    , m_importIdEdit(new QLineEdit(this))
    , m_accountingFilePath(accountingFilePath)
    , m_receiptImageLabel(new QLabel(this))
    , m_receiptPathLabel(new QLabel(this))
    , m_receiptScrollArea(new QScrollArea(this))
    , m_openReceiptButton(new QPushButton(tr("Open receipt"), this))
    , m_targetsTable(new QTableWidget(this))
    , m_totalLabel(new QLabel(this))
{
    setWindowTitle(tr("Transaction Details"));

    m_bookingDateEdit->setCalendarPopup(true);
    m_bookingDateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_paymentDateEdit->setCalendarPopup(true);
    m_paymentDateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));

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
    formLayout->addRow(tr("Booked"), m_bookingDateEdit);
    formLayout->addRow(tr("Paid"), m_paymentDateEdit);
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

    m_receiptImageLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    m_receiptImageLabel->setMinimumSize(kReceiptPreviewMinimumWidth, 360);
    m_receiptImageLabel->setText(tr("Drop receipt image here"));
    m_receiptImageLabel->setAcceptDrops(true);
    m_receiptImageLabel->installEventFilter(this);
    m_receiptScrollArea->setWidget(m_receiptImageLabel);
    m_receiptScrollArea->setWidgetResizable(false);
    m_receiptScrollArea->setAcceptDrops(true);
    m_receiptScrollArea->setMinimumWidth(kReceiptPreviewMinimumWidth);
    m_receiptScrollArea->viewport()->setAcceptDrops(true);
    m_receiptScrollArea->viewport()->installEventFilter(this);
    m_receiptPathLabel->setWordWrap(true);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttonBox->button(QDialogButtonBox::Ok);
    auto *addSplitButton = buttonBox->addButton(tr("Add counterpart"), QDialogButtonBox::ActionRole);
    auto *removeSplitButton = buttonBox->addButton(tr("Remove counterpart"), QDialogButtonBox::ActionRole);
    auto *removeReceiptButton = buttonBox->addButton(tr("Remove receipt"), QDialogButtonBox::ActionRole);

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
    connect(removeReceiptButton, &QPushButton::clicked, this, [this]() {
        setReceiptPath(QString());
    });
    connect(m_openReceiptButton, &QPushButton::clicked, this, [this]() {
        const QString path = absoluteReceiptPath();
        if (!path.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        }
    });
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_sourceAccountCombo, &QComboBox::currentTextChanged, this, &TransactionDetailsDialog::updateValidity);
    connect(m_amountSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &TransactionDetailsDialog::updateValidity);

    auto *leftLayout = new QVBoxLayout();
    leftLayout->addLayout(formLayout);
    leftLayout->addWidget(m_targetsTable);
    leftLayout->addWidget(m_totalLabel);

    auto *receiptLayout = new QVBoxLayout();
    receiptLayout->addWidget(new QLabel(tr("Receipt"), this));
    receiptLayout->addWidget(m_receiptScrollArea, 1);
    receiptLayout->addWidget(m_receiptPathLabel);
    receiptLayout->addWidget(m_openReceiptButton);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->addLayout(leftLayout, 5);
    contentLayout->addLayout(receiptLayout, 4);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(contentLayout);
    layout->addWidget(buttonBox);

    resize(980, 680);
}

void TransactionDetailsDialog::setTransaction(const Transaction &transaction)
{
    m_transactionId = transaction.id;
    m_bookingDateEdit->setDate(transaction.date.isValid() ? transaction.date : QDate::currentDate());
    m_paymentDateEdit->setDate(transaction.paymentDate.isValid() ? transaction.paymentDate : m_bookingDateEdit->date());
    m_sourceAccountCombo->setCurrentText(transaction.sourceAccount);
    m_amountSpin->setValue(transaction.amount);
    m_partyEdit->setText(transaction.party);
    m_memoEdit->setPlainText(transaction.memo);
    m_importSourceEdit->setText(transaction.importSource);
    m_importIdEdit->setText(transaction.importId);
    setReceiptPath(transaction.receiptPath);

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
    transaction.date = m_bookingDateEdit->date();
    transaction.paymentDate = m_paymentDateEdit->date();
    transaction.sourceAccount = m_sourceAccountCombo->currentText().trimmed();
    transaction.amount = m_amountSpin->value();
    transaction.party = m_partyEdit->text().trimmed();
    transaction.memo = m_memoEdit->toPlainText();
    transaction.importSource = m_importSourceEdit->text().trimmed();
    transaction.importId = m_importIdEdit->text().trimmed();
    transaction.receiptPath = m_receiptPath;
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

bool TransactionDetailsDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_receiptImageLabel || watched == m_receiptScrollArea->viewport()) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto *dragEvent = static_cast<QDragMoveEvent *>(event);
            if (dragEvent->mimeData()->hasUrls()) {
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::Drop) {
            auto *dropEvent = static_cast<QDropEvent *>(event);
            const QList<QUrl> urls = dropEvent->mimeData()->urls();
            if (!urls.isEmpty()) {
                setReceiptPath(urls.first().toLocalFile());
                dropEvent->acceptProposedAction();
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

QString TransactionDetailsDialog::absoluteReceiptPath() const
{
    if (m_receiptPath.isEmpty()) {
        return QString();
    }
    const QFileInfo info(m_receiptPath);
    if (info.isAbsolute()) {
        return info.absoluteFilePath();
    }
    return QDir(QFileInfo(m_accountingFilePath).absolutePath()).absoluteFilePath(m_receiptPath);
}

void TransactionDetailsDialog::setReceiptPath(const QString &receiptPath)
{
    m_receiptPath = receiptPath.trimmed();
    updateReceiptPreview();
}

void TransactionDetailsDialog::updateReceiptPreview()
{
    m_receiptPathLabel->setText(m_receiptPath.isEmpty() ? tr("No receipt") : m_receiptPath);

    const QString path = absoluteReceiptPath();
    m_openReceiptButton->setEnabled(!path.isEmpty());
    if (receiptIsPdf()) {
        QString errorMessage;
        const QString previewPath = renderPdfPreview(path, &errorMessage);
        if (previewPath.isEmpty()) {
            m_receiptImageLabel->setPixmap(QPixmap());
            m_receiptImageLabel->resize(kReceiptPreviewMinimumWidth, 360);
            m_receiptImageLabel->setText(tr("Could not render PDF receipt\nPath: %1\nReason: %2").arg(path, errorMessage));
            return;
        }

        QImageReader reader(previewPath);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (image.isNull()) {
            m_receiptImageLabel->setPixmap(QPixmap());
            m_receiptImageLabel->resize(kReceiptPreviewMinimumWidth, 360);
            m_receiptImageLabel->setText(tr("Could not load PDF preview\nPath: %1\nReason: %2").arg(previewPath, reader.errorString()));
            return;
        }

        showReceiptImage(image);
        return;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (path.isEmpty() || image.isNull()) {
        m_receiptImageLabel->setPixmap(QPixmap());
        m_receiptImageLabel->resize(kReceiptPreviewMinimumWidth, 360);
        if (m_receiptPath.isEmpty()) {
            m_receiptImageLabel->setText(tr("Drop receipt image here"));
        } else {
            QStringList formats;
            for (const QByteArray &format : QImageReader::supportedImageFormats()) {
                formats.append(QString::fromLatin1(format));
            }
            m_receiptImageLabel->setText(tr("Could not load receipt image\nPath: %1\nReason: %2\nSupported formats: %3")
                                             .arg(path,
                                                  reader.errorString(),
                                                  formats.join(QStringLiteral(", "))));
        }
        return;
    }

    showReceiptImage(image);
}

void TransactionDetailsDialog::showReceiptImage(const QImage &image)
{
    const double widthMillimeters = imageWidthMillimeters(image);
    const int targetWidth = qMax(kReceiptPreviewMinimumWidth, m_receiptScrollArea->viewport()->width() - 2);
    QImage displayImage = image;

    if (image.width() > 0
        && ((widthMillimeters > 0.0 && widthMillimeters < 100.0)
            || (widthMillimeters <= 0.0 && image.height() > image.width()))) {
        displayImage = image.scaledToWidth(targetWidth, Qt::SmoothTransformation);
    }

    const QPixmap pixmap = QPixmap::fromImage(displayImage);
    m_receiptImageLabel->setText(QString());
    m_receiptImageLabel->setPixmap(pixmap);
    m_receiptImageLabel->resize(pixmap.size());
}

double TransactionDetailsDialog::imageWidthMillimeters(const QImage &image) const
{
    if (image.dotsPerMeterX() <= 0) {
        return 0.0;
    }
    return (static_cast<double>(image.width()) / static_cast<double>(image.dotsPerMeterX())) * 1000.0;
}

bool TransactionDetailsDialog::receiptIsPdf() const
{
    return QFileInfo(m_receiptPath).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0;
}

QString TransactionDetailsDialog::renderPdfPreview(const QString &path, QString *errorMessage) const
{
    if (!m_pdfPreviewDir.isValid()) {
        if (errorMessage) {
            *errorMessage = tr("Could not create temporary preview folder");
        }
        return QString();
    }

    const QString outputPrefix = QDir(m_pdfPreviewDir.path()).filePath(QStringLiteral("receipt-preview"));
    QProcess process;
    process.setProgram(QStringLiteral("pdftoppm"));
    process.setArguments({QStringLiteral("-jpeg"),
                          QStringLiteral("-singlefile"),
                          QStringLiteral("-f"),
                          QStringLiteral("1"),
                          QStringLiteral("-l"),
                          QStringLiteral("1"),
                          QStringLiteral("-r"),
                          QStringLiteral("150"),
                          path,
                          outputPrefix});
    process.start();
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished();
        if (errorMessage) {
            *errorMessage = tr("pdftoppm did not finish in time");
        }
        return QString();
    }

    const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = stderrText.isEmpty()
                ? tr("pdftoppm exited with code %1").arg(process.exitCode())
                : stderrText;
        }
        return QString();
    }

    const QString previewPath = outputPrefix + QStringLiteral(".jpg");
    if (!QFileInfo::exists(previewPath)) {
        if (errorMessage) {
            *errorMessage = tr("pdftoppm did not create %1").arg(previewPath);
        }
        return QString();
    }
    return previewPath;
}
