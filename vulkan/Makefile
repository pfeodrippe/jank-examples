# Vulkan SDF Editor Makefile

CXX = clang++
IMGUI_DIR = ../vendor/imgui
CXXFLAGS = -std=c++17 -O2 -I/opt/homebrew/include -I/opt/homebrew/include/SDL3 -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends
LDFLAGS = -L/opt/homebrew/lib -lvulkan -lSDL3 -lshaderc_shared \
          -framework Cocoa -framework IOKit -framework IOSurface -framework Metal -framework QuartzCore

# ImGui object files
IMGUI_OBJS = imgui/imgui.o imgui/imgui_draw.o imgui/imgui_widgets.o imgui/imgui_tables.o \
             imgui/imgui_impl_sdl3.o imgui/imgui_impl_vulkan.o

GLSLC = glslangValidator

# Vulkan ICD for MoltenVK
export VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export DYLD_LIBRARY_PATH=/opt/homebrew/lib

.PHONY: all clean run live test

# Default: build everything
all: sdf_renderer sdf_live sdf_raymarch.spv sdf_scene.spv blit.vert.spv blit.frag.spv

# Live editor (main target)
live: sdf_live sdf_scene.spv blit.vert.spv blit.frag.spv
	./sdf_live

# Static renderer
run: sdf_renderer sdf_raymarch.spv output
	./sdf_renderer
	open output/sdf_vulkan.ppm

# Executables
sdf_renderer: sdf_renderer.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

sdf_live: sdf_live.cpp $(IMGUI_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(IMGUI_OBJS) $(LDFLAGS)

# Shaders
sdf_raymarch.spv: sdf_raymarch.comp
	$(GLSLC) -V $< -o $@

sdf_scene.spv: sdf_scene.comp
	$(GLSLC) -V $< -o $@

blit.vert.spv: blit.vert
	$(GLSLC) -V $< -o $@

blit.frag.spv: blit.frag
	$(GLSLC) -V $< -o $@

# Shader reload (for manual reload during development)
shader: sdf_scene.spv
	@echo "Shader recompiled"

output:
	mkdir -p output

# Test basic Vulkan
test: vk_test
	./vk_test

vk_test: vk_test.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f sdf_renderer sdf_live vk_test *.spv
	rm -rf output

# Help
help:
	@echo "SDF Editor Build Targets:"
	@echo "  make live   - Build and run the live editor (main)"
	@echo "  make run    - Build and run static renderer"
	@echo "  make shader - Recompile scene shader only"
	@echo "  make test   - Test Vulkan setup"
	@echo "  make clean  - Remove build artifacts"
