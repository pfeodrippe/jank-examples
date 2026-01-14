// vybed_format.cpp - .vybed file format implementation
// See ai/20260111-vybed-file-format.md for specification

#include "vybed_format.hpp"
#include "animation_thread.h"
#include <fstream>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <filesystem>

#ifdef __APPLE__
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace vybe::drawing {

// Thread-local error message
static thread_local std::string g_lastError;

static void setError(const std::string& msg) {
    g_lastError = msg;
    fprintf(stderr, "[vybed] Error: %s\n", msg.c_str());
}

// =============================================================================
// JSON Generation (minimal, no external dependency)
// =============================================================================

static std::string escapeJsonString(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c;
        }
    }
    return result;
}

static std::string getCurrentTimestamp() {
    time_t now = time(nullptr);
    struct tm* t = gmtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", t);
    return buf;
}

static std::string playModeToString(animation::PlayMode mode) {
    switch (mode) {
        case animation::PlayMode::Forward: return "forward";
        case animation::PlayMode::Backward: return "backward";
        case animation::PlayMode::PingPong: return "pingpong";
        case animation::PlayMode::Random: return "random";
        default: return "forward";
    }
}

static animation::PlayMode stringToPlayMode(const std::string& s) {
    if (s == "backward") return animation::PlayMode::Backward;
    if (s == "pingpong") return animation::PlayMode::PingPong;
    if (s == "random") return animation::PlayMode::Random;
    return animation::PlayMode::Forward;
}

static std::string onionModeToString(animation::OnionSkinMode mode) {
    switch (mode) {
        case animation::OnionSkinMode::Off: return "off";
        case animation::OnionSkinMode::Before: return "before";
        case animation::OnionSkinMode::After: return "after";
        case animation::OnionSkinMode::Both: return "both";
        default: return "off";
    }
}

static animation::OnionSkinMode stringToOnionMode(const std::string& s) {
    if (s == "before") return animation::OnionSkinMode::Before;
    if (s == "after") return animation::OnionSkinMode::After;
    if (s == "both") return animation::OnionSkinMode::Both;
    return animation::OnionSkinMode::Off;
}

// Generate JSON header for the weave
static std::string generateJsonHeader(const animation::Weave& weave, const std::string& name) {
    std::ostringstream json;
    std::string timestamp = getCurrentTimestamp();

    json << "{\n";
    json << "  \"version\": " << VYBED_VERSION << ",\n";
    json << "  \"name\": \"" << escapeJsonString(name) << "\",\n";
    json << "  \"created\": \"" << timestamp << "\",\n";
    json << "  \"modified\": \"" << timestamp << "\",\n";
    json << "  \"app_version\": \"1.0.0\",\n";

    // Canvas
    json << "  \"canvas\": {\n";
    json << "    \"width\": " << weave.canvasWidth << ",\n";
    json << "    \"height\": " << weave.canvasHeight << ",\n";
    json << "    \"background\": [" << weave.backgroundColor[0] << ", "
         << weave.backgroundColor[1] << ", " << weave.backgroundColor[2] << ", "
         << weave.backgroundColor[3] << "]\n";
    json << "  },\n";

    // Playback
    json << "  \"playback\": {\n";
    json << "    \"global_speed\": " << weave.globalSpeed << ",\n";
    json << "    \"is_playing\": " << (weave.isPlaying ? "true" : "false") << "\n";
    json << "  },\n";

    // Onion skin
    json << "  \"onion_skin\": {\n";
    json << "    \"mode\": \"" << onionModeToString(weave.onionSkinMode) << "\",\n";
    json << "    \"before\": " << weave.onionSkinBefore << ",\n";
    json << "    \"after\": " << weave.onionSkinAfter << ",\n";
    json << "    \"opacity\": " << weave.onionSkinOpacity << "\n";
    json << "  },\n";

    // Active thread
    json << "  \"active_thread\": " << weave.activeThreadIndex << ",\n";

    // Threads (metadata only, stroke data offset/size computed during write)
    json << "  \"threads\": [\n";
    for (size_t i = 0; i < weave.threads.size(); i++) {
        const auto& thread = weave.threads[i];
        json << "    {\n";
        json << "      \"id\": " << i << ",\n";
        json << "      \"name\": \"" << escapeJsonString(thread.name) << "\",\n";
        json << "      \"color\": [" << thread.style.r << ", " << thread.style.g << ", "
             << thread.style.b << ", " << thread.style.a << "],\n";
        json << "      \"line_width\": " << thread.style.lineWidth << ",\n";
        json << "      \"opacity\": " << thread.style.opacity << ",\n";
        json << "      \"fps\": " << thread.fps << ",\n";
        json << "      \"play_mode\": \"" << playModeToString(thread.playMode) << "\",\n";
        json << "      \"visible\": " << (thread.visible ? "true" : "false") << ",\n";
        json << "      \"locked\": " << (thread.locked ? "true" : "false") << ",\n";
        json << "      \"frame_count\": " << thread.frames.size() << "\n";
        json << "    }";
        if (i < weave.threads.size() - 1) json << ",";
        json << "\n";
    }
    json << "  ]\n";
    json << "}";

    return json.str();
}

