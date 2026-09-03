// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file scope_view.h
 * @brief Oscilloscope view of a live audio stream.
 *
 * A canvas that redraws itself from a pull source ~30 times a second. The
 * source is anything that can hand over the newest N mono samples — a
 * crosspad::ScopeTap on an engine is the intended one — plus the fundamental
 * of what is playing, which sizes the sweep (crosspad::scopeWindowSamples).
 * The sweep starts at a rising zero-crossing (crosspad::scopeTrigger), so the
 * wave stands still instead of sliding sideways at the beat frequency between
 * the note and the refresh.
 *
 * Everything the timer needs is allocated at create(): the sample buffer and
 * the canvas pixels come from the heap once and are freed with the object.
 *
 * ```cpp
 * lv_obj_t* scope = create_scope_view(parent, 120, 60);
 * scope_view_set_source(scope, &pullFromEngine, engine);
 * // ...
 * scope_view_clear_source(scope);   // before the engine goes away
 * ```
 */

#ifdef USE_LVGL
#include "lvgl.h"
#include <cstdint>

namespace crosspad_gui {

/**
 * @brief Pull the newest @p n mono samples, oldest first.
 *
 * @param ctx        whatever was handed to scope_view_set_source().
 * @param out        buffer for @p n floats, full scale ±1.
 * @param hz         fundamental of the note being played, 0 if none is known.
 * @param sampleRate rate of the samples in @p out, 0 if unknown.
 * @return how many samples were actually written; 0 draws a flat line.
 */
using ScopeSource = uint32_t (*)(void* ctx, float* out, uint32_t n,
                                 float* hz, uint32_t* sampleRate);

} // namespace crosspad_gui

/// Create a scope of @p w × @p h pixels. Sourceless (flat) until it is given one.
lv_obj_t* create_scope_view(lv_obj_t* parent, int32_t w, int32_t h);

/// Point the scope at a stream. Null @p src parks it, same as clearing.
void scope_view_set_source(lv_obj_t* scope, crosspad_gui::ScopeSource src, void* ctx);

/// Stop pulling. Call before whatever `ctx` refers to is destroyed.
void scope_view_clear_source(lv_obj_t* scope);

#endif // USE_LVGL
