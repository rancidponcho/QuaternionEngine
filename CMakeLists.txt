cmake_minimum_required(VERSION 3.22.1)
project(Quaternion C)

# ------------------------------------------------------------------------------
# 0. Global Configuration
# ------------------------------------------------------------------------------
if(ANDROID)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
endif()

# ------------------------------------------------------------------------------
# 1. Fetch SDL3
# ------------------------------------------------------------------------------
set(SDL_SHARED ON CACHE BOOL "" FORCE) 
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
    SOURCE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL"
)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL/CMakeLists.txt")
    message(STATUS "Downloading SDL3...")
    FetchContent_Populate(SDL3)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/SDL ${CMAKE_CURRENT_BINARY_DIR}/sdl_build EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------
# 2. Define the Target
# ------------------------------------------------------------------------------
file(GLOB_RECURSE SOURCE_FILES "src/*.c")

if(ANDROID)
    # --- ANDROID ---
    add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "main")
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3-shared log)

elseif(APPLE)
    # --- MACOS / iOS ---
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCE_FILES})
    
    # Configure Bundle Properties (Required for Signing)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.rancidponcho.quaternion"
        MACOSX_BUNDLE_BUNDLE_NAME "Quaternion"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.rancidponcho.quaternion"
        # XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "YOUR_TEAM_ID" # Uncomment for physical iOS device
    )

    find_library(METAL_LIB Metal)
    find_library(COCOA_LIB Cocoa) # macOS only, ignored on iOS
    find_library(IOKIT_LIB IOKit)
    
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3 ${METAL_LIB} ${COCOA_LIB} ${IOKIT_LIB})

else()
    # --- LINUX / WINDOWS ---
    add_executable(${PROJECT_NAME} ${SOURCE_FILES})
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)
    
    if(WIN32)
        # 1. Copy SDL3.dll
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMENT "Copying SDL3.dll..."
        )

        # 2. Copy Shaders (Make the directory first, then copy the file)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/BasicCompute.spv"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders/BasicCompute.spv"
            COMMENT "Copying Shaders..."
        )
    endif()

    if(UNIX)
        target_link_libraries(${PROJECT_NAME} PRIVATE m)
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ------------------------------------------------------------------------------
# 3. Shader Compilation (The Robust Pipeline)
# ------------------------------------------------------------------------------
find_program(DXC_EXECUTABLE dxc)
find_program(SPIRV_CROSS_EXECUTABLE spirv-cross)
find_program(XCRUN_EXECUTABLE xcrun)

# Function: Compile HLSL -> SPV -> (Optional) Metal
# Params:
#   SOURCE:   Path to .hlsl file
#   SPV_OUT:  Path where .spv should be saved (usually assets/shaders/X.spv)
#   PROFILE:  cs_6_0, etc.
#   ENTRY:    main
function(compile_shader SOURCE SPV_OUT PROFILE ENTRY)
    if(DXC_EXECUTABLE)
        
        # 1. ALWAYS compile HLSL -> SPIR-V
        # This ensures Android/PC assets are always up to date.
        add_custom_command(
            OUTPUT ${SPV_OUT}
            COMMAND ${DXC_EXECUTABLE} -T ${PROFILE} -E ${ENTRY} -spirv -fspv-target-env=vulkan1.0 -Fo ${SPV_OUT} ${SOURCE}
            DEPENDS ${SOURCE}
            COMMENT "Compiling HLSL -> SPIR-V: ${SOURCE}"
        )

        # 2. APPLE ONLY: Convert SPIR-V -> MSL -> AIR -> Metallib
        if(APPLE AND SPIRV_CROSS_EXECUTABLE AND XCRUN_EXECUTABLE)
            
            # We build Metal artifacts in the BUILD directory, not source assets.
            # This keeps your source tree clean.
            get_filename_component(BASENAME ${SOURCE} NAME_WE)
            set(MSL_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${BASENAME}.metal")
            set(AIR_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${BASENAME}.air")
            set(LIB_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${BASENAME}.metallib")
            
            file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")

	    # Detect which SDK to use (iphoneos, iphonesimulator, or macosx)
	    if(CMAKE_OSX_SYSROOT)
    	        set(METAL_SDK ${CMAKE_OSX_SYSROOT})
	    else()
    	    	set(METAL_SDK "macosx")
	    endif()

	    add_custom_command(
    	    OUTPUT ${LIB_OUT}
    	    # Step A: SPIR-V -> MSL
    	    COMMAND ${SPIRV_CROSS_EXECUTABLE} ${SPV_OUT} --msl --output ${MSL_OUT}
    
  	    	# Step B: Compile (-c) using the CORRECT SDK
    	    COMMAND ${XCRUN_EXECUTABLE} -sdk ${METAL_SDK} metal -c ${MSL_OUT} -o ${AIR_OUT}
    
    	    # Step C: Link (metallib) using the CORRECT SDK
            COMMAND ${XCRUN_EXECUTABLE} -sdk ${METAL_SDK} metallib ${AIR_OUT} -o ${LIB_OUT}
    
	    	DEPENDS ${SPV_OUT}
       	    COMMENT "Building Metal Library (${METAL_SDK}): ${LIB_OUT}"
	    )

            # Add to Bundle Resources so it copies into Quaternion.app/Contents/Resources/
            target_sources(${PROJECT_NAME} PRIVATE ${LIB_OUT})
            set_source_files_properties(${LIB_OUT} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/shaders)
        
        else()
            # NON-APPLE: Ensure the target depends on the SPV generation
            get_filename_component(SH_TARGET ${SOURCE} NAME_WE)
            add_custom_target(Shader_${SH_TARGET} DEPENDS ${SPV_OUT})
            add_dependencies(${PROJECT_NAME} Shader_${SH_TARGET})
        endif()
    endif()
endfunction()

# --- Register Shaders ---
# Note: We output .spv to assets/shaders so Android Gradle picks it up.
compile_shader(
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/BasicCompute.hlsl" 
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/BasicCompute.spv" 
    cs_6_0 
    main
)
