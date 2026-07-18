#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

/**
 * @brief One pass in the post-processing chain: a named shader plus an optional
 * input texture and float parameters.
 */
struct ShaderPassConfig
{
    std::string shader;                 //!< Name of an uploaded shader.
    std::string texture;                //!< Optional named texture bound as uTexture.
    std::map<std::string, float> params; //!< Custom float uniforms.
};

/**
 * @brief Thread-safe store of uploaded post-processing shaders and the ordered
 * chain that applies them.
 *
 * Uploads and chain changes happen on the network thread; the render thread
 * reads a snapshot (only when the generation counter changes), compiles the
 * shaders in its GL context and writes compile status back here.
 */
class ShaderChainStore
{
public:
    struct ShaderStatus
    {
        std::string name;
        bool compiled{false};
        std::string error;
    };

    struct Snapshot
    {
        std::uint64_t generation{0};
        std::uint64_t paramsGeneration{0};
        std::vector<ShaderPassConfig> chain;
        std::map<std::string, std::string> sources; //!< Sources for shaders referenced by the chain.
    };

    /// Adds or replaces a shader's source. Bumps the generation.
    void SetShader(const std::string& name, std::string source);

    /// Removes a shader. Returns true if it existed. Bumps the generation.
    bool RemoveShader(const std::string& name);

    /// True if a shader with this name exists.
    bool HasShader(const std::string& name) const;

    /// Replaces the ordered chain. Bumps the generation.
    void SetChain(std::vector<ShaderPassConfig> chain);

    /**
     * @brief Merges @p params into the params of the chain pass at @p index,
     * without changing the chain structure. Bumps only the params generation so
     * the render thread updates uniform values without recompiling.
     * @return false if @p index is out of range.
     */
    bool UpdateParams(std::size_t index, const std::map<std::string, float>& params);

    /// Current chain configuration.
    std::vector<ShaderPassConfig> Chain() const;

    /// Structure generation (chain/shaders); a change requires recompilation.
    std::uint64_t Generation() const;

    /// Params generation; a change requires only refreshing uniform values.
    std::uint64_t ParamsGeneration() const;

    /// Snapshot for the render thread: generation, chain, and referenced sources.
    Snapshot GetSnapshot() const;

    /// Called by the render thread after (re)compiling a shader.
    void SetCompileStatus(const std::string& name, bool compiled, const std::string& error);

    /// Listing of all uploaded shaders with their last compile status.
    std::vector<ShaderStatus> ListShaders() const;

private:
    struct ShaderInfo
    {
        std::string source;
        bool compiled{false};
        std::string error;
    };

    mutable std::mutex _mutex;
    std::map<std::string, ShaderInfo> _shaders;
    std::vector<ShaderPassConfig> _chain;
    std::uint64_t _generation{0};
    std::uint64_t _paramsGeneration{0};
};
