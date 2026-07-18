#pragma once

#include "network/VisualState.h"

#include <Poco/Logger.h>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

class ProjectMWrapper;
class ShaderChainStore;
class TextureStore;

/**
 * @brief Per-frame inputs made available to post-processing shaders.
 */
struct PostProcessInputs
{
    float time{0.0f}; //!< Seconds since start, exposed as the uTime uniform.
};

/**
 * @brief Renders projectM into an offscreen buffer, then applies a built-in
 * transform (mirror/rotation/zoom) followed by a configurable chain of
 * user-uploaded post-processing shaders.
 */
class VisualPostProcessor
{
public:
    VisualPostProcessor() = default;
    ~VisualPostProcessor();

    bool Initialize(int width, int height);
    bool Resize(int width, int height);
    void Render(ProjectMWrapper& projectM, const VisualState& state,
                ShaderChainStore& shaders, TextureStore& textures,
                const PostProcessInputs& inputs);
    void Shutdown();

    bool Active() const;

private:
    struct UserParam
    {
        std::string name;
        int location{-1};
        float value{0.0F};
    };

    struct UserPass
    {
        std::string shaderName;
        std::string textureName;
        std::size_t chainIndex{0}; //!< Position in the chain, for live params refresh.
        std::uint32_t program{0};
        int inputLocation{-1};
        int textureLocation{-1};
        int resolutionLocation{-1};
        int timeLocation{-1};
        std::vector<UserParam> params;
    };

    static std::uint32_t CompileShader(std::uint32_t type, const char* source);
    static std::uint32_t LinkProgram(std::uint32_t vertexShader, std::uint32_t fragmentShader);
    bool CreateFramebuffer(std::uint32_t& framebuffer, std::uint32_t& texture, int width, int height);
    bool CreateRenderTarget(int width, int height);
    void DestroyRenderTarget();

    void SyncChain(ShaderChainStore& shaders);
    void ClearPasses();
    std::uint32_t NamedGLTexture(TextureStore& textures, const std::string& name);
    void ClearTextureCache();

    std::uint32_t _framebuffer{0};
    std::uint32_t _texture{0};
    std::uint32_t _framebuffer2{0};
    std::uint32_t _texture2{0};
    std::uint32_t _vertexArray{0};
    std::uint32_t _program{0};
    std::uint32_t _fallbackTexture{0};
    int _mirrorXLocation{-1};
    int _mirrorYLocation{-1};
    int _rotationLocation{-1};
    int _zoomLocation{-1};
    int _outputSizeLocation{-1};
    int _width{0};
    int _height{0};
    bool _active{false};

    std::vector<UserPass> _passes;
    std::uint64_t _chainGeneration{0};
    std::uint64_t _paramsGeneration{0};
    std::map<std::string, std::uint32_t> _textureCache; //!< Named texture -> GL texture.
    std::uint64_t _textureStoreGeneration{0};

    Poco::Logger& _logger{Poco::Logger::get("VisualPostProcessor")};
};
