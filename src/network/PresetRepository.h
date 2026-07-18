#pragma once

#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

enum class PresetErrorKind
{
    InvalidId,
    NotFound,
    ReadOnly,
    AlreadyExists,
    Conflict,
    TooLarge
};

class PresetError : public std::runtime_error
{
public:
    PresetError(PresetErrorKind kind, const std::string& message);
    PresetErrorKind Kind() const;

private:
    PresetErrorKind _kind;
};

struct PresetDocument
{
    std::string id;
    std::string name;
    std::string source;
    std::string etag;
    bool writable{false};
};

struct PresetList
{
    std::vector<PresetDocument> items;
    std::size_t total{0};
};

class PresetRepository
{
public:
    PresetRepository(std::string workspaceRoot,
                     std::vector<std::string> bundledRoots,
                     std::size_t maxPresetBytes);

    PresetList List(std::size_t offset, std::size_t limit,
                    const std::string& sourceFilter,
                    const std::string& query) const;
    PresetDocument Get(const std::string& id, bool includeSource = true) const;
    PresetDocument Create(const std::string& id, const std::string& source);
    PresetDocument Update(const std::string& id, const std::string& source,
                          const std::string& expectedEtag);
    std::string ResolvePath(const std::string& id) const;

    std::size_t MaxPresetBytes() const;
    const std::string& WorkspaceRoot() const;

private:
    struct ResolvedPreset
    {
        std::string id;
        std::string path;
        bool writable;
    };

    ResolvedPreset Resolve(const std::string& id, bool mustExist) const;
    static std::string NormalizeId(const std::string& id);
    static std::string Hash(const std::string& source);
    static std::string ReadFile(const std::string& path, std::size_t maxBytes);
    static void Scan(const std::string& root, const std::string& prefix,
                     bool writable, std::vector<ResolvedPreset>& results);
    void WriteFile(const ResolvedPreset& preset, const std::string& source) const;
    void ValidateWritablePath(const std::string& path) const;

    std::string _workspaceRoot;
    std::vector<std::string> _bundledRoots;
    std::size_t _maxPresetBytes;
    mutable std::recursive_mutex _mutex;
};
