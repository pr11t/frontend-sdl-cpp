#include "network/ShaderChainStore.h"

#include <utility>

void ShaderChainStore::SetShader(const std::string& name, std::string source)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto& info = _shaders[name];
    info.source = std::move(source);
    info.compiled = false;
    info.error.clear();
    ++_generation;
}

bool ShaderChainStore::RemoveShader(const std::string& name)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const bool existed = _shaders.erase(name) > 0;
    if (existed)
    {
        ++_generation;
    }
    return existed;
}

bool ShaderChainStore::HasShader(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _shaders.find(name) != _shaders.end();
}

void ShaderChainStore::SetChain(std::vector<ShaderPassConfig> chain)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _chain = std::move(chain);
    ++_generation;
}

std::vector<ShaderPassConfig> ShaderChainStore::Chain() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _chain;
}

std::uint64_t ShaderChainStore::Generation() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _generation;
}

ShaderChainStore::Snapshot ShaderChainStore::GetSnapshot() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    Snapshot snapshot;
    snapshot.generation = _generation;
    snapshot.chain = _chain;
    for (const auto& pass : _chain)
    {
        const auto it = _shaders.find(pass.shader);
        if (it != _shaders.end())
        {
            snapshot.sources[pass.shader] = it->second.source;
        }
    }
    return snapshot;
}

void ShaderChainStore::SetCompileStatus(const std::string& name, bool compiled, const std::string& error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _shaders.find(name);
    if (it != _shaders.end())
    {
        it->second.compiled = compiled;
        it->second.error = error;
    }
}

std::vector<ShaderChainStore::ShaderStatus> ShaderChainStore::ListShaders() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<ShaderStatus> result;
    result.reserve(_shaders.size());
    for (const auto& [name, info] : _shaders)
    {
        result.push_back({name, info.compiled, info.error});
    }
    return result;
}
