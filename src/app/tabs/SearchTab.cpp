#include "tabs/SearchTab.h"

#include "models/FileTableModel.h"

#include <QtConcurrent/QtConcurrentRun>

#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QKeySequence>
#include <QShortcut>
#include <QShowEvent>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#endif

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

QString pathToQString(const fs::path& path)
{
    const auto text = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()),
                             static_cast<qsizetype>(text.size()));
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
    m_pathEdit->setText(QStringLiteral("C:\\"));

    m_browseButton = new QPushButton(tr("Обзор…"), this);
    m_scanButton = new QPushButton(tr("Сканировать"), this);
    m_scanButton->setShortcut(QKeySequence(Qt::Key_F5));
    m_scanButton->setToolTip(tr("Сканировать или отменить (F5)"));

    pathRow->addWidget(pathLabel);
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(m_browseButton);
    pathRow->addWidget(m_scanButton);
    layout->addLayout(pathRow);

    m_includeHidden = new QCheckBox(tr("Включая скрытые (dot-файлы)"), this);
    m_includeHidden->setChecked(false);

    m_useMft = new QCheckBox(tr("Быстрый поиск MFT (нужен Администратор)"),
                             this);
    m_useMft->setChecked(false);
    m_useMft->setToolTip(tr("Читает таблицу MFT напрямую. Требуются права "
                            "Администратора и файловая система NTFS."));

    auto* optionsRow = new QHBoxLayout;
    optionsRow->addWidget(m_includeHidden);
    optionsRow->addWidget(m_useMft);
    optionsRow->addStretch(1);
    layout->addLayout(optionsRow);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(
        tr("Фильтр по имени (например, .exe)..."));
    m_filterEdit->setEnabled(false);
    layout->addWidget(m_filterEdit);

    m_model = new FileTableModel(this);

    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(Qt::UserRole);
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(FileTableModel::NameColumn);
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
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);

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
    connect(&m_watcher, &QFutureWatcher<disktool::app::ScanJobResult>::finished,
            this, &SearchTab::onScanFinished);
    connect(m_filterEdit, &QLineEdit::textChanged, m_proxy,
            &QSortFilterProxyModel::setFilterFixedString);
    connect(m_table, &QTableView::customContextMenuRequested,
            this, &SearchTab::showContextMenu);

    auto* focusPath = new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")),
                                    this);
    connect(focusPath, &QShortcut::activated, this, [this] {
        m_pathEdit->setFocus();
        m_pathEdit->selectAll();
    });

    auto* focusFilter = new QShortcut(QKeySequence::Find, this);
    connect(focusFilter, &QShortcut::activated, this, [this] {
        m_filterEdit->setFocus();
        m_filterEdit->selectAll();
    });

    connect(m_useMft, &QCheckBox::clicked, this, [this](bool checked) {
        if (!checked || m_isAdmin) {
            return;
        }

        QMessageBox::information(
            this, tr("Быстрый поиск MFT"),
            tr("Требуются права Администратора. Запустите DiscTool от имени "
               "администратора."));
        // Keep the checkbox checked: the scan will attempt MFT, report the
        // access error and fall back to the regular scanner automatically.
    });

    m_batchTimer.setInterval(100);
    connect(&m_batchTimer, &QTimer::timeout,
            this, &SearchTab::flushPendingBatch);
}

SearchTab::~SearchTab()
{
    m_cancelFlag.store(true);
    if (m_watcher.isRunning()) {
        m_watcher.waitForFinished();
    }
}

void SearchTab::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    if (m_initialScanStarted) {
        return;
    }
    m_initialScanStarted = true;

    if (m_isAdmin) {
        m_useMft->setChecked(true);
    }

    // Defer until the window is fully laid out so initial results are visible
    // immediately rather than before the first paint.
    QTimer::singleShot(0, this, &SearchTab::startScan);
}

