// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file markdown_view.cpp
 * @brief Simple markdown renderer for LVGL
 */

#include "crosspad-gui/components/markdown_view.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace crosspad_gui {

/* ── Line types ──────────────────────────────────────────────────────── */

enum class LineType { H1, H2, BULLET, TABLE_ROW, BLANK, TEXT };

struct ParsedLine {
    LineType type;
    std::string text;
};

/* ── Parser ──────────────────────────────────────────────────────────── */

static std::vector<ParsedLine> parseMarkdown(const std::string& markdown)
{
    std::vector<ParsedLine> lines;
    std::string line;

    /* Lines as std::getline cut them: on '\n', a last line without one still
     * counts, a trailing '\n' adds no empty line after it. */
    size_t from = 0;
    while (from < markdown.size()) {
        size_t nl = markdown.find('\n', from);
        if (nl == std::string::npos) nl = markdown.size();
        line.assign(markdown, from, nl - from);
        from = nl + 1;

        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty()) {
            lines.push_back({LineType::BLANK, ""});
        } else if (line.rfind("## ", 0) == 0) {
            lines.push_back({LineType::H2, line.substr(3)});
        } else if (line.rfind("# ", 0) == 0) {
            lines.push_back({LineType::H1, line.substr(2)});
        } else if (line.rfind("- ", 0) == 0) {
            lines.push_back({LineType::BULLET, line.substr(2)});
        } else if (line.front() == '|') {
            // Skip separator rows like |---|---|
            bool isSeparator = true;
            for (char c : line) {
                if (c != '|' && c != '-' && c != ' ' && c != ':') {
                    isSeparator = false;
                    break;
                }
            }
            if (!isSeparator) {
                std::vector<std::string> cells;
                size_t pos = 1;
                while (pos < line.size()) {
                    size_t next = line.find('|', pos);
                    if (next == std::string::npos) break;
                    std::string cell = line.substr(pos, next - pos);
                    size_t start = cell.find_first_not_of(' ');
                    size_t end = cell.find_last_not_of(' ');
                    if (start != std::string::npos)
                        cell = cell.substr(start, end - start + 1);
                    else
                        cell = "";
                    cells.push_back(cell);
                    pos = next + 1;
                }
                std::string formatted;
                bool isGrid = (cells.size() >= 4 && cells[0].size() <= 2);
                for (size_t i = 0; i < cells.size(); i++) {
                    if (i > 0) formatted += isGrid ? "   " : " - ";
                    formatted += cells[i];
                }
                lines.push_back({LineType::TABLE_ROW, formatted});
            }
        } else {
            lines.push_back({LineType::TEXT, line});
        }
    }
    return lines;
}

/* ── Renderer ────────────────────────────────────────────────────────── */

static const lv_color_t COLOR_H1     = lv_color_hex(0x00E676);
static const lv_color_t COLOR_H2     = lv_color_hex(0x66BB6A);
static const lv_color_t COLOR_BULLET = lv_color_hex(0xAAAAAA);
static const lv_color_t COLOR_TABLE  = lv_color_hex(0xCCCCCC);
static const lv_color_t COLOR_TEXT   = lv_color_hex(0xDDDDDD);

static void renderLine(lv_obj_t* parent, const ParsedLine& line)
{
    if (line.type == LineType::BLANK) {
        lv_obj_t* spacer = lv_obj_create(parent);
        lv_obj_set_size(spacer, LV_PCT(100), 4);
        lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(spacer, 0, 0);
        lv_obj_set_style_pad_all(spacer, 0, 0);
        lv_obj_remove_flag(spacer, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        return;
    }

    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);

    switch (line.type) {
        case LineType::H1:
            lv_label_set_text(lbl, line.text.c_str());
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lbl, COLOR_H1, 0);
            break;
        case LineType::H2:
            lv_label_set_text(lbl, line.text.c_str());
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(lbl, COLOR_H2, 0);
            break;
        case LineType::BULLET: {
            std::string bullet = std::string("\xE2\x80\xA2 ") + line.text;
            lv_label_set_text(lbl, bullet.c_str());
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(lbl, COLOR_BULLET, 0);
            break;
        }
        case LineType::TABLE_ROW:
            lv_label_set_text(lbl, line.text.c_str());
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(lbl, COLOR_TABLE, 0);
            break;
        case LineType::TEXT:
            lv_label_set_text(lbl, line.text.c_str());
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(lbl, COLOR_TEXT, 0);
            break;
        default:
            break;
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */

void markdown_render(lv_obj_t* parent, const std::string& markdown)
{
    lv_obj_clean(parent);
    auto lines = parseMarkdown(markdown);
    for (auto& line : lines) {
        renderLine(parent, line);
    }
}

namespace {

struct FileCloser {
    void operator()(FILE* f) const { std::fclose(f); }
};

} // namespace

void markdown_render_file(lv_obj_t* parent, const std::string& filepath)
{
    std::unique_ptr<FILE, FileCloser> file(std::fopen(filepath.c_str(), "r"));
    if (!file) {
        lv_obj_clean(parent);
        lv_obj_t* lbl = lv_label_create(parent);
        lv_label_set_text(lbl, "File not found");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF4444), 0);
        return;
    }
    std::string text;
    constexpr size_t kChunk = 512;
    for (;;) {
        const size_t had = text.size();
        text.resize(had + kChunk);
        const size_t n = std::fread(&text[had], 1, kChunk, file.get());
        text.resize(had + n);
        if (n < kChunk) break;
    }
    file.reset();
    markdown_render(parent, text);
}

} // namespace crosspad_gui
