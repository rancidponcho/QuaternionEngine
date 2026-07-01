#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>

#define UI_INVALID_ID UINT32_MAX

#define UI_COLOR_RGBA(r, g, b, a) \
    ((((uint32_t)(r) & 0xffu) << 24) | \
     (((uint32_t)(g) & 0xffu) << 16) | \
     (((uint32_t)(b) & 0xffu) << 8)  | \
     ((uint32_t)(a) & 0xffu))

typedef struct EngineContext EngineContext;
typedef struct UI UI;

typedef struct UIStyle {
    uint8_t padding;
    uint8_t border;
    uint32_t text_color;
    uint32_t fill_color;
    uint32_t border_color;
} UIStyle;

void UI_Init(EngineContext* ctx);

uint32_t UI_CreatePanel(UI* ui, int x, int y, uint16_t width);
void UI_DestroyPanel(UI* ui, uint32_t id);
bool UI_SetPanelText(EngineContext* ctx, uint32_t panel_id, const char* text);
bool UI_SetPanelPosition(EngineContext* ctx, uint32_t panel_id, int x, int y);
bool UI_SetPanelStyle(EngineContext* ctx, uint32_t panel_id, const UIStyle* style);
bool UI_SetPanelWidth(EngineContext* ctx, uint32_t panel_id, uint16_t width);

#endif // UI_H
