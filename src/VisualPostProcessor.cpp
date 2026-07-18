#include "VisualPostProcessor.h"

#include "ProjectMWrapper.h"

#include "network/ShaderChainStore.h"
#include "network/TextureStore.h"

#ifdef USE_GLES
#include <SDL2/SDL_opengles2.h>
#elif defined(USE_GLEW)
#include <GL/glew.h>
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL_opengl.h>
#endif

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace {

#ifdef USE_GLES
constexpr auto glslHeader = "#version 300 es\nprecision highp float;\n";
#else
constexpr auto glslHeader = "#version 330 core\n";
#endif

const std::string vertexShaderSource = std::string(glslHeader) + R"(
out vec2 textureCoordinate;
const vec2 positions[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);
void main()
{
    vec2 position = positions[gl_VertexID];
    textureCoordinate = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

const std::string transformShaderSource = std::string(glslHeader) + R"(
in vec2 textureCoordinate;
out vec4 fragmentColor;
uniform sampler2D projectMTexture;
uniform bool mirrorX;
uniform bool mirrorY;
uniform float rotationRadians;
uniform float zoom;
uniform vec2 outputSize;
void main()
{
    vec2 point = textureCoordinate - vec2(0.5);
    float aspect = outputSize.x / outputSize.y;
    point.x *= aspect;
    point /= zoom;
    if (mirrorX) point.x = -point.x;
    if (mirrorY) point.y = -point.y;
    float cosine = cos(rotationRadians);
    float sine = sin(rotationRadians);
    point = mat2(cosine, -sine, sine, cosine) * point;
    point.x /= aspect;
    vec2 sampleCoordinate = point + vec2(0.5);
    if (any(lessThan(sampleCoordinate, vec2(0.0))) ||
        any(greaterThan(sampleCoordinate, vec2(1.0))))
    {
        fragmentColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
    else
    {
        fragmentColor = texture(projectMTexture, sampleCoordinate);
    }
}
)";

// Preamble/epilogue wrapping a user post-processing shader. The user supplies a
// function `vec4 effect(vec2 uv)` and may declare extra `uniform float <name>;`.
const std::string userShaderPreamble = std::string(glslHeader) + R"(
in vec2 textureCoordinate;
out vec4 fragmentColor;
uniform sampler2D uInput;
uniform sampler2D uTexture;
uniform vec2 uResolution;
uniform float uTime;
)";

constexpr auto userShaderEpilogue = R"(
void main()
{
    fragmentColor = effect(textureCoordinate);
}
)";

constexpr float degreesToRadians = 0.017453292519943295769F;

} // namespace

VisualPostProcessor::~VisualPostProcessor()
{
    Shutdown();
}