// =============================================================================
// Binary Stroke Data Serialization
// =============================================================================

static void writeStrokeBrush(std::ostream& os, const animation::StrokeBrush& brush) {
    write_i32(os, brush.brushType);
    write_f32(os, brush.size);
    write_f32(os, brush.hardness);
    write_f32(os, brush.opacity);
    write_f32(os, brush.spacing);
    write_i32(os, brush.shapeTextureId);
    write_i32(os, brush.grainTextureId);
    write_f32(os, brush.grainScale);
    write_u8(os, brush.shapeInverted ? 1 : 0);
    write_f32(os, brush.sizePressure);
    write_f32(os, brush.opacityPressure);
    write_f32(os, brush.sizeJitter);
    write_f32(os, brush.opacityJitter);
    write_f32(os, brush.rotationJitter);
    write_f32(os, brush.scatter);
}

static void readStrokeBrush(std::istream& is, animation::StrokeBrush& brush) {
    brush.brushType = read_i32(is);
    brush.size = read_f32(is);
    brush.hardness = read_f32(is);
    brush.opacity = read_f32(is);
    brush.spacing = read_f32(is);
    brush.shapeTextureId = read_i32(is);
    brush.grainTextureId = read_i32(is);
    brush.grainScale = read_f32(is);
    brush.shapeInverted = read_u8(is) != 0;
    brush.sizePressure = read_f32(is);
    brush.opacityPressure = read_f32(is);
    brush.sizeJitter = read_f32(is);
    brush.opacityJitter = read_f32(is);
    brush.rotationJitter = read_f32(is);
    brush.scatter = read_f32(is);
}

static void writeStrokeData(std::ostream& os, const animation::Weave& weave) {
    auto startPos = os.tellp();
    int totalStrokes = 0;
    int totalPoints = 0;

    // For each thread
    for (const auto& thread : weave.threads) {
        // Number of frames
        write_u16(os, static_cast<uint16_t>(thread.frames.size()));

        // For each frame
        for (const auto& frame : thread.frames) {
            // Number of strokes
            write_u16(os, static_cast<uint16_t>(frame.strokes.size()));
            totalStrokes += frame.strokes.size();

            // For each stroke
            for (const auto& stroke : frame.strokes) {
                totalPoints += stroke.points.size();

                // Point count
                write_u16(os, static_cast<uint16_t>(stroke.points.size()));

                // Color RGBA
                write_f32(os, stroke.r);
                write_f32(os, stroke.g);
                write_f32(os, stroke.b);
                write_f32(os, stroke.a);

                // Brush data
                writeStrokeBrush(os, stroke.brush);

                // Points
                for (const auto& pt : stroke.points) {
                    write_f32(os, pt.x);
                    write_f32(os, pt.y);
                    write_f32(os, pt.pressure);
                    write_f32(os, pt.timestamp);
                }
            }
        }
    }

    auto endPos = os.tellp();
    auto strokeDataSize = endPos - startPos;
    printf("[vybed] Stroke data: %d strokes, %d points, %lld bytes written\n",
           totalStrokes, totalPoints, (long long)strokeDataSize);
}

static bool readStrokeData(std::istream& is, animation::Weave& weave) {
    // For each thread (must match JSON header thread count)
    for (auto& thread : weave.threads) {
        uint16_t frameCount = read_u16(is);
        if (is.fail()) return false;

        thread.frames.clear();
        thread.frames.resize(frameCount);

        // For each frame
        for (auto& frame : thread.frames) {
            uint16_t strokeCount = read_u16(is);
            if (is.fail()) return false;

            frame.strokes.reserve(strokeCount);

            // For each stroke
            for (uint16_t s = 0; s < strokeCount; s++) {
                animation::AnimStroke stroke;

                uint16_t pointCount = read_u16(is);
                if (is.fail()) return false;

                // Color
                stroke.r = read_f32(is);
                stroke.g = read_f32(is);
                stroke.b = read_f32(is);
                stroke.a = read_f32(is);

                // Brush
                readStrokeBrush(is, stroke.brush);

                // Points
                stroke.points.reserve(pointCount);
                for (uint16_t p = 0; p < pointCount; p++) {
                    animation::StrokePoint pt;
                    pt.x = read_f32(is);
                    pt.y = read_f32(is);
                    pt.pressure = read_f32(is);
                    pt.timestamp = read_f32(is);
                    stroke.points.push_back(pt);
                }

                // Update bounds
                stroke.updateBounds();

                frame.strokes.push_back(std::move(stroke));
            }
        }
    }

    return !is.fail();
}

