# Overlay port: pin libprojectM to an unreleased master commit.
#
# WHY THIS EXISTS
# ---------------
# The frontend calls projectm_opengl_render_frame_fbo() (see
# src/ProjectMWrapper.cpp -> RenderFrameToFramebuffer, used by the
# visual.postProcessingEnabled path in src/VisualPostProcessor.cpp).
#
# That symbol is the ONLY supported way to make libprojectM composite its
# output into a caller-provided framebuffer: internally ProjectM::RenderFrame()
# hard-binds the target FBO itself (glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ...)),
# so a caller cannot capture the frame by pre-binding its own FBO.
#
# The API was added upstream in commit bd2b1ba9f ("Add API function to enable
# rendering to custom FBO IDs", Jun 2024) and is NOT in any release tag: the
# newest projectm release is 4.1.7 and the vcpkg baseline (a62ce77) ships 4.1.4,
# neither of which exports it. Building the frontend against a released projectm
# therefore fails with: C3861: 'projectm_opengl_render_frame_fbo' not found.
#
# This overlay pins projectm to a specific master commit that exports the symbol
# so the vcpkg manifest build (deploy hosts) matches what the CI release
# workflows already do -- they build projectm from source at master's HEAD.
#
# FOLLOW-UP / RECONSIDER
# ----------------------
# Pinning production to unreleased master is a temporary measure. Prefer one of:
#   1. Bump REF/SHA512 to the first projectm release tag that ships the FBO API,
#      then delete this overlay and let the vcpkg baseline resolve projectm; or
#   2. Rework src/VisualPostProcessor.cpp so post-processing does NOT need the
#      FBO API -- render to the default framebuffer as usual, then copy the
#      backbuffer into a texture (glCopyTexSubImage2D) and run the shader pass
#      over that. Costs one extra full-frame GPU copy but removes the unreleased
#      dependency entirely and lets us drop this overlay.
#
# Pinned commit: projectM-visualizer/projectm @ 2f244141320f6b97b09bf99964cc72a4efdfcfd3
# (reports version 4.2.0-dev; on origin/master, publicly reachable).

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO projectM-visualizer/projectm
    REF 2f244141320f6b97b09bf99964cc72a4efdfcfd3
    SHA512 688b92011d7a4bd246e7d04d1c9b49024846465d21e10075873d6af9c0c45e82176f1c389fd0451bc7843dee3a9ee1e0b80c05766c4609e0551e7489fc8ee32d
    HEAD_REF master
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        "boost-filesystem" ENABLE_BOOST_FILESYSTEM
)

if (NOT ENABLE_BOOST_FILESYSTEM)
    message(STATUS
        "If your current vcpkg target triplet or toolchain does not support C++17 or lacks std::filesystem support, "
        "please enable the \"boost-filesystem\" feature.")
endif ()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}

        # Use projectm-eval and GLM from vcpkg ports (no vendored submodule
        # needed, so the plain GitHub source tarball above is sufficient).
        # master declares find_package(projectM-Eval QUIET) with no version
        # constraint, so the baseline projectm-eval port satisfies it.
        -DENABLE_SYSTEM_PROJECTM_EVAL=ON
        -DENABLE_SYSTEM_GLM=ON

        # Enforce additional build flags
        -DENABLE_PLAYLIST=ON
        -DENABLE_SDL_UI=OFF
        -DBUILD_TESTING=OFF
        -DBUILD_DOCS=OFF
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(
    PACKAGE_NAME "projectM4"
    CONFIG_PATH "lib/cmake/projectM4"
    DO_NOT_DELETE_PARENT_CONFIG_PATH
)

vcpkg_cmake_config_fixup(
    PACKAGE_NAME "projectM4Playlist"
    CONFIG_PATH "lib/cmake/projectM4Playlist"
)

vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
