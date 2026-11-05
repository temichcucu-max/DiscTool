#include "tabs/CleanupTab.h"

#include "FormatUtils.h"

#include <QtConcurrent/QtConcurrentRun>

#include <QAbstractButton>
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
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

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

QString pathToQString(const fs::path& path)
{
    const auto text = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()),
                             static_cast<qsizetype>(text.size()));
}

QString errorToString(const std::string& message)
{
    return QString::fromUtf8(message.data(),
                             static_cast<qsizetype>(message.size()));
}

}  // namespace

CleanupTab::CleanupTab(QWidget* parent)
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

    auto* optionsRow = new QHBoxLayout;
    m_findFiles = new QCheckBox(tr("Искать пустые файлы"), this);
    m_findFiles->setChecked(true);
    m_findFiles->setToolTip(tr("Применяется при следующем сканировании"));

    m_findDirectories = new QCheckBox(tr("Искать пустые папки"), this);
    m_findDirectories->setChecked(true);
    m_findDirectories->setToolTip(tr("Применяется при следующем сканировании"));

    optionsRow->addWidget(m_findFiles);
    optionsRow->addWidget(m_findDirectories);
    optionsRow->addStretch(1);
    layout->addLayout(optionsRow);

    m_model = new QStandardItemModel(0, ColumnCount, this);
    m_model->setHorizontalHeaderLabels(
        {tr("Тип"), tr("Путь"), tr("Размер")});

    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideMiddle);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSortingEnabled(false);

    auto* header = m_table->horizontalHeader();
    header->setSectionResizeMode(TypeColumn, QHeaderView::Fixed);
    header->setSectionResizeMode(PathColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(SizeColumn, QHeaderView::ResizeToContents);
    m_table->setColumnWidth(TypeColumn, 110);

    layout->addWidget(m_table, 1);

    auto* trashRow = new QHBoxLayout;
    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    m_progressBar->setTextVisible(true);

    m_cancelTrashButton = new QPushButton(tr("Отменить"), this);
    m_cancelTrashButton->setVisible(false);

    trashRow->addWidget(m_progressBar, 1);
    trashRow->addWidget(m_cancelTrashButton);
    layout->addLayout(trashRow);

    auto* footer = new QHBoxLayout;
    m_summary = new QLabel(this);
    m_deleteButton = new QPushButton(tr("Удалить выбранное (в Корзину)"), this);
    m_deleteButton->setEnabled(false);

    footer->addWidget(m_summary, 1);
    footer->addWidget(m_deleteButton);
    layout->addLayout(footer);

    connect(m_browseButton, &QPushButton::clicked,
            this, &CleanupTab::browseForDirectory);
    connect(m_scanButton, &QPushButton::clicked, this, [this] {
        if (m_watcher.isRunning()) {
            cancelScan();
        } else {
            startScan();
        }
    });
    connect(&m_watcher, &QFutureWatcher<disktool::core::ScanResult>::finished,
            this, &CleanupTab::onScanFinished);
    connect(m_model, &QStandardItemModel::itemChanged,
            this, [this](QStandardItem*) { updateDeleteButton(); });
    connect(m_deleteButton, &QPushButton::clicked,
            this, &CleanupTab::deleteSelected);

    m_trashWorker = new TrashWorker(this);
    connect(m_trashWorker, &TrashWorker::progress, this,
            [this](int processed, int total) {
                m_progressBar->setRange(0, total);
                m_progressBar->setValue(processed);
            },
            Qt::QueuedConnection);
    connect(&m_trashWatcher, &QFutureWatcher<TrashResult>::finished,
            this, &CleanupTab::onTrashFinished);
    connect(m_cancelTrashButton, &QPushButton::clicked,
            this, &CleanupTab::cancelTrash);

    updateSummary();
}

CleanupTab::~CleanupTab()
{
    m_cancelFlag.store(true);
    if (m_trashWorker != nullptr) {
        m_trashWorker->requestAbort();
    }
    if (m_watcher.isRunning()) {
        m_watcher.waitForFinished();
    }
    if (m_trashWatcher.isRunning()) {
        m_trashWatcher.waitForFinished();
    }
}

void CleanupTab::browseForDirectory()
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

