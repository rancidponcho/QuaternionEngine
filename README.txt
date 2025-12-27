QUATERNION ENGINE

A cross-platform C game framework using SDL3 and SDL_GPU.
Targets: Linux, Windows, macOS, Android, iOS.

PROJECT STRUCTURE

- CMakeLists.txt: The master build configuration. Handles platform detection and shader compilation.
- src/: Shared C source code (Engine lifecycle, Input, Game Logic).
- assets/shaders/: HLSL shader source files (.hlsl) and compiled binaries (.spv).
- android/: Android-specific project files (Gradle wrapper & Manifest).
- external/: Dependency storage (SDL3 is downloaded here automatically).
- Makefile: Convenience wrapper for Linux/macOS.
- build.bat: Convenience wrapper for Windows.

--------------------------------------------------------------------------------

PREREQUISITES

1. Linux (Debian/Ubuntu)
   System Tools:
       sudo apt install build-essential cmake git

   SDL3 Dependencies (Video/Audio/Input):
       sudo apt install libasound2-dev libpulse-dev libx11-dev libxext-dev \
       libxrandr-dev libxcursor-dev libxi-dev libxss-dev libwayland-dev \
       libxkbcommon-dev

   Shader Compiler (DXC):
       You must install the DirectX Shader Compiler ('dxc') to build shaders.
       Download the latest release from: https://github.com/microsoft/DirectXShaderCompiler/releases
       
       Installation (Example):
       1. Download the tarball (e.g., linux_dxc_...x86_64.tar.gz).
       2. Extract it to a folder (e.g., ~/tools/dxc).
       3. Add the /bin folder to your PATH in ~/.bashrc:
          export PATH=$PATH:~/path/to/dxc/bin

2. macOS
   Requires Xcode Command Line Tools:
       xcode-select --install

   Install CMake and Vulkan SDK (includes 'dxc') via Homebrew:
       brew install cmake
       brew install --cask vulkan-sdk

3. Windows
   - Install Visual Studio Community (Select "Desktop development with C++").
   - Install CMake (usually included in VS, or install separately).
   - Install the Vulkan SDK (https://vulkan.lunarg.com/). This includes 'dxc.exe'.
     Ensure 'dxc' is added to your System PATH during installation.

4. Android
   - Linux/Mac users: Ensure ANDROID_HOME and NDK_HOME environment variables
     are set in your shell (e.g., .bashrc).
   - Windows users: Install Android Studio and set the ANDROID_HOME env var.
   - Note: Android builds use the NDK's internal tools, but 'dxc' is still
     useful for verifying shaders locally.

--------------------------------------------------------------------------------

BUILDING & RUNNING

--- Linux & macOS (Terminal) ---
We use a Makefile to handle bootstrapping, shader compilation, and building.

Commands:
make
    Compiles shaders (HLSL -> SPIR-V) and the desktop executable (Debug).

make run
    Compiles and immediately runs the app.

make clean
    Removes the build/ directory and compiled shader binaries.

make nuke
    Hard Reset. Deletes build, Android caches, AND the SDL source.


--- Windows (Command Line) ---
Run the convenience script from cmd or PowerShell:
    build.bat

Alternatively: Open the project folder in Visual Studio 2022. It will
auto-detect CMake. Just press the Green Play button.

--------------------------------------------------------------------------------

MOBILE DEPLOYMENT

--- Android ---
The project uses the Gradle build system, wrapped by our Makefile.

One-Step Build (Linux/Mac):
    make android          (Builds Debug APK)
    make android-release  (Builds Release APK)

Manual Build:
    cd android
    ./gradlew assembleDebug

Where is the APK?
    android/app/build/outputs/apk/debug/app-debug.apk


--- iOS ---
iOS builds require generating an Xcode Project via CMake.

1. Create an iOS build folder:
   mkdir build_ios && cd build_ios

2. Generate the Xcode project:
   cmake .. -G Xcode -DCMAKE_SYSTEM_NAME=iOS

3. Open the project:
   open Quaternion.xcodeproj

4. Select your connected iPhone (or Simulator) in Xcode and hit Run.