bool VisualPostProcessor::Initialize(int width, int height)
{
    if (_active)
    {
        return true;
    }
    try
    {
        std::uint32_t vertexShader = 0;
        std::uint32_t fragmentShader = 0;
        try
        {
            vertexShader = CompileShader(GL_VERTEX_SHADER, vertexShaderSource.c_str());
            fragmentShader = CompileShader(GL_FRAGMENT_SHADER, transformShaderSource.c_str());
            _program = LinkProgram(vertexShader, fragmentShader);
        }
        catch (...)
        {
            if (vertexShader != 0) { glDeleteShader(vertexShader); }
            if (fragmentShader != 0) { glDeleteShader(fragmentShader); }
            throw;
        }
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        glGenVertexArrays(1, reinterpret_cast<GLuint*>(&_vertexArray));
        _mirrorXLocation = glGetUniformLocation(_program, "mirrorX");
        _mirrorYLocation = glGetUniformLocation(_program, "mirrorY");
        _rotationLocation = glGetUniformLocation(_program, "rotationRadians");
        _zoomLocation = glGetUniformLocation(_program, "zoom");
        _outputSizeLocation = glGetUniformLocation(_program, "outputSize");
        glUseProgram(_program);
        glUniform1i(glGetUniformLocation(_program, "projectMTexture"), 0);
        glUseProgram(0);

        // 1x1 black fallback so user shaders sampling uTexture with no bound
        // texture get defined behaviour.
        const unsigned char black[4] = {0, 0, 0, 255};
        glGenTextures(1, reinterpret_cast<GLuint*>(&_fallbackTexture));
        glBindTexture(GL_TEXTURE_2D, _fallbackTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (!CreateRenderTarget(width, height))
        {
            throw std::runtime_error("Post-processing framebuffer is incomplete.");
        }
        _active = true;
        poco_information_f2(_logger, "Visual post-processing enabled at %?dx%?d.", width, height);
        return true;
    }
    catch (const std::exception& error)
    {
        poco_error_f1(_logger,
                      "Visual post-processing initialization failed; using direct rendering: %s",
                      std::string(error.what()));
        Shutdown();
        return false;
    }
}

bool VisualPostProcessor::Resize(int width, int height)
{
    if (!_active || (width == _width && height == _height))
    {
        return true;
    }
    DestroyRenderTarget();
    if (CreateRenderTarget(width, height))
    {
        return true;
    }
    poco_error(_logger,
               "Could not resize visual post-processing framebuffer; using direct rendering.");
    Shutdown();
    return false;
}

void VisualPostProcessor::SyncChain(ShaderChainStore& shaders)
{
    const auto structureGeneration = shaders.Generation();
    if (structureGeneration != _chainGeneration)
    {
        // Chain structure changed: recompile everything.
        _chainGeneration = structureGeneration;
        ClearPasses();
        const auto snapshot = shaders.GetSnapshot();
        _paramsGeneration = snapshot.paramsGeneration;
        for (std::size_t index = 0; index < snapshot.chain.size(); ++index)
        {
            const auto& config = snapshot.chain[index];
            const auto sourceIt = snapshot.sources.find(config.shader);
            if (sourceIt == snapshot.sources.end())
            {
                shaders.SetCompileStatus(config.shader, false, "Shader not found.");
                continue;
            }

            const std::string wrapped = userShaderPreamble + sourceIt->second + userShaderEpilogue;
            std::string error;
            std::uint32_t vertexShader = 0;
            std::uint32_t fragmentShader = 0;
            std::uint32_t program = 0;
            try
            {
                vertexShader = CompileShader(GL_VERTEX_SHADER, vertexShaderSource.c_str());
                fragmentShader = CompileShader(GL_FRAGMENT_SHADER, wrapped.c_str());
                program = LinkProgram(vertexShader, fragmentShader);
            }
            catch (const std::exception& exception)
            {
                error = exception.what();
            }
            if (vertexShader != 0) { glDeleteShader(vertexShader); }
            if (fragmentShader != 0) { glDeleteShader(fragmentShader); }

            if (program == 0)
            {
                shaders.SetCompileStatus(config.shader, false, error);
                continue;
            }

            UserPass pass;
            pass.shaderName = config.shader;
            pass.textureName = config.texture;
            pass.chainIndex = index;
            pass.program = program;
            pass.inputLocation = glGetUniformLocation(program, "uInput");
            pass.textureLocation = glGetUniformLocation(program, "uTexture");
            pass.resolutionLocation = glGetUniformLocation(program, "uResolution");
            pass.timeLocation = glGetUniformLocation(program, "uTime");
            for (const auto& [paramName, paramValue] : config.params)
            {
                UserParam param;
                param.name = paramName;
                param.location = glGetUniformLocation(program, paramName.c_str());
                param.value = paramValue;
                pass.params.push_back(std::move(param));
            }
            _passes.push_back(std::move(pass));
            shaders.SetCompileStatus(config.shader, true, "");
        }
        return;
    }

    const auto paramsGeneration = shaders.ParamsGeneration();
    if (paramsGeneration != _paramsGeneration)
    {
        // Only parameter values changed: refresh them in place, no recompile.
        _paramsGeneration = paramsGeneration;
        const auto snapshot = shaders.GetSnapshot();
        for (auto& pass : _passes)
        {
            if (pass.chainIndex >= snapshot.chain.size())
            {
                continue;
            }
            const auto& configParams = snapshot.chain[pass.chainIndex].params;
            for (auto& param : pass.params)
            {
                const auto it = configParams.find(param.name);
                if (it != configParams.end())
                {
                    param.value = it->second;
                }
            }
        }
    }
}

void VisualPostProcessor::Render(ProjectMWrapper& projectM, const VisualState& state,
                                 ShaderChainStore& shaders, TextureStore& textures,
                                 const PostProcessInputs& inputs)
{
    // 1. Render projectM into framebuffer A.
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    glViewport(0, 0, _width, _height);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    projectM.RenderFrameToFramebuffer(_framebuffer);

    SyncChain(shaders);
    if (textures.Generation() != _textureStoreGeneration)
    {
        ClearTextureCache();
        _textureStoreGeneration = textures.Generation();
    }

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // Pass 0 is the built-in transform; passes 1..N are user shaders. Ping-pong
    // between framebuffers A and B; the final pass renders to the screen.
    const std::size_t totalPasses = 1 + _passes.size();
    GLuint readTexture = _texture;
    for (std::size_t i = 0; i < totalPasses; ++i)
    {
        const bool last = (i + 1 == totalPasses);
        GLuint targetFramebuffer = 0;
        GLuint nextTexture = 0;
        if (!last)
        {
            if (readTexture == _texture)
            {
                targetFramebuffer = _framebuffer2;
                nextTexture = _texture2;
            }
            else
            {
                targetFramebuffer = _framebuffer;
                nextTexture = _texture;
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
        glViewport(0, 0, _width, _height);
        glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);

        if (i == 0)
        {
            glUseProgram(_program);
            glUniform1i(_mirrorXLocation, state.mirrorX ? 1 : 0);
            glUniform1i(_mirrorYLocation, state.mirrorY ? 1 : 0);
            glUniform1f(_rotationLocation, static_cast<float>(state.rotationDegrees) * degreesToRadians);
            glUniform1f(_zoomLocation, static_cast<float>(state.zoom));
            glUniform2f(_outputSizeLocation, static_cast<float>(_width), static_cast<float>(_height));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, readTexture);
        }
        else
        {
            const UserPass& pass = _passes[i - 1];
            glUseProgram(pass.program);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, readTexture);
            if (pass.inputLocation >= 0) { glUniform1i(pass.inputLocation, 0); }

            glActiveTexture(GL_TEXTURE1);
            GLuint boundTexture = _fallbackTexture;
            if (!pass.textureName.empty())
            {
                const GLuint named = NamedGLTexture(textures, pass.textureName);
                if (named != 0) { boundTexture = named; }
            }
            glBindTexture(GL_TEXTURE_2D, boundTexture);
            if (pass.textureLocation >= 0) { glUniform1i(pass.textureLocation, 1); }

            if (pass.resolutionLocation >= 0)
            {
                glUniform2f(pass.resolutionLocation, static_cast<float>(_width), static_cast<float>(_height));
            }
            if (pass.timeLocation >= 0) { glUniform1f(pass.timeLocation, inputs.time); }
            for (const auto& param : pass.params)
            {
                if (param.location >= 0) { glUniform1f(param.location, param.value); }
            }
        }

        glBindVertexArray(_vertexArray);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        readTexture = nextTexture;
    }

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void VisualPostProcessor::Shutdown()
{
    ClearPasses();
    ClearTextureCache();
    DestroyRenderTarget();
    if (_fallbackTexture != 0)
    {
        glDeleteTextures(1, reinterpret_cast<GLuint*>(&_fallbackTexture));
        _fallbackTexture = 0;
    }
    if (_vertexArray != 0)
    {
        glDeleteVertexArrays(1, reinterpret_cast<GLuint*>(&_vertexArray));
        _vertexArray = 0;
    }
    if (_program != 0)
    {
        glDeleteProgram(_program);
        _program = 0;
    }
    _chainGeneration = 0;
    _textureStoreGeneration = 0;
    _active = false;
}

bool VisualPostProcessor::Active() const
{
    return _active;
}

void VisualPostProcessor::ClearPasses()
{
    for (auto& pass : _passes)
    {
        if (pass.program != 0)
        {
            glDeleteProgram(pass.program);
        }
    }
    _passes.clear();
}

std::uint32_t VisualPostProcessor::NamedGLTexture(TextureStore& textures, const std::string& name)
{
    const auto cached = _textureCache.find(name);
    if (cached != _textureCache.end())
    {
        return cached->second;
    }
    const auto image = textures.Find(name);
    if (!image || image->pixels.empty() || image->width <= 0 || image->height <= 0)
    {
        return 0;
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->width, image->height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, image->pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    _textureCache[name] = texture;
    return texture;
}

void VisualPostProcessor::ClearTextureCache()
{
    for (const auto& [name, texture] : _textureCache)
    {
        if (texture != 0)
        {
            glDeleteTextures(1, &texture);
        }
    }
    _textureCache.clear();
}

std::uint32_t VisualPostProcessor::CompileShader(std::uint32_t type, const char* source)
{
    const auto shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE)
    {
        return shader;
    }

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> log(static_cast<std::size_t>(std::max(logLength, 1)));
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error("Shader compilation failed: " + std::string(log.data()));
}

std::uint32_t VisualPostProcessor::LinkProgram(std::uint32_t vertexShader,
                                               std::uint32_t fragmentShader)
{
    const auto program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE)
    {
        return program;
    }

    GLint logLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> log(static_cast<std::size_t>(std::max(logLength, 1)));
    glGetProgramInfoLog(program, logLength, nullptr, log.data());
    glDeleteProgram(program);
    throw std::runtime_error("Shader linking failed: " + std::string(log.data()));
}

