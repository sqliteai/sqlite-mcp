#
#  Makefile
#  sqlitemcp
#
#  Created by Gioele Cantoni on 05/11/25.
#

# Supports compilation for Linux, macOS, Windows, Android and iOS

# Customize sqlite3 executable with
# make test SQLITE3=/opt/homebrew/Cellar/sqlite/3.49.1/bin/sqlite3
SQLITE3 ?= sqlite3

# Set default platform if not specified
ifeq ($(OS),Windows_NT)
	PLATFORM := windows
	HOST := windows
	CPUS := $(shell powershell -Command "[Environment]::ProcessorCount")
else
	HOST = $(shell uname -s | tr '[:upper:]' '[:lower:]')
	ifeq ($(HOST),darwin)
		PLATFORM := macos
		CPUS := $(shell sysctl -n hw.ncpu)
	else
		PLATFORM := $(HOST)
		CPUS := $(shell nproc)
	endif
endif

# Speed up builds by using all available CPU cores
MAKEFLAGS += -j$(CPUS)

# Compiler and flags
CC = gcc
CARGO_ENV = CARGO_TARGET_DIR=$(RUST_TARGET_DIR) RUSTFLAGS="$(RUSTFLAGS)"
ifeq ($(PLATFORM),android)
	OPENSSL_INSTALL_DIR = $(BUILD_DIR)/openssl/$(PLATFORM)/$(ARCH)
	CARGO_ENV += OPENSSL_DIR=$(CURDIR)/$(OPENSSL_INSTALL_DIR)
endif
CARGO = $(CARGO_ENV) cargo
CFLAGS = -Wall -Wextra -Wno-unused-parameter -I$(SRC_DIR) -I$(LIBS_DIR) -Os -ffunction-sections -fdata-sections

# Directories
SRC_DIR = src
DIST_DIR = dist
BUILD_DIR = build
RUST_TARGET_DIR = $(BUILD_DIR)/rust-target
LIBS_DIR = libs
VPATH = $(SRC_DIR)

# Rust FFI library
MCP_FFI_LIB = $(RUST_TARGET_DIR)/release/libmcp_ffi.a
LDFLAGS = -L$(RUST_TARGET_DIR)/release

# Platform-specific settings
ifeq ($(PLATFORM),windows)
	TARGET := $(DIST_DIR)/mcp.dll
	LDFLAGS += -shared -Wl,--gc-sections
	DEF_FILE := $(BUILD_DIR)/mcp.def
	STRIP = strip --strip-unneeded $@
	LIBS = -lmcp_ffi -lws2_32 -luserenv -lbcrypt -lntdll -lgcc -lgcc_eh -lpthread
	T_LIBS = -lm
	RUSTFLAGS = -C opt-level=z -C lto=fat -C codegen-units=1 -C strip=symbols
else ifeq ($(PLATFORM),macos)
	TARGET := $(DIST_DIR)/mcp.dylib
	MACOS_MIN_VERSION = 11.0
	ifndef ARCH
		LDFLAGS += -arch x86_64 -arch arm64
		CFLAGS += -arch x86_64 -arch arm64
	else
		LDFLAGS += -arch $(ARCH)
		CFLAGS += -arch $(ARCH)
	endif
	LDFLAGS += -dynamiclib -undefined dynamic_lookup -headerpad_max_install_names -mmacosx-version-min=$(MACOS_MIN_VERSION) -Wl,-dead_strip
	CFLAGS += -mmacosx-version-min=$(MACOS_MIN_VERSION)
	CARGO_ENV += MACOSX_DEPLOYMENT_TARGET=$(MACOS_MIN_VERSION)
	CARGO = $(CARGO_ENV) cargo
	STRIP = strip -x -S -r $@
	LIBS = -lmcp_ffi -framework CoreFoundation -framework Security -lresolv
	T_LIBS = -lpthread -ldl -lm
	RUSTFLAGS = -C opt-level=z -C lto=fat -C codegen-units=1 -C strip=symbols
