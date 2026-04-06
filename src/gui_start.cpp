#ifdef USE_LVGL

#include "crosspad-gui/gui_start.h"
#include "crosspad-gui/bootstrap.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/components/app_launcher.h"
#include "crosspad-gui/components/app_orchestrator.h"
#include "crosspad-gui/platform/IGuiPlatform.h"

namespace crosspad_gui {

GuiStartResult gui_start(lv_obj_t* screen, const GuiStartConfig& config) {
    GuiStartResult result{};

    if (!config.platform || !config.filesystem) {
        return result;
    }

    // Helper lambdas for optional LVGL mutex
    auto lock = [&]() {
        if (config.lvgl_mutex.lock) config.lvgl_mutex.lock(3000);
    };
    auto unlock = [&]() {
        if (config.lvgl_mutex.unlock) config.lvgl_mutex.unlock();
    };

    // 1. Bootstrap platform adapters (no LVGL calls — safe outside lock)
    bootstrap(config.platform, config.filesystem);

    // 2-7: All LVGL operations wrapped in optional mutex
    lock();

    // 2. Initialize LVGL styles
    initStyles();

    // 3. Register built-in apps (power off dialog)
    launcher_register_power_off();

    // 4. Configure and init orchestrator
    auto& orch = AppOrchestrator::getInstance();
    orch.init(config.orchestrator);

    // 5. Populate apps from registry (container is set later in start())
    orch.populateApps(nullptr);

    // 6. Build main screen config — fill in encoder from platform if not set
    auto msConfig = config.main_screen;
    if (!msConfig.encoder) {
        msConfig.encoder = config.platform->getNavigationEncoder();
    }
    msConfig.on_select = AppOrchestrator::onAppSelected;
    msConfig.on_close  = AppOrchestrator::onPopupClose;

    // 7. Load main screen (creates status bar, app container, launcher grid)
    auto msResult = orch.loadMainScreen(screen, msConfig);

    unlock();

    result.status_bar    = msResult.status_bar;
    result.app_container = msResult.app_container;
    result.encoder_group = msResult.encoder_group;
    result.launcher_grid = msResult.launcher_grid;
    result.success       = true;

    return result;
}

} // namespace crosspad_gui

#endif // USE_LVGL
