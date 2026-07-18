#include "ToastMessage.h"

#include "gui/ProjectMGUI.h"

#include "imgui.h"

#include <Poco/Util/Application.h>

#include <algorithm>
#include <utility>

ToastMessage::ToastMessage(ToastOptions options)
    : _gui(Poco::Util::Application::instance().getSubsystem<ProjectMGUI>())
    , _options(std::move(options))
    , _totalTime(_options.displayTime)
    , _displayTimeLeft(_options.displayTime)
{
}

bool ToastMessage::Draw(float lastFrameTime)
{
    _displayTimeLeft -= lastFrameTime;

    const float elapsed = _totalTime - _displayTimeLeft;
    const float progress = _totalTime > 0.0f ? std::clamp(elapsed / _totalTime, 0.0f, 1.0f) : 1.0f;

    // Fade in over the first 0.3s and out over the last second.
    const float fadeIn = std::min(elapsed / 0.3f, 1.0f);
    const float fadeOut = std::min(_displayTimeLeft, 1.0f);
    const float alpha = std::clamp(std::min(fadeIn, fadeOut), 0.0f, 1.0f);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 wp = viewport->WorkPos;
    const ImVec2 ws = viewport->WorkSize;
    constexpr float margin = 40.0f;
    const float cx = wp.x + ws.x * 0.5f;
    const float cy = wp.y + ws.y * 0.5f;

    ImVec2 pos(cx, cy);
    ImVec2 pivot(0.5f, 0.5f);
    switch (_options.anchor)
    {
        case ToastOptions::Anchor::Center: break;
        case ToastOptions::Anchor::Top: pos = {cx, wp.y + margin}; pivot = {0.5f, 0.0f}; break;
        case ToastOptions::Anchor::Bottom: pos = {cx, wp.y + ws.y - margin}; pivot = {0.5f, 1.0f}; break;
        case ToastOptions::Anchor::Left: pos = {wp.x + margin, cy}; pivot = {0.0f, 0.5f}; break;
        case ToastOptions::Anchor::Right: pos = {wp.x + ws.x - margin, cy}; pivot = {1.0f, 0.5f}; break;
        case ToastOptions::Anchor::TopLeft: pos = {wp.x + margin, wp.y + margin}; pivot = {0.0f, 0.0f}; break;
        case ToastOptions::Anchor::TopRight: pos = {wp.x + ws.x - margin, wp.y + margin}; pivot = {1.0f, 0.0f}; break;
        case ToastOptions::Anchor::BottomLeft: pos = {wp.x + margin, wp.y + ws.y - margin}; pivot = {0.0f, 1.0f}; break;
        case ToastOptions::Anchor::BottomRight: pos = {wp.x + ws.x - margin, wp.y + ws.y - margin}; pivot = {1.0f, 1.0f}; break;
    }

    if (_options.animation == ToastOptions::Animation::Scroll)
    {
        // Horizontal ticker: the window's left edge travels from the right edge
        // to well past the left edge over the full display time. The anchor sets
        // the vertical position only.
        pivot.x = 0.0f;
        pos.x = wp.x + ws.x - progress * (ws.x * 2.0f);
    }
    else if (_options.animation == ToastOptions::Animation::Slide)
    {
        // Slide in from the left over the first quarter of the display time.
        const float slide = std::clamp(progress / 0.25f, 0.0f, 1.0f);
        const float ease = 1.0f - (1.0f - slide) * (1.0f - slide);
        pos.x += (1.0f - ease) * -300.0f;
    }

    constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration |
                                             ImGuiWindowFlags_AlwaysAutoResize |
                                             ImGuiWindowFlags_NoSavedSettings |
                                             ImGuiWindowFlags_NoFocusOnAppearing |
                                             ImGuiWindowFlags_NoNav |
                                             ImGuiWindowFlags_NoMove |
                                             ImGuiWindowFlags_NoInputs;

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(0.35f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 10));

    if (ImGui::Begin("Toast", nullptr, windowFlags))
    {
        ImGui::SetWindowFontScale(_options.scale);
        _gui.PushToastFont();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(_options.r, _options.g, _options.b, _options.a));
        ImGui::TextUnformatted(_options.text.c_str());
        ImGui::PopStyleColor();
        _gui.PopFont();
    }
    ImGui::End();

    ImGui::PopStyleVar(2);

    return _displayTimeLeft > 0.0f;
}
