cmake_minimum_required(VERSION 3.22.1)
project(Quaternion C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# 1. GLOBAL CONFIGURATION
# ==============================================================================

# [cite_start]Android: Enforce 16KB page size for Android 15+ compliance [cite: 5]
if(ANDROID)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
endif()

# [cite_start]SDL3: Static linking on iOS prevents "Library not loaded" runtime crashes [cite: 5]
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED ON  CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
endif()

set(SDL_TEST OFF CACHE BOOL "" FORCE)

# Dependency: SDL3 (FetchContent)
include(FetchContent)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
    SOURCE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL"
)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL/CMakeLists.txt")
    message(STATUS "Initializing SDL3 Submodule...")
    FetchContent_Populate(SDL3)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/SDL ${CMAKE_CURRENT_BINARY_DIR}/sdl_build EXCLUDE_FROM_ALL)

# ==============================================================================
# 2. TARGET DEFINITION
# ==============================================================================
file(GLOB_RECURSE SOURCE_FILES "src/*.c")

# ------------------------------------------------------------------------------
# Platform: Android
# ------------------------------------------------------------------------------
if(ANDROID)
    add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "main")
    
    # [cite_start]Link: JNI / Shared SDL [cite: 6]
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3-shared log)

# ------------------------------------------------------------------------------
# Platform: Apple (macOS / iOS)
# ------------------------------------------------------------------------------
elseif(APPLE)
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCE_FILES})

    # [cite_start]Signing: resolve identity via environment or fallback [cite: 7, 8]
    set(MY_TEAM_ID $ENV{APPLE_TEAM_ID})
    
    if(NOT MY_TEAM_ID)
        message(FATAL_ERROR "Build Failed: APPLE_TEAM_ID env var missing. Export it or define in CMakeLists.txt.")
    endif()

    # [cite_start]Bundle Config: Identifier & Team Identity [cite: 8]
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.rancidponcho.quaternion"
        MACOSX_BUNDLE_BUNDLE_NAME   "Quaternion"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.rancidponcho.quaternion"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${MY_TEAM_ID}
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist"
    )

    # [cite_start]Sync SDL3 signing team to match main target [cite: 9]
    if(TARGET SDL3-shared)
        set_target_properties(SDL3-shared PROPERTIES XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${MY_TEAM_ID})
    endif()

    # Link: Metal & SDL
    find_library(METAL_LIB Metal)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3 ${METAL_LIB})

    # Sub-Platform: iOS
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        # [cite_start]Asset: Launch Screen (Enables full-screen context) [cite: 10]
        set(LAUNCH_SCREEN_STORYBOARD "ios/LaunchScreen.storyboard")
        target_sources(${PROJECT_NAME} PRIVATE ${LAUNCH_SCREEN_STORYBOARD})
        set_source_files_properties(${LAUNCH_SCREEN_STORYBOARD} PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources
        )
        
        # 3. Register it in Info.plist so iOS looks for "LaunchScreen"
        set_target_properties(${PROJECT_NAME} PROPERTIES
            XCODE_ATTRIBUTE_INFOPLIST_KEY_UILaunchStoryboardName "LaunchScreen"
        )
        # [cite_start]Link: iOS System Frameworks [cite: 11]
        find_library(UIKIT_LIB UIKit)
        find_library(FOUNDATION_LIB Foundation)
        find_library(QUARTZ_LIB QuartzCore)
        
        target_link_libraries(${PROJECT_NAME} PRIVATE ${UIKIT_LIB} ${FOUNDATION_LIB} ${QUARTZ_LIB})
    
    # Sub-Platform: macOS
    else()
        find_library(COCOA_LIB Cocoa)
        find_library(IOKIT_LIB IOKit)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${COCOA_LIB} ${IOKIT_LIB})
    endif()