// =============================================================================
// Simple JSON Parsing (minimal, just for our format)
// =============================================================================

static std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\": \"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\":\"";  // No space
        pos = json.find(search);
    }
    if (pos == std::string::npos) return "";

    pos += search.length();
    size_t end = json.find("\"", pos);
    if (end == std::string::npos) return "";

    return json.substr(pos, end - pos);
}

static int extractJsonInt(const std::string& json, const std::string& key, int defaultVal = 0) {
    std::string search = "\"" + key + "\": ";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\":";
        pos = json.find(search);
    }
    if (pos == std::string::npos) return defaultVal;

    pos += search.length();
    return std::atoi(json.c_str() + pos);
}

static float extractJsonFloat(const std::string& json, const std::string& key, float defaultVal = 0.0f) {
    std::string search = "\"" + key + "\": ";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\":";
        pos = json.find(search);
    }
    if (pos == std::string::npos) return defaultVal;

    pos += search.length();
    return static_cast<float>(std::atof(json.c_str() + pos));
}

static bool extractJsonBool(const std::string& json, const std::string& key, bool defaultVal = false) {
    std::string search = "\"" + key + "\": ";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\":";
        pos = json.find(search);
    }
    if (pos == std::string::npos) return defaultVal;

    pos += search.length();
    return json.substr(pos, 4) == "true";
}

static std::vector<float> extractJsonFloatArray(const std::string& json, const std::string& key) {
    std::vector<float> result;
    std::string search = "\"" + key + "\": [";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\":[";
        pos = json.find(search);
    }
    if (pos == std::string::npos) return result;

    pos += search.length();
    size_t end = json.find("]", pos);
    if (end == std::string::npos) return result;

    std::string arrayStr = json.substr(pos, end - pos);

    // Parse comma-separated floats
    std::istringstream iss(arrayStr);
    std::string token;
    while (std::getline(iss, token, ',')) {
        result.push_back(static_cast<float>(std::atof(token.c_str())));
    }

    return result;
}

// Extract thread objects from JSON
static std::vector<std::string> extractJsonThreads(const std::string& json) {
    std::vector<std::string> threads;

    size_t pos = json.find("\"threads\": [");
    if (pos == std::string::npos) {
        pos = json.find("\"threads\":[");
    }
    if (pos == std::string::npos) return threads;

    pos = json.find("[", pos);
    if (pos == std::string::npos) return threads;
    pos++;

    // Find each thread object
    int braceCount = 0;
    size_t threadStart = 0;
    bool inThread = false;

    for (size_t i = pos; i < json.length(); i++) {
        char c = json[i];

        if (c == '{') {
            if (!inThread) {
                inThread = true;
                threadStart = i;
            }
            braceCount++;
        } else if (c == '}') {
            braceCount--;
            if (braceCount == 0 && inThread) {
                threads.push_back(json.substr(threadStart, i - threadStart + 1));
                inThread = false;
            }
        } else if (c == ']' && !inThread) {
            break;  // End of threads array
        }
    }

    return threads;
}

