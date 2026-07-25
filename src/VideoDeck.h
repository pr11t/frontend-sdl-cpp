#pragma once

#include <Poco/Logger.h>

#include <cstdint>
#include <string>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

/**
 * @brief Proof-of-concept video source ("video deck").
 *
 * Decodes a video file with FFmpeg into a single OpenGL texture that is updated
 * in place every frame, and can draw that texture full-screen. Mirrors the role
 * of Deck (a source that produces a color texture via Texture()) so it can later
 * be exposed to the shader-chain compositor as a named deck texture. Compositing
 * / "make it look nice" is intentionally out of scope for this POC.
 *
 * Prepare() performs FFmpeg work only and may run on a worker thread.
 * InitializeGraphics(), Update(), RenderToScreen(), and destruction after
 * graphics initialization must run on the render thread with the GL context
 * current.
 */
class VideoDeck
{
public:
    enum class UpdateResult
    {
        Running,
        EndOfStream
    };

    explicit VideoDeck(std::string path);
    ~VideoDeck();

    VideoDeck(const VideoDeck&) = delete;
    VideoDeck& operator=(const VideoDeck&) = delete;

    /// Convenience startup path: prepares the decoder and initializes graphics.
    void Initialize();

    /// Opens FFmpeg, initializes decoding/scaling, and decodes the first RGBA frame.
    /// Does not call OpenGL and may run on a background thread.
    void Prepare();

    /// Creates the OpenGL pipeline and uploads the frame produced by Prepare().
    void InitializeGraphics();

    /// Advances playback to @p nowMilliseconds (SDL_GetTicks) and uploads the current
    /// frame to the texture. Loops back to the start at end-of-stream.
    UpdateResult Update(std::uint32_t nowMilliseconds);

    /**
     * @brief Replaces the exhausted decoder with an independently prepared
     * decoder for the same video, preserving the existing OpenGL pipeline.
     */
    void RestartFromPrepared(VideoDeck& prepared, std::uint32_t nowMilliseconds);

    /// Draws the current frame full-screen (aspect-fit, letterboxed) to the bound framebuffer.
    void RenderToScreen(int screenWidth, int screenHeight);

    /// The live color texture holding the current video frame (0 until the first frame).
    std::uint32_t Texture() const { return _texture; }

    /// POC verification helper: reads the default framebuffer and writes it as a
    /// binary PPM (upright). Used to prove the render path without a screen grab.
    void DumpScreen(const std::string& path, int width, int height) const;

    int Width() const { return _width; }
    int Height() const { return _height; }
    const std::string& Path() const { return _path; }

private:
    bool DecodeNextFrame();  //!< Fills _frame with the next decoded frame; false at EOF.
    void ConvertCurrentFrame(); //!< Converts _frame into _rgba without calling OpenGL.
    void UploadPixels(); //!< Uploads _rgba into _texture.
    void UploadCurrentFrame(); //!< Converts _frame and uploads it into _texture.
    void CreatePipeline();   //!< Compiles the passthrough shader and creates the VAO.
    void Shutdown();

    std::string _path;

    AVFormatContext* _formatContext{nullptr};
    AVCodecContext* _codecContext{nullptr};
    SwsContext* _swsContext{nullptr};
    AVFrame* _frame{nullptr};
    AVPacket* _packet{nullptr};
    int _videoStreamIndex{-1};
    double _timeBaseSeconds{0.0};

    std::vector<std::uint8_t> _rgba; //!< Tightly packed RGBA of the current frame.
    int _width{0};
    int _height{0};

    double _currentFramePts{-1.0}; //!< Presentation time (seconds) of the frame in _texture.
    std::uint32_t _startMilliseconds{0};
    bool _started{false};
    bool _prepared{false};

    std::uint32_t _texture{0};
    std::uint32_t _program{0};
    std::uint32_t _vertexArray{0};
    int _scaleLocation{-1};
    int _textureLocation{-1};

    Poco::Logger& _logger{Poco::Logger::get("VideoDeck")};
};
