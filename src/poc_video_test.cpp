// Standalone POC harness: verifies VideoDeck (FFmpeg decode -> GL texture ->
// passthrough render) in isolation, without the app's audio/projectM/network
// subsystems. Renders a few frames into a hidden window's framebuffer and dumps
// the result to a PPM. Usage: poc_video_test <video> <out.ppm>
#include "VideoDeck.h"

#include <SDL2/SDL.h>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#elif defined(USE_GLEW)
#include <GL/glew.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL_opengl.h>
#endif

#include <cstdio>
#include <exception>

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <video> <out.ppm>\n", argv[0]);
        return 2;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    const int width = 960;
    const int height = 540;
    SDL_Window* window = SDL_CreateWindow("poc_video_test", 0, 0, width, height,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (window == nullptr)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context == nullptr)
    {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return 1;
    }

    try
    {
        VideoDeck deck(argv[1]);
        deck.Initialize();
        // Decode/advance a few frames, rendering each into the (hidden) back buffer.
        for (int i = 0; i < 12; ++i)
        {
            deck.Update(SDL_GetTicks());
            deck.RenderToScreen(width, height);
        }
        deck.DumpScreen(argv[2], width, height);
        std::printf("poc_video_test: wrote %s (%dx%d)\n", argv[2], deck.Width(), deck.Height());
    }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "poc_video_test failed: %s\n", ex.what());
        return 1;
    }

    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
