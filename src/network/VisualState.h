#pragma once

#include <cstdint>
#include <mutex>

enum VisualProperty : std::uint32_t
{
    VisualPropertyNone = 0,
    VisualPropertyMirrorX = 1U << 0U,
    VisualPropertyMirrorY = 1U << 1U,
    VisualPropertyRotation = 1U << 2U,
    VisualPropertyZoom = 1U << 3U,
    VisualPropertyAll = VisualPropertyMirrorX | VisualPropertyMirrorY |
                        VisualPropertyRotation | VisualPropertyZoom
};

struct VisualState
{
    bool enabled{false};
    bool mirrorX{false};
    bool mirrorY{false};
    double rotationDegrees{0.0};
    double zoom{1.0};
};

struct VisualStatePatch
{
    std::uint32_t properties{VisualPropertyNone};
    bool mirrorX{false};
    bool mirrorY{false};
    double rotationDegrees{0.0};
    double zoom{1.0};
};

class VisualStateStore
{
public:
    explicit VisualStateStore(bool enabled = false);

    VisualState Get() const;
    void SetEnabled(bool enabled);
    void Apply(const VisualStatePatch& patch);
    void Reset();

private:
    mutable std::mutex _mutex;
    VisualState _state;
};