static bool parseJsonHeader(const std::string& json, animation::Weave& weave, std::string& name) {
    name = extractJsonString(json, "name");

    // Canvas
    weave.canvasWidth = extractJsonInt(json, "width", 1024);
    weave.canvasHeight = extractJsonInt(json, "height", 1024);

    auto bgColor = extractJsonFloatArray(json, "background");
    if (bgColor.size() >= 4) {
        weave.backgroundColor[0] = bgColor[0];
        weave.backgroundColor[1] = bgColor[1];
        weave.backgroundColor[2] = bgColor[2];
        weave.backgroundColor[3] = bgColor[3];
    }

    // Playback
    weave.globalSpeed = extractJsonFloat(json, "global_speed", 1.0f);
    weave.isPlaying = false;  // Always start paused

    // Onion skin
    std::string onionMode = extractJsonString(json, "mode");
    // Find the onion_skin section to parse mode correctly
    size_t onionPos = json.find("\"onion_skin\"");
    if (onionPos != std::string::npos) {
        std::string onionSection = json.substr(onionPos);
        weave.onionSkinMode = stringToOnionMode(extractJsonString(onionSection, "mode"));
        weave.onionSkinBefore = extractJsonInt(onionSection, "before", 2);
        weave.onionSkinAfter = extractJsonInt(onionSection, "after", 2);
        weave.onionSkinOpacity = extractJsonFloat(onionSection, "opacity", 0.3f);
    }

    weave.activeThreadIndex = extractJsonInt(json, "active_thread", 0);

    // Parse threads
    auto threadJsons = extractJsonThreads(json);
    weave.threads.clear();

    for (const auto& threadJson : threadJsons) {
        animation::AnimThread thread;
        thread.name = extractJsonString(threadJson, "name");

        auto color = extractJsonFloatArray(threadJson, "color");
        if (color.size() >= 4) {
            thread.style.r = color[0];
            thread.style.g = color[1];
            thread.style.b = color[2];
            thread.style.a = color[3];
        }

        thread.style.lineWidth = extractJsonFloat(threadJson, "line_width", 3.0f);
        thread.style.opacity = extractJsonFloat(threadJson, "opacity", 1.0f);
        thread.fps = extractJsonFloat(threadJson, "fps", 12.0f);
        thread.playMode = stringToPlayMode(extractJsonString(threadJson, "play_mode"));
        thread.visible = extractJsonBool(threadJson, "visible", true);
        thread.locked = extractJsonBool(threadJson, "locked", false);

        // Frame count is used to pre-size, but actual frames come from binary data
        int frameCount = extractJsonInt(threadJson, "frame_count", 1);
        thread.frames.resize(frameCount);

        weave.threads.push_back(std::move(thread));
    }

    // Ensure at least one thread
    if (weave.threads.empty()) {
        weave.addThread();
    }

    return true;
}

// =============================================================================
// Public API Implementation
// =============================================================================

bool save_vybed(const std::string& path,
                const animation::Weave& weave,
                const std::string& name,
                const uint8_t* thumbnail_png,
                size_t thumbnail_size) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        setError("Failed to open file for writing: " + path);
        return false;
    }

    // Magic number
    file.write(VYBED_MAGIC, 8);

    // Generate JSON header
    std::string jsonHeader = generateJsonHeader(weave, name);

    // Header size
    write_u32(file, static_cast<uint32_t>(jsonHeader.size()));

    // JSON header
    file.write(jsonHeader.c_str(), jsonHeader.size());

    // Thumbnail size and data
    write_u32(file, static_cast<uint32_t>(thumbnail_size));
    if (thumbnail_size > 0 && thumbnail_png != nullptr) {
        file.write(reinterpret_cast<const char*>(thumbnail_png), thumbnail_size);
    }

    // Binary stroke data
    writeStrokeData(file, weave);

    if (!file) {
        setError("Failed to write file: " + path);
        return false;
    }

    printf("[vybed] Saved %zu threads, %zu bytes to %s\n",
           weave.threads.size(), file.tellp(), path.c_str());
    return true;
}

bool load_vybed(const std::string& path,
                animation::Weave& weave,
                std::string& name_out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        setError("Failed to open file: " + path);
        return false;
    }

    // Check magic
    char magic[8];
    file.read(magic, 8);
    if (memcmp(magic, VYBED_MAGIC, 8) != 0) {
        setError("Invalid file format (bad magic)");
        return false;
    }

    // Read header size
    uint32_t headerSize = read_u32(file);
    if (headerSize > 1024 * 1024) {  // Sanity check: 1MB max header
        setError("Invalid header size");
        return false;
    }

    // Read JSON header
    std::string jsonHeader(headerSize, '\0');
    file.read(&jsonHeader[0], headerSize);

    // Parse JSON
    if (!parseJsonHeader(jsonHeader, weave, name_out)) {
        setError("Failed to parse JSON header");
        return false;
    }

    // Skip thumbnail
    uint32_t thumbnailSize = read_u32(file);
    file.seekg(thumbnailSize, std::ios::cur);

    // Read stroke data
    if (!readStrokeData(file, weave)) {
        setError("Failed to read stroke data");
        return false;
    }

    // Reset playback state
    weave.isPlaying = false;
    for (auto& thread : weave.threads) {
        thread.currentFrameIndex = 0;
        thread.frameAccumulator = 0;
        thread.pingPongDirection = 1;
    }

    printf("[vybed] Loaded %zu threads from %s\n", weave.threads.size(), path.c_str());
    return true;
}

