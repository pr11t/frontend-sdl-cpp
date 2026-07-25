#pragma once

enum class VideoFit
{
    Stretch,
    Cover,
    Contain
};

struct VideoLayout
{
    VideoFit fit{VideoFit::Cover};
    float scale{1.0F};
    float offsetX{0.0F};
    float offsetY{0.0F};
};
