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
    memset(ctx->ui.text_buffer, 0, sizeof(ctx->ui.text_buffer));
    memset(ctx->ui.text_meta_buffer, 0, sizeof(ctx->ui.text_meta_buffer));
    memset(ctx->ui.panel_meta_buffer, 0, sizeof(ctx->ui.panel_meta_buffer));

    ctx->ui.text_line_free_count = MAX_UI_TEXT_LINES;
    ctx->ui.panel_free_count = MAX_UI_PANELS;
    ctx->ui.text_dirty = true;
    ctx->ui.text_meta_dirty = true;
    ctx->ui.panel_dirty = true;

    for (uint32_t i = 0; i < MAX_UI_TEXT_LINES; i++) {
        ctx->ui.text_lines[i].id = (uint8_t)i;
        ctx->ui.text_lines[i].x = 0;
        ctx->ui.text_lines[i].y = 0;
        ctx->ui.text_lines[i].width = 0;
        ctx->ui.text_lines[i].height = 0;
        ctx->ui.text_lines[i].length = 0;
        ctx->ui.text_lines[i].owner_panel = UI_INVALID_ID;
        ctx->ui.text_lines[i].next_id = UI_INVALID_ID;
        ctx->ui.text_lines[i].style = UI_DEFAULT_TEXT_LINE_STYLE;
        ctx->ui.text_lines[i].active = false;
        ctx->ui.text_lines[i].dirty = false;

        ctx->ui.text_line_free_stack[i] = MAX_UI_TEXT_LINES - 1u - i;
    }

    for (uint32_t i = 0; i < MAX_UI_PANELS; i++) {
        ctx->ui.panels[i].id = (uint8_t)i;
        ctx->ui.panels[i].x = 0;
        ctx->ui.panels[i].y = 0;
        ctx->ui.panels[i].width = 0;
        ctx->ui.panels[i].height = 0;
        ctx->ui.panels[i].first_line_id = UI_INVALID_ID;
        ctx->ui.panels[i].line_count = 0;
        ctx->ui.panels[i].text_length = 0;
        ctx->ui.panels[i].text[0] = '\0';
        ctx->ui.panels[i].style = UI_DEFAULT_PANEL_STYLE;
        ctx->ui.panels[i].active = false;
        ctx->ui.panels[i].dirty = false;

        ctx->ui.panel_free_stack[i] = MAX_UI_PANELS - 1u - i;
    }
}
