#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include "ui/ui.h"

#include <stdbool.h>
#include <stdint.h>

#define MAX_UI_TEXT_LINES 256
#define MAX_CHARS_PER_TEXT_LINE 256
#define MAX_CHARS_PER_PANEL_TEXT 2048
#define MAX_UI_PANELS 64
#define UI_TEXT_LINE_GAP 1

#define UITEXTLINE_META_LENGTH 0
#define UITEXTLINE_META_X 1
#define UITEXTLINE_META_Y 2
#define UITEXTLINE_META_WIDTH 3
#define UITEXTLINE_META_HEIGHT 4
#define UITEXTLINE_META_PADDING 5
#define UITEXTLINE_META_BORDER 6
#define UITEXTLINE_META_FLAGS 7
#define UITEXTLINE_META_TEXT_COLOR 8
#define UITEXTLINE_META_FILL_COLOR 9
#define UITEXTLINE_META_BORDER_COLOR 10
#define UITEXTLINE_META_RESERVED 11
#define UITEXTLINE_META_STRIDE 12
#define UITEXTLINE_FLAG_ACTIVE 0x1u

#define UIPANEL_META_X 0
#define UIPANEL_META_Y 1
#define UIPANEL_META_WIDTH 2
#define UIPANEL_META_HEIGHT 3
#define UIPANEL_META_PADDING 4
#define UIPANEL_META_BORDER 5
#define UIPANEL_META_FLAGS 6
#define UIPANEL_META_FILL_COLOR 7
#define UIPANEL_META_BORDER_COLOR 8
#define UIPANEL_META_RESERVED 9
#define UIPANEL_META_STRIDE 10
#define UIPANEL_FLAG_ACTIVE 0x1u

typedef struct UITextLine {
    uint8_t id;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t length;
    uint32_t owner_panel;
    uint32_t next_id;
    UIStyle style;
    bool active;
    bool dirty;
} UITextLine;

typedef struct UIPanel {
    uint8_t id;
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t first_line_id;
    uint16_t line_count;
    uint16_t text_length;
    char text[MAX_CHARS_PER_PANEL_TEXT + 1];
    UIStyle style;
    bool active;
    bool dirty;
} UIPanel;

struct UI {
    UITextLine text_lines[MAX_UI_TEXT_LINES];
    UIPanel panels[MAX_UI_PANELS];

    uint32_t text_buffer[MAX_UI_TEXT_LINES][MAX_CHARS_PER_TEXT_LINE];
    uint32_t text_meta_buffer[MAX_UI_TEXT_LINES][UITEXTLINE_META_STRIDE];
    uint32_t panel_meta_buffer[MAX_UI_PANELS][UIPANEL_META_STRIDE];

    uint32_t text_line_free_stack[MAX_UI_TEXT_LINES];
    uint32_t text_line_free_count;

    uint32_t panel_free_stack[MAX_UI_PANELS];
    uint32_t panel_free_count;

    bool text_dirty;
    bool text_meta_dirty;
    bool panel_dirty;
};

extern const UIStyle UI_DEFAULT_TEXT_LINE_STYLE;
extern const UIStyle UI_DEFAULT_PANEL_STYLE;

uint32_t UI_AllocTextLine(UI* ui, int x, int y, const UIStyle* style, uint32_t owner_panel);
void UI_FreeTextLine(UI* ui, uint32_t id);
bool UI_WriteTextLineChars(UI* ui, UITextLine* line, const char* text, uint16_t length);
void UI_UpdateTextLineMeta(UI* ui, UITextLine* line);

#endif // UI_INTERNAL_H
