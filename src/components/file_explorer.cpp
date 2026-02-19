#ifdef USE_LVGL

#include "crosspad-gui/components/file_explorer.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad-gui/widgets/footer_button.h"
#include "crosspad-gui/platform/IFileSystem.h"

#include <vector>
#include <algorithm>
#include <cstring>

extern lv_indev_t * indev_encoderTop;

static lv_group_t * file_explorer_group = nullptr;
static lv_obj_t * itemList = NULL;
static lv_obj_t * loadButton = NULL;

#define MAX_PATH_LEN 128

static crosspad_gui::FileItem currentFile;
static crosspad_gui::FileItem currentFolder;
static lv_subject_t Folder;
static std::vector<crosspad_gui::FileItem> entries;

static bool alreadyOpened = false;
static FileFilter _fileFilter;

// Optional callbacks
static file_preview_cb_t s_previewCb = nullptr;
static file_scan_lifecycle_cb_t s_beforeScanCb = nullptr;
static file_scan_lifecycle_cb_t s_afterScanCb = nullptr;

void file_explorer_set_preview_callback(file_preview_cb_t cb) {
    s_previewCb = cb;
}

void file_explorer_set_scan_callbacks(file_scan_lifecycle_cb_t beforeScan,
                                       file_scan_lifecycle_cb_t afterScan) {
    s_beforeScanCb = beforeScan;
    s_afterScanCb = afterScan;
}

crosspad_gui::FileItem GetCurrentFile() { return currentFile; }
crosspad_gui::FileItem GetCurrentFolder() { return currentFolder; }

// Forward declarations
static bool IndexDirectory(const std::string & path);
static void UpdateEntriesList();
static void ItemSelected_cb(lv_event_t * e);

// Check file extension against filter
static bool MatchesFilter(const std::string & filename) {
    if (_fileFilter == FILTER_NONE) return false;
    if (_fileFilter == FILTER_ALL) return true;

    // Convert to lowercase for comparison
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    auto endsWith = [&](const char * ext) {
        size_t extLen = strlen(ext);
        return lower.size() >= extLen && lower.compare(lower.size() - extLen, extLen, ext) == 0;
    };

    switch (_fileFilter) {
        case FILTER_AUDIO:
            return endsWith(".wav") || endsWith(".mp3");
        case FILTER_IMAGE:
            return endsWith(".png") || endsWith(".bmp") || endsWith(".gif");
        case FILTER_CONFIG:
            return endsWith(".txt") || endsWith(".json");
        default:
            return false;
    }
}

// Set the folder title (cropped if too long)
static void SetFolderTitle(const std::string & path) {
    std::string cropped;
    if (path.length() > 30) cropped = path.substr(path.length() - 30);
    else cropped = path;
    lv_subject_copy_string(&Folder, cropped.c_str());
}

// Back button
static void addBackButton(lv_obj_t * list) {
    lv_obj_t * btn = lv_list_add_button(list, LV_SYMBOL_LEFT, "Go back...");
    lv_obj_add_style(btn, &styleFileExplorerEntry, 0);
    lv_obj_add_style(btn, &styleFileExplorerEntrySelected, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn, &styleFileExplorerEntrySelected, LV_STATE_FOCUSED);
    lv_group_add_obj(file_explorer_group, btn);
    lv_obj_add_event_cb(btn, [](lv_event_t * e) {
        size_t pos = currentFolder.path.find_last_of('/');
        std::string newPath = (pos != std::string::npos) ? currentFolder.path.substr(0, pos) : "";
        currentFolder.path = newPath.empty() ? "/crosspad" : newPath;
        pos = currentFolder.path.find_last_of('/');
        currentFolder.name = (pos != std::string::npos) ? currentFolder.path.substr(pos + 1) : currentFolder.path;
        if (IndexDirectory(currentFolder.path)) {
            SetFolderTitle(currentFolder.path);
            UpdateEntriesList();
        }
    }, LV_EVENT_CLICKED, NULL);
}

// ── Main creation ───────────────────────────────────────────────────────────

lv_obj_t * lv_CreateFileExplorer(lv_obj_t * parent, FileFilter fileFilter,
                                  const std::string & startPath,
                                  lv_event_cb_t cb, void * user_data)
{
    if (alreadyOpened) return NULL;
    alreadyOpened = true;
    _fileFilter = fileFilter;
    currentFile = {"", "", false};
    currentFolder = {startPath, "", true};

    // Encoder group
    static lv_group_t * fe_group = lv_group_create();
    file_explorer_group = fe_group;
    lv_indev_set_group(indev_encoderTop, fe_group);

    // Container
    lv_obj_t * cont = lv_obj_create(parent);
    lv_obj_set_size(cont, lv_obj_get_content_width(parent), lv_obj_get_content_height(parent));
    lv_obj_add_style(cont, &styleAppContainer, 0);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    // Subject for folder title
    static char buf1[MAX_PATH_LEN];
    static char buf2[MAX_PATH_LEN];
    lv_subject_init_string(&Folder, buf1, buf2, MAX_PATH_LEN, startPath.c_str());

    // Title label
    lv_obj_t * title = lv_label_create(cont);
    lv_label_bind_text(title, &Folder, NULL);
    lv_obj_add_style(title, &styleTitle, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, lv_obj_get_content_width(lv_screen_active()));

    // Close button — resets current file and triggers load callback
    lv_obj_t * closeBtn = lv_CreateFooterButton(cont, "Close", -50, [](lv_event_t * event) {
        alreadyOpened = false;
        currentFile = {"", "", false};
        lv_obj_send_event(loadButton, LV_EVENT_CLICKED, NULL);
    });

    // Load button
    loadButton = lv_CreateFooterButton(cont, "Load", 50, cb, user_data);

    // Item list
    const int offsetLR = 15;
    const int yOffset = -5;
    const int height = 140;
    const int width = lv_obj_get_content_width(parent) - (2 * offsetLR);

    itemList = lv_list_create(cont);
    lv_group_add_obj(fe_group, itemList);
    lv_obj_add_style(itemList, &styleFileExplorerList, 0);
    lv_obj_add_style(itemList, &styleFileExplorerScrollbar, LV_PART_SCROLLBAR);
    lv_obj_set_size(itemList, width, height);
    lv_obj_align(itemList, LV_ALIGN_CENTER, 0, yOffset);

    lv_group_add_obj(fe_group, closeBtn);
    lv_group_add_obj(fe_group, loadButton);

    IndexDirectory(startPath);
    lv_async_call([](void *) {
        UpdateEntriesList();
    }, NULL);

    return cont;
}

