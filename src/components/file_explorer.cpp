#ifdef USE_LVGL

#include "crosspad-gui/components/file_explorer.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad-gui/widgets/footer_button.h"
#include "crosspad-gui/platform/IFileSystem.h"
#include "crosspad-gui/components/power_gesture.h"

#include <vector>
#include <algorithm>
#include <cstring>

#include "crosspad-gui/platform/IGuiPlatform.h"

static lv_group_t * file_explorer_group = nullptr;
/* Two panes: the folders of the current directory on the left, and whatever
 * the highlighted one holds on the right. For picking a sample out of a kit
 * that is the whole job -- the old single list made you walk into a folder and
 * back out again to see what was in the next one. */
static lv_obj_t * itemList = NULL;      // left: folders (or files, in a leaf)
static lv_obj_t * fileList = NULL;      // right: what the highlighted folder holds
static lv_obj_t * loadButton = NULL;
static lv_obj_t * closeButton = NULL;

/// Rows carry (pane << 16 | index); this index means "the row that goes back".
static constexpr size_t kBackRow = 0xFFFF;
static int s_activePane = 0;            // which pane the encoder is walking

#define MAX_PATH_LEN 128

static crosspad_gui::FileItem currentFile;
// Set only on an explicit click. Reaching Load with the encoder walks focus
// back over the other entries, so focus-follows-selection would swap the pick
// for whatever it passed last.
static bool currentFilePinned = false;
static crosspad_gui::FileItem currentFolder;
static lv_subject_t Folder;
static std::vector<crosspad_gui::FileItem> entries;      // the left pane's listing
static std::vector<crosspad_gui::FileItem> subEntries;   // the right pane's listing
static std::string subFolderPath;                        // what the right pane shows

static bool alreadyOpened = false;
// The folder the explorer was opened on: the back gesture climbs out of
// subfolders down to it, and closes the explorer once there.
static std::string s_rootPath;
static uint32_t s_backToken = 0;
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

/* The right pane is a directory listing, and a listing is a card read on the
 * LVGL thread. Debounced for the same reason as the preview: spinning past ten
 * folders must not queue ten scans, only the one you stopped on. */
static lv_timer_t * s_subTimer = nullptr;
static char         s_subWanted[MAX_PATH_LEN] = {0};

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

static void UpdateFileList();
static void FocusPane(int pane);

static void SubScanTimer_cb(lv_timer_t * t) {
    lv_timer_pause(t);
    if (!s_subWanted[0]) return;
    subFolderPath = s_subWanted;
    subEntries.clear();
    crosspad_gui::getFileSystem().listDirectory(subFolderPath, subEntries);
    UpdateFileList();
}

