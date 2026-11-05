#include "disktool/core/tree_builder.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace disktool::core {
namespace {

std::string toUtf8(const fs::path& path)
{
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string nodeName(const fs::path& path)
{
    auto name = toUtf8(path.filename());
    if (name.empty()) {
        // Drive roots such as "C:\" have an empty filename.
        name = toUtf8(path);
    }
    return name;
}

// Maps a directory node to a name -> child index lookup so that building a
// directory with many files stays linear instead of O(n^2).
using ChildIndex =
    std::unordered_map<const DirNode*, std::unordered_map<std::string, std::size_t>>;

DirNode* findOrCreateChild(DirNode& parent, const fs::path& component,
                           const fs::path& fullPath, ChildIndex& index)
{
    auto& byName = index[&parent];
    const auto name = toUtf8(component);

    const auto found = byName.find(name);
    if (found != byName.end()) {
        return parent.children[found->second].get();
    }

    auto child = std::make_unique<DirNode>();
    child->path = fullPath;
    child->name = name;
    parent.children.push_back(std::move(child));
    byName.emplace(name, parent.children.size() - 1);
    return parent.children.back().get();
}

void aggregate(DirNode& node)
{
    node.cumulativeSize = node.ownSize;
    node.cumulativeFileCount = node.ownFileCount;
    for (auto& child : node.children) {
        aggregate(*child);
        node.cumulativeSize += child->cumulativeSize;
        node.cumulativeFileCount += child->cumulativeFileCount;
    }
}

void sortRecursive(DirNode& node)
{
    for (auto& child : node.children) {
        sortRecursive(*child);
    }

    std::stable_sort(node.children.begin(), node.children.end(),
                     [](const std::unique_ptr<DirNode>& left,
                        const std::unique_ptr<DirNode>& right) {
                         if (left->cumulativeSize != right->cumulativeSize) {
                             return left->cumulativeSize > right->cumulativeSize;
                         }
                         return left->name < right->name;
                     });
}

}  // namespace

std::unique_ptr<DirNode> buildTree(const ScanResult& scan, const fs::path& root)
{
    auto rootNode = std::make_unique<DirNode>();
    rootNode->path = root;
    rootNode->name = nodeName(root);
    rootNode->isDirectory = true;

    ChildIndex index;

    for (const auto& entry : scan.entries) {
        const auto relative = entry.path.lexically_relative(root);
        if (relative.empty() || relative.is_absolute()) {
            continue;  // outside the scanned root (e.g. another drive)
        }

        // Normalise away "." components and reject anything that would escape
        // the root. lexically_relative normally removes "..", but a caller may
        // hand-build a ScanResult, so the guard stays.
        const fs::path dot = ".";
        const fs::path dotdot = "..";

        std::vector<fs::path> components;
        bool escapesRoot = false;
        for (const auto& component : relative) {
            const auto& native = component.native();
            if (native == dot.native()) {
                continue;
            }
            if (native == dotdot.native()) {
                escapesRoot = true;
                break;
            }
            components.push_back(component);
        }

        if (escapesRoot || components.empty()) {
            continue;
        }

        DirNode* current = rootNode.get();
        fs::path currentPath = root;

        for (std::size_t position = 0; position < components.size(); ++position) {
            currentPath /= components[position];
            const bool isLast = position + 1 == components.size();

            if (isLast && !entry.isDirectory) {
                // Files are not materialised as nodes in v1: their bytes and
                // count belong to the containing directory's own totals.
                current->ownSize += entry.size;
                ++current->ownFileCount;
                break;
            }

            // Every created node is a directory: intermediate components are
            // always directories, and the last one is a directory here.
            current = findOrCreateChild(*current, components[position],
                                        currentPath, index);
        }
    }

    aggregate(*rootNode);
    return rootNode;
}

void sortBySizeDescending(DirNode& node)
{
    sortRecursive(node);
}

}  // namespace disktool::core