else ifeq ($(PLATFORM),android)
	ifndef ARCH
		$(error "Android ARCH must be set to ARCH=x86_64 or ARCH=arm64-v8a")
	endif
	ifndef ANDROID_NDK
		$(error "Android NDK must be set")
	endif
	BIN = $(ANDROID_NDK)/toolchains/llvm/prebuilt/$(HOST)-x86_64/bin
	PATH := $(BIN):$(PATH)
	ifneq (,$(filter $(ARCH),arm64 arm64-v8a))
		override ARCH := aarch64
		ANDROID_ABI := android26
	else ifeq ($(ARCH),armeabi-v7a)
		override ARCH := armv7a
		ANDROID_ABI := androideabi26
	else
		ANDROID_ABI := android26
	endif
	CC = $(BIN)/$(ARCH)-linux-$(ANDROID_ABI)-clang
	OPENSSL = $(OPENSSL_INSTALL_DIR)/lib/libssl.a
	TARGET := $(DIST_DIR)/mcp.so
	LDFLAGS += -shared -L$(OPENSSL_INSTALL_DIR)/lib -Wl,--gc-sections
	CFLAGS += -fPIC -I$(OPENSSL_INSTALL_DIR)/include
	STRIP = $(BIN)/llvm-strip --strip-unneeded $@
	LIBS = -lmcp_ffi -ldl -lm -lssl -lcrypto
	T_LIBS = -ldl -lm
	RUSTFLAGS = -C opt-level=z -C codegen-units=1 -C strip=symbols
else ifeq ($(PLATFORM),ios)
	TARGET := $(DIST_DIR)/mcp.dylib
	SDK := -isysroot $(shell xcrun --sdk iphoneos --show-sdk-path) -miphoneos-version-min=11.0
	LDFLAGS += -dynamiclib $(SDK) -headerpad_max_install_names -Wl,-dead_strip
	CFLAGS += -arch arm64 $(SDK)
	STRIP = strip -x -S $@
	LIBS = -lmcp_ffi -framework CoreFoundation -framework Security -lSystem -lresolv
	T_LIBS = -lpthread -ldl -lm
	RUSTFLAGS = -C opt-level=z -C lto=fat -C codegen-units=1 -C strip=symbols
else ifeq ($(PLATFORM),ios-sim)
	TARGET := $(DIST_DIR)/mcp.dylib
	SDK := -isysroot $(shell xcrun --sdk iphonesimulator --show-sdk-path) -miphonesimulator-version-min=11.0
	LDFLAGS += -arch x86_64 -arch arm64 -dynamiclib $(SDK) -headerpad_max_install_names -Wl,-dead_strip
	CFLAGS += -arch x86_64 -arch arm64 $(SDK)
	STRIP = strip -x -S $@
	LIBS = -lmcp_ffi -framework CoreFoundation -framework Security -lSystem -lresolv
	T_LIBS = -lpthread -ldl -lm
	RUSTFLAGS = -C opt-level=z -C lto=fat -C codegen-units=1 -C strip=symbols
else # linux
	TARGET := $(DIST_DIR)/mcp.so
	LDFLAGS += -shared -Wl,--gc-sections
	CFLAGS += -fPIC
	STRIP = strip --strip-unneeded $@
	LIBS = -lmcp_ffi -lpthread -ldl -lm -lssl -lcrypto
	T_LIBS = -lpthread -ldl -lm
	RUSTFLAGS = -C opt-level=z -C codegen-units=1 -C strip=symbols
endif

# Windows .def file generation
$(DEF_FILE):
ifeq ($(PLATFORM),windows)
	@echo "LIBRARY mcp.dll" > $@
	@echo "EXPORTS" >> $@
	@echo "    sqlite3_mcp_init" >> $@
endif

$(shell mkdir -p $(BUILD_DIR) $(DIST_DIR))
all: extension

OPENSSL_VERSION = openssl-3.6.0
OPENSSL_TARBALL = $(BUILD_DIR)/$(OPENSSL_VERSION).tar.gz
$(OPENSSL_TARBALL):
	curl -L -o $(OPENSSL_TARBALL) https://github.com/openssl/openssl/releases/download/$(OPENSSL_VERSION)/$(OPENSSL_VERSION).tar.gz