static void ScheduleSubScan(const std::string & path) {
    snprintf(s_subWanted, sizeof(s_subWanted), "%s", path.c_str());
    if (!s_subTimer) s_subTimer = lv_timer_create(SubScanTimer_cb, 180, nullptr);
    lv_timer_reset(s_subTimer);
    lv_timer_resume(s_subTimer);
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
static void UpdateFileList();
static void FocusPane(int pane);
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

// One level up. Above the start folder there is still /crosspad, which the
// in-list "Go back..." entry keeps reaching; the power gesture stops at the
// start folder and closes the explorer instead (see explorerBack_cb).
static void GoUpOneLevel() {
    size_t pos = currentFolder.path.find_last_of('/');
    std::string newPath = (pos != std::string::npos) ? currentFolder.path.substr(0, pos) : "";
    currentFolder.path = newPath.empty() ? "/crosspad" : newPath;
    pos = currentFolder.path.find_last_of('/');
    currentFolder.name = (pos != std::string::npos) ? currentFolder.path.substr(pos + 1) : currentFolder.path;
    currentFilePinned = false;   // new listing, old pick is gone
    if (IndexDirectory(currentFolder.path)) {
        SetFolderTitle(currentFolder.path);
        UpdateEntriesList();
    }
}

// Close with nothing picked. Routed through the Load button because the owner
// hung its teardown (delete the explorer, take the encoder back, resume its
// timers) on that callback; a bare delete here would skip all of it.
static void CloseExplorer() {
    alreadyOpened = false;
    currentFile = {"", "", false};
    currentFilePinned = false;
    if (loadButton) lv_obj_send_event(loadButton, LV_EVENT_CLICKED, NULL);
}

/* The power button's short click, while the explorer is up: one folder level
 * per click, then out of the explorer — never straight out of the app, which
 * is what the framework fallback would have done from three levels deep. */
static bool explorerBack_cb(void *) {
    if (!alreadyOpened) return false;
    /* Out of the contents pane before out of the folder: the right pane is a
     * level of its own as far as the back button is concerned. */
    if (s_activePane == 1) {
        FocusPane(0);
        return true;
    }
    /* Climb only while the listing is *below* the folder the explorer was
     * opened on. The in-list "Go back..." can walk above it, all the way to
     * /crosspad where it clamps — and a rule of "not the start folder, so go
     * up" would then climb forever and never close the explorer. */
    const bool below = currentFolder.path.size() > s_rootPath.size() &&
                       currentFolder.path.compare(0, s_rootPath.size(), s_rootPath) == 0;
    if (below) {
        GoUpOneLevel();
        return true;
    }
    CloseExplorer();
    return true;
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
    s_rootPath = startPath;

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
        fileList = NULL;
        loadButton = NULL;
        closeButton = NULL;
        s_activePane = 0;
        entries.clear();
        subEntries.clear();
        subFolderPath.clear();
        if (s_previewTimer) {
            lv_timer_delete(s_previewTimer);
            s_previewTimer = nullptr;
        }
        if (s_subTimer) {
            lv_timer_delete(s_subTimer);
            s_subTimer = nullptr;
        }
        crosspad_gui::ui_back_remove(s_backToken);
        s_backToken = 0;
        if (s_prevEncoderIndev) {
            // Guarded: by the time an app teardown reaches this DELETE, the
            // group the explorer was opened over is usually already freed.
            lv_indev_restore_group(s_prevEncoderIndev, s_prevEncoderGroup);
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
    const int listY     = 28;
    const int listH     = 144;       
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

    /* Two panes of equal width. The lists are not in the encoder group -- their
     * rows are, and only those of the pane being walked (see FocusPane). */
    const int paneGap = 6;
    const int paneW   = (listW - paneGap) / 2;
    auto makePane = [&](int x) {
        lv_obj_t * list = lv_list_create(cont);
        lv_obj_add_style(list, &styleFileExplorerList, 0);
        lv_obj_add_style(list, &styleFileExplorerScrollbar, LV_PART_SCROLLBAR);
        lv_obj_set_size(list, paneW, listH);
        lv_obj_set_pos(list, x, listY);
        /* The theme draws the border after the children, a second pass over the
         * whole list on every refresh, for one pixel of black on a dark
         * screen. */
        lv_obj_set_style_border_post(list, false, 0);
        lv_obj_set_style_pad_all(list, 0, 0);
        return list;
    };
    itemList = makePane(listX);
    fileList = makePane(listX + paneW + paneGap);

    closeButton = lv_CreateFooterButton(cont, "Close", -btnXOff, [](lv_event_t *) {
        CloseExplorer();
    });
    lv_obj_set_width(closeButton, btnW);

    loadButton = lv_CreateFooterButton(cont, "Load", btnXOff, cb, user_data);
    lv_obj_set_width(loadButton, btnW);

    s_backToken = crosspad_gui::ui_back_push(explorerBack_cb, nullptr);

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

/// Row styling and wiring, shared by both panes. The tag is (pane << 16 | idx).
static void AttachRow(lv_obj_t * btn, int pane, size_t idx) {
    void * tag = (void *)(intptr_t)(((size_t)pane << 16) | (idx & 0xFFFF));
    lv_obj_add_event_cb(btn, ItemSelected_cb, LV_EVENT_CLICKED, tag);
    lv_obj_add_event_cb(btn, ItemSelected_cb, LV_EVENT_FOCUSED, tag);
    lv_obj_add_event_cb(btn, ItemSelected_cb, LV_EVENT_LONG_PRESSED, tag);
    /* The default theme grows a button on press and rings it on focus, both
     * animated (LV_THEME_DEFAULT_GROW, LV_THEME_DEFAULT_TRANSITION_TIME): a
     * transformed or outlined row stops covering its own area, which put
     * everything underneath back into the redraw.
     *
     * Neutralised with local values, never removed. Taking those styles out of
     * the object wholesale -- and blanking the transition pointer along with
     * them -- is what crashed the browser on entry. */
    lv_obj_set_style_transform_width(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUSED);
    /* Opaque, in the list's own colour: a transparent row covers nothing, so
     * every pixel of it was drawn through the list's background and the
     * container behind that. Same picture, one fill instead of three. */
    lv_obj_set_style_bg_color(btn, CROSSPAD_THEME_PRIMARY, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    /* Small rows, and the cost follows them down twice over: a shorter row is
     * fewer pixels to repaint when the selection lands on it, and a 10 px glyph
     * is about half the coverage of a 14 px one -- text is what this list
     * spends its time drawing. */
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_10, 0);
    lv_obj_set_style_pad_ver(btn, 2, 0);
    lv_obj_set_style_pad_hor(btn, 4, 0);
    lv_obj_set_style_pad_column(btn, 4, 0);
    lv_obj_set_height(btn, 18);
    lv_obj_add_style(btn, &styleFileExplorerEntry, 0);
    lv_obj_add_style(btn, &styleFileExplorerEntrySelected, LV_STATE_FOCUSED);
    lv_obj_add_style(btn, &styleFileExplorerEntrySelected, LV_STATE_FOCUS_KEY);
    /* lv_button_create() sets SCROLL_ON_FOCUS itself and LVGL's version of it
     * animates the scroll -- every frame of that animation repaints the whole
     * list. Off; the focus handler brings the row into view in one step. */
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICK_FOCUSABLE);
}

/// Hand the encoder the rows of one pane, plus the two footer buttons.
///
/// Only the active pane's rows are in the group: with seventy kits on the left,
/// walking through all of them to reach the contents on the right would be no
/// better than the single list this replaced.
static void FocusPane(int pane) {
    s_activePane = pane;
    if (!file_explorer_group) return;

    lv_group_remove_all_objs(file_explorer_group);
    lv_obj_t * list = (pane == 0) ? itemList : fileList;
    uint32_t rows = list ? lv_obj_get_child_count(list) : 0;
    for (uint32_t i = 0; i < rows; ++i) {
        lv_group_add_obj(file_explorer_group, lv_obj_get_child(list, i));
    }
    if (closeButton) lv_group_add_obj(file_explorer_group, closeButton);
    if (loadButton)  lv_group_add_obj(file_explorer_group, loadButton);

    /* Past the row that only goes back: arriving on the contents pane should
     * put the encoder on the first thing in it. */
    const uint32_t landing = (pane == 1 && rows > 1) ? 1 : 0;
    if (rows > 0) lv_group_focus_obj(lv_obj_get_child(list, landing));
    else if (closeButton) lv_group_focus_obj(closeButton);
}

static const char * FilterSymbol() {
    switch (_fileFilter) {
        case FILTER_AUDIO:  return LV_SYMBOL_AUDIO;
        case FILTER_IMAGE:  return LV_SYMBOL_IMAGE;
        case FILTER_CONFIG: return LV_SYMBOL_SETTINGS;
        default:            return LV_SYMBOL_FILE;
    }
}

/// The right pane: what the folder highlighted on the left holds.
static void UpdateFileList() {
    if (!fileList) return;
    lv_list_cleanup(fileList);

    if (subEntries.empty()) {
        if (!subFolderPath.empty()) {
            lv_obj_t * empty = lv_list_add_text(fileList, "empty");
            lv_obj_set_style_text_font(empty, &lv_font_montserrat_10, 0);
        }
        if (s_activePane == 1) FocusPane(0);   // nothing here to walk
        return;
    }

    /* A way back to the folders that does not need the power button. */
    lv_obj_t * back = lv_list_add_button(fileList, LV_SYMBOL_LEFT, "Folders");
    AttachRow(back, 1, kBackRow);

    for (size_t i = 0; i < subEntries.size(); ++i) {
        if (!subEntries[i].isFolder) continue;
        lv_obj_t * row = lv_list_add_button(fileList, LV_SYMBOL_DIRECTORY,
                                            subEntries[i].name.c_str());
        AttachRow(row, 1, i);
    }
    if (_fileFilter != FILTER_NONE) {
        const char * symbol = FilterSymbol();
        for (size_t i = 0; i < subEntries.size(); ++i) {
            if (subEntries[i].isFolder || !MatchesFilter(subEntries[i].name)) continue;
            lv_obj_t * row = lv_list_add_button(fileList, symbol, subEntries[i].name.c_str());
            AttachRow(row, 1, i);
        }
    }
    if (s_activePane == 1) FocusPane(1);   // the rows it was walking are gone
}

/// The left pane: the folders here, or the files when there are no folders --
/// a kit's own directory is a leaf and has nothing but samples in it.
static void UpdateEntriesList() {
    if (!itemList) return;
    lv_list_cleanup(itemList);
    subEntries.clear();
    subFolderPath.clear();
    if (fileList) lv_list_cleanup(fileList);

    lv_obj_t * back = lv_list_add_button(itemList, LV_SYMBOL_LEFT, "Go back...");
    AttachRow(back, 0, kBackRow);

    bool anyFolder = false;
    for (const auto & item : entries) {
        if (item.isFolder) { anyFolder = true; break; }
    }

    lv_obj_t * first = nullptr;
    if (anyFolder) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (!entries[i].isFolder) continue;
            lv_obj_t * row = lv_list_add_button(itemList, LV_SYMBOL_DIRECTORY,
                                                entries[i].name.c_str());
            AttachRow(row, 0, i);
            if (!first) first = row;
        }
    } else if (_fileFilter != FILTER_NONE) {
        const char * symbol = FilterSymbol();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].isFolder || !MatchesFilter(entries[i].name)) continue;
            lv_obj_t * row = lv_list_add_button(itemList, symbol, entries[i].name.c_str());
            AttachRow(row, 0, i);
            if (!first) first = row;
        }
    }

    FocusPane(0);
    /* On the first folder rather than on "Go back...", so the right pane has
     * something in it the moment the browser opens. */
    if (first) lv_group_focus_obj(first);
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
    const lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * entry = (lv_obj_t *)lv_event_get_target_obj(e);

    const size_t tag  = (size_t)(intptr_t)lv_event_get_user_data(e);
    const int    pane = (int)(tag >> 16);
    const size_t idx  = tag & 0xFFFF;

    if (code == LV_EVENT_FOCUSED) {
        /* By the page, not by the row. Every scroll repaints the whole pane,
         * and a row-at-a-time scroll pays that on every click of the encoder.
         * Landing the row that left the view at the far edge leaves a
         * screenful of margin in the direction of travel, so the next few
         * clicks only restyle two rows. */
        lv_obj_t * list = (pane == 0) ? itemList : fileList;
        if (list) {
            lv_area_t row, box;
            lv_obj_get_coords(entry, &row);
            lv_obj_get_coords(list, &box);
            if (row.y1 < box.y1)      lv_obj_scroll_by(list, 0, box.y2 - row.y2, LV_ANIM_OFF);
            else if (row.y2 > box.y2) lv_obj_scroll_by(list, 0, box.y1 - row.y1, LV_ANIM_OFF);
        }
    }

    /* The two rows that are not entries at all. */
    if (idx == kBackRow) {
        if (code == LV_EVENT_CLICKED) {
            if (pane == 0) GoUpOneLevel();
            else           FocusPane(0);
        }
        return;
    }

    std::vector<crosspad_gui::FileItem> & vec = (pane == 0) ? entries : subEntries;
    if (idx >= vec.size()) return;
    crosspad_gui::FileItem * item = &vec[idx];

    switch (code) {
    case LV_EVENT_LONG_PRESSED:
        if (item->isFolder) {
            EnterFolder(*item);
        } else {
            currentFile = *item;
            currentFilePinned = true;
            if (loadButton) lv_obj_send_event(loadButton, LV_EVENT_CLICKED, NULL);
        }
        break;

    case LV_EVENT_CLICKED:
        if (item->isFolder) {
            /* On the left a folder is not entered, it is looked into: the
             * click hands the encoder to its contents, already listed on the
             * right. Entering it -- making it the current directory -- is what
             * a click on the right does, which is how a kit with subfolders
             * stays reachable. */
            if (pane == 0) {
                /* Pressed before the debounce got round to it: the pane has to
                 * hold this folder's contents now, not in 180 ms. */
                if (subFolderPath != item->path) {
                    subFolderPath = item->path;
                    subEntries.clear();
                    crosspad_gui::getFileSystem().listDirectory(subFolderPath, subEntries);
                    UpdateFileList();
                }
                FocusPane(1);
            } else {
                EnterFolder(*item);
            }
        } else {
            if (s_previewCb) s_previewCb(item->path.c_str());
            currentFile = *item;
            currentFilePinned = true;
        }
        break;

    case LV_EVENT_FOCUSED:
        if (item->isFolder) {
            /* The right pane follows the highlight on the left. */
            if (pane == 0) ScheduleSubScan(item->path);
            break;
        }
        // Folders never become the selection, or focusing one on the way to
        // Load leaves the button silently doing nothing.
        if (!currentFilePinned) currentFile = *item;
        if (s_previewCb && lv_indev_get_type(lv_indev_active()) == LV_INDEV_TYPE_ENCODER) {
            SchedulePreview(item->path.c_str());
        }
        break;

    default:
        break;
    }
}

#endif // USE_LVGL
