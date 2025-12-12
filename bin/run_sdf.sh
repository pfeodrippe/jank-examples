#!/bin/bash
# SDF Vulkan Viewer - Runs vybe.sdf
set -e
cd "$(dirname "$0")/.."

export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
export PATH="/Users/pfeodrippe/dev/jank/compiler+runtime/build:/usr/bin:/bin:$PATH"

# Vulkan environment for MoltenVK
export VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export DYLD_LIBRARY_PATH=/opt/homebrew/lib

# Build imgui objects if needed
if [ ! -f vulkan/imgui/imgui.o ]; then
    echo "Building imgui..."
    make -C vulkan/imgui
fi

# Compile blit shaders if needed
GLSLC=glslangValidator
if [ ! -f vulkan_kim/blit.vert.spv ] || [ vulkan_kim/blit.vert -nt vulkan_kim/blit.vert.spv ]; then
    echo "Compiling blit.vert..."
    $GLSLC -V vulkan_kim/blit.vert -o vulkan_kim/blit.vert.spv
fi
if [ ! -f vulkan_kim/blit.frag.spv ] || [ vulkan_kim/blit.frag -nt vulkan_kim/blit.frag.spv ]; then
    echo "Compiling blit.frag..."
    $GLSLC -V vulkan_kim/blit.frag -o vulkan_kim/blit.frag.spv
fi

# Build jank arguments for SDF viewer
JANK_ARGS=(
    -I/opt/homebrew/include
    -I/opt/homebrew/include/SDL3
    -I.
    -Ivendor/imgui
    -Ivendor/imgui/backends
    -L/opt/homebrew/lib
    -l/opt/homebrew/lib/libvulkan.dylib
    -l/opt/homebrew/lib/libSDL3.dylib
    -l/opt/homebrew/lib/libshaderc_shared.dylib
    --obj vulkan/imgui/imgui.o
    --obj vulkan/imgui/imgui_draw.o
    --obj vulkan/imgui/imgui_widgets.o
    --obj vulkan/imgui/imgui_tables.o
    --obj vulkan/imgui/imgui_impl_sdl3.o
    --obj vulkan/imgui/imgui_impl_vulkan.o
    --framework Cocoa
    --framework IOKit
    --framework IOSurface
    --framework Metal
    --framework QuartzCore
    --module-path src
    run-main vybe.sdf -main
)

# Run SDF viewer
jank "${JANK_ARGS[@]}"
