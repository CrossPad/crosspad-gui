#ifdef USE_LVGL

#include "crosspad-gui/components/file_explorer.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad-gui/widgets/footer_button.h"
#include "crosspad-gui/platform/IFileSystem.h"

#include <vector>
#include <algorithm>
#include <cstring>

#include "crosspad-gui/platform/IGuiPlatform.h"

static lv_group_t * file_explorer_group = nullptr;
static lv_obj_t * itemList = NULL;
static lv_obj_t * loadButton = NULL;

#define MAX_PATH_LEN 128

static crosspad_gui::FileItem currentFile;
// Set only on an explicit click. Reaching Load with the encoder walks focus
// back over the other entries, so focus-follows-selection would swap the pick
// for whatever it passed last.
static bool currentFilePinned = false;
static crosspad_gui::FileItem currentFolder;
static lv_subject_t Folder;
static std::vector<crosspad_gui::FileItem> entries;

static bool alreadyOpened = false;
static FileFilter _fileFilter;
static lv_group_t * s_prevEncoderGroup = nullptr;
static lv_indev_t * s_prevEncoderIndev = nullptr;

// Optional callbacks
static file_preview_cb_t s_previewCb = nullptr;
static file_scan_lifecycle_cb_t s_beforeScanCb = nullptr;
static file_scan_lifecycle_cb_t s_afterScanCb = nullptr;

// Debounced preview: FOCUSED fires per detent, and previewing each one floods
// the SD/audio path. The timer fires once the selection rests.
static lv_timer_t * s_previewTimer = nullptr;
static char         s_previewPath[MAX_PATH_LEN] = {0};

static void previewTimer_cb(lv_timer_t * t) {
    lv_timer_pause(t); // one-shot per arming
    if (s_previewPath[0] && s_previewCb) {
        s_previewCb(s_previewPath);
    }
}

static void SchedulePreview(const char * path) {
    strncpy(s_previewPath, path, MAX_PATH_LEN - 1);
    s_previewPath[MAX_PATH_LEN - 1] = '\0';
    if (!s_previewTimer) s_previewTimer = lv_timer_create(previewTimer_cb, 220, nullptr);
    lv_timer_reset(s_previewTimer); // restart the window on each new focus
    lv_timer_resume(s_previewTimer);
}

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

