#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <filesystem>
#include <vector>

struct TrashResult {
    int moved = 0;
    int failed = 0;
    bool cancelled = false;
    QStringList succeeded;
    QStringList failures;
};

class TrashProgressSink;

/// Moves selected files/directories to the Windows Recycle Bin with
/// IFileOperation. The object lives in the GUI thread, but run() is called from
/// a QtConcurrent worker; only run()/the progress sink touch the COM state.
/// Signals are delivered to the GUI thread through Qt's queued connections.
class TrashWorker final : public QObject
{
    Q_OBJECT

public:
    explicit TrashWorker(QObject* parent = nullptr);

    void resetAbort();
    void requestAbort();
    [[nodiscard]] bool abortRequested() const;

    [[nodiscard]] TrashResult run(
        const std::vector<std::filesystem::path>& paths);

signals:
    void progress(int processed, int total);

private:
    friend class TrashProgressSink;

    void reportResult(const QString& path, bool success);

    std::atomic_bool m_abortRequested{false};
    int m_total = 0;
    int m_processed = 0;
    int m_movedCount = 0;
    int m_failedCount = 0;
    QStringList m_succeeded;
    QStringList m_failures;
};
