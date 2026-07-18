# Building and running this fork

This document covers the additions in this personal fork. The upstream
[README](../README.md#building-from-source) remains the general projectMSDL
build reference.

## Fork-specific requirements

In addition to the upstream dependencies, this fork requires:

- libprojectM 4.2 or newer, including the Playlist component
- POCO Foundation, JSON, Util, XML, and Net
- OpenGL 3.3, or OpenGL ES 3.2 when building with `ENABLE_GLES`
- GLEW on Windows for modern OpenGL entry points

The checked-in `vcpkg.json` manifest declares the required Windows
dependencies and POCO features.

Initialize the repository's submodules after cloning:

```sh
git submodule update --init --recursive
```

## macOS

Install the frontend build dependencies:

```sh
brew install cmake ninja sdl2 poco freetype git
```

Build and install libprojectM:

```sh
git clone --recurse-submodules \
  https://github.com/projectM-visualizer/projectm.git
cd projectm

cmake -G Ninja -S . -B cmake-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/dev/projectm-install" \
  -DENABLE_PLAYLIST=ON \
  -DBUILD_SHARED_LIBS=ON

cmake --build cmake-build --parallel
cmake --install cmake-build
```

Build and install this frontend:

```sh
cmake -G Ninja -S . -B cmake-build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/dev/projectm-install" \
  -DCMAKE_INSTALL_PREFIX="$HOME/dev/projectm-app" \
  -DENABLE_INSTALL_BDEPS=ON

cmake --build cmake-build-release --parallel
cmake --install cmake-build-release
```

The installed application bundle is:

```text
~/dev/projectm-app/projectM.app
```

Run the executable inside the bundle when passing command-line options:

```sh
"$HOME/dev/projectm-app/projectM.app/Contents/MacOS/projectM" \
  --networkBindAddress=127.0.0.1 \
  --enableVisualPostProcessing
```

## Windows with vcpkg

Install or clone vcpkg and set `VCPKG_ROOT`. CMake manifest mode installs the
dependencies declared in `vcpkg.json`, including POCO Net and GLEW.

From a Visual Studio developer shell:

```bat
cmake -S . -B cmake-build ^
  -G "Visual Studio 17 2022" ^
  -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
  -DCMAKE_INSTALL_PREFIX=C:\projectm-app

cmake --build cmake-build --config Release --parallel
cmake --install cmake-build --config Release
```

The manifest also installs libprojectM. If using a separately built
libprojectM, add its installation prefix through `CMAKE_PREFIX_PATH`.

The installed executable is normally:

```text
C:\projectm-app\projectMSDL.exe
```

Example:

```bat
C:\projectm-app\projectMSDL.exe ^
  --networkBindAddress=127.0.0.1 ^
  --enableVisualPostProcessing
```

Binding to a LAN address may trigger a Windows Firewall prompt. Do not expose
the unauthenticated API to untrusted networks.

## Bundling presets and textures

Provide preset and texture source directories when configuring:

```sh
cmake -S . -B cmake-build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DPRESET_DIRS="/path/to/presets" \
  -DTEXTURE_DIRS="/path/to/textures"
```

Use a semicolon-separated list for multiple directories. Installation copies
their contents into the platform's preset and texture locations.

Bundled preset paths are readable but not writable through the API. API-created
presets are stored below `network.presetWorkspace`.

## Tests

Configure with tests enabled:

```sh
cmake -S . -B cmake-build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_TESTING=ON

cmake --build cmake-build-release --parallel
ctest --test-dir cmake-build-release --output-on-failure
```

The network integration test opens an ephemeral loopback TCP port.

## Runtime configuration

The application creates a user configuration file in the platform config
directory. The most relevant fork-specific defaults are:

```properties
network.enabled = true
network.bindAddress = 0.0.0.0
network.port = 8080
network.presetWorkspace = ${system.configHomeDir}/projectM/presets
network.maxPresetBytes = 1048576
visual.postProcessingEnabled = false
```

Prefer `127.0.0.1` unless another machine must reach the API. See the
[network API reference](network-api.md) for endpoint documentation and the
full security warning.
