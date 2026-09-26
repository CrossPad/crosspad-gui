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

    auto lock = [&]() {
        if (config.lvgl_mutex.lock) config.lvgl_mutex.lock(3000);
    };
    auto unlock = [&]() {
        if (config.lvgl_mutex.unlock) config.lvgl_mutex.unlock();
    };

    // Platform adapters first: no LVGL calls, so this is safe outside the lock.
    bootstrap(config.platform, config.filesystem);

    lock();

    initStyles();

    // Built-in apps (the power-off dialog).
    launcher_register_power_off();

    auto& orch = AppOrchestrator::getInstance();
    orch.init(config.orchestrator);

    // From the registry; the container is set later, in start().
    orch.populateApps(nullptr);

    // Fill the encoder in from the platform if the caller left it unset.
    auto msConfig = config.main_screen;
    if (!msConfig.encoder) {
        msConfig.encoder = config.platform->getNavigationEncoder();
    }
    msConfig.on_select = AppOrchestrator::onAppSelected;
    msConfig.on_close  = AppOrchestrator::onPopupClose;

    auto msResult = orch.loadMainScreen(screen, msConfig);

    unlock();

    result.status_bar    = msResult.status_bar;
    result.app_container = msResult.app_container;
    result.encoder_group = msResult.encoder_group;
    result.launcher      = msResult.launcher;
    result.success       = true;

    return result;
}

} // namespace crosspad_gui

#endif // USE_LVGL
