#include "ProjectMGUI.h"

#include "AnonymousProFont.h"
#include "LiberationSansFont.h"
#include "ProjectMWrapper.h"
#include "SDLRenderingWindow.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <Poco/NotificationCenter.h>

#include <Poco/Util/Application.h>

#include <algorithm>
#include <cfloat>
#include <string>
#include <utility>

const char* ProjectMGUI::name() const
{
    return "Preset Selection GUI";
}

void ProjectMGUI::initialize(Poco::Util::Application& app)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    Poco::Path userConfigurationDir = Poco::Path::configHome();
    userConfigurationDir.makeDirectory().append("projectM/");
    userConfigurationDir.setFileName(app.config().getString("application.baseName") + ".UI.ini");
    _uiIniFileName = userConfigurationDir.toString();

    io.IniFilename = _uiIniFileName.c_str();

    ImGui::StyleColorsDark();

    auto& renderingWindow = Poco::Util::Application::instance().getSubsystem<SDLRenderingWindow>();
    auto& projectMWrapper = Poco::Util::Application::instance().getSubsystem<ProjectMWrapper>();

    _projectMWrapper = &projectMWrapper;
    _renderingWindow = renderingWindow.GetRenderingWindow();
    _glContext = renderingWindow.GetGlContext();

    ImGui_ImplSDL2_InitForOpenGL(_renderingWindow, _glContext);
    ImGui_ImplOpenGL3_Init("#version 150");

    UpdateFontSize();

    // Set a sensible minimum window size to prevent layout assertions
    auto& style = ImGui::GetStyle();
    style.WindowMinSize = {128, 128};

    Poco::NotificationCenter::defaultCenter().addObserver(_displayToastNotificationObserver);
}

void ProjectMGUI::uninitialize()
{
    Poco::NotificationCenter::defaultCenter().removeObserver(_displayToastNotificationObserver);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    _projectMWrapper = nullptr;
    _renderingWindow = nullptr;
    _glContext = nullptr;
}

void ProjectMGUI::UpdateFontSize()
{
    ImGuiIO& io = ImGui::GetIO();

    auto displayIndex = SDL_GetWindowDisplayIndex(_renderingWindow);
    if (displayIndex < 0)
    {
        poco_debug_f1(_logger, "Could not get display index for application window: %s", std::string(SDL_GetError()));
        return;
    }

    auto newScalingFactor = GetScalingFactor();

    // Only interested in changes of .05 or more
    if (std::abs(_textScalingFactor - newScalingFactor) < 0.05)
    {
        return;
    }

    poco_debug_f3(_logger, "Scaling factor change for display %?d: %hf -> %hf", displayIndex, _textScalingFactor, newScalingFactor);

    _textScalingFactor = newScalingFactor;

    ImFontConfig config;
    config.MergeMode = true;

    io.Fonts->Clear();
    _uiFont = io.Fonts->AddFontFromMemoryCompressedTTF(&AnonymousPro_compressed_data, AnonymousPro_compressed_size, floor(24.0f * _textScalingFactor));
    _toastFont = io.Fonts->AddFontFromMemoryCompressedTTF(&LiberationSans_compressed_data, LiberationSans_compressed_size, floor(40.0f * _textScalingFactor));
    io.Fonts->Build();

    ImGui::GetStyle().ScaleAllSizes(1.0);
}

void ProjectMGUI::ProcessInput(const SDL_Event& event)
{
    ImGui_ImplSDL2_ProcessEvent(&event);
}

void ProjectMGUI::Toggle()
{
    _visible = !_visible;
}

void ProjectMGUI::Visible(bool visible)
{
    _visible = visible;
}

bool ProjectMGUI::Visible() const
{
    return _visible;
}

void ProjectMGUI::SetPerformanceMetrics(float fps, double frameTimeMilliseconds)
{
    _measuredFps = fps;
    _frameTimeMilliseconds = frameTimeMilliseconds;
}

