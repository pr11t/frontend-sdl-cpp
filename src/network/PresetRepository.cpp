#include "network/PresetRepository.h"

#include <Poco/DigestEngine.h>
#include <Poco/DirectoryIterator.h>
#include <Poco/File.h>
#include <Poco/FileStream.h>
#include <Poco/Path.h>
#include <Poco/SHA2Engine.h>
#include <Poco/UUIDGenerator.h>

#include <algorithm>
#include <cctype>
#include <sstream>

PresetError::PresetError(PresetErrorKind kind, const std::string& message)
    : std::runtime_error(message)
    , _kind(kind)
{
}

PresetErrorKind PresetError::Kind() const
{
    return _kind;
}

PresetRepository::PresetRepository(std::string workspaceRoot,
                                   std::vector<std::string> bundledRoots,
                                   std::size_t maxPresetBytes)
    : _workspaceRoot(Poco::Path(workspaceRoot).absolute().makeDirectory().toString())
    , _bundledRoots(std::move(bundledRoots))
    , _maxPresetBytes(maxPresetBytes)
{
    if (_maxPresetBytes == 0)
    {
        throw std::invalid_argument("Maximum preset size must be greater than zero.");
    }
    Poco::File(_workspaceRoot).createDirectories();
    for (auto& root : _bundledRoots)
    {
        root = Poco::Path(root).absolute().makeDirectory().toString();
    }
}

PresetList PresetRepository::List(std::size_t offset, std::size_t limit,
                                  const std::string& sourceFilter,
                                  const std::string& query) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    std::vector<ResolvedPreset> found;
    if (sourceFilter.empty() || sourceFilter == "workspace")
    {
        Scan(_workspaceRoot, "workspace/", true, found);
    }
    if (sourceFilter.empty() || sourceFilter == "bundled")
    {
        for (const auto& root : _bundledRoots)
        {
            if (Poco::File(root).exists())
            {
                Scan(root, "bundled/", false, found);
            }
        }
    }
    if (!sourceFilter.empty() && sourceFilter != "workspace" && sourceFilter != "bundled")
    {
        throw PresetError(PresetErrorKind::InvalidId, "source must be workspace or bundled.");
    }

    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    found.erase(std::remove_if(found.begin(), found.end(), [&](const ResolvedPreset& item) {
                    auto lowerId = item.id;
                    std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(),
                                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
                    return !lowerQuery.empty() && lowerId.find(lowerQuery) == std::string::npos;
                }),
                found.end());
    std::sort(found.begin(), found.end(),
              [](const ResolvedPreset& left, const ResolvedPreset& right) { return left.id < right.id; });
    found.erase(std::unique(found.begin(), found.end(),
                            [](const ResolvedPreset& left, const ResolvedPreset& right) {
                                return left.id == right.id;
                            }),
                found.end());

    PresetList result;
    result.total = found.size();
    const auto end = std::min(found.size(), offset + limit);
    for (auto index = std::min(offset, found.size()); index < end; ++index)
    {
        const auto& item = found[index];
        result.items.push_back({item.id, Poco::Path(item.path).getBaseName(), "", "", item.writable});
    }
    return result;
}

PresetDocument PresetRepository::Get(const std::string& id, bool includeSource) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    const auto preset = Resolve(id, true);
    const auto source = ReadFile(preset.path, _maxPresetBytes);
    return {preset.id, Poco::Path(preset.path).getBaseName(),
            includeSource ? source : "", Hash(source), preset.writable};
}

PresetDocument PresetRepository::Create(const std::string& id, const std::string& source)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto preset = Resolve(id, false);
    if (!preset.writable)
    {
        throw PresetError(PresetErrorKind::ReadOnly, "Only workspace presets can be created.");
    }
    if (Poco::File(preset.path).exists())
    {
        throw PresetError(PresetErrorKind::AlreadyExists, "Preset already exists.");
    }
    WriteFile(preset, source);
    return Get(preset.id);
}

PresetDocument PresetRepository::Update(const std::string& id, const std::string& source,
                                        const std::string& expectedEtag)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto preset = Resolve(id, true);
    if (!preset.writable)
    {
        throw PresetError(PresetErrorKind::ReadOnly, "Bundled presets are read-only.");
    }
    const auto current = Get(id);
    if (expectedEtag.empty() || expectedEtag != current.etag)
    {
        throw PresetError(PresetErrorKind::Conflict, "Preset has changed; retrieve it again before updating.");
    }
    WriteFile(preset, source);
    return Get(preset.id);
}

std::string PresetRepository::ResolvePath(const std::string& id) const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return Resolve(id, true).path;
}

std::string PresetRepository::ResolveId(const std::string& path) const
{
    if (path.empty())
    {
        return "";
    }
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    const auto absolute = Poco::Path(path).absolute().toString();

    const auto match = [&absolute](const std::string& root,
                                   const std::string& prefix) -> std::string {
        if (absolute.size() <= root.size() || absolute.compare(0, root.size(), root) != 0)
        {
            return "";
        }
        auto relative = absolute.substr(root.size());
        std::replace(relative.begin(), relative.end(), '\\', '/');
        return prefix + relative;
    };

    auto id = match(_workspaceRoot, "workspace/");
    if (!id.empty())
    {
        return id;
    }
    for (const auto& root : _bundledRoots)
    {
        id = match(root, "bundled/");
        if (!id.empty())
        {
            return id;
        }
    }
    return "";
}