$(OPENSSL): $(OPENSSL_TARBALL)
	mkdir -p $(BUILD_DIR)/openssl
	tar -xzf $(OPENSSL_TARBALL) -C $(BUILD_DIR)/openssl
	cd $(BUILD_DIR)/openssl/$(OPENSSL_VERSION) && \
	./Configure android-$(if $(filter aarch64,$(ARCH)),arm64,$(if $(filter armv7a,$(ARCH)),arm,$(ARCH))) \
		--prefix=$(CURDIR)/$(OPENSSL_INSTALL_DIR) \
		no-shared no-unit-test -D__ANDROID_API__=26 && \
	$(MAKE) && $(MAKE) install_sw
	rm -rf $(BUILD_DIR)/openssl/$(OPENSSL_VERSION)

# Build the Rust FFI static library
ifeq ($(PLATFORM),android)
staticlib: $(OPENSSL)
else
staticlib:
endif
ifeq ($(PLATFORM),macos)
  ifndef ARCH
	@echo "Checking Rust targets for macOS universal build..."
	@rustup target list | grep -q "x86_64-apple-darwin (installed)" || rustup target add x86_64-apple-darwin
	@rustup target list | grep -q "aarch64-apple-darwin (installed)" || rustup target add aarch64-apple-darwin
	$(CARGO) build --release --target x86_64-apple-darwin
	$(CARGO) build --release --target aarch64-apple-darwin
	@mkdir -p $(RUST_TARGET_DIR)/release
	lipo -create \
		$(RUST_TARGET_DIR)/x86_64-apple-darwin/release/libmcp_ffi.a \
		$(RUST_TARGET_DIR)/aarch64-apple-darwin/release/libmcp_ffi.a \
		-output $(RUST_TARGET_DIR)/release/libmcp_ffi.a
  else
    ifeq ($(ARCH),x86_64)
	@echo "Checking Rust target for macOS x86_64..."
	@rustup target list | grep -q "x86_64-apple-darwin (installed)" || rustup target add x86_64-apple-darwin
	$(CARGO) build --release --target x86_64-apple-darwin
	@mkdir -p $(RUST_TARGET_DIR)/release
	cp $(RUST_TARGET_DIR)/x86_64-apple-darwin/release/libmcp_ffi.a $(RUST_TARGET_DIR)/release/libmcp_ffi.a
    else ifeq ($(ARCH),arm64)
	@echo "Checking Rust target for macOS arm64..."
	@rustup target list | grep -q "aarch64-apple-darwin (installed)" || rustup target add aarch64-apple-darwin
	$(CARGO) build --release --target aarch64-apple-darwin
	@mkdir -p $(RUST_TARGET_DIR)/release
	cp $(RUST_TARGET_DIR)/aarch64-apple-darwin/release/libmcp_ffi.a $(RUST_TARGET_DIR)/release/libmcp_ffi.a
    endif
  endif
else ifeq ($(PLATFORM),ios)
	@echo "Checking Rust target for iOS..."
	@rustup target list | grep -q "aarch64-apple-ios (installed)" || rustup target add aarch64-apple-ios
	$(CARGO) build --release --target aarch64-apple-ios
	@mkdir -p $(RUST_TARGET_DIR)/release
	cp $(RUST_TARGET_DIR)/aarch64-apple-ios/release/libmcp_ffi.a $(RUST_TARGET_DIR)/release/libmcp_ffi.a
else ifeq ($(PLATFORM),ios-sim)
	@echo "Checking Rust targets for iOS Simulator..."
	@rustup target list | grep -q "aarch64-apple-ios-sim (installed)" || rustup target add aarch64-apple-ios-sim
	@rustup target list | grep -q "x86_64-apple-ios (installed)" || rustup target add x86_64-apple-ios
	$(CARGO) build --release --target aarch64-apple-ios-sim
	$(CARGO) build --release --target x86_64-apple-ios
	@mkdir -p $(RUST_TARGET_DIR)/release
	lipo -create \
		$(RUST_TARGET_DIR)/aarch64-apple-ios-sim/release/libmcp_ffi.a \
		$(RUST_TARGET_DIR)/x86_64-apple-ios/release/libmcp_ffi.a \
		-output $(RUST_TARGET_DIR)/release/libmcp_ffi.a
