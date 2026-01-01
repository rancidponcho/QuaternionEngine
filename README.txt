================================================================================
QUATERNION ENGINE                                                    v0.1-init
================================================================================

A cross-platform high-performance game framework.
Written in C. Powered by SDL3 and SDL_GPU.

Supported Targets:
    * Linux (X11/Wayland)
    * macOS (Metal)
    * Windows (D3D12/Vulkan)
    * iOS (Metal)
    * Android (Vulkan)

================================================================================
DIRECTORY LAYOUT
================================================================================

/src
    /core       - Lifecycle, Input, Hardware Abstraction (HAL)
    /render     - Shader pipeline, Compute Dispatch, Resolution Scaling
/assets
    /shaders    - HLSL source (.hlsl) and compiled bytecode (.spv)
/external       - SDL3 (fetched automatically via CMake)
/ios            - iOS-specific resources (Storyboards, Info.plist)
/android        - Gradle build system and Manifests

Files:
CMakeLists.txt  - Master build configuration
Makefile        - *nix build automation wrapper
build.bat       - Windows build automation wrapper

================================================================================
PREREQUISITES
================================================================================

1. COMPILER
   - GCC / Clang (Linux/macOS)
   - MSVC (Windows)
   - CMake 3.22+

2. SHADER COMPILER
   - Microsoft DirectX Shader Compiler ('dxc')
   - REQUIRED for all platforms (compiles HLSL -> SPIR-V).
   - Linux: Download release binary, add to PATH.
   - macOS: `brew install vulkan-sdk`
   - Windows: Included in Vulkan SDK.

3. LIBRARIES (Linux Only)
   - SDL3 build dependencies:
     build-essential libasound2-dev libpulse-dev libx11-dev libxext-dev
     libxrandr-dev libxcursor-dev libxi-dev libxss-dev libwayland-dev
     libxkbcommon-dev

================================================================================
BUILDING
================================================================================

All build artifacts are output to ./build/ (or ./build_ios/, ./build_android/).

--------------------------------------------------------------------------------
LINUX / MACOS
--------------------------------------------------------------------------------
    make            # Compile Debug build
    make run        # Compile and Run
    make clean      # Clean build directory

--------------------------------------------------------------------------------
WINDOWS
--------------------------------------------------------------------------------
    build.bat       # Compile and Run

    Alternatively, open folder in Visual Studio 2022.

--------------------------------------------------------------------------------
IOS
--------------------------------------------------------------------------------
    make ios        # Generate Xcode Project and build Debug .app

    To install on device:
    1. open build_ios/Quaternion.xcodeproj
    2. Select device.
    3. Cmd+R

    NOTE: Requires valid Apple Development Team ID.
    Set via env var: export APPLE_TEAM_ID="XXXXXXXX"
    Or edit CMakeLists.txt directly.

--------------------------------------------------------------------------------
ANDROID
--------------------------------------------------------------------------------
    make android    # Build Debug APK

    APK Location:
    android/app/build/outputs/apk/debug/app-debug.apk

================================================================================
NOTES
================================================================================

* PAGE SIZES: Android builds enforce 16KB page alignment for Android 15+
  compatibility.

* SHADERS: Engine uses a unified HLSL shader path.
  - PC/Android: Compiled to SPIR-V.
  - Apple: Transpiled SPIR-V -> MSL -> Metallib via SPIRV-Cross.

* INPUT: Input system uses raw polling for low latency. Events are
  processed before the render dispatch.