std::size_t PresetRepository::MaxPresetBytes() const
{
    return _maxPresetBytes;
}

const std::string& PresetRepository::WorkspaceRoot() const
{
    return _workspaceRoot;
}

PresetRepository::ResolvedPreset PresetRepository::Resolve(const std::string& rawId, bool mustExist) const
{
    const auto id = NormalizeId(rawId);
    std::string relative;
    std::vector<std::string> roots;
    bool writable = false;
    if (id.compare(0, 10, "workspace/") == 0)
    {
        relative = id.substr(10);
        roots.push_back(_workspaceRoot);
        writable = true;
    }
    else if (id.compare(0, 8, "bundled/") == 0)
    {
        relative = id.substr(8);
        roots = _bundledRoots;
    }
    else
    {
        throw PresetError(PresetErrorKind::InvalidId, "Preset ID must start with workspace/ or bundled/.");
    }

    for (const auto& root : roots)
    {
        Poco::Path path(root);
        path.append(relative);
        const auto candidate = path.toString();
        if (!mustExist || Poco::File(candidate).exists())
        {
            if (mustExist && (!Poco::File(candidate).isFile() || Poco::File(candidate).isLink()))
            {
                break;
            }
            return {id, candidate, writable};
        }
    }
    throw PresetError(PresetErrorKind::NotFound, "Preset was not found.");
}

std::string PresetRepository::NormalizeId(const std::string& input)
{
    std::string id = input;
    std::replace(id.begin(), id.end(), '\\', '/');
    if (id.empty() || id.front() == '/' || id.find('\0') != std::string::npos)
    {
        throw PresetError(PresetErrorKind::InvalidId, "Invalid preset ID.");
    }
    std::stringstream parts(id);
    std::string part;
    while (std::getline(parts, part, '/'))
    {
        if (part.empty() || part == "." || part == ".." || part.find(':') != std::string::npos)
        {
            throw PresetError(PresetErrorKind::InvalidId, "Preset ID contains an invalid path segment.");
        }
    }
    if (Poco::Path(id).getExtension() != "milk")
    {
        throw PresetError(PresetErrorKind::InvalidId, "Preset ID must end in .milk.");
    }
    return id;
}

std::string PresetRepository::Hash(const std::string& source)
{
    Poco::SHA2Engine engine;
    engine.update(source);
    return Poco::DigestEngine::digestToHex(engine.digest());
}

std::string PresetRepository::ReadFile(const std::string& path, std::size_t maxBytes)
{
    Poco::File file(path);
    if (file.getSize() > maxBytes)
    {
        throw PresetError(PresetErrorKind::TooLarge, "Preset exceeds the configured size limit.");
    }
    Poco::FileInputStream input(path);
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

void PresetRepository::Scan(const std::string& root, const std::string& prefix,
                            bool writable, std::vector<ResolvedPreset>& results)
{
    for (Poco::DirectoryIterator iterator(root), end; iterator != end; ++iterator)
    {
        if (iterator->isLink())
        {
            continue;
        }
        if (iterator->isDirectory())
        {
            Scan(iterator.path().toString(), prefix + iterator.name() + "/", writable, results);
        }
        else if (iterator->isFile() && iterator.path().getExtension() == "milk")
        {
            results.push_back({prefix + iterator.name(),
                               iterator.path().toString(), writable});
        }
    }
}

void PresetRepository::WriteFile(const ResolvedPreset& preset, const std::string& source) const
{
    if (source.size() > _maxPresetBytes)
    {
        throw PresetError(PresetErrorKind::TooLarge, "Preset exceeds the configured size limit.");
    }
    ValidateWritablePath(preset.path);
    Poco::Path target(preset.path);
    Poco::File(target.parent()).createDirectories();
    const auto temporary = preset.path + ".tmp-" +
                           Poco::UUIDGenerator::defaultGenerator().createRandom().toString();
    {
        Poco::FileOutputStream output(temporary);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
        output.close();
        if (!output)
        {
            Poco::File(temporary).remove();
            throw std::runtime_error("Failed to write preset.");
        }
    }
    Poco::File(temporary).renameTo(preset.path);
}

void PresetRepository::ValidateWritablePath(const std::string& path) const
{
    Poco::Path current(_workspaceRoot);
    Poco::Path relative(path.substr(_workspaceRoot.size()));
    for (int index = 0; index < relative.depth(); ++index)
    {
        current.pushDirectory(relative.directory(index));
        Poco::File component(current);
        if (component.exists() && component.isLink())
        {
            throw PresetError(PresetErrorKind::InvalidId, "Symbolic links are not allowed in preset paths.");
        }
    }
}
