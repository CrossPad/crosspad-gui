// SPDX-License-Identifier: GPL-3.0-or-later
// ui_tree_probe() and ui_tree_json() on a headless display: what is visible,
// the top layer after the screen, label text from descendants, the JSON escapes.
#include <catch2/catch_test_macros.hpp>

#include <ArduinoJson.h>

#include "crosspad-gui/components/ui_probe.h"
#include "gui_test_platform.h"

#include <cstring>
#include <string>

using namespace crosspad_gui;

namespace {

lv_obj_t* button(lv_obj_t* parent, const char* text, int32_t x, int32_t y)
{
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, text);
    return b;
}

uint16_t probe(UiNode* out, uint16_t max)
{
    lv_obj_update_layout(lv_screen_active());
    lv_obj_update_layout(lv_layer_top());
    return ui_tree_probe(out, max);
}

}  // namespace

TEST_CASE("ui_probe: visible only", "[ui_probe]")
{
    guitest::GuiFixture f;
    lv_obj_clean(lv_layer_top());
    lv_obj_t* scr = f.screen;
    button(scr, "Play", 10, 10);
    lv_obj_add_flag(button(scr, "Ghost", 10, 60), LV_OBJ_FLAG_HIDDEN);
    button(scr, "Far", 400, 10);

    UiNode out[32];
    const uint16_t n = probe(out, 32);

    const UiNode* play = nullptr;
    for (uint16_t i = 0; i < n; ++i) {
        if (out[i].text && !strcmp(out[i].text, "Play") && !strcmp(out[i].cls, "button")) play = &out[i];
        if (out[i].text) REQUIRE(strcmp(out[i].text, "Ghost") != 0);
        REQUIRE(out[i].x < 320);
    }
    REQUIRE(play != nullptr);
    CHECK(play->interactive);
}

TEST_CASE("ui_probe: top layer", "[ui_probe]")
{
    guitest::GuiFixture f;
    lv_obj_clean(lv_layer_top());
    button(f.screen, "Play", 10, 10);
    button(lv_layer_top(), "OK", 100, 100);

    UiNode out[32];
    const uint16_t n = probe(out, 32);

    int play = -1, ok = -1;
    for (uint16_t i = 0; i < n; ++i) {
        if (out[i].text && !strcmp(out[i].text, "Play") && play < 0) play = i;
        if (out[i].text && !strcmp(out[i].text, "OK") && ok < 0) ok = i;
    }
    REQUIRE(play >= 0);
    REQUIRE(ok > play);
}

TEST_CASE("ui_probe: text from descendant", "[ui_probe]")
{
    guitest::GuiFixture f;
    lv_obj_clean(lv_layer_top());
    lv_obj_t* scr = f.screen;

    lv_obj_t* near = lv_button_create(scr);
    lv_obj_t* box = lv_obj_create(near);
    lv_label_set_text(lv_label_create(box), "Deep");

    lv_obj_t* far = lv_button_create(scr);
    lv_obj_set_pos(far, 0, 100);
    lv_obj_t* p = far;
    for (int i = 0; i < 3; ++i) p = lv_obj_create(p);
    lv_label_set_text(lv_label_create(p), "TooDeep");

    UiNode out[32];
    const uint16_t n = probe(out, 32);

    const UiNode* b = nullptr;
    const UiNode* deep = nullptr;
    for (uint16_t i = 0; i < n; ++i) {
        if (!strcmp(out[i].cls, "button")) {
            if (out[i].y < 50) b = &out[i];
            else deep = &out[i];
        }
    }
    REQUIRE(b != nullptr);
    REQUIRE(deep != nullptr);
    REQUIRE(b->text != nullptr);
    CHECK(std::string(b->text) == "Deep");
    CHECK(deep->text == nullptr);
}

TEST_CASE("ui_probe: max respected", "[ui_probe]")
{
    guitest::GuiFixture f;
    lv_obj_clean(lv_layer_top());
    for (int i = 0; i < 5; ++i) button(f.screen, "x", 0, i * 30);
    UiNode out[2];
    CHECK(probe(out, 2) == 2);
}

TEST_CASE("ui_probe: ui_tree_json escapes", "[ui_probe]")
{
    const char* text = "a\"b\\c\nŻółć";
    UiNode nd{"label", 1, 2, 3, 4, -1, text, true, false, false, true};
    std::string json;
    ui_tree_json(&nd, 1, "Sam\"pler", json);

    JsonDocument doc;
    REQUIRE(deserializeJson(doc, json) == DeserializationError::Ok);
    CHECK(std::string(doc["running"].as<const char*>()) == "Sam\"pler");
    CHECK(doc["lcd"]["w"].as<int>() == 320);
    JsonObject o = doc["nodes"][0];
    CHECK(std::string(o["text"].as<const char*>()) == text);
    CHECK(o["parent"].as<int>() == -1);
    CHECK(o["interactive"].as<bool>());
    CHECK(o["focused"].as<bool>());
    CHECK_FALSE(o["checked"].as<bool>());

    nd.text = nullptr;
    json.clear();
    ui_tree_json(&nd, 1, nullptr, json);
    REQUIRE(deserializeJson(doc, json) == DeserializationError::Ok);
    CHECK(doc["nodes"][0]["text"].isNull());
}
