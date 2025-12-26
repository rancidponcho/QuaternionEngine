cmake_minimum_required(VERSION 3.22.1)

# STRICT C Support
project(Quaternion C)

# ------------------------------------------------------------------------------
# 1. Configure SDL Options BEFORE downloading
# ------------------------------------------------------------------------------
# We force these settings so we know exactly what targets get created
set(SDL_SHARED ON CACHE BOOL "" FORCE) 
set(SDL_STATIC ON CACHE BOOL "" FORCE) # <--- This fixes your error
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
set(SOURCE_FILES src/main.c)

if(ANDROID)
    # --- ANDROID MODE ---
    add_library(main SHARED ${SOURCE_FILES})
    
    # Android MUST use the shared version of SDL
    target_link_libraries(main PRIVATE SDL3::SDL3-shared)
    target_link_libraries(main PRIVATE log)

else()
    # --- DESKTOP (Linux/Win/Mac) MODE ---
    add_executable(main ${SOURCE_FILES})
    
    # Use the generic "SDL3::SDL3" alias. 
    # Since we set SDL_STATIC=ON above, this will work nicely.
    target_link_libraries(main PRIVATE SDL3::SDL3)
    
    # Link Math library (essential for Linux C dev)
    if(UNIX AND NOT APPLE)
        target_link_libraries(main PRIVATE m)
    endif()

endif()
