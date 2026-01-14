// file_manager_ios.h - iOS file operations for .vybed files
// Handles Documents directory, file listing, and path management

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Get the app's Documents directory path (persistent, backed up to iCloud)
// Returns: null-terminated string (do not free)
const char* ios_get_documents_path(void);

// Get the Drawings subdirectory (creates if needed)
// Returns: null-terminated string (do not free)
const char* ios_get_drawings_path(void);

// Ensure the Drawings directory exists
// Returns: 1 on success, 0 on failure
int ios_ensure_drawings_directory(void);

// Save current weave to Documents/Drawings/
// name: Drawing name (will generate filename from this)
// Returns: full path to saved file, or NULL on failure
const char* ios_save_drawing(const char* name);

// Load a drawing from path
// Returns: 1 on success, 0 on failure
int ios_load_drawing(const char* path);

// Get list of saved drawings
// Returns: number of files found
// paths: array to fill with file paths (caller allocates)
// max_paths: maximum number of paths to return
int ios_list_drawings(char** paths, int max_paths);

// Delete a drawing file
// Returns: 1 on success, 0 on failure
int ios_delete_drawing(const char* path);

// Rename a drawing (changes name in file metadata, not filename)
// Returns: 1 on success, 0 on failure
int ios_rename_drawing(const char* path, const char* new_name);

// Get the name of a drawing from its .vybed file
// Returns: null-terminated string (do not free), or NULL if not found
const char* ios_get_drawing_name(const char* path);

// Capture current frame as PNG thumbnail
// buffer: output buffer for PNG data (if NULL, just returns required size)
// buffer_size: size of buffer
// Returns: actual size of PNG data, or 0 on failure
int ios_capture_thumbnail(unsigned char* buffer, int buffer_size);

#ifdef __cplusplus
}
#endif
