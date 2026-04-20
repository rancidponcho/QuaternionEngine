# ==============================================================================
# QUATERNION BUILD SYSTEM
# ==============================================================================
PROJECT_NAME := Quaternion
BUILD_DIR    := build
ANDROID_DIR  := android
SDL_SRC      := external/SDL

.PHONY: all clean clean-android bootstrap android desktop macos ios

# Default target
all: desktop

# ------------------------------------------------------------------------------
# DEPENDENCIES
# ------------------------------------------------------------------------------
bootstrap: $(SDL_SRC)/CMakeLists.txt $(ANDROID_DIR)/gradlew

# Fetch SDL3 only if missing
$(SDL_SRC)/CMakeLists.txt:
	@echo "--- Fetching SDL3 ---"
	git clone --depth 1 https://github.com/libsdl-org/SDL.git $(SDL_SRC)

# Setup Android wrapper only if missing
$(ANDROID_DIR)/gradlew:
	@echo "--- Setting up Android Gradle ---"
	cp $(SDL_SRC)/android-project/gradlew $(ANDROID_DIR)/
	cp $(SDL_SRC)/android-project/gradlew.bat $(ANDROID_DIR)/
	cp -r $(SDL_SRC)/android-project/gradle $(ANDROID_DIR)/
	chmod +x $(ANDROID_DIR)/gradlew

# ------------------------------------------------------------------------------
# DESKTOP (Linux / Windows)
# ------------------------------------------------------------------------------
desktop: bootstrap
	@mkdir -p $(BUILD_DIR)
	@# Only run CMake Config if cache is missing (Speeds up re-builds)
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		echo "--- Configuring Desktop ---"; \
		cd $(BUILD_DIR) && cmake ..; \
	fi
	@echo "--- Building Desktop ---"
	cd $(BUILD_DIR) && cmake --build .

# ------------------------------------------------------------------------------
# MACOS (Xcode)
# ------------------------------------------------------------------------------
macos: bootstrap
	@mkdir -p build_macos
	@# Check for .xcodeproj to avoid re-configuring every single run
	@if [ ! -d build_macos/$(PROJECT_NAME).xcodeproj ]; then \
		echo "--- Configuring macOS Project ---"; \
		cd build_macos && cmake -G Xcode .. -DCMAKE_SYSTEM_NAME=Darwin; \
	fi
	@echo "--- Building macOS App ---"
	cd build_macos && cmake --build . --config Debug

# ------------------------------------------------------------------------------
# IOS (Device + Signing)
# ------------------------------------------------------------------------------
ios: bootstrap
	@mkdir -p build_ios
	@# Only configure if project is missing. PREVENTS SLOW REBUILDS.
	@if [ ! -d build_ios/$(PROJECT_NAME).xcodeproj ]; then \
		echo "--- Configuring iOS Project ---"; \
		cd build_ios && cmake -G Xcode .. \
			-DCMAKE_SYSTEM_NAME=iOS \
			-DCMAKE_OSX_SYSROOT=iphoneos \
			-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0; \
	fi
	@echo "--- Building iOS Device (Automatic Signing) ---"
	cd build_ios && cmake --build . --config Debug -- -allowProvisioningUpdates

# ------------------------------------------------------------------------------
# ANDROID (Gradle)
# ------------------------------------------------------------------------------
android:
	@echo "--- Building Android APK ---"
	cd $(ANDROID_DIR) && ./gradlew assembleDebug

# ------------------------------------------------------------------------------
# RELEASE (Optimized Desktop Build)
# ------------------------------------------------------------------------------
release: bootstrap
	@mkdir -p $(BUILD_DIR)_release
	@if [ ! -f $(BUILD_DIR)_release/CMakeCache.txt ]; then \
		echo "--- Configuring Release ---"; \
		cd $(BUILD_DIR)_release && cmake -DCMAKE_BUILD_TYPE=Release ..; \
	fi
	@echo "--- Building Release ---"
	cd $(BUILD_DIR)_release && cmake --build . --config Release

# ------------------------------------------------------------------------------
# UTILITIES
# ------------------------------------------------------------------------------
# Nuke everything to force a full re-config
clean:
	rm -rf $(BUILD_DIR) build_macos build_ios
	@echo "Build directories removed."

clean-android:
	cd $(ANDROID_DIR) && ./gradlew clean
