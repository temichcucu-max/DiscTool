#include "TrashWorker.h"

#include <QFile>
#include <QString>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <knownfolders.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace fs = std::filesystem;

namespace {

QString pathToQString(const fs::path& path)
{
    const auto text = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()),
                             static_cast<qsizetype>(text.size()));
}

#ifdef _WIN32

constexpr std::size_t kBatchSize = 500;

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    [[nodiscard]] T* get() const { return m_ptr; }
    [[nodiscard]] T* operator->() const { return m_ptr; }
    [[nodiscard]] explicit operator bool() const { return m_ptr != nullptr; }

    T** put()
    {
        reset();
        return &m_ptr;
    }

    void reset(T* pointer = nullptr)
    {
        if (m_ptr != nullptr) {
            m_ptr->Release();
        }
        m_ptr = pointer;
    }

private:
    T* m_ptr = nullptr;
};

QString displayName(IShellItem* item)
{
    if (item == nullptr) {
        return {};
    }

    LPWSTR name = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &name))) {
        return {};
    }

    const QString result = QString::fromWCharArray(name);
    CoTaskMemFree(name);
    return result;
}

#endif  // _WIN32

}  // namespace

#ifdef _WIN32

/// Receives per-item callbacks on the worker thread. Cancellation is checked
/// here. The Windows SDK's IFileOperation has no Abort() method; returning
/// E_ABORT from a Pre*/UpdateProgress callback is the documented way to stop
/// the operation.
class TrashProgressSink final : public IFileOperationProgressSink
{
public:
    explicit TrashProgressSink(TrashWorker* worker)
        : m_worker(worker)
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                             void** object) override
    {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (IsEqualIID(riid, IID_IUnknown)
            || IsEqualIID(riid, IID_IFileOperationProgressSink)) {
            *object = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&m_reference));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG reference = InterlockedDecrement(&m_reference);
        if (reference == 0) {
            delete this;
        }
        return static_cast<ULONG>(reference);
    }

    HRESULT STDMETHODCALLTYPE StartOperations() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem*, LPCWSTR) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem*, LPCWSTR,
                                             HRESULT, IShellItem*) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem*, IShellItem*,
                                          LPCWSTR) override
    {
        return m_worker->abortRequested() ? E_ABORT : S_OK;
    }

    HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem* item, IShellItem*,
                                           LPCWSTR, HRESULT result,
                                           IShellItem*) override
    {
        m_worker->reportResult(displayName(item), SUCCEEDED(result));
        return m_worker->abortRequested() ? E_ABORT : S_OK;
    }

    HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem*, IShellItem*,
                                          LPCWSTR) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem*, IShellItem*,
                                           LPCWSTR, HRESULT,
                                           IShellItem*) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD, IShellItem*) override
    {
        return m_worker->abortRequested() ? E_ABORT : S_OK;
    }

    HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem* item,
                                             HRESULT result,
                                             IShellItem*) override
    {
        m_worker->reportResult(displayName(item), SUCCEEDED(result));
        return m_worker->abortRequested() ? E_ABORT : S_OK;
    }

    HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem*, LPCWSTR) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR,
                                          DWORD, HRESULT,
                                          IShellItem*) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override
    {
        return m_worker->abortRequested() ? E_ABORT : S_OK;
    }

    HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }

private:
    ~TrashProgressSink() = default;

    LONG m_reference = 1;
    TrashWorker* m_worker = nullptr;
};

#endif  // _WIN32

TrashWorker::TrashWorker(QObject* parent)
    : QObject(parent)
{
}

void TrashWorker::resetAbort()
{
    m_abortRequested.store(false);
}

void TrashWorker::requestAbort()
{
    m_abortRequested.store(true);
}

bool TrashWorker::abortRequested() const
{
    return m_abortRequested.load();
}

void TrashWorker::reportResult(const QString& path, bool success)
{
    ++m_processed;
    if (success) {
        ++m_movedCount;
        if (!path.isEmpty()) {
            m_succeeded.append(path);
        }
    } else {
        ++m_failedCount;
        if (!path.isEmpty()) {
            m_failures.append(path);
        }
    }

    if (m_processed % 25 == 0 || m_processed == m_total) {
        emit progress(m_processed, m_total);
    }
}

