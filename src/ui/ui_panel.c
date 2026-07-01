#include "ui/ui_internal.h"

#include <SDL3/SDL_error.h>
#include <string.h>

#include "core/engine.h"
#include "ui/text_layout.h"

typedef bool (*UI_WrappedLineFn)(void* user, const char* text, uint16_t length);

static uint32_t UI_PanelFlags(const UIPanel* panel) {
    uint32_t flags = 0;

    if (panel->active) {
        flags |= UIPANEL_FLAG_ACTIVE;
    }

    return flags;
}

static uint16_t UI_ClampCharsPerLine(uint32_t chars_per_line) {
    if (chars_per_line == 0) {
        return 1;
    }
    if (chars_per_line > MAX_CHARS_PER_TEXT_LINE) {
        return MAX_CHARS_PER_TEXT_LINE;
    }

    return (uint16_t)chars_per_line;
}

static UIPanel* UI_GetActivePanel(UI* ui, uint32_t panel_id, const char* function_name) {
    if (panel_id >= MAX_UI_PANELS || !ui->panels[panel_id].active) {
        SDL_SetError("%s received inactive panel", function_name);
        return NULL;
    }

    return &ui->panels[panel_id];
}

static uint16_t UI_GetPanelCharsPerLine(const UIPanel* panel) {
    uint16_t inset = (uint16_t)((panel->style.padding + panel->style.border) * 2u);
    uint16_t content_width = panel->width > inset ? (uint16_t)(panel->width - inset) : 1;

    return UI_ClampCharsPerLine(content_width / TEXT_LAYOUT_GLYPH_W);
}

static void UI_UpdatePanelHeight(UIPanel* panel) {
    uint16_t inset = (uint16_t)((panel->style.padding + panel->style.border) * 2u);
    uint16_t visible_lines = panel->line_count > 0 ? panel->line_count : 1;
    uint16_t line_gaps = visible_lines > 0 ? (uint16_t)(visible_lines - 1u) : 0;

    panel->height = (uint16_t)(
        inset +
        visible_lines * TEXT_LAYOUT_GLYPH_H +
        line_gaps * UI_TEXT_LINE_GAP
    );
}

static void UI_UpdatePanelMeta(UI* ui, UIPanel* panel) {
    uint8_t id = panel->id;

    ui->panel_meta_buffer[id][UIPANEL_META_X] = (uint32_t)panel->x;
    ui->panel_meta_buffer[id][UIPANEL_META_Y] = (uint32_t)panel->y;
    ui->panel_meta_buffer[id][UIPANEL_META_WIDTH] = panel->width;
    ui->panel_meta_buffer[id][UIPANEL_META_HEIGHT] = panel->height;
    ui->panel_meta_buffer[id][UIPANEL_META_PADDING] = panel->style.padding;
    ui->panel_meta_buffer[id][UIPANEL_META_BORDER] = panel->style.border;
    ui->panel_meta_buffer[id][UIPANEL_META_FLAGS] = UI_PanelFlags(panel);
    ui->panel_meta_buffer[id][UIPANEL_META_FILL_COLOR] = panel->style.fill_color;
    ui->panel_meta_buffer[id][UIPANEL_META_BORDER_COLOR] = panel->style.border_color;
    ui->panel_meta_buffer[id][UIPANEL_META_RESERVED] = 0;
}

static void UI_UpdatePanelTextLinePlacements(UI* ui, UIPanel* panel) {
    int inset = panel->style.padding + panel->style.border;
    uint32_t line_id = panel->first_line_id;
    uint16_t line_index = 0;

    while (line_id != UI_INVALID_ID) {
        UITextLine* line = &ui->text_lines[line_id];
        line->x = (int16_t)(panel->x + inset);
        line->y = (int16_t)(panel->y + inset + (int)line_index * (TEXT_LAYOUT_GLYPH_H + UI_TEXT_LINE_GAP));
        line->style.text_color = panel->style.text_color;
        UI_UpdateTextLineMeta(ui, line);
        line->dirty = true;

        line_id = line->next_id;
        line_index++;
    }

    ui->text_meta_dirty = true;
}

static bool UI_EmitWrappedLine(
    UI_WrappedLineFn sink,
    void* sink_user,
    const char* text,
    size_t length,
    uint16_t* line_count
) {
    if (length > MAX_CHARS_PER_TEXT_LINE) {
        SDL_SetError("Wrapped line exceeds MAX_CHARS_PER_TEXT_LINE");
        return false;
    }
    if (*line_count == UINT16_MAX) {
        SDL_SetError("Too many wrapped text lines");
        return false;
    }

    if (sink && !sink(sink_user, text, (uint16_t)length)) {
        return false;
    }

    (*line_count)++;
    return true;
}