void CleanupTab::startScan()
{
    const QString text = m_pathEdit->text().trimmed();
    if (text.isEmpty()) {
        QMessageBox::warning(this, tr("Очистка"),
                             tr("Укажите папку для сканирования."));
        return;
    }

    if (!m_findFiles->isChecked() && !m_findDirectories->isChecked()) {
        QMessageBox::warning(this, tr("Очистка"),
                             tr("Выберите, что искать: пустые файлы или папки."));
        return;
    }

    const QFileInfo info(text);
    if (!info.exists() || !info.isDir()) {
        QMessageBox::warning(this, tr("Очистка"),
                             tr("Папка не найдена:\n%1").arg(text));
        return;
    }

    m_cancelFlag.store(false);
    m_lastErrors.clear();
    m_rootPath = toPath(text);

    m_model->removeRows(0, m_model->rowCount());
    m_items.clear();
    updateSummary();
    updateDeleteButton();

    emit scanStarted();

    disktool::core::ScanOptions options;
    options.cancelToken = &m_cancelFlag;
    options.onProgress = [this](std::uintmax_t files, std::uintmax_t dirs) {
        emit scanProgress(static_cast<qulonglong>(files),
                          static_cast<qulonglong>(dirs));
    };

    setScanning(true);

    const auto root = m_rootPath;
    auto future = QtConcurrent::run(
        [options = std::move(options), root] {
            return disktool::core::Scanner(options).scan(root);
        });
    m_watcher.setFuture(future);
}

void CleanupTab::cancelScan()
{
    if (!m_watcher.isRunning()) {
        return;
    }

    m_cancelFlag.store(true);
    m_scanButton->setText(tr("Отмена..."));
    m_scanButton->setEnabled(false);
}

void CleanupTab::onScanFinished()
{
    disktool::core::ScanResult result;
    try {
        result = m_watcher.result();
    } catch (const std::exception& exception) {
        setScanning(false);
        m_lastErrors = {std::string("scan failed: ") + exception.what()};
        m_summary->setText(tr("Ошибка сканирования."));
        emit scanFinished(0, 0, 0, 0, 1, false);
        return;
    } catch (...) {
        setScanning(false);
        m_lastErrors = {"scan failed: unknown error"};
        m_summary->setText(tr("Ошибка сканирования."));
        emit scanFinished(0, 0, 0, 0, 1, false);
        return;
    }

    m_lastErrors = result.errors;

    const bool cancelled = std::any_of(
        m_lastErrors.begin(), m_lastErrors.end(), [](const std::string& message) {
            return message.find("cancelled by user") != std::string::npos;
        });

    if (cancelled) {
        // A partially scanned directory may look empty even though it has
        // unscanned content, so deletion must stay disabled.
        m_model->removeRows(0, m_model->rowCount());
        m_items.clear();
        m_summary->setText(
            tr("Сканирование отменено — результаты неполные."));
        setScanning(false);
        updateDeleteButton();
    } else {
        disktool::core::CleanupOptions options;
        options.includeEmptyFiles = m_findFiles->isChecked();
        options.includeEmptyDirectories = m_findDirectories->isChecked();

        try {
            const auto cleanup =
                disktool::core::findEmptyEntries(result, m_rootPath, options);
            populate(cleanup);
        } catch (const std::exception& exception) {
            m_lastErrors.push_back(
                std::string("cleanup analysis failed: ") + exception.what());
            m_summary->setText(tr("Не удалось построить список."));
        }

        setScanning(false);
    }

    emit scanFinished(
        static_cast<qulonglong>(result.totalFiles),
        static_cast<qulonglong>(result.totalDirectories),
        static_cast<qulonglong>(result.totalBytes),
        static_cast<qlonglong>(result.duration.count()),
        static_cast<int>(m_lastErrors.size()),
        cancelled);
}

void CleanupTab::setScanning(bool scanning)
{
    m_pathEdit->setEnabled(!scanning);
    m_browseButton->setEnabled(!scanning);
    m_findFiles->setEnabled(!scanning);
    m_findDirectories->setEnabled(!scanning);
    m_scanButton->setEnabled(true);
    m_scanButton->setText(scanning ? tr("Отменить") : tr("Сканировать"));

    if (scanning) {
        m_deleteButton->setEnabled(false);
    } else {
        updateDeleteButton();
    }
}

void CleanupTab::populate(const disktool::core::CleanupResult& result)
{
    m_model->removeRows(0, m_model->rowCount());
    m_items.clear();
    m_items.reserve(result.emptyFiles.size() + result.emptyDirectories.size());

    for (const auto& item : result.emptyFiles) {
        appendItem(item);
    }
    for (const auto& item : result.emptyDirectories) {
        appendItem(item);
    }

    updateSummary();
    updateDeleteButton();
}

void CleanupTab::appendItem(const disktool::core::CleanupItem& item)
{
    auto* type = new QStandardItem(item.isDirectory ? tr("Папка") : tr("Файл"));
    type->setCheckable(true);
    type->setCheckState(Qt::Checked);
    type->setEditable(false);

    auto* path = new QStandardItem(pathToQString(item.path));
    path->setEditable(false);
    path->setToolTip(pathToQString(item.path));

    auto* size = new QStandardItem(
        item.isDirectory ? QStringLiteral("—")
                         : disktool::app::formatBytes(item.size));
    size->setEditable(false);
    size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_model->appendRow({type, path, size});
    m_items.push_back(item);
}