TrashResult TrashWorker::run(const std::vector<fs::path>& paths)
{
    TrashResult result;

    m_total = static_cast<int>(paths.size());
    m_processed = 0;
    m_movedCount = 0;
    m_failedCount = 0;
    m_succeeded.clear();
    m_failures.clear();

    if (paths.empty()) {
        result.cancelled = m_abortRequested.load();
        return result;
    }

#ifdef _WIN32
    const auto failAll = [this, &paths] {
        for (const auto& path : paths) {
            reportResult(pathToQString(path), false);
        }
    };

    const HRESULT initialized = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninitialize = SUCCEEDED(initialized);

    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        failAll();
        emit progress(m_processed, m_total);
        result.moved = m_movedCount;
        result.failed = m_failedCount;
        result.failures = m_failures;
        result.cancelled = m_abortRequested.load();
        return result;
    }

    ComPtr<IFileOperation> operation;
    HRESULT operationResult = CoCreateInstance(
        CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_IFileOperation,
        reinterpret_cast<void**>(operation.put()));

    if (FAILED(operationResult)) {
        failAll();
    } else {
        operation->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION
                                     | FOF_SILENT | FOF_NOERRORUI);

        auto* sink = new TrashProgressSink(this);
        DWORD cookie = 0;
        if (FAILED(operation->Advise(sink, &cookie))) {
            sink->Release();
            failAll();
        } else {
            for (std::size_t start = 0; start < paths.size();
                 start += kBatchSize) {
                if (m_abortRequested.load()) {
                    break;
                }

                const std::size_t end =
                    std::min(start + kBatchSize, paths.size());

                std::vector<PIDLIST_ABSOLUTE> pidls;
                std::vector<PCIDLIST_ABSOLUTE> pidlArray;
                std::vector<fs::path> batchPaths;
                pidls.reserve(end - start);
                pidlArray.reserve(end - start);
                batchPaths.reserve(end - start);

                for (std::size_t index = start; index < end; ++index) {
                    PIDLIST_ABSOLUTE pidl = nullptr;
                    SFGAOF attributes = 0;
                    const std::wstring native = paths[index].wstring();
                    if (SUCCEEDED(SHParseDisplayName(
                            native.c_str(), nullptr, &pidl, 0, &attributes))) {
                        pidls.push_back(pidl);
                        pidlArray.push_back(pidl);
                        batchPaths.push_back(paths[index]);
                    } else {
                        reportResult(pathToQString(paths[index]), false);
                    }
                }

                if (!pidlArray.empty()) {
                    ComPtr<IShellItemArray> items;
                    operationResult = SHCreateShellItemArrayFromIDLists(
                        static_cast<UINT>(pidlArray.size()), pidlArray.data(),
                        items.put());

                    if (SUCCEEDED(operationResult)) {
                        operationResult = operation->DeleteItems(items.get());
                    }

                    if (FAILED(operationResult)) {
                        for (const auto& path : batchPaths) {
                            reportResult(pathToQString(path), false);
                        }
                    } else {
                        operation->PerformOperations();
                    }
                }

                for (PIDLIST_ABSOLUTE pidl : pidls) {
                    CoTaskMemFree(pidl);
                }

                if (m_abortRequested.load()) {
                    break;
                }
            }

            operation->Unadvise(cookie);
            sink->Release();
        }
    }

    emit progress(m_processed, m_total);

    result.moved = m_movedCount;
    result.failed = m_failedCount;
    result.succeeded = m_succeeded;
    result.failures = m_failures;
    result.cancelled = m_abortRequested.load();

    if (uninitialize) {
        CoUninitialize();
    }
    return result;

#else
    for (const auto& path : paths) {
        if (m_abortRequested.load()) {
            break;
        }
        reportResult(pathToQString(path), QFile::moveToTrash(pathToQString(path)));
    }

    emit progress(m_processed, m_total);

    result.moved = m_movedCount;
    result.failed = m_failedCount;
    result.succeeded = m_succeeded;
    result.failures = m_failures;
    result.cancelled = m_abortRequested.load();
    return result;
#endif
}
