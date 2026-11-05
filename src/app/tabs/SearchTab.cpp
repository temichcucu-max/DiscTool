#include "tabs/SearchTab.h"

#include "models/FileTableModel.h"

#include <QtConcurrent/QtConcurrentRun>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <utility>

namespace fs = std::filesystem;

namespace {

fs::path toPath(const QString& text)
{
#ifdef _WIN32
    return fs::path(text.toStdWString());
#else
    return fs::path(text.toUtf8().constData());
#endif
}

QString errorToString(const std::string& message)
{
    return QString::fromUtf8(message.data(),
                             static_cast<qsizetype>(message.size()));
}

}  // namespace

SearchTab::SearchTab(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    auto* pathRow = new QHBoxLayout;
    auto* pathLabel = new QLabel(tr("Папка:"), this);

    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setPlaceholderText(QStringLiteral(R"(C:\Users\...)"));

    m_browseButton = new QPushButton(tr("Обзор…"), this);
    m_scanButton = new QPushButton(tr("Сканировать"), this);

    pathRow->addWidget(pathLabel);
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(m_browseButton);
    pathRow->addWidget(m_scanButton);
    layout->addLayout(pathRow);

    m_includeHidden = new QCheckBox(tr("Включая скрытые (dot-файлы)"), this);
    layout->addWidget(m_includeHidden);

    m_model = new FileTableModel(this);

    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(Qt::UserRole);
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setDynamicSortFilter(true);

    m_table = new QTableView(this);
    m_table->setModel(m_proxy);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(FileTableModel::NameColumn, Qt::AscendingOrder);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideMiddle);
    m_table->verticalHeader()->setVisible(false);

    auto* header = m_table->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setSectionResizeMode(FileTableModel::FullPathColumn,
                                 QHeaderView::Stretch);
    m_table->setColumnWidth(FileTableModel::NameColumn, 260);
    m_table->setColumnWidth(FileTableModel::SizeColumn, 100);
    m_table->setColumnWidth(FileTableModel::ModifiedColumn, 160);
    m_table->setColumnWidth(FileTableModel::TypeColumn, 90);

    layout->addWidget(m_table, 1);

    connect(m_browseButton, &QPushButton::clicked,
            this, &SearchTab::browseForDirectory);
    connect(m_scanButton, &QPushButton::clicked, this, [this] {
        if (m_watcher.isRunning()) {
            cancelScan();
        } else {
            startScan();
        }
    });
    connect(&m_watcher, &QFutureWatcher<disktool::core::ScanResult>::finished,
            this, &SearchTab::onScanFinished);
}

SearchTab::~SearchTab()
{
    m_cancelFlag.store(true);
    if (m_watcher.isRunning()) {
        m_watcher.waitForFinished();
    }
}

void SearchTab::browseForDirectory()
{
    const QString current = m_pathEdit->text().trimmed();
    const QString initial = (!current.isEmpty() && QFileInfo(current).isDir())
        ? current
        : QDir::homePath();

    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("Выбор папки"), initial);
    if (!directory.isEmpty()) {
        m_pathEdit->setText(QDir::toNativeSeparators(directory));
    }
}

void SearchTab::startScan()
{
    const QString text = m_pathEdit->text().trimmed();
    if (text.isEmpty()) {
        QMessageBox::warning(this, tr("Сканирование"),
                             tr("Укажите папку для сканирования."));
        return;
    }

    const QFileInfo info(text);
    if (!info.exists() || !info.isDir()) {
        QMessageBox::warning(this, tr("Сканирование"),
                             tr("Папка не найдена:\n%1").arg(text));
        return;
    }

    m_cancelFlag.store(false);
    m_lastErrors.clear();
    m_model->clearEntries();

    emit scanStarted();

    disktool::core::ScanOptions options;
    options.includeHidden = m_includeHidden->isChecked();
    options.cancelToken = &m_cancelFlag;
    options.onProgress = [this](std::uintmax_t files, std::uintmax_t dirs) {
        emit scanProgress(static_cast<qulonglong>(files),
                          static_cast<qulonglong>(dirs));
    };

    setScanning(true);

    const auto root = toPath(text);
    auto future = QtConcurrent::run(
        [options = std::move(options), root] {
            return disktool::core::Scanner(options).scan(root);
        });
    m_watcher.setFuture(future);
}

void SearchTab::cancelScan()
{
    if (!m_watcher.isRunning()) {
        return;
    }

    m_cancelFlag.store(true);
    m_scanButton->setText(tr("Отмена..."));
    m_scanButton->setEnabled(false);
}

void SearchTab::onScanFinished()
{
    disktool::core::ScanResult result;
    try {
        result = m_watcher.result();
    } catch (const std::exception& exception) {
        setScanning(false);
        m_lastErrors = {std::string("scan failed: ") + exception.what()};
        emit scanFinished(0, 0, 0, 0, 1, false);
        return;
    } catch (...) {
        setScanning(false);
        m_lastErrors = {"scan failed: unknown error"};
        emit scanFinished(0, 0, 0, 0, 1, false);
        return;
    }

    m_lastErrors = result.errors;
    m_model->setEntries(std::move(result.entries));
    setScanning(false);

    const bool cancelled = std::any_of(
        m_lastErrors.begin(), m_lastErrors.end(), [](const std::string& message) {
            return message.find("cancelled by user") != std::string::npos;
        });

    emit scanFinished(
        static_cast<qulonglong>(result.totalFiles),
        static_cast<qulonglong>(result.totalDirectories),
        static_cast<qulonglong>(result.totalBytes),
        static_cast<qlonglong>(result.duration.count()),
        static_cast<int>(m_lastErrors.size()),
        cancelled);
}

void SearchTab::setScanning(bool scanning)
{
    m_pathEdit->setEnabled(!scanning);
    m_browseButton->setEnabled(!scanning);
    m_includeHidden->setEnabled(!scanning);
    m_scanButton->setEnabled(true);
    m_scanButton->setText(scanning ? tr("Отменить") : tr("Сканировать"));
}

void SearchTab::showErrorsDialog()
{
    if (m_lastErrors.empty()) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Ошибки сканирования"));
    dialog.resize(760, 420);

    auto* layout = new QVBoxLayout(&dialog);
    auto* text = new QPlainTextEdit(&dialog);
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);

    QString content;
    for (const auto& message : m_lastErrors) {
        content += errorToString(message);
        content += QLatin1Char('\n');
    }
    text->setPlainText(content);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->addWidget(text);
    layout->addWidget(buttons);

    dialog.exec();
}
