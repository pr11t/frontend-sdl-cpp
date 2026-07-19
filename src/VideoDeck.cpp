#include "VideoDeck.h"

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

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

#ifdef USE_GLES
constexpr auto glslHeader = "#version 300 es\nprecision mediump float;\n";
#else
constexpr auto glslHeader = "#version 330 core\n";
#endif

// VBO-less full-screen triangle. uScale letterboxes the quad to preserve the
// video aspect ratio; the sampled row is flipped because FFmpeg gives us the
// top row first while GL texture row 0 is the bottom.
const std::string vertexShaderSource = std::string(glslHeader) + R"(
out vec2 textureCoordinate;
uniform vec2 uScale;
void main()
{
    const vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vec2 position = positions[gl_VertexID];
    textureCoordinate = vec2(position.x * 0.5 + 0.5, 1.0 - (position.y * 0.5 + 0.5));
    gl_Position = vec4(position * uScale, 0.0, 1.0);
}
)";

const std::string fragmentShaderSource = std::string(glslHeader) + R"(
in vec2 textureCoordinate;
out vec4 fragmentColor;
uniform sampler2D videoTexture;
void main()
{
    fragmentColor = texture(videoTexture, textureCoordinate);
}
)";

std::uint32_t CompileShader(std::uint32_t type, const char* source)
{
    std::uint32_t shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("VideoDeck shader compile failed: ") + log);
    }
    return shader;
}

} // namespace

VideoDeck::VideoDeck(std::string path)
    : _path(std::move(path))
{
}

VideoDeck::~VideoDeck()
{
    Shutdown();
}

