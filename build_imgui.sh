#!/bin/bash
set -e

cd "$(dirname "$0")"

echo "=== Building ImGui + rlImGui ==="

# Create output directory
mkdir -p vendor/imgui/build

# Compile ImGui core files
echo "Compiling ImGui core..."
clang++ -c -O2 -std=c++17 -DNDEBUG \
    -I vendor/imgui \
    vendor/imgui/imgui.cpp \
    -o vendor/imgui/build/imgui.o

clang++ -c -O2 -std=c++17 -DNDEBUG \
    -I vendor/imgui \
    vendor/imgui/imgui_draw.cpp \
    -o vendor/imgui/build/imgui_draw.o

clang++ -c -O2 -std=c++17 -DNDEBUG \
    -I vendor/imgui \
    vendor/imgui/imgui_tables.cpp \
    -o vendor/imgui/build/imgui_tables.o

clang++ -c -O2 -std=c++17 -DNDEBUG \
    -I vendor/imgui \
    vendor/imgui/imgui_widgets.cpp \
    -o vendor/imgui/build/imgui_widgets.o

clang++ -c -O2 -std=c++17 -DNDEBUG \
    -I vendor/imgui \
    vendor/imgui/imgui_demo.cpp \
    -o vendor/imgui/build/imgui_demo.o

# Compile rlImGui (raylib integration)
echo "Compiling rlImGui..."
clang++ -c -O2 -std=c++17 -DNDEBUG \
    -DNO_FONT_AWESOME \
    -I vendor/imgui \
    -I vendor/raylib/src \
    vendor/rlImGui/rlImGui.cpp \
    -o vendor/imgui/build/rlImGui.o

echo ""
echo "=== Build complete ==="
echo ""
echo "Object files created in vendor/imgui/build/"
ls -la vendor/imgui/build/*.o
