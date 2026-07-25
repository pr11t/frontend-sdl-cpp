add_subdirectory(notifications)
add_subdirectory(gui)
add_subdirectory(network)

set(PROJECTM_CONFIGURATION_FILE "${CMAKE_CURRENT_BINARY_DIR}/projectMSDL.properties")
set(PROJECTM_CONFIGURATION_FILE "${PROJECTM_CONFIGURATION_FILE}" PARENT_SCOPE)
configure_file(resources/projectMSDL.properties.in "${PROJECTM_CONFIGURATION_FILE}" @ONLY)

add_executable(projectMSDL WIN32 MACOSX_BUNDLE
        AudioCapture.cpp
        AudioCapture.h
        Deck.cpp
        Deck.h
        FPSLimiter.cpp
        FPSLimiter.h
        ProjectMSDLApplication.cpp
        ProjectMSDLApplication.h
        ProjectMWrapper.cpp
        ProjectMWrapper.h
        RenderLoop.cpp
        RenderLoop.h
        SDLRenderingWindow.cpp
        SDLRenderingWindow.h
        VideoDeck.cpp
        VideoDeck.h
        VisualPostProcessor.cpp
        VisualPostProcessor.h
        network/NetworkControlSubsystem.cpp
        network/NetworkControlSubsystem.h
        main.cpp
        projectMSDL.rc
        )

string(TIMESTAMP BUILD_YEAR "%Y" UTC)
set_target_properties(projectMSDL PROPERTIES
        OUTPUT_NAME "$<IF:$<PLATFORM_ID:Darwin>,projectM,projectMSDL>" # On macOS, we name the app bundle just "projectM"
        MACOSX_BUNDLE_BUNDLE_NAME "projectM"
        MACOSX_BUNDLE_BUNDLE_VERSION "${projectMSDL_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright © ${BUILD_YEAR} The projectM team and contributors"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.projectm.frontend.sdl2"
        MACOSX_BUNDLE_ICON_FILE "projectMSDL.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${projectMSDL_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in"
        )

if (ENABLE_GLES)
    target_compile_definitions(projectMSDL
            PUBLIC
            USE_GLES
            )
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_sources(projectMSDL
            PRIVATE
            AudioCaptureImpl_WASAPI.h
            AudioCaptureImpl_WASAPI.cpp
            )
    target_compile_definitions(projectMSDL
            PRIVATE
            AUDIO_IMPL_HEADER="AudioCaptureImpl_WASAPI.h"
            )
else ()
    target_sources(projectMSDL
            PRIVATE
            AudioCaptureImpl_SDL.h
            AudioCaptureImpl_SDL.cpp
            )
    target_compile_definitions(projectMSDL
            PRIVATE
            AUDIO_IMPL_HEADER="AudioCaptureImpl_SDL.h"
            )
endif ()

# GLEW provides the modern desktop OpenGL entry points used by both the
# post-processing renderer and older libprojectM versions.
if (TARGET GLEW::GLEW)
    set(PROJECTMSDL_GLEW_TARGET GLEW::GLEW)
elseif (TARGET GLEW::glew)
    set(PROJECTMSDL_GLEW_TARGET GLEW::glew)
elseif (TARGET GLEW::glew_s)
    set(PROJECTMSDL_GLEW_TARGET GLEW::glew_s)
endif ()

if (PROJECTMSDL_GLEW_TARGET)
    target_compile_definitions(projectMSDL
            PRIVATE
            USE_GLEW
            )
endif ()

set_source_files_properties(ProjectMSDLApplication.cpp PROPERTIES
        COMPILE_DEFINITIONS PROJECTMSDL_CONFIG_LOCATION=\"${DEFAULT_CONFIG_PATH}\"
        )

target_compile_definitions(projectMSDL
        PRIVATE
        PROJECTMSDL_VERSION="${PROJECT_VERSION}"
        )

# FFmpeg powers the video deck. vcpkg supplies FindFFMPEG on Windows and other
# manifest builds. Pkg-config is the normal fallback for Homebrew/Linux.
find_package(FFMPEG QUIET)
if (FFMPEG_FOUND)
    set(PROJECTMSDL_FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES})
    target_include_directories(projectMSDL PRIVATE ${FFMPEG_INCLUDE_DIRS})
    if (FFMPEG_LIBRARY_DIRS)
        target_link_directories(projectMSDL PRIVATE ${FFMPEG_LIBRARY_DIRS})
    endif ()
