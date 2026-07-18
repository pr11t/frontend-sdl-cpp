#pragma once

#include <string>

/**
 * @brief Appearance and behaviour options for an on-screen toast message.
 */
struct ToastOptions
{
    enum class Anchor
    {
        Center,
        Top,
        Bottom,
        Left,
        Right,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    enum class Animation
    {
        Fade,   //!< Static, fading out near the end (default).
        Scroll, //!< Scrolls horizontally across the screen like a ticker.
        Slide   //!< Slides in from the nearest edge, then rests.
    };

    std::string text;
    float displayTime{3.0f}; //!< Seconds to display.
    Anchor anchor{Anchor::Center};
    Animation animation{Animation::Fade};
    float r{1.0f}; //!< Text colour, 0..1.
    float g{1.0f};
    float b{1.0f};
    float a{1.0f};
    float scale{1.0f}; //!< Font size multiplier.
};