static bool MatchesFilter(const std::string & filename) {
    if (_fileFilter == FILTER_NONE) return false;
    if (_fileFilter == FILTER_ALL) return true;

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
    currentFilePinned = false;
    currentFolder = {startPath, "", true};

    // Remember the previous binding, to restore it on close.
    static lv_group_t * fe_group = lv_group_create();
    file_explorer_group = fe_group;
    lv_indev_t * nav = crosspad_gui::getGuiPlatform().getNavigationEncoder();
    if (nav) {
        s_prevEncoderIndev = nav;
        s_prevEncoderGroup = lv_indev_get_group(nav);
        lv_indev_set_group(nav, fe_group);
    }

    // PCT(100) ties to the parent's post-layout box rather than a stale
    // content_height; SCROLLABLE off so children cannot push past the bounds.
    lv_obj_t * cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_add_style(cont, &styleAppContainer, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_update_layout(cont);

    // Every deletion path: Load, Close, app exit, async wipe.
    lv_obj_add_event_cb(cont, [](lv_event_t *) {
        alreadyOpened = false;
        itemList = NULL;
        loadButton = NULL;
        entries.clear();
        if (s_previewTimer) {
            lv_timer_delete(s_previewTimer);
            s_previewTimer = nullptr;
        }
        if (s_prevEncoderIndev) {
            lv_indev_set_group(s_prevEncoderIndev, s_prevEncoderGroup);
            s_prevEncoderIndev = nullptr;
            s_prevEncoderGroup = nullptr;
        }
    }, LV_EVENT_DELETE, NULL);

    // Subject for folder title
    static char buf1[MAX_PATH_LEN];
    static char buf2[MAX_PATH_LEN];
    lv_subject_init_string(&Folder, buf1, buf2, MAX_PATH_LEN, startPath.c_str());

    // Explicit Y positions for 320x240. The list stops above the footer, so a
    // long listing cannot displace the buttons.
    const int titleY    = 4;
    const int listY     = 30;
    const int listH     = 140;       
    const int listLR    = 8;
    const int contW     = lv_obj_get_content_width(cont);
    const int btnW      = 90;
    const int btnGap    = 12;
    const int btnXOff   = (btnW + btnGap) / 2;

    // Absolute layout: no flex re-flow.
    lv_obj_set_layout(cont, LV_LAYOUT_NONE);
    const int titleW = contW - 2 * listLR;
    const int listW  = contW - 2 * listLR;
    const int titleX = (contW - titleW) / 2;
    const int listX  = (contW - listW) / 2;

    // Title label
    lv_obj_t * title = lv_label_create(cont);
    lv_label_bind_text(title, &Folder, NULL);
    lv_obj_add_style(title, &styleTitle, 0);
    lv_obj_set_size(title, titleW, LV_SIZE_CONTENT);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(title, titleX, titleY);

    itemList = lv_list_create(cont);
    lv_group_add_obj(fe_group, itemList);
    lv_obj_add_style(itemList, &styleFileExplorerList, 0);
    lv_obj_add_style(itemList, &styleFileExplorerScrollbar, LV_PART_SCROLLBAR);
    lv_obj_set_size(itemList, listW, listH);
    lv_obj_set_pos(itemList, listX, listY);

    lv_obj_t * closeBtn = lv_CreateFooterButton(cont, "Close", -btnXOff, [](lv_event_t * event) {
        alreadyOpened = false;
        currentFile = {"", "", false};
        currentFilePinned = false;
        lv_obj_send_event(loadButton, LV_EVENT_CLICKED, NULL);
    });
    lv_obj_set_width(closeBtn, btnW);

    loadButton = lv_CreateFooterButton(cont, "Load", btnXOff, cb, user_data);
    lv_obj_set_width(loadButton, btnW);

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

// The path MUST be taken by value: `item` lives in `entries`, which
// IndexDirectory() clears before refilling. By reference, every entry's path
// was built from a destroyed string — names survived through SSO, heap paths
// came back empty, and the picked sample was stored as "".
static void EnterFolder(const crosspad_gui::FileItem & item)
{
    const std::string folderPath = item.path;
    currentFolder = item;
    currentFilePinned = false;   // new listing, old pick is gone
    SetFolderTitle(folderPath);
    IndexDirectory(folderPath);
    UpdateEntriesList();
}

// ── Item selection callback ─────────────────────────────────────────────────

static void ItemSelected_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * entry = (lv_obj_t *)lv_event_get_target_obj(e);
    const char * name = lv_list_get_button_text(itemList, entry);

    if (code >= LV_EVENT_COVER_CHECK) return;

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
            EnterFolder(*foundItem);
        } else {
            currentFile = *foundItem;
            currentFilePinned = true;
            if (loadButton) {
                lv_obj_send_event(loadButton, LV_EVENT_CLICKED, NULL);
            }
        }
        break;

    case LV_EVENT_CLICKED:
        if (!foundItem->isFolder) {
            if (s_previewCb) {
                s_previewCb(foundItem->path.c_str());
            }
            currentFile = *foundItem;
            currentFilePinned = true;
        } else if (lv_indev_get_type(lv_indev_active()) == LV_INDEV_TYPE_ENCODER) {
            EnterFolder(*foundItem);
        }
        break;

    case LV_EVENT_FOCUSED:
        // Folders never become the selection, or focusing one on the way to
        // Load leaves the button silently doing nothing.
        if (!foundItem->isFolder && !currentFilePinned) {
            currentFile = *foundItem;
        }
        if (!foundItem->isFolder && s_previewCb) {
            if (lv_indev_get_type(lv_indev_active()) == LV_INDEV_TYPE_ENCODER) {
                SchedulePreview(foundItem->path.c_str());
            }
        }
        break;

    default:
        break;
    }
}

#endif // USE_LVGL
