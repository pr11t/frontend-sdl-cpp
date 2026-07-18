#include "VisualPostProcessor.h"

#include "ProjectMWrapper.h"

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
constexpr auto vertexShaderSource = R"(#version 300 es
precision highp float;
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

constexpr auto fragmentShaderSource = R"(#version 300 es
precision highp float;
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
#else
constexpr auto vertexShaderSource = R"(#version 330 core
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

constexpr auto fragmentShaderSource = R"(#version 330 core
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
#endif

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
            vertexShader = CompileShader(GL_VERTEX_SHADER, vertexShaderSource);
            fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
            _program = LinkProgram(vertexShader, fragmentShader);
        }
        catch (...)
        {
            if (vertexShader != 0)
            {
                glDeleteShader(vertexShader);
            }
            if (fragmentShader != 0)
            {
                glDeleteShader(fragmentShader);
            }
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

        if (!CreateRenderTarget(width, height))
        {
            throw std::runtime_error("Post-processing framebuffer is incomplete.");
        }
        _active = true;
        poco_information_f2(_logger,
                            "Visual post-processing enabled at %?dx%?d.",
                            width, height);
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

void VisualPostProcessor::Render(ProjectMWrapper& projectM, const VisualState& state)
{
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    glViewport(0, 0, _width, _height);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    projectM.RenderFrameToFramebuffer(_framebuffer);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, _width, _height);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(_program);
    glUniform1i(_mirrorXLocation, state.mirrorX ? 1 : 0);
    glUniform1i(_mirrorYLocation, state.mirrorY ? 1 : 0);
    glUniform1f(_rotationLocation,
                static_cast<float>(state.rotationDegrees) * degreesToRadians);
    glUniform1f(_zoomLocation, static_cast<float>(state.zoom));
    glUniform2f(_outputSizeLocation, static_cast<float>(_width),
                static_cast<float>(_height));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _texture);
    glBindVertexArray(_vertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void VisualPostProcessor::Shutdown()
{
    DestroyRenderTarget();
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
    _active = false;
}

bool VisualPostProcessor::Active() const
{
    return _active;
}

std::uint32_t VisualPostProcessor::CompileShader(std::uint32_t type,
                                                 const char* source)
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

bool VisualPostProcessor::CreateRenderTarget(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return false;
    }

    glGenTextures(1, reinterpret_cast<GLuint*>(&_texture));
    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, reinterpret_cast<GLuint*>(&_framebuffer));
    glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, _texture, 0);
    const auto complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                          GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!complete)
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
    if (_framebuffer != 0)
    {
        glDeleteFramebuffers(1, reinterpret_cast<GLuint*>(&_framebuffer));
        _framebuffer = 0;
    }
    if (_texture != 0)
    {
        glDeleteTextures(1, reinterpret_cast<GLuint*>(&_texture));
        _texture = 0;
    }
    _width = 0;
    _height = 0;
}
