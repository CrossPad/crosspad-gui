// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings: the Audio and USB categories (USB carries the capture routing rows).

#ifdef USE_LVGL

#include "settings_ui_internal.h"
#include "crosspad-gui/components/ui_toast.h"
#include "crosspad-gui/components/ui_modal.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/stm32/reg_map.h"
#include "crosspad/platform/PlatformServices.hpp"
#include "crosspad/audio/IOutputLevels.hpp"
#include "crosspad/audio/IAudioBuffer.hpp"

#include <cstdio>

using namespace crosspad;

namespace crosspad_gui {

using namespace settings_detail;

// ---- Audio ----

// Audio engine dropdown index storage. Initialized from current settings on
// each panel build. extra_cb writes the resolved kSampleRates[]/kFrameCounts[]
// value back into audioEngine on change. Restart required to take effect.
static uint8_t s_audioEngineSrIdx  = 1;
static uint8_t s_audioEngineFcIdx  = 2;

static void audio_engine_sr_cb(lv_event_t * e) {
    (void)e;
    auto* s = get_settings();
    if (!s) return;
    if (s_audioEngineSrIdx < AudioEngineSettings::kSampleRatesCount) {
        s->audioEngine.sampleRate = AudioEngineSettings::kSampleRates[s_audioEngineSrIdx];
    }
}

// Speaker amplifier master switch. ON does not power the amp permanently — it
// permits Stm32Manager's SpeakerAmpGate to raise REG_CTL_AUDIO_EN while the
// output bus carries signal and to drop it after the quiet hold, so an idle
// device draws no amp current. OFF forces it down immediately.
static void speaker_amp_cb(lv_event_t * e) {
    (void)e;
    auto* s = get_settings();
    if (!s) return;
    // The gate itself reads speakersEnabled from settings on its own tick
    // (Stm32Manager), so the UI only mirrors the field and kills the amp at once
    // when switching off — waiting 100 ms for the poll would be audible.
    s->stm.audioEn = s->masterFX.speakersEnabled ? 1 : 0;
    if (!s->masterFX.speakersEnabled) {
        getGuiPlatform().writeStmConfig8(REG_CTL_AUDIO_EN, 0);
    } else {
        getGuiPlatform().setSpeakerVolume(s->masterFX.outVolumeCodec[0][0]);
    }
}

// The device enumerates one descriptor set at a time — CDC+MIDI or MIDI+UAC2 —
// and only one of them carries audio, so this is a USB profile choice, not an
// audio feature. s_usbModeIdx shadows the persisted bool because the dropdown
// row helper writes a uint8_t index.
static uint8_t s_usbModeIdx = 0;

static void usb_mode_cb(lv_event_t * e) {
    (void)e;
    auto* s = get_settings();
    if (!s) return;
    bool audio = (s_usbModeIdx != 0);
    if (audio == s->usbAudioEnabled) return;
    s->usbAudioEnabled = audio;
    // The row helper saves the shadow index before calling us; the bool it
    // stands for still needs its own store or the choice dies at reboot.
    save_settings();
    getGuiPlatform().setUsbAudioEnabled(audio);
    ui_toast_show(audio ? "USB Audio on — device re-enumerating"
                        : "USB Serial+MIDI on — device re-enumerating",
                  2500, UI_TOAST_INFO);
    /* The page is rebuilt once the dropdown has finished with this event,
     * not from inside it. */
    lv_async_call([](void*) { if (s_current_category >= 0) show_detail(s_current_category); }, nullptr);
}

/* The output volumes back into the hardware from the settings fields: one
 * per physical output where the platform names them (IOutputLevels), the
 * speaker and headphone pair everywhere else. */
void settings_detail::apply_output_volumes(crosspad::CrosspadSettings* s) {
    if (crosspad::IOutputLevels* ol = crosspad::getPlatformServices().outputLevels) {
        for (int i = 0; i < ol->outputs(); ++i) ol->setLevel(i, ol->level(i));
        return;
    }
    auto& plat = crosspad_gui::getGuiPlatform();
    plat.setSpeakerVolume(s->masterFX.outVolumeCodec[0][0]);
    plat.setHeadphoneVolume(s->masterFX.outVolumeCodec[1][0]);
}

/* One slider per IOutputLevels output; the row helper writes a uint8_t, so
 * each has a shadow here and the callback hands on the one that moved. */
static constexpr int kMaxOutputSliders = 8;
static uint8_t s_outputLevels[kMaxOutputSliders];

static void output_level_cb(lv_event_t * e) {
    (void)e;
    crosspad::IOutputLevels* ol = crosspad::getPlatformServices().outputLevels;
    if (!ol) return;
    for (int i = 0; i < ol->outputs() && i < kMaxOutputSliders; ++i) {
        if (s_outputLevels[i] != ol->level(i)) ol->setLevel(i, s_outputLevels[i]);
    }
}

static void speaker_volume_cb(lv_event_t * e) {
    (void)e;
    auto* s = get_settings();
    if (s) crosspad_gui::getGuiPlatform().setSpeakerVolume(s->masterFX.outVolumeCodec[0][0]);
}

static void headphone_volume_cb(lv_event_t * e) {
    (void)e;
    auto* s = get_settings();
    if (s) crosspad_gui::getGuiPlatform().setHeadphoneVolume(s->masterFX.outVolumeCodec[1][0]);
}

static void audio_engine_fc_cb(lv_event_t * e) {
    (void)e;
    auto* s = get_settings();
    if (!s) return;
    if (s_audioEngineFcIdx < AudioEngineSettings::kFrameCountsCount) {
        s->audioEngine.frameCount = AudioEngineSettings::kFrameCounts[s_audioEngineFcIdx];
    }
}

// ---- Audio buffer ----
//
// One choice of block and output queue from the platform (IAudioBuffer),
// applied while playing. Shown as the block in frames and the queue in ms:
// the queue is what a pad waits for after the render picks its note up.
static uint8_t s_audioBufferIdx = 0;
static lv_obj_t * s_audioBufferDd = nullptr;

static void audio_buffer_cb(lv_event_t * e) {
    (void)e;
    crosspad::IAudioBuffer * ab = crosspad::getPlatformServices().audioBuffer;
    if (!ab) return;
    if (!ab->select(s_audioBufferIdx)) {
        ui_toast_show("Buffer not changed", 2000, UI_TOAST_INFO);
        s_audioBufferIdx = ab->current();
        if (s_audioBufferDd) lv_dropdown_set_selected(s_audioBufferDd, s_audioBufferIdx);
    }
}

/// "64 · 4.4 ms" per choice, one a line, into @p out (the dropdown copies it).
static void audio_buffer_options(crosspad::IAudioBuffer & ab, char * out, size_t cap) {
    size_t len = 0;
    out[0] = '\0';
    const uint32_t rate = ab.sampleRate();
    for (uint8_t i = 0; i < ab.choices() && len < cap; ++i) {
        const crosspad::IAudioBuffer::Choice c = ab.choice(i);
        const unsigned tenthsMs = rate ? (unsigned)((c.queueFrames * 10000u + rate / 2) / rate) : 0u;
        const int n = snprintf(out + len, cap - len, "%s%u · %u.%u ms", i ? "\n" : "",
                               (unsigned)c.blockFrames, tenthsMs / 10, tenthsMs % 10);
        if (n > 0) len += (size_t)n;
    }
}

// ---- USB Audio routing ----
//
// Mirrors of the live codec routing, refreshed from the platform on every
// panel build. They are UI shadows, not settings: what a USB host records is
// the Mixer's USB bus, set on ROUTE, and every boot starts there; the other
// sources are for testing and last until the next boot.
static uint8_t s_routePresetIdx  = 0;
static uint8_t s_routeCaptureIdx = 0;
static uint8_t s_routeAdcIdx[2]  = {0, 0};
static uint8_t s_routeDacIdx[2]  = {2, 2};
static bool    s_routeMute[2]    = {false, false};

// The dropdowns themselves, so a change on one row can correct the others in
// place. Rebuilding the panel from a row's own VALUE_CHANGED handler would
// delete the widget mid-dispatch. Rewritten on every build of this panel and
// only ever read from those widgets' own callbacks, so they cannot outlive it.
static lv_obj_t * s_routePresetDd  = nullptr;
static lv_obj_t * s_routeCaptureDd = nullptr;
static lv_obj_t * s_routeAdcDd[2]  = {nullptr, nullptr};

// Order must match the platform's preset table; the trailing entry is the
// read-only "no preset describes this" state.
static const char * kRoutePresetOptions =
    "Microphones\nLine in\nSpeaker bus\nHeadphone bus\nMix (mics + line)\nMixer ROUTE\nCustom";
static constexpr uint8_t kRoutePresetCustomIdx = 6;

// The dropdown widget inside a row built by settings_row_dropdown(): label
// first, dropdown second.
static lv_obj_t * row_dropdown(lv_obj_t * row) {
    return row ? lv_obj_get_child(row, 1) : nullptr;
}

static void dd_select(lv_obj_t * dd, uint8_t idx) {
    if (dd) lv_dropdown_set_selected(dd, idx);
}

// Pull the live routing back into the widgets after any row changed it.
static void route_sync_from_device() {
    AudioRouteState st{};
    if (!getGuiPlatform().getAudioRoute(st)) return;

    s_routePresetIdx  = (st.preset >= kRoutePresetCustomIdx) ? kRoutePresetCustomIdx : st.preset;
    s_routeCaptureIdx = st.captureCodec <= 1 ? st.captureCodec : 0;
    for (int c = 0; c < 2; ++c) {
        s_routeAdcIdx[c] = st.adcInput[c] <= 2 ? st.adcInput[c] : 0;
    }

    dd_select(s_routePresetDd, s_routePresetIdx);
    dd_select(s_routeCaptureDd, s_routeCaptureIdx);
    dd_select(s_routeAdcDd[0], s_routeAdcIdx[0]);
    dd_select(s_routeAdcDd[1], s_routeAdcIdx[1]);
}

// Applies immediately; the row helper has already written and saved the value.
// dB rather than percent: the digital capture tap has none of the codec ADC's
// input gain, and the ~11 dB a good recording level needs is past what a percent
// fits in a byte. The mixer clamps its output, so overdriving distorts.
static void daw_gain_cb(lv_event_t * e) {
    (void)e;
    if (auto* s = get_settings()) {
        crosspad_gui::getGuiPlatform().setDawOutputGain(s->usbAudioGainDb);
    }
}

static void route_preset_cb(lv_event_t * e) {
    (void)e;
    if (s_routePresetIdx >= kRoutePresetCustomIdx) {
        // "Custom" is a report, not a destination — re-applying it would mean
        // picking an arbitrary routing on the user's behalf. Snap back.
        route_sync_from_device();
        return;
    }
    getGuiPlatform().setAudioRoutePreset(s_routePresetIdx);
    route_sync_from_device();
}

static void route_capture_cb(lv_event_t * e) {
    (void)e;
    getGuiPlatform().setAudioCaptureCodec(s_routeCaptureIdx);
    route_sync_from_device();
}

static void route_adc0_cb(lv_event_t * e) {
    (void)e;
    getGuiPlatform().setAudioAdcInput(0, s_routeAdcIdx[0]);
    route_sync_from_device();
}

static void route_adc1_cb(lv_event_t * e) {
    (void)e;
    getGuiPlatform().setAudioAdcInput(1, s_routeAdcIdx[1]);
    route_sync_from_device();
}

// DAC codes are 1-based (1=LINE1 2=LINE2 3=ALL); the dropdown is 0-based.
// Output routing is independent of the capture path, so it never changes the
// preset.
static void route_dac0_cb(lv_event_t * e) {
    (void)e;
    getGuiPlatform().setAudioDacOutput(0, (uint8_t)(s_routeDacIdx[0] + 1));
}

static void route_dac1_cb(lv_event_t * e) {
    (void)e;
    getGuiPlatform().setAudioDacOutput(1, (uint8_t)(s_routeDacIdx[1] + 1));
}

static void route_mute0_cb(lv_event_t * e) {
    (void)e;
    getGuiPlatform().setAudioCodecMute(0, s_routeMute[0]);
}

static void route_mute1_cb(lv_event_t * e) {
    (void)e;
    getGuiPlatform().setAudioCodecMute(1, s_routeMute[1]);
}

// Builds the routing rows from the live state, so the panel always shows what
// the codecs are actually doing — including a routing a HIL script or a host
// tool set over SysEx behind the UI's back.
static void build_audio_route_rows(lv_obj_t * parent, lv_group_t * group) {
    s_routePresetDd = s_routeCaptureDd = s_routeAdcDd[0] = s_routeAdcDd[1] = nullptr;

    AudioRouteState st{};
    if (!getGuiPlatform().getAudioRoute(st)) return;

    s_routePresetIdx  = (st.preset >= kRoutePresetCustomIdx) ? kRoutePresetCustomIdx : st.preset;
    s_routeCaptureIdx = st.captureCodec <= 1 ? st.captureCodec : 0;
    for (int c = 0; c < 2; ++c) {
        s_routeAdcIdx[c] = st.adcInput[c] <= 2 ? st.adcInput[c] : 0;
        s_routeDacIdx[c] = (st.dacOutput[c] >= 1 && st.dacOutput[c] <= 3)
                             ? (uint8_t)(st.dacOutput[c] - 1) : 2;
        s_routeMute[c]   = st.mute[c] != 0;
    }

    if (auto* cfg = get_settings()) {
        settings_row_slider(parent, group, "DAW Output dB", 0, 36,
                            &cfg->usbAudioGainDb, daw_gain_cb);
    }

    settings_section_header(parent, "Routing (advanced, until restart)");

    s_routePresetDd = row_dropdown(
        settings_row_dropdown(parent, group, "Recording Source",
                              kRoutePresetOptions, &s_routePresetIdx, route_preset_cb));

    s_routeCaptureDd = row_dropdown(
        settings_row_dropdown(parent, group, "Capture Codec",
                              "0 - Phones\n1 - Line out",
                              &s_routeCaptureIdx, route_capture_cb));
    s_routeAdcDd[0] = row_dropdown(
        settings_row_dropdown(parent, group, "Codec 0 Input",
                              "Differential\nLINE1 (loop)\nLINE2 (mics)",
                              &s_routeAdcIdx[0], route_adc0_cb));
    s_routeAdcDd[1] = row_dropdown(
        settings_row_dropdown(parent, group, "Codec 1 Input",
                              "Differential\nLINE1 (loop)\nLINE2 (line in)",
                              &s_routeAdcIdx[1], route_adc1_cb));
    settings_row_dropdown(parent, group, "Codec 0 Output",
                          "LINE1\nLINE2\nBoth",
                          &s_routeDacIdx[0], route_dac0_cb);
    settings_row_dropdown(parent, group, "Codec 1 Output",
                          "LINE1\nLINE2\nBoth",
                          &s_routeDacIdx[1], route_dac1_cb);
    settings_row_switch(parent, group, "Mute Codec 0", &s_routeMute[0], route_mute0_cb);
    settings_row_switch(parent, group, "Mute Codec 1", &s_routeMute[1], route_mute1_cb);
}

void settings_detail::cat_build_audio(lv_obj_t * parent, lv_group_t * group) {
    auto* s = get_settings();
    if (!s) return;


    crosspad::IAudioBuffer * ab = crosspad::getPlatformServices().audioBuffer;
    if (ab) {
        /* Live: block and queue together, as one choice the board can run. */
        settings_section_header(parent, "Engine");
        s_audioBufferIdx = ab->current();
        char options[160];
        audio_buffer_options(*ab, options, sizeof(options));
        s_audioBufferDd = row_dropdown(settings_row_dropdown(parent, group, "Buffer", options,
                                                             &s_audioBufferIdx, audio_buffer_cb));
        if (s_audioBufferDd) lv_obj_set_width(s_audioBufferDd, 120);
        settings_section_header(parent, "Engine (restart required)");
    } else {
        settings_section_header(parent, "Engine (restart required)");
    }

    s_audioEngineSrIdx = 1;
    for (uint8_t i = 0; i < AudioEngineSettings::kSampleRatesCount; ++i) {
        if (AudioEngineSettings::kSampleRates[i] == s->audioEngine.sampleRate) {
            s_audioEngineSrIdx = i; break;
        }
    }
    settings_row_dropdown(parent, group, "Sample Rate",
                          "44100\n48000\n96000",
                          &s_audioEngineSrIdx, audio_engine_sr_cb);

    if (!ab) {
        s_audioEngineFcIdx = 2;
        for (uint8_t i = 0; i < AudioEngineSettings::kFrameCountsCount; ++i) {
            if (AudioEngineSettings::kFrameCounts[i] == s->audioEngine.frameCount) {
                s_audioEngineFcIdx = i; break;
            }
        }
        settings_row_dropdown(parent, group, "Frame Count",
                              "32\n48\n64\n128\n256\n512",
                              &s_audioEngineFcIdx, audio_engine_fc_cb);
    }

    settings_section_header(parent, "Output Volume");

    settings_row_switch(parent, group, "Speakers (auto on play)",
                        &s->masterFX.speakersEnabled, speaker_amp_cb);

    // Codec volumes are the only ones on a real signal path (the drawer and the
    // volume overlay drive the same fields). masterFX.inVolume/outVolume and the
    // per-effect bypass flags have no consumer on any platform — they are not
    // exposed here so the panel cannot promise what the firmware does not do.
    if (crosspad::IOutputLevels* ol = crosspad::getPlatformServices().outputLevels) {
        for (int i = 0; i < ol->outputs() && i < kMaxOutputSliders; ++i) {
            s_outputLevels[i] = (uint8_t)ol->level(i);
            settings_row_slider(parent, group, ol->name(i), 0, 100, &s_outputLevels[i], output_level_cb);
        }
        return;
    }

    settings_row_slider(parent, group, "Phones", 0, 100,
                        &s->masterFX.outVolumeCodec[0][0], speaker_volume_cb);

    settings_row_slider(parent, group, "Line out", 0, 100,
                        &s->masterFX.outVolumeCodec[1][0], headphone_volume_cb);
}

// ---- USB ----

static void usb_transfer_confirm_cb(bool confirmed, void * user_data) {
    (void)user_data;
    if (confirmed) getGuiPlatform().requestUsbTransfer();
}

static void usb_transfer_start_cb(lv_event_t * e) {
    (void)e;
    ui_modal_confirm("SD card as USB drive",
                     "The CrossPad restarts and audio stops. Your computer will see "
                     "the SD card as a USB drive.",
                     "Restart", "Cancel", usb_transfer_confirm_cb, nullptr, s_group);
}

void settings_detail::cat_build_usb(lv_obj_t * parent, lv_group_t * group) {
    auto* s = get_settings();
    if (!s) return;

    // Persisted preference: re-applied on the next boot, so this row is both
    // the live switch and the default profile.
    s_usbModeIdx = s->usbAudioEnabled ? 1 : 0;
    lv_obj_t * mode_row = settings_row_dropdown(parent, group, "Mode",
                                                "MIDI + Serial\nMIDI + Audio",
                                                &s_usbModeIdx, usb_mode_cb);
    // The shared 90 px default clips both labels, and a mode you cannot read
    // is the reason this row exists.
    if (lv_obj_t * dd = row_dropdown(mode_row)) lv_obj_set_width(dd, 140);

    settings_row_info(parent, "Active", s->usbAudioEnabled ? "MIDI + Audio (UAC2)"
                                                           : "MIDI + Serial (CDC)");
    // Audio mode has no CDC — five IN endpoints, and MIDI plus UAC2 claim them
    // all — so say what the switch costs before it is thrown.
    settings_row_info(parent, "Serial console / OTA",
                      s->usbAudioEnabled ? "unavailable" : "available");
    // A host asks for the serial profile over SysEx; with this on, the device
    // confirms first (the platform raises the prompt). The row helper persists.
    settings_row_switch(parent, group, "Ask before USB serial", &s->usbSerialGuard, nullptr);

    if (getGuiPlatform().usbTransferSupported()) {
        settings_row_action(parent, group, "SD card as USB drive", "Start",
                            usb_transfer_start_cb);
    }

    // Routing is live in both USB profiles — the codecs do not care which
    // descriptor set is enumerated — so the rows stay usable while the device
    // is still on CDC+MIDI and the choice is ready before the switch.
    // audioRouteSupported() gates them for boards without it.
    if (getGuiPlatform().audioRouteSupported()) {
        build_audio_route_rows(parent, group);
    }
}

} // namespace crosspad_gui

#endif // USE_LVGL