static bool UI_WrapText(
    const char* text,
    uint16_t chars_per_line,
    UI_WrappedLineFn sink,
    void* sink_user,
    uint16_t* out_line_count
) {
    const char* p = text ? text : "";
    char line[MAX_CHARS_PER_TEXT_LINE];
    uint16_t line_len = 0;
    uint16_t line_count = 0;

    chars_per_line = UI_ClampCharsPerLine(chars_per_line);

    while (*p) {
        if (*p == '\r') {
            p++;
            continue;
        }

        if (*p == '\n') {
            if (!UI_EmitWrappedLine(sink, sink_user, line, line_len, &line_count)) {
                return false;
            }
            line_len = 0;
            p++;
            continue;
        }

        while (*p == ' ' || *p == '\t') {
            p++;
        }

        if (*p == '\0') {
            break;
        }
        if (*p == '\n') {
            continue;
        }

        const char* word = p;
        size_t word_len = 0;
        while (p[word_len] &&
               p[word_len] != ' ' &&
               p[word_len] != '\t' &&
               p[word_len] != '\r' &&
               p[word_len] != '\n')
        {
            word_len++;
        }
        p += word_len;

        while (word_len > 0) {
            if (word_len > chars_per_line) {
                if (line_len > 0) {
                    if (!UI_EmitWrappedLine(sink, sink_user, line, line_len, &line_count)) {
                        return false;
                    }
                    line_len = 0;
                }

                if (!UI_EmitWrappedLine(sink, sink_user, word, chars_per_line, &line_count)) {
                    return false;
                }

                word += chars_per_line;
                word_len -= chars_per_line;
                continue;
            }

            uint16_t separator = line_len > 0 ? 1 : 0;
            if (line_len == 0 || line_len + separator + word_len <= chars_per_line) {
                if (separator) {
                    line[line_len++] = ' ';
                }

                memcpy(&line[line_len], word, word_len);
                line_len = (uint16_t)(line_len + word_len);
                word_len = 0;
            } else {
                if (!UI_EmitWrappedLine(sink, sink_user, line, line_len, &line_count)) {
                    return false;
                }
                line_len = 0;
            }
        }
    }

    if (line_len > 0) {
        if (!UI_EmitWrappedLine(sink, sink_user, line, line_len, &line_count)) {
            return false;
        }
    }

    if (out_line_count) {
        *out_line_count = line_count;
    }

    return true;
}

static void UI_FreePanelTextLines(UI* ui, UIPanel* panel) {
    uint32_t line_id = panel->first_line_id;

    while (line_id != UI_INVALID_ID) {
        UITextLine* line = &ui->text_lines[line_id];
        uint32_t next_id = line->next_id;
        UI_FreeTextLine(ui, line_id);
        line_id = next_id;
    }

    panel->first_line_id = UI_INVALID_ID;
    panel->line_count = 0;
}

typedef struct UI_PanelWrapSink {
    UI* ui;
    UIPanel* panel;
    uint32_t last_line_id;
} UI_PanelWrapSink;

static bool UI_AddPanelWrappedLine(void* user, const char* text, uint16_t length) {
    UI_PanelWrapSink* sink = user;
    UIPanel* panel = sink->panel;
    uint16_t line_index = panel->line_count;
    int inset = panel->style.padding + panel->style.border;

    UIStyle line_style = UI_DEFAULT_TEXT_LINE_STYLE;
    line_style.text_color = panel->style.text_color;
    line_style.fill_color = UI_COLOR_RGBA(0, 0, 0, 0);
    line_style.border_color = UI_COLOR_RGBA(0, 0, 0, 0);

    uint32_t line_id = UI_AllocTextLine(
        sink->ui,
        panel->x + inset,
        panel->y + inset + (int)line_index * (TEXT_LAYOUT_GLYPH_H + UI_TEXT_LINE_GAP),
        &line_style,
        panel->id
    );
    if (line_id == UI_INVALID_ID) {
        SDL_SetError("No free text lines for panel text");
        return false;
    }

    UITextLine* line = &sink->ui->text_lines[line_id];
    if (!UI_WriteTextLineChars(sink->ui, line, text, length)) {
        UI_FreeTextLine(sink->ui, line_id);
        return false;
    }

    if (panel->first_line_id == UI_INVALID_ID) {
        panel->first_line_id = line_id;
    } else {
        sink->ui->text_lines[sink->last_line_id].next_id = line_id;
    }

    sink->last_line_id = line_id;
    panel->line_count++;
    return true;
}

