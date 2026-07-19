// POC demo: composite a projectM preset (deck 0) over the video deck (deck 1),
// rendered offscreen and written as raw RGB24 frames for FFmpeg to encode. This
// runs without the app's audio subsystem (which hangs on headless CoreAudio),
// feeding projectM a synthetic waveform so the preset animates.
//
// Usage: poc_demo <video> <preset.milk> <textureDir> <out.rgb> <frames> <W> <H>
#include "VideoDeck.h"

#include <projectM-4/projectM.h>
#include <projectM-4/render_opengl.h>

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

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <vector>

namespace {

const char* kVertex =
    "#version 330 core\n"
    "out vec2 uv;\n"
    "void main(){\n"
    "  const vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));\n"
    "  uv = p[gl_VertexID]*0.5+0.5;\n"
    "  gl_Position = vec4(p[gl_VertexID],0,1);\n"
    "}\n";

// Video as a dimmed base, projectM swirl added on top (additive glow).
const char* kFragment =
    "#version 330 core\n"
    "in vec2 uv; out vec4 frag;\n"
    "uniform sampler2D projectMTexture; uniform sampler2D videoTexture;\n"
    "void main(){\n"
    "  vec3 p = texture(projectMTexture, uv).rgb;\n"
    "  vec3 v = texture(videoTexture, vec2(uv.x, 1.0-uv.y)).rgb;\n"
    "  vec3 o = v*0.55 + p;\n"
    "  frag = vec4(min(o, vec3(1.0)), 1.0);\n"
    "}\n";

GLuint Compile(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader error: %s\n", log);
        std::exit(1);
    }
    return s;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 8)
    {
        std::fprintf(stderr, "usage: %s <video> <preset.milk> <textureDir> <out.rgb> <frames> <W> <H>\n", argv[0]);
        return 2;
    }
    const char* videoPath = argv[1];
    const char* presetPath = argv[2];
    const char* textureDir = argv[3];
    const char* outPath = argv[4];
    const int frames = std::atoi(argv[5]);
    const int width = std::atoi(argv[6]);
    const int height = std::atoi(argv[7]);

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window = SDL_CreateWindow("poc_demo", 0, 0, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!window || !ctx)
    {
        std::fprintf(stderr, "GL init: %s\n", SDL_GetError());
        return 1;
    }

    // projectM (deck 0).
    projectm_handle pm = projectm_create();
    if (!pm)
    {
        std::fprintf(stderr, "projectm_create failed\n");
        return 1;
    }
    projectm_set_window_size(pm, width, height);
    projectm_set_mesh_size(pm, 96, 54);
    projectm_set_fps(pm, 30);
    const char* texPaths[1] = {textureDir};
    projectm_set_texture_search_paths(pm, texPaths, 1);
    projectm_load_preset_file(pm, presetPath, false);

    // projectM FBO/texture.
    GLuint pmTex = 0;
    GLuint pmFbo = 0;
    glGenTextures(1, &pmTex);
    glBindTexture(GL_TEXTURE_2D, pmTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &pmFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, pmFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pmTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Composite program + VAO.
    GLuint prog = glCreateProgram();
    glAttachShader(prog, Compile(GL_VERTEX_SHADER, kVertex));
    glAttachShader(prog, Compile(GL_FRAGMENT_SHADER, kFragment));
    glLinkProgram(prog);
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

    VideoDeck deck(videoPath);
    try
    {
        deck.Initialize();
    }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "VideoDeck: %s\n", ex.what());
        return 1;
    }

    FILE* out = std::fopen(outPath, "wb");
    if (!out)
    {
        std::fprintf(stderr, "cannot open %s\n", outPath);
        return 1;
    }

    const unsigned int pcmCount = 512;
    std::vector<float> pcm(pcmCount * 2);
    std::vector<unsigned char> rgba(static_cast<std::size_t>(width) * height * 4);
    std::vector<unsigned char> rgb(static_cast<std::size_t>(width) * height * 3);
    double sampleClock = 0.0;

    for (int f = 0; f < frames; ++f)
    {
        // Synthetic music so the preset reacts (bass pulse + tone).
        for (unsigned int i = 0; i < pcmCount; ++i)
        {
            const double t = (sampleClock + i) / 44100.0;
            const double env = 0.5 + 0.5 * std::sin(2.0 * M_PI * 2.0 * t);
            const float s = static_cast<float>(0.6 * env * std::sin(2.0 * M_PI * 90.0 * t) +
                                               0.25 * std::sin(2.0 * M_PI * 440.0 * t));
            pcm[i * 2] = s;
            pcm[i * 2 + 1] = s;
        }
        sampleClock += pcmCount;
        projectm_pcm_add_float(pm, pcm.data(), pcmCount, PROJECTM_STEREO);

        // Deck 0: projectM into its FBO.
        glBindFramebuffer(GL_FRAMEBUFFER, pmFbo);
        glViewport(0, 0, width, height);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        projectm_opengl_render_frame_fbo(pm, pmFbo);

        // Deck 1: advance the video texture.
        deck.Update(SDL_GetTicks());

        // Composite to the default framebuffer.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glDisable(GL_BLEND);
        glUseProgram(prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, pmTex);
        glUniform1i(glGetUniformLocation(prog, "projectMTexture"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, deck.Texture());
        glUniform1i(glGetUniformLocation(prog, "videoTexture"), 1);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Read back (bottom-up) and write RGB24 top-down.
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        for (int y = height - 1; y >= 0; --y)
        {
            const unsigned char* src = rgba.data() + static_cast<std::size_t>(y) * width * 4;
            unsigned char* dst = rgb.data() + static_cast<std::size_t>(height - 1 - y) * width * 3;
            for (int x = 0; x < width; ++x)
            {
                dst[x * 3 + 0] = src[x * 4 + 0];
                dst[x * 3 + 1] = src[x * 4 + 1];
                dst[x * 3 + 2] = src[x * 4 + 2];
            }
        }
        std::fwrite(rgb.data(), 1, rgb.size(), out);

        SDL_Delay(33); // real-time pacing so projectM and the video both advance ~30fps.
    }

    std::fclose(out);
    projectm_destroy(pm);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("poc_demo: wrote %d frames (%dx%d) to %s\n", frames, width, height, outPath);
    return 0;
}
