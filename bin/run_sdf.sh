#!/bin/bash
# SDF Vulkan Viewer - Runs vybe.sdf
set -e
cd "$(dirname "$0")/.."

# Parse arguments
STANDALONE=false
OUTPUT_NAME="sdf-viewer"
while [[ $# -gt 0 ]]; do
    case $1 in
        --standalone)
            STANDALONE=true
            shift
            ;;
        -o|--output)
            OUTPUT_NAME="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--standalone] [-o|--output <name>]"
            exit 1
            ;;
    esac
done

# jank installation paths
JANK_DIR="/Users/pfeodrippe/dev/jank/compiler+runtime/build"
JANK_LIB_DIR="$JANK_DIR/llvm-install/usr/local/lib"

# ============================================================================
# macOS App Bundle Creation
# ============================================================================
create_macos_app_bundle() {
    local EXECUTABLE="$1"
    local APP_NAME="$2"
    local APP_BUNDLE="${APP_NAME}.app"

    echo ""
    echo "============================================"
    echo "Creating macOS App Bundle: $APP_BUNDLE"
    echo "============================================"

    # Clean and create bundle structure
    rm -rf "$APP_BUNDLE"
    mkdir -p "$APP_BUNDLE/Contents/MacOS"
    mkdir -p "$APP_BUNDLE/Contents/Frameworks"
    mkdir -p "$APP_BUNDLE/Contents/Resources"

    # Copy executable (as the real binary)
    cp "$EXECUTABLE" "$APP_BUNDLE/Contents/MacOS/${APP_NAME}-bin"

    # Create launcher script that sets up environment
    cat > "$APP_BUNDLE/Contents/MacOS/$APP_NAME" << LAUNCHER
#!/bin/bash
# Launcher script for $APP_NAME
DIR="\$(cd "\$(dirname "\$0")" && pwd)"
RESOURCES="\$(cd "\$DIR/../Resources" && pwd)"

# Set Vulkan ICD path to bundled MoltenVK
export VK_ICD_FILENAMES="\$RESOURCES/vulkan/icd.d/MoltenVK_icd.json"

# Set C/C++ include paths for JIT compilation at runtime
# CPATH is used by clang to find headers during JIT compilation
# Include bundled C++ headers FIRST to avoid conflicts with system headers
export CPATH="\$RESOURCES/include/c++/v1:\$RESOURCES/include:\$RESOURCES/include/flecs:\$RESOURCES/include/imgui:\$RESOURCES/include/imgui/backends"

# Set CXX to bundled clang for jank JIT compilation
# jank checks CXX env var as fallback when hardcoded path doesn't exist
export CXX="\$RESOURCES/bin/clang++"

# Set working directory to Resources for shader loading
cd "\$RESOURCES"

# Launch the actual binary
exec "\$DIR/${APP_NAME}-bin" "\$@"
LAUNCHER
    chmod +x "$APP_BUNDLE/Contents/MacOS/$APP_NAME"

    # Copy shader resources
    echo "Copying shader resources..."
    cp -r vulkan_kim "$APP_BUNDLE/Contents/Resources/"

    # Copy jank source files
    echo "Copying jank source files..."
    cp -r src "$APP_BUNDLE/Contents/Resources/"

    # Copy header files for JIT compilation at runtime
    # CPATH in launcher script points to these directories
    echo "Copying header files..."
    mkdir -p "$APP_BUNDLE/Contents/Resources/include/vybe"
    mkdir -p "$APP_BUNDLE/Contents/Resources/include/flecs"
    mkdir -p "$APP_BUNDLE/Contents/Resources/include/imgui/backends"

    # vybe headers
    cp vendor/vybe/*.h "$APP_BUNDLE/Contents/Resources/include/vybe/" 2>/dev/null || true

    # flecs headers
    cp vendor/flecs/distr/flecs.h "$APP_BUNDLE/Contents/Resources/include/flecs/" 2>/dev/null || true

    # imgui headers (main headers and backends)
    cp vendor/imgui/*.h "$APP_BUNDLE/Contents/Resources/include/imgui/" 2>/dev/null || true
    cp vendor/imgui/backends/*.h "$APP_BUNDLE/Contents/Resources/include/imgui/backends/" 2>/dev/null || true

    # NOTE: We intentionally do NOT bundle sdf_engine.hpp and its dependencies
    # (SDL3, Vulkan, shaderc headers). Direct cpp/sdfx.X calls from nREPL won't
    # work in standalone mode due to JIT/AOT state sharing issues.
    # Instead, use the pre-compiled jank wrapper functions in vybe.sdf namespace.
    # To add new functionality, add jank wrappers and rebuild standalone.

    # Bundle clang for JIT compilation (jank requires clang 22)
    echo "Bundling clang for JIT..."
    mkdir -p "$APP_BUNDLE/Contents/Resources/bin"
    mkdir -p "$APP_BUNDLE/Contents/Resources/lib"
    # Copy clang binary (just the driver - uses libclang-cpp.dylib)
    cp "$JANK_LIB_DIR/../bin/clang-22" "$APP_BUNDLE/Contents/Resources/bin/"
    ln -sf clang-22 "$APP_BUNDLE/Contents/Resources/bin/clang"
    # Fix clang's rpath to find libs in Frameworks (../../Frameworks from Resources/bin)
    install_name_tool -add_rpath "@executable_path/../../Frameworks" "$APP_BUNDLE/Contents/Resources/bin/clang-22"
    # Fix system libc++ reference to use bundled version
    install_name_tool -change "/usr/lib/libc++.1.dylib" "@executable_path/../../Frameworks/libc++.1.dylib" "$APP_BUNDLE/Contents/Resources/bin/clang-22"

    # Create clang++ wrapper that forces bundled headers (clang has hardcoded absolute paths)
    cat > "$APP_BUNDLE/Contents/Resources/bin/clang++" << 'CLANG_WRAPPER'
#!/bin/bash
# Wrapper to force clang to use bundled C++ headers instead of hardcoded build paths
DIR="$(cd "$(dirname "$0")" && pwd)"
RESOURCES="$(cd "$DIR/.." && pwd)"

# -nostdinc++ removes hardcoded C++ include paths
# -isystem adds bundled headers as system includes
exec "$DIR/clang-22" -nostdinc++ \
    -isystem "$RESOURCES/include/c++/v1" \
    -isystem "$RESOURCES/lib/clang/22/include" \
    "$@"
CLANG_WRAPPER
    chmod +x "$APP_BUNDLE/Contents/Resources/bin/clang++"

    # Copy clang resource directory (headers for JIT)
    cp -r "$JANK_LIB_DIR/clang" "$APP_BUNDLE/Contents/Resources/lib/"
    # Copy C++ standard library headers (to avoid conflicts with system headers)
    echo "Bundling C++ headers..."
    cp -r "$JANK_LIB_DIR/../include" "$APP_BUNDLE/Contents/Resources/"
    # Create Frameworks symlink in Resources for libraries that use @executable_path/../Frameworks
    # (when clang runs, its @executable_path is Resources/bin, so libs look in Resources/Frameworks)
    ln -sf ../Frameworks "$APP_BUNDLE/Contents/Resources/Frameworks"

    # Copy MoltenVK ICD for Vulkan discovery
    echo "Setting up Vulkan ICD..."
    mkdir -p "$APP_BUNDLE/Contents/Resources/vulkan/icd.d"
    # Create ICD manifest pointing to bundled MoltenVK (relative path)
    cat > "$APP_BUNDLE/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json" << 'MOLTENVK_ICD'
{
    "file_format_version" : "1.0.0",
    "ICD": {
        "library_path": "../../../Frameworks/libMoltenVK.dylib",
        "api_version" : "1.2.0"
    }
}
MOLTENVK_ICD

    # Collect all dylibs that need to be bundled
    echo "Bundling dynamic libraries..."

    # jank runtime libs
    JANK_LIBS=(
        "$JANK_LIB_DIR/libLLVM.dylib"
        "$JANK_LIB_DIR/libclang-cpp.dylib"
        "$JANK_LIB_DIR/libc++.1.dylib"
        "$JANK_LIB_DIR/libc++abi.1.dylib"
        "$JANK_LIB_DIR/libunwind.1.dylib"
    )

    # Our project libs
    PROJECT_LIBS=(
        "vulkan/libsdf_deps.dylib"
    )

    # System/homebrew libs that jank depends on
    EXTERNAL_LIBS=(
        "/opt/homebrew/opt/openssl@3/lib/libcrypto.3.dylib"
        "/opt/homebrew/opt/zstd/lib/libzstd.1.dylib"
    )

    # SDL3/Vulkan/shaderc libs for full portability
    GRAPHICS_LIBS=(
        "/opt/homebrew/lib/libSDL3.dylib"
        "/opt/homebrew/lib/libvulkan.dylib"
        "/opt/homebrew/lib/libshaderc_shared.dylib"
        "/opt/homebrew/lib/libMoltenVK.dylib"
    )

    # Copy all libs to Frameworks
    for lib in "${JANK_LIBS[@]}" "${PROJECT_LIBS[@]}" "${EXTERNAL_LIBS[@]}" "${GRAPHICS_LIBS[@]}"; do
        if [ -f "$lib" ]; then
            libname=$(basename "$lib")
            echo "  Copying $libname"
            cp "$lib" "$APP_BUNDLE/Contents/Frameworks/"
        else
            echo "  Warning: $lib not found"
        fi
    done

    # Follow symlinks for versioned libs
    for lib in "$APP_BUNDLE/Contents/Frameworks/"*.dylib; do
        if [ -L "$lib" ]; then
            target=$(readlink "$lib")
            if [ -f "/opt/homebrew/lib/$target" ]; then
                cp "/opt/homebrew/lib/$target" "$APP_BUNDLE/Contents/Frameworks/"
            fi
        fi
    done

    # Create versioned symlinks (executable may reference libSDL3.0.dylib, libvulkan.1.dylib, etc.)
    echo "Creating versioned library symlinks..."
    (cd "$APP_BUNDLE/Contents/Frameworks" && \
        ln -sf libSDL3.dylib libSDL3.0.dylib && \
        ln -sf libvulkan.dylib libvulkan.1.dylib && \
        ln -sf libshaderc_shared.dylib libshaderc_shared.1.dylib)

    # Fix library paths in the executable
    echo "Fixing library paths..."
    local EXEC_PATH="$APP_BUNDLE/Contents/MacOS/${APP_NAME}-bin"

    # Fix @rpath references to point to Frameworks
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$EXEC_PATH" 2>/dev/null || true

    # Fix hardcoded paths in executable
    install_name_tool -change "/opt/homebrew/opt/openssl@3/lib/libcrypto.3.dylib" \
        "@executable_path/../Frameworks/libcrypto.3.dylib" "$EXEC_PATH"
    install_name_tool -change "/opt/homebrew/opt/zstd/lib/libzstd.1.dylib" \
        "@executable_path/../Frameworks/libzstd.1.dylib" "$EXEC_PATH"
    install_name_tool -change "vulkan/libsdf_deps.dylib" \
        "@executable_path/../Frameworks/libsdf_deps.dylib" "$EXEC_PATH"

    # Fix graphics library paths in executable (both versioned and unversioned)
    install_name_tool -change "/opt/homebrew/lib/libSDL3.dylib" \
        "@executable_path/../Frameworks/libSDL3.dylib" "$EXEC_PATH" 2>/dev/null || true
    install_name_tool -change "/opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib" \
        "@executable_path/../Frameworks/libSDL3.0.dylib" "$EXEC_PATH" 2>/dev/null || true
    install_name_tool -change "/opt/homebrew/lib/libvulkan.dylib" \
        "@executable_path/../Frameworks/libvulkan.dylib" "$EXEC_PATH" 2>/dev/null || true
    install_name_tool -change "/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib" \
        "@executable_path/../Frameworks/libvulkan.1.dylib" "$EXEC_PATH" 2>/dev/null || true
    install_name_tool -change "/opt/homebrew/lib/libshaderc_shared.dylib" \
        "@executable_path/../Frameworks/libshaderc_shared.dylib" "$EXEC_PATH" 2>/dev/null || true
    install_name_tool -change "/opt/homebrew/opt/shaderc/lib/libshaderc_shared.1.dylib" \
        "@executable_path/../Frameworks/libshaderc_shared.1.dylib" "$EXEC_PATH" 2>/dev/null || true

    # Fix library paths in the bundled dylibs themselves
    for lib in "$APP_BUNDLE/Contents/Frameworks/"*.dylib; do
        libname=$(basename "$lib")
        echo "  Fixing paths in $libname"

        # Update the library's own ID
        install_name_tool -id "@executable_path/../Frameworks/$libname" "$lib" 2>/dev/null || true

        # Fix references to other libs
        install_name_tool -change "/opt/homebrew/opt/openssl@3/lib/libcrypto.3.dylib" \
            "@executable_path/../Frameworks/libcrypto.3.dylib" "$lib" 2>/dev/null || true
        install_name_tool -change "/opt/homebrew/opt/zstd/lib/libzstd.1.dylib" \
            "@executable_path/../Frameworks/libzstd.1.dylib" "$lib" 2>/dev/null || true

        # Fix graphics library references
        install_name_tool -change "/opt/homebrew/lib/libSDL3.dylib" \
            "@executable_path/../Frameworks/libSDL3.dylib" "$lib" 2>/dev/null || true
        install_name_tool -change "/opt/homebrew/lib/libvulkan.dylib" \
            "@executable_path/../Frameworks/libvulkan.dylib" "$lib" 2>/dev/null || true
        install_name_tool -change "/opt/homebrew/lib/libshaderc_shared.dylib" \
            "@executable_path/../Frameworks/libshaderc_shared.dylib" "$lib" 2>/dev/null || true
        install_name_tool -change "/opt/homebrew/lib/libMoltenVK.dylib" \
            "@executable_path/../Frameworks/libMoltenVK.dylib" "$lib" 2>/dev/null || true

        # Fix versioned lib references (SDL3.0, vulkan.1, etc)
        install_name_tool -change "/opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib" \
            "@executable_path/../Frameworks/libSDL3.0.dylib" "$lib" 2>/dev/null || true
        install_name_tool -change "/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib" \
            "@executable_path/../Frameworks/libvulkan.1.dylib" "$lib" 2>/dev/null || true
        install_name_tool -change "/opt/homebrew/opt/shaderc/lib/libshaderc_shared.1.dylib" \
            "@executable_path/../Frameworks/libshaderc_shared.1.dylib" "$lib" 2>/dev/null || true

        # Fix system libc++ references to use bundled version
        install_name_tool -change "/usr/lib/libc++.1.dylib" \
            "@executable_path/../Frameworks/libc++.1.dylib" "$lib" 2>/dev/null || true
        install_name_tool -change "/usr/lib/libc++abi.dylib" \
            "@executable_path/../Frameworks/libc++abi.1.dylib" "$lib" 2>/dev/null || true

        # Fix jank lib cross-references
        for jank_lib in "${JANK_LIBS[@]}"; do
            jank_libname=$(basename "$jank_lib")
            install_name_tool -change "$jank_lib" \
                "@executable_path/../Frameworks/$jank_libname" "$lib" 2>/dev/null || true
            install_name_tool -change "@rpath/$jank_libname" \
                "@executable_path/../Frameworks/$jank_libname" "$lib" 2>/dev/null || true
        done
    done

    # Create Info.plist
    echo "Creating Info.plist..."
    cat > "$APP_BUNDLE/Contents/Info.plist" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>$APP_NAME</string>
    <key>CFBundleIdentifier</key>
    <string>com.vybe.$APP_NAME</string>
    <key>CFBundleName</key>
    <string>$APP_NAME</string>
    <key>CFBundleDisplayName</key>
    <string>SDF Viewer</string>
    <key>CFBundleVersion</key>
    <string>1.0.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key>
    <true/>
</dict>
</plist>
PLIST

    # Create PkgInfo
    echo "APPL????" > "$APP_BUNDLE/Contents/PkgInfo"

    # Clean up the raw executable
    rm -f "$EXECUTABLE"

    # Re-sign all modified binaries (required after install_name_tool)
    echo "Code signing..."
    # Sign frameworks first
    for lib in "$APP_BUNDLE/Contents/Frameworks/"*.dylib; do
        codesign --force --sign - "$lib" 2>/dev/null || true
    done
    # Sign the main executable binary
    codesign --force --sign - "$EXEC_PATH"
    # Sign the whole bundle
    codesign --force --sign - "$APP_BUNDLE"

    echo ""
    echo "============================================"
    echo "App bundle created: $APP_BUNDLE"
    echo "============================================"
    echo ""
    echo "Contents:"
    du -sh "$APP_BUNDLE"
    echo ""
    echo "To run: open $APP_BUNDLE"
    echo "Or:     $APP_BUNDLE/Contents/MacOS/$APP_NAME"

    # Create DMG for distribution (preserves permissions)
    echo ""
    echo "Creating DMG for distribution..."
    DMG_NAME="${APP_NAME}.dmg"
    rm -f "$DMG_NAME"
    hdiutil create -volname "$APP_NAME" -srcfolder "$APP_BUNDLE" -ov -format UDZO "$DMG_NAME"
    echo ""
    echo "============================================"
    echo "DMG created: $DMG_NAME"
    echo "============================================"
    echo "Share this DMG file - it preserves all permissions."
    ls -lh "$DMG_NAME"
}

# ============================================================================
# Linux Standalone Creation
# ============================================================================
create_linux_standalone() {
    local EXECUTABLE="$1"
    local APP_NAME="$2"
    local DIST_DIR="${APP_NAME}-linux"

    echo ""
    echo "============================================"
    echo "Creating Linux Standalone: $DIST_DIR"
    echo "============================================"

    # Clean and create directory structure
    rm -rf "$DIST_DIR"
    mkdir -p "$DIST_DIR/bin"
    mkdir -p "$DIST_DIR/lib"
    mkdir -p "$DIST_DIR/resources"
    mkdir -p "$DIST_DIR/include"

    # Copy executable
    cp "$EXECUTABLE" "$DIST_DIR/bin/${APP_NAME}-bin"

    # Create launcher script
    cat > "$DIST_DIR/$APP_NAME" << 'LAUNCHER'
#!/bin/bash
# Launcher script for Linux standalone
DIR="$(cd "$(dirname "$0")" && pwd)"

# Set library path
export LD_LIBRARY_PATH="$DIR/lib:$LD_LIBRARY_PATH"

# Set C/C++ include paths for JIT compilation at runtime
export CPATH="$DIR/include/c++/v1:$DIR/include:$DIR/include/flecs:$DIR/include/imgui:$DIR/include/imgui/backends"

# Set CXX to bundled clang for jank JIT compilation
export CXX="$DIR/bin/clang++"

# Set working directory to resources for shader loading
cd "$DIR/resources"

# Launch the actual binary
exec "$DIR/bin/${APP_NAME##*/}-bin" "$@"
LAUNCHER
    chmod +x "$DIST_DIR/$APP_NAME"

    # Copy shader resources
    echo "Copying shader resources..."
    cp -r vulkan_kim "$DIST_DIR/resources/"

    # Copy jank source files
    echo "Copying jank source files..."
    cp -r src "$DIST_DIR/resources/"

    # Copy header files for JIT compilation at runtime
    echo "Copying header files..."
    mkdir -p "$DIST_DIR/include/vybe"
    mkdir -p "$DIST_DIR/include/flecs"
    mkdir -p "$DIST_DIR/include/imgui/backends"

    # vybe headers
    cp vendor/vybe/*.h "$DIST_DIR/include/vybe/" 2>/dev/null || true

    # flecs headers
    cp vendor/flecs/distr/flecs.h "$DIST_DIR/include/flecs/" 2>/dev/null || true

    # imgui headers
    cp vendor/imgui/*.h "$DIST_DIR/include/imgui/" 2>/dev/null || true
    cp vendor/imgui/backends/*.h "$DIST_DIR/include/imgui/backends/" 2>/dev/null || true

    # Bundle clang for JIT compilation
    echo "Bundling clang for JIT..."
    local JANK_BIN_DIR="$JANK_DIR/llvm-install/usr/local/bin"
    cp "$JANK_BIN_DIR/clang-22" "$DIST_DIR/bin/"
    ln -sf clang-22 "$DIST_DIR/bin/clang"

    # Create clang++ wrapper
    cat > "$DIST_DIR/bin/clang++" << 'CLANG_WRAPPER'
#!/bin/bash
DIR="$(cd "$(dirname "$0")" && pwd)"
RESOURCES="$(cd "$DIR/.." && pwd)"
exec "$DIR/clang-22" -nostdinc++ \
    -isystem "$RESOURCES/include/c++/v1" \
    -isystem "$RESOURCES/lib/clang/22/include" \
    "$@"
CLANG_WRAPPER
    chmod +x "$DIST_DIR/bin/clang++"

    # Copy clang resource directory
    mkdir -p "$DIST_DIR/lib"
    cp -r "$JANK_LIB_DIR/clang" "$DIST_DIR/lib/"

    # Copy C++ standard library headers
    echo "Bundling C++ headers..."
    cp -r "$JANK_LIB_DIR/../include" "$DIST_DIR/"

    # Collect shared libraries to bundle
    echo "Bundling shared libraries..."

    # jank runtime libs
    local JANK_LIBS=(
        "$JANK_LIB_DIR/libLLVM.so"
        "$JANK_LIB_DIR/libclang-cpp.so"
        "$JANK_LIB_DIR/libc++.so.1"
        "$JANK_LIB_DIR/libc++abi.so.1"
        "$JANK_LIB_DIR/libunwind.so.1"
    )

    # Try to find versioned libs if main ones don't exist
    for lib in "${JANK_LIBS[@]}"; do
        if [ -f "$lib" ]; then
            echo "  Copying $(basename "$lib")"
            cp "$lib" "$DIST_DIR/lib/"
        else
            # Try with version suffix
            for versioned in "${lib}".*; do
                if [ -f "$versioned" ]; then
                    echo "  Copying $(basename "$versioned")"
                    cp "$versioned" "$DIST_DIR/lib/"
                    break
                fi
            done
        fi
    done

    # Copy our project shared lib if it exists
    if [ -f "vulkan/libsdf_deps.so" ]; then
        cp "vulkan/libsdf_deps.so" "$DIST_DIR/lib/"
    fi

    # System libraries needed (find and copy from system)
    local SYSTEM_LIBS=(
        libvulkan.so
        libSDL2-2.0.so
        libshaderc_shared.so
    )

    for lib in "${SYSTEM_LIBS[@]}"; do
        # Try to find the library
        local lib_path=$(ldconfig -p 2>/dev/null | grep "$lib" | head -1 | awk '{print $NF}')
        if [ -n "$lib_path" ] && [ -f "$lib_path" ]; then
            echo "  Copying $lib from $lib_path"
            cp "$lib_path" "$DIST_DIR/lib/"
        else
            # Try common paths
            for search_path in /usr/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/local/lib; do
                if [ -f "$search_path/$lib" ]; then
                    echo "  Copying $lib from $search_path"
                    cp "$search_path/$lib" "$DIST_DIR/lib/"
                    break
                fi
            done
        fi
    done

    # Fix library paths using patchelf
    echo "Fixing library paths with patchelf..."
    if command -v patchelf &> /dev/null; then
        # Set RPATH on the executable
        patchelf --set-rpath '$ORIGIN/../lib' "$DIST_DIR/bin/${APP_NAME}-bin" 2>/dev/null || true

        # Set RPATH on clang
        patchelf --set-rpath '$ORIGIN/../lib' "$DIST_DIR/bin/clang-22" 2>/dev/null || true

        # Fix library RPATHs
        for lib in "$DIST_DIR/lib/"*.so*; do
            if [ -f "$lib" ] && [ ! -L "$lib" ]; then
                patchelf --set-rpath '$ORIGIN' "$lib" 2>/dev/null || true
            fi
        done
    else
        echo "Warning: patchelf not found, library paths not fixed"
    fi

    # Create versioned symlinks
    echo "Creating versioned library symlinks..."
    (cd "$DIST_DIR/lib" && \
        ln -sf libvulkan.so libvulkan.so.1 2>/dev/null || true && \
        ln -sf libSDL2-2.0.so libSDL2.so 2>/dev/null || true && \
        ln -sf libc++.so.1 libc++.so 2>/dev/null || true && \
        ln -sf libc++abi.so.1 libc++abi.so 2>/dev/null || true)

    # Clean up the raw executable
    rm -f "$EXECUTABLE"

    echo ""
    echo "============================================"
    echo "Linux standalone created: $DIST_DIR"
    echo "============================================"
    echo ""
    echo "Contents:"
    du -sh "$DIST_DIR"
    echo ""
    echo "To run: ./$DIST_DIR/$APP_NAME"

    # Create tarball for distribution
    echo ""
    echo "Creating tarball for distribution..."
    TAR_NAME="${APP_NAME}-linux.tar.gz"
    rm -f "$TAR_NAME"
    tar -czvf "$TAR_NAME" "$DIST_DIR"
    echo ""
    echo "============================================"
    echo "Tarball created: $TAR_NAME"
    echo "============================================"
    ls -lh "$TAR_NAME"
}

# Platform-specific environment setup
case "$(uname -s)" in
    Darwin)
        export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
        export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:/usr/bin:/bin:$PATH"

        # Vulkan environment for MoltenVK
        export VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
        export DYLD_LIBRARY_PATH=/opt/homebrew/lib
        ;;
    Linux)
        # Linux jank path (adjust as needed for your setup)
        if [ -d "$HOME/jank/compiler+runtime/build" ]; then
            export PATH="$HOME/jank/compiler+runtime/build:$PATH"
            JANK_DIR="$HOME/jank/compiler+runtime/build"
            JANK_LIB_DIR="$JANK_DIR/llvm-install/usr/local/lib"
        fi
        ;;
esac

# Build imgui objects if needed
if [ ! -f vulkan/imgui/imgui.o ]; then
    echo "Building imgui..."
    make -C vulkan/imgui
fi

# Compile blit shaders if needed
# Use platform-specific glslang compiler
if command -v glslangValidator &> /dev/null; then
    GLSLC=glslangValidator
elif command -v glslangValidator4 &> /dev/null; then
    GLSLC=glslangValidator4
else
    echo "Warning: glslangValidator not found, skipping shader compilation"
    GLSLC=""
fi

if [ -n "$GLSLC" ]; then
    if [ ! -f vulkan_kim/blit.vert.spv ] || [ vulkan_kim/blit.vert -nt vulkan_kim/blit.vert.spv ]; then
        echo "Compiling blit.vert..."
        $GLSLC -V vulkan_kim/blit.vert -o vulkan_kim/blit.vert.spv
    fi
    if [ ! -f vulkan_kim/blit.frag.spv ] || [ vulkan_kim/blit.frag -nt vulkan_kim/blit.frag.spv ]; then
        echo "Compiling blit.frag..."
        $GLSLC -V vulkan_kim/blit.frag -o vulkan_kim/blit.frag.spv
    fi
fi

# Build stb_impl.o if needed
if [ ! -f vulkan/stb_impl.o ] || [ vulkan/stb_impl.c -nt vulkan/stb_impl.o ]; then
    echo "Compiling stb_impl..."
    clang -c vulkan/stb_impl.c -o vulkan/stb_impl.o
fi

# Build vybe_flecs_jank.o if needed (jank-runtime-dependent flecs helpers)
if [ ! -f vendor/vybe/vybe_flecs_jank.o ] || [ vendor/vybe/vybe_flecs_jank.cpp -nt vendor/vybe/vybe_flecs_jank.o ]; then
    echo "Compiling vybe_flecs_jank..."
    # Determine jank source path based on platform
    case "$(uname -s)" in
        Darwin)
            JANK_SRC=/Users/pfeodrippe/dev/jank/compiler+runtime
            ;;
        Linux)
            JANK_SRC=$HOME/jank/compiler+runtime
            ;;
    esac
    clang++ -c vendor/vybe/vybe_flecs_jank.cpp -o vendor/vybe/vybe_flecs_jank.o \
        -DIMMER_HAS_LIBGC=1 \
        -I$JANK_SRC/include/cpp \
        -I$JANK_SRC/third-party \
        -I$JANK_SRC/third-party/bdwgc/include \
        -I$JANK_SRC/third-party/immer \
        -I$JANK_SRC/third-party/bpptree/include \
        -I$JANK_SRC/third-party/folly \
        -I$JANK_SRC/third-party/boost-multiprecision/include \
        -I$JANK_SRC/third-party/boost-preprocessor/include \
        -I$JANK_SRC/build/llvm-install/usr/local/include \
        -Ivendor -Ivendor/flecs/distr \
        -std=c++20 -fPIC
fi

# Object files needed for both JIT and linking
OBJ_FILES=(
    vulkan/imgui/imgui.o
    vulkan/imgui/imgui_draw.o
    vulkan/imgui/imgui_widgets.o
    vulkan/imgui/imgui_tables.o
    vulkan/imgui/imgui_impl_sdl3.o
    vulkan/imgui/imgui_impl_vulkan.o
    vulkan/stb_impl.o
    vendor/flecs/distr/flecs.o
    vendor/flecs/distr/flecs_jank_wrapper_native.o
    vendor/vybe/vybe_flecs_jank.o
)

# Build jank arguments for SDF viewer (platform-specific)
case "$(uname -s)" in
    Darwin)
        JANK_ARGS=(
            -I/opt/homebrew/include
            -I/opt/homebrew/include/SDL3
            -I.
            -Ivendor
            -Ivendor/imgui
            -Ivendor/imgui/backends
            -Ivendor/flecs/distr
            -L/opt/homebrew/lib
            --framework Cocoa
            --framework IOKit
            --framework IOSurface
            --framework Metal
            --framework QuartzCore
            --module-path src
        )
        DYLIBS=(
            /opt/homebrew/lib/libvulkan.dylib
            /opt/homebrew/lib/libSDL3.dylib
            /opt/homebrew/lib/libshaderc_shared.dylib
        )
        SHARED_LIB_EXT="dylib"
        ;;
    Linux)
        JANK_ARGS=(
            -I/usr/include
            -I/usr/include/SDL2
            -I.
            -Ivendor
            -Ivendor/imgui
            -Ivendor/imgui/backends
            -Ivendor/flecs/distr
            -L/usr/lib
            -L/usr/lib/x86_64-linux-gnu
            -L/usr/lib/aarch64-linux-gnu
            --module-path src
        )
        # Find SDL2 and Vulkan libraries on Linux
        DYLIBS=()
        for lib in libvulkan.so libSDL2-2.0.so libshaderc_shared.so; do
            lib_path=$(ldconfig -p 2>/dev/null | grep "$lib" | head -1 | awk '{print $NF}')
            if [ -n "$lib_path" ] && [ -f "$lib_path" ]; then
                DYLIBS+=("$lib_path")
            fi
        done
        SHARED_LIB_EXT="so"
        ;;
esac

if [ "$STANDALONE" = true ]; then
    echo "Building standalone executable: $OUTPUT_NAME"

    # Create a shared library from object files for JIT loading and AOT linking
    SHARED_LIB="vulkan/libsdf_deps.$SHARED_LIB_EXT"
    echo "Creating shared library..."

    case "$(uname -s)" in
        Darwin)
            clang++ -dynamiclib -o "$SHARED_LIB" "${OBJ_FILES[@]}" \
                -framework Cocoa -framework IOKit -framework IOSurface -framework Metal -framework QuartzCore \
                -L/opt/homebrew/lib -lvulkan -lSDL3 -lshaderc_shared \
                -Wl,-undefined,dynamic_lookup
            ;;
        Linux)
            clang++ -shared -fPIC -o "$SHARED_LIB" "${OBJ_FILES[@]}" \
                -L/usr/lib -L/usr/lib/x86_64-linux-gnu -L/usr/lib/aarch64-linux-gnu \
                -lvulkan -lSDL2 -lshaderc_shared \
                -Wl,--allow-shlib-undefined
            ;;
    esac

    # JIT needs dylibs (for symbol resolution during compilation)
    for lib in "${DYLIBS[@]}"; do
        JANK_ARGS+=(--jit-lib "$lib")
    done
    JANK_ARGS+=(--jit-lib "$PWD/$SHARED_LIB")

    # JIT also needs object files for symbol resolution during compilation
    for obj in "${OBJ_FILES[@]}"; do
        JANK_ARGS+=(--obj "$obj")
    done

    # Dynamic libraries for AOT linker
    for lib in "${DYLIBS[@]}"; do
        JANK_ARGS+=(--link-lib "$lib")
    done
    JANK_ARGS+=(--link-lib "$PWD/$SHARED_LIB")

    jank "${JANK_ARGS[@]}" compile -o "$OUTPUT_NAME" vybe.sdf

    # Create platform-specific distribution
    case "$(uname -s)" in
        Darwin)
            create_macos_app_bundle "$OUTPUT_NAME" "$OUTPUT_NAME"
            ;;
        Linux)
            create_linux_standalone "$OUTPUT_NAME" "$OUTPUT_NAME"
            ;;
        *)
            echo "Unknown platform, executable at: $OUTPUT_NAME"
            ;;
    esac
else
    # For JIT mode: use --obj and --lib (not -l which expects library names)
    for obj in "${OBJ_FILES[@]}"; do
        JANK_ARGS+=(--obj "$obj")
    done
    for lib in "${DYLIBS[@]}"; do
        JANK_ARGS+=(--lib "$lib")
    done
    jank "${JANK_ARGS[@]}" run-main vybe.sdf -main
fi
