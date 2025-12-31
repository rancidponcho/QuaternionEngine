# --- Configuration ---
PROJECT_NAME := Quaternion
BUILD_DIR := build
ANDROID_DIR := android
SDL_SRC := external/SDL

# --- Targets ---
.PHONY: all clean clean-android bootstrap android desktop macos ios

# Default
all: desktop

bootstrap: $(SDL_SRC)/CMakeLists.txt $(ANDROID_DIR)/gradlew

$(SDL_SRC)/CMakeLists.txt:
	@echo "--- Fetching SDL3 ---"
	git clone --depth 1 https://github.com/libsdl-org/SDL.git $(SDL_SRC)

$(ANDROID_DIR)/gradlew:
	@echo "--- Setting up Android Gradle ---"
	cp $(SDL_SRC)/android-project/gradlew $(ANDROID_DIR)/
	cp $(SDL_SRC)/android-project/gradlew.bat $(ANDROID_DIR)/
	cp -r $(SDL_SRC)/android-project/gradle $(ANDROID_DIR)/
	chmod +x $(ANDROID_DIR)/gradlew

# --- Desktop (Linux/Win) ---
desktop: bootstrap
	@echo "--- Building Desktop ---"
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake ..
	cd $(BUILD_DIR) && cmake --build .

# --- macOS (Xcode Project) ---
macos: bootstrap
	@echo "--- Generating macOS Project ---"
	mkdir -p build_macos
	cd build_macos && cmake -G Xcode .. -DCMAKE_SYSTEM_NAME=Darwin
	@echo "--- Opening Xcode ---"
	open build_macos/Quaternion.xcodeproj

# --- iOS (Xcode Project) ---
ios: bootstrap
	@echo "--- Generating iOS Project ---"
	mkdir -p build_ios
	cd build_ios && cmake -G Xcode .. -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
	@echo "--- Opening Xcode ---"
	open build_ios/Quaternion.xcodeproj

# --- Android ---
android:
	cd $(ANDROID_DIR) && ./gradlew assembleDebug

# --- Clean ---
clean:
	rm -rf $(BUILD_DIR) build_macos build_ios

clean-android:
	cd $(ANDROID_DIR) && ./gradlew clean