else ifeq ($(PLATFORM),windows)
	@echo "Checking Rust target for Windows..."
	@rustup target list | grep -q "x86_64-pc-windows-gnu (installed)" || rustup target add x86_64-pc-windows-gnu
	$(CARGO) build --release --target x86_64-pc-windows-gnu
	@mkdir -p $(RUST_TARGET_DIR)/release
	cp $(RUST_TARGET_DIR)/x86_64-pc-windows-gnu/release/libmcp_ffi.a $(RUST_TARGET_DIR)/release/libmcp_ffi.a
else ifeq ($(PLATFORM),android)
  ifeq ($(ARCH),aarch64)
	@echo "Checking Rust target for Android ARM64..."
	@rustup target list | grep -q "aarch64-linux-android (installed)" || rustup target add aarch64-linux-android
	$(CARGO) build --release --target aarch64-linux-android
	@mkdir -p $(RUST_TARGET_DIR)/release
	cp $(RUST_TARGET_DIR)/aarch64-linux-android/release/libmcp_ffi.a $(RUST_TARGET_DIR)/release/libmcp_ffi.a
  else ifeq ($(ARCH),armv7a)
	@echo "Checking Rust target for Android ARMv7..."
	@rustup target list | grep -q "armv7-linux-androideabi (installed)" || rustup target add armv7-linux-androideabi
	CC=$(CC) AR=$(BIN)/llvm-ar $(CARGO) build --release --target armv7-linux-androideabi
	@mkdir -p $(RUST_TARGET_DIR)/release
	cp $(RUST_TARGET_DIR)/armv7-linux-androideabi/release/libmcp_ffi.a $(RUST_TARGET_DIR)/release/libmcp_ffi.a
  else ifeq ($(ARCH),x86_64)
	@echo "Checking Rust target for Android x86_64..."
	@rustup target list | grep -q "x86_64-linux-android (installed)" || rustup target add x86_64-linux-android
	$(CARGO) build --release --target x86_64-linux-android
	@mkdir -p $(RUST_TARGET_DIR)/release
	cp $(RUST_TARGET_DIR)/x86_64-linux-android/release/libmcp_ffi.a $(RUST_TARGET_DIR)/release/libmcp_ffi.a
  endif
else
	$(CARGO) build --release
endif

$(TARGET): staticlib $(DEF_FILE)
	$(CC) $(CFLAGS) $(SRC_DIR)/sqlite-mcp.c $(LDFLAGS) $(LIBS) -o $@
	$(STRIP)

extension: $(TARGET)

T_TARGET = $(BUILD_DIR)/test
$(T_TARGET): $(TARGET)
	$(CC) $(CFLAGS) -O3 test/main.c $(LIBS_DIR)/sqlite3.c -o $@ $(T_LIBS)

test: $(T_TARGET)
	$(SQLITE3) ":memory:" -cmd ".bail on" ".load ./dist/mcp" "SELECT mcp_version();"
	$(BUILD_DIR)/test

# clean everything except OpenSSL libs
clean:
	rm -rf $(DIST_DIR)
	find $(BUILD_DIR) -mindepth 1 -maxdepth 1 ! -name openssl -exec rm -rf {} +
	$(CARGO) clean

# XCFramework build for Swift Package Manager
.NOTPARALLEL: %.dylib
%.dylib:
	rm -rf $(BUILD_DIR) && $(MAKE) PLATFORM=$*
	mv $(DIST_DIR)/mcp.dylib $(DIST_DIR)/$@

define PLIST
<?xml version=\"1.0\" encoding=\"UTF-8\"?>\
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\
<plist version=\"1.0\">\
<dict>\
<key>CFBundleDevelopmentRegion</key>\
<string>en</string>\
<key>CFBundleExecutable</key>\
<string>mcp</string>\
<key>CFBundleIdentifier</key>\
<string>ai.sqlite.mcp</string>\
<key>CFBundleInfoDictionaryVersion</key>\
<string>6.0</string>\
<key>CFBundlePackageType</key>\
<string>FMWK</string>\
<key>CFBundleSignature</key>\
<string>????</string>\
<key>CFBundleVersion</key>\
<string>$(shell make version)</string>\
<key>CFBundleShortVersionString</key>\
<string>$(shell make version)</string>\
<key>MinimumOSVersion</key>\
<string>11.0</string>\
</dict>\
</plist>
endef