else ()
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(FFMPEG_PC QUIET IMPORTED_TARGET
                libavformat
                libavcodec
                libswscale
                libavutil
                )
    endif ()
    if (TARGET PkgConfig::FFMPEG_PC)
        set(PROJECTMSDL_FFMPEG_LIBRARIES PkgConfig::FFMPEG_PC)
    else ()
        # Last-resort support for conventional local installations without
        # pkg-config metadata.
        find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
                HINTS
                /opt/homebrew/opt/ffmpeg/include
                /usr/local/opt/ffmpeg/include
                /opt/homebrew/include
                /usr/local/include
                )
        find_library(FFMPEG_AVCODEC avcodec
                HINTS /opt/homebrew/opt/ffmpeg/lib /usr/local/opt/ffmpeg/lib)
        find_library(FFMPEG_AVFORMAT avformat
                HINTS /opt/homebrew/opt/ffmpeg/lib /usr/local/opt/ffmpeg/lib)
        find_library(FFMPEG_AVUTIL avutil
                HINTS /opt/homebrew/opt/ffmpeg/lib /usr/local/opt/ffmpeg/lib)
        find_library(FFMPEG_SWSCALE swscale
                HINTS /opt/homebrew/opt/ffmpeg/lib /usr/local/opt/ffmpeg/lib)
        if (NOT FFMPEG_INCLUDE_DIR OR NOT FFMPEG_AVCODEC OR NOT FFMPEG_AVFORMAT
                OR NOT FFMPEG_AVUTIL OR NOT FFMPEG_SWSCALE)
            message(FATAL_ERROR
                    "FFmpeg (avcodec/avformat/avutil/swscale) not found. "
                    "Use the vcpkg manifest or install FFmpeg with pkg-config metadata."
                    )
        endif ()
        target_include_directories(projectMSDL PRIVATE ${FFMPEG_INCLUDE_DIR})
        set(PROJECTMSDL_FFMPEG_LIBRARIES
                ${FFMPEG_AVFORMAT}
                ${FFMPEG_AVCODEC}
                ${FFMPEG_SWSCALE}
                ${FFMPEG_AVUTIL}
                )
    endif ()
endif ()

target_link_libraries(projectMSDL
        PRIVATE
        ProjectMSDL-GUI
        ProjectMSDL-Network
        ProjectMSDL-Notifications
        libprojectM::playlist
        Poco::Util
        ${PROJECTMSDL_GLEW_TARGET}
        SDL2::SDL2$<$<STREQUAL:${SDL2_LINKAGE},static>:-static>
        SDL2::SDL2main
        OpenGL::$<IF:$<BOOL:${ENABLE_GLES}>,GLES3,GL>
        ${PROJECTMSDL_FFMPEG_LIBRARIES}
        )

if (MSVC)
    set_target_properties(projectMSDL
            PROPERTIES
            VS_DPI_AWARE "PerMonitor"
            )
endif ()

# POC verification harness: exercises VideoDeck offscreen (no audio/projectM), so
# the FFmpeg->GL render path can be checked without the app's CoreAudio init.
add_executable(poc_video_test
        VideoDeck.cpp
        VideoDeck.h
        poc_video_test.cpp
        )
target_include_directories(poc_video_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if (FFMPEG_FOUND)
    target_include_directories(poc_video_test PRIVATE ${FFMPEG_INCLUDE_DIRS})
    if (FFMPEG_LIBRARY_DIRS)
        target_link_directories(poc_video_test PRIVATE ${FFMPEG_LIBRARY_DIRS})
    endif ()
elseif (FFMPEG_INCLUDE_DIR)
    target_include_directories(poc_video_test PRIVATE ${FFMPEG_INCLUDE_DIR})
endif ()
target_link_libraries(poc_video_test
        PRIVATE
        Poco::Foundation
        ${PROJECTMSDL_GLEW_TARGET}
        SDL2::SDL2$<$<STREQUAL:${SDL2_LINKAGE},static>:-static>
        SDL2::SDL2main
        OpenGL::$<IF:$<BOOL:${ENABLE_GLES}>,GLES3,GL>
        ${PROJECTMSDL_FFMPEG_LIBRARIES}
        )
if (PROJECTMSDL_GLEW_TARGET)
    target_compile_definitions(poc_video_test PRIVATE USE_GLEW)
endif ()

# POC demo: composite a projectM preset over the video deck, offscreen, to raw frames.
add_executable(poc_demo
        VideoDeck.cpp
        VideoDeck.h
        poc_demo.cpp
        )
target_include_directories(poc_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if (FFMPEG_FOUND)
    target_include_directories(poc_demo PRIVATE ${FFMPEG_INCLUDE_DIRS})
    if (FFMPEG_LIBRARY_DIRS)
        target_link_directories(poc_demo PRIVATE ${FFMPEG_LIBRARY_DIRS})
    endif ()
elseif (FFMPEG_INCLUDE_DIR)
    target_include_directories(poc_demo PRIVATE ${FFMPEG_INCLUDE_DIR})
endif ()
target_link_libraries(poc_demo
        PRIVATE
        Poco::Foundation
        libprojectM::projectM
        ${PROJECTMSDL_GLEW_TARGET}
        SDL2::SDL2$<$<STREQUAL:${SDL2_LINKAGE},static>:-static>
        SDL2::SDL2main
        OpenGL::$<IF:$<BOOL:${ENABLE_GLES}>,GLES3,GL>
        ${PROJECTMSDL_FFMPEG_LIBRARIES}
        )
if (PROJECTMSDL_GLEW_TARGET)
    target_compile_definitions(poc_demo PRIVATE USE_GLEW)
endif ()
