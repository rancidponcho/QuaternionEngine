cmake_minimum_required(VERSION 3.22.1)

project(Quaternion C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------------------------
# 1. Configure SDL Options BEFORE downloading
# ------------------------------------------------------------------------------
# Force these settings to know exactly what targets get created
set(SDL_SHARED ON CACHE BOOL "" FORCE) 
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)

# ------------------------------------------------------------------------------
# 2. Fetch SDL3
# ------------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
    # Force download to "external/SDL" so Gradle can find the Java files later
    SOURCE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL"
)

# Check if SDL is already downloaded to save time
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL/CMakeLists.txt")
    message(STATUS "Downloading SDL3... (This may take a moment)")
    FetchContent_Populate(SDL3)
endif()

# Make SDL available to the build
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/SDL ${CMAKE_CURRENT_BINARY_DIR}/sdl_build EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------
# 3. Define the Target
# ------------------------------------------------------------------------------
include_directories(src)

file(GLOB_RECURSE SOURCE_FILES "src/*.c")

if(ANDROID)
    # --- ANDROID MODE ---
    add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})
    
    # Android MUST use the shared version of SDL
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3-shared)
    target_link_libraries(${PROJECT_NAME} PRIVATE log)

else()
    # --- DESKTOP (Linux/Win/Mac) MODE ---
    add_executable(${PROJECT_NAME} ${SOURCE_FILES})
    
    # Use the generic "SDL3::SDL3" alias. 
    # Since we set SDL_STATIC=ON above, this will work nicely.
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)
    
    # Link Math library (essential for Linux C dev)
    if(UNIX AND NOT APPLE)
        target_link_libraries(${PROJECT_NAME} PRIVATE m)
    endif()

    if(WIN32)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3-shared>
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Copying SDL3.dll to executable directory..."
        )
    endif()

endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ------------------------------------------------------------------------------
# 4. Shader Compilation (HLSL -> SPIR-V)
# ------------------------------------------------------------------------------

# Find the DXC or SPIRV-CROSS
# Note: On Windows, this often finds the one inside the Vulkan SDK.
find_program(DXC_EXECUTABLE dxc)
if(NOT DXC_EXECUTABLE)
    message(WARNING "DXC not found! Shaders will not be recompiled. Install the Vulkan SDK.")
else()
    message(STATUS "Found DXC: ${DXC_EXECUTABLE}")
endif()

find_program(SPIRV_CROSS_EXECUTABLE spirv-cross)
if(NOT SPIRV_CROSS_EXECUTABLE)
    message(WARNING "SPIRV-CROSS not found! Shaders will not be recompiled. Install the Vulkan SDK.")
else()
    message(STATUS "Found SPIRV-CROSS: ${SPIRV_CROSS_EXECUTABLE}")
endif()

# Helper function to compile shaders
function(compile_shader SHADER_SOURCE SHADER_OUTPUT PROFILE ENTRYPOINT)
    if(DXC_EXECUTABLE)
        get_filename_component(OUT_DIR ${SHADER_OUTPUT} DIRECTORY)
        file(MAKE_DIRECTORY ${OUT_DIR})

        # 1. Always compile to SPIR-V (Everyone needs this as the base)
        # Output to a temp file or the final destination depending on platform
        set(SPIRV_BIN ${SHADER_OUTPUT})
        
        add_custom_command(
            OUTPUT ${SPIRV_BIN}
            COMMAND ${DXC_EXECUTABLE} -T ${PROFILE} -E ${ENTRYPOINT} -spirv -Fo ${SPIRV_BIN} ${SHADER_SOURCE}
            DEPENDS ${SHADER_SOURCE}
            COMMENT "Compiling ${SHADER_SOURCE} -> SPIR-V"
            VERBATIM
        )

        # 2. If on Apple, Translate SPIR-V -> Metal (MSL)
        if(APPLE)
            if(SPIRV_CROSS_EXECUTABLE)
                add_custom_command(
                    OUTPUT ${SHADER_OUTPUT}
                    COMMAND ${SPIRV_CROSS_EXECUTABLE} ${SPIRV_BIN} --msl --output ${SHADER_OUTPUT}
                    DEPENDS ${SPIRV_BIN}
                    COMMENT "Translating SPIR-V -> Metal (MSL)"
                )
                # Helper target to ensure MSL is built
                set(FINAL_OUTPUT ${SHADER_OUTPUT})
            else()
                message(WARNING "spirv-cross not found! iOS/Mac shaders will not be generated.")
            endif()
        else()
            # On Linux/Windows/Android, the SPIR-V is the final output
            set(FINAL_OUTPUT ${SPIRV_BIN})
        endif()

        # 3. Create a target for this specific shader so the main app can depend on it
        get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME_WE)
        add_custom_target(Shader_${SHADER_NAME} DEPENDS ${FINAL_OUTPUT})
        add_dependencies(${PROJECT_NAME} Shader_${SHADER_NAME})

    endif()
endfunction()

# Register shaders
# Syntax: compile_shader(SourceFile OutputFile Profile EntryPoint)
set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")

# Shader 1: Basic Compute
set(COMPUTE_SHADER_SRC "${SHADER_DIR}/BasicCompute.hlsl")
set(COMPUTE_SHADER_BIN "${SHADER_DIR}/BasicCompute.spv")
compile_shader(${COMPUTE_SHADER_SRC} ${COMPUTE_SHADER_BIN} cs_6_0 main)
