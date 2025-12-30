# --- Configuration ---
PROJECT_NAME := Quaternion
BUILD_DIR := build
ANDROID_DIR := android
SDL_SRC := external/SDL

# Detect OS for Desktop Run
UNAME_S := $(shell uname -s)

# --- Main Targets ---

.PHONY: all clean clean-android bootstrap android desktop run

# Default target: Build Desktop
all: desktop

# 1. Bootstrap: Downloads SDL3 and sets up Android Gradle wrappers
bootstrap: $(SDL_SRC)/CMakeLists.txt $(ANDROID_DIR)/gradlew

# Logic to fetch SDL if missing
$(SDL_SRC)/CMakeLists.txt:
	@echo "--- Bootstrapping SDL3 ---"
	cmake -S . -B build_temp
	rm -rf build_temp
	@echo "--- SDL3 Downloaded ---"

# Logic to copy Gradle wrapper from SDL to our Android folder
$(ANDROID_DIR)/gradlew: $(SDL_SRC)/CMakeLists.txt
	@echo "--- Setting up Android Gradle Wrapper ---"
	cp $(SDL_SRC)/android-project/gradlew $(ANDROID_DIR)/
	cp $(SDL_SRC)/android-project/gradlew.bat $(ANDROID_DIR)/
	cp -r $(SDL_SRC)/android-project/gradle $(ANDROID_DIR)/
	chmod +x $(ANDROID_DIR)/gradlew

# 2. Desktop Build (Linux/Mac/Win)
desktop: bootstrap
	@echo "--- Building for Desktop ---"
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake .. && make

run: desktop
	@echo "--- Running Desktop App ---"
	$(BUILD_DIR)/${PROJECT_NAME}

# 3. Android Build
android:
	@echo "--- Building Android APK (Debug) ---"
	cd $(ANDROID_DIR) && ./gradlew assembleDebug

android-release: bootstrap
	@echo "--- Building Android APK (Release) ---"
	cd $(ANDROID_DIR) && ./gradlew assembleRelease

# 4. Clean Up
clean:
	@echo "--- Cleaning Desktop Build ---"
	rm -rf $(BUILD_DIR)
	rm -rf build_temp

clean-android:
	@echo "--- Cleaning Android Build ---"
	cd $(ANDROID_DIR) && ./gradlew clean

nuke: clean clean-android
	@echo "--- Nuking External Dependencies (SDL) ---"
	rm -rf external/
	# Remove the copied gradle wrapper files to reset completely
	rm -f $(ANDROID_DIR)/gradlew $(ANDROID_DIR)/gradlew.bat
	rm -rf $(ANDROID_DIR)/gradle