// ── Directory scanning via IFileSystem ──────────────────────────────────────

static bool IndexDirectory(const std::string & path) {
    if (s_beforeScanCb) s_beforeScanCb();

    entries.clear();
    bool ok = crosspad_gui::getFileSystem().listDirectory(path, entries);

    if (s_afterScanCb) s_afterScanCb();
    return ok;
}

// ── Update list UI ──────────────────────────────────────────────────────────

static void UpdateEntriesList() {
    lv_list_cleanup(itemList);
    addBackButton(itemList);

    // Folders first
    for (const auto & item : entries) {
        if (item.isFolder) {
            lv_obj_t * listBtn = lv_list_add_button(itemList, LV_SYMBOL_DIRECTORY, item.name.c_str());
            lv_obj_add_event_cb(listBtn, ItemSelected_cb, LV_EVENT_ALL, NULL);
            lv_obj_add_style(listBtn, &styleFileExplorerEntry, 0);
            lv_obj_add_style(listBtn, &styleFileExplorerEntrySelected, LV_STATE_FOCUSED);
            lv_obj_add_style(listBtn, &styleFileExplorerEntrySelected, LV_STATE_FOCUS_KEY);
            lv_obj_add_flag(listBtn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_obj_add_flag(listBtn, LV_OBJ_FLAG_CLICKABLE);
            lv_group_add_obj(file_explorer_group, listBtn);
        }
    }

    // Then files matching filter
    if (_fileFilter != FILTER_NONE) {
        for (const auto & item : entries) {
            if (!item.isFolder && MatchesFilter(item.name)) {
                const char * symbol = LV_SYMBOL_FILE;
                if (_fileFilter == FILTER_AUDIO) symbol = LV_SYMBOL_AUDIO;
                else if (_fileFilter == FILTER_IMAGE) symbol = LV_SYMBOL_IMAGE;
                else if (_fileFilter == FILTER_CONFIG) symbol = LV_SYMBOL_SETTINGS;

                lv_obj_t * listBtn = lv_list_add_button(itemList, symbol, item.name.c_str());
                lv_obj_add_event_cb(listBtn, ItemSelected_cb, LV_EVENT_ALL, NULL);
                lv_obj_add_style(listBtn, &styleFileExplorerEntry, 0);
                lv_obj_add_style(listBtn, &styleFileExplorerEntrySelected, LV_STATE_FOCUSED);
                lv_obj_add_style(listBtn, &styleFileExplorerEntrySelected, LV_STATE_FOCUS_KEY);
                lv_obj_add_flag(listBtn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
                lv_obj_add_flag(listBtn, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_flag(listBtn, LV_OBJ_FLAG_CLICK_FOCUSABLE);
                lv_group_add_obj(file_explorer_group, listBtn);
            }
        }
    }
}

// ── Item selection callback ─────────────────────────────────────────────────

static void ItemSelected_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * entry = (lv_obj_t *)lv_event_get_target_obj(e);
    const char * name = lv_list_get_button_text(itemList, entry);

    if (code >= LV_EVENT_COVER_CHECK) return;

    // Find item in entries
    crosspad_gui::FileItem * foundItem = nullptr;
    for (auto & item : entries) {
        if (item.name == name) {
            foundItem = &item;
            break;
        }
    }
    if (!foundItem) return;

    switch (code) {
    case LV_EVENT_LONG_PRESSED:
        if (foundItem->isFolder) {
            currentFolder = *foundItem;
            SetFolderTitle(foundItem->path);
            IndexDirectory(foundItem->path);
            UpdateEntriesList();
        } else {
            currentFile = *foundItem;
            if (loadButton) {
                lv_obj_send_event(loadButton, LV_EVENT_CLICKED, NULL);
            }
        }
        break;

    case LV_EVENT_CLICKED:
        if (!foundItem->isFolder) {
            // Preview on click
            if (s_previewCb) {
                s_previewCb(foundItem->path.c_str());
            }
            currentFile = *foundItem;
        } else if (lv_indev_get_type(lv_indev_active()) == LV_INDEV_TYPE_ENCODER) {
            // Encoder click on folder = enter
            currentFolder = *foundItem;
            SetFolderTitle(foundItem->path);
            IndexDirectory(foundItem->path);
            UpdateEntriesList();
        }
        break;

    case LV_EVENT_FOCUSED:
        currentFile = *foundItem;
        if (!foundItem->isFolder && s_previewCb) {
            if (lv_indev_get_type(lv_indev_active()) == LV_INDEV_TYPE_ENCODER) {
                s_previewCb(foundItem->path.c_str());
            }
        }
        break;

    default:
        break;
    }
}

#endif // USE_LVGL
