#pragma once

#include <cstdint>
#include <memory>
#include <vector>

/**
 * @brief A decoded, in-memory image usable as a projectM texture.
 *
 * Pixels are tightly-packed RGBA, bottom row first (OpenGL convention), ready
 * to be handed to projectM's texture-load callback. Instances are immutable
 * once created and shared via DecodedImagePtr so they can be produced on the
 * network thread and consumed on the render thread without copying.
 */
struct DecodedImage
{
    std::vector<std::uint8_t> pixels; //!< RGBA pixel data, bottom row first.
    int width{0};
    int height{0};
    int channels{4};
};

using DecodedImagePtr = std::shared_ptr<const DecodedImage>;
