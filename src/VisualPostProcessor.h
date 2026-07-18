#pragma once

#include "network/VisualState.h"

#include <Poco/Logger.h>

#include <cstdint>
#include <string>

class ProjectMWrapper;

class VisualPostProcessor
{
public:
    VisualPostProcessor() = default;
    ~VisualPostProcessor();

    bool Initialize(int width, int height);
    bool Resize(int width, int height);
    void Render(ProjectMWrapper& projectM, const VisualState& state);
    void Shutdown();

    bool Active() const;

private:
    static std::uint32_t CompileShader(std::uint32_t type, const char* source);
    static std::uint32_t LinkProgram(std::uint32_t vertexShader,
                                     std::uint32_t fragmentShader);
    bool CreateRenderTarget(int width, int height);
    void DestroyRenderTarget();

    std::uint32_t _framebuffer{0};
    std::uint32_t _texture{0};
    std::uint32_t _vertexArray{0};
    std::uint32_t _program{0};
    int _mirrorXLocation{-1};
    int _mirrorYLocation{-1};
    int _rotationLocation{-1};
    int _zoomLocation{-1};
    int _outputSizeLocation{-1};
    int _width{0};
    int _height{0};
    bool _active{false};

    Poco::Logger& _logger{Poco::Logger::get("VisualPostProcessor")};
};
