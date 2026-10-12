#pragma once

#ifdef USE_LVGL
#include "lvgl.h"
#include <string>

namespace crosspad_gui {

/// One row of the explorer: a path as the explorer was given it (the folder it
/// lists, plus the name), never the platform's file system path.
struct FileItem {
    std::string path;
    std::string name;
    bool isFolder;
};

} // namespace crosspad_gui

// File type filter
enum FileFilter {
    FILTER_NONE   = 0,   // Show folders only
    FILTER_AUDIO  = 1,   // .wav, .mp3
    FILTER_IMAGE  = 2,   // .png, .bmp, .gif
    FILTER_CONFIG = 3,   // .txt, .json
    FILTER_ALL    = 4    // All files
};

// Get the currently selected file (valid after load callback)
crosspad_gui::FileItem GetCurrentFile();

// Get the currently selected folder
crosspad_gui::FileItem GetCurrentFolder();

// Create a file explorer widget
// @param parent     Parent LVGL object
// @param fileFilter Which file types to show
// @param startPath  Initial directory path
// @param cb         Callback for Load button (receives LV_EVENT_CLICKED)
// @param user_data  User data passed to the callback
// @return Container object, or NULL if already opened
lv_obj_t * lv_CreateFileExplorer(lv_obj_t * parent, FileFilter fileFilter,
                                  const std::string & startPath,
                                  lv_event_cb_t cb, void * user_data = nullptr);

// Optional: set a callback to preview files on focus (e.g. audio playback)
// @param cb Called with the full file path when a file is focused/clicked,
//           and with nullptr when the explorer closes (stop the audition)
typedef void (*file_preview_cb_t)(const char * path);
void file_explorer_set_preview_callback(file_preview_cb_t cb);

// Optional: set callbacks for scan lifecycle (e.g. pause/resume background tasks)
typedef void (*file_scan_lifecycle_cb_t)();
void file_explorer_set_scan_callbacks(file_scan_lifecycle_cb_t beforeScan,
                                       file_scan_lifecycle_cb_t afterScan);

#endif // USE_LVGL