void VideoDeck::Initialize()
{
    if (avformat_open_input(&_formatContext, _path.c_str(), nullptr, nullptr) != 0)
    {
        throw std::runtime_error("VideoDeck: could not open input '" + _path + "'.");
    }
    if (avformat_find_stream_info(_formatContext, nullptr) < 0)
    {
        throw std::runtime_error("VideoDeck: could not read stream info from '" + _path + "'.");
    }

    const AVCodec* codec = nullptr;
    _videoStreamIndex = av_find_best_stream(_formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (_videoStreamIndex < 0 || codec == nullptr)
    {
        throw std::runtime_error("VideoDeck: no video stream in '" + _path + "'.");
    }

    AVStream* stream = _formatContext->streams[_videoStreamIndex];
    _timeBaseSeconds = av_q2d(stream->time_base);

    _codecContext = avcodec_alloc_context3(codec);
    if (_codecContext == nullptr ||
        avcodec_parameters_to_context(_codecContext, stream->codecpar) < 0 ||
        avcodec_open2(_codecContext, codec, nullptr) < 0)
    {
        throw std::runtime_error("VideoDeck: could not open decoder for '" + _path + "'.");
    }

    _width = _codecContext->width;
    _height = _codecContext->height;
    if (_width <= 0 || _height <= 0)
    {
        throw std::runtime_error("VideoDeck: invalid video dimensions.");
    }

    _swsContext = sws_getContext(_width, _height, _codecContext->pix_fmt,
                                 _width, _height, AV_PIX_FMT_RGBA,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (_swsContext == nullptr)
    {
        throw std::runtime_error("VideoDeck: could not create swscale context.");
    }

    _frame = av_frame_alloc();
    _packet = av_packet_alloc();
    _rgba.assign(static_cast<std::size_t>(_width) * _height * 4, 0);
    if (_frame == nullptr || _packet == nullptr)
    {
        throw std::runtime_error("VideoDeck: out of memory allocating FFmpeg frames.");
    }

    CreatePipeline();

    poco_information_f3(_logger, "VideoDeck ready: %s (%dx%d)", _path, _width, _height);
}

void VideoDeck::CreatePipeline()
{
    glGenTextures(1, reinterpret_cast<GLuint*>(&_texture));
    glBindTexture(GL_TEXTURE_2D, _texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _width, _height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    std::uint32_t vertexShader = CompileShader(GL_VERTEX_SHADER, vertexShaderSource.c_str());
    std::uint32_t fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource.c_str());
    _program = glCreateProgram();
    glAttachShader(_program, vertexShader);
    glAttachShader(_program, fragmentShader);
    glLinkProgram(_program);
    GLint status = GL_FALSE;
    glGetProgramiv(_program, GL_LINK_STATUS, &status);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    if (status != GL_TRUE)
    {
        char log[1024];
        glGetProgramInfoLog(_program, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("VideoDeck program link failed: ") + log);
    }

    _scaleLocation = glGetUniformLocation(_program, "uScale");
    _textureLocation = glGetUniformLocation(_program, "videoTexture");

    glGenVertexArrays(1, reinterpret_cast<GLuint*>(&_vertexArray));
}

bool VideoDeck::DecodeNextFrame()
{
    while (true)
    {
        int ret = avcodec_receive_frame(_codecContext, _frame);
        if (ret == 0)
        {
            return true;
        }
        if (ret == AVERROR_EOF)
        {
            return false;
        }
        if (ret != AVERROR(EAGAIN))
        {
            return false;
        }

        // Decoder needs more input.
        int read = av_read_frame(_formatContext, _packet);
        if (read < 0)
        {
            avcodec_send_packet(_codecContext, nullptr); // flush; drains remaining frames then EOF.
            continue;
        }
        if (_packet->stream_index == _videoStreamIndex)
        {
            avcodec_send_packet(_codecContext, _packet);
        }
        av_packet_unref(_packet);
    }
}

void VideoDeck::UploadCurrentFrame()
{
    std::uint8_t* destData[4] = {_rgba.data(), nullptr, nullptr, nullptr};
    int destLineSize[4] = {_width * 4, 0, 0, 0};
    sws_scale(_swsContext, _frame->data, _frame->linesize, 0, _height, destData, destLineSize);

    glBindTexture(GL_TEXTURE_2D, _texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _width, _height, GL_RGBA, GL_UNSIGNED_BYTE, _rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    const double pts = static_cast<double>(_frame->best_effort_timestamp) * _timeBaseSeconds;
    _currentFramePts = pts < 0.0 ? _currentFramePts + 1.0 / 30.0 : pts;
    av_frame_unref(_frame);
}

void VideoDeck::SeekToStart()
{
    av_seek_frame(_formatContext, _videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(_codecContext);
    _currentFramePts = -1.0;
}

void VideoDeck::Update(std::uint32_t nowMilliseconds)
{
    if (!_started)
    {
        _startMilliseconds = nowMilliseconds;
        _started = true;
    }

    const double elapsed = static_cast<double>(nowMilliseconds - _startMilliseconds) / 1000.0;

    // Decode forward until the frame we hold is due (bounded so a long stall can't
    // spin forever in one render tick).
    for (int guard = 0; guard < 240; ++guard)
    {
        if (_currentFramePts >= 0.0 && _currentFramePts > elapsed)
        {
            break;
        }
        if (!DecodeNextFrame())
        {
            SeekToStart();
            _startMilliseconds = nowMilliseconds;
            if (!DecodeNextFrame())
            {
                break;
            }
        }
        UploadCurrentFrame();
    }
}

void VideoDeck::RenderToScreen(int screenWidth, int screenHeight)
{
    if (_texture == 0 || screenWidth <= 0 || screenHeight <= 0)
    {
        return;
    }

    // Letterbox: shrink the quad on the axis where the screen is "longer" than
    // the video so the aspect ratio is preserved.
    const double videoAspect = static_cast<double>(_width) / _height;
    const double screenAspect = static_cast<double>(screenWidth) / screenHeight;
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    if (screenAspect > videoAspect)
    {
        scaleX = static_cast<float>(videoAspect / screenAspect);
    }
    else
    {
        scaleY = static_cast<float>(screenAspect / videoAspect);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screenWidth, screenHeight);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(_program);
    glUniform2f(_scaleLocation, scaleX, scaleY);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, _texture);
    glUniform1i(_textureLocation, 0);
    glBindVertexArray(_vertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void VideoDeck::DumpScreen(const std::string& path, int width, int height) const
{
    if (width <= 0 || height <= 0)
    {
        return;
    }
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << " " << height << "\n255\n";
    // glReadPixels returns bottom row first; write top-down for an upright image.
    for (int y = height - 1; y >= 0; --y)
    {
        const std::uint8_t* row = pixels.data() + static_cast<std::size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x)
        {
            out.put(static_cast<char>(row[x * 4 + 0]));
            out.put(static_cast<char>(row[x * 4 + 1]));
            out.put(static_cast<char>(row[x * 4 + 2]));
        }
    }
    poco_information_f1(_logger, "VideoDeck dumped frame to %s", path);
}

void VideoDeck::Shutdown()
{
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
    if (_texture != 0)
    {
        glDeleteTextures(1, reinterpret_cast<GLuint*>(&_texture));
        _texture = 0;
    }
    if (_swsContext != nullptr)
    {
        sws_freeContext(_swsContext);
        _swsContext = nullptr;
    }
    if (_packet != nullptr)
    {
        av_packet_free(&_packet);
    }
    if (_frame != nullptr)
    {
        av_frame_free(&_frame);
    }
    if (_codecContext != nullptr)
    {
        avcodec_free_context(&_codecContext);
    }
    if (_formatContext != nullptr)
    {
        avformat_close_input(&_formatContext);
    }
}
