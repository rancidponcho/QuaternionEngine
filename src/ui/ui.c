#include "ui/ui_internal.h"

#include <string.h>

#include "core/engine.h"

const UIStyle UI_DEFAULT_TEXT_LINE_STYLE = {
    .padding = 0,
    .border = 0,
    .text_color = UI_COLOR_RGBA(255, 255, 255, 255),
    .fill_color = UI_COLOR_RGBA(0, 0, 0, 160),
    .border_color = UI_COLOR_RGBA(255, 255, 255, 255)
};

const UIStyle UI_DEFAULT_PANEL_STYLE = {
    .padding = 4,
    .border = 1,
    .text_color = UI_COLOR_RGBA(255, 255, 255, 255),
    .fill_color = UI_COLOR_RGBA(0, 0, 0, 176),
    .border_color = UI_COLOR_RGBA(255, 255, 255, 255)
};

void UI_Init(EngineContext* ctx) {
    UI* ui = &ctx->ui;
    memset(ui, 0, sizeof(*ui));

    ui->text_line_free_count = MAX_UI_TEXT_LINES;
    ui->panel_free_count = MAX_UI_PANELS;
    ui->text_dirty = true;
    ui->text_meta_dirty = true;
    ui->panel_dirty = true;

    for (uint32_t i = 0; i < MAX_UI_TEXT_LINES; i++) {
        ui->text_lines[i].id = (uint8_t)i;
        ui->text_lines[i].owner_panel = UI_INVALID_ID;
        ui->text_lines[i].next_id = UI_INVALID_ID;
        ui->text_lines[i].style = UI_DEFAULT_TEXT_LINE_STYLE;
        ui->text_line_free_stack[i] = MAX_UI_TEXT_LINES - 1u - i;
    }

    for (uint32_t i = 0; i < MAX_UI_PANELS; i++) {
        ui->panels[i].id = (uint8_t)i;
        ui->panels[i].first_line_id = UI_INVALID_ID;
        ui->panels[i].style = UI_DEFAULT_PANEL_STYLE;
        ui->panel_free_stack[i] = MAX_UI_PANELS - 1u - i;
    }
}
