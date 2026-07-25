#pragma once

#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <vector>

enum class TextOverlayAnchor
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

enum class TextOverlayAlignment
{
    Left,
    Center,
    Right
};

enum class TextOverlayFont
{
    Sans,
    Mono
};

struct TextOverlayColor
{
    float r{1.0F};
    float g{1.0F};
    float b{1.0F};
    float a{1.0F};
};

struct TextOverlay
{
    std::string name;
    std::string text;
    bool visible{true};
    float x{0.5F}; //!< Horizontal position, normalized to the viewport.
    float y{0.85F}; //!< Vertical position, normalized to the viewport.
    TextOverlayAnchor anchor{TextOverlayAnchor::Center};
    TextOverlayAlignment alignment{TextOverlayAlignment::Center};
    TextOverlayFont font{TextOverlayFont::Sans};
    float size{1.0F};
    float maxWidth{0.8F}; //!< Maximum text width, normalized to the viewport.
    float padding{12.0F}; //!< Background padding in logical pixels.
    float cornerRadius{8.0F};
    TextOverlayColor color;
    TextOverlayColor background{0.0F, 0.0F, 0.0F, 0.55F};
};

/**
 * Thread-safe storage for persistent, named external text overlays.
 *
 * HTTP request threads update the store while the render thread consumes a
 * copied snapshot. The small overlay count keeps this simpler than adding
 * render commands and also makes GET immediately reflect accepted updates.
 */
class TextOverlayStore
{
public:
    void Set(const std::string& name, TextOverlay overlay);
    bool Find(const std::string& name, TextOverlay& overlay) const;
    bool Remove(const std::string& name);
    std::size_t Clear();
    std::vector<TextOverlay> List() const;

private:
    mutable std::mutex _mutex;
    std::map<std::string, TextOverlay> _overlays;
};