bool load_vybed_thumbnail(const std::string& path,
                          std::vector<uint8_t>& thumbnail_out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    // Check magic
    char magic[8];
    file.read(magic, 8);
    if (memcmp(magic, VYBED_MAGIC, 8) != 0) return false;

    // Skip header
    uint32_t headerSize = read_u32(file);
    file.seekg(headerSize, std::ios::cur);

    // Read thumbnail
    uint32_t thumbnailSize = read_u32(file);
    if (thumbnailSize == 0) return false;

    thumbnail_out.resize(thumbnailSize);
    file.read(reinterpret_cast<char*>(thumbnail_out.data()), thumbnailSize);

    return !file.fail();
}

bool load_vybed_info(const std::string& path, VybedFileInfo& info) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    // Check magic
    char magic[8];
    file.read(magic, 8);
    if (memcmp(magic, VYBED_MAGIC, 8) != 0) return false;

    // Read header
    uint32_t headerSize = read_u32(file);
    if (headerSize > 1024 * 1024) return false;

    std::string jsonHeader(headerSize, '\0');
    file.read(&jsonHeader[0], headerSize);

    // Extract info
    info.path = path;
    info.name = extractJsonString(jsonHeader, "name");
    info.created = extractJsonString(jsonHeader, "created");
    info.modified = extractJsonString(jsonHeader, "modified");

    // Count threads and frames
    auto threadJsons = extractJsonThreads(jsonHeader);
    info.threadCount = static_cast<int>(threadJsons.size());
    info.totalFrames = 0;
    for (const auto& t : threadJsons) {
        info.totalFrames += extractJsonInt(t, "frame_count", 0);
    }

    // Check thumbnail
    uint32_t thumbnailSize = read_u32(file);
    info.hasThumbnail = (thumbnailSize > 0);

    return true;
}

std::vector<std::string> list_vybed_files(const std::string& directory) {
    std::vector<std::string> files;

#ifdef __APPLE__
    DIR* dir = opendir(directory.c_str());
    if (!dir) return files;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.length() > 6 && name.substr(name.length() - 6) == ".vybed") {
            files.push_back(directory + "/" + name);
        }
    }
    closedir(dir);
#else
    // Fallback using C++17 filesystem (may not work on all iOS)
    try {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().extension() == ".vybed") {
                files.push_back(entry.path().string());
            }
        }
    } catch (...) {}
#endif

    return files;
}

std::string generate_vybed_filename() {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char buf[64];
    strftime(buf, sizeof(buf), "Drawing_%Y%m%d_%H%M%S.vybed", t);
    return buf;
}

} // namespace vybe::drawing

// =============================================================================
// C API Implementation
// =============================================================================

static std::string g_generatedFilename;

extern "C" {

int vybed_save(const char* path, const char* name) {
    auto& weave = animation::getCurrentWeave();
    return vybe::drawing::save_vybed(path, weave, name ? name : "Untitled") ? 1 : 0;
}

int vybed_load(const char* path) {
    auto& weave = animation::getCurrentWeave();
    std::string name;
    bool result = vybe::drawing::load_vybed(path, weave, name);
    if (result) {
        weave.invalidateAllCaches();
    }
    return result ? 1 : 0;
}

int vybed_get_thumbnail(const char* path, uint8_t* buffer, int buffer_size) {
    std::vector<uint8_t> thumbnail;
    if (!vybe::drawing::load_vybed_thumbnail(path, thumbnail)) {
        return 0;
    }

    int size = static_cast<int>(thumbnail.size());
    if (buffer != nullptr && buffer_size >= size) {
        memcpy(buffer, thumbnail.data(), size);
    }
    return size;
}

int vybed_list_files(const char* directory, char** paths, int max_paths) {
    auto files = vybe::drawing::list_vybed_files(directory);
    int count = std::min(static_cast<int>(files.size()), max_paths);

    if (paths != nullptr) {
        for (int i = 0; i < count; i++) {
            // Note: caller must free these strings
            paths[i] = strdup(files[i].c_str());
        }
    }

    return static_cast<int>(files.size());
}

const char* vybed_generate_filename() {
    g_generatedFilename = vybe::drawing::generate_vybed_filename();
    return g_generatedFilename.c_str();
}

const char* vybed_get_error() {
    // g_lastError is defined in vybe::drawing namespace at the top of this file
    return "";  // Simplified - error logging goes to stderr via setError()
}

} // extern "C"