void SearchTab::setAdminPrivileges(bool isAdmin)
{
    m_isAdmin = isAdmin;

    if (m_isAdmin) {
        m_useMft->setToolTip(tr("Читает таблицу MFT напрямую. Требуются права "
                                "Администратора и файловая система NTFS."));
        return;
    }

    m_useMft->setToolTip(tr("Требуются права Администратора."));
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
    if (!m_filterEdit->text().isEmpty()) {
        m_filterEdit->clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_batchMutex);
        m_incomingBatch.clear();
    }
    m_acceptBatches = true;
    m_batchTimer.start();

    const bool useMft = m_useMft->isChecked();

    emit scanStarted();

    disktool::core::ScanOptions options;
    options.includeHidden = m_includeHidden->isChecked();
    options.cancelToken = &m_cancelFlag;
    options.onProgress = [this](std::uintmax_t files, std::uintmax_t dirs) {
        emit scanProgress(static_cast<qulonglong>(files),
                          static_cast<qulonglong>(dirs));
    };
    options.onBatch = [this](const std::vector<disktool::core::FileEntry>& batch) {
        queueBatch(batch);
    };

    disktool::core::MftScanOptions mftOptions;
    mftOptions.cancelToken = &m_cancelFlag;
    mftOptions.onProgress = [this](std::uintmax_t files, std::uintmax_t dirs) {
        emit scanProgress(static_cast<qulonglong>(files),
                          static_cast<qulonglong>(dirs));
    };
    mftOptions.onBatch = [this](const std::vector<disktool::core::FileEntry>& batch) {
        queueBatch(batch);
    };

    setScanning(true);

    const auto root = toPath(text);
    auto future = QtConcurrent::run(
        [options = std::move(options), mftOptions = std::move(mftOptions),
         root, useMft]() mutable {
            return disktool::app::runScanJob(
                root, std::move(options), useMft, std::move(mftOptions));
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
    m_batchTimer.stop();
    flushPendingBatch();
    m_acceptBatches = false;

    disktool::app::ScanJobResult outcome;
    try {
        outcome = m_watcher.result();
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

    if (outcome.mftFailed) {
        setScanning(false);
        m_model->clearEntries();

        if (outcome.cancelled) {
            m_lastErrors = {outcome.mftError.empty() ? "cancelled by user"
                                                     : outcome.mftError};
            emit scanFinished(
                0, 0, 0,
                static_cast<qlonglong>(outcome.scan.duration.count()),
                static_cast<int>(m_lastErrors.size()), true);
            return;
        }

        // MFT is only an optimisation: warn, disable the option and restart
        // with the regular std::filesystem scanner.
        m_useMft->setChecked(false);
        const QString detail = outcome.mftError.empty()
            ? tr("неизвестная ошибка")
            : errorToString(outcome.mftError);
        QMessageBox::warning(
            this, tr("Быстрый поиск MFT"),
            tr("Не удалось прочитать MFT:\n%1\n\n"
               "Переключаюсь на обычный поиск...")
                .arg(detail));
        startScan();
        return;
    }

    auto result = std::move(outcome.scan);
    m_lastErrors = result.errors;

    // Batches already populated the model incrementally. Replacing it with the
    // same entries would reset a potentially huge model and freeze the GUI.
    const auto expectedRows = result.totalFiles + result.totalDirectories;
    if (m_model->rowCount() != static_cast<int>(expectedRows)) {
        m_model->setEntries(std::move(result.entries));
    }
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
    m_useMft->setEnabled(!scanning);
    m_filterEdit->setEnabled(!scanning);

    if (scanning) {
        m_table->setSortingEnabled(false);
        m_proxy->setDynamicSortFilter(false);
        // Clear the sort column entirely: with a sorted proxy, every inserted
        // batch would be merged into the sorted mapping (O(n) per batch).
        m_table->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
        m_proxy->sort(-1);
    } else {
        m_proxy->setDynamicSortFilter(true);

        // Sorting a million-row proxy at the end of an MFT scan would block
        // the GUI for seconds. Sort small results for convenience and leave
        // large results in discovery order; clicking a header still sorts.
        auto* header = m_table->horizontalHeader();
        if (m_model->rowCount() <= 20000) {
            header->setSortIndicator(FileTableModel::NameColumn,
                                     Qt::AscendingOrder);
            m_proxy->sort(FileTableModel::NameColumn, Qt::AscendingOrder);
        } else {
            header->setSortIndicator(-1, Qt::AscendingOrder);
            m_proxy->sort(-1);
        }
        m_table->setSortingEnabled(true);
    }

    m_scanButton->setEnabled(true);
    m_scanButton->setText(scanning ? tr("Отменить") : tr("Сканировать"));
}

void SearchTab::appendBatch(
    const std::vector<disktool::core::FileEntry>& entries)
{
    if (!m_acceptBatches) {
        return;
    }
    m_model->appendEntries(entries);
}

void SearchTab::queueBatch(
    const std::vector<disktool::core::FileEntry>& entries)
{
    if (entries.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_batchMutex);
    m_incomingBatch.insert(m_incomingBatch.end(), entries.begin(),
                           entries.end());
}

void SearchTab::flushPendingBatch()
{
    std::vector<disktool::core::FileEntry> batch;
    {
        std::lock_guard<std::mutex> lock(m_batchMutex);
        batch.swap(m_incomingBatch);
    }

    if (!m_acceptBatches || batch.empty()) {
        return;
    }

    appendBatch(batch);
}

void SearchTab::showContextMenu(const QPoint& position)
{
    const QModelIndex proxyIndex = m_table->indexAt(position);
    if (!proxyIndex.isValid()) {
        return;
    }

    const QModelIndex sourceIndex = m_table->model() == m_proxy
        ? m_proxy->mapToSource(proxyIndex)
        : proxyIndex;
    const auto* entry = m_model->entryAt(sourceIndex.row());
    if (entry == nullptr) {
        return;
    }

    const QString path = pathToQString(entry->path);

    QMenu menu(this);
    auto* openAction = menu.addAction(tr("Открыть"));
    auto* openPathAction = menu.addAction(tr("Открыть путь"));
    menu.addSeparator();
    auto* copyAction = menu.addAction(tr("Копировать полное имя"));
    menu.addSeparator();
    auto* propertiesAction = menu.addAction(tr("Свойства"));

    QAction* selected = menu.exec(m_table->viewport()->mapToGlobal(position));
    if (selected == nullptr) {
        return;
    }

    if (selected == openAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else if (selected == openPathAction) {
#ifdef _WIN32
        PIDLIST_ABSOLUTE pidl = nullptr;
        const std::wstring native = entry->path.wstring();
        if (SUCCEEDED(SHParseDisplayName(native.c_str(), nullptr, &pidl, 0,
                                         nullptr))) {
            SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
            CoTaskMemFree(pidl);
        }
#else
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
    } else if (selected == copyAction) {
        QGuiApplication::clipboard()->setText(path);
    } else if (selected == propertiesAction) {
#ifdef _WIN32
        const std::wstring native = entry->path.wstring();
        SHObjectProperties(nullptr, SHOP_FILEPATH, native.c_str(), nullptr);
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#endif
    }
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