bool VisualPostProcessor::CreateFramebuffer(std::uint32_t& framebuffer, std::uint32_t& texture,
                                            int width, int height)
{
    glGenTextures(1, reinterpret_cast<GLuint*>(&texture));
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, reinterpret_cast<GLuint*>(&framebuffer));
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    const auto complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return complete;
}

bool VisualPostProcessor::CreateRenderTarget(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return false;
    }
    if (!CreateFramebuffer(_framebuffer, _texture, width, height) ||
        !CreateFramebuffer(_framebuffer2, _texture2, width, height))
    {
        DestroyRenderTarget();
        return false;
    }
    _width = width;
    _height = height;
    return true;
}

void VisualPostProcessor::DestroyRenderTarget()
{
    if (_framebuffer != 0) { glDeleteFramebuffers(1, reinterpret_cast<GLuint*>(&_framebuffer)); _framebuffer = 0; }
    if (_texture != 0) { glDeleteTextures(1, reinterpret_cast<GLuint*>(&_texture)); _texture = 0; }
    if (_framebuffer2 != 0) { glDeleteFramebuffers(1, reinterpret_cast<GLuint*>(&_framebuffer2)); _framebuffer2 = 0; }
    if (_texture2 != 0) { glDeleteTextures(1, reinterpret_cast<GLuint*>(&_texture2)); _texture2 = 0; }
    _width = 0;
    _height = 0;
}