static bool UI_RebuildPanelTextLines(UI* ui, UIPanel* panel, const char* text) {
    uint16_t chars_per_line = UI_GetPanelCharsPerLine(panel);
    uint16_t new_line_count = 0;

    if (!UI_WrapText(text, chars_per_line, NULL, NULL, &new_line_count)) {
        return false;
    }

    if (new_line_count > ui->text_line_free_count + panel->line_count) {
        SDL_SetError("Not enough free text lines for panel text");
        return false;
    }

    UI_FreePanelTextLines(ui, panel);

    UI_PanelWrapSink sink = {
        .ui = ui,
        .panel = panel,
        .last_line_id = UI_INVALID_ID
    };

    if (!UI_WrapText(text, chars_per_line, UI_AddPanelWrappedLine, &sink, NULL)) {
        UI_FreePanelTextLines(ui, panel);
        UI_UpdatePanelHeight(panel);
        UI_UpdatePanelMeta(ui, panel);
        ui->panel_dirty = true;
        return false;
    }

    UI_UpdatePanelHeight(panel);
    UI_UpdatePanelMeta(ui, panel);
    panel->dirty = true;
    ui->panel_dirty = true;

    return true;
}

uint32_t UI_CreatePanel(UI* ui, int x, int y, uint16_t width) {
    if (ui->panel_free_count == 0) {
        return UI_INVALID_ID;
    }

    uint32_t id = ui->panel_free_stack[--ui->panel_free_count];
    UIPanel* panel = &ui->panels[id];

    panel->id = (uint8_t)id;
    panel->x = (int16_t)x;
    panel->y = (int16_t)y;
    panel->width = width;
    panel->first_line_id = UI_INVALID_ID;
    panel->line_count = 0;
    panel->text_length = 0;
    panel->text[0] = '\0';
    panel->style = UI_DEFAULT_PANEL_STYLE;
    panel->active = true;
    panel->dirty = false;

    UI_UpdatePanelHeight(panel);
    UI_UpdatePanelMeta(ui, panel);

    ui->panel_dirty = true;
    return id;
}

void UI_DestroyPanel(UI* ui, uint32_t id) {
    if (id >= MAX_UI_PANELS) {
        return;
    }

    UIPanel* panel = &ui->panels[id];
    if (!panel->active) {
        return;
    }

    UI_FreePanelTextLines(ui, panel);

    panel->x = 0;
    panel->y = 0;
    panel->width = 0;
    panel->height = 0;
    panel->first_line_id = UI_INVALID_ID;
    panel->line_count = 0;
    panel->text_length = 0;
    panel->text[0] = '\0';
    panel->style = UI_DEFAULT_PANEL_STYLE;
    panel->active = false;
    panel->dirty = false;

    memset(ui->panel_meta_buffer[id], 0, sizeof(ui->panel_meta_buffer[id]));
    ui->panel_free_stack[ui->panel_free_count++] = id;
    ui->panel_dirty = true;
}

bool UI_SetPanelText(EngineContext* ctx, uint32_t panel_id, const char* text) {
    UI* ui = &ctx->ui;
    UIPanel* panel = UI_GetActivePanel(ui, panel_id, "UI_SetPanelText");
    const char* safe_text = text ? text : "";

    if (!panel) {
        return false;
    }

    size_t length = strlen(safe_text);
    if (length > MAX_CHARS_PER_PANEL_TEXT) {
        SDL_SetError("Panel text exceeds MAX_CHARS_PER_PANEL_TEXT");
        return false;
    }

    if (!UI_RebuildPanelTextLines(ui, panel, safe_text)) {
        return false;
    }

    memcpy(panel->text, safe_text, length);
    panel->text[length] = '\0';
    panel->text_length = (uint16_t)length;

    return true;
}

bool UI_SetPanelPosition(EngineContext* ctx, uint32_t panel_id, int x, int y) {
    UI* ui = &ctx->ui;
    UIPanel* panel = UI_GetActivePanel(ui, panel_id, "UI_SetPanelPosition");

    if (!panel) {
        return false;
    }

    panel->x = (int16_t)x;
    panel->y = (int16_t)y;
    UI_UpdatePanelTextLinePlacements(ui, panel);
    UI_UpdatePanelMeta(ui, panel);
    panel->dirty = true;
    ui->panel_dirty = true;

    return true;
}

bool UI_SetPanelStyle(EngineContext* ctx, uint32_t panel_id, const UIStyle* style) {
    UI* ui = &ctx->ui;
    UIPanel* panel = UI_GetActivePanel(ui, panel_id, "UI_SetPanelStyle");

    if (!panel) {
        return false;
    }
    if (!style) {
        SDL_SetError("UI_SetPanelStyle received NULL style");
        return false;
    }

    UIStyle old_style = panel->style;
    panel->style = *style;

    if (!UI_RebuildPanelTextLines(ui, panel, panel->text)) {
        panel->style = old_style;
        return false;
    }

    return true;
}

bool UI_SetPanelWidth(EngineContext* ctx, uint32_t panel_id, uint16_t width) {
    UI* ui = &ctx->ui;
    UIPanel* panel = UI_GetActivePanel(ui, panel_id, "UI_SetPanelWidth");

    if (!panel) {
        return false;
    }

    uint16_t old_width = panel->width;
    panel->width = width;

    if (!UI_RebuildPanelTextLines(ui, panel, panel->text)) {
        panel->width = old_width;
        return false;
    }

    return true;
}
