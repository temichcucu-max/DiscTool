#include "tabs/AnalysisTab.h"

#include "models/DirTreeModel.h"
#include "widgets/TreemapWidget.h"

#include <QtConcurrent/QtConcurrentRun>

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTreeView>
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

AnalysisTab::AnalysisTab(QWidget* parent)
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

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    m_model = new DirTreeModel(this);
    m_tree = new QTreeView(splitter);
    m_tree->setModel(m_model);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSortingEnabled(false);
    m_tree->setExpandsOnDoubleClick(true);

    auto* header = m_tree->header();
    header->setSectionResizeMode(DirTreeModel::NameColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(DirTreeModel::SizeColumn,
                                 QHeaderView::ResizeToContents);
    header->setSectionResizeMode(DirTreeModel::FilesColumn,
                                 QHeaderView::ResizeToContents);
    header->setSectionResizeMode(DirTreeModel::PercentColumn,
                                 QHeaderView::ResizeToContents);

    auto* right = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    auto* breadcrumbRow = new QHBoxLayout;
    m_upButton = new QPushButton(tr("Вверх"), right);
    m_upButton->setEnabled(false);
    m_upButton->setToolTip(tr("Перейти к родительской папке"));

    m_breadcrumb = new QLineEdit(right);
    m_breadcrumb->setReadOnly(true);
    m_breadcrumb->setFrame(false);
    m_breadcrumb->setPlaceholderText(tr("Папка не выбрана"));

    breadcrumbRow->addWidget(m_upButton);
    breadcrumbRow->addWidget(m_breadcrumb, 1);
    rightLayout->addLayout(breadcrumbRow);

    m_treemap = new TreemapWidget(right);
    rightLayout->addWidget(m_treemap, 1);

    splitter->addWidget(m_tree);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({420, 540});

    layout->addWidget(splitter, 1);

    connect(m_browseButton, &QPushButton::clicked,
            this, &AnalysisTab::browseForDirectory);
    connect(m_scanButton, &QPushButton::clicked, this, [this] {
        if (m_watcher.isRunning()) {
            cancelScan();
        } else {
            startScan();
        }
    });
    connect(&m_watcher, &QFutureWatcher<disktool::core::ScanResult>::finished,
            this, &AnalysisTab::onScanFinished);
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &AnalysisTab::onCurrentIndexChanged);
    connect(m_treemap, &TreemapWidget::itemActivated,
            this, &AnalysisTab::activateTreemapItem);
    connect(m_upButton, &QPushButton::clicked, this, &AnalysisTab::goUp);
}

AnalysisTab::~AnalysisTab()
{
    m_cancelFlag.store(true);
    if (m_watcher.isRunning()) {
        m_watcher.waitForFinished();
    }
}

void AnalysisTab::browseForDirectory()
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

void AnalysisTab::startScan()
{
    const QString text = m_pathEdit->text().trimmed();
    if (text.isEmpty()) {
        QMessageBox::warning(this, tr("Анализ диска"),
                             tr("Укажите папку для сканирования."));
        return;
    }

    const QFileInfo info(text);
    if (!info.exists() || !info.isDir()) {
        QMessageBox::warning(this, tr("Анализ диска"),
                             tr("Папка не найдена:\n%1").arg(text));
        return;
    }

    m_cancelFlag.store(false);
    m_lastErrors.clear();
    m_rootPath = toPath(text);
    m_currentNode = nullptr;

    m_model->setRoot(nullptr);
    m_treemap->setCurrent(nullptr);
    updateBreadcrumb(nullptr);
    m_upButton->setEnabled(false);

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

void AnalysisTab::cancelScan()
{
    if (!m_watcher.isRunning()) {
        return;
    }

    m_cancelFlag.store(true);
    m_scanButton->setText(tr("Отмена..."));
    m_scanButton->setEnabled(false);
}

void AnalysisTab::onScanFinished()
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

    auto tree = disktool::core::buildTree(result, m_rootPath);
    disktool::core::sortBySizeDescending(*tree);

    // Drop pointers into the old tree before the model destroys it.
    m_currentNode = nullptr;
    m_treemap->setCurrent(nullptr);
    m_model->setRoot(std::move(tree));

    m_tree->expandToDepth(2);

    const QModelIndex rootIndex = m_model->index(0, 0, QModelIndex());
    if (rootIndex.isValid()) {
        m_tree->setCurrentIndex(rootIndex);
        m_tree->scrollTo(rootIndex);
        showNode(rootIndex);
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

void AnalysisTab::setScanning(bool scanning)
{
    m_pathEdit->setEnabled(!scanning);
    m_browseButton->setEnabled(!scanning);
    m_scanButton->setEnabled(true);
    m_scanButton->setText(scanning ? tr("Отменить") : tr("Сканировать"));
}

void AnalysisTab::onCurrentIndexChanged(const QModelIndex& current,
                                        const QModelIndex& previous)
{
    Q_UNUSED(previous);

    if (!current.isValid()) {
        m_currentNode = nullptr;
        m_treemap->setCurrent(nullptr);
        updateBreadcrumb(nullptr);
        m_upButton->setEnabled(false);
        return;
    }

    showNode(current);
}

void AnalysisTab::showNode(const QModelIndex& index)
{
    const auto* node = m_model->nodeFromIndex(index);
    m_currentNode = node;
    m_treemap->setCurrent(node);
    updateBreadcrumb(node);
    m_upButton->setEnabled(node != nullptr && m_model->parent(index).isValid());
}

void AnalysisTab::updateBreadcrumb(const disktool::core::DirNode* node)
{
    if (node == nullptr) {
        m_breadcrumb->clear();
        m_breadcrumb->setToolTip({});
        return;
    }

    const QString path = pathToQString(node->path);
    m_breadcrumb->setText(path);
    m_breadcrumb->setToolTip(path);
    m_breadcrumb->setCursorPosition(0);
}

void AnalysisTab::goUp()
{
    if (m_currentNode == nullptr) {
        return;
    }

    const QModelIndex current = m_model->indexForNode(m_currentNode);
    const QModelIndex parent = m_model->parent(current);
    if (!parent.isValid()) {
        return;
    }

    m_tree->setCurrentIndex(parent);
    m_tree->scrollTo(parent);
}

void AnalysisTab::activateTreemapItem(std::size_t index)
{
    if (m_currentNode == nullptr || index >= m_currentNode->children.size()) {
        return;
    }

    const auto* child = m_currentNode->children[index].get();
    const QModelIndex childIndex = m_model->indexForNode(child);
    if (!childIndex.isValid()) {
        return;
    }

    m_tree->expand(childIndex);
    m_tree->setCurrentIndex(childIndex);
    m_tree->scrollTo(childIndex, QAbstractItemView::PositionAtCenter);
}

void AnalysisTab::showErrorsDialog()
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
