#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_TEXTBOXES 256
#define MAX_CHARS_PER_TEXTBOX 256

typedef struct EngineContext EngineContext;
typedef struct Font Font;

typedef struct TextBox {
    uint8_t id;
    Font* font;

    int x;
    int y;

    uint32_t length;
    bool active;
} TextBox;

typedef struct UI {
    TextBox textboxes[MAX_TEXTBOXES];

    uint16_t text_buffer[MAX_TEXTBOXES][MAX_CHARS_PER_TEXTBOX];

    uint32_t free_stack[MAX_TEXTBOXES];
    uint32_t free_count;
} UI;

void UI_Init(EngineContext* ctx);

uint32_t UI_CreateTextBox(UI* ui, Font* font, int x, int y);
void UI_DestroyTextBox(UI* ui, uint32_t id);

#endif // UI_H
