@echo off
REM --- Windows Convenience Wrapper ---

REM 1. Bootstrap SDL if missing
if not exist "external\SDL\CMakeLists.txt" (
    echo [Batch] Bootstrapping SDL3...
    cmake -S . -B build_temp
    rmdir /s /q build_temp
)

REM 2. Create build folder
if not exist "build" mkdir build

REM 3. Run CMake and Compile
cd build
cmake ..
cmake --build . --config Debug

REM 4. Run the app
echo [Batch] Running Application...
Debug\main.exe
cd ..
