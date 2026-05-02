cmake_minimum_required(VERSION 3.22.1)
project(Quaternion C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# 1. GLOBAL CONFIGURATION
# ==============================================================================

if(ANDROID)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED ON  CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
endif()

set(SDL_TEST OFF CACHE BOOL "" FORCE)

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

set(SOURCE_FILES
    "src/main.c"
    "src/app/app.c"
    "src/core/engine.c"
    "src/core/input.c"
    "src/core/hotload.c"
    "src/render/renderer.c"
    "src/assets/assets.c"
    "src/assets/font.c"
    "src/ui/ui.c"
)

# Shader outputs live in the build tree, not the source tree.
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

set(BASIC_SPV "${SHADER_OUTPUT_DIR}/BasicCompute.spv")
set(TEXT_SPV  "${SHADER_OUTPUT_DIR}/TextOverlayCompute.spv")

if(ANDROID)
    add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "main")
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3-shared log)

elseif(APPLE)
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCE_FILES})

    set(MY_TEAM_ID $ENV{APPLE_TEAM_ID})
    if(NOT MY_TEAM_ID)
        message(FATAL_ERROR "Build Failed: APPLE_TEAM_ID env var missing. Export it or define in CMakeLists.txt.")
    endif()

    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.rancidponcho.quaternion"
        MACOSX_BUNDLE_BUNDLE_NAME   "Quaternion"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.rancidponcho.quaternion"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${MY_TEAM_ID}
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist"
    )

    if(TARGET SDL3-shared)
        set_target_properties(SDL3-shared PROPERTIES XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${MY_TEAM_ID})
    endif()

    find_library(METAL_LIB Metal)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3 ${METAL_LIB})

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(LAUNCH_SCREEN_STORYBOARD "ios/LaunchScreen.storyboard")
        target_sources(${PROJECT_NAME} PRIVATE ${LAUNCH_SCREEN_STORYBOARD})
        set_source_files_properties(${LAUNCH_SCREEN_STORYBOARD} PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources
        )

        set_target_properties(${PROJECT_NAME} PROPERTIES
            XCODE_ATTRIBUTE_INFOPLIST_KEY_UILaunchStoryboardName "LaunchScreen"
        )

        find_library(UIKIT_LIB UIKit)
        find_library(FOUNDATION_LIB Foundation)
        find_library(QUARTZ_LIB QuartzCore)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${UIKIT_LIB} ${FOUNDATION_LIB} ${QUARTZ_LIB})
    else()
        find_library(COCOA_LIB Cocoa)
        find_library(IOKIT_LIB IOKit)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${COCOA_LIB} ${IOKIT_LIB})
    endif()

else()
    add_executable(${PROJECT_NAME} ${SOURCE_FILES})
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)

    if(WIN32)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3>
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Deploying SDL3.dll"
        )
    endif()

    if(UNIX AND NOT APPLE)
        target_link_libraries(${PROJECT_NAME} PRIVATE m)
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ==============================================================================
# 3. SHADER PIPELINE
# ==============================================================================

find_program(DXC_EXECUTABLE dxc)
find_program(SPIRV_CROSS_EXECUTABLE spirv-cross)
find_program(XCRUN_EXECUTABLE xcrun)

function(compile_shader SOURCE SPV_OUT PROFILE ENTRY)
    if(NOT DXC_EXECUTABLE)
        return()
    endif()

    add_custom_command(
        OUTPUT  ${SPV_OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUTPUT_DIR}"
        COMMAND ${DXC_EXECUTABLE}
            -T ${PROFILE}
            -E ${ENTRY}
            -spirv
            -fspv-target-env=vulkan1.0
            -Fo ${SPV_OUT}
            ${SOURCE}
        DEPENDS ${SOURCE}
        COMMENT "Compiling HLSL -> SPIR-V: ${SOURCE}"
    )

    if(APPLE AND SPIRV_CROSS_EXECUTABLE AND XCRUN_EXECUTABLE)
        get_filename_component(BASENAME ${SOURCE} NAME_WE)
        set(MSL_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${BASENAME}.metal")
        set(AIR_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${BASENAME}.air")
        set(LIB_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${BASENAME}.metallib")

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
        get_filename_component(SH_TARGET ${SOURCE} NAME_WE)
        add_custom_target(Shader_${SH_TARGET} DEPENDS ${SPV_OUT})
    endif()
endfunction()

compile_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/BasicCompute.hlsl"
    "${BASIC_SPV}"
    cs_6_0
    main
)

compile_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/TextOverlayCompute.hlsl"
    "${TEXT_SPV}"
    cs_6_0
    main
)

# ==============================================================================
# 4. SHADER DEPLOYMENT
# ==============================================================================

if(NOT APPLE)
    add_custom_target(DeployShaders ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BASIC_SPV}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/shaders/BasicCompute.spv"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TEXT_SPV}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/shaders/TextOverlayCompute.spv"
        DEPENDS
            "${BASIC_SPV}"
            "${TEXT_SPV}"
        COMMENT "Deploying shaders"
    )

    add_dependencies(${PROJECT_NAME}
        DeployShaders
        Shader_BasicCompute
        Shader_TextOverlayCompute
    )
endif()
