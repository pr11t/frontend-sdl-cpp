#include "network/VisualState.h"

VisualStateStore::VisualStateStore(bool enabled)
{
    _state.enabled = enabled;
}

VisualState VisualStateStore::Get() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _state;
}

void VisualStateStore::SetEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _state.enabled = enabled;
}

void VisualStateStore::Apply(const VisualStatePatch& patch)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if ((patch.properties & VisualPropertyMirrorX) != 0U)
    {
        _state.mirrorX = patch.mirrorX;
    }
    if ((patch.properties & VisualPropertyMirrorY) != 0U)
    {
        _state.mirrorY = patch.mirrorY;
    }
    if ((patch.properties & VisualPropertyRotation) != 0U)
    {
        _state.rotationDegrees = patch.rotationDegrees;
    }
    if ((patch.properties & VisualPropertyZoom) != 0U)
    {
        _state.zoom = patch.zoom;
    }
}

void VisualStateStore::Reset()
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto enabled = _state.enabled;
    _state = {};
    _state.enabled = enabled;
}
