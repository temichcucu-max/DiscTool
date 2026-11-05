#include "disktool/core/scanner.h"

#include <chrono>
#include <exception>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace disktool::core {
namespace {

std::string toUtf8(const fs::path& path)
{
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string errorMessage(const fs::path& path, const std::error_code& error)
{
    return toUtf8(path) + ": " + error.message();
}

bool hasHiddenName(const fs::path& path)
{
    const auto name = path.filename().native();
    return !name.empty() && name.front() == static_cast<fs::path::value_type>('.');
}

}  // namespace

Scanner::Scanner(ScanOptions options)
    : m_options(options)
{
}

ScanResult Scanner::scan(const fs::path& root) const
{
    const auto started = std::chrono::steady_clock::now();

    ScanResult result;
    const auto finish = [&result, started] {
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
    };

    auto directoryOptions = fs::directory_options::skip_permission_denied;
    if (m_options.followSymlinks) {
        directoryOptions |= fs::directory_options::follow_directory_symlink;
    }

    std::error_code error;
    fs::recursive_directory_iterator it(root, directoryOptions, error);
    if (error) {
        result.errors.push_back(errorMessage(root, error));
        finish();
        return result;
    }

    if (m_options.maxDepth == 0) {
        finish();
        return result;
    }

    const fs::recursive_directory_iterator end;

    // Advances the iterator, reporting a failure. Returns false when the scan
    // cannot continue safely.
    const auto advance = [&](const fs::path& current) {
        error.clear();
        it.increment(error);
        if (error) {
            result.errors.push_back(errorMessage(current, error));
            error.clear();
            return false;
        }
        return true;
    };

    try {
        while (it != end) {
            const auto current = it->path();
            const auto depth = static_cast<std::uintmax_t>(it.depth()) + 1;

            if (!m_options.includeHidden && hasHiddenName(current)) {
                it.disable_recursion_pending();
                if (!advance(current)) {
                    break;
                }
                continue;
            }

            std::error_code typeError;
            const bool isDirectory = it->is_directory(typeError);
            if (typeError) {
                // Broken symlink, vanished file, unreadable metadata, etc.
                result.errors.push_back(errorMessage(current, typeError));
                it.disable_recursion_pending();
                if (!advance(current)) {
                    break;
                }
                continue;
            }

            FileEntry entry;
            entry.path = current;
            entry.isDirectory = isDirectory;

            std::error_code timeError;
            entry.modified = fs::last_write_time(current, timeError);
            if (timeError) {
                result.errors.push_back(errorMessage(current, timeError));
            }

            if (isDirectory) {
                ++result.totalDirectories;
            } else {
                std::error_code sizeError;
                entry.size = fs::file_size(current, sizeError);
                if (sizeError) {
                    result.errors.push_back(errorMessage(current, sizeError));
                    entry.size = 0;
                }
                result.totalBytes += entry.size;
                ++result.totalFiles;
            }

            result.entries.push_back(std::move(entry));

            if (depth >= m_options.maxDepth) {
                it.disable_recursion_pending();
            }

            if (!advance(current)) {
                break;
            }
        }
    } catch (const std::exception& exception) {
        // Defensive catch-all: std::filesystem is normally queried through
        // error_code overloads above, but allocation failures and unexpected
        // iterator errors must not escape scan().
        result.errors.push_back(toUtf8(root) + ": " + exception.what());
    } catch (...) {
        result.errors.push_back(toUtf8(root) + ": unknown error");
    }

    finish();
    return result;
}

}  // namespace disktool::core