# ------------------------------------------------------------------------------
# Platform: Desktop (Windows / Linux)
# ------------------------------------------------------------------------------
else()
    add_executable(${PROJECT_NAME} ${SOURCE_FILES})
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)

    # [cite_start]Windows: DLL Management [cite: 12]
    if(WIN32)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Deploying SDL3.dll"
        )
    endif()

    # [cite_start]Linux: Math Library [cite: 13]
    if(UNIX AND NOT APPLE)
        target_link_libraries(${PROJECT_NAME} PRIVATE m)
    endif()

    # [cite_start]Shader Deployment (Non-Bundle Systems) [cite: 13, 14, 15]
    if(WIN32 OR (UNIX AND NOT APPLE))
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/BasicCompute.spv" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders/BasicCompute.spv"
            COMMENT "Deploying Shaders"
        )
    endif()
endif()

# Includes: Global visibility for "src" (Required for cross-file headers)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ==============================================================================
# 3. SHADER PIPELINE
# ==============================================================================
find_program(DXC_EXECUTABLE dxc)
find_program(SPIRV_CROSS_EXECUTABLE spirv-cross)
find_program(XCRUN_EXECUTABLE xcrun)

# Routine: compile_shader
# Purpose: Transpiles HLSL -> SPIR-V (All) -> Metal (Apple)
function(compile_shader SOURCE SPV_OUT PROFILE ENTRY)
    if(NOT DXC_EXECUTABLE)
        return()
    endif()

    # [cite_start]Stage 1: Vulkan SPIR-V Generation [cite: 16, 17]
    add_custom_command(
        OUTPUT  ${SPV_OUT}
        COMMAND ${DXC_EXECUTABLE} -T ${PROFILE} -E ${ENTRY} -spirv -fspv-target-env=vulkan1.0 -Fo ${SPV_OUT} ${SOURCE}
        DEPENDS ${SOURCE}
        COMMENT "Compiling HLSL -> SPIR-V: ${SOURCE}"
    )

    # [cite_start]Stage 2: Metal Translation (Apple Only) [cite: 18, 19]
    if(APPLE AND SPIRV_CROSS_EXECUTABLE AND XCRUN_EXECUTABLE)
        get_filename_component(BASENAME ${SOURCE} NAME_WE)
        set(MSL_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${BASENAME}.metal")
        set(AIR_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${BASENAME}.air")
        set(LIB_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${BASENAME}.metallib")
        
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")

        # [cite_start]SDK Selection Logic [cite: 20]
        if(CMAKE_OSX_SYSROOT)
            set(METAL_SDK ${CMAKE_OSX_SYSROOT})
        elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
            set(METAL_SDK "iphoneos")
        else()
            set(METAL_SDK "macosx")
        endif()

        add_custom_command(
            OUTPUT  ${LIB_OUT}
            COMMAND ${SPIRV_CROSS_EXECUTABLE} ${SPV_OUT} --msl --output ${MSL_OUT}
            COMMAND ${XCRUN_EXECUTABLE} -sdk ${METAL_SDK} metal -c ${MSL_OUT} -o ${AIR_OUT}
            COMMAND ${XCRUN_EXECUTABLE} -sdk ${METAL_SDK} metallib ${AIR_OUT} -o ${LIB_OUT}
            DEPENDS ${SPV_OUT}
            COMMENT "Building Metal Library: ${LIB_OUT}"
        )

        target_sources(${PROJECT_NAME} PRIVATE ${LIB_OUT})
        set_source_files_properties(${LIB_OUT} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/shaders)
    
    else()
        # [cite_start]Ensure non-Apple builds track shader dependencies [cite: 23]
        get_filename_component(SH_TARGET ${SOURCE} NAME_WE)
        add_custom_target(Shader_${SH_TARGET} DEPENDS ${SPV_OUT})
        add_dependencies(${PROJECT_NAME} Shader_${SH_TARGET})
    endif()
endfunction()

# [cite_start]Asset: BasicCompute.hlsl [cite: 24]
compile_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/BasicCompute.hlsl" 
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/BasicCompute.spv" 
    cs_6_0 
    main
)