void ProjectMGUI::Draw(const std::vector<TextOverlay>& textOverlays,
                       bool externalVisualsEnabled)
{
    const bool displayFps = Poco::Util::Application::instance().config().getBool(
        "projectM.displayFps", false);
    const bool displayTextOverlays =
        externalVisualsEnabled &&
        std::any_of(textOverlays.begin(), textOverlays.end(),
                    [](const TextOverlay& overlay) {
                        return overlay.visible && !overlay.text.empty();
                    });

    // Don't render UI at all if there's no need.
    if (!_toast && !_visible && !displayFps && !displayTextOverlays)
    {
        return;
    }

    if (_userScalingFactor != GetClampedUserScalingFactor())
    {
        UpdateFontSize();
    }

    ImGui_ImplSDL2_NewFrame();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    float secondsSinceLastFrame = .0f;
    if (_lastFrameTicks == 0)
    {
        _lastFrameTicks = SDL_GetTicks64();
    }
    else
    {
        auto currentFrameTicks = SDL_GetTicks64();
        secondsSinceLastFrame = static_cast<float>(currentFrameTicks - _lastFrameTicks) * .001f;
        _lastFrameTicks = currentFrameTicks;
    }

    if (displayTextOverlays)
    {
        for (const auto& overlay : textOverlays)
        {
            if (overlay.visible && !overlay.text.empty())
            {
                DrawTextOverlay(overlay);
            }
        }
    }

    if (_toast)
    {
        if (!_toast->Draw(secondsSinceLastFrame))
        {
            _toast.reset();
        }
    }

    if (_visible)
    {
        _mainMenu.Draw();
        _settingsWindow.Draw();
        _aboutWindow.Draw();
        _helpWindow.Draw();
    }

    if (displayFps)
    {
        constexpr auto flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoInputs;
        const auto& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(
            ImVec2(io.DisplaySize.x - 10.0F, 10.0F),
            ImGuiCond_Always, ImVec2(1.0F, 0.0F));
        ImGui::SetNextWindowBgAlpha(0.65F);
        if (ImGui::Begin("##PerformanceOverlay", nullptr, flags))
        {
            ImGui::Text("%.1f FPS  |  %.2f ms", _measuredFps,
                        _frameTimeMilliseconds);
        }
        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ProjectMGUI::DrawTextOverlay(const TextOverlay& overlay)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workPosition = viewport->WorkPos;
    const ImVec2 workSize = viewport->WorkSize;
    const ImVec2 position(
        workPosition.x + workSize.x * overlay.x,
        workPosition.y + workSize.y * overlay.y);

    ImVec2 pivot(0.5F, 0.5F);
    switch (overlay.anchor)
    {
        case TextOverlayAnchor::Center: break;
        case TextOverlayAnchor::Top: pivot = {0.5F, 0.0F}; break;
        case TextOverlayAnchor::Bottom: pivot = {0.5F, 1.0F}; break;
        case TextOverlayAnchor::Left: pivot = {0.0F, 0.5F}; break;
        case TextOverlayAnchor::Right: pivot = {1.0F, 0.5F}; break;
        case TextOverlayAnchor::TopLeft: pivot = {0.0F, 0.0F}; break;
        case TextOverlayAnchor::TopRight: pivot = {1.0F, 0.0F}; break;
        case TextOverlayAnchor::BottomLeft: pivot = {0.0F, 1.0F}; break;
        case TextOverlayAnchor::BottomRight: pivot = {1.0F, 1.0F}; break;
    }

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoInputs;

    const float scaledPadding = overlay.padding * _textScalingFactor;
    const float maximumTextWidth =
        std::max(1.0F, workSize.x * overlay.maxWidth);
    ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0.0F, 0.0F),
        ImVec2(maximumTextWidth + scaledPadding * 2.0F, FLT_MAX));
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(scaledPadding, scaledPadding));
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowRounding,
        overlay.cornerRadius * _textScalingFactor);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(overlay.background.r, overlay.background.g,
               overlay.background.b, overlay.background.a));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));

    ImFont* font = overlay.font == TextOverlayFont::Mono ? _uiFont : _toastFont;
    ImGui::PushFont(font);
    const std::string windowName = "##TextOverlay-" + overlay.name;
    if (ImGui::Begin(windowName.c_str(), nullptr, flags))
    {
        ImGui::SetWindowFontScale(overlay.size);
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(overlay.color.r, overlay.color.g,
                   overlay.color.b, overlay.color.a));

        const float availableWidth =
            std::min(maximumTextWidth, ImGui::GetContentRegionAvail().x);
        const float unwrappedWidth =
            ImGui::CalcTextSize(overlay.text.c_str()).x;
        float alignmentOffset = 0.0F;
        if (overlay.alignment == TextOverlayAlignment::Center)
        {
            alignmentOffset = (availableWidth - unwrappedWidth) * 0.5F;
        }
        else if (overlay.alignment == TextOverlayAlignment::Right)
        {
            alignmentOffset = availableWidth - unwrappedWidth;
        }
        if (alignmentOffset > 0.0F)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + alignmentOffset);
        }

        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + maximumTextWidth);
        ImGui::TextWrapped("%s", overlay.text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopFont();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

bool ProjectMGUI::WantsKeyboardInput()
{
    auto& io = ImGui::GetIO();
    return io.WantCaptureKeyboard;
}

bool ProjectMGUI::WantsMouseInput()
{
    auto& io = ImGui::GetIO();
    return io.WantCaptureMouse;
}

void ProjectMGUI::PushToastFont()
{
    ImGui::PushFont(_toastFont);
}

void ProjectMGUI::PushUIFont()
{
    ImGui::PushFont(_uiFont);
}

void ProjectMGUI::PopFont()
{
    ImGui::PopFont();
}

void ProjectMGUI::ShowSettingsWindow()
{
    _settingsWindow.Show();
}

void ProjectMGUI::ShowAboutWindow()
{
    _aboutWindow.Show();
}

void ProjectMGUI::ShowHelpWindow()
{
    _helpWindow.Show();
}

float ProjectMGUI::GetScalingFactor()
{
    int windowWidth;
    int windowHeight;
    int renderWidth;
    int renderHeight;

    SDL_GetWindowSize(_renderingWindow, &windowWidth, &windowHeight);
    SDL_GL_GetDrawableSize(_renderingWindow, &renderWidth, &renderHeight);

    _userScalingFactor = GetClampedUserScalingFactor();

    // If the OS has a scaled UI, this will return the inverse factor. E.g. if the display is scaled to 200%,
    // the renderWidth (in actual pixels) will be twice as much as the "virtual" unscaled window width.
    return ((static_cast<float>(windowWidth) / static_cast<float>(renderWidth)) + (static_cast<float>(windowHeight) / static_cast<float>(renderHeight))) * 0.5f * _userScalingFactor;
}

float ProjectMGUI::GetClampedUserScalingFactor()
{
    return std::min(3.0f, std::max(0.1f, static_cast<float>(Poco::Util::Application::instance().config().getDouble("window.uiScale", 1.0))));
}

void ProjectMGUI::DisplayToastNotificationHandler(const Poco::AutoPtr<DisplayToastNotification>& notification)
{
    if (Poco::Util::Application::instance().config().getBool("projectM.displayToasts", true))
    {
        _toast = std::make_unique<ToastMessage>(notification->Options());
    }
}
