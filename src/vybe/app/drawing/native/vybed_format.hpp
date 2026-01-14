// vybed_format.hpp - .vybed file format for saving Vybe drawings
// Format: Magic + JSON header + PNG thumbnail + binary stroke data
//
// See ai/20260111-vybed-file-format.md for full specification

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <cstring>

namespace animation {
    struct Weave;  // Forward declaration
}

namespace vybe::drawing {

// =============================================================================
// Constants
// =============================================================================

constexpr char VYBED_MAGIC[8] = {'V','Y','B','E','D','0','0','1'};
constexpr uint32_t VYBED_VERSION = 1;

// =============================================================================
// File Info (for gallery display)
// =============================================================================

struct VybedFileInfo {
    std::string path;
    std::string name;
    std::string created;
    std::string modified;
    int threadCount;
    int totalFrames;
    bool hasThumbnail;
};

// =============================================================================
// API
// =============================================================================

// Save weave to .vybed file
// thumbnail_png/thumbnail_size: Optional PNG data for gallery preview (can be nullptr/0)
// Returns true on success
bool save_vybed(const std::string& path,
                const animation::Weave& weave,
                const std::string& name,
                const uint8_t* thumbnail_png = nullptr,
                size_t thumbnail_size = 0);

// Load weave from .vybed file
// Returns true on success
bool load_vybed(const std::string& path,
                animation::Weave& weave,
                std::string& name_out);

// Load only the thumbnail (fast, for gallery)
// Returns true if thumbnail exists and was loaded
bool load_vybed_thumbnail(const std::string& path,
                          std::vector<uint8_t>& thumbnail_out);

// Load file info without loading stroke data
bool load_vybed_info(const std::string& path, VybedFileInfo& info);

// List all .vybed files in directory
std::vector<std::string> list_vybed_files(const std::string& directory);

// Generate default filename based on timestamp
std::string generate_vybed_filename();

// =============================================================================
// Binary Helpers (Little Endian)
// =============================================================================

inline void write_u8(std::ostream& os, uint8_t value) {
    os.write(reinterpret_cast<const char*>(&value), 1);
}

inline void write_u16(std::ostream& os, uint16_t value) {
    os.write(reinterpret_cast<const char*>(&value), 2);
}

inline void write_u32(std::ostream& os, uint32_t value) {
    os.write(reinterpret_cast<const char*>(&value), 4);
}

inline void write_i32(std::ostream& os, int32_t value) {
    os.write(reinterpret_cast<const char*>(&value), 4);
}

inline void write_f32(std::ostream& os, float value) {
    os.write(reinterpret_cast<const char*>(&value), 4);
}

inline uint8_t read_u8(std::istream& is) {
    uint8_t value;
    is.read(reinterpret_cast<char*>(&value), 1);
    return value;
}

inline uint16_t read_u16(std::istream& is) {
    uint16_t value;
    is.read(reinterpret_cast<char*>(&value), 2);
    return value;
}

inline uint32_t read_u32(std::istream& is) {
    uint32_t value;
    is.read(reinterpret_cast<char*>(&value), 4);
    return value;
}

inline int32_t read_i32(std::istream& is) {
    int32_t value;
    is.read(reinterpret_cast<char*>(&value), 4);
    return value;
}

inline float read_f32(std::istream& is) {
    float value;
    is.read(reinterpret_cast<char*>(&value), 4);
    return value;
}

} // namespace vybe::drawing

// =============================================================================
// C API for JIT integration
// =============================================================================

extern "C" {

// Save current weave to file (returns 1 on success, 0 on failure)
int vybed_save(const char* path, const char* name);

// Load weave from file (returns 1 on success, 0 on failure)
int vybed_load(const char* path);

// Get thumbnail data (returns size, fills buffer if not null)
int vybed_get_thumbnail(const char* path, uint8_t* buffer, int buffer_size);

// List files in directory (returns count, fills paths array)
int vybed_list_files(const char* directory, char** paths, int max_paths);

// Generate default filename
const char* vybed_generate_filename();

// Get last error message
const char* vybed_get_error();

} // extern "C"