define MODULEMAP
framework module mcp {\
  umbrella header \"sqlite-mcp.h\"\
  export *\
}
endef

LIB_NAMES = ios.dylib ios-sim.dylib macos.dylib
FMWK_NAMES = ios-arm64 ios-arm64_x86_64-simulator macos-arm64_x86_64
$(DIST_DIR)/%.xcframework: $(LIB_NAMES)
	@$(foreach i,1 2 3,\
		lib=$(word $(i),$(LIB_NAMES)); \
		fmwk=$(word $(i),$(FMWK_NAMES)); \
		mkdir -p $(DIST_DIR)/$$fmwk/mcp.framework/Headers; \
		mkdir -p $(DIST_DIR)/$$fmwk/mcp.framework/Modules; \
		cp $(SRC_DIR)/sqlite-mcp.h $(DIST_DIR)/$$fmwk/mcp.framework/Headers; \
		printf "$(PLIST)" > $(DIST_DIR)/$$fmwk/mcp.framework/Info.plist; \
		printf "$(MODULEMAP)" > $(DIST_DIR)/$$fmwk/mcp.framework/Modules/module.modulemap; \
		mv $(DIST_DIR)/$$lib $(DIST_DIR)/$$fmwk/mcp.framework/mcp; \
		install_name_tool -id "@rpath/mcp.framework/mcp" $(DIST_DIR)/$$fmwk/mcp.framework/mcp; \
	)
	xcodebuild -create-xcframework $(foreach fmwk,$(FMWK_NAMES),-framework $(DIST_DIR)/$(fmwk)/mcp.framework) -output $@
	rm -rf $(foreach fmwk,$(FMWK_NAMES),$(DIST_DIR)/$(fmwk))

xcframework: $(DIST_DIR)/mcp.xcframework

AAR_ARM64 = packages/android/src/main/jniLibs/arm64-v8a/
AAR_ARM = packages/android/src/main/jniLibs/armeabi-v7a/
AAR_X86 = packages/android/src/main/jniLibs/x86_64/
aar:
	mkdir -p $(AAR_ARM64) $(AAR_ARM) $(AAR_X86)
	$(MAKE) clean && $(MAKE) PLATFORM=android ARCH=arm64-v8a
	mv $(DIST_DIR)/mcp.so $(AAR_ARM64)
	$(MAKE) clean && $(MAKE) PLATFORM=android ARCH=armeabi-v7a
	mv $(DIST_DIR)/mcp.so $(AAR_ARM)
	$(MAKE) clean && $(MAKE) PLATFORM=android ARCH=x86_64
	mv $(DIST_DIR)/mcp.so $(AAR_X86)
	cd packages/android && ./gradlew clean assembleRelease
	cp packages/android/build/outputs/aar/android-release.aar $(DIST_DIR)/mcp.aar

version:
	@echo $(shell sed -n 's/^#define SQLITE_MCP_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' $(SRC_DIR)/sqlite-mcp.h)

help:
	@echo "sqlite-mcp Makefile"
	@echo ""
	@echo "Usage: make [PLATFORM=platform] [ARCH=arch] [target]"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build the extension (default)"
	@echo "  extension  - Build the SQLite extension"
	@echo "  staticlib  - Build only the Rust static library"
	@echo "  test       - Run quick CLI test + C test suite"
	@echo "  xcframework- Build XCFramework for Swift Package Manager (macOS/iOS)"
	@echo "  aar        - Build Android AAR package (requires ANDROID_NDK)"
	@echo "  clean      - Remove all build artifacts"
	@echo "  version    - Display extension version"
	@echo "  help       - Display this help message"
	@echo ""
	@echo "Platforms:"
	@echo "  macos      - macOS (default on Darwin)"
	@echo "  linux      - Linux (default on Linux)"
	@echo "  windows    - Windows (default on Windows)"
	@echo "  android    - Android (requires ARCH and ANDROID_NDK)"
	@echo "  ios        - iOS device"
	@echo "  ios-sim    - iOS simulator"
	@echo ""
	@echo "Examples:"
	@echo "  make                           # Build for current platform"
	@echo "  make test                      # Build and test"
	@echo "  make PLATFORM=android ARCH=arm64-v8a  # Build for Android ARM64"

.PHONY: all extension staticlib test xcframework aar clean version help
