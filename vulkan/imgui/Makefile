# ImGui Makefile for Vulkan/SDL3 backend

IMGUI_DIR = ../../vendor/imgui
CXX = clang++

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    # macOS - use Homebrew paths
    CXXFLAGS = -O2 -std=c++17 -fPIC -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -I/opt/homebrew/include -I/opt/homebrew/include/SDL3
else
    # Linux - use pkg-config or standard paths
    CXXFLAGS = -O2 -std=c++17 -fPIC -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(shell pkg-config --cflags sdl3 2>/dev/null || echo "-I/usr/local/include -I/usr/local/include/SDL3")
endif

OBJS = imgui.o imgui_draw.o imgui_widgets.o imgui_tables.o imgui_impl_sdl3.o imgui_impl_vulkan.o

all: $(OBJS)

imgui.o: $(IMGUI_DIR)/imgui.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

imgui_draw.o: $(IMGUI_DIR)/imgui_draw.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

imgui_widgets.o: $(IMGUI_DIR)/imgui_widgets.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

imgui_tables.o: $(IMGUI_DIR)/imgui_tables.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

imgui_impl_sdl3.o: $(IMGUI_DIR)/backends/imgui_impl_sdl3.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

imgui_impl_vulkan.o: $(IMGUI_DIR)/backends/imgui_impl_vulkan.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

.PHONY: all clean
