#include "FPSLimiter.h"

#include <SDL2/SDL.h>

void FPSLimiter::TargetFPS(int fps)
{
    if (fps)
    {
        _targetFrameTime = 1000 / fps;
    }
    else
    {
        _targetFrameTime = 0;
    }
}

float FPSLimiter::FPS() const
{
    double frameTimeSum{0.0};
    uint32_t frameTimeCount{ 0 };

    for (auto _lastFrameTime : _lastFrameTimes)
    {
        if (_lastFrameTime > 0)
        {
            frameTimeCount++;
            frameTimeSum += _lastFrameTime;
        }
    }

    if (frameTimeCount == 0)
    {
        return 0.0f;
    }

    return static_cast<float>(1000.0 / (frameTimeSum / frameTimeCount));
}

double FPSLimiter::FrameTimeMilliseconds() const
{
    const int lastOffset = (_nextFrameTimesOffset + 9) % 10;
    return _lastFrameTimes[lastOffset];
}

double FPSLimiter::WorkTimeMilliseconds() const
{
    return _lastWorkTimeMilliseconds;
}

void FPSLimiter::StartFrame()
{
    _lastTickCount = SDL_GetTicks();
    _lastPerformanceCounter = SDL_GetPerformanceCounter();
}

void FPSLimiter::EndFrame()
{
    uint32_t frameTime = SDL_GetTicks() - _lastTickCount;
    const auto performanceFrequency = static_cast<double>(SDL_GetPerformanceFrequency());
    _lastWorkTimeMilliseconds =
        static_cast<double>(SDL_GetPerformanceCounter() - _lastPerformanceCounter) *
        1000.0 / performanceFrequency;

    if (_targetFrameTime && frameTime < _targetFrameTime)
    {
        SDL_Delay(_targetFrameTime - frameTime);
        frameTime = SDL_GetTicks() - _lastTickCount;
    }

    _lastFrameTimes[_nextFrameTimesOffset] =
        static_cast<double>(SDL_GetPerformanceCounter() - _lastPerformanceCounter) *
        1000.0 / performanceFrequency;
    _nextFrameTimesOffset = (_nextFrameTimesOffset + 1) % 10;
}
