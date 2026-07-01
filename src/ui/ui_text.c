#include "ui/ui_internal.h"

#include <SDL3/SDL_error.h>
#include <string.h>

#include "ui/text_layout.h"

static uint32_t UI_TextLineFlags(const UITextLine* line) {
    uint32_t flags = 0;

    if (line->active) {
        flags |= UITEXTLINE_FLAG_ACTIVE;
    }

    return flags;
}

static void UI_UpdateTextLineLayout(UITextLine* line) {
    TextLayoutMetrics metrics = TextLayout_MeasureSingleLine(
        line->length,
        line->style.padding,
        line->style.border
    );

    line->width = metrics.box_width;
    line->height = metrics.box_height;
}

void UI_UpdateTextLineMeta(UI* ui, UITextLine* line) {
    uint8_t id = line->id;

    ui->text_meta_buffer[id][UITEXTLINE_META_LENGTH] = line->length;
    ui->text_meta_buffer[id][UITEXTLINE_META_X] = (uint32_t)line->x;
    ui->text_meta_buffer[id][UITEXTLINE_META_Y] = (uint32_t)line->y;
    ui->text_meta_buffer[id][UITEXTLINE_META_WIDTH] = line->width;
    ui->text_meta_buffer[id][UITEXTLINE_META_HEIGHT] = line->height;
    ui->text_meta_buffer[id][UITEXTLINE_META_PADDING] = line->style.padding;
    ui->text_meta_buffer[id][UITEXTLINE_META_BORDER] = line->style.border;
    ui->text_meta_buffer[id][UITEXTLINE_META_FLAGS] = UI_TextLineFlags(line);
    ui->text_meta_buffer[id][UITEXTLINE_META_TEXT_COLOR] = line->style.text_color;
    ui->text_meta_buffer[id][UITEXTLINE_META_FILL_COLOR] = line->style.fill_color;
    ui->text_meta_buffer[id][UITEXTLINE_META_BORDER_COLOR] = line->style.border_color;
    ui->text_meta_buffer[id][UITEXTLINE_META_RESERVED] = 0;
}

static void UI_ResetTextLine(UI* ui, uint32_t id) {
    UITextLine* line = &ui->text_lines[id];

    line->length = 0;
    line->width = 0;
    line->height = 0;
    line->owner_panel = UI_INVALID_ID;
    line->next_id = UI_INVALID_ID;
    line->active = false;
    line->dirty = false;
    line->style = UI_DEFAULT_TEXT_LINE_STYLE;

    memset(ui->text_buffer[id], 0, sizeof(ui->text_buffer[id]));
    memset(ui->text_meta_buffer[id], 0, sizeof(ui->text_meta_buffer[id]));
}

void UI_FreeTextLine(UI* ui, uint32_t id) {
    if (id >= MAX_UI_TEXT_LINES || !ui->text_lines[id].active) {
        return;
    }

    UI_ResetTextLine(ui, id);
    ui->text_line_free_stack[ui->text_line_free_count++] = id;
    ui->text_dirty = true;
    ui->text_meta_dirty = true;
}

uint32_t UI_AllocTextLine(UI* ui, int x, int y, const UIStyle* style, uint32_t owner_panel) {
    if (ui->text_line_free_count == 0) {
        return UI_INVALID_ID;
    }

    uint32_t id = ui->text_line_free_stack[--ui->text_line_free_count];
    UITextLine* line = &ui->text_lines[id];

    line->id = (uint8_t)id;
    line->x = (int16_t)x;
    line->y = (int16_t)y;
    line->length = 0;
    line->owner_panel = owner_panel;
    line->next_id = UI_INVALID_ID;
    line->style = style ? *style : UI_DEFAULT_TEXT_LINE_STYLE;
    line->active = true;
    line->dirty = false;

    UI_UpdateTextLineLayout(line);
    UI_UpdateTextLineMeta(ui, line);

    ui->text_meta_dirty = true;

    return id;
}

bool UI_WriteTextLineChars(UI* ui, UITextLine* line, const char* text, uint16_t length) {
    if (length > MAX_CHARS_PER_TEXT_LINE) {
        SDL_SetError("Text exceeds MAX_CHARS_PER_TEXT_LINE");
        return false;
    }

    uint8_t id = line->id;

    for (uint16_t i = 0; i < length; i++) {
        ui->text_buffer[id][i] = (uint32_t)(unsigned char)text[i];
    }

    for (uint16_t i = length; i < MAX_CHARS_PER_TEXT_LINE; i++) {
        ui->text_buffer[id][i] = 0;
    }

    line->length = length;
    UI_UpdateTextLineLayout(line);
    UI_UpdateTextLineMeta(ui, line);
    line->dirty = true;

    ui->text_dirty = true;
    ui->text_meta_dirty = true;

    return true;
}