void CleanupTab::updateSummary()
{
    qsizetype files = 0;
    qsizetype directories = 0;
    for (const auto& item : m_items) {
        if (item.isDirectory) {
            ++directories;
        } else {
            ++files;
        }
    }

    m_summary->setText(
        tr("Найдено: %1 пустых файлов, %2 пустых папок")
            .arg(files)
            .arg(directories));
}

void CleanupTab::updateDeleteButton()
{
    m_deleteButton->setEnabled(!m_watcher.isRunning()
                               && !m_trashWatcher.isRunning()
                               && !checkedRows().empty());
}

std::vector<int> CleanupTab::checkedRows() const
{
    std::vector<int> rows;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const auto* item = m_model->item(row, TypeColumn);
        if (item != nullptr && item->checkState() == Qt::Checked) {
            rows.push_back(row);
        }
    }
    return rows;
}

void CleanupTab::deleteSelected()
{
    const auto rows = checkedRows();
    if (rows.empty()) {
        return;
    }

    const bool confirmed = [this, count = rows.size()] {
        QMessageBox confirmation(
            QMessageBox::Question, tr("Очистка"),
            tr("Вы уверены, что хотите удалить %1 элементов в Корзину?")
                .arg(count),
            QMessageBox::Yes | QMessageBox::No, this);
        confirmation.setDefaultButton(QMessageBox::No);
        if (auto* yes = confirmation.button(QMessageBox::Yes)) {
            yes->setText(tr("Удалить"));
        }
        if (auto* no = confirmation.button(QMessageBox::No)) {
            no->setText(tr("Отмена"));
        }
        return confirmation.exec() == QMessageBox::Yes;
    }();

    if (!confirmed) {
        return;
    }

    std::vector<fs::path> paths;
    paths.reserve(rows.size());
    for (const int row : rows) {
        paths.push_back(m_items[static_cast<std::size_t>(row)].path);
    }

    setDeleting(true);
    m_progressBar->setRange(0, static_cast<int>(paths.size()));
    m_progressBar->setValue(0);
    m_trashWorker->resetAbort();

    auto future = QtConcurrent::run(
        [worker = m_trashWorker, paths = std::move(paths)] {
            return worker->run(paths);
        });
    m_trashWatcher.setFuture(future);
}

void CleanupTab::cancelTrash()
{
    if (!m_trashWatcher.isRunning()) {
        return;
    }

    m_trashWorker->requestAbort();
    m_cancelTrashButton->setEnabled(false);
    m_summary->setText(tr("Отмена удаления..."));
}

void CleanupTab::onTrashFinished()
{
    TrashResult result;
    try {
        result = m_trashWatcher.result();
    } catch (const std::exception& exception) {
        result.failures.append(QString::fromUtf8(exception.what()));
    } catch (...) {
        result.failures.append(tr("неизвестная ошибка удаления"));
    }

    setDeleting(false);

    QSet<QString> succeeded;
    succeeded.reserve(static_cast<qsizetype>(result.succeeded.size()));
    for (const QString& path : result.succeeded) {
        succeeded.insert(path.toLower());
    }

    for (int row = static_cast<int>(m_items.size()) - 1; row >= 0; --row) {
        const QString path =
            pathToQString(m_items[static_cast<std::size_t>(row)].path);
        if (succeeded.contains(path.toLower())) {
            m_model->removeRow(row);
            m_items.erase(m_items.begin() + row);
        }
    }

    updateSummary();
    updateDeleteButton();

    QString message = tr("Отправлено в Корзину: %1.").arg(result.moved);
    if (result.cancelled) {
        message += QLatin1Char('\n');
        message += tr("Удаление отменено. Необработанные элементы остались "
                      "в списке.");
    }
    if (!result.failures.isEmpty()) {
        message += QLatin1Char('\n');
        message += tr("Не удалось переместить: %1.").arg(result.failures.size());

        constexpr int kShownFailures = 50;
        const int shown = std::min(
            static_cast<int>(result.failures.size()), kShownFailures);
        for (int index = 0; index < shown; ++index) {
            message += QLatin1Char('\n');
            message += result.failures.at(index);
        }
        if (result.failures.size() > shown) {
            message += QLatin1Char('\n');
            message += tr("… и ещё %1").arg(result.failures.size() - shown);
        }
    }
    QMessageBox::information(this, tr("Очистка"), message);
}

void CleanupTab::setDeleting(bool deleting)
{
    m_pathEdit->setEnabled(!deleting);
    m_browseButton->setEnabled(!deleting);
    m_scanButton->setEnabled(!deleting);
    m_findFiles->setEnabled(!deleting);
    m_findDirectories->setEnabled(!deleting);
    m_progressBar->setVisible(deleting);
    m_cancelTrashButton->setVisible(deleting);
    m_cancelTrashButton->setEnabled(deleting);

    if (deleting) {
        m_deleteButton->setEnabled(false);
    } else {
        updateDeleteButton();
    }
}

void CleanupTab::showErrorsDialog()
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
